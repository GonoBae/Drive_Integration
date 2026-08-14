#include "physics/vehicle_physics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

constexpr double DEG2RAD = std::numbers::pi_v<double> / 180.0;
constexpr double RAD2DEG = 180.0 / std::numbers::pi_v<double>;

float gear_direction(VehicleGear gear)
{
    switch (gear) {
    case VehicleGear::Drive:   return 1.f;
    case VehicleGear::Reverse: return -1.f;
    case VehicleGear::Neutral: return 0.f;
    }
    return 0.f;
}

double normalize_heading(double radians)
{
    const double full_turn = 2.0 * std::numbers::pi_v<double>;
    radians = std::fmod(radians, full_turn);
    return radians < 0.0 ? radians + full_turn : radians;
}

} // namespace

VehiclePhysics::VehiclePhysics(double lat, double lon, double alt, float heading,
                               VehicleParameters parameters)
    : parameters_(parameters)
    , origin_lat_(lat)
    , origin_lon_(lon)
    , heading_rad_(heading * DEG2RAD)
{
    if (parameters_.mass_kg <= 0.f || parameters_.wheelbase_m <= 0.f ||
        parameters_.tire_radius_m <= 0.f) {
        throw std::invalid_argument("Vehicle parameters must be positive");
    }

    state_.lat     = lat;
    state_.lon     = lon;
    state_.alt     = alt;
    state_.heading = heading;
    state_.rpm     = parameters_.idle_rpm;
}

void VehiclePhysics::set_input(const VehicleInput& input) {
    std::lock_guard lock(input_mutex_);
    input_ = input;
    input_.throttle = std::isfinite(input_.throttle)
        ? std::clamp(input_.throttle, 0.f, 1.f) : 0.f;
    input_.brake = std::isfinite(input_.brake)
        ? std::clamp(input_.brake, 0.f, 1.f) : 0.f;
    input_.steering = std::isfinite(input_.steering)
        ? std::clamp(input_.steering, -1.f, 1.f) : 0.f;

    if (input_.gear != VehicleGear::Neutral &&
        input_.gear != VehicleGear::Drive &&
        input_.gear != VehicleGear::Reverse) {
        input_.gear = VehicleGear::Neutral;
    }
}

VehicleState VehiclePhysics::update(double dt)
{
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.1) {
        throw std::invalid_argument("Physics dt must be in the range (0, 0.1]");
    }

    VehicleInput in;
    {
        std::lock_guard lock(input_mutex_);
        in = input_;
    }

    const float fdt = static_cast<float>(dt);

    // Timestamp
    auto now = std::chrono::system_clock::now();
    state_.timestamp = std::chrono::duration<double>(now.time_since_epoch()).count();

    // --- Longitudinal forces ---
    const float previous_speed = state_.speed;
    const float gear_sign = gear_direction(in.gear);
    const float available_drive_force = in.gear == VehicleGear::Reverse
        ? parameters_.max_reverse_force_n
        : parameters_.max_drive_force_n;
    const float drive_force = gear_sign * in.throttle * available_drive_force;

    float drag_force = 0.f;
    float rolling_force = 0.f;
    if (std::abs(previous_speed) > STOP_EPSILON) {
        const float motion_sign = std::copysign(1.f, previous_speed);
        drag_force = -0.5f * parameters_.air_density_kg_m3
                   * parameters_.drag_coefficient
                   * parameters_.frontal_area_m2
                   * previous_speed * std::abs(previous_speed);
        rolling_force = -motion_sign * parameters_.rolling_resistance_coeff
                      * parameters_.mass_kg * GRAVITY;
    }

    const float unbraked_accel =
        (drive_force + drag_force + rolling_force) / parameters_.mass_kg;
    float next_speed = previous_speed + unbraked_accel * fdt;

    // Passive resistance must not make a stopped vehicle roll in reverse.
    if (std::abs(previous_speed) > STOP_EPSILON &&
        previous_speed * next_speed < 0.f &&
        drive_force * previous_speed >= 0.f) {
        next_speed = 0.f;
    }

    const float service_brake_force = in.brake * parameters_.max_service_brake_n;
    const float brake_force = in.handbrake
        ? std::max(service_brake_force, parameters_.max_handbrake_force_n)
        : service_brake_force;
    if (brake_force > 0.f && std::abs(next_speed) > 0.f) {
        const float brake_delta = std::min(
            std::abs(next_speed), brake_force / parameters_.mass_kg * fdt);
        next_speed -= std::copysign(brake_delta, next_speed);
    }

    if (std::abs(next_speed) < STOP_EPSILON && in.throttle == 0.f) {
        next_speed = 0.f;
    }

    state_.speed = std::clamp(next_speed,
                              -parameters_.max_reverse_speed_mps,
                              parameters_.max_forward_speed_mps);
    state_.accel = (state_.speed - previous_speed) / fdt;

    // --- Kinematic bicycle steering ---
    state_.steering_angle = in.steering * parameters_.max_steering_angle_rad;
    state_.yaw_rate = state_.speed / parameters_.wheelbase_m
                    * std::tan(state_.steering_angle);
    heading_rad_ = normalize_heading(heading_rad_ + state_.yaw_rate * dt);
    state_.heading = static_cast<float>(heading_rad_ * RAD2DEG);

    east_m_  += state_.speed * std::sin(heading_rad_) * dt;
    north_m_ += state_.speed * std::cos(heading_rad_) * dt;
    state_.east  = east_m_;
    state_.north = north_m_;

    state_.lat = origin_lat_ + (north_m_ / EARTH_R) * RAD2DEG;
    state_.lon = origin_lon_ +
        (east_m_ / (EARTH_R * std::cos(origin_lat_ * DEG2RAD))) * RAD2DEG;

    // R1 visual attitude approximation. Suspension dynamics will replace this later.
    const float lateral_accel = state_.speed * state_.yaw_rate;
    state_.roll = std::clamp(
        static_cast<float>(-std::atan2(lateral_accel, GRAVITY) * RAD2DEG),
        -8.f, 8.f);
    state_.pitch = std::clamp(
        static_cast<float>(-std::atan2(state_.accel, GRAVITY) * RAD2DEG),
        -6.f, 6.f);

    // --- RPM (single-ratio driveline for the first physics milestone) ---
    const float wheel_rpm = std::abs(state_.speed)
        / (2.f * std::numbers::pi_v<float> * parameters_.tire_radius_m) * 60.f;
    const float gear_ratio = in.gear == VehicleGear::Reverse
        ? parameters_.reverse_gear_ratio
        : parameters_.drive_gear_ratio;
    const float coupled_rpm = wheel_rpm * gear_ratio * parameters_.final_drive_ratio;
    const float neutral_rpm = parameters_.idle_rpm
        + in.throttle * (parameters_.max_rpm - parameters_.idle_rpm) * 0.5f;
    state_.rpm = std::clamp(
        in.gear == VehicleGear::Neutral ? neutral_rpm : coupled_rpm,
        parameters_.idle_rpm,
        parameters_.max_rpm);
    state_.gear = in.gear;

    // --- Fuel ---
    if (state_.fuel > 0.f)
        state_.fuel = std::max(
            0.f, state_.fuel - in.throttle * parameters_.fuel_rate_percent_s * fdt);

    return state_;
}

VehicleState VehiclePhysics::get_state() const {
    return state_;
}
