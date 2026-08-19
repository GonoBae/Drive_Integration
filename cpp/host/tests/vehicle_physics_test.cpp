#include "physics/vehicle_physics.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr double kInitialLat = 40.7069;
constexpr double kInitialLon = -74.0095;

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
        total_load += wheel.normal_load;
    }
    require(std::abs(total_load - parameters.mass_kg * 9.80665f) < 1.f,
            "initial wheel loads must sum to vehicle weight");
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

    input.steering = 0.15f;
    vehicle.set_input(input);
    advance(vehicle, 60);

    const auto before = vehicle.get_state();
    const auto state = vehicle.update(kDt);
    require(state.steering_angle > 0.f, "right steering must have a positive wheel angle");
    require(state.yaw_rate > 0.f, "right steering must produce a positive yaw rate");
    require(state.angular_velocity_body.z < 0.0,
            "canonical body angular Z must be negative for a right turn");
    require(state.heading > 0.f && state.heading < 180.f,
            "right steering must rotate clockwise from north");
    require(state.east > 0.0, "right turn from north must move east");

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

void test_right_turn_loads_outer_left_wheels_without_doubling_transfer()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.6f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = 0.12f;
    vehicle.set_input(input);
    advance(vehicle, 45);

    const auto state = vehicle.get_state();
    const float left_load = state.wheels[0].normal_load + state.wheels[2].normal_load;
    const float right_load = state.wheels[1].normal_load + state.wheels[3].normal_load;
    const float total_load = left_load + right_load;
    require(state.yaw_rate > 0.f, "test setup must produce a right turn");
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
            require(wheel.steering_angle > 0.f, "front wheels must steer right");
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
        test_idle_and_handbrake_are_stable();
        test_throttle_accelerates_and_input_is_clamped();
        test_service_brake_stops_without_reversing();
        test_reverse_requires_reverse_gear();
        test_bicycle_model_turns_right();
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
