#pragma once

#include <cstdint>
#include <mutex>

struct VehicleInput {
    float throttle  = 0.f;   // 0 ~ 1
    float brake     = 0.f;   // 0 ~ 1
    float steering  = 0.f;   // -1 (left) ~ 1 (right)
    bool  handbrake = false;
};

struct VehicleState {
    uint32_t entity_id = 1;
    double   timestamp = 0.0;
    double   lat       = 0.0;
    double   lon       = 0.0;
    double   alt       = 0.0;
    float    heading   = 0.f;  // degrees 0~360
    float    pitch     = 0.f;
    float    roll      = 0.f;
    float    speed     = 0.f;  // m/s
    float    accel     = 0.f;  // m/s²
    float    fuel      = 100.f; // %
    float    rpm       = 800.f;
};

class VehiclePhysics {
public:
    VehiclePhysics(double lat, double lon, double alt, float heading);

    void         set_input(const VehicleInput& input);
    VehicleState update(double dt);
    VehicleState get_state() const;

private:
    VehicleState       state_;
    VehicleInput       input_;
    mutable std::mutex input_mutex_;

    // Physics constants
    static constexpr float  MAX_SPEED        = 50.f;   // m/s (~180 km/h)
    static constexpr float  MAX_REVERSE_SPEED= 15.f;   // m/s (~54 km/h)
    static constexpr float  MAX_ACCEL        = 8.f;    // m/s²
    static constexpr float  MAX_BRAKE        = 20.f;   // m/s²
    static constexpr float  MAX_REVERSE_ACCEL= 5.f;    // m/s²
    static constexpr float  HANDBRAKE_DECEL  = 30.f;   // m/s²
    static constexpr float  DRAG_COEFF       = 0.012f; // quadratic drag
    static constexpr float  MAX_STEER_RATE         = 60.f;   // deg/s at full steer
    static constexpr float  HANDBRAKE_DRIFT_MULT   = 1.8f;   // 핸드브레이크 드리프트 배율
    static constexpr float  ROLL_INTENSITY          = 6.f;    // 코너링 롤 강도 (degrees)
    static constexpr float  FUEL_RATE        = 0.004f; // %/s at full throttle
    static constexpr float  IDLE_RPM         = 800.f;
    static constexpr float  MAX_RPM          = 7000.f;
    static constexpr double EARTH_R          = 6371000.0; // meters
};
