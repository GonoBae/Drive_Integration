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
        parameters_.tire_radius_m <= 0.f || parameters_.yaw_inertia_kg_m2 <= 0.f ||
        parameters_.pitch_inertia_kg_m2 <= 0.f || parameters_.roll_inertia_kg_m2 <= 0.f ||
        parameters_.front_track_m <= 0.f || parameters_.rear_track_m <= 0.f ||
        parameters_.tire_corner_stiffness_n_rad <= 0.f ||
        parameters_.tire_longitudinal_stiffness_n <= 0.f ||
        parameters_.wheel_inertia_kg_m2 <= 0.f || parameters_.tire_friction <= 0.f) {
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

    // --- Four-wheel planar contact and tire forces ---
    const float previous_speed = body_longitudinal_speed_mps_;
    const float gear_sign = gear_direction(in.gear);
    const float available_drive_force = in.gear == VehicleGear::Reverse
        ? parameters_.max_reverse_force_n
        : parameters_.max_drive_force_n;
    const float total_drive_force = gear_sign * in.throttle * available_drive_force;
    const float service_brake_force = in.brake * parameters_.max_service_brake_n;
    const float total_brake_force = in.handbrake
        ? std::max(service_brake_force, parameters_.max_handbrake_force_n)
        : service_brake_force;
    state_.steering_angle = in.steering * parameters_.max_steering_angle_rad;
    const float half_wheelbase = parameters_.wheelbase_m * 0.5f;
    const std::array<float, 4> wheel_x{half_wheelbase, half_wheelbase,
                                      -half_wheelbase, -half_wheelbase};
    const std::array<float, 4> wheel_y{-parameters_.front_track_m * 0.5f,
                                       parameters_.front_track_m * 0.5f,
                                      -parameters_.rear_track_m * 0.5f,
                                       parameters_.rear_track_m * 0.5f};

    float total_force_x = 0.f;
    float total_force_y = 0.f;
    float total_yaw_moment = 0.f;
    const float longitudinal_transfer = parameters_.mass_kg * state_.accel
                                      * parameters_.cg_height_m / parameters_.wheelbase_m;
    const float lateral_accel_estimate = body_longitudinal_speed_mps_ * yaw_rate_rad_s_;
    const float lateral_transfer_front = parameters_.mass_kg * lateral_accel_estimate
                                       * parameters_.cg_height_m / (2.f * parameters_.front_track_m);
    const float lateral_transfer_rear = parameters_.mass_kg * lateral_accel_estimate
                                      * parameters_.cg_height_m / (2.f * parameters_.rear_track_m);
    const float front_base_load = parameters_.mass_kg * GRAVITY * 0.25f - longitudinal_transfer * 0.5f;
    const float rear_base_load = parameters_.mass_kg * GRAVITY * 0.25f + longitudinal_transfer * 0.5f;
    const float speed_for_slip = std::max(std::abs(body_longitudinal_speed_mps_),
                                          parameters_.low_speed_lateral_cutoff_mps);

    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        auto& wheel = state_.wheels[index];
        wheel.wheel_index = static_cast<uint32_t>(index);
        wheel.in_contact = true; // R1 flat-ground contact; MapPackage queries replace this.
        wheel.steering_angle = index < 2 ? state_.steering_angle : 0.f;
        const float side_sign = index % 2 == 0 ? -1.f : 1.f;
        const float lateral_transfer = index < 2 ? lateral_transfer_front : lateral_transfer_rear;
        wheel.normal_load = std::max(0.f,
            (index < 2 ? front_base_load : rear_base_load) + side_sign * lateral_transfer);

        const float wheel_velocity_x = body_longitudinal_speed_mps_ - yaw_rate_rad_s_ * wheel_y[index];
        const float wheel_velocity_y = body_lateral_speed_mps_ + yaw_rate_rad_s_ * wheel_x[index];
        const float cosine = std::cos(wheel.steering_angle);
        const float sine = std::sin(wheel.steering_angle);
        const float longitudinal_velocity = cosine * wheel_velocity_x + sine * wheel_velocity_y;
        const float lateral_velocity = -sine * wheel_velocity_x + cosine * wheel_velocity_y;

        wheel.slip_angle = std::atan2(lateral_velocity, speed_for_slip);
        const float wheel_surface_speed = wheel_angular_speed_rad_s_[index] * parameters_.tire_radius_m;
        const float slip_denominator = std::max(std::abs(longitudinal_velocity), 1.f);
        wheel.longitudinal_slip = std::clamp(
            (wheel_surface_speed - longitudinal_velocity) / slip_denominator, -1.f, 1.f);
        float tire_force_x = parameters_.tire_longitudinal_stiffness_n * wheel.longitudinal_slip;
        float tire_force_y = std::abs(body_longitudinal_speed_mps_) < parameters_.low_speed_lateral_cutoff_mps
            ? 0.f
            : -parameters_.tire_corner_stiffness_n_rad * wheel.slip_angle;

        const float friction_limit = parameters_.tire_friction * wheel.normal_load;
        const float force_magnitude = std::hypot(tire_force_x, tire_force_y);
        if (force_magnitude > friction_limit && force_magnitude > 0.f) {
            const float scale = friction_limit / force_magnitude;
            tire_force_x *= scale;
            tire_force_y *= scale;
        }
        wheel.longitudinal_force = tire_force_x;
        wheel.lateral_force = tire_force_y;
        const float drive_torque = index >= 2
            ? total_drive_force * parameters_.tire_radius_m * 0.5f : 0.f;
        float brake_direction = 0.f;
        if (std::abs(wheel_angular_speed_rad_s_[index]) > STOP_EPSILON) {
            brake_direction = std::copysign(1.f, wheel_angular_speed_rad_s_[index]);
        } else if (std::abs(longitudinal_velocity) > STOP_EPSILON) {
            brake_direction = std::copysign(1.f, longitudinal_velocity);
        }
        const float brake_torque = total_brake_force * parameters_.tire_radius_m * 0.25f * brake_direction;
        const float angular_accel = (drive_torque - tire_force_x * parameters_.tire_radius_m - brake_torque)
                                  / parameters_.wheel_inertia_kg_m2;
        const float previous_angular_speed = wheel_angular_speed_rad_s_[index];
        wheel_angular_speed_rad_s_[index] += angular_accel * fdt;
        if (total_brake_force > 0.f && previous_angular_speed * wheel_angular_speed_rad_s_[index] < 0.f) {
            wheel_angular_speed_rad_s_[index] = 0.f;
        }
        wheel.angular_speed = wheel_angular_speed_rad_s_[index];

        const float body_force_x = cosine * tire_force_x - sine * tire_force_y;
        const float body_force_y = sine * tire_force_x + cosine * tire_force_y;
        total_force_x += body_force_x;
        total_force_y += body_force_y;
        total_yaw_moment += wheel_x[index] * body_force_y - wheel_y[index] * body_force_x;

        const double wheel_forward = wheel_x[index];
        const double wheel_right = wheel_y[index];
        wheel.contact_point_enu.x = east_m_ + wheel_forward * std::sin(heading_rad_)
            + wheel_right * std::cos(heading_rad_);
        wheel.contact_point_enu.y = north_m_ + wheel_forward * std::cos(heading_rad_)
            - wheel_right * std::sin(heading_rad_);
        wheel.contact_point_enu.z = 0.0;
    }

    // R1 uses one rotational degree of freedom per axle. Keep the left/right
    // wheels mechanically synchronized instead of exposing independent tire
    // solver jitter as different visual wheel speeds.
    const auto synchronize_axle = [this](std::size_t first, std::size_t second) {
        const float axle_speed = 0.5f *
            (wheel_angular_speed_rad_s_[first] + wheel_angular_speed_rad_s_[second]);
        wheel_angular_speed_rad_s_[first] = axle_speed;
        wheel_angular_speed_rad_s_[second] = axle_speed;
    };
    synchronize_axle(0, 1);
    synchronize_axle(2, 3);

    if (std::abs(body_longitudinal_speed_mps_) > STOP_EPSILON) {
        const float motion_sign = std::copysign(1.f, body_longitudinal_speed_mps_);
        total_force_x -= 0.5f * parameters_.air_density_kg_m3
                       * parameters_.drag_coefficient * parameters_.frontal_area_m2
                       * body_longitudinal_speed_mps_ * std::abs(body_longitudinal_speed_mps_);
        total_force_x -= motion_sign * parameters_.rolling_resistance_coeff
                       * parameters_.mass_kg * GRAVITY;
    }

    const float longitudinal_accel = total_force_x / parameters_.mass_kg
                                   + body_lateral_speed_mps_ * yaw_rate_rad_s_;
    const float lateral_accel_body = total_force_y / parameters_.mass_kg
                                  - body_longitudinal_speed_mps_ * yaw_rate_rad_s_;
    body_longitudinal_speed_mps_ += longitudinal_accel * fdt;
    body_lateral_speed_mps_ += lateral_accel_body * fdt;
    yaw_rate_rad_s_ += total_yaw_moment / parameters_.yaw_inertia_kg_m2 * fdt;

    if (total_brake_force > 0.f) {
        const bool crossed_stop = previous_speed * body_longitudinal_speed_mps_ < 0.f;
        const bool held_at_stop = std::abs(previous_speed) <= STOP_EPSILON;
        if (crossed_stop || held_at_stop) {
            body_longitudinal_speed_mps_ = 0.f;
        }
    }
    if (std::abs(body_longitudinal_speed_mps_) < STOP_EPSILON && in.throttle == 0.f) {
        body_longitudinal_speed_mps_ = 0.f;
    }
    body_longitudinal_speed_mps_ = std::clamp(body_longitudinal_speed_mps_,
        -parameters_.max_reverse_speed_mps, parameters_.max_forward_speed_mps);

    state_.speed = body_longitudinal_speed_mps_;
    state_.accel = (state_.speed - previous_speed) / fdt;
    state_.yaw_rate = yaw_rate_rad_s_;
    heading_rad_ = normalize_heading(heading_rad_ + yaw_rate_rad_s_ * dt);
    state_.heading = static_cast<float>(heading_rad_ * RAD2DEG);

    const double east_velocity = body_longitudinal_speed_mps_ * std::sin(heading_rad_)
                               + body_lateral_speed_mps_ * std::cos(heading_rad_);
    const double north_velocity = body_longitudinal_speed_mps_ * std::cos(heading_rad_)
                                - body_lateral_speed_mps_ * std::sin(heading_rad_);
    east_m_ += east_velocity * dt;
    north_m_ += north_velocity * dt;
    state_.east  = east_m_;
    state_.north = north_m_;
    state_.position_enu = {east_m_, north_m_, parameters_.cg_height_m};
    state_.linear_velocity_body = {body_longitudinal_speed_mps_, body_lateral_speed_mps_, 0.0};
    state_.angular_velocity_body = {roll_rate_rad_s_, pitch_rate_rad_s_, yaw_rate_rad_s_};

    // Published wheel rotation drives the Unreal presentation. A stopped
    // chassis must not show tire frames creeping because of sub-threshold
    // wheel/slip oscillation. The axle pairs remain bit-identical.
    const bool chassis_stationary = std::abs(state_.speed) <= STOP_EPSILON;
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        state_.wheels[index].angular_speed = chassis_stationary
            ? 0.f
            : wheel_angular_speed_rad_s_[index];
    }

    state_.lat = origin_lat_ + (north_m_ / EARTH_R) * RAD2DEG;
    state_.lon = origin_lon_ +
        (east_m_ / (EARTH_R * std::cos(origin_lat_ * DEG2RAD))) * RAD2DEG;

    // Chassis roll/pitch response around a suspension-equivalent spring and damper.
    const float roll_moment = -total_force_y * parameters_.cg_height_m
        - parameters_.attitude_spring_n_m_rad * roll_rad_
        - parameters_.attitude_damping_n_m_s_rad * roll_rate_rad_s_;
    const float pitch_moment = total_force_x * parameters_.cg_height_m
        - parameters_.attitude_spring_n_m_rad * pitch_rad_
        - parameters_.attitude_damping_n_m_s_rad * pitch_rate_rad_s_;
    roll_rate_rad_s_ += roll_moment / parameters_.roll_inertia_kg_m2 * fdt;
    pitch_rate_rad_s_ += pitch_moment / parameters_.pitch_inertia_kg_m2 * fdt;
    roll_rad_ = std::clamp(roll_rad_ + roll_rate_rad_s_ * fdt,
                           static_cast<float>(-8.0 * DEG2RAD),
                           static_cast<float>(8.0 * DEG2RAD));
    pitch_rad_ = std::clamp(pitch_rad_ + pitch_rate_rad_s_ * fdt,
                            static_cast<float>(-6.0 * DEG2RAD),
                            static_cast<float>(6.0 * DEG2RAD));
    state_.roll = static_cast<float>(roll_rad_ * RAD2DEG);
    state_.pitch = static_cast<float>(pitch_rad_ * RAD2DEG);
    state_.angular_velocity_body = {roll_rate_rad_s_, pitch_rate_rad_s_, yaw_rate_rad_s_};

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
