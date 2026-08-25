#include "physics/vehicle_physics.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr double kInitialLat = 40.7069;
constexpr double kInitialLon = -74.0095;

class PlaneGroundQuery final : public simcore_host::GroundQuery {
public:
    PlaneGroundQuery(double east_slope, double north_slope, double intercept)
        : east_slope_(east_slope)
        , north_slope_(north_slope)
        , intercept_(intercept)
    {
    }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const double height = intercept_
            + east_slope_ * request.origin_enu.east_m
            + north_slope_ * request.origin_enu.north_m;
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        // Deliberately return a non-unit normal; VehiclePhysics owns the
        // normalization at the GroundQuery boundary.
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {-east_slope_, -north_slope_, 1.0},
            distance};
    }

private:
    double east_slope_ = 0.0;
    double north_slope_ = 0.0;
    double intercept_ = 0.0;
};

class NoGroundQuery final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest&) const override
    {
        return std::nullopt;
    }
};

class WestHalfGroundQuery final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (request.origin_enu.east_m >= 0.0) {
            return std::nullopt;
        }
        const double distance = request.origin_enu.up_m;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, 0.0},
            {0.0, 0.0, 1.0},
            distance};
    }
};

class DownwardNormalGroundQuery final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, 0.0},
            {0.0, 0.0, -1.0},
            request.origin_enu.up_m};
    }
};

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void advance(VehiclePhysics& physics, int steps)
{
    for (int i = 0; i < steps; ++i) {
        physics.update(kDt);
    }
}

VehiclePhysics make_vehicle()
{
    return VehiclePhysics(kInitialLat, kInitialLon, 0.0, 0.f);
}

double signed_heading_delta_radians(float current_degrees, float previous_degrees)
{
    double delta = std::fmod(
        static_cast<double>(current_degrees - previous_degrees) + 540.0,
        360.0) - 180.0;
    return delta * std::numbers::pi_v<double> / 180.0;
}

void test_initial_state_is_a_complete_four_wheel_snapshot()
{
    VehicleParameters parameters;
    VehiclePhysics vehicle(kInitialLat, kInitialLon, 7.0, 0.f, parameters);
    const auto state = vehicle.get_state();

    require(state.position_enu.z == parameters.cg_height_m,
            "initial chassis position must include CG height");
    float total_load = 0.f;
    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        require(wheel.wheel_index == index,
                "initial wheel indices must be unique and canonical");
        require(wheel.in_contact, "initial flat-ground wheels must be in contact");
        require(wheel.normal_load > 0.f,
                "initial wheels must publish their static normal load");
        require(wheel.contact_point_enu.z == 0.0,
                "default FlatGroundQuery must publish a zero-height hit");
        require(wheel.contact_normal_enu.x == 0.0
                && wheel.contact_normal_enu.y == 0.0
                && wheel.contact_normal_enu.z == 1.0,
                "default FlatGroundQuery must publish an ENU up normal");
        total_load += wheel.normal_load;
    }
    require(std::abs(total_load - parameters.mass_kg * 9.80665f) < 1.f,
            "initial wheel loads must sum to vehicle weight");
}

void test_suspension_model_clamps_stroke_and_force()
{
    simcore_host::SuspensionParameters parameters;
    parameters.rest_length_m = 0.4f;
    parameters.max_compression_m = 0.1f;
    parameters.max_extension_m = 0.2f;
    parameters.spring_rate_n_per_m = 10000.f;
    parameters.damper_rate_n_s_per_m = 500.f;
    parameters.max_force_n = 750.f;

    const auto compressed = simcore_host::evaluate_suspension(
        parameters, -10.f, 0.f, false, 0.01f);
    require(std::abs(compressed.bounded_length_m - 0.3f) < 1e-6f,
            "suspension length must stop at the compression stroke bound");
    require(std::abs(compressed.compression_m - 0.1f) < 1e-6f,
            "suspension compression must be bounded");
    require(compressed.normal_force_n == parameters.max_force_n,
            "spring force must stop at the configured maximum");

    const auto extended = simcore_host::evaluate_suspension(
        parameters, 10.f, compressed.compression_m, true, 0.1f);
    require(std::abs(extended.bounded_length_m - 0.6f) < 1e-6f,
            "suspension length must stop at the extension stroke bound");
    require(std::abs(extended.compression_m + 0.2f) < 1e-6f,
            "extension must be represented as bounded negative compression");
    require(extended.normal_force_n == 0.f,
            "an extended spring cannot pull the ground toward the vehicle");

    const auto repeated = simcore_host::evaluate_suspension(
        parameters, 0.35f, 0.02f, true, 0.1f);
    const auto repeated_again = simcore_host::evaluate_suspension(
        parameters, 0.35f, 0.02f, true, 0.1f);
    require(repeated.bounded_length_m == repeated_again.bounded_length_m
            && repeated.compression_m == repeated_again.compression_m
            && repeated.compression_velocity_mps
                == repeated_again.compression_velocity_mps
            && repeated.normal_force_n == repeated_again.normal_force_n,
            "identical suspension inputs must produce bit-identical results");
}

