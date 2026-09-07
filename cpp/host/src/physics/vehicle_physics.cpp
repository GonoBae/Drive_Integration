#include "physics/vehicle_physics.hpp"
#include "physics/motorcycle_balance.hpp"

#include "coordinates/body_frame_adapter.hpp"
#include "physics/chassis_ground_contact.hpp"
#include "physics/runtime_tire_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace {

constexpr double DEG2RAD = std::numbers::pi_v<double> / 180.0;
constexpr double RAD2DEG = 180.0 / std::numbers::pi_v<double>;
constexpr float GRAVITY_MPS2 = 9.80665f;
constexpr float MIN_CONTACT_NORMAL_LOAD_N = 0.001f;
// Shared by the velocity constraint and the next-step tire support estimate.
// A ground ray alone is not evidence of a loaded compression stop.
constexpr double HARD_STOP_ACTIVATION_SLOP_M = 1e-4;
// A downward-only ray that starts exactly at the suspension mount cannot
// recover once a fast-rising surface is a few centimeters above the reduced
// model's lagging chassis pose. Lift the probe while preserving the actual
// mount-to-ground distance used by the suspension calculation.
// The probe must start above the terrain even while the chassis attitude still
// lags a newly encountered steep grade. Four metres covers the full vehicle
// footprint at the exporter-supported slopes and lets the non-penetration
// constraint recover before a wheel ray is lost.
constexpr double GROUND_PENETRATION_RECOVERY_M = 4.0;
// Coverage detection must remain independent from suspension droop. During a
// flat-to-steep transition the front constraint can lift the chassis while the
// rear ground is several metres below the raised query origin. A deeper ray
// still reports the authored surface; suspension contact remains bounded by
// its own maximum extension below.
constexpr double GROUND_QUERY_DEPTH_M = 20.0;
// The exporter and reduced suspension model are intended for drivable ground,
// not near-vertical walls. Bound a discontinuous four-wheel support fit before
// it can rotate the chassis basis into a singular ground-query pose.
constexpr double MAX_WHEEL_SUPPORT_ATTITUDE_RAD = 60.0 * DEG2RAD;
constexpr double COLLISION_BODY_GROUND_CLEARANCE_M = 0.10;
// Below roughly a 6 km/h rigid-body delta-v, contacts are treated as ordinary
// parking/curb loads. Stronger impacts accumulate visible body damage.
constexpr double DAMAGE_IMPULSE_THRESHOLD_N_S = 2500.0;
constexpr double DAMAGE_IMPULSE_PER_PERCENT_N_S = 500.0;
// A forward-driven rigid wheel can climb a square step only while the contact
// corner is below its centre (rise < radius). The R1 curb contract is narrower:
// authored curb rise and continuous support behind it may be at most 75% of R.
constexpr double MAX_SUPPORTED_CURB_RISE_TIRE_RADIUS_FRACTION = 0.75;
constexpr double CURB_SUPPORT_MIN_RISE_M = 0.02;
constexpr double CURB_SUPPORT_RASTER_HEIGHT_TOLERANCE_TIRE_RADIUS_FRACTION = 0.30;
// The near pair represents the wheel's immediate step transition. A second
// pair three tire radii from the curb centre verifies the raised support that
// the suspension is actually about to occupy. Keeping these roles separate
// prevents a narrow curb-top sample from certifying a missing sidewalk.
constexpr double CURB_FAR_SUPPORT_TIRE_RADIUS_MULTIPLIER = 3.0;

struct GroundBasis {
    // ENU unit vectors for the vehicle's terrain-tangent solver axes.
    std::array<double, 3> forward{};
    std::array<double, 3> right{};
    std::array<double, 3> normal{};
};

struct BodyBasis {
    // ENU unit vectors for canonical vehicle coordinates. wheel_x is forward,
    // wheel_y is right, and suspension travel follows the body-up axis.
    std::array<double, 3> forward{};
    std::array<double, 3> right{};
    std::array<double, 3> up{};
};

std::array<double, 3> cross_product(
    const std::array<double, 3>& lhs,
    const std::array<double, 3>& rhs)
{
    return {
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0]};
}

double dot_product(
    const std::array<double, 3>& lhs,
    const std::array<double, 3>& rhs)
{
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

std::array<float, 2> integrate_body_planar_velocity(
    float forward_speed, float lateral_speed, float yaw_rate,
    float forward_force, float lateral_force, float mass, double dt)
{
    // Freeze the sampled forces and yaw rate for this first-order step, but
    // integrate the rotating-frame term exactly. Explicit Euler would add
    // kinetic energy even with zero force: |v_next|^2=|v|^2*(1+(r*dt)^2).
    // This rotation preserves speed when force-free; it neither supplies grip
    // nor alters the tire-generated yaw moment or the steering request.
    const double angle = static_cast<double>(yaw_rate) * dt;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double angle_squared = angle * angle;
    const double sinc = std::abs(angle) < 1e-5
        ? 1.0 - angle_squared / 6.0 + angle_squared * angle_squared / 120.0
        : sine / angle;
    const double cosc = std::abs(angle) < 1e-5
        ? angle * (0.5 - angle_squared / 24.0 + angle_squared * angle_squared / 720.0)
        : (1.0 - cosine) / angle;
    const double forward_acceleration = static_cast<double>(forward_force) / mass;
    const double lateral_acceleration = static_cast<double>(lateral_force) / mass;
    return {
        static_cast<float>(cosine * forward_speed + sine * lateral_speed
            + dt * (sinc * forward_acceleration + cosc * lateral_acceleration)),
        static_cast<float>(-sine * forward_speed + cosine * lateral_speed
            + dt * (-cosc * forward_acceleration + sinc * lateral_acceleration))};
}

std::array<double, 3> make_horizontal_right(double heading_rad)
{
    return {std::cos(heading_rad), -std::sin(heading_rad), 0.0};
}

float generalized_pitch_inertia(
    const VehicleParameters& parameters, float roll_rad)
{
    // Pitch is an intrinsic Euler rotation about horizontal-right, not the
    // already-rolled body-right axis. Expressing that axis in body principal
    // coordinates gives {0, cos(roll), sin(roll)}.
    const float cosine = std::cos(roll_rad);
    const float sine = std::sin(roll_rad);
    return parameters.pitch_inertia_kg_m2 * cosine * cosine
        + parameters.yaw_inertia_kg_m2 * sine * sine;
}

struct GeneralizedAttitudeAcceleration {
    float roll_rad_s2 = 0.f;
    float pitch_rad_s2 = 0.f;
};

GeneralizedAttitudeAcceleration solve_generalized_attitude_acceleration(
    const VehicleParameters& parameters,
    float pitch_rad,
    float roll_rad,
    float pitch_rate_rad_s,
    float roll_rate_rad_s,
    float canonical_navigation_yaw_rate_rad_s,
    float canonical_navigation_yaw_accel_rad_s2,
    float roll_generalized_moment_n_m,
    float pitch_generalized_moment_n_m)
{
    // Exact yaw->pitch->roll Euler dynamics for roll/pitch, conditional on the
    // yaw trajectory prescribed by the planar tire/collision solver. E maps
    // qdot={roll,pitch,canonical yaw} to the true RH-FLU body omega. Keeping
    // E-dot and gyroscopic bias terms avoids the previous body-axis/small-angle
    // mixture on compound terrain. Drivable terrain remains below the Euler
    // pitch singularity (the exporter/support contract is <= 60 degrees).
    const double pitch_sine = std::sin(pitch_rad);
    const double pitch_cosine = std::cos(pitch_rad);
    const double roll_sine = std::sin(roll_rad);
    const double roll_cosine = std::cos(roll_rad);
    const double pitch_rate = pitch_rate_rad_s;
    const double roll_rate = roll_rate_rad_s;
    const double yaw_rate = canonical_navigation_yaw_rate_rad_s;

    const std::array<double, 3> omega{
        roll_rate + yaw_rate * pitch_sine,
        -pitch_rate * roll_cosine
            + yaw_rate * roll_sine * pitch_cosine,
        pitch_rate * roll_sine
            + yaw_rate * roll_cosine * pitch_cosine};
    const std::array<double, 3> e_dot_q_dot{
        yaw_rate * pitch_rate * pitch_cosine,
        pitch_rate * roll_rate * roll_sine
            + yaw_rate * (roll_rate * roll_cosine * pitch_cosine
                - pitch_rate * roll_sine * pitch_sine),
        pitch_rate * roll_rate * roll_cosine
            + yaw_rate * (-roll_rate * roll_sine * pitch_cosine
                - pitch_rate * roll_cosine * pitch_sine)};
    const std::array<double, 3> angular_momentum{
        parameters.roll_inertia_kg_m2 * omega[0],
        parameters.pitch_inertia_kg_m2 * omega[1],
        parameters.yaw_inertia_kg_m2 * omega[2]};
    const auto gyroscopic = cross_product(omega, angular_momentum);
    const std::array<double, 3> body_bias{
        parameters.roll_inertia_kg_m2 * e_dot_q_dot[0] + gyroscopic[0],
        parameters.pitch_inertia_kg_m2 * e_dot_q_dot[1] + gyroscopic[1],
        parameters.yaw_inertia_kg_m2 * e_dot_q_dot[2] + gyroscopic[2]};
    const double roll_bias = body_bias[0];
    const double pitch_bias =
        -roll_cosine * body_bias[1] + roll_sine * body_bias[2];
    const double roll_yaw_mass =
        parameters.roll_inertia_kg_m2 * pitch_sine;
    const double pitch_yaw_mass =
        (parameters.yaw_inertia_kg_m2 - parameters.pitch_inertia_kg_m2)
        * roll_sine * roll_cosine * pitch_cosine;
    const double pitch_mass = generalized_pitch_inertia(parameters, roll_rad);

    return {
        static_cast<float>((roll_generalized_moment_n_m - roll_bias
            - roll_yaw_mass * canonical_navigation_yaw_accel_rad_s2)
            / parameters.roll_inertia_kg_m2),
        static_cast<float>((pitch_generalized_moment_n_m - pitch_bias
            - pitch_yaw_mass * canonical_navigation_yaw_accel_rad_s2)
            / pitch_mass)};
}

BodyBasis make_body_basis(
    double heading_rad, double pitch_rad, double roll_rad)
{
    const double heading_sine = std::sin(heading_rad);
    const double heading_cosine = std::cos(heading_rad);
    const double pitch_sine = std::sin(pitch_rad);
    const double pitch_cosine = std::cos(pitch_rad);
    const double roll_sine = std::sin(roll_rad);
    const double roll_cosine = std::cos(roll_rad);

    const std::array<double, 3> horizontal_forward{
        heading_sine, heading_cosine, 0.0};
    const std::array<double, 3> horizontal_right{
        heading_cosine, -heading_sine, 0.0};
    const std::array<double, 3> pitch_up{
        -pitch_sine * horizontal_forward[0],
        -pitch_sine * horizontal_forward[1],
        pitch_cosine};

    BodyBasis basis;
    basis.forward = {
        pitch_cosine * horizontal_forward[0],
        pitch_cosine * horizontal_forward[1],
        pitch_sine};
    basis.right = {
        roll_cosine * horizontal_right[0] - roll_sine * pitch_up[0],
        roll_cosine * horizontal_right[1] - roll_sine * pitch_up[1],
        -roll_sine * pitch_up[2]};
    basis.up = {
        roll_sine * horizontal_right[0] + roll_cosine * pitch_up[0],
        roll_sine * horizontal_right[1] + roll_cosine * pitch_up[1],
        roll_cosine * pitch_up[2]};
    return basis;
}

Vector3State make_body_angular_velocity(
    float pitch_rad,
    float roll_rad,
    float pitch_rate_rad_s,
    float roll_rate_rad_s,
    float canonical_navigation_yaw_rate_rad_s)
{
    // VehiclePhysics integrates yaw, pitch, and roll as Euler generalized
    // rates, while the public contract is a true right-handed FLU angular
    // velocity vector. Include the compound-attitude coupling instead of
    // publishing the small-angle {roll_dot,-pitch_dot,yaw_dot} shortcut.
    const float pitch_sine = std::sin(pitch_rad);
    const float pitch_cosine = std::cos(pitch_rad);
    const float roll_sine = std::sin(roll_rad);
    const float roll_cosine = std::cos(roll_rad);
    return {
        roll_rate_rad_s
            + canonical_navigation_yaw_rate_rad_s * pitch_sine,
        -pitch_rate_rad_s * roll_cosine
            + canonical_navigation_yaw_rate_rad_s
                * roll_sine * pitch_cosine,
        pitch_rate_rad_s * roll_sine
            + canonical_navigation_yaw_rate_rad_s
                * roll_cosine * pitch_cosine};
}

struct WheelSupportPoint {
    double forward_m = 0.0;
    double right_m = 0.0;
    double up_m = 0.0;
};

bool fit_wheel_support_grades(
    const std::array<WheelSupportPoint, 4>& points,
    std::size_t point_count,
    double prior_forward_grade,
    double prior_right_grade,
    double& out_forward_grade,
    double& out_right_grade)
{
    if (point_count < 2) {
        return false;
    }

    double mean_forward = 0.0;
    double mean_right = 0.0;
    double mean_up = 0.0;
    for (std::size_t index = 0; index < point_count; ++index) {
        mean_forward += points[index].forward_m;
        mean_right += points[index].right_m;
        mean_up += points[index].up_m;
    }
    const double inverse_count = 1.0 / static_cast<double>(point_count);
    mean_forward *= inverse_count;
    mean_right *= inverse_count;
    mean_up *= inverse_count;

    double forward_forward = 0.0;
    double right_right = 0.0;
    double forward_right = 0.0;
    double forward_up = 0.0;
    double right_up = 0.0;
    for (std::size_t index = 0; index < point_count; ++index) {
        const double forward = points[index].forward_m - mean_forward;
        const double right = points[index].right_m - mean_right;
        const double up = points[index].up_m - mean_up;
        forward_forward += forward * forward;
        right_right += right * right;
        forward_right += forward * right;
        forward_up += forward * up;
        right_up += right * up;
    }

    // A same-axle or same-side contact pair constrains only one slope axis.
    // A small Tikhonov prior keeps the unobservable axis at the local-normal
    // estimate while remaining negligible for a full wheelbase/track sample.
    constexpr double support_regularization_m2 = 1e-4;
    forward_forward += support_regularization_m2;
    right_right += support_regularization_m2;
    forward_up += support_regularization_m2 * prior_forward_grade;
    right_up += support_regularization_m2 * prior_right_grade;
    const double determinant =
        forward_forward * right_right - forward_right * forward_right;
    if (!std::isfinite(determinant) || determinant <= 1e-12) {
        return false;
    }

    const double forward_grade =
        (forward_up * right_right - right_up * forward_right)
        / determinant;
    const double right_grade =
        (right_up * forward_forward - forward_up * forward_right)
        / determinant;
    if (!std::isfinite(forward_grade) || !std::isfinite(right_grade)) {
        return false;
    }

    const double maximum_grade = std::tan(MAX_WHEEL_SUPPORT_ATTITUDE_RAD);
    out_forward_grade = std::clamp(
        forward_grade, -maximum_grade, maximum_grade);
    out_right_grade = std::clamp(
        right_grade, -maximum_grade, maximum_grade);
    return true;
}

GroundBasis make_ground_basis_from_normal(
    double heading_rad, const std::array<double, 3>& surface_normal)
{
    const double heading_sine = std::sin(heading_rad);
    const double heading_cosine = std::cos(heading_rad);

    const double normal_length = std::hypot(
        surface_normal[0], surface_normal[1], surface_normal[2]);
    GroundBasis basis;
    for (std::size_t axis = 0; axis < basis.normal.size(); ++axis) {
        basis.normal[axis] = surface_normal[axis] / normal_length;
    }

    // Project the navigation-heading vector onto the fixed ENU surface plane.
    // Collision may change yaw, but it must never rotate the authored terrain
    // normal along with the vehicle.
    const std::array<double, 3> horizontal_forward{
        heading_sine, heading_cosine, 0.0};
    const double forward_normal_dot =
        horizontal_forward[0] * basis.normal[0]
        + horizontal_forward[1] * basis.normal[1];
    std::array<double, 3> projected_forward{
        horizontal_forward[0] - forward_normal_dot * basis.normal[0],
        horizontal_forward[1] - forward_normal_dot * basis.normal[1],
        -forward_normal_dot * basis.normal[2]};
    const double forward_length = std::hypot(
        projected_forward[0], projected_forward[1], projected_forward[2]);
    for (std::size_t axis = 0; axis < projected_forward.size(); ++axis) {
        basis.forward[axis] = projected_forward[axis] / forward_length;
    }

    // forward x normal points toward vehicle-right in the ENU frame.
    basis.right = {
        basis.forward[1] * basis.normal[2]
            - basis.forward[2] * basis.normal[1],
        basis.forward[2] * basis.normal[0]
            - basis.forward[0] * basis.normal[2],
        basis.forward[0] * basis.normal[1]
            - basis.forward[1] * basis.normal[0]};
    return basis;
}

GroundBasis make_ground_basis(
    double heading_rad, float ground_pitch_rad, float ground_roll_rad)
{
    const double heading_sine = std::sin(heading_rad);
    const double heading_cosine = std::cos(heading_rad);
    const double forward_grade = std::tan(ground_pitch_rad);
    const double left_grade = std::tan(ground_roll_rad);

    // Reconstruct the surface gradient in ENU from the forward/left grades.
    const double east_grade = forward_grade * heading_sine
                            - left_grade * heading_cosine;
    const double north_grade = forward_grade * heading_cosine
                             + left_grade * heading_sine;
    return make_ground_basis_from_normal(
        heading_rad, {-east_grade, -north_grade, 1.0});
}

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

double current_unix_time_seconds()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration<double>(now.time_since_epoch()).count();
}

