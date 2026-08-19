#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>

#include <boost/asio.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <timeapi.h>
#endif

#include "config.hpp"
#include "physics/vehicle_physics.hpp"
#include "protocol/vehicle_messages.hpp"
#include "publisher/zmq_publisher.hpp"
#include "simulation_clock.hpp"
#include "websocket/ws_server.hpp"

namespace net  = boost::asio;   // 비동기 네트워크

namespace {

class PlatformTimerResolution {
public:
    PlatformTimerResolution()
    {
#ifdef _WIN32
        active_ = timeBeginPeriod(kResolutionMs) == TIMERR_NOERROR;
        if (!active_) {
            std::cerr << "[Timing] failed to request 1ms Windows timer resolution\n";
        }
#endif
    }

    ~PlatformTimerResolution()
    {
#ifdef _WIN32
        if (active_) {
            timeEndPeriod(kResolutionMs);
        }
#endif
    }

    PlatformTimerResolution(const PlatformTimerResolution&) = delete;
    PlatformTimerResolution& operator=(const PlatformTimerResolution&) = delete;

private:
#ifdef _WIN32
    static constexpr UINT kResolutionMs = 1;
    bool active_ = false;
#endif
};

} // namespace

int main() {
    PlatformTimerResolution timer_resolution;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
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
    SimulationClock simulation_clock(Config::PHYSICS_HZ);
    auto last_command_time = SimulationClock::Clock::time_point::min();
    std::uint64_t last_command_sequence = 0;
    std::string active_source_id;
    bool safe_stop_active = true;
    bool estop_latched = false;
    std::uint32_t last_reported_overrun_count = 0;
    VehicleInput last_logged_input;
    auto last_control_change_time = SimulationClock::Clock::time_point::min();
    std::uint64_t control_change_revision = 0;
    std::uint64_t reported_control_change_revision = 0;

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
        [&physics, &last_command_time, &last_command_sequence,
         &active_source_id, &safe_stop_active, &estop_latched,
         &last_logged_input, &last_control_change_time,
         &control_change_revision](const std::string& msg) {
            std::string error;
            auto input = simcore_host::parse_control_command_envelope(msg, &error);
            if (!input) {
                std::cerr << "[Input] " << error << "\n";
                return;
            }
            if (input->source_id.empty()) {
                std::cerr << "[Input] missing source_id\n";
                return;
            }
            if (input->map_package_checksum != Config::MAP_PACKAGE_CHECKSUM) {
                std::cerr << "[Input] map package checksum mismatch\n";
                return;
            }
            if (estop_latched && !input->estop) {
                std::cerr << "[Safety] E-stop is latched; restart is required to reset\n";
                return;
            }
            if (!active_source_id.empty() && input->source_id != active_source_id) {
                std::cerr << "[Input] rejected non-owner source " << input->source_id << "\n";
                return;
            }
            if (input->sequence == 0 || input->sequence <= last_command_sequence) {
                std::cerr << "[Input] rejected stale sequence\n";
                return;
            }
            active_source_id = input->source_id;
            last_command_sequence = input->sequence;
            last_command_time = SimulationClock::Clock::now();
            safe_stop_active = false;
            estop_latched = estop_latched || input->estop;
            physics.set_input(input->input);
            const bool changed = std::abs(last_logged_input.throttle - input->input.throttle) > 0.001f
                || std::abs(last_logged_input.brake - input->input.brake) > 0.001f
                || std::abs(last_logged_input.steering - input->input.steering) > 0.001f
                || last_logged_input.handbrake != input->input.handbrake
                || last_logged_input.gear != input->input.gear;
            if (changed) {
                last_logged_input = input->input;
                last_control_change_time = last_command_time;
                ++control_change_revision;
                std::cout << "[Input] change revision=" << control_change_revision
                          << " sequence=" << input->sequence
                          << " throttle=" << input->input.throttle
                          << " brake=" << input->input.brake
                          << " steering=" << input->input.steering
                          << " handbrake=" << input->input.handbrake << "\n";
            }
        },
        // Unreal 접속 시: 현재 state를 binary Protobuf로 전송
        [&physics, &make_metadata, &simulation_clock]() -> std::string {
            return simcore_host::serialize_world_state_envelope(
                physics.get_state(),
                make_metadata(simulation_clock.simulation_time_ns()));
        });

    ws_server.start();

    // 60Hz 물리 계산 + Unreal direct broadcast + ZMQ observer publish
    net::steady_timer timer(ioc);
    std::function<void()> schedule = [&]() {
        // 타이머 만료 시간 설정
        timer.expires_at(simulation_clock.next_deadline());

        // 타이머 만료 시 실행할 콜백
        // 정상 만료 -> ec = 0(false) -> 계속 실행
        // 타이머 취소 -> ec != 0(true) -> return 으로 중단
        timer.async_wait([&](boost::system::error_code ec) {
            if (ec) return;
            const auto now = SimulationClock::Clock::now();
            const auto command_age = last_command_time == SimulationClock::Clock::time_point::min()
                ? std::chrono::nanoseconds::max()
                : std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_command_time);
            if (command_age.count() > Config::COMMAND_TIMEOUT_NS && !safe_stop_active) {
                const auto command_age_ms = std::chrono::duration<double, std::milli>(
                    command_age).count();
                VehicleInput safe_input;
                safe_input.brake = 1.f;
                safe_input.handbrake = true;
                physics.set_input(safe_input);
                safe_stop_active = true;
                active_source_id.clear();
                last_command_sequence = 0;
                std::cerr << "[Safety] command timeout age_ms=" << command_age_ms
                          << "; SafeStop applied\n";
            }
            auto state = physics.update(Config::PHYSICS_DT);
            if (reported_control_change_revision != control_change_revision) {
                const auto command_to_tick = std::chrono::duration<double, std::milli>(
                    SimulationClock::Clock::now() - last_control_change_time).count();
                reported_control_change_revision = control_change_revision;
                std::cout << "[Latency] input revision=" << control_change_revision
                          << " applied_to_tick_ms=" << command_to_tick << "\n";
            }
            simulation_clock.advance(SimulationClock::Clock::now());
            if (simulation_clock.overrun_count() != last_reported_overrun_count) {
                last_reported_overrun_count = simulation_clock.overrun_count();
                std::cerr << "[Timing] tick overrun count=" << last_reported_overrun_count << "\n";
            }
            ws_server.broadcast_binary(
                simcore_host::serialize_world_state_envelope(
                    state,
                    make_metadata(simulation_clock.simulation_time_ns())));
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