void test_invalid_suspension_parameters_are_rejected()
{
    VehicleParameters parameters;
    parameters.suspension.rest_length_m =
        parameters.suspension.max_compression_m - 0.01f;
    bool rejected = false;
    try {
        VehiclePhysics vehicle(
            kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "vehicle construction must reject an impossible suspension stroke");
}

void test_ground_query_publishes_sloped_hit_and_normal()
{
    constexpr double east_slope = 0.02;
    constexpr double north_slope = -0.01;
    auto ground = std::make_shared<PlaneGroundQuery>(east_slope, north_slope, 0.0);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    const auto state = vehicle.get_state();

    for (const auto& wheel : state.wheels) {
        require(wheel.in_contact, "a shallow plane must contact every wheel");
        const double expected_height = east_slope * wheel.contact_point_enu.x
            + north_slope * wheel.contact_point_enu.y;
        require(std::abs(wheel.contact_point_enu.z - expected_height) < 1e-9,
                "GroundQuery hit height must be published without flattening");
        const double normal_length = std::hypot(
            wheel.contact_normal_enu.x,
            wheel.contact_normal_enu.y,
            wheel.contact_normal_enu.z);
        require(std::abs(normal_length - 1.0) < 1e-9,
                "published ground normal must be normalized");
        require(wheel.contact_normal_enu.x < 0.0
                && wheel.contact_normal_enu.y > 0.0
                && wheel.contact_normal_enu.z > 0.0,
                "published normal must retain the slope direction");
    }
}

void test_invalid_ground_normal_is_rejected()
{
    auto ground = std::make_shared<DownwardNormalGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    const auto state = vehicle.get_state();

    for (const auto& wheel : state.wheels) {
        require(!wheel.in_contact,
                "a non-up-facing ground normal must not create contact");
        require(wheel.normal_load == 0.f,
                "an invalid ground normal must not create suspension load");
    }
}

void test_no_ground_removes_all_tire_forces()
{
    auto ground = std::make_shared<NoGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    advance(vehicle, 60);

    const auto state = vehicle.get_state();
    require(state.speed == 0.f,
            "free-spinning driven wheels must not accelerate an airborne chassis");
    for (const auto& wheel : state.wheels) {
        require(!wheel.in_contact, "missing GroundQuery hits must publish no contact");
        require(wheel.normal_load == 0.f,
                "an airborne wheel must publish zero normal load");
        require(wheel.longitudinal_force == 0.f && wheel.lateral_force == 0.f,
                "an airborne wheel must not generate tire force");
        require(wheel.longitudinal_slip == 0.f && wheel.slip_angle == 0.f,
                "airborne tire slip is undefined and must publish zero");
    }
}

void test_partial_ground_only_loads_hit_wheels()
{
    auto ground = std::make_shared<WestHalfGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 3);
    const auto state = vehicle.get_state();

    require(state.wheels[0].in_contact && state.wheels[2].in_contact,
            "west-side left wheels must receive ground hits");
    require(!state.wheels[1].in_contact && !state.wheels[3].in_contact,
            "east-side right wheels must remain airborne");
    require(state.wheels[0].normal_load > 0.f
            && state.wheels[2].normal_load > 0.f,
            "each hit wheel must retain its independent suspension load");
    for (std::size_t index : {std::size_t{1}, std::size_t{3}}) {
        const auto& wheel = state.wheels[index];
        require(wheel.normal_load == 0.f
                && wheel.longitudinal_force == 0.f
                && wheel.lateral_force == 0.f,
                "each no-hit wheel must independently publish zero load and force");
    }
}

