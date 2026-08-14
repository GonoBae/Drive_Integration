#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>

#include <boost/asio.hpp>
#include <boost/json.hpp>

#include "config.hpp"
#include "physics/vehicle_physics.hpp"
#include "publisher/zmq_publisher.hpp"
#include "websocket/ws_server.hpp"

// Protobuf generated
#include "vehicle.pb.h"

namespace net  = boost::asio;   // 비동기 네트워크
namespace json = boost::json;

// VehicleState → Protobuf 직렬화
static std::string serialize_state(const VehicleState& s)
{
    simcore::EntityStatePacket packet;
    auto* e = packet.add_entities();    // 추가된 항목의 주소를 반환
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
    e->set_east(s.east);
    e->set_north(s.north);
    e->set_yaw_rate(s.yaw_rate);
    e->set_steering_angle(s.steering_angle);
    e->set_gear(static_cast<simcore::VehicleGear>(s.gear));
    return packet.SerializeAsString();
}

static VehicleGear parse_gear(const json::value& value)
{
    if (value.is_string()) {
        const auto gear = value.as_string();
        if (gear == "drive" || gear == "D") return VehicleGear::Drive;
        if (gear == "reverse" || gear == "R") return VehicleGear::Reverse;
        if (gear == "neutral" || gear == "N") return VehicleGear::Neutral;
    } else if (value.is_int64()) {
        switch (value.as_int64()) {
        case -1: return VehicleGear::Reverse;
        case 0:  return VehicleGear::Neutral;
        case 1:  return VehicleGear::Drive;
        }
    }
    throw std::invalid_argument("gear must be drive/D/1, neutral/N/0, or reverse/R/-1");
}

// Unreal JSON 입력 → VehicleInput 파싱
// 편의성을 위해 JSON 사용
static VehicleInput parse_input(const std::string& msg) {
    VehicleInput in;
    try
    {
        auto obj = json::parse(msg).as_object();
        if (obj.contains("throttle"))  in.throttle  = json::value_to<float>(obj.at("throttle"));
        if (obj.contains("brake"))     in.brake      = json::value_to<float>(obj.at("brake"));
        if (obj.contains("steering"))  in.steering   = json::value_to<float>(obj.at("steering"));
        if (obj.contains("handbrake")) in.handbrake  = obj.at("handbrake").as_bool();
        if (obj.contains("gear"))      in.gear       = parse_gear(obj.at("gear"));
    }
    catch (const std::exception& e)
    {
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
        // 타이머 만료 시간 설정
        timer.expires_after(std::chrono::microseconds(
            static_cast<long>(1'000'000.0 / Config::PHYSICS_HZ)));

        // 타이머 만료 시 실행할 콜백
        // 정상 만료 -> ec = 0(false) -> 계속 실행
        // 타이머 취소 -> ec != 0(true) -> return 으로 중단
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
