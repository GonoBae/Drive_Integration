#include "physics/powertrain_model.hpp"
#include "physics/vehicle_physics.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
constexpr float dt = 1.f / 60.f;
constexpr float rpm_to_rad_s = 2.f * std::numbers::pi_v<float> / 60.f;

void require(bool result, const char* message)
{
    if (!result) throw std::runtime_error(message);
}

bool close(double a, double b, double tolerance = 1e-5)
{
    return std::abs(a - b) <= tolerance;
}

simcore_host::PowertrainParameters modules()
{
    simcore_host::PowertrainParameters p;
    p.engine.torque_curve = {{800.f, 120.f}, {2000.f, 200.f}, {4000.f, 220.f}, {6500.f, 150.f}};
    p.transmission.forward_ratios = {3.6f, 2.1f, 1.4f, 1.f, 0.75f};
    p.transmission.upshift_rpm = 4800.f;
    p.transmission.downshift_rpm = 1600.f;
    p.transmission.shift_duration_s = 0.2f;
    p.fuel_tank.capacity_l = 60.f;
    p.fuel_tank.initial_fuel_l = 30.f;
    return p;
}

void test_torque_curve_and_validation()
{
    auto p = modules();
    require(simcore_host::valid_powertrain_parameters(p), "reference powertrain rejected");
    require(simcore_host::interpolate_engine_torque(p.engine, 800.f) == 120.f
        && simcore_host::interpolate_engine_torque(p.engine, 1400.f) == 160.f
        && simcore_host::interpolate_engine_torque(p.engine, 3000.f) == 210.f
        && simcore_host::interpolate_engine_torque(p.engine, -10.f) == 120.f
        && simcore_host::interpolate_engine_torque(p.engine, 7000.f) == 150.f,
        "torque curve interpolation or endpoint bounds are incorrect");
    p.engine.torque_curve[1].rpm = p.engine.torque_curve[0].rpm;
    require(!simcore_host::valid_powertrain_parameters(p), "duplicate torque sample accepted");
    p = modules(); p.engine.rotational_inertia_kg_m2 = 0.f;
    require(!simcore_host::valid_powertrain_parameters(p), "zero engine inertia accepted");
    p = modules(); p.transmission.forward_ratios = {2.f, 3.f};
    require(!simcore_host::valid_powertrain_parameters(p), "ascending forward ratios accepted");
    p = modules(); p.transmission.downshift_rpm = p.engine.idle_rpm;
    require(!simcore_host::valid_powertrain_parameters(p), "idle-equal downshift accepted");
    p = modules(); p.transmission.upshift_rpm = p.engine.max_rpm;
    require(!simcore_host::valid_powertrain_parameters(p), "redline-equal upshift accepted");
    p = modules(); p.transmission.shift_duration_s = 0.f;
    require(!simcore_host::valid_powertrain_parameters(p), "zero shift time accepted");
    p = modules(); p.transmission.shift_duration_s = 0.00001f;
    require(simcore_host::valid_powertrain_parameters(p), "positive representable shift time rejected");
    p = modules(); p.fuel_tank.initial_fuel_l = 61.f;
    require(!simcore_host::valid_powertrain_parameters(p), "overfilled fuel tank accepted");
    p = modules(); p.fuel_tank.fuel_density_kg_l = std::numeric_limits<float>::quiet_NaN();
    require(!simcore_host::valid_powertrain_parameters(p), "nonfinite fuel density accepted");
}

