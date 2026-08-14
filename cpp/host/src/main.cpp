#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>

#include <boost/asio.hpp>

#include "config.hpp"
#include "physics/vehicle_physics.hpp"
#include "protocol/vehicle_messages.hpp"
#include "publisher/zmq_publisher.hpp"
#include "websocket/ws_server.hpp"

namespace net  = boost::asio;   // 비동기 네트워크

static std::uint64_t simulation_time_ns(std::uint64_t tick_index)
{
    constexpr long double kNanosecondsPerSecond = 1'000'000'000.0L;
    return static_cast<std::uint64_t>(
        static_cast<long double>(tick_index) * kNanosecondsPerSecond / Config::PHYSICS_HZ);
}

int main() {
    std::cout << "[SimCore] Starting...\n";

    // Physics engine
    VehiclePhysics physics(
        Config::DEFAULT_ORIGIN_LAT, Config::DEFAULT_ORIGIN_LON,
        Config::DEFAULT_ORIGIN_ALT, Config::DEFAULT_SPAWN_HEADING);

    // ZMQ Publisher (→ Python Relay)
    ZmqPublisher publisher(Config::ZMQ_BIND_ADDR);

    // Asio io_context (single-threaded)
    net::io_context ioc;

    std::uint64_t message_sequence = 1;
    std::uint64_t tick_index = 0;

    auto make_metadata = [&](std::uint64_t sim_time_ns) {
        return simcore_host::EnvelopeMetadata{
            message_sequence++,
            sim_time_ns,
            Config::SOURCE_ID,
            Config::MAP_PACKAGE_CHECKSUM,
        };
    };

    // WebSocket Server (Unreal ↔ C++ binary Protobuf)
    WsServer ws_server(
        ioc,
        Config::WS_PORT,
        // 입력 수신 시: ControlCommand 적용
        [&physics](const std::string& msg) {
            std::string error;
            auto input = simcore_host::parse_control_command_envelope(msg, &error);
            if (!input) {
                std::cerr << "[Input] " << error << "\n";
                return;
            }
            physics.set_input(*input);
        },
        // Unreal 접속 시: 현재 state를 binary Protobuf로 전송
        [&physics, &make_metadata, &tick_index]() -> std::string {
            return simcore_host::serialize_world_state_envelope(
                physics.get_state(),
                make_metadata(simulation_time_ns(tick_index)));
        });

    ws_server.start();

    // 60Hz 물리 계산 + Unreal direct broadcast + ZMQ observer publish
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
            ++tick_index;
            auto state = physics.update(Config::PHYSICS_DT);
            ws_server.broadcast_binary(
                simcore_host::serialize_world_state_envelope(
                    state,
                    make_metadata(simulation_time_ns(tick_index))));
            publisher.publish(simcore_host::serialize_entity_state_packet(state));
            schedule();
        });
    };
    schedule();

    std::cout << "[SimCore] WS binary :" << Config::WS_PORT
              << "  ZMQ " << Config::ZMQ_BIND_ADDR << "\n";

    ioc.run();
    return 0;
}
