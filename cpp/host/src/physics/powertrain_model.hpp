#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace simcore_host {

struct TorqueCurvePoint { float rpm = 0.f; float torque_nm = 0.f; };

struct EngineParameters {
    float idle_rpm = 800.f;
    float max_rpm = 6500.f;
    float rotational_inertia_kg_m2 = 0.25f;
    float response_time_s = 0.15f;
    float idle_fuel_lph = 0.8f;
    float bsfc_g_per_kwh = 260.f;
    std::vector<TorqueCurvePoint> torque_curve;
};

struct TransmissionParameters {
    std::vector<float> forward_ratios;
    float reverse_ratio = 3.2f;
    float max_input_torque_nm = 400.f;
    float efficiency = 0.95f;
    float upshift_rpm = 5000.f;
    float downshift_rpm = 1800.f;
    float shift_duration_s = 0.25f;
};

struct DrivetrainParameters {
    float final_drive_ratio = 3.42f;
    float front_torque_fraction = 0.f;
    float efficiency = 0.95f;
};

struct FuelTankParameters {
    float capacity_l = 50.f;
    float initial_fuel_l = 50.f;
    float fuel_density_kg_l = 0.745f;
};

struct PowertrainParameters {
    EngineParameters engine;
    TransmissionParameters transmission;
    DrivetrainParameters drivetrain;
    FuelTankParameters fuel_tank;
};

struct PowertrainState {
    float engine_rpm = 0.f;
    float filtered_throttle = 0.f;
    std::size_t forward_gear = 1;
    std::size_t pending_forward_gear = 1;
    float shift_remaining_s = 0.f;
    int selected_direction = 1; // -1 reverse, 0 neutral, +1 drive
    double remaining_fuel_l = 0.0;
    double fuel_flow_lph = 0.0;
    float combustion_torque_nm = 0.f;
    float transmission_input_torque_nm = 0.f;
    float crank_power_kw = 0.f;
    std::array<float, 2> axle_drive_torque_nm{}; // total axle, signed
    bool shifting = false;
    bool drive_inhibited = false;
};

struct PowertrainInput {
    float throttle = 0.f;
    int direction = 1;
    float vehicle_speed_mps = 0.f;
    // Actual half-shaft speeds, including tire-radius and slip differences.
    float front_wheel_angular_speed_rad_s = 0.f;
    float rear_wheel_angular_speed_rad_s = 0.f;
    bool torque_inhibited = false; // brake, limiter, handbrake or safety gate
};

[[nodiscard]] bool valid_powertrain_parameters(const PowertrainParameters& parameters);
[[nodiscard]] float interpolate_engine_torque(const EngineParameters& engine, float rpm);
[[nodiscard]] PowertrainState initial_powertrain_state(const PowertrainParameters& parameters);
void advance_powertrain(const PowertrainParameters& parameters, PowertrainState& state,
    const PowertrainInput& input, float dt_seconds);

} // namespace simcore_host