void test_vehicle_suspension_force_respects_configured_bound()
{
    VehicleParameters parameters;
    parameters.suspension.spring_rate_n_per_m = 100000.f;
    parameters.suspension.max_force_n = 5000.f;
    auto raised_ground = std::make_shared<simcore_host::FlatGroundQuery>(0.2);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, raised_ground);
    const auto state = vehicle.get_state();

    for (const auto& wheel : state.wheels) {
        require(wheel.in_contact, "raised flat ground must remain inside the query range");
        require(wheel.normal_load == parameters.suspension.max_force_n,
                "published suspension load must respect the configured force bound");
    }
}

void test_idle_and_handbrake_are_stable()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 120);

    const auto state = vehicle.get_state();
    require(state.speed == 0.f, "handbrake at rest must not create reverse speed");
    require(state.east == 0.0 && state.north == 0.0,
            "stationary vehicle must not move");
    for (const auto& wheel : state.wheels) {
        require(wheel.angular_speed == 0.f,
                "stationary chassis must publish zero wheel angular speed");
    }
}

void test_throttle_accelerates_and_input_is_clamped()
{
    auto normal = make_vehicle();
    auto clamped = make_vehicle();

    VehicleInput normal_input;
    normal_input.throttle = 1.f;
    normal.set_input(normal_input);

    VehicleInput oversized_input;
    oversized_input.throttle = 10.f;
    clamped.set_input(oversized_input);

    advance(normal, 180);
    advance(clamped, 180);

    const auto normal_state = normal.get_state();
    const auto clamped_state = clamped.get_state();
    require(normal_state.speed > 10.f, "full throttle must accelerate the vehicle");
    require(std::abs(normal_state.speed - clamped_state.speed) < 1e-5f,
            "throttle input must be clamped to one");
    require(normal_state.north > 0.0 && normal_state.lat > kInitialLat,
            "heading zero must move north in local ENU and WGS84 output");
}

void test_service_brake_stops_without_reversing()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.throttle = 0.f;
    input.brake = 1.f;
    vehicle.set_input(input);
    for (int i = 0; i < 300; ++i) {
        const auto state = vehicle.update(kDt);
        require(state.speed >= 0.f, "service brake must not engage reverse");
    }

    require(vehicle.get_state().speed == 0.f, "service brake must stop the vehicle");
    for (const auto& wheel : vehicle.get_state().wheels) {
        require(wheel.angular_speed == 0.f,
                "stopped vehicle must not publish residual wheel rotation");
    }
}

void test_reverse_requires_reverse_gear()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.gear = VehicleGear::Reverse;
    input.throttle = 0.6f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    const auto state = vehicle.get_state();
    require(state.speed < -1.f, "reverse throttle must produce negative speed");
    require(state.north < 0.0, "reverse at heading zero must move south");
    require(state.gear == VehicleGear::Reverse, "state must expose the active gear");
}

void test_bicycle_model_turns_right()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = -0.15f;
    vehicle.set_input(input);
    advance(vehicle, 60);

    const auto before = vehicle.get_state();
    const auto state = vehicle.update(kDt);
    require(state.steering_angle < 0.f,
            "right steering must have a negative canonical wheel angle");
    require(state.yaw_rate < 0.f,
            "right steering must produce a negative canonical yaw rate");
    require(state.angular_velocity_body.z < 0.0,
            "canonical body angular Z must be negative for a right turn");
    require(std::abs(state.angular_velocity_body.z - state.yaw_rate) < 1e-6,
            "scalar yaw rate and FLU angular Z must share one sign contract");
    require(state.heading > 0.f && state.heading < 180.f,
            "right steering must rotate clockwise from north");
    require(state.east > 0.0, "right turn from north must move east");
    const double heading_rate = signed_heading_delta_radians(
        state.heading, before.heading) / kDt;
    require(std::abs(heading_rate + state.yaw_rate) < 1e-3,
            "clockwise navigation heading rate must be negative FLU body yaw rate");

    const double heading_rad = state.heading * std::numbers::pi_v<double> / 180.0;
    const double expected_east_velocity = state.linear_velocity_body.x * std::sin(heading_rad)
                                        - state.linear_velocity_body.y * std::cos(heading_rad);
    const double expected_north_velocity = state.linear_velocity_body.x * std::cos(heading_rad)
                                         + state.linear_velocity_body.y * std::sin(heading_rad);
    require(std::abs((state.east - before.east) / kDt - expected_east_velocity) < 1e-4,
            "Y-left body velocity must reconstruct ENU east velocity");
    require(std::abs((state.north - before.north) / kDt - expected_north_velocity) < 1e-4,
            "Y-left body velocity must reconstruct ENU north velocity");

    const double roll_rate_from_angle = (state.roll - before.roll)
        * std::numbers::pi_v<double> / 180.0 / kDt;
    require(std::abs(roll_rate_from_angle - state.angular_velocity_body.x) < 1e-3,
            "canonical roll angle and body angular X must use the same sign");
}