float surface_profile_scale(
    const VehicleParameters& parameters,
    simcore_host::GroundSurfaceMaterialId material_id)
{
    switch (material_id) {
    case simcore_host::GroundSurfaceMaterialId::Asphalt:
        return parameters.surface_asphalt_friction_scale;
    case simcore_host::GroundSurfaceMaterialId::LowFriction:
        return parameters.surface_low_friction_scale;
    case simcore_host::GroundSurfaceMaterialId::Rough:
        return parameters.surface_rough_friction_scale;
    case simcore_host::GroundSurfaceMaterialId::Default:
        return parameters.surface_default_friction_scale;
    }
    return parameters.surface_default_friction_scale;
}

float effective_surface_friction_multiplier(
    const VehicleParameters& parameters,
    simcore_host::GroundSurfaceMaterialId material_id,
    float authored_multiplier)
{
    const float safe_authored_multiplier =
        std::isfinite(authored_multiplier) && authored_multiplier > 0.f
        ? authored_multiplier
        : 1.f;
    return safe_authored_multiplier
        * surface_profile_scale(parameters, material_id);
}

} // namespace

VehiclePhysics::VehiclePhysics(double lat, double lon, double alt, float heading,
                               VehicleParameters parameters,
                               std::shared_ptr<const simcore_host::GroundQuery> ground_query,
                               std::shared_ptr<const simcore_host::CollisionWorld> collision_world)
    : parameters_(parameters)
    , ground_query_(std::move(ground_query))
    , collision_world_(std::move(collision_world))
    , origin_lat_(lat)
    , origin_lon_(lon)
    , origin_alt_(alt)
    , spawn_heading_rad_(normalize_heading(heading * DEG2RAD))
    , heading_rad_(spawn_heading_rad_)
{
    if (!valid_vehicle_parameters(parameters_)) {
        throw std::invalid_argument("Vehicle parameters are invalid");
    }

    if (!std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(alt) ||
        !std::isfinite(heading)) {
        throw std::invalid_argument("Initial vehicle pose must be finite");
    }

    if (!ground_query_) {
        ground_query_ = std::make_shared<simcore_host::FlatGroundQuery>();
    }
    reset();
}

void VehiclePhysics::reset()
{
    {
        std::lock_guard lock(input_mutex_);
        input_ = VehicleInput{};
    }

    state_ = VehicleState{};
    state_.timestamp = current_unix_time_seconds();
    east_m_ = 0.0;
    north_m_ = 0.0;
    heading_rad_ = spawn_heading_rad_;
    body_longitudinal_speed_mps_ = 0.f;
    solver_lateral_speed_mps_ = 0.f;
    solver_yaw_rate_rad_s_ = 0.f;
    solver_road_wheel_angle_rad_ = 0.f;
    pitch_rad_ = 0.f;
    roll_rad_ = 0.f;
    pitch_rate_rad_s_ = 0.f;
    roll_rate_rad_s_ = 0.f;
    vertical_speed_mps_ = 0.f;
    ground_pitch_rad_ = 0.f;
    ground_roll_rad_ = 0.f;
    support_pitch_rad_ = 0.f;
    support_roll_rad_ = 0.f;
    support_attitude_valid_ = false;
    motorcycle_rider_attached_ = true;
    applied_drive_force_n_ = 0.f;
    wheel_angular_speed_rad_s_.fill(0.f);
    suspension_compression_m_.fill(0.f);
    suspension_base_force_n_.fill(0.f);
    suspension_had_contact_.fill(false);
    wheel_contact_support_ = {};
    previous_hard_stop_normal_force_n_.fill(0.f);
    compression_stop_active_.fill(false);
    ground_query_hit_count_ = 0;
    ground_surface_hit_by_wheel_.fill(false);
    ground_surface_material_by_wheel_.fill(
        simcore_host::GroundSurfaceMaterialId::Default);
    ground_friction_multiplier_by_wheel_.fill(1.f);
    last_resolved_dynamic_proxies_.clear();
    last_collision_contacts_.clear();
    last_runtime_proxy_contacts_.clear();

    runtime_tire_supports_.clear();

    state_.lat = origin_lat_;
    state_.lon = origin_lon_;
    state_.alt = origin_alt_;
    state_.heading = static_cast<float>(heading_rad_ * RAD2DEG);
    state_.rpm = parameters_.idle_rpm;
    state_.position_enu = {0.0, 0.0, parameters_.cg_height_m};
    state_.collision_half_length_m = static_cast<float>(
        parameters_.wheelbase_m * 0.5 + parameters_.collision_body_overhang_m);
    state_.collision_half_width_m = static_cast<float>(
        std::max(parameters_.front_track_m, parameters_.rear_track_m) * 0.5
        + parameters_.collision_body_side_padding_m);
    state_.collision_half_height_m = static_cast<float>(
        parameters_.collision_body_half_height_m);
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        auto& wheel = state_.wheels[index];
        wheel.wheel_index = static_cast<std::uint32_t>(index);
    }

    // Discover the authored ground attitude before enforcing suspension
    // clearance. Lifting a still-level chassis first makes its footprint grow
    // across a slope and leaves the reset pose visibly hovering.
    update_wheel_contacts(0.f, false, false, true);
    pitch_rad_ = support_pitch_rad_;
    roll_rad_ = support_roll_rad_;

    // cg_height_m is a clearance measured along the supporting normal. Place
    // the reset reference at that clearance so a planar slope starts at the
    // same suspension compression as flat ground, without a multi-frame heave
    // transient after every PIE restart.
    const simcore_host::GroundQueryRequest centre_request{
        {east_m_, north_m_, state_.position_enu.z
            + GROUND_PENETRATION_RECOVERY_M},
        GROUND_QUERY_DEPTH_M + GROUND_PENETRATION_RECOVERY_M};
    if (const auto centre_hit = ground_query_->query_down(centre_request)) {
        const double normal_length = std::hypot(
            centre_hit->normal_enu.east_m,
            centre_hit->normal_enu.north_m,
            centre_hit->normal_enu.up_m);
        const double normal_up = normal_length > 1e-9
            ? centre_hit->normal_enu.up_m / normal_length
            : 0.0;
        if (std::isfinite(centre_hit->point_enu.up_m)
            && std::isfinite(normal_up) && normal_up > 1e-6) {
            state_.position_enu.z = centre_hit->point_enu.up_m
                + parameters_.cg_height_m / normal_up;
        }
    }

    // The discovery pass must not leak level-body contact history into the
    // aligned suspension sample.
    state_.wheels = {};
    wheel_angular_speed_rad_s_.fill(0.f);
    suspension_compression_m_.fill(0.f);
    suspension_base_force_n_.fill(0.f);
    suspension_had_contact_.fill(false);
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        state_.wheels[index].wheel_index = static_cast<std::uint32_t>(index);
    }

    resolve_non_penetrating_ground_contacts(true);
    state_.pitch = static_cast<float>(pitch_rad_ * RAD2DEG);
    state_.roll = static_cast<float>(roll_rad_ * RAD2DEG);
}

void VehiclePhysics::replace_environment(
    std::shared_ptr<const simcore_host::GroundQuery> ground_query,
    std::shared_ptr<const simcore_host::CollisionWorld> collision_world)
{
    if (!ground_query) {
        throw std::invalid_argument(
            "Vehicle environment requires an authoritative ground query");
    }
    ground_query_ = std::move(ground_query);
    collision_world_ = std::move(collision_world);
    reset();
}

void VehiclePhysics::replace_parameters(VehicleParameters parameters)
{
    if (!valid_vehicle_parameters(parameters)) {
        throw std::invalid_argument("Vehicle parameters are invalid");
    }
    parameters_ = std::move(parameters);
    reset();
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
    return update(dt, {});
}

void VehiclePhysics::refresh_runtime_tire_supports(
    const std::vector<simcore_host::KinematicCollisionProxy>& proxies)
{
    runtime_tire_supports_.clear();
    for (const auto& proxy : proxies) {
        if (!simcore_host::is_low_tire_obstacle(proxy, *ground_query_, parameters_.tire_radius_m)) continue;
        const auto& shape = std::get<simcore_host::ObbPrism>(proxy.shape);
        const double lateral = (shape.center_enu.east_m-east_m_)*std::cos(heading_rad_)
            - (shape.center_enu.north_m-north_m_)*std::sin(heading_rad_);
        const double angle = shape.heading_rad-heading_rad_;
        const double half_span = shape.half_length_m*std::abs(std::sin(angle))
            + shape.half_width_m*std::abs(std::cos(angle));
        const double half_track = parameters_.single_track ? 0.0
            : std::min(parameters_.front_track_m,parameters_.rear_track_m)*.5;
        const bool reaches_tires = std::abs(std::abs(lateral)-half_track) <= half_span+.12;
        // If both tire tracks straddle an object taller than the actual belly
        // clearance, preserve its collision. A low rounded label alone is not
        // permission for a torso to pass through the underside of the chassis.
        const double belly_clearance = parameters_.cg_height_m
            + parameters_.chassis_shell_center_up_offset_m
            - parameters_.chassis_shell_half_height_m;
        if (reaches_tires || 2.0*shape.half_height_m+.06 <= belly_clearance+1e-6)
            runtime_tire_supports_.push_back(proxy);
    }
}

