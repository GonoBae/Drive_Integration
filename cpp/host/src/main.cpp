#include <chrono>
#include <functional>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/json.hpp>

#include "config.hpp"
#include "physics/vehicle_physics.hpp"
#include "publisher/zmq_publisher.hpp"
#include "websocket/ws_server.hpp"

// Protobuf generated
#include "vehicle.pb.h"

namespace net  = boost::asio;
namespace json = boost::json;

// VehicleState → Protobuf 직렬화
static std::string serialize_state(const VehicleState& s) {
    simcore::EntityStatePacket packet;
    auto* e = packet.add_entities();
    e->set_entity_id(s.entity_id);
    e->set_timestamp(s.timestamp);
    e->set_lat(s.lat);
    e->set_lon(s.lon);
    e->set_alt(s.alt);
    e->set_heading(s.heading);
    e->set_pitch(s.pitch);
    e->set_roll(s.roll);
    e->set_speed(s.speed);
    e->set_accel(s.accel);
    e->set_fuel(s.fuel);
    e->set_rpm(s.rpm);
    return packet.SerializeAsString();
}

// Unreal JSON 입력 → VehicleInput 파싱
static VehicleInput parse_input(const std::string& msg) {
    VehicleInput in;
    try {
        auto obj = json::parse(msg).as_object();
        if (obj.contains("throttle"))  in.throttle  = json::value_to<float>(obj.at("throttle"));
        if (obj.contains("brake"))     in.brake      = json::value_to<float>(obj.at("brake"));
        if (obj.contains("steering"))  in.steering   = json::value_to<float>(obj.at("steering"));
        if (obj.contains("handbrake")) in.handbrake  = obj.at("handbrake").as_bool();
    } catch (const std::exception& e) {
        std::cerr << "[Input] Parse error: " << e.what() << "\n";
    }
    return in;
}

int main() {
    std::cout << "[SimCore] Starting...\n";

    // Physics engine
    VehiclePhysics physics(
        Config::INIT_LAT, Config::INIT_LON,
        Config::INIT_ALT, Config::INIT_HEADING);

    // ZMQ Publisher (→ Python Relay)
    ZmqPublisher publisher(Config::ZMQ_BIND_ADDR);

    // Asio io_context (single-threaded)
    net::io_context ioc;

    // WebSocket Server (← Unreal 입력)
    WsServer ws_server(
        ioc,
        Config::WS_PORT,
        // 입력 수신 시: physics 업데이트
        [&physics](const std::string& msg) {
            physics.set_input(parse_input(msg));
        },
        // Unreal 접속 시: 초기 스폰 위치 전송
        [&physics]() -> std::string {
            auto s = physics.get_state();
            json::object obj;
            obj["type"]    = "init";
            obj["lat"]     = s.lat;
            obj["lon"]     = s.lon;
            obj["alt"]     = s.alt;
            obj["heading"] = s.heading;
            return json::serialize(obj);
        });

    ws_server.start();

    // 60Hz 물리 계산 + ZMQ publish 타이머
    net::steady_timer timer(ioc);
    std::function<void()> schedule = [&]() {
        timer.expires_after(std::chrono::microseconds(
            static_cast<long>(1'000'000.0 / Config::PHYSICS_HZ)));

        timer.async_wait([&](boost::system::error_code ec) {
            if (ec) return;
            auto state = physics.update(Config::PHYSICS_DT);
            publisher.publish(serialize_state(state));
            schedule();
        });
    };
    schedule();

    std::cout << "[SimCore] WS :" << Config::WS_PORT
              << "  ZMQ " << Config::ZMQ_BIND_ADDR << "\n";

    ioc.run();
    return 0;
}
