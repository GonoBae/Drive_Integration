#include "physics/vehicle_physics.hpp"

#include <cmath>
#include <iostream>
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

    const auto state = vehicle.get_state();
    require(state.steering_angle > 0.f, "right steering must have a positive wheel angle");
    require(state.yaw_rate > 0.f, "right steering must produce a positive yaw rate");
    require(state.heading > 0.f && state.heading < 180.f,
            "right steering must rotate clockwise from north");
    require(state.east > 0.0, "right turn from north must move east");
}

} // namespace

int main()
{
    try {
        test_idle_and_handbrake_are_stable();
        test_throttle_accelerates_and_input_is_clamped();
        test_service_brake_stops_without_reversing();
        test_reverse_requires_reverse_gear();
        test_bicycle_model_turns_right();
        std::cout << "vehicle_physics_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vehicle_physics_tests: " << error.what() << '\n';
        return 1;
    }
}