VehicleState VehiclePhysics::update(
    double dt,
    std::vector<simcore_host::KinematicCollisionProxy> dynamic_proxies)
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
    refresh_runtime_tire_supports(dynamic_proxies);
    last_resolved_dynamic_proxies_.clear();
    last_collision_contacts_.clear();
    last_runtime_proxy_contacts_.clear();
    std::vector<simcore_host::KinematicCollisionProxy>
        accepted_resolved_dynamic_proxies;
    std::vector<simcore_host::CollisionContact> accepted_collision_contacts;
    std::vector<simcore_host::RuntimeProxyContact> accepted_runtime_proxy_contacts;
    std::array<double, 4> tick_hard_stop_impulses{};
    wheel_contact_support_ = {};
    const auto previous_hard_stop_normal_force_n =
        previous_hard_stop_normal_force_n_;
    // Commit a new estimate only after the complete step is accepted. A
    // coverage rollback or an exception must not retain an older reaction.
    previous_hard_stop_normal_force_n_.fill(0.f);

    // R1 is a ground-bound vehicle model: it has no airborne, cliff, or jump
    // semantics. Keep the last published supported pose for this step so a
    // missing MapPackage cell/boundary fails closed instead of letting the
    // server integrate an endless fall below the Unreal world.
    const VehicleState step_start_state = state_;
    const double step_start_east_m = east_m_;
    const double step_start_north_m = north_m_;
    const double step_start_heading_rad = heading_rad_;
    const float step_start_pitch_rad = pitch_rad_;
    const float step_start_roll_rad = roll_rad_;
    const float step_start_ground_pitch_rad = ground_pitch_rad_;
    const float step_start_ground_roll_rad = ground_roll_rad_;
    const float step_start_support_pitch_rad = support_pitch_rad_;
    const float step_start_support_roll_rad = support_roll_rad_;
    const bool step_start_support_attitude_valid = support_attitude_valid_;
    const bool step_start_rider_attached = motorcycle_rider_attached_;
    const auto step_start_suspension_compression = suspension_compression_m_;
    const auto step_start_suspension_force = suspension_base_force_n_;
    const auto step_start_suspension_contact = suspension_had_contact_;

    // Timestamp
    const double update_timestamp = current_unix_time_seconds();
    state_.timestamp = update_timestamp;

    // Sample the authoritative ground at the current pose before tire forces
    // are evaluated. The constructor seeds this state so the first update also
    // starts with a complete contact snapshot.
    update_wheel_contacts(fdt, true, false, false);
    // Each corner is an independent wheel + suspension support.  Project the
    // spring/damper reaction onto that corner's own ground normal instead of
    // multiplying the summed load by one averaged terrain alignment.  This is
    // important on ramps, cross-slopes, and diagonal bumps where the four
    // contact normals need not be identical.
    const BodyBasis force_sample_body_basis = make_body_basis(
        heading_rad_, pitch_rad_, roll_rad_);
    std::array<float, 4> suspension_normal_force_n{};
    float suspension_heave_force_n = 0.f;
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        const auto& wheel = state_.wheels[index];
        if (!wheel.in_contact) {
            continue;
        }
        const double body_up_normal_dot =
            force_sample_body_basis.up[0] * wheel.contact_normal_enu.x
            + force_sample_body_basis.up[1] * wheel.contact_normal_enu.y
            + force_sample_body_basis.up[2] * wheel.contact_normal_enu.z;
        suspension_normal_force_n[index] = suspension_base_force_n_[index]
            * static_cast<float>(std::max(0.0, body_up_normal_dot));
        wheel_contact_support_[index].suspension_normal_force_n =
            suspension_normal_force_n[index];
        if (compression_stop_active_[index]) {
            wheel_contact_support_[index].tire_hard_stop_normal_force_n =
                previous_hard_stop_normal_force_n[index];
        }
        suspension_heave_force_n += suspension_normal_force_n[index];
    }

    // --- Four-wheel planar contact and tire forces ---
    const float previous_speed = body_longitudinal_speed_mps_;
    const float gear_sign = gear_direction(in.gear);
    const float configured_drive_force = in.gear == VehicleGear::Reverse
        ? parameters_.max_reverse_force_n
        : parameters_.max_drive_force_n;
    // A force-only driveline produces unbounded power as road speed rises.
    // Preserve launch torque while capping high-speed power to the configured
    // sedan output.
    const float power_limited_drive_force = parameters_.max_drive_power_w
        / std::max(std::abs(body_longitudinal_speed_mps_), 1.f);
    const float available_drive_force = std::min(
        configured_drive_force, power_limited_drive_force);
    const bool forward_limiter_active = gear_sign > 0.f
        && body_longitudinal_speed_mps_ >= parameters_.max_forward_speed_mps;
    const bool reverse_limiter_active = gear_sign < 0.f
        && body_longitudinal_speed_mps_ <= -parameters_.max_reverse_speed_mps;
    // Clamping chassis speed without cutting drive torque lets wheel angular
    // speed wind up forever at the limiter. Remove only the torque that would
    // accelerate farther beyond the configured speed; tire reaction then
    // brings wheel surface speed back toward road speed.
    const bool drive_force_suppressed = forward_limiter_active
        || reverse_limiter_active
        || in.brake > 0.f;
    const float desired_drive_force = drive_force_suppressed
        ? 0.f
        : gear_sign * in.throttle * available_drive_force;
    // Model the finite response of a real powertrain. In particular, a digital
    // keyboard press must not apply the full axle force as a one-frame impulse.
    // A direction change first sheds the old-direction force before building
    // torque in the new direction.
    if (applied_drive_force_n_ * desired_drive_force < 0.f) {
        const float force_step = parameters_.drive_force_fall_rate_n_per_s * fdt;
        applied_drive_force_n_ -= std::copysign(
            std::min(std::abs(applied_drive_force_n_), force_step),
            applied_drive_force_n_);
    } else {
        const bool increasing_force =
            std::abs(desired_drive_force) > std::abs(applied_drive_force_n_);
        const float response_rate = increasing_force
            ? parameters_.drive_force_rise_rate_n_per_s
            : parameters_.drive_force_fall_rate_n_per_s;
        const float force_step = response_rate * fdt;
        applied_drive_force_n_ += std::clamp(
            desired_drive_force - applied_drive_force_n_,
            -force_step,
            force_step);
    }
    const float total_drive_force = applied_drive_force_n_;
    const float service_brake_force = in.brake * parameters_.max_service_brake_n;
    const float total_brake_force = in.handbrake
        ? std::max(service_brake_force, parameters_.max_handbrake_force_n)
        : service_brake_force;
    // The input requests a road-wheel angle, independent of vehicle speed.
    // Move the rack at a finite rate, then apply Ackermann geometry below.
    // Tire slip, normal loads and friction determine the resulting turn;
    // no speed-sensitive input assist silently reduces the requested angle.
    const float steering_target_rad =
        simcore_host::BodyFrameAdapter::canonical_steering_to_solver(in.steering)
        * parameters_.max_steering_angle_rad;
    const bool steering_is_returning =
        solver_road_wheel_angle_rad_ * steering_target_rad <= 0.f
        || std::abs(steering_target_rad) < std::abs(solver_road_wheel_angle_rad_);
    const float steering_rate_rad_s = steering_is_returning
        ? parameters_.steering_return_rate_rad_s
        : parameters_.steering_rate_rad_s;
    const float maximum_steering_step_rad = steering_rate_rad_s * fdt;
    solver_road_wheel_angle_rad_ += std::clamp(
        steering_target_rad - solver_road_wheel_angle_rad_,
        -maximum_steering_step_rad,
        maximum_steering_step_rad);
    const float solver_steering_angle = solver_road_wheel_angle_rad_;
    state_.steering_angle =
        simcore_host::BodyFrameAdapter::solver_steering_to_canonical(
            solver_steering_angle);
    // The configured static axle split locates the CG between the axles. A
    // front-heavy sedan has a shorter CG-to-front arm and a longer rear arm.
    const float cg_to_rear_axle_m = parameters_.wheelbase_m
        * parameters_.front_static_load_fraction;
    const float cg_to_front_axle_m = parameters_.wheelbase_m
        - cg_to_rear_axle_m;
    const std::array<float, 4> wheel_x{
        cg_to_front_axle_m, cg_to_front_axle_m,
        -cg_to_rear_axle_m, -cg_to_rear_axle_m};
    const float contact_width_scale = parameters_.single_track ? 0.f : 0.5f;
    const std::array<float, 4> wheel_y{-parameters_.front_track_m * contact_width_scale,
                                       parameters_.front_track_m * contact_width_scale,
                                      -parameters_.rear_track_m * contact_width_scale,
                                       parameters_.rear_track_m * contact_width_scale};

    // The body is supported at four suspension mounts, not directly by an
    // averaged terrain normal. Unequal spring/damper reactions create the
    // physical moments that make the chassis follow front/rear and left/right
    // wheel height differences. wheel_y is solver-right-positive, whereas
    // positive roll raises the canonical left side. Pitch is an intrinsic
    // rotation about horizontal-right; a rolled body therefore contributes
    // cos(roll) of a body-right suspension torque to that generalized axis.
    float suspension_roll_moment_n_m = 0.f;
    float suspension_pitch_moment_n_m = 0.f;
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        if (!state_.wheels[index].in_contact) {
            continue;
        }
        const float suspension_force = suspension_base_force_n_[index];
        suspension_roll_moment_n_m -= wheel_y[index] * suspension_force;
        suspension_pitch_moment_n_m += wheel_x[index] * suspension_force
            * std::cos(roll_rad_);
    }

    // Ackermann geometry: both front tires point toward the same instantaneous
    // turn centre, so the inside wheel steers farther than the outside wheel.
    std::array<float, 4> solver_wheel_steering_angles{};
    if (std::abs(solver_steering_angle) > 1e-6f) {
        const float steering_sign = std::copysign(1.f, solver_steering_angle);
        const float centre_radius = parameters_.wheelbase_m
            / std::abs(std::tan(solver_steering_angle));
        for (std::size_t index = 0; index < 2; ++index) {
            const float wheel_radius = std::max(
                0.05f, centre_radius - steering_sign * wheel_y[index]);
            solver_wheel_steering_angles[index] = steering_sign
                * std::atan(parameters_.wheelbase_m / wheel_radius);
        }
    }

    float total_force_x = 0.f;
    float total_force_y = 0.f;
    float total_yaw_moment = 0.f;
    float total_normal_load = 0.f;

    const GroundBasis ground_basis = make_ground_basis(
        heading_rad_, ground_pitch_rad_, ground_roll_rad_);
    const float gravity_force_x = -parameters_.mass_kg * GRAVITY_MPS2
        * static_cast<float>(ground_basis.forward[2]);
    const float gravity_force_y = -parameters_.mass_kg * GRAVITY_MPS2
        * static_cast<float>(ground_basis.right[2]);
    std::array<GroundBasis, 4> wheel_ground_bases{};
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        if (state_.wheels[index].in_contact) {
            wheel_ground_bases[index] = make_ground_basis_from_normal(
                heading_rad_,
                {state_.wheels[index].contact_normal_enu.x,
                 state_.wheels[index].contact_normal_enu.y,
                 state_.wheels[index].contact_normal_enu.z});
        } else {
            wheel_ground_bases[index] = ground_basis;
        }
    }
    std::array<float, 4> wheel_longitudinal_velocity{};
    std::array<float, 4> wheel_lateral_velocity{};
    std::array<float, 4> wheel_cosine{};
    std::array<float, 4> wheel_sine{};
    std::array<float, 4> tire_force_y_by_wheel{};
    std::array<float, 4> unconstrained_tire_force_y_by_wheel{};
    std::array<float, 4> tire_friction_limit_by_wheel{};
    std::array<float, 4> longitudinal_force_reserve{};
    std::array<float, 4> previous_longitudinal_slip{};
    std::array<std::array<double, 3>, 4> contact_force_world_by_wheel{};

    // Build each massless wheel hub's planar solver velocity in ENU, then
    // project it onto that wheel's own tangent plane. Reusing the averaged
    // terrain vx/vy directly for every corner makes slip inconsistent with the
    // direction of the applied force whenever adjacent Landscape triangles
    // have different normals. Suspension/heave motion is along the algebraic
    // travel axis and is intentionally not treated as rigid tire scrub.
    const float canonical_navigation_yaw_rate =
        simcore_host::BodyFrameAdapter::solver_heading_rate_to_canonical_yaw_rate(
            solver_yaw_rate_rad_s_);

    // Allocate lateral grip first. A large longitudinal request must not scale
    // steering force away and make a modest-speed turn feel frictionless.
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        auto& wheel = state_.wheels[index];
        previous_longitudinal_slip[index] = wheel.longitudinal_slip;
        wheel.wheel_index = static_cast<uint32_t>(index);
        const float solver_wheel_steering = solver_wheel_steering_angles[index];
        wheel.steering_angle =
            simcore_host::BodyFrameAdapter::solver_steering_to_canonical(
                solver_wheel_steering);
        // Spring/damper force is capped by the suspension model. A compressed
        // hard stop supplies an additional real normal reaction, measured from
        // the prior accepted tick's velocity impulse. It is a one-step force
        // estimate because tire integration precedes this tick's constraints.
        // The impulse has already acted on body heave/roll/pitch: use it only
        // in the tire budget, never add it to those body forces a second time.
        // Nor is the rigid-stop reaction subject to the spring's force cap.
        wheel.normal_load = wheel.in_contact
            ? std::clamp(
                suspension_normal_force_n[index],
                0.f,
                parameters_.suspension.max_force_n)
                + wheel_contact_support_[index].tire_hard_stop_normal_force_n
            : 0.f;
        total_normal_load += wheel.normal_load;

        const float common_wheel_velocity_x =
            body_longitudinal_speed_mps_
            - solver_yaw_rate_rad_s_ * wheel_y[index];
        const float common_wheel_velocity_y =
            solver_lateral_speed_mps_
            + solver_yaw_rate_rad_s_ * wheel_x[index];
        const std::array<double, 3> patch_velocity_world{
            common_wheel_velocity_x * ground_basis.forward[0]
                + common_wheel_velocity_y * ground_basis.right[0],
            common_wheel_velocity_x * ground_basis.forward[1]
                + common_wheel_velocity_y * ground_basis.right[1],
            common_wheel_velocity_x * ground_basis.forward[2]
                + common_wheel_velocity_y * ground_basis.right[2]};
        const auto& wheel_basis = wheel_ground_bases[index];
        const float wheel_velocity_x = static_cast<float>(
            dot_product(patch_velocity_world, wheel_basis.forward));
        const float wheel_velocity_y = static_cast<float>(
            dot_product(patch_velocity_world, wheel_basis.right));
        wheel_cosine[index] = std::cos(solver_wheel_steering);
        wheel_sine[index] = std::sin(solver_wheel_steering);
        wheel_longitudinal_velocity[index] =
            wheel_cosine[index] * wheel_velocity_x
            + wheel_sine[index] * wheel_velocity_y;
        wheel_lateral_velocity[index] =
            -wheel_sine[index] * wheel_velocity_x
            + wheel_cosine[index] * wheel_velocity_y;

        if (!wheel.in_contact) {
            wheel.longitudinal_slip = 0.f;
            wheel.slip_angle = 0.f;
            continue;
        }

        // Near zero forward speed, atan2(vy, |vx|) becomes singular. Use a
        // physical regularization speed while keeping lateral grip active.
        const float speed_for_slip = std::max(
            std::abs(wheel_longitudinal_velocity[index]),
            parameters_.low_speed_slip_reference_mps);
        const float internal_slip_angle = std::atan2(
            wheel_lateral_velocity[index], speed_for_slip);
        wheel.slip_angle =
            simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(
                internal_slip_angle);

        const float surface_friction_multiplier =
            effective_surface_friction_multiplier(
                parameters_,
                ground_surface_material_by_wheel_[index],
                ground_friction_multiplier_by_wheel_[index]);
        const float friction_limit = parameters_.tire_friction
            * surface_friction_multiplier * wheel.normal_load;
        tire_friction_limit_by_wheel[index] = friction_limit;
        const float lateral_limit =
            friction_limit * parameters_.lateral_grip_priority;
        const float corner_stiffness = index < 2
            ? parameters_.front_tire_corner_stiffness_n_rad
            : parameters_.rear_tire_corner_stiffness_n_rad;
        unconstrained_tire_force_y_by_wheel[index] =
            -corner_stiffness * internal_slip_angle;
        tire_force_y_by_wheel[index] = std::clamp(
            unconstrained_tire_force_y_by_wheel[index],
            -lateral_limit,
            lateral_limit);
        const bool rear_handbrake_tire = in.handbrake && index >= 2;
        // A mechanical side brake acts on the rear axle. A locked/slipping
        // rear tire spends its friction budget resisting longitudinal motion
        // first; only the remaining circle can generate cornering force. The
        // normal driving path intentionally keeps the existing lateral-first
        // allocation so service braking and ordinary steering are unchanged.
        longitudinal_force_reserve[index] = rear_handbrake_tire
            ? friction_limit
            : std::sqrt(std::max(
                0.f,
                friction_limit * friction_limit
                    - tire_force_y_by_wheel[index]
                        * tire_force_y_by_wheel[index]));
    }

    // Model equal half-shaft torque with traction control. If either wheel on a
    // driven axle loses contact, torque is cut for that axle instead of winding
    // the airborne wheel up. With both wheels grounded, requested torque is
    // limited by the weaker wheel's remaining friction reserve.
    std::array<float, 2> axle_drive_torque_per_wheel{};
    for (std::size_t axle = 0; axle < axle_drive_torque_per_wheel.size(); ++axle) {
        const float axle_drive_fraction = axle == 0
            ? parameters_.front_drive_torque_fraction
            : 1.f - parameters_.front_drive_torque_fraction;
        const std::size_t left_index = axle * 2;
        const std::size_t right_index = left_index + 1;
        if (axle_drive_fraction <= 0.f
            || drive_force_suppressed
            || gear_sign * total_drive_force <= STOP_EPSILON
            || !state_.wheels[left_index].in_contact
            || !state_.wheels[right_index].in_contact) {
            continue;
        }

        const float drive_direction = std::copysign(1.f, total_drive_force);
        const float driven_slip = std::max({
            0.f,
            drive_direction * previous_longitudinal_slip[left_index],
            drive_direction * previous_longitudinal_slip[right_index]});
        float traction_control_scale = 1.f;
        if (driven_slip > parameters_.traction_control_slip_target) {
            traction_control_scale = std::clamp(
                (parameters_.traction_control_full_cut_slip - driven_slip)
                    / (parameters_.traction_control_full_cut_slip
                       - parameters_.traction_control_slip_target),
                0.f,
                1.f);
        }

        const float requested_torque_per_wheel = total_drive_force
            * axle_drive_fraction * parameters_.tire_radius_m * 0.5f
            * traction_control_scale;
        const float torque_capacity_per_wheel = std::min(
            longitudinal_force_reserve[left_index],
            longitudinal_force_reserve[right_index])
            * parameters_.tire_radius_m;
        axle_drive_torque_per_wheel[axle] = std::clamp(
            requested_torque_per_wheel,
            -torque_capacity_per_wheel,
            torque_capacity_per_wheel);
    }

    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        auto& wheel = state_.wheels[index];
        const float longitudinal_velocity = wheel_longitudinal_velocity[index];
        const float drive_torque = axle_drive_torque_per_wheel[index / 2];
        float brake_direction = 0.f;
        if (std::abs(wheel_angular_speed_rad_s_[index]) > STOP_EPSILON) {
            brake_direction = std::copysign(1.f, wheel_angular_speed_rad_s_[index]);
        } else if (std::abs(longitudinal_velocity) > STOP_EPSILON) {
            brake_direction = std::copysign(1.f, longitudinal_velocity);
        }
        const float service_axle_fraction = index < 2
            ? parameters_.front_service_brake_fraction
            : 1.f - parameters_.front_service_brake_fraction;
        const float service_brake_per_wheel = service_brake_force
            * service_axle_fraction * 0.5f;
        const float parking_brake_per_wheel = index >= 2 && in.handbrake
            ? parameters_.max_handbrake_force_n * 0.5f
            : 0.f;
        const float wheel_brake_force = std::max(
            service_brake_per_wheel, parking_brake_per_wheel);
        const float brake_torque = wheel_brake_force
            * parameters_.tire_radius_m * brake_direction;
        const float previous_angular_speed = wheel_angular_speed_rad_s_[index];
        const float applied_torque = drive_torque - brake_torque;

        float tire_force_x = 0.f;
        float tire_force_y = tire_force_y_by_wheel[index];
        if (wheel.in_contact) {
            const float slip_denominator =
                std::max(std::abs(longitudinal_velocity), 1.f);

            // Couple tire force and wheel acceleration implicitly. This stays
            // stable at the 60 Hz host tick while still publishing real slip.
            const float longitudinal_gain =
                parameters_.tire_longitudinal_stiffness_n / slip_denominator;
            const float angular_step = fdt / parameters_.wheel_inertia_kg_m2;
            const float implicit_denominator = 1.f + angular_step
                * parameters_.tire_radius_m * parameters_.tire_radius_m
                * longitudinal_gain;
            const float implicit_angular_speed =
                (previous_angular_speed
                 + angular_step
                    * (applied_torque
                       + parameters_.tire_radius_m * longitudinal_gain
                            * longitudinal_velocity))
                / implicit_denominator;
            const float raw_implicit_slip =
                (implicit_angular_speed * parameters_.tire_radius_m
                    - longitudinal_velocity) / slip_denominator;
            const float implicit_slip = std::clamp(
                raw_implicit_slip, -1.f, 1.f);
            const float raw_longitudinal_force =
                parameters_.tire_longitudinal_stiffness_n * implicit_slip;
            tire_force_x = std::clamp(
                raw_longitudinal_force,
                -longitudinal_force_reserve[index],
                longitudinal_force_reserve[index]);

            if (in.handbrake && index >= 2) {
                const float friction_limit =
                    tire_friction_limit_by_wheel[index];
                const float remaining_lateral_force = std::sqrt(std::max(
                    0.f,
                    friction_limit * friction_limit
                        - tire_force_x * tire_force_x));
                tire_force_y = std::clamp(
                    unconstrained_tire_force_y_by_wheel[index],
                    -remaining_lateral_force,
                    remaining_lateral_force);
            }

            const bool tire_force_saturated =
                raw_implicit_slip != implicit_slip
                || std::abs(tire_force_x - raw_longitudinal_force) > 1e-4f;
            wheel_angular_speed_rad_s_[index] = tire_force_saturated
                ? previous_angular_speed + angular_step
                    * (applied_torque
                       - tire_force_x * parameters_.tire_radius_m)
                : implicit_angular_speed;
            wheel.longitudinal_slip = std::clamp(
                (wheel_angular_speed_rad_s_[index] * parameters_.tire_radius_m
                    - longitudinal_velocity) / slip_denominator,
                -1.f,
                1.f);
        } else {
            // Drive torque is already zero for a partially grounded axle. The
            // remaining passive damping settles any previous free rotation.
            const float free_spin_damping_torque =
                parameters_.wheel_free_spin_damping_n_m_s
                * previous_angular_speed;
            wheel_angular_speed_rad_s_[index] = previous_angular_speed
                + (applied_torque - free_spin_damping_torque)
                    / parameters_.wheel_inertia_kg_m2 * fdt;
            wheel.longitudinal_slip = 0.f;
            wheel.slip_angle = 0.f;
        }

        if ((total_brake_force > 0.f || !wheel.in_contact)
            && previous_angular_speed * wheel_angular_speed_rad_s_[index] < 0.f) {
            wheel_angular_speed_rad_s_[index] = 0.f;
        }
        wheel.longitudinal_force = tire_force_x;
        wheel.lateral_force =
            simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(
                tire_force_y);
        wheel.angular_speed = wheel_angular_speed_rad_s_[index];

        const float body_force_x =
            wheel_cosine[index] * tire_force_x
            - wheel_sine[index] * tire_force_y;
        const float body_force_y =
            wheel_sine[index] * tire_force_x
            + wheel_cosine[index] * tire_force_y;
        const auto& wheel_basis = wheel_ground_bases[index];
        contact_force_world_by_wheel[index] = {
            body_force_x * wheel_basis.forward[0]
                + body_force_y * wheel_basis.right[0],
            body_force_x * wheel_basis.forward[1]
                + body_force_y * wheel_basis.right[1],
            body_force_x * wheel_basis.forward[2]
                + body_force_y * wheel_basis.right[2]};
        const float common_force_x = static_cast<float>(
            contact_force_world_by_wheel[index][0] * ground_basis.forward[0]
            + contact_force_world_by_wheel[index][1] * ground_basis.forward[1]
            + contact_force_world_by_wheel[index][2] * ground_basis.forward[2]);
        const float common_force_y = static_cast<float>(
            contact_force_world_by_wheel[index][0] * ground_basis.right[0]
            + contact_force_world_by_wheel[index][1] * ground_basis.right[1]
            + contact_force_world_by_wheel[index][2] * ground_basis.right[2]);
        total_force_x += common_force_x;
        total_force_y += common_force_y;
        total_yaw_moment +=
            wheel_x[index] * common_force_y - wheel_y[index] * common_force_x;
    }

    // A stopped, braked tire supports a static contact force even though both
    // wheel speed and slip are exactly zero. The kinetic slip equations above
    // cannot create that force by themselves, so solve the grade reaction from
    // equilibrium while it remains inside the combined friction capacity.
    // This is what lets a real car remain planted on a hill without slowly
    // pitching away from the road normal or acquiring hidden lateral drift.
    bool static_hold_succeeded = false;
    const bool static_hold_candidate = total_brake_force > 0.f
        && std::abs(previous_speed) <= STOP_EPSILON
        && std::abs(solver_lateral_speed_mps_) <= STOP_EPSILON
        && std::abs(solver_yaw_rate_rad_s_) <= STOP_EPSILON
        && total_normal_load > MIN_CONTACT_NORMAL_LOAD_N;
    const float required_static_force_x = -gravity_force_x;
    const float required_static_force_y = -gravity_force_y;
    const float required_static_force = std::hypot(
        required_static_force_x, required_static_force_y);
    float static_friction_capacity = 0.f;
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        if (!state_.wheels[index].in_contact) {
            continue;
        }
        static_friction_capacity += parameters_.tire_friction
            * effective_surface_friction_multiplier(
                parameters_,
                ground_surface_material_by_wheel_[index],
                ground_friction_multiplier_by_wheel_[index])
            * state_.wheels[index].normal_load;
    }
    if (static_hold_candidate
        && required_static_force <= static_friction_capacity + 1e-3f) {
        total_force_x = 0.f;
        total_force_y = 0.f;
        total_yaw_moment = 0.f;
        contact_force_world_by_wheel.fill({});
        for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
            auto& wheel = state_.wheels[index];
            if (!wheel.in_contact) {
                continue;
            }
            const float load_fraction = wheel.normal_load / total_normal_load;
            const float body_force_x = required_static_force_x * load_fraction;
            const float body_force_y = required_static_force_y * load_fraction;
            const float tire_force_x = wheel_cosine[index] * body_force_x
                + wheel_sine[index] * body_force_y;
            const float tire_force_y = -wheel_sine[index] * body_force_x
                + wheel_cosine[index] * body_force_y;
            wheel.longitudinal_force = tire_force_x;
            wheel.lateral_force =
                simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(
                    tire_force_y);
            wheel.longitudinal_slip = 0.f;
            wheel.slip_angle = 0.f;
            wheel_angular_speed_rad_s_[index] = 0.f;
            wheel.angular_speed = 0.f;

            const auto& wheel_basis = wheel_ground_bases[index];
            contact_force_world_by_wheel[index] = {
                body_force_x * wheel_basis.forward[0]
                    + body_force_y * wheel_basis.right[0],
                body_force_x * wheel_basis.forward[1]
                    + body_force_y * wheel_basis.right[1],
                body_force_x * wheel_basis.forward[2]
                    + body_force_y * wheel_basis.right[2]};
            const float common_force_x = static_cast<float>(
                contact_force_world_by_wheel[index][0] * ground_basis.forward[0]
                + contact_force_world_by_wheel[index][1] * ground_basis.forward[1]
                + contact_force_world_by_wheel[index][2] * ground_basis.forward[2]);
            const float common_force_y = static_cast<float>(
                contact_force_world_by_wheel[index][0] * ground_basis.right[0]
                + contact_force_world_by_wheel[index][1] * ground_basis.right[1]
                + contact_force_world_by_wheel[index][2] * ground_basis.right[2]);
            total_force_x += common_force_x;
            total_force_y += common_force_y;
            total_yaw_moment += wheel_x[index] * common_force_y
                - wheel_y[index] * common_force_x;
        }
        static_hold_succeeded = true;
    }

    // Resolve each final tire force at its own published contact patch and
    // project r x F onto the sprung body's roll/pitch axes.  This preserves
    // diagonal/asymmetric contact geometry instead of replacing four patches
    // with one aggregate force at a fixed CG-height lever.  Gravity acts at the
    // CG, while aerodynamic/driveline drag are body/internal model forces, so
    // none of those are part of this contact torque.
    float tire_contact_roll_moment_n_m = 0.f;
    float tire_contact_pitch_moment_n_m = 0.f;
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        if (!state_.wheels[index].in_contact) {
            continue;
        }
        const auto& force_world = contact_force_world_by_wheel[index];
        const std::array<double, 3> patch_from_cg{
            state_.wheels[index].contact_point_enu.x - state_.position_enu.x,
            state_.wheels[index].contact_point_enu.y - state_.position_enu.y,
            state_.wheels[index].contact_point_enu.z - state_.position_enu.z};
        const auto torque_world = cross_product(patch_from_cg, force_world);
        tire_contact_roll_moment_n_m += static_cast<float>(
            torque_world[0] * force_sample_body_basis.forward[0]
            + torque_world[1] * force_sample_body_basis.forward[1]
            + torque_world[2] * force_sample_body_basis.forward[2]);
        tire_contact_pitch_moment_n_m += static_cast<float>(
            dot_product(torque_world, make_horizontal_right(heading_rad_)));
    }

    float rolling_contact_force_x = 0.f;

    if (std::abs(body_longitudinal_speed_mps_) > STOP_EPSILON) {
        const float motion_sign = std::copysign(1.f, body_longitudinal_speed_mps_);
        const float rolling_resistance_force = motion_sign
            * parameters_.rolling_resistance_coeff * total_normal_load;
        total_force_x -= rolling_resistance_force;
        rolling_contact_force_x = -rolling_resistance_force;
        total_force_x -= 0.5f * parameters_.air_density_kg_m3
                       * parameters_.drag_coefficient * parameters_.frontal_area_m2
                       * body_longitudinal_speed_mps_ * std::abs(body_longitudinal_speed_mps_);
        if (in.gear != VehicleGear::Neutral && in.throttle <= 0.f) {
            // Engine/driveline drag is separate from road rolling resistance:
            // Drive and Reverse coast down more heavily, while Neutral remains
            // a free-rolling comparison path.
            total_force_x -= parameters_.drivetrain_drag_n_per_mps
                * body_longitudinal_speed_mps_;
        }
    }

    // Rolling resistance is also a road contact force. Resolve its below-CG
    // lever in world space so compound roll can create the appropriate roll
    // and horizontal-right pitch generalized moments.
    const std::array<double, 3> rolling_force_world{
        rolling_contact_force_x * ground_basis.forward[0],
        rolling_contact_force_x * ground_basis.forward[1],
        rolling_contact_force_x * ground_basis.forward[2]};
    const std::array<double, 3> rolling_patch_from_cg{
        -parameters_.cg_height_m * force_sample_body_basis.up[0],
        -parameters_.cg_height_m * force_sample_body_basis.up[1],
        -parameters_.cg_height_m * force_sample_body_basis.up[2]};
    const auto rolling_torque_world = cross_product(
        rolling_patch_from_cg, rolling_force_world);
    const float rolling_contact_roll_moment_n_m = static_cast<float>(
        dot_product(rolling_torque_world, force_sample_body_basis.forward));
    const float rolling_contact_pitch_moment_n_m = static_cast<float>(
        dot_product(rolling_torque_world, make_horizontal_right(heading_rad_)));

    // Resolve gravity against the orthonormal surface basis. On a compound
    // grade this avoids treating pitch and roll as two unrelated inclines.
    total_force_x += gravity_force_x;
    total_force_y += gravity_force_y;

    const float longitudinal_accel = total_force_x / parameters_.mass_kg
                                   + solver_lateral_speed_mps_ * solver_yaw_rate_rad_s_;
    const auto integrated_velocity = integrate_body_planar_velocity(
        body_longitudinal_speed_mps_, solver_lateral_speed_mps_, solver_yaw_rate_rad_s_,
        total_force_x, total_force_y, parameters_.mass_kg, dt);
    body_longitudinal_speed_mps_ = integrated_velocity[0];
    solver_lateral_speed_mps_ = integrated_velocity[1];
    solver_yaw_rate_rad_s_ += total_yaw_moment / parameters_.yaw_inertia_kg_m2 * fdt;

    if (total_brake_force > 0.f) {
        const bool crossed_stop = previous_speed * body_longitudinal_speed_mps_ < 0.f;
        const bool held_at_stop = std::abs(previous_speed) <= STOP_EPSILON;
        if (crossed_stop || (held_at_stop && static_hold_succeeded)) {
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

    // Preserve the existing semi-implicit heading convention: horizontal
    // velocity is evaluated at the predicted post-step heading. CollisionWorld
    // then owns the actual XY/heading integration and may project/impulse that
    // planar body without touching suspension, pitch, roll, or vertical state.
    const double predicted_heading_rad = normalize_heading(
        heading_rad_ + solver_yaw_rate_rad_s_ * dt);
    GroundBasis position_ground_basis = make_ground_basis_from_normal(
        predicted_heading_rad, ground_basis.normal);
    double east_velocity =
        body_longitudinal_speed_mps_ * position_ground_basis.forward[0]
        + solver_lateral_speed_mps_ * position_ground_basis.right[0];
    double north_velocity =
        body_longitudinal_speed_mps_ * position_ground_basis.forward[1]
        + solver_lateral_speed_mps_ * position_ground_basis.right[1];

    if (collision_world_) {
        const double collision_half_length =
            parameters_.wheelbase_m * 0.5 + parameters_.collision_body_overhang_m;
        const double collision_half_width =
            std::max(parameters_.front_track_m, parameters_.rear_track_m) * 0.5
            + parameters_.collision_body_side_padding_m;
        std::vector<std::string> tire_supported_curb_ids;
        const auto support_height = [&](double east, double north)
            -> std::optional<double> {
            const simcore_host::GroundQueryRequest request{
                {east, north, state_.position_enu.z
                    + GROUND_PENETRATION_RECOVERY_M},
                GROUND_QUERY_DEPTH_M + GROUND_PENETRATION_RECOVERY_M};
            const auto hit = ground_query_->query_down(request);
            if (!hit || !std::isfinite(hit->distance_m)
                || hit->distance_m < 0.0
                || hit->distance_m > request.max_distance_m
                || !std::isfinite(hit->point_enu.up_m)
                || !std::isfinite(hit->normal_enu.up_m)
                || hit->normal_enu.up_m <= 0.0) {
                return std::nullopt;
            }
            return hit->point_enu.up_m;
        };
        for (const auto& collider : collision_world_->static_colliders()) {
            if (collider.semantic != simcore_host::StaticColliderSemantic::Curb)
                continue;
            const double sine = std::sin(collider.shape.heading_rad);
            const double cosine = std::cos(collider.shape.heading_rad);
            const std::array<double, 2> forward{sine, cosine};
            const std::array<double, 2> right{cosine, -sine};
            const auto local_coordinates = [&](double east, double north) {
                const double relative_east = east
                    - collider.shape.center_enu.east_m;
                const double relative_north = north
                    - collider.shape.center_enu.north_m;
                return std::array<double, 2>{
                    relative_east * forward[0] + relative_north * forward[1],
                    relative_east * right[0] + relative_north * right[1]};
            };
            const auto start_local = local_coordinates(east_m_, north_m_);
            const auto end_local = local_coordinates(
                east_m_ + east_velocity * dt,
                north_m_ + north_velocity * dt);
            const auto body_projection = [&](double body_heading,
                                             const std::array<double, 2>& axis) {
                const std::array<double, 2> body_forward{
                    std::sin(body_heading), std::cos(body_heading)};
                const std::array<double, 2> body_right{
                    std::cos(body_heading), -std::sin(body_heading)};
                return collision_half_length * std::abs(
                           body_forward[0] * axis[0]
                           + body_forward[1] * axis[1])
                    + collision_half_width * std::abs(
                           body_right[0] * axis[0]
                           + body_right[1] * axis[1]);
            };
            // Body projection is Lipschitz-bounded by its circumscribed radius.
            // This padding conservatively covers orientations between the tick's
            // start and predicted end without turning the filter into speed-only
            // padding that could still miss a diagonal crossing.
            const double rotation_projection_padding = std::hypot(
                collision_half_length, collision_half_width)
                * std::abs(static_cast<double>(solver_yaw_rate_rad_s_) * dt);
            const double swept_body_forward_extent =
                std::max(
                    body_projection(heading_rad_, forward),
                    body_projection(predicted_heading_rad, forward))
                + rotation_projection_padding;
            const double swept_body_right_extent =
                std::max(
                    body_projection(heading_rad_, right),
                    body_projection(predicted_heading_rad, right))
                + rotation_projection_padding;
            const double expanded_forward_half_extent =
                collider.shape.half_length_m + swept_body_forward_extent
                + parameters_.tire_radius_m;
            const double expanded_right_half_extent =
                collider.shape.half_width_m + swept_body_right_extent
                + parameters_.tire_radius_m;
            const auto segment_stays_outside = [](
                double start, double end, double half_extent) {
                return (start < -half_extent && end < -half_extent)
                    || (start > half_extent && end > half_extent);
            };
            if (segment_stays_outside(
                    start_local[0], end_local[0], expanded_forward_half_extent)
                || segment_stays_outside(
                    start_local[1], end_local[1], expanded_right_half_extent)) {
                continue;
            }
            const double support_interval_min = std::max(
                -collider.shape.half_length_m,
                std::min(start_local[0], end_local[0])
                    - swept_body_forward_extent);
            const double support_interval_max = std::min(
                collider.shape.half_length_m,
                std::max(start_local[0], end_local[0])
                    + swept_body_forward_extent);
            if (support_interval_min > support_interval_max) {
                continue;
            }
            const double near_sample_offset = collider.shape.half_width_m
                + parameters_.tire_radius_m;
            const double far_sample_offset = collider.shape.half_width_m
                + parameters_.tire_radius_m
                    * CURB_FAR_SUPPORT_TIRE_RADIUS_MULTIPLIER;
            const double maximum_supported_rise = parameters_.tire_radius_m
                * MAX_SUPPORTED_CURB_RISE_TIRE_RADIUS_FRACTION;
            const double support_height_tolerance = parameters_.tire_radius_m
                * CURB_SUPPORT_RASTER_HEIGHT_TOLERANCE_TIRE_RADIUS_FRACTION;
            const double collider_height = collider.shape.half_height_m * 2.0;
            bool entire_swept_location_is_supported =
                collider_height <= maximum_supported_rise + 1e-6;
            const double support_interval_span =
                support_interval_max - support_interval_min;
            const int support_segment_count = std::max(
                1,
                static_cast<int>(std::ceil(
                    support_interval_span / parameters_.tire_radius_m)));
            for (int sample = 0;
                 entire_swept_location_is_supported
                    && sample <= support_segment_count;
                 ++sample) {
                const double alpha = static_cast<double>(sample)
                    / support_segment_count;
                const double support_local_forward = support_interval_min
                    + support_interval_span * alpha;
                const double support_base_east =
                    collider.shape.center_enu.east_m
                    + forward[0] * support_local_forward;
                const double support_base_north =
                    collider.shape.center_enu.north_m
                    + forward[1] * support_local_forward;
                const auto near_negative_side = support_height(
                    support_base_east - right[0] * near_sample_offset,
                    support_base_north - right[1] * near_sample_offset);
                const auto near_positive_side = support_height(
                    support_base_east + right[0] * near_sample_offset,
                    support_base_north + right[1] * near_sample_offset);
                const auto far_negative_side = support_height(
                    support_base_east - right[0] * far_sample_offset,
                    support_base_north - right[1] * far_sample_offset);
                const auto far_positive_side = support_height(
                    support_base_east + right[0] * far_sample_offset,
                    support_base_north + right[1] * far_sample_offset);
                if (!near_negative_side || !near_positive_side
                    || !far_negative_side || !far_positive_side) {
                    entire_swept_location_is_supported = false;
                    break;
                }
                const double near_support_delta =
                    *near_positive_side - *near_negative_side;
                const double far_support_delta =
                    *far_positive_side - *far_negative_side;
                const double support_rise = std::abs(near_support_delta);
                const bool far_support_rises_on_same_side =
                    (near_support_delta > 0.0 && far_support_delta > 0.0)
                    || (near_support_delta < 0.0 && far_support_delta < 0.0);
                entire_swept_location_is_supported =
                    support_rise >= CURB_SUPPORT_MIN_RISE_M
                    // A 50 cm heightfield can undershoot the road-side near
                    // sample by a few centimetres at a cell diagonal. The
                    // collider and far plateau still define the true step, so
                    // allow only the same bounded raster tolerance already
                    // required for their height agreement.
                    && support_rise <= maximum_supported_rise
                        + support_height_tolerance + 1e-6
                    && far_support_rises_on_same_side
                    && std::abs(collider_height - std::abs(far_support_delta))
                        <= support_height_tolerance;
            }
            if (entire_swept_location_is_supported) {
                // The curb is only removed from the planar body prism solve.
                // The <=R-spaced along samples cover the whole swept body
                // footprint. Each near pair verifies the immediate step; the
                // far pair must repeat that rise in the same direction and at
                // the collider's authored height. Comparing height deltas is
                // invariant to a grade running along the curb.
                // Wheel rays independently raise each suspension corner; no
                // z/yaw pose is injected here.
                tire_supported_curb_ids.push_back(collider.collider_id);
            }
        }
        const double body_ground_height =
            state_.position_enu.z - parameters_.cg_height_m;
        const double surface_vertical_rate = ground_basis.normal[2] > 1e-9
            ? -(ground_basis.normal[0] * east_velocity
                + ground_basis.normal[1] * north_velocity)
                / ground_basis.normal[2]
            : 0.0;
        const double start_body_bottom = body_ground_height
            + COLLISION_BODY_GROUND_CLEARANCE_M;
        const double predicted_body_bottom = start_body_bottom
            + surface_vertical_rate * dt;
        const double swept_body_bottom = std::min(
            start_body_bottom, predicted_body_bottom);
        const double swept_body_top = std::max(
            start_body_bottom + parameters_.collision_body_half_height_m * 2.0,
            predicted_body_bottom + parameters_.collision_body_half_height_m * 2.0);
        simcore_host::PlanarRigidBody collision_body{
            "ego",
            {
                {east_m_, north_m_},
                (swept_body_bottom + swept_body_top) * 0.5,
                heading_rad_,
                collision_half_length,
                collision_half_width,
                (swept_body_top - swept_body_bottom) * 0.5,
            },
            {east_velocity, north_velocity},
            solver_yaw_rate_rad_s_,
            parameters_.mass_kg,
            parameters_.yaw_inertia_kg_m2,
        };
        std::vector<std::string> tire_supported_proxy_ids;
        for (const auto& proxy : runtime_tire_supports_) tire_supported_proxy_ids.push_back(proxy.proxy_id);
        const auto collision_result = collision_world_
            ->integrate_with_tire_supported_curbs(
                std::move(collision_body), dt, std::move(dynamic_proxies),
                std::move(tire_supported_curb_ids), std::move(tire_supported_proxy_ids));
        accepted_resolved_dynamic_proxies =
            collision_result.resolved_dynamic_proxies;
        // Final wheel constraints must use the accepted moving prop position.
        refresh_runtime_tire_supports(accepted_resolved_dynamic_proxies);
        accepted_collision_contacts = collision_result.contacts;
        accepted_runtime_proxy_contacts = collision_result.runtime_proxy_contacts;
        const auto strongest_contact = std::max_element(
            collision_result.contacts.begin(),
            collision_result.contacts.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.accumulated_normal_impulse_n_s
                    < rhs.accumulated_normal_impulse_n_s;
            });
        if (strongest_contact != collision_result.contacts.end()
            && std::isfinite(
                strongest_contact->accumulated_normal_impulse_n_s)
            && strongest_contact->accumulated_normal_impulse_n_s
                > DAMAGE_IMPULSE_THRESHOLD_N_S) {
            const double impulse =
                strongest_contact->accumulated_normal_impulse_n_s;
            state_.last_impact_impulse_n_s = static_cast<float>(impulse);
            if (parameters_.single_track &&
                ((impulse >= parameters_.mass_kg * 4.0 && std::abs(previous_speed) >= 2.5f)
                    || (impulse >= 250.0 && (std::abs(roll_rad_) >= 1.134464f
                        || std::abs(pitch_rad_) >= 1.134464f))))
                motorcycle_rider_attached_ = false;
            for (const auto& contact : collision_result.contacts) {
                simcore_host::record_vehicle_dent(state_.dent_patches,
                    collision_result.body.shape, contact);
            }
            state_.damage_percent = std::clamp(
                state_.damage_percent + static_cast<float>(
                    (impulse - DAMAGE_IMPULSE_THRESHOLD_N_S)
                    / DAMAGE_IMPULSE_PER_PERCENT_N_S),
                0.f,
                100.f);
            if (state_.collision_event_sequence
                != std::numeric_limits<std::uint32_t>::max()) {
                ++state_.collision_event_sequence;
            }

            const double relative_east =
                strongest_contact->contact_point_enu.east_m
                - collision_result.body.shape.center_enu.east_m;
            const double relative_north =
                strongest_contact->contact_point_enu.north_m
                - collision_result.body.shape.center_enu.north_m;
            const double heading_sine = std::sin(
                collision_result.body.shape.heading_rad);
            const double heading_cosine = std::cos(
                collision_result.body.shape.heading_rad);
            const double local_forward =
                relative_east * heading_sine
                + relative_north * heading_cosine;
            const double local_right =
                relative_east * heading_cosine
                - relative_north * heading_sine;
            if (std::abs(local_forward) / collision_half_length
                >= std::abs(local_right) / collision_half_width) {
                state_.damage_zone = local_forward >= 0.0
                    ? VehicleDamageZone::Front
                    : VehicleDamageZone::Rear;
            } else {
                state_.damage_zone = local_right >= 0.0
                    ? VehicleDamageZone::Right
                    : VehicleDamageZone::Left;
            }
        }
        east_m_ = collision_result.body.shape.center_enu.east_m;
        north_m_ = collision_result.body.shape.center_enu.north_m;
        heading_rad_ = collision_result.body.shape.heading_rad;
        solver_yaw_rate_rad_s_ = static_cast<float>(
            collision_result.body.heading_rate_rad_s);
        east_velocity = collision_result.body.linear_velocity_enu_mps.east_m;
        north_velocity = collision_result.body.linear_velocity_enu_mps.north_m;

        // Convert the collision-resolved horizontal ENU velocity back into the
        // terrain-tangent solver axes. Solving the 2x2 horizontal projection
        // avoids losing speed on a grade by applying a simple dot product.
        position_ground_basis = make_ground_basis_from_normal(
            heading_rad_, ground_basis.normal);
        const double horizontal_determinant =
            position_ground_basis.forward[0]
                * position_ground_basis.right[1]
            - position_ground_basis.forward[1]
                * position_ground_basis.right[0];
        if (std::abs(horizontal_determinant) > 1e-9) {
            body_longitudinal_speed_mps_ = static_cast<float>(
                (east_velocity * position_ground_basis.right[1]
                 - north_velocity * position_ground_basis.right[0])
                / horizontal_determinant);
            solver_lateral_speed_mps_ = static_cast<float>(
                (position_ground_basis.forward[0] * north_velocity
                 - position_ground_basis.forward[1] * east_velocity)
                / horizontal_determinant);
        } else {
            body_longitudinal_speed_mps_ = 0.f;
            solver_lateral_speed_mps_ = 0.f;
            solver_yaw_rate_rad_s_ = 0.f;
            east_velocity = 0.0;
            north_velocity = 0.0;
        }
    } else {
        heading_rad_ = predicted_heading_rad;
        east_m_ += east_velocity * dt;
        north_m_ += north_velocity * dt;
    }

    state_.speed = body_longitudinal_speed_mps_;
    state_.accel = (state_.speed - previous_speed) / fdt;
    const float canonical_navigation_yaw_rate_after_collision =
        simcore_host::BodyFrameAdapter::solver_heading_rate_to_canonical_yaw_rate(
            solver_yaw_rate_rad_s_);
    state_.heading = static_cast<float>(heading_rad_ * RAD2DEG);

    const float supported_gravity = GRAVITY_MPS2
        * static_cast<float>(ground_basis.normal[2]);
    // Heave is the sum of the four independent corner reactions.  The average
    // ground plane remains useful for the gravity component, but it is not a
    // synthetic fifth suspension spring.
    const float vertical_accel =
        suspension_heave_force_n / parameters_.mass_kg
        - supported_gravity;
    vertical_speed_mps_ = std::clamp(
        vertical_speed_mps_ + vertical_accel * fdt, -20.f, 20.f);
    const double surface_vertical_displacement = ground_basis.normal[2] > 1e-9
        ? -(ground_basis.normal[0] * (east_m_ - step_start_east_m)
            + ground_basis.normal[1] * (north_m_ - step_start_north_m))
            / ground_basis.normal[2]
        : 0.0;
    const double normal_up_for_heave = std::max(
        ground_basis.normal[2], 1e-6);
    state_.position_enu.z += surface_vertical_displacement
        + vertical_speed_mps_ * dt / normal_up_for_heave;
    state_.east  = east_m_;
    state_.north = north_m_;
    state_.position_enu.x = east_m_;
    state_.position_enu.y = north_m_;
    // Refresh the published hit point and normal from the post-integration
    // pose. Spring/damper history advances only in the pre-force sample above.
    resolve_non_penetrating_ground_contacts(false, &tick_hard_stop_impulses);
    state_.linear_velocity_body = {
        body_longitudinal_speed_mps_,
        simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(
            solver_lateral_speed_mps_),
        // The reduced terrain model treats chassis motion as tangent to its
        // ground-aligned body frame. Full suspension heave velocity projection
        // is deferred with the ground-tangent 3D tire solver.
        0.0};
    state_.angular_velocity_body = make_body_angular_velocity(
        pitch_rad_, roll_rad_, pitch_rate_rad_s_, roll_rate_rad_s_,
        canonical_navigation_yaw_rate_after_collision);
    state_.yaw_rate = static_cast<float>(state_.angular_velocity_body.z);

    // Published wheel rotation drives the Unreal presentation. A stopped
    // chassis must not show tire frames creeping because of sub-threshold
    // wheel/slip oscillation. While moving, left/right speeds remain
    // independent so the inside and outside tires follow their actual paths.
    const bool chassis_stationary = std::abs(state_.speed) <= STOP_EPSILON;
    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        state_.wheels[index].angular_speed = chassis_stationary
            ? 0.f
            : wheel_angular_speed_rad_s_[index];
    }

    state_.lat = origin_lat_ + (north_m_ / EARTH_R) * RAD2DEG;
    state_.lon = origin_lon_ +
        (east_m_ / (EARTH_R * std::cos(origin_lat_ * DEG2RAD))) * RAD2DEG;

    // The sprung body is driven by four independent suspension reactions and
    // tire-patch forces at CG height.  The legacy attitude K/C fields remain
    // serialized as zero for v4 compatibility; the fitted support plane is
    // reset/diagnostic data, not a hidden force or a motion boundary.
    float roll_moment = suspension_roll_moment_n_m
        + tire_contact_roll_moment_n_m
        + rolling_contact_roll_moment_n_m;
    if (parameters_.single_track) {
        const bool supported = (state_.wheels[0].in_contact || state_.wheels[1].in_contact)
            && (state_.wheels[2].in_contact || state_.wheels[3].in_contact);
        roll_moment += simcore_host::motorcycle_balance_torque(
            simcore_host::motorcycle_equilibrium_roll(body_longitudinal_speed_mps_,
                canonical_navigation_yaw_rate_after_collision),
            roll_rad_, roll_rate_rad_s_, roll_moment, parameters_.roll_inertia_kg_m2,
            parameters_.mass_kg, parameters_.cg_height_m, supported, motorcycle_rider_attached_);
    }
    const float pitch_moment = suspension_pitch_moment_n_m
        + tire_contact_pitch_moment_n_m
        + rolling_contact_pitch_moment_n_m;
    const float canonical_navigation_yaw_accel =
        (canonical_navigation_yaw_rate_after_collision
            - canonical_navigation_yaw_rate) / fdt;
    const auto attitude_acceleration = solve_generalized_attitude_acceleration(
        parameters_,
        pitch_rad_,
        roll_rad_,
        pitch_rate_rad_s_,
        roll_rate_rad_s_,
        canonical_navigation_yaw_rate_after_collision,
        canonical_navigation_yaw_accel,
        roll_moment,
        pitch_moment);
    roll_rate_rad_s_ += attitude_acceleration.roll_rad_s2 * fdt;
    pitch_rate_rad_s_ += attitude_acceleration.pitch_rad_s2 * fdt;
    // Semi-implicit integration is stable at the 60 Hz fixed step for the
    // configured spring/damper/inertia values.  Do not clamp ordinary pitch or
    // roll: suspension travel and unilateral contact are the physical limits.
    roll_rad_ += roll_rate_rad_s_ * fdt;
    pitch_rad_ += pitch_rate_rad_s_ * fdt;
    state_.roll = static_cast<float>(roll_rad_ * RAD2DEG);
    state_.pitch = static_cast<float>(pitch_rad_ * RAD2DEG);
    state_.angular_velocity_body = make_body_angular_velocity(
        pitch_rad_, roll_rad_, pitch_rate_rad_s_, roll_rate_rad_s_,
        canonical_navigation_yaw_rate_after_collision);
    state_.yaw_rate = static_cast<float>(state_.angular_velocity_body.z);

    // Attitude changes move every suspension mount vertically. Re-evaluate the
    // final pose so the state published to Unreal cannot penetrate between the
    // earlier position solve and this attitude solve.
    resolve_non_penetrating_ground_contacts(false, &tick_hard_stop_impulses);

    const simcore_host::GroundQueryRequest centre_coverage_request{
        {east_m_, north_m_, state_.position_enu.z
            + GROUND_PENETRATION_RECOVERY_M},
        GROUND_QUERY_DEPTH_M + GROUND_PENETRATION_RECOVERY_M};
    const auto centre_coverage_hit =
        ground_query_->query_down(centre_coverage_request);
    const bool centre_has_ground_coverage = centre_coverage_hit.has_value()
        && std::isfinite(centre_coverage_hit->distance_m)
        && centre_coverage_hit->distance_m >= 0.0
        && centre_coverage_hit->distance_m
            <= centre_coverage_request.max_distance_m
        && std::isfinite(centre_coverage_hit->normal_enu.up_m)
        && centre_coverage_hit->normal_enu.up_m > 0.0;
    const bool full_front_edge_lost =
        !ground_surface_hit_by_wheel_[0] && !ground_surface_hit_by_wheel_[1];
    const bool full_rear_edge_lost =
        !ground_surface_hit_by_wheel_[2] && !ground_surface_hit_by_wheel_[3];
    const bool full_left_edge_lost =
        !ground_surface_hit_by_wheel_[0] && !ground_surface_hit_by_wheel_[2];
    const bool full_right_edge_lost =
        !ground_surface_hit_by_wheel_[1] && !ground_surface_hit_by_wheel_[3];
    const bool terminal_footprint_coverage_lost = full_front_edge_lost
        || full_rear_edge_lost || full_left_edge_lost || full_right_edge_lost;
    if (!centre_has_ground_coverage || ground_query_hit_count_ == 0
        || terminal_footprint_coverage_lost) {
        // A single missing corner remains physical three-wheel partial support,
        // and diagonally opposed two-wheel coverage remains solvable. Once a
        // complete axle or side leaves authored terrain, however, the remaining
        // two springs would tip this non-airborne reduced model through the map
        // before the centre query reaches the boundary. Restore the previous
        // supported pose instead. Fitting the exporter to the Ground Actor moves
        // this terminal safety edge to the actual Landscape extent.
        state_ = step_start_state;
        state_.timestamp = update_timestamp;
        east_m_ = step_start_east_m;
        north_m_ = step_start_north_m;
        heading_rad_ = step_start_heading_rad;
        pitch_rad_ = step_start_pitch_rad;
        roll_rad_ = step_start_roll_rad;
        pitch_rate_rad_s_ = 0.f;
        roll_rate_rad_s_ = 0.f;
        vertical_speed_mps_ = 0.f;
        ground_pitch_rad_ = step_start_ground_pitch_rad;
        ground_roll_rad_ = step_start_ground_roll_rad;
        support_pitch_rad_ = step_start_support_pitch_rad;
        support_roll_rad_ = step_start_support_roll_rad;
        support_attitude_valid_ = step_start_support_attitude_valid;
        motorcycle_rider_attached_ = step_start_rider_attached;
        body_longitudinal_speed_mps_ = 0.f;
        solver_lateral_speed_mps_ = 0.f;
        solver_yaw_rate_rad_s_ = 0.f;
        applied_drive_force_n_ = 0.f;
        wheel_angular_speed_rad_s_.fill(0.f);
        suspension_compression_m_ = step_start_suspension_compression;
        suspension_base_force_n_ = step_start_suspension_force;
        suspension_had_contact_ = step_start_suspension_contact;

        resolve_non_penetrating_ground_contacts(false);

        state_.speed = 0.f;
        state_.accel = 0.f;
        state_.yaw_rate = 0.f;
        state_.linear_velocity_body = {};
        state_.angular_velocity_body = {};
        state_.rpm = parameters_.idle_rpm;
        state_.gear = in.gear;
        state_.steering_angle =
            simcore_host::BodyFrameAdapter::solver_steering_to_canonical(
                solver_steering_angle);
        for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
            auto& wheel = state_.wheels[index];
            // A coverage stop arrests motion, not the steering linkage. Keep
            // the same Ackermann angles as the normal integration path.
            wheel.steering_angle =
                simcore_host::BodyFrameAdapter::solver_steering_to_canonical(
                    solver_wheel_steering_angles[index]);
            wheel.angular_speed = 0.f;
            wheel.longitudinal_slip = 0.f;
            wheel.slip_angle = 0.f;
            wheel.longitudinal_force = 0.f;
            wheel.lateral_force = 0.f;
        }
        // The attempted motion was discarded. Its reactions must not appear
        // as support supplied by this published fail-closed state.
        wheel_contact_support_ = {};
        return state_;
    }

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

    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        wheel_contact_support_[index].hard_stop_impulse_n_s =
            tick_hard_stop_impulses[index];
        if (state_.wheels[index].in_contact && compression_stop_active_[index]) {
            // The producing dt is essential if callers change their step
            // duration. Internal contact re-queries use dt=0 and cannot be
            // used to convert an impulse into a normal force.
            const double reaction_force_n = tick_hard_stop_impulses[index] / dt;
            if (std::isfinite(reaction_force_n)
                && reaction_force_n <= std::numeric_limits<float>::max()) {
                previous_hard_stop_normal_force_n_[index] =
                    static_cast<float>(reaction_force_n);
            }
        }
    }
    last_resolved_dynamic_proxies_ =
        std::move(accepted_resolved_dynamic_proxies);
    last_collision_contacts_ = std::move(accepted_collision_contacts);
    last_runtime_proxy_contacts_ = std::move(accepted_runtime_proxy_contacts);
    return state_;
}

