#include "physics/vehicle_physics.hpp"

#include "coordinates/body_frame_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

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
                               VehicleParameters parameters,
                               std::shared_ptr<const simcore_host::GroundQuery> ground_query)
    : parameters_(parameters)
    , ground_query_(std::move(ground_query))
    , origin_lat_(lat)
    , origin_lon_(lon)
    , heading_rad_(normalize_heading(heading * DEG2RAD))
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
    if (!simcore_host::valid_suspension_parameters(parameters_.suspension)) {
        throw std::invalid_argument("Suspension parameters are invalid");
    }

    if (!std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(alt) ||
        !std::isfinite(heading)) {
        throw std::invalid_argument("Initial vehicle pose must be finite");
    }

    state_.lat = lat;
    state_.lon = lon;
    state_.alt = alt;
    state_.heading = static_cast<float>(heading_rad_ * RAD2DEG);
    state_.rpm = parameters_.idle_rpm;
    state_.position_enu = {0.0, 0.0, parameters_.cg_height_m};
    if (!ground_query_) {
        ground_query_ = std::make_shared<simcore_host::FlatGroundQuery>();
    }
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        auto& wheel = state_.wheels[index];
        wheel.wheel_index = static_cast<std::uint32_t>(index);
    }
    update_wheel_contacts(0.f, true);
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

    // Sample the authoritative ground at the current pose before tire forces
    // are evaluated. The constructor seeds this state so the first update also
    // starts with a complete contact snapshot.
    update_wheel_contacts(fdt, true);

    // --- Four-wheel planar contact and tire forces ---
    const float previous_speed = body_longitudinal_speed_mps_;
    const float gear_sign = gear_direction(in.gear);
    const float available_drive_force = in.gear == VehicleGear::Reverse
        ? parameters_.max_reverse_force_n
        : parameters_.max_drive_force_n;
    const bool forward_limiter_active = gear_sign > 0.f
        && body_longitudinal_speed_mps_ >= parameters_.max_forward_speed_mps;
    const bool reverse_limiter_active = gear_sign < 0.f
        && body_longitudinal_speed_mps_ <= -parameters_.max_reverse_speed_mps;
    // Clamping chassis speed without cutting drive torque lets wheel angular
    // speed wind up forever at the limiter. Remove only the torque that would
    // accelerate farther beyond the configured speed; tire reaction then
    // brings wheel surface speed back toward road speed.
    const float total_drive_force = forward_limiter_active || reverse_limiter_active
        ? 0.f
        : gear_sign * in.throttle * available_drive_force;
    const float service_brake_force = in.brake * parameters_.max_service_brake_n;
    const float total_brake_force = in.handbrake
        ? std::max(service_brake_force, parameters_.max_handbrake_force_n)
        : service_brake_force;
    const float solver_steering_angle =
        simcore_host::BodyFrameAdapter::canonical_steering_to_solver(in.steering)
        * parameters_.max_steering_angle_rad;
    state_.steering_angle =
        simcore_host::BodyFrameAdapter::solver_steering_to_canonical(
            solver_steering_angle);
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
    float total_normal_load = 0.f;
    const float longitudinal_transfer = parameters_.mass_kg * state_.accel
                                      * parameters_.cg_height_m / parameters_.wheelbase_m;
    const float lateral_accel_estimate =
        body_longitudinal_speed_mps_ * solver_yaw_rate_rad_s_;
    // A 50/50 static axle split is used until CG longitudinal position becomes
    // a parameter. Each axle must use only its share of vehicle mass; using the
    // full mass on both axles doubles the total lateral load transfer.
    const float front_axle_mass = parameters_.mass_kg * 0.5f;
    const float rear_axle_mass = parameters_.mass_kg * 0.5f;
    const float lateral_transfer_front = front_axle_mass * lateral_accel_estimate
                                       * parameters_.cg_height_m / (2.f * parameters_.front_track_m);
    const float lateral_transfer_rear = rear_axle_mass * lateral_accel_estimate
                                      * parameters_.cg_height_m / (2.f * parameters_.rear_track_m);
    const float speed_for_slip = std::max(std::abs(body_longitudinal_speed_mps_),
                                          parameters_.low_speed_lateral_cutoff_mps);

    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        auto& wheel = state_.wheels[index];
        wheel.wheel_index = static_cast<uint32_t>(index);
        const float solver_wheel_steering = index < 2 ? solver_steering_angle : 0.f;
        wheel.steering_angle =
            simcore_host::BodyFrameAdapter::solver_steering_to_canonical(
                solver_wheel_steering);
        // Solver Y is right-positive. Positive lateral acceleration therefore
        // describes a right turn and loads the outer (left) wheels.
        const float side_sign = index % 2 == 0 ? 1.f : -1.f;
        const float lateral_transfer = index < 2 ? lateral_transfer_front : lateral_transfer_rear;
        const float longitudinal_load_adjustment = index < 2
            ? -longitudinal_transfer * 0.5f
            : longitudinal_transfer * 0.5f;
        wheel.normal_load = wheel.in_contact
            ? std::clamp(
                suspension_base_force_n_[index]
                    + longitudinal_load_adjustment
                    + side_sign * lateral_transfer,
                0.f,
                parameters_.suspension.max_force_n)
            : 0.f;
        total_normal_load += wheel.normal_load;

        const float wheel_velocity_x =
            body_longitudinal_speed_mps_ - solver_yaw_rate_rad_s_ * wheel_y[index];
        const float wheel_velocity_y =
            solver_lateral_speed_mps_ + solver_yaw_rate_rad_s_ * wheel_x[index];
        const float cosine = std::cos(solver_wheel_steering);
        const float sine = std::sin(solver_wheel_steering);
        const float longitudinal_velocity = cosine * wheel_velocity_x + sine * wheel_velocity_y;
        const float lateral_velocity = -sine * wheel_velocity_x + cosine * wheel_velocity_y;

        float tire_force_x = 0.f;
        float tire_force_y = 0.f;
        if (wheel.in_contact) {
            const float internal_slip_angle =
                std::atan2(lateral_velocity, speed_for_slip);
            wheel.slip_angle =
                simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(
                    internal_slip_angle);
            const float wheel_surface_speed =
                wheel_angular_speed_rad_s_[index] * parameters_.tire_radius_m;
            const float slip_denominator =
                std::max(std::abs(longitudinal_velocity), 1.f);
            wheel.longitudinal_slip = std::clamp(
                (wheel_surface_speed - longitudinal_velocity) / slip_denominator,
                -1.f,
                1.f);
            tire_force_x = parameters_.tire_longitudinal_stiffness_n
                * wheel.longitudinal_slip;
            tire_force_y = std::abs(body_longitudinal_speed_mps_)
                    < parameters_.low_speed_lateral_cutoff_mps
                ? 0.f
                : -parameters_.tire_corner_stiffness_n_rad * internal_slip_angle;

            const float friction_limit = parameters_.tire_friction * wheel.normal_load;
            const float force_magnitude = std::hypot(tire_force_x, tire_force_y);
            if (force_magnitude > friction_limit && force_magnitude > 0.f) {
                const float scale = friction_limit / force_magnitude;
                tire_force_x *= scale;
                tire_force_y *= scale;
            }
        } else {
            wheel.longitudinal_slip = 0.f;
            wheel.slip_angle = 0.f;
        }
        wheel.longitudinal_force = tire_force_x;
        wheel.lateral_force =
            simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(
                tire_force_y);
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
                       * total_normal_load;
    }

    const float longitudinal_accel = total_force_x / parameters_.mass_kg
                                   + solver_lateral_speed_mps_ * solver_yaw_rate_rad_s_;
    const float lateral_accel_body = total_force_y / parameters_.mass_kg
                                  - body_longitudinal_speed_mps_ * solver_yaw_rate_rad_s_;
    body_longitudinal_speed_mps_ += longitudinal_accel * fdt;
    solver_lateral_speed_mps_ += lateral_accel_body * fdt;
    solver_yaw_rate_rad_s_ += total_yaw_moment / parameters_.yaw_inertia_kg_m2 * fdt;

    if (total_brake_force > 0.f) {
        const bool crossed_stop = previous_speed * body_longitudinal_speed_mps_ < 0.f;
        const bool held_at_stop = std::abs(previous_speed) <= STOP_EPSILON;
        if (crossed_stop || held_at_stop) {
            body_longitudinal_speed_mps_ = 0.f;
            // The reduced planar model has no static-contact constraint. Once
            // the brakes hold the chassis at zero longitudinal speed, settle
            // the remaining lateral/yaw modes explicitly instead of allowing
            // an imperceptible but unbounded sideways drift.
            solver_lateral_speed_mps_ = 0.f;
            solver_yaw_rate_rad_s_ = 0.f;
            wheel_angular_speed_rad_s_.fill(0.f);
        }
    }
    if (std::abs(body_longitudinal_speed_mps_) < STOP_EPSILON && in.throttle == 0.f) {
        body_longitudinal_speed_mps_ = 0.f;
    }
    body_longitudinal_speed_mps_ = std::clamp(body_longitudinal_speed_mps_,
        -parameters_.max_reverse_speed_mps, parameters_.max_forward_speed_mps);

    state_.speed = body_longitudinal_speed_mps_;
    state_.accel = (state_.speed - previous_speed) / fdt;
    state_.yaw_rate =
        simcore_host::BodyFrameAdapter::solver_yaw_rate_to_canonical(
            solver_yaw_rate_rad_s_);
    heading_rad_ = normalize_heading(
        heading_rad_
        + simcore_host::BodyFrameAdapter::canonical_yaw_rate_to_heading_rate(
            state_.yaw_rate) * dt);
    state_.heading = static_cast<float>(heading_rad_ * RAD2DEG);

    const double east_velocity = body_longitudinal_speed_mps_ * std::sin(heading_rad_)
                               + solver_lateral_speed_mps_ * std::cos(heading_rad_);
    const double north_velocity = body_longitudinal_speed_mps_ * std::cos(heading_rad_)
                                - solver_lateral_speed_mps_ * std::sin(heading_rad_);
    east_m_ += east_velocity * dt;
    north_m_ += north_velocity * dt;
    state_.east  = east_m_;
    state_.north = north_m_;
    state_.position_enu = {east_m_, north_m_, parameters_.cg_height_m};
    // Refresh the published hit point and normal from the post-integration
    // pose. Spring/damper history advances only in the pre-force sample above.
    update_wheel_contacts(0.f, false);
    state_.linear_velocity_body = {
        body_longitudinal_speed_mps_,
        simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(
            solver_lateral_speed_mps_),
        0.0};
    state_.angular_velocity_body = {
        -roll_rate_rad_s_, pitch_rate_rad_s_, state_.yaw_rate};

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
    state_.roll = static_cast<float>(-roll_rad_ * RAD2DEG);
    state_.pitch = static_cast<float>(pitch_rad_ * RAD2DEG);
    state_.angular_velocity_body = {
        -roll_rate_rad_s_, pitch_rate_rad_s_, state_.yaw_rate};

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

