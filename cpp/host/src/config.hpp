#pragma once

namespace Config {

    // ZMQ Publisher (→ Python Relay)
    inline constexpr const char* ZMQ_BIND_ADDR = "tcp://0.0.0.0:5555";

    // WebSocket Server (← Unreal input)
    inline constexpr unsigned short WS_PORT = 9000;

    // Physics loop
    inline constexpr double PHYSICS_HZ = 60.0;
    inline constexpr double PHYSICS_DT = 1.0 / PHYSICS_HZ;

    // Initial vehicle spawn position (서울 시청)
    inline constexpr double INIT_LAT     = 37.5665;
    inline constexpr double INIT_LON     = 126.9780;
    inline constexpr double INIT_ALT     = 10.0;
    inline constexpr float  INIT_HEADING = 0.f;

} // namespace Config