VehicleState VehiclePhysics::get_state() const {
    return state_;
}

std::array<WheelContactSupportDiagnostics, 4>
VehiclePhysics::get_wheel_contact_support_diagnostics() const
{
    return wheel_contact_support_;
}

bool VehiclePhysics::resolve_non_penetrating_ground_contacts(
    bool update_suspension,
    std::array<double, 4>* tick_hard_stop_impulses)
{
    // A wheel or chassis-shell correction can move a sample onto a different
    // baked triangle. Re-query both contact families in the same tick so a
    // roof/side impact cannot leave a one-frame terrain penetration.
    constexpr int maximum_surface_requery_passes = 4;
    bool any_correction = false;
    for (int pass = 0; pass < maximum_surface_requery_passes; ++pass) {
        const bool wheel_corrected = update_wheel_contacts(
            0.f, update_suspension && pass == 0, true, false,
            tick_hard_stop_impulses);

        const double collision_half_length =
            parameters_.wheelbase_m * 0.5 + parameters_.collision_body_overhang_m;
        const double collision_half_width =
            std::max(parameters_.front_track_m, parameters_.rear_track_m) * 0.5
            + parameters_.collision_body_side_padding_m;
        const GroundBasis local_ground_basis = make_ground_basis(
            heading_rad_, ground_pitch_rad_, ground_roll_rad_);
        const double normal_up = std::max(
            local_ground_basis.normal[2], 1e-6);
        const auto chassis_result =
            simcore_host::resolve_chassis_ground_contact(
                {
                    east_m_,
                    north_m_,
                    state_.position_enu.z,
                    heading_rad_,
                    pitch_rad_,
                    roll_rad_,
                    vertical_speed_mps_ / normal_up,
                    pitch_rate_rad_s_,
                    roll_rate_rad_s_,
                },
                {
                    parameters_.chassis_shell_center_up_offset_m,
                    collision_half_length,
                    collision_half_width,
                    parameters_.chassis_shell_half_height_m,
                    parameters_.mass_kg,
                    parameters_.roll_inertia_kg_m2,
                    parameters_.pitch_inertia_kg_m2,
                    parameters_.yaw_inertia_kg_m2,
                },
                *ground_query_);
        state_.position_enu.z = chassis_result.pose.up_m;
        pitch_rad_ = static_cast<float>(chassis_result.pose.pitch_rad);
        roll_rad_ = static_cast<float>(chassis_result.pose.roll_rad);
        pitch_rate_rad_s_ = static_cast<float>(
            chassis_result.pose.pitch_rate_rad_s);
        roll_rate_rad_s_ = static_cast<float>(
            chassis_result.pose.roll_rate_rad_s);
        vertical_speed_mps_ = static_cast<float>(
            chassis_result.pose.root_up_velocity_mps * normal_up);
        state_.pitch = static_cast<float>(pitch_rad_ * RAD2DEG);
        state_.roll = static_cast<float>(roll_rad_ * RAD2DEG);

        const bool corrected = wheel_corrected || chassis_result.corrected;
        any_correction = any_correction || corrected;
        if (!corrected) {
            return any_correction;
        }
    }

    // The final bounded pass changed the pose, so refresh the public wheel
    // points/normals even if another hard-stop pass would be required next tick.
    update_wheel_contacts(0.f, false);
    return any_correction;
}

