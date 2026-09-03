#pragma once

#include <cstdint>

namespace Config {

    // Protocol
    inline constexpr const char* SOURCE_ID = "simcore-cpp-host";

    // Optional ZMQ observer (→ frozen Python Relay). The default build does
    // not compile or bind this transport.
    inline constexpr const char* ZMQ_BIND_ADDR = "tcp://localhost:5555";

    // WebSocket Server (← Unreal input)
    inline constexpr unsigned short WS_PORT = 9000;

    // Physics loop
    inline constexpr double PHYSICS_HZ = 60.0;
    inline constexpr double PHYSICS_DT = 1.0 / PHYSICS_HZ;
    inline constexpr std::uint64_t COMMAND_TIMEOUT_NS = 250'000'000;
    // A control packet whose generation clock trails receive time by more than
    // this amount is considered queued/stale and cannot re-arm SafeStop.
    inline constexpr std::uint64_t MAX_COMMAND_QUEUE_AGE_NS = 100'000'000;
    inline constexpr std::uint64_t HARD_COMMAND_TIMEOUT_NS = 1'000'000'000;

    // Temporary Wall/Broad ENU anchor. The MapPackage loader will replace
    // altitude and the actual drivable-lane spawn when it is implemented.
    inline constexpr double DEFAULT_ORIGIN_LAT = 40.70694;
    inline constexpr double DEFAULT_ORIGIN_LON = -74.01083;
    inline constexpr double DEFAULT_ORIGIN_ALT = 0.0;
    inline constexpr float  DEFAULT_SPAWN_HEADING = 0.f;

} // namespace Config
