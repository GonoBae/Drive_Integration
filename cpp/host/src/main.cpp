#include <chrono>
#include <exception>
#include <iostream>
#include <memory>

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
#include "publisher/zmq_publisher.hpp"
#include "simulation_host.hpp"
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

int run_simcore()
{
    PlatformTimerResolution timer_resolution;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "[SimCore] Starting...\n";

    // ZMQ Publisher (→ Python Relay)
    ZmqPublisher publisher(Config::ZMQ_BIND_ADDR);

    // Asio io_context (single-threaded)
    net::io_context ioc;

    std::unique_ptr<WsServer> ws_server;
    SimulationHost host(
        ioc,
        {
            Config::DEFAULT_ORIGIN_LAT,
            Config::DEFAULT_ORIGIN_LON,
            Config::DEFAULT_ORIGIN_ALT,
            Config::DEFAULT_SPAWN_HEADING,
            Config::PHYSICS_HZ,
            std::chrono::nanoseconds(Config::COMMAND_TIMEOUT_NS),
            std::chrono::nanoseconds(Config::MAX_COMMAND_QUEUE_AGE_NS),
            Config::SOURCE_ID,
            Config::MAP_PACKAGE_CHECKSUM,
        },
        {
            [&ws_server](const std::string& message) {
                ws_server->broadcast_binary(message);
            },
            [&publisher](const std::string& message) {
                publisher.publish(message);
            },
            [&ws_server](const std::string& reason) {
                ws_server->close_all(reason);
            },
        });

    ws_server = std::make_unique<WsServer>(
        ioc,
        Config::WS_PORT,
        [&host](const std::string& message) {
            host.handle_control_message(message);
        },
        [&host] {
            return host.make_initial_world_state();
        });

    ws_server->start();
    host.start();

    std::cout << "[SimCore] WS binary :" << Config::WS_PORT
              << "  ZMQ " << Config::ZMQ_BIND_ADDR << "\n";

    ioc.run();
    host.stop();
    ws_server->stop();
    return 0;
}

int main()
{
    try {
        return run_simcore();
    } catch (const std::exception& error) {
        std::cerr << "[SimCore] fatal error: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[SimCore] fatal unknown error\n";
        return 1;
    }
}