void VehiclePhysics::update_wheel_contacts(float dt_seconds, bool update_suspension)
{
    const double half_wheelbase = parameters_.wheelbase_m * 0.5;
    const std::array<double, 4> wheel_x{
        half_wheelbase, half_wheelbase, -half_wheelbase, -half_wheelbase};
    const std::array<double, 4> wheel_y{
        -parameters_.front_track_m * 0.5, parameters_.front_track_m * 0.5,
        -parameters_.rear_track_m * 0.5, parameters_.rear_track_m * 0.5};

    const double heading_sine = std::sin(heading_rad_);
    const double heading_cosine = std::cos(heading_rad_);
    const double maximum_query_distance = parameters_.tire_radius_m
        + parameters_.suspension.rest_length_m
        + parameters_.suspension.max_extension_m;

    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        auto& wheel = state_.wheels[index];
        const double mount_east = east_m_ + wheel_x[index] * heading_sine
                                + wheel_y[index] * heading_cosine;
        const double mount_north = north_m_ + wheel_x[index] * heading_cosine
                                  - wheel_y[index] * heading_sine;
        const simcore_host::GroundQueryRequest request{
            {mount_east, mount_north, state_.position_enu.z},
            maximum_query_distance};
        const auto hit = ground_query_->query_down(request);

        const bool finite_hit = hit.has_value()
            && std::isfinite(hit->distance_m)
            && hit->distance_m >= 0.0
            && hit->distance_m <= maximum_query_distance
            && std::isfinite(hit->point_enu.east_m)
            && std::isfinite(hit->point_enu.north_m)
            && std::isfinite(hit->point_enu.up_m)
            && std::isfinite(hit->normal_enu.east_m)
            && std::isfinite(hit->normal_enu.north_m)
            && std::isfinite(hit->normal_enu.up_m);
        const double normal_length = finite_hit
            ? std::hypot(
                hit->normal_enu.east_m,
                hit->normal_enu.north_m,
                hit->normal_enu.up_m)
            : 0.0;
        const bool valid_hit = finite_hit
            && normal_length > 1e-9
            && hit->normal_enu.up_m > 0.0;

        wheel.in_contact = valid_hit;
        if (!valid_hit) {
            wheel.contact_point_enu = {};
            wheel.contact_normal_enu = {};
            wheel.normal_load = 0.f;
            wheel.longitudinal_slip = 0.f;
            wheel.slip_angle = 0.f;
            wheel.longitudinal_force = 0.f;
            wheel.lateral_force = 0.f;
            if (update_suspension) {
                suspension_compression_m_[index] =
                    -parameters_.suspension.max_extension_m;
                suspension_base_force_n_[index] = 0.f;
                suspension_had_contact_[index] = false;
            }
            continue;
        }

        wheel.contact_point_enu = {
            hit->point_enu.east_m,
            hit->point_enu.north_m,
            hit->point_enu.up_m};
        wheel.contact_normal_enu = {
            hit->normal_enu.east_m / normal_length,
            hit->normal_enu.north_m / normal_length,
            hit->normal_enu.up_m / normal_length};

        if (update_suspension) {
            const float measured_suspension_length =
                static_cast<float>(hit->distance_m) - parameters_.tire_radius_m;
            const auto suspension = simcore_host::evaluate_suspension(
                parameters_.suspension,
                measured_suspension_length,
                suspension_compression_m_[index],
                suspension_had_contact_[index],
                dt_seconds);
            suspension_compression_m_[index] = suspension.compression_m;
            suspension_base_force_n_[index] = suspension.normal_force_n;
            suspension_had_contact_[index] = true;
            wheel.normal_load = suspension.normal_force_n;
        }
    }
}