void test_engine_response_inertia_and_neutral()
{
    auto light = modules();
    auto heavy = light;
    light.engine.rotational_inertia_kg_m2 = 0.1f;
    heavy.engine.rotational_inertia_kg_m2 = 1.5f;
    auto a = simcore_host::initial_powertrain_state(light);
    auto b = simcore_host::initial_powertrain_state(heavy);
    simcore_host::PowertrainInput input;
    input.direction = 0;
    input.throttle = 1.f;
    double combustion_work_j = 0.0;
    const double initial_rotor_energy = 0.5 * heavy.engine.rotational_inertia_kg_m2
        * std::pow(b.engine_rpm * rpm_to_rad_s, 2);
    for (int tick = 0; tick < 60; ++tick) {
        simcore_host::advance_powertrain(light, a, input, dt);
        simcore_host::advance_powertrain(heavy, b, input, dt);
        require(a.axle_drive_torque_nm[0] == 0.f && a.axle_drive_torque_nm[1] == 0.f,
            "neutral delivered drive torque");
        require(a.engine_rpm <= light.engine.max_rpm && b.engine_rpm <= heavy.engine.max_rpm,
            "engine exceeded configured redline");
        combustion_work_j += b.crank_power_kw * 1000.0 * dt;
    }
    require(a.engine_rpm > b.engine_rpm + 500.f && b.engine_rpm > heavy.engine.idle_rpm,
        "engine rotational inertia did not change neutral rev response");
    const double final_rotor_energy = 0.5 * heavy.engine.rotational_inertia_kg_m2
        * std::pow(b.engine_rpm * rpm_to_rad_s, 2);
    require(final_rotor_energy - initial_rotor_energy <= combustion_work_j * 1.00001,
        "engine rotor gained more energy than combustion supplied");

    auto fast = modules();
    auto slow = fast;
    fast.engine.response_time_s = 0.05f;
    slow.engine.response_time_s = 1.f;
    a = simcore_host::initial_powertrain_state(fast);
    b = simcore_host::initial_powertrain_state(slow);
    simcore_host::advance_powertrain(fast, a, input, dt);
    simcore_host::advance_powertrain(slow, b, input, dt);
    require(a.filtered_throttle > b.filtered_throttle * 5.f
        && a.filtered_throttle < 1.f && a.combustion_torque_nm < 120.f,
        "engine response time did not filter a throttle step");
}

void test_automatic_shift_hysteresis_and_torque_cut()
{
    const auto p = modules();
    auto state = simcore_host::initial_powertrain_state(p);
    state.engine_rpm = 5000.f;
    simcore_host::PowertrainInput input;
    input.throttle = 0.8f;
    input.vehicle_speed_mps = 14.f;
    input.rear_wheel_angular_speed_rad_s = 5000.f * rpm_to_rad_s
        / (p.transmission.forward_ratios[0] * p.drivetrain.final_drive_ratio);
    simcore_host::advance_powertrain(p, state, input, dt);
    require(state.shifting && state.forward_gear == 1 && state.pending_forward_gear == 2,
        "upper shift threshold did not queue second gear");
    int shift_ticks = 1;
    while (state.shifting && shift_ticks < 120) {
        require(state.axle_drive_torque_nm[1] == 0.f && state.transmission_input_torque_nm == 0.f,
            "drive torque leaked through a gear shift");
        simcore_host::advance_powertrain(p, state, input, dt);
        ++shift_ticks;
    }
    require(state.forward_gear == 2 && !state.shifting
        && shift_ticks * dt >= p.transmission.shift_duration_s
        && shift_ticks * dt <= p.transmission.shift_duration_s + 3.f * dt,
        "shift completion did not respect finite duration");
    for (int tick = 0; tick < 90; ++tick) {
        simcore_host::advance_powertrain(p, state, input, dt);
        require(state.forward_gear == 2 && !state.shifting, "gearbox hunted at a steady shaft speed");
    }
    input.rear_wheel_angular_speed_rad_s = 1000.f * rpm_to_rad_s
        / (p.transmission.forward_ratios[1] * p.drivetrain.final_drive_ratio);
    simcore_host::advance_powertrain(p, state, input, dt);
    require(state.shifting && state.pending_forward_gear == 1, "lower shift threshold ignored");
    for (int tick = 0; tick < 60; ++tick) simcore_host::advance_powertrain(p, state, input, dt);
    require(state.forward_gear == 1 && !state.shifting, "downshift did not complete");
}

