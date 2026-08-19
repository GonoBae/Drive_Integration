#pragma once

#include <cstdint>
#include <array>
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
    float front_track_m           = 1.58f;
    float rear_track_m            = 1.58f;
    float cg_height_m             = 0.55f;
    float yaw_inertia_kg_m2       = 2500.f;
    float pitch_inertia_kg_m2     = 2200.f;
    float roll_inertia_kg_m2      = 700.f;
    float attitude_spring_n_m_rad = 45000.f;
    float attitude_damping_n_m_s_rad = 7500.f;
    float tire_corner_stiffness_n_rad = 55000.f;
    float tire_longitudinal_stiffness_n = 14000.f;
    float tire_friction           = 1.0f;
    float wheel_inertia_kg_m2     = 1.8f;
    float low_speed_lateral_cutoff_mps = 0.5f;
};

struct Vector3State {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct WheelState {
    // 0=front-left, 1=front-right, 2=rear-left, 3=rear-right.
    uint32_t wheel_index = 0;
    bool in_contact = true;
    float steering_angle = 0.f;
    float angular_speed = 0.f;
    float normal_load = 0.f;
    float longitudinal_slip = 0.f;
    float slip_angle = 0.f;
    float longitudinal_force = 0.f;
    float lateral_force = 0.f; // wheel-local left-positive force
    Vector3State contact_point_enu;
    Vector3State contact_normal_enu{0.0, 0.0, 1.0};
};

struct VehicleState {
    uint32_t entity_id = 1;
    double   timestamp = 0.0;
    double   lat       = 0.0;
    double   lon       = 0.0;
    double   alt       = 0.0;
    float    heading   = 0.f;  // degrees 0~360
    float    pitch     = 0.f;  // canonical body-frame pitch (degrees)
    float    roll      = 0.f;  // canonical body-frame roll (degrees)
    float    speed     = 0.f;  // m/s
    float    accel     = 0.f;  // m/s²
    float    fuel      = 100.f; // %
    float    rpm       = 800.f;
    double   east      = 0.0;   // local ENU X (meters)
    double   north     = 0.0;   // local ENU Y (meters)
    float    yaw_rate  = 0.f;   // navigation heading rate, positive = right turn
    float    steering_angle = 0.f; // road wheel angle (radians)
    VehicleGear gear   = VehicleGear::Drive;
    Vector3State position_enu;
    // Published body vectors use the right-handed canonical frame:
    // X=forward, Y=left, Z=up. Internal solver lateral values are right-positive.
    Vector3State linear_velocity_body;
    Vector3State angular_velocity_body;
    std::array<WheelState, 4> wheels;
};

class VehiclePhysics {
public:
    VehiclePhysics(double lat, double lon, double alt, float heading,
                   VehicleParameters parameters = {});

    void         set_input(const VehicleInput& input);
    VehicleState update(double dt);
    VehicleState get_state() const;

private:
    void update_wheel_contact_points();

    VehicleState       state_;
    VehicleInput       input_;
    VehicleParameters  parameters_;
    mutable std::mutex input_mutex_;

    double origin_lat_  = 0.0;
    double origin_lon_  = 0.0;
    double east_m_      = 0.0;
    double north_m_     = 0.0;
    double heading_rad_ = 0.0;
    float body_longitudinal_speed_mps_ = 0.f;
    float body_lateral_speed_mps_ = 0.f;
    float yaw_rate_rad_s_ = 0.f;
    float pitch_rad_ = 0.f;
    float roll_rad_ = 0.f;
    float pitch_rate_rad_s_ = 0.f;
    float roll_rate_rad_s_ = 0.f;
    std::array<float, 4> wheel_angular_speed_rad_s_{};

    static constexpr float  GRAVITY          = 9.80665f;
    static constexpr float  STOP_EPSILON     = 0.01f;
    static constexpr double EARTH_R          = 6371000.0; // meters
};