bool VehiclePhysics::update_wheel_contacts(
    float dt_seconds, bool update_suspension, bool enforce_non_penetration,
    bool update_attitude_target,
    std::array<double, 4>* tick_hard_stop_impulses)
{
    const double cg_to_rear_axle_m = parameters_.wheelbase_m
        * parameters_.front_static_load_fraction;
    const double cg_to_front_axle_m = parameters_.wheelbase_m
        - cg_to_rear_axle_m;
    const std::array<double, 4> wheel_x{
        cg_to_front_axle_m, cg_to_front_axle_m,
        -cg_to_rear_axle_m, -cg_to_rear_axle_m};
    const double contact_width_scale = parameters_.single_track ? 0.0 : 0.5;
    const std::array<double, 4> wheel_y{
        -parameters_.front_track_m * contact_width_scale, parameters_.front_track_m * contact_width_scale,
        -parameters_.rear_track_m * contact_width_scale, parameters_.rear_track_m * contact_width_scale};

    const double heading_sine = std::sin(heading_rad_);
    const double heading_cosine = std::cos(heading_rad_);
    const BodyBasis body_basis = make_body_basis(
        heading_rad_, pitch_rad_, roll_rad_);
    simcore_host::GroundPointEnu normal_sum{};
    std::size_t surface_hit_count = 0;
    simcore_host::GroundPointEnu contact_normal_sum{};
    std::size_t contact_normal_count = 0;
    std::array<WheelSupportPoint, 4> wheel_support_points{};
    std::size_t wheel_support_point_count = 0;
    struct SuspensionHardStopConstraint {
        bool valid = false;
        std::array<double, 3> normal{};
        simcore_host::GroundPointEnu point{};
        double wheel_x_m = 0.0;
        double wheel_y_m = 0.0;
    };
    std::array<SuspensionHardStopConstraint, 4> hard_stop_constraints{};

    for (std::size_t index = 0; index < state_.wheels.size(); ++index) {
        auto& wheel = state_.wheels[index];
        const bool had_published_contact = wheel.in_contact;
        // Apply one orthonormal 3D body transform to the complete wheel
        // footprint. The previous split XY/Z formula preserved horizontal
        // axle offsets while also adding pitch height, stretching a 2.7 m
        // wheelbase to more than 3.5 m on a 40-degree grade.
        const double mount_east = east_m_
            + wheel_x[index] * body_basis.forward[0]
            + wheel_y[index] * body_basis.right[0];
        const double mount_north = north_m_
            + wheel_x[index] * body_basis.forward[1]
            + wheel_y[index] * body_basis.right[1];
        const double orientation_up_offset =
            wheel_x[index] * body_basis.forward[2]
            + wheel_y[index] * body_basis.right[2];
        const double mount_up = state_.position_enu.z
            + orientation_up_offset;
        const simcore_host::GroundQueryRequest request{
            {mount_east, mount_north,
             mount_up + GROUND_PENETRATION_RECOVERY_M},
            GROUND_QUERY_DEPTH_M + GROUND_PENETRATION_RECOVERY_M};
        const auto hit = simcore_host::runtime_tire_contact(request,
            ground_query_->query_down(request), parameters_.tire_radius_m, runtime_tire_supports_);

        // GroundHit::distance_m is measured from the lifted query origin.
        // Suspension travel and penetration recovery use the real mount pose.
        const double vertical_mount_to_ground_distance = hit.has_value()
            ? mount_up - hit->point_enu.up_m
            : 0.0;

        const bool finite_hit = hit.has_value()
            && std::isfinite(hit->distance_m)
            && hit->distance_m >= 0.0
            && hit->distance_m
                <= GROUND_QUERY_DEPTH_M + GROUND_PENETRATION_RECOVERY_M
            && std::isfinite(vertical_mount_to_ground_distance)
            && vertical_mount_to_ground_distance
                >= -GROUND_PENETRATION_RECOVERY_M
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
        const double normal_up = normal_length > 1e-9
            ? hit->normal_enu.up_m / normal_length
            : 0.0;
        const bool valid_surface_hit = finite_hit
            && normal_length > 1e-9
            && normal_up > 0.0;
        ground_surface_hit_by_wheel_[index] = valid_surface_hit;
        ground_surface_material_by_wheel_[index] = valid_surface_hit
            ? hit->surface_material_id
            : simcore_host::GroundSurfaceMaterialId::Default;
        ground_friction_multiplier_by_wheel_[index] = valid_surface_hit
            && std::isfinite(hit->friction_multiplier)
            && hit->friction_multiplier > 0.0
            ? static_cast<float>(hit->friction_multiplier)
            : 1.f;
        const double normal_east = valid_surface_hit
            ? hit->normal_enu.east_m / normal_length : 0.0;
        const double normal_north = valid_surface_hit
            ? hit->normal_enu.north_m / normal_length : 0.0;
        const double body_up_normal_dot = valid_surface_hit
            ? body_basis.up[0] * normal_east
                + body_basis.up[1] * normal_north
                + body_basis.up[2] * normal_up
            : 0.0;
        const double mount_normal_clearance = valid_surface_hit
            ? normal_east * (mount_east - hit->point_enu.east_m)
                + normal_north * (mount_north - hit->point_enu.north_m)
                + normal_up * (mount_up - hit->point_enu.up_m)
            : 0.0;
        const double measured_suspension_length = body_up_normal_dot > 1e-6
            ? (mount_normal_clearance - parameters_.tire_radius_m)
                / body_up_normal_dot
            : std::numeric_limits<double>::infinity();
        const double maximum_suspension_length =
            parameters_.suspension.rest_length_m
            + parameters_.suspension.max_extension_m;
        const bool suspension_contact = valid_surface_hit
            && body_up_normal_dot > 1e-6
            && std::isfinite(measured_suspension_length)
            && measured_suspension_length <= maximum_suspension_length;
        const double minimum_suspension_length =
            parameters_.suspension.rest_length_m
            - parameters_.suspension.max_compression_m;
        const double compression_stop_clearance = mount_normal_clearance
            - parameters_.tire_radius_m
            - minimum_suspension_length * body_up_normal_dot;
        compression_stop_active_[index] = suspension_contact
            && compression_stop_clearance <= HARD_STOP_ACTIVATION_SLOP_M;

        if (valid_surface_hit) {
            normal_sum.east_m += normal_east;
            normal_sum.north_m += normal_north;
            normal_sum.up_m += normal_up;
            ++surface_hit_count;

            if (body_up_normal_dot > 1e-6) {
                hard_stop_constraints[index] = {
                    true,
                    {normal_east, normal_north, normal_up},
                    hit->point_enu,
                    wheel_x[index],
                    wheel_y[index]};
            }
        }

        wheel.in_contact = suspension_contact;
        if (!suspension_contact) {
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

        wheel.contact_normal_enu = {
            normal_east,
            normal_north,
            normal_up};
        contact_normal_sum.east_m += normal_east;
        contact_normal_sum.north_m += normal_north;
        contact_normal_sum.up_m += normal_up;
        ++contact_normal_count;
        // Suspension travel follows body-up. Solve its intersection with the
        // sampled tangent plane, then publish the actual round-tire patch.
        // Consumers reconstruct this exact centre as patch + normal * radius.
        const double tire_center_east = mount_east
            - body_basis.up[0] * measured_suspension_length;
        const double tire_center_north = mount_north
            - body_basis.up[1] * measured_suspension_length;
        const double tire_center_up = mount_up
            - body_basis.up[2] * measured_suspension_length;
        const double tire_center_delta_east = tire_center_east - east_m_;
        const double tire_center_delta_north = tire_center_north - north_m_;
        wheel_support_points[wheel_support_point_count++] = {
            tire_center_delta_east * heading_sine
                + tire_center_delta_north * heading_cosine,
            tire_center_delta_east * heading_cosine
                - tire_center_delta_north * heading_sine,
            tire_center_up};
        wheel.contact_point_enu = {
            tire_center_east - normal_east * parameters_.tire_radius_m,
            tire_center_north - normal_north * parameters_.tire_radius_m,
            tire_center_up - normal_up * parameters_.tire_radius_m};

        const float measured_suspension_length_m =
            static_cast<float>(measured_suspension_length);
        if (update_suspension) {
            const auto suspension = simcore_host::evaluate_suspension(
                parameters_.suspension,
                measured_suspension_length_m,
                suspension_compression_m_[index],
                suspension_had_contact_[index],
                dt_seconds);
            suspension_compression_m_[index] = suspension.compression_m;
            suspension_base_force_n_[index] = suspension.normal_force_n;
            suspension_had_contact_[index] = true;
            wheel.normal_load = std::max(
                suspension.normal_force_n, MIN_CONTACT_NORMAL_LOAD_N);
        } else if (!had_published_contact || wheel.normal_load <= 0.f) {
            // A penetration lift can restore a wheel after the force-sampling
            // pass. Seed its load immediately so a published contact never
            // carries a contradictory zero normal force.
            const auto suspension = simcore_host::evaluate_suspension(
                parameters_.suspension,
                measured_suspension_length_m,
                suspension_compression_m_[index],
                false,
                0.f);
            wheel.normal_load = std::max(
                suspension.normal_force_n, MIN_CONTACT_NORMAL_LOAD_N);
        }
    }

    ground_query_hit_count_ = surface_hit_count;

    if (surface_hit_count >= 1 && normal_sum.up_m > 1e-9) {
        const double east_grade = -normal_sum.east_m / normal_sum.up_m;
        const double north_grade = -normal_sum.north_m / normal_sum.up_m;
        const double forward_grade = east_grade * heading_sine
                                   + north_grade * heading_cosine;
        const double left_grade = -east_grade * heading_cosine
                                + north_grade * heading_sine;
        ground_pitch_rad_ = static_cast<float>(std::atan(forward_grade));
        ground_roll_rad_ = static_cast<float>(std::atan(left_grade));
    }

    if (update_attitude_target) {
        double support_forward_grade = 0.0;
        double support_right_grade = 0.0;
        bool has_support_target = false;
        if (contact_normal_count >= 1 && contact_normal_sum.up_m > 1e-9) {
            const double east_grade =
                -contact_normal_sum.east_m / contact_normal_sum.up_m;
            const double north_grade =
                -contact_normal_sum.north_m / contact_normal_sum.up_m;
            support_forward_grade = east_grade * heading_sine
                                  + north_grade * heading_cosine;
            const double left_grade = -east_grade * heading_cosine
                                    + north_grade * heading_sine;
            support_right_grade = -left_grade;
            has_support_target = true;
        } else if (!support_attitude_valid_ && surface_hit_count >= 1) {
            // Reset/recovery may begin just outside suspension droop. Use the
            // verified terrain basis once to seed a target; after that, zero
            // contact freezes the last valid wheel-supported attitude.
            support_forward_grade = std::tan(ground_pitch_rad_);
            support_right_grade = -std::tan(ground_roll_rad_);
            has_support_target = true;
        }

        if (has_support_target) {
            // Local normals orient each individual tire but do not describe
            // the plane spanned by tires at different heights. The centre fit
            // makes a front axle on a ramp or one side on a bump drive chassis
            // pitch/roll. Contact normals remain the deterministic prior for
            // rank-deficient one/two-wheel support.
            fit_wheel_support_grades(
                wheel_support_points,
                wheel_support_point_count,
                support_forward_grade,
                support_right_grade,
                support_forward_grade,
                support_right_grade);
            const double maximum_grade =
                std::tan(MAX_WHEEL_SUPPORT_ATTITUDE_RAD);
            support_forward_grade = std::clamp(
                support_forward_grade, -maximum_grade, maximum_grade);
            support_right_grade = std::clamp(
                support_right_grade, -maximum_grade, maximum_grade);
            support_pitch_rad_ = static_cast<float>(
                std::atan(support_forward_grade));
            support_roll_rad_ = static_cast<float>(
                std::atan(-support_right_grade));
            support_attitude_valid_ = true;
        }
    }

    if (enforce_non_penetration && surface_hit_count > 0) {
        // A tire/suspension compression stop is a unilateral constraint on
        // the sprung body, not a world-Z teleport. Solve q={z, roll, pitch}
        // with bounded simultaneous projected position iterations. A front
        // corner therefore raises/pitches the body and a side corner raises/
        // rolls it, avoiding both terrain penetration and the old "floating
        // body" response where every impact lifted the chassis only in Z.
        const double minimum_suspension_length =
            parameters_.suspension.rest_length_m
            - parameters_.suspension.max_compression_m;
        bool corrected = false;
        // Ordinary road penetration converges in a few passes.  Keep enough
        // iterations for deterministic recovery after a temporarily missing
        // GroundQuery (up to the documented four-metre probe envelope), where
        // the initial violation can be much larger than a normal time step.
        constexpr int constraint_iterations = 32;
        constexpr double penetration_tolerance_m = 1e-6;
        constexpr double maximum_constraint_translation_step_m = 0.15;
        constexpr double maximum_constraint_rotation_step_rad =
            2.0 * DEG2RAD;
        for (int iteration = 0; iteration < constraint_iterations; ++iteration) {
            // Evaluate every corner from the same body pose and apply the
            // averaged correction.  A sequential full correction is
            // order-dependent for a deep, symmetric recovery and can choose a
            // needless one-side-up solution even though pure heave satisfies
            // all four constraints.
            const BodyBasis constrained_basis = make_body_basis(
                heading_rad_, pitch_rad_, roll_rad_);
            double accumulated_z_correction = 0.0;
            double accumulated_roll_correction = 0.0;
            double accumulated_pitch_correction = 0.0;
            std::size_t correction_count = 0;
            for (const auto& constraint : hard_stop_constraints) {
                if (!constraint.valid) {
                    continue;
                }

                const std::array<double, 3> stop_offset{
                    constraint.wheel_x_m * constrained_basis.forward[0]
                        + constraint.wheel_y_m * constrained_basis.right[0]
                        - minimum_suspension_length * constrained_basis.up[0],
                    constraint.wheel_x_m * constrained_basis.forward[1]
                        + constraint.wheel_y_m * constrained_basis.right[1]
                        - minimum_suspension_length * constrained_basis.up[1],
                    constraint.wheel_x_m * constrained_basis.forward[2]
                        + constraint.wheel_y_m * constrained_basis.right[2]
                        - minimum_suspension_length * constrained_basis.up[2]};
                const std::array<double, 3> stop_world{
                    east_m_ + stop_offset[0],
                    north_m_ + stop_offset[1],
                    state_.position_enu.z + stop_offset[2]};
                const double clearance =
                    constraint.normal[0]
                        * (stop_world[0] - constraint.point.east_m)
                    + constraint.normal[1]
                        * (stop_world[1] - constraint.point.north_m)
                    + constraint.normal[2]
                        * (stop_world[2] - constraint.point.up_m)
                    - parameters_.tire_radius_m;
                if (clearance >= -penetration_tolerance_m) {
                    continue;
                }

                const double jacobian_z = constraint.normal[2];
                const double jacobian_roll = dot_product(
                    constraint.normal,
                    cross_product(constrained_basis.forward, stop_offset));
                const auto pitch_axis = make_horizontal_right(heading_rad_);
                const double jacobian_pitch = dot_product(
                    constraint.normal,
                    cross_product(pitch_axis, stop_offset));
                const double pitch_inertia = generalized_pitch_inertia(
                    parameters_, roll_rad_);
                const double effective_inverse_mass =
                    jacobian_z * jacobian_z / parameters_.mass_kg
                    + jacobian_roll * jacobian_roll
                        / parameters_.roll_inertia_kg_m2
                    + jacobian_pitch * jacobian_pitch
                        / pitch_inertia;
                if (!std::isfinite(effective_inverse_mass)
                    || effective_inverse_mass <= 1e-12) {
                    continue;
                }

                const double multiplier = -clearance / effective_inverse_mass;
                accumulated_z_correction +=
                    multiplier * jacobian_z / parameters_.mass_kg;
                accumulated_roll_correction += multiplier * jacobian_roll
                    / parameters_.roll_inertia_kg_m2;
                accumulated_pitch_correction += multiplier * jacobian_pitch
                    / pitch_inertia;
                ++correction_count;
            }
            if (correction_count == 0) {
                break;
            }
            const double inverse_correction_count =
                1.0 / static_cast<double>(correction_count);
            state_.position_enu.z += std::clamp(
                accumulated_z_correction * inverse_correction_count,
                -maximum_constraint_translation_step_m,
                maximum_constraint_translation_step_m);
            roll_rad_ += static_cast<float>(
                std::clamp(
                    accumulated_roll_correction * inverse_correction_count,
                    -maximum_constraint_rotation_step_rad,
                    maximum_constraint_rotation_step_rad));
            pitch_rad_ += static_cast<float>(
                std::clamp(
                    accumulated_pitch_correction * inverse_correction_count,
                    -maximum_constraint_rotation_step_rad,
                    maximum_constraint_rotation_step_rad));
            corrected = true;
        }

        // The trust region above protects a lone/deep hit from turning one
        // linearized solve into a 30+ degree attitude jump.  If its bounded
        // iterations still leave a residual, finish with the minimum pure-Z
        // lift that satisfies every up-facing tangent plane. This emergency
        // path favors a temporary heave correction over a flipped chassis.
        const BodyBasis residual_basis = make_body_basis(
            heading_rad_, pitch_rad_, roll_rad_);
        double residual_z_lift_m = 0.0;
        for (const auto& constraint : hard_stop_constraints) {
            if (!constraint.valid || constraint.normal[2] <= 1e-9) {
                continue;
            }
            const std::array<double, 3> stop_offset{
                constraint.wheel_x_m * residual_basis.forward[0]
                    + constraint.wheel_y_m * residual_basis.right[0]
                    - minimum_suspension_length * residual_basis.up[0],
                constraint.wheel_x_m * residual_basis.forward[1]
                    + constraint.wheel_y_m * residual_basis.right[1]
                    - minimum_suspension_length * residual_basis.up[1],
                constraint.wheel_x_m * residual_basis.forward[2]
                    + constraint.wheel_y_m * residual_basis.right[2]
                    - minimum_suspension_length * residual_basis.up[2]};
            const double clearance =
                constraint.normal[0]
                    * (east_m_ + stop_offset[0] - constraint.point.east_m)
                + constraint.normal[1]
                    * (north_m_ + stop_offset[1] - constraint.point.north_m)
                + constraint.normal[2]
                    * (state_.position_enu.z + stop_offset[2]
                       - constraint.point.up_m)
                - parameters_.tire_radius_m;
            residual_z_lift_m = std::max(
                residual_z_lift_m,
                (-clearance + penetration_tolerance_m)
                    / constraint.normal[2]);
        }
        if (residual_z_lift_m > 0.0) {
            state_.position_enu.z += residual_z_lift_m;
            corrected = true;
        }

        if (corrected) {
            // Remove only the velocity component that would immediately drive
            // a hard-stop corner back into the surface (zero restitution).
            const GroundBasis local_ground_basis = make_ground_basis(
                heading_rad_, ground_pitch_rad_, ground_roll_rad_);
            const double normal_up_for_rate = std::max(
                local_ground_basis.normal[2], 1e-6);
            double root_z_rate_mps = vertical_speed_mps_ / normal_up_for_rate;
            // The three generalized rates couple every corner. Revisit the
            // constraints until all are separating; a single forward sweep
            // can make an earlier corner approach again and inject an
            // order-dependent roll/pitch rate into a symmetric four-wheel hit.
            constexpr int velocity_constraint_iterations = 32;
            constexpr double impulse_delta_tolerance = 1e-8;
            struct VelocityConstraint {
                bool active = false;
                double jacobian_z = 0.0;
                double jacobian_roll = 0.0;
                double jacobian_pitch = 0.0;
                double effective_inverse_mass = 0.0;
            };
            std::array<VelocityConstraint, 4> velocity_constraints{};
            const BodyBasis velocity_basis = make_body_basis(
                heading_rad_, pitch_rad_, roll_rad_);
            const auto pitch_axis = make_horizontal_right(heading_rad_);
            const double pitch_inertia = generalized_pitch_inertia(
                parameters_, roll_rad_);
            for (std::size_t constraint_index = 0;
                 constraint_index < hard_stop_constraints.size();
                 ++constraint_index) {
                const auto& constraint = hard_stop_constraints[constraint_index];
                if (!constraint.valid) {
                    continue;
                }
                const std::array<double, 3> stop_offset{
                    constraint.wheel_x_m * velocity_basis.forward[0]
                        + constraint.wheel_y_m * velocity_basis.right[0]
                        - minimum_suspension_length * velocity_basis.up[0],
                    constraint.wheel_x_m * velocity_basis.forward[1]
                        + constraint.wheel_y_m * velocity_basis.right[1]
                        - minimum_suspension_length * velocity_basis.up[1],
                    constraint.wheel_x_m * velocity_basis.forward[2]
                        + constraint.wheel_y_m * velocity_basis.right[2]
                        - minimum_suspension_length * velocity_basis.up[2]};
                const double clearance =
                    constraint.normal[0]
                        * (east_m_ + stop_offset[0] - constraint.point.east_m)
                    + constraint.normal[1]
                        * (north_m_ + stop_offset[1] - constraint.point.north_m)
                    + constraint.normal[2]
                        * (state_.position_enu.z + stop_offset[2]
                           - constraint.point.up_m)
                    - parameters_.tire_radius_m;
                if (clearance > HARD_STOP_ACTIVATION_SLOP_M) {
                    // A valid ray is not necessarily a compressed hard stop.
                    // Do not lock suspension travel at unloaded corners.
                    continue;
                }
                auto& velocity_constraint =
                    velocity_constraints[constraint_index];
                velocity_constraint.active = true;
                velocity_constraint.jacobian_z = constraint.normal[2];
                velocity_constraint.jacobian_roll = dot_product(
                    constraint.normal,
                    cross_product(velocity_basis.forward, stop_offset));
                velocity_constraint.jacobian_pitch = dot_product(
                    constraint.normal,
                    cross_product(pitch_axis, stop_offset));
                velocity_constraint.effective_inverse_mass =
                    velocity_constraint.jacobian_z
                        * velocity_constraint.jacobian_z / parameters_.mass_kg
                    + velocity_constraint.jacobian_roll
                        * velocity_constraint.jacobian_roll
                        / parameters_.roll_inertia_kg_m2
                    + velocity_constraint.jacobian_pitch
                        * velocity_constraint.jacobian_pitch / pitch_inertia;
            }
            std::array<double, 4> accumulated_constraint_impulses{};
            for (int velocity_iteration = 0;
                 velocity_iteration < velocity_constraint_iterations;
                 ++velocity_iteration) {
                double maximum_impulse_delta = 0.0;
                for (std::size_t sweep_index = 0;
                     sweep_index < hard_stop_constraints.size();
                     ++sweep_index) {
                    const std::size_t constraint_index =
                        velocity_iteration % 2 == 0
                            ? sweep_index
                            : hard_stop_constraints.size() - 1 - sweep_index;
                    const auto& constraint =
                        velocity_constraints[constraint_index];
                    if (!constraint.active
                        || constraint.effective_inverse_mass <= 1e-12) {
                        continue;
                    }
                    const double approaching_rate =
                        constraint.jacobian_z * root_z_rate_mps
                        + constraint.jacobian_roll * roll_rate_rad_s_
                        + constraint.jacobian_pitch * pitch_rate_rad_s_;
                    // Project the accumulated unilateral impulse, not only the
                    // current approaching velocity. This lets later corners
                    // reduce an earlier over-impulse and converges to the
                    // symmetric LCP solution instead of an order-biased one.
                    const double previous_impulse =
                        accumulated_constraint_impulses[constraint_index];
                    const double next_impulse = std::max(
                        0.0,
                        previous_impulse
                            - approaching_rate
                                / constraint.effective_inverse_mass);
                    const double impulse_delta =
                        next_impulse - previous_impulse;
                    accumulated_constraint_impulses[constraint_index] =
                        next_impulse;
                    maximum_impulse_delta = std::max(
                        maximum_impulse_delta, std::abs(impulse_delta));
                    if (std::abs(impulse_delta) <= impulse_delta_tolerance) {
                        continue;
                    }
                    root_z_rate_mps += impulse_delta * constraint.jacobian_z
                        / parameters_.mass_kg;
                    roll_rate_rad_s_ += static_cast<float>(
                        impulse_delta * constraint.jacobian_roll
                            / parameters_.roll_inertia_kg_m2);
                    pitch_rate_rad_s_ += static_cast<float>(
                        impulse_delta * constraint.jacobian_pitch
                            / pitch_inertia);
                }
                if (maximum_impulse_delta <= impulse_delta_tolerance) {
                    break;
                }
            }
            if (tick_hard_stop_impulses) {
                // Each projection has already changed velocity. Accumulate its
                // final unilateral reaction once, including both the heave and
                // attitude solves and any surface re-query passes this tick.
                for (std::size_t index = 0;
                     index < accumulated_constraint_impulses.size(); ++index) {
                    (*tick_hard_stop_impulses)[index] +=
                        accumulated_constraint_impulses[index];
                }
            }
            vertical_speed_mps_ = static_cast<float>(
                root_z_rate_mps * normal_up_for_rate);
            state_.pitch = static_cast<float>(pitch_rad_ * RAD2DEG);
            state_.roll = static_cast<float>(roll_rad_ * RAD2DEG);
            state_.angular_velocity_body = make_body_angular_velocity(
                pitch_rad_, roll_rad_, pitch_rate_rad_s_, roll_rate_rad_s_,
                simcore_host::BodyFrameAdapter::solver_heading_rate_to_canonical_yaw_rate(
                    solver_yaw_rate_rad_s_));
            state_.yaw_rate = static_cast<float>(
                state_.angular_velocity_body.z);
            return true;
        }
    }
    return false;
}