void test_reverse_brakes_and_efficiency_limits()
{
    auto p = modules();
    p.engine.torque_curve = {{800.f, 200.f}, {6500.f, 200.f}};
    p.transmission.max_input_torque_nm = 50.f;
    p.transmission.efficiency = 0.8f;
    p.drivetrain.efficiency = 0.9f;
    p.drivetrain.front_torque_fraction = 0.4f;
    auto state = simcore_host::initial_powertrain_state(p);
    simcore_host::PowertrainInput input;
    input.throttle = 1.f;
    for (int tick = 0; tick < 180; ++tick) simcore_host::advance_powertrain(p, state, input, dt);
    const float expected = 50.f * p.transmission.forward_ratios[0]
        * p.drivetrain.final_drive_ratio * 0.8f * 0.9f;
    require(state.transmission_input_torque_nm == 50.f
        && close(state.axle_drive_torque_nm[0], expected * 0.4f, 1e-4)
        && close(state.axle_drive_torque_nm[1], expected * 0.6f, 1e-4),
        "transmission input limit, efficiency or torque split is incorrect");
    input.torque_inhibited = true;
    simcore_host::advance_powertrain(p, state, input, dt);
    require(state.axle_drive_torque_nm[0] == 0.f && state.axle_drive_torque_nm[1] == 0.f,
        "brake safety did not immediately cut torque");
    input.torque_inhibited = false;
    input.direction = -1;
    input.vehicle_speed_mps = 4.f;
    for (int tick = 0; tick < 90; ++tick) {
        simcore_host::advance_powertrain(p, state, input, dt);
        require(state.drive_inhibited && state.axle_drive_torque_nm[1] == 0.f,
            "reverse torque applied before the forward-moving vehicle slowed");
    }
    input.vehicle_speed_mps = 0.f;
    for (int tick = 0; tick < 180; ++tick) simcore_host::advance_powertrain(p, state, input, dt);
    const float reverse = -50.f * p.transmission.reverse_ratio
        * p.drivetrain.final_drive_ratio * 0.8f * 0.9f;
    require(close(state.axle_drive_torque_nm[0] + state.axle_drive_torque_nm[1], reverse, 1e-4),
        "reverse ratio/sign did not reach the differential");
}

void test_fuel_units_power_budget_and_exhaustion()
{
    auto p = modules();
    p.engine.idle_fuel_lph = 1.2f;
    auto state = simcore_host::initial_powertrain_state(p);
    simcore_host::PowertrainInput input;
    const double start_l = state.remaining_fuel_l;
    for (int tick = 0; tick < 600; ++tick) simcore_host::advance_powertrain(p, state, input, 0.1f);
    require(close(start_l - state.remaining_fuel_l, p.engine.idle_fuel_lph * (600.0 * 0.1f) / 3600.0, 1e-10),
        "idle litres/hour conversion is incorrect");

    state = simcore_host::initial_powertrain_state(p);
    input.throttle = 0.6f;
    double expected_litres = 0.0;
    for (int tick = 0; tick < 600; ++tick) {
        input.rear_wheel_angular_speed_rad_s = static_cast<float>(tick) * 0.08f;
        simcore_host::advance_powertrain(p, state, input, dt);
        expected_litres += (p.engine.idle_fuel_lph
            + p.engine.bsfc_g_per_kwh * static_cast<double>(state.crank_power_kw)
                / (1000.0 * p.fuel_tank.fuel_density_kg_l)) * dt / 3600.0;
        const double delivered_kw = state.axle_drive_torque_nm[1]
            * input.rear_wheel_angular_speed_rad_s / 1000.0;
        require(delivered_kw <= state.crank_power_kw * p.transmission.efficiency
            * p.drivetrain.efficiency + 0.0001,
            "shaft torque invented power while engine RPM was still catching up");
    }
    require(close(p.fuel_tank.initial_fuel_l - state.remaining_fuel_l, expected_litres, 1e-9),
        "BSFC generated-power/density conversion is incorrect");

    p.fuel_tank.initial_fuel_l = 0.000001f;
    state = simcore_host::initial_powertrain_state(p);
    input.rear_wheel_angular_speed_rad_s = 0.f;
    for (int tick = 0; tick < 4; ++tick) simcore_host::advance_powertrain(p, state, input, dt);
    require(state.remaining_fuel_l == 0.0 && state.fuel_flow_lph == 0.0
        && state.axle_drive_torque_nm[0] == 0.f && state.axle_drive_torque_nm[1] == 0.f
        && std::isfinite(state.engine_rpm), "empty tank continued producing torque or became nonfinite");
    const float stopped_fuel_rpm = state.engine_rpm;
    for (int tick = 0; tick < 60; ++tick) simcore_host::advance_powertrain(p, state, input, dt);
    require(state.engine_rpm < stopped_fuel_rpm, "unfueled engine did not spin down");
    state = simcore_host::initial_powertrain_state(p);
    require(state.remaining_fuel_l == p.fuel_tank.initial_fuel_l && state.forward_gear == 1,
        "powertrain reset did not restore fuel and first gear");
}