void test_positive_steering_turns_left_in_the_public_contract()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = 0.15f;
    vehicle.set_input(input);
    advance(vehicle, 60);

    const auto before = vehicle.get_state();
    const auto state = vehicle.update(kDt);
    require(state.steering_angle > 0.f,
            "positive canonical steering must steer left");
    require(state.yaw_rate > 0.f,
            "a forward left turn must have positive FLU body yaw");
    require(std::abs(state.angular_velocity_body.z - state.yaw_rate) < 1e-6,
            "published angular Z must equal the scalar body yaw rate");
    require(state.heading > 180.f,
            "left steering from north must reduce clockwise heading through 360 degrees");
    require(state.east < 0.0, "left turn from north must move west");

    const double heading_rate = signed_heading_delta_radians(
        state.heading, before.heading) / kDt;
    require(std::abs(heading_rate + state.yaw_rate) < 1e-3,
            "navigation heading and FLU body yaw must have opposite signs");

    const float left_load = state.wheels[0].normal_load + state.wheels[2].normal_load;
    const float right_load = state.wheels[1].normal_load + state.wheels[3].normal_load;
    require(right_load > left_load,
            "a left turn must load the outer right wheels");
    require(state.wheels[0].steering_angle > 0.f
            && state.wheels[1].steering_angle > 0.f,
            "front wheel state must publish left steering as positive");
}

void test_left_steering_publishes_wheel_local_force_signs()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = 0.15f;
    vehicle.set_input(input);
    const auto state = vehicle.update(kDt);

    for (std::size_t index = 0; index < 2; ++index) {
        require(state.wheels[index].lateral_force > 0.f,
                "left steering must initially produce left-positive front tire force");
        require(state.wheels[index].slip_angle < 0.f,
                "left steering must publish the opposing wheel-local slip angle");
    }
}

void test_reverse_keeps_command_sign_but_reverses_vehicle_yaw_response()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.gear = VehicleGear::Reverse;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = 0.15f;
    vehicle.set_input(input);
    const auto state = vehicle.update(kDt);

    require(state.steering_angle > 0.f,
            "left steering command must remain positive while reversing");
    require(state.yaw_rate < 0.f,
            "reverse motion must invert the yaw response to a left steering angle");
}

void test_right_turn_loads_outer_left_wheels_without_doubling_transfer()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.6f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = -0.12f;
    vehicle.set_input(input);
    advance(vehicle, 45);

    const auto state = vehicle.get_state();
    const float left_load = state.wheels[0].normal_load + state.wheels[2].normal_load;
    const float right_load = state.wheels[1].normal_load + state.wheels[3].normal_load;
    const float total_load = left_load + right_load;
    require(state.yaw_rate < 0.f, "test setup must produce a right turn");
    require(left_load > right_load,
            "a right turn must load the outer left wheels");
    require(std::abs(total_load - 1500.f * 9.80665f) < 1.f,
            "lateral transfer must redistribute rather than add normal load");
}

void test_four_wheel_contact_and_force_limits()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.7f;
    input.steering = 0.2f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    const auto state = vehicle.get_state();
    require(state.linear_velocity_body.x > 0.0,
            "body-frame longitudinal velocity must be published");
    require(state.position_enu.y > 0.0,
            "3D ENU position must track north travel");
    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        require(wheel.in_contact, "flat-ground wheel must remain in contact");
        require(wheel.normal_load > 0.f, "contact wheel must carry normal load");
        require(std::isfinite(wheel.slip_angle), "wheel slip angle must be finite");
        const float force = std::hypot(wheel.longitudinal_force, wheel.lateral_force);
        require(force <= wheel.normal_load + 1e-3f,
                "tire force must remain inside the friction circle");
        if (index < 2) {
            require(wheel.steering_angle > 0.f, "front wheels must steer left");
        } else {
            require(wheel.steering_angle == 0.f, "rear wheels must not steer");
        }
    }
    require(std::abs(state.wheels[2].longitudinal_slip) > 1e-5f ||
            std::abs(state.wheels[3].longitudinal_slip) > 1e-5f,
            "driven wheels must publish longitudinal slip under throttle");
    require(state.wheels[0].angular_speed == state.wheels[1].angular_speed,
            "front left/right wheel angular speeds must be synchronized");
    require(state.wheels[2].angular_speed == state.wheels[3].angular_speed,
            "rear left/right wheel angular speeds must be synchronized");
}

