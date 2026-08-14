#pragma once

namespace Config {

    // Protocol
    inline constexpr const char* SOURCE_ID = "simcore-cpp-host";
    inline constexpr const char* MAP_PACKAGE_CHECKSUM = "unset";

    // ZMQ Publisher (→ Python Relay)
    inline constexpr const char* ZMQ_BIND_ADDR = "tcp://localhost:5555";

    // WebSocket Server (← Unreal input)
    inline constexpr unsigned short WS_PORT = 9000;

    // Physics loop
    inline constexpr double PHYSICS_HZ = 60.0;
    inline constexpr double PHYSICS_DT = 1.0 / PHYSICS_HZ;

    // Temporary Wall/Broad ENU anchor. The MapPackage loader will replace
    // altitude and the actual drivable-lane spawn when it is implemented.
    inline constexpr double DEFAULT_ORIGIN_LAT = 40.70694;
    inline constexpr double DEFAULT_ORIGIN_LON = -74.01083;
    inline constexpr double DEFAULT_ORIGIN_ALT = 0.0;
    inline constexpr float  DEFAULT_SPAWN_HEADING = 0.f;

} // namespace Config