void test_vehicle_modules_replace_legacy_caps_and_reset()
{
    VehicleParameters p;
    p.powertrain = modules();
    p.max_drive_force_n = 1.f;
    p.max_reverse_force_n = 1.f;
    p.max_drive_power_w = 1.f;
    p.fuel_rate_percent_s = 99.f;
    p.idle_rpm = 100.f;
    p.max_rpm = 200.f;
    VehiclePhysics vehicle(0, 0, 0, 0, p);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    VehicleState state;
    std::size_t maximum_gear = 1;
    for (int tick = 0; tick < 1800; ++tick) {
        state = vehicle.update(1.0 / 60.0);
        maximum_gear = std::max(maximum_gear, vehicle.get_powertrain_diagnostics()->forward_gear);
        require(std::isfinite(state.speed) && std::isfinite(state.rpm), "modular integration became nonfinite");
    }
    require(state.speed > 10.f && maximum_gear >= 2 && state.rpm > 200.f && state.fuel > 49.f,
        "modular vehicle still used hidden legacy force/power/RPM/fuel fields");
    input.brake = 1.f;
    vehicle.set_input(input);
    state = vehicle.update(1.0 / 60.0);
    require(vehicle.get_powertrain_diagnostics()->axle_drive_torque_nm[1] == 0.f,
        "vehicle service brake did not suppress modular torque");
    vehicle.reset();
    state = vehicle.get_state();
    const auto reset = *vehicle.get_powertrain_diagnostics();
    require(state.fuel == 50.f && state.rpm == p.powertrain->engine.idle_rpm
        && reset.forward_gear == 1 && reset.filtered_throttle == 0.f
        && reset.remaining_fuel_l == p.powertrain->fuel_tank.initial_fuel_l,
        "vehicle reset did not reset modular telemetry/state");
}

void test_mixed_radius_axles_preserve_torque_not_force_split()
{
    VehicleParameters p;
    p.powertrain = modules();
    p.powertrain->engine.torque_curve = {{800.f, 100.f}, {6500.f, 100.f}};
    p.powertrain->drivetrain.front_torque_fraction = 0.4f;
    p.front_axle_contact.tire = resolved_tire_parameters(p, 0);
    p.rear_axle_contact.tire = resolved_tire_parameters(p, 2);
    p.front_axle_contact.tire->radius_m = 0.25f;
    p.rear_axle_contact.tire->radius_m = 0.4f;
    p.front_axle_contact.suspension = p.suspension;
    p.rear_axle_contact.suspension = p.suspension;
    p.front_axle_contact.suspension->rest_length_m += 0.07f;
    p.rear_axle_contact.suspension->rest_length_m -= 0.08f;
    VehiclePhysics vehicle(0, 0, 0, 0, p);
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    VehicleState state;
    for (int tick = 0; tick < 240; ++tick) state = vehicle.update(1.0 / 60.0);
    const auto diagnostics = *vehicle.get_powertrain_diagnostics();
    require(close(diagnostics.axle_drive_torque_nm[0] / diagnostics.axle_drive_torque_nm[1], 2.0 / 3.0, 1e-6),
        "differential torque split changed with tire radius");
    const double front_road_torque = (state.wheels[0].longitudinal_force + state.wheels[1].longitudinal_force) * 0.25;
    const double rear_road_torque = (state.wheels[2].longitudinal_force + state.wheels[3].longitudinal_force) * 0.4;
    require(front_road_torque > 10.0 && rear_road_torque > 10.0
        && close(front_road_torque / rear_road_torque, 2.0 / 3.0, 0.04),
        "wheel force integration split force instead of shaft torque for mixed tire sizes");
}