void test_contact_points_use_the_published_post_integration_pose()
{
    VehicleParameters parameters;
    VehiclePhysics vehicle(kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    VehicleInput input;
    input.throttle = 0.7f;
    input.steering = 0.2f;
    vehicle.set_input(input);
    advance(vehicle, 180);

    const auto state = vehicle.get_state();
    const double heading = state.heading * std::numbers::pi_v<double> / 180.0;
    const double half_wheelbase = parameters.wheelbase_m * 0.5;
    const double expected_front_left_east = state.east
        + half_wheelbase * std::sin(heading)
        - parameters.front_track_m * 0.5 * std::cos(heading);
    const double expected_front_left_north = state.north
        + half_wheelbase * std::cos(heading)
        + parameters.front_track_m * 0.5 * std::sin(heading);

    require(std::abs(state.wheels[0].contact_point_enu.x
                     - expected_front_left_east) < 1e-6,
            "wheel contact east coordinate must use the published chassis pose");
    require(std::abs(state.wheels[0].contact_point_enu.y
                     - expected_front_left_north) < 1e-6,
            "wheel contact north coordinate must use the published chassis pose");
}

void test_braking_settles_lateral_and_yaw_motion()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.7f;
    vehicle.set_input(input);
    advance(vehicle, 180);

    input.steering = 0.25f;
    vehicle.set_input(input);
    advance(vehicle, 90);

    input.throttle = 0.f;
    input.steering = 0.f;
    input.brake = 1.f;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 600);

    const auto state = vehicle.get_state();
    require(state.speed == 0.f, "brakes must settle longitudinal motion");
    require(state.linear_velocity_body.y == 0.0,
            "brakes must settle residual lateral motion");
    require(state.yaw_rate == 0.f,
            "brakes must settle residual yaw motion");
}

void test_driveline_does_not_wind_up_at_speed_limiter()
{
    const auto run_to_limiter = [](VehicleGear gear) {
        VehicleParameters parameters;
        VehiclePhysics vehicle(kInitialLat, kInitialLon, 0.0, 0.f, parameters);
        VehicleInput input;
        input.throttle = 1.f;
        input.gear = gear;
        vehicle.set_input(input);
        advance(vehicle, 6000);

        const auto state = vehicle.get_state();
        const float speed_limit = gear == VehicleGear::Reverse
            ? parameters.max_reverse_speed_mps
            : parameters.max_forward_speed_mps;
        require(std::abs(state.speed) <= speed_limit + 1e-4f,
                "chassis speed must respect the configured limiter");
        const float wheel_speed_limit = speed_limit / parameters.tire_radius_m;
        for (const auto& wheel : state.wheels) {
            require(std::abs(wheel.angular_speed) < wheel_speed_limit * 2.f,
                    "wheel angular speed must remain bounded at the chassis limiter");
        }
    };

    run_to_limiter(VehicleGear::Drive);
    run_to_limiter(VehicleGear::Reverse);
}

} // namespace

int main()
{
    try {
        test_initial_state_is_a_complete_four_wheel_snapshot();
        test_suspension_model_clamps_stroke_and_force();
        test_invalid_suspension_parameters_are_rejected();
        test_ground_query_publishes_sloped_hit_and_normal();
        test_invalid_ground_normal_is_rejected();
        test_no_ground_removes_all_tire_forces();
        test_partial_ground_only_loads_hit_wheels();
        test_vehicle_suspension_force_respects_configured_bound();
        test_idle_and_handbrake_are_stable();
        test_throttle_accelerates_and_input_is_clamped();
        test_service_brake_stops_without_reversing();
        test_reverse_requires_reverse_gear();
        test_bicycle_model_turns_right();
        test_positive_steering_turns_left_in_the_public_contract();
        test_left_steering_publishes_wheel_local_force_signs();
        test_reverse_keeps_command_sign_but_reverses_vehicle_yaw_response();
        test_right_turn_loads_outer_left_wheels_without_doubling_transfer();
        test_four_wheel_contact_and_force_limits();
        test_contact_points_use_the_published_post_integration_pose();
        test_braking_settles_lateral_and_yaw_motion();
        test_driveline_does_not_wind_up_at_speed_limiter();
        std::cout << "vehicle_physics_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vehicle_physics_tests: " << error.what() << '\n';
        return 1;
    }
}
