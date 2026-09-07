#include "physics/motorcycle_balance.hpp"
#include "physics/vehicle_physics.hpp"
#include "player_vehicle_profile.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void test_turn(float steering)
{
    const auto parameters = simcore_host::make_player_vehicle_parameters({},
        simcore_host::RuntimeVehicleClass::Motorcycle);
    VehiclePhysics bike(40.7069, -74.0095, 0.0, 0.f, parameters);
    VehicleInput input;
    input.throttle = .35f;
    bike.set_input(input);
    for (int step = 0; step < 240; ++step) bike.update(1.0/60.0);
    require(bike.get_state().speed > 3.f, "motorcycle must accelerate on two axle contacts");
    input.steering = steering;
    bike.set_input(input);
    double roll_sum = 0.0;
    double yaw_sum = 0.0;
    for (int step = 0; step < 240; ++step) {
        const auto state = bike.update(1.0/60.0);
        require(std::isfinite(state.roll) && std::abs(state.roll) < 55.f,
            "ordinary motorcycle corner must not topple outwards");
        require(state.position_enu.z > .2, "bike chassis must remain above ground");
        for (int axle : {0, 2}) {
            const auto& first = state.wheels[axle].contact_point_enu;
            const auto& second = state.wheels[axle+1].contact_point_enu;
            require(std::hypot(first.x-second.x, first.y-second.y) < 1e-5,
                "paired protocol slots must share one physical centerline contact");
        }
        if (step > 120) { roll_sum += state.roll; yaw_sum += state.yaw_rate; }
    }
    std::cout << "steering=" << steering << " roll=" << roll_sum/119
              << " yaw=" << yaw_sum/119 << " speed=" << bike.get_state().speed << '\n';
    require(yaw_sum * steering > .5, "motorcycle must turn in the requested direction");
    require(roll_sum * steering < -20.0, "motorcycle must bank into the turn");
    bike.reset();
    require(std::abs(bike.get_state().roll) < .1, "reset must restore upright balance state");
}
}

int main()
{
    try {
        require(simcore_host::motorcycle_equilibrium_roll(10.f, .5f) < 0.f,
            "left turn bank sign");
        require(simcore_host::motorcycle_equilibrium_roll(10.f, -.5f) > 0.f,
            "right turn bank sign");
        require(simcore_host::motorcycle_equilibrium_roll(0.f, 1.f) == 0.f,
            "steering while stopped must not lean bike");
        require(simcore_host::motorcycle_equilibrium_roll(-10.f, -.5f) < 0.f,
            "reverse bank follows acceleration, not yaw alone");
        require(simcore_host::motorcycle_balance_torque(0.f, .3f, 1.f, 10.f,
            70.f, 240.f, .42f, false, true) == 0.f, "airborne bike has no rider balance torque");
        require(simcore_host::motorcycle_balance_torque(0.f, .3f, 1.f, 10.f,
            70.f, 240.f, .42f, true, false) == 0.f, "ejected rider cannot balance motorcycle");
        require(!VehicleParameters{}.single_track, "sedan must retain its four contact model");
        test_turn(.12f);
        test_turn(-.12f);
        std::cout << "motorcycle_physics_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "motorcycle_physics_tests: " << error.what() << '\n';
        return 1;
    }
}