class BoundedGround final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(const simcore_host::GroundQueryRequest& request) const override
    {
        if (request.origin_enu.north_m > 6.0 || request.origin_enu.up_m < 0.0
            || request.origin_enu.up_m > request.max_distance_m) return std::nullopt;
        return simcore_host::GroundHit{{request.origin_enu.east_m, request.origin_enu.north_m, 0.0},
            {0.0, 0.0, 1.0}, request.origin_enu.up_m};
    }
};

void test_rejected_ground_step_restores_powertrain_transaction()
{
    VehicleParameters p;
    p.powertrain = modules();
    VehiclePhysics vehicle(0, 0, 0, 0, p, std::make_shared<BoundedGround>());
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    bool stopped = false;
    for (int tick = 0; tick < 1200; ++tick) {
        const auto before = *vehicle.get_powertrain_diagnostics();
        const auto telemetry_before = vehicle.get_state();
        const auto state = vehicle.update(1.0 / 60.0);
        if (!vehicle.ground_coverage_limited()) continue;
        const auto after = *vehicle.get_powertrain_diagnostics();
        require(after.remaining_fuel_l == before.remaining_fuel_l
            && after.engine_rpm == before.engine_rpm && after.filtered_throttle == before.filtered_throttle
            && after.forward_gear == before.forward_gear && after.pending_forward_gear == before.pending_forward_gear
            && after.shift_remaining_s == before.shift_remaining_s && after.selected_direction == before.selected_direction
            && after.axle_drive_torque_nm[1] == 0.f && after.fuel_flow_lph == 0.0
            && state.rpm == telemetry_before.rpm && state.fuel == telemetry_before.fuel && state.speed == 0.f,
            "rejected terrain step committed fuel/shift/RPM evolution");
        stopped = true;
        break;
    }
    require(stopped, "transaction test did not reach terrain boundary");
}

void test_removing_modules_restores_legacy_path_exactly()
{
    VehicleParameters legacy;
    VehicleParameters modular = legacy;
    modular.powertrain = modules();
    VehiclePhysics restored(0, 0, 0, 0, modular);
    VehiclePhysics reference(0, 0, 0, 0, legacy);
    restored.replace_parameters(legacy);
    require(!restored.get_powertrain_diagnostics(), "module state survived legacy parameter replacement");
    for (int tick = 0; tick < 600; ++tick) {
        VehicleInput input;
        input.throttle = tick < 300 ? 0.4f : 0.f;
        input.steering = tick > 180 && tick < 350 ? 0.08f : 0.f;
        input.brake = tick > 420 ? 0.5f : 0.f;
        restored.set_input(input);
        reference.set_input(input);
        const auto a = restored.update(1.0 / 60.0);
        const auto b = reference.update(1.0 / 60.0);
        require(a.speed == b.speed && a.rpm == b.rpm && a.fuel == b.fuel
            && a.east == b.east && a.north == b.north && a.pitch == b.pitch && a.roll == b.roll,
            "legacy path changed after clearing optional modules");
        for (std::size_t i = 0; i < 4; ++i) {
            require(a.wheels[i].angular_speed == b.wheels[i].angular_speed
                && a.wheels[i].normal_load == b.wheels[i].normal_load
                && a.wheels[i].longitudinal_force == b.wheels[i].longitudinal_force
                && a.wheels[i].lateral_force == b.wheels[i].lateral_force,
                "legacy tire trajectory changed after clearing optional modules");
        }
    }
}
} // namespace

int main()
{
    try {
        test_torque_curve_and_validation();
        test_engine_response_inertia_and_neutral();
        test_automatic_shift_hysteresis_and_torque_cut();
        test_reverse_brakes_and_efficiency_limits();
        test_fuel_units_power_budget_and_exhaustion();
        test_vehicle_modules_replace_legacy_caps_and_reset();
        test_mixed_radius_axles_preserve_torque_not_force_split();
        test_rejected_ground_step_restores_powertrain_transaction();
        test_removing_modules_restores_legacy_path_exactly();
        std::cout << "powertrain_physics_tests: 9 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "powertrain_physics_tests: " << error.what() << '\n';
        return 1;
    }
}
