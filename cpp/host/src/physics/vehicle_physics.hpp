#pragma once

#include <cstdint>
#include <mutex>

enum class VehicleGear : uint8_t {
    Neutral = 0,
    Drive   = 1,
    Reverse = 2,
};

struct VehicleInput {
    float throttle  = 0.f;   // 0 ~ 1
    float brake     = 0.f;   // 0 ~ 1
    float steering  = 0.f;   // -1 (left) ~ 1 (right)
    bool  handbrake = false;
    VehicleGear gear = VehicleGear::Drive;
};

struct VehicleParameters {
    float mass_kg                 = 1500.f;
    float wheelbase_m             = 2.70f;
    float max_steering_angle_rad  = 0.55850536f; // 32 degrees
    float max_drive_force_n       = 8000.f;
    float max_reverse_force_n     = 5000.f;
    float max_service_brake_n     = 18000.f;
    float max_handbrake_force_n   = 22000.f;
    float rolling_resistance_coeff = 0.015f;
    float drag_coefficient        = 0.29f;
    float frontal_area_m2         = 2.20f;
    float air_density_kg_m3       = 1.225f;
    float tire_radius_m           = 0.32f;
    float final_drive_ratio       = 3.42f;
    float drive_gear_ratio        = 3.50f;
    float reverse_gear_ratio      = 3.20f;
    float max_forward_speed_mps   = 50.f;
    float max_reverse_speed_mps   = 15.f;
    float idle_rpm                = 800.f;
    float max_rpm                 = 7000.f;
    float fuel_rate_percent_s     = 0.004f;
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
    double   east      = 0.0;   // local ENU X (meters)
    double   north     = 0.0;   // local ENU Y (meters)
    float    yaw_rate  = 0.f;   // rad/s, positive = right turn
    float    steering_angle = 0.f; // road wheel angle (radians)
    VehicleGear gear   = VehicleGear::Drive;
};

class VehiclePhysics {
public:
    VehiclePhysics(double lat, double lon, double alt, float heading,
                   VehicleParameters parameters = {});

    void         set_input(const VehicleInput& input);
    VehicleState update(double dt);
    VehicleState get_state() const;

private:
    VehicleState       state_;
    VehicleInput       input_;
    VehicleParameters  parameters_;
    mutable std::mutex input_mutex_;

    double origin_lat_  = 0.0;
    double origin_lon_  = 0.0;
    double east_m_      = 0.0;
    double north_m_     = 0.0;
    double heading_rad_ = 0.0;

    static constexpr float  GRAVITY          = 9.80665f;
    static constexpr float  STOP_EPSILON     = 0.01f;
    static constexpr double EARTH_R          = 6371000.0; // meters
};
