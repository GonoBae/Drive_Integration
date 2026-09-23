#include "traffic/npc_vehicle_modules.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double dt = 1.0 / 60.0;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

VehicleParameters powered(float torque = 180.f)
{
    VehicleParameters p;
    p.powertrain.emplace();
    p.powertrain->engine.torque_curve = {{800.f, torque}, {6500.f, torque}};
    p.powertrain->transmission.forward_ratios = {3.6f, 2.1f, 1.4f, 1.f};
    p.powertrain->transmission.upshift_rpm = 4800.f;
    p.powertrain->transmission.downshift_rpm = 1600.f;
    p.powertrain->transmission.shift_duration_s = 0.2f;
    p.powertrain->fuel_tank.capacity_l = 60.f;
    p.powertrain->fuel_tank.initial_fuel_l = 30.f;
    return p;
}

void test_inheritance_and_suspension_only_preserve_legacy_motion()
{
    VehicleParameters p;
    require(!simcore_host::NpcVehicleModules::selected(p), "default NPC enabled module dynamics");
    p.front_axle_contact.suspension = p.suspension;
    p.front_axle_contact.suspension->damper_rate_n_s_per_m *= 2.f;
    simcore_host::NpcVehicleModules modules(p, 1500.0);
    require(simcore_host::NpcVehicleModules::selected(p), "suspension metadata selection was lost");
    double reference = 0.0;
    double actual = 0.0;
    for (int tick = 0; tick < 600; ++tick) {
        const double desired = tick < 300 ? 7.0 : 0.0;
        const double rate = desired >= reference ? 1.5 : 3.0;
        reference += std::clamp(desired - reference, -rate * dt, rate * dt);
        actual = modules.speed_after_step(actual, desired, 1.5, 3.0, dt);
        require(reference == actual, "suspension-only NPC unexpectedly gained synthetic tire/ride dynamics");
    }
    require(!modules.powertrain(), "suspension-only NPC created an engine");
}

void test_shared_engine_changes_acceleration_and_consumes_real_fuel()
{
    simcore_host::NpcVehicleModules weak(powered(70.f), 1500.0);
    simcore_host::NpcVehicleModules strong(powered(240.f), 1500.0);
    double a = 0.0, b = 0.0;
    for (int tick = 0; tick < 240; ++tick) {
        a = weak.speed_after_step(a, 20.0, 4.0, 3.0, dt);
        b = strong.speed_after_step(b, 20.0, 4.0, 3.0, dt);
    }
    require(b > a * 1.5 && a > 0.5, "NPC acceleration did not come from selected engine torque");
    require(weak.powertrain()->remaining_fuel_l < 30.0
        && strong.powertrain()->remaining_fuel_l < 30.0
        && strong.powertrain()->engine_rpm > 800.f,
        "NPC fuel/RPM did not advance with selected modules");
}

void test_depleted_npc_cannot_propulse_forward_or_backward()
{
    auto p = powered();
    p.powertrain->fuel_tank.initial_fuel_l = 0.f;
    simcore_host::NpcVehicleModules modules(p, 1500.0);
    double speed = 0.0;
    for (const int direction : {1, -1}) {
        modules.set_direction(direction);
        for (int tick = 0; tick < 240; ++tick)
            speed = modules.speed_after_step(speed, 10.0, 4.0, 3.0, dt);
        require(speed == 0.0 && modules.powertrain()->remaining_fuel_l == 0.0,
            "empty NPC tank still produced route propulsion");
    }
}

void test_tires_limit_acceleration_braking_and_corner_speed()
{
    auto dry = powered(300.f);
    dry.front_axle_contact.tire = resolved_tire_parameters(dry, 0);
    dry.rear_axle_contact.tire = resolved_tire_parameters(dry, 2);
    auto slippery = dry;
    slippery.front_axle_contact.tire->friction_coefficient = 0.15f;
    slippery.rear_axle_contact.tire->friction_coefficient = 0.15f;
    simcore_host::NpcVehicleModules a(dry, 1500.0), b(slippery, 1500.0);
    double dry_speed = 0.0, slippery_speed = 0.0;
    for (int tick = 0; tick < 180; ++tick) {
        dry_speed = a.speed_after_step(dry_speed, 12.0, 4.0, 3.0, dt);
        slippery_speed = b.speed_after_step(slippery_speed, 12.0, 4.0, 3.0, dt);
    }
    require(dry_speed > slippery_speed * 2.0, "NPC tire grip did not limit longitudinal force");
    require(b.braking_limit_mps2(3.0) < a.braking_limit_mps2(3.0), "tire grip did not affect stopping envelope");
    a.set_road_context(simcore_host::GroundSurfaceMaterialId::Asphalt, 1.0, 0.0, 0.1);
    b.set_road_context(simcore_host::GroundSurfaceMaterialId::Asphalt, 1.0, 0.0, 0.1);
    require(b.corner_speed_limit_mps() < a.corner_speed_limit_mps() * 0.5,
        "tire grip did not limit route corner speed");
    const double dry_brake = a.braking_limit_mps2(20.0);
    a.set_road_context(simcore_host::GroundSurfaceMaterialId::LowFriction, 0.2, 0.0, 0.1);
    require(a.braking_limit_mps2(20.0) < dry_brake * 0.25, "authored ground friction was ignored");
}

void test_tire_radius_changes_npc_wheel_force()
{
    auto small = powered(90.f);
    small.front_axle_contact.tire = resolved_tire_parameters(small, 0);
    small.rear_axle_contact.tire = resolved_tire_parameters(small, 2);
    small.front_axle_contact.tire->radius_m = small.rear_axle_contact.tire->radius_m = 0.25f;
    auto large = small;
    large.front_axle_contact.tire->radius_m = large.rear_axle_contact.tire->radius_m = 0.45f;
    simcore_host::NpcVehicleModules a(small, 1500.0), b(large, 1500.0);
    double va = 0.0, vb = 0.0;
    for (int tick = 0; tick < 180; ++tick) {
        va = a.speed_after_step(va, 20.0, 5.0, 3.0, dt);
        vb = b.speed_after_step(vb, 20.0, 5.0, 3.0, dt);
    }
    require(va > vb * 1.3, "NPC shaft torque was not divided by the selected tire radius");
}

simcore_host::TrafficNetwork straight_network()
{
    simcore_host::TrafficNetwork network;
    simcore_host::TrafficLane lane;
    lane.id = 10;
    lane.width_m = 4.0;
    lane.speed_limit_mps = 10.0;
    lane.terminal = true;
    for (double north = 0.0; north <= 1000.0; north += 100.0)
        lane.points.push_back({0.0, north, 0.0});
    network.lanes.push_back(std::move(lane));
    return network;
}

void test_follower_uses_module_motion_and_full_tick_idle_fuel()
{
    auto p = powered();
    p.powertrain->engine.idle_fuel_lph = 1.2f;
    simcore_host::NpcVehicleModules modules(p, 1500.0);
    simcore_host::NpcLaneFollowerConfig settings;
    settings.max_speed_mps = 10.0;
    simcore_host::NpcLaneFollower follower(settings);
    simcore_host::FlatGroundQuery ground;
    follower.rebuild(straight_network(), ground, {10});
    for (int tick = 0; tick < 120; ++tick) {
        const auto& state = follower.step(dt, {}, true,
            settings.front_extent_m + settings.stop_margin_m, 55.6, 0.0, &modules);
        require(state.speed_mps == 0.0 && state.distance_travelled_m == 0.0,
            "module dynamics overrode a route obstacle stop");
    }
    const double used = p.powertrain->fuel_tank.initial_fuel_l - modules.powertrain()->remaining_fuel_l;
    require(std::abs(used - 1.2 * 120.0 * dt / 3600.0) < 1e-9,
        "stopped NPC skipped idle fuel in internally substepped time");
    for (int tick = 0; tick < 240; ++tick) follower.step(dt, {}, true, {}, 55.6, 0.0, &modules);
    require(follower.state().distance_travelled_m > 5.0 && follower.state().speed_mps > 2.0,
        "route follower did not use actual modular acceleration");

    p.powertrain->fuel_tank.initial_fuel_l = 0.f;
    modules = simcore_host::NpcVehicleModules(p, 1500.0);
    follower.reset();
    for (int tick = 0; tick < 180; ++tick) follower.step(dt, {}, true, {}, 55.6, 0.0, &modules);
    require(follower.state().distance_travelled_m == 0.0, "follower bypassed a depleted module model");
}

void test_module_snapshot_rollback_and_reverse_engagement()
{
    simcore_host::NpcVehicleModules modules(powered(), 1500.0);
    double speed = 0.0;
    for (int tick = 0; tick < 90; ++tick) speed = modules.speed_after_step(speed, 8.0, 2.0, 3.0, dt);
    const auto before = modules;
    const auto before_state = *modules.powertrain();
    const double attempted = modules.speed_after_step(speed, 8.0, 2.0, 3.0, dt);
    require(modules.powertrain()->remaining_fuel_l < before_state.remaining_fuel_l,
        "rollback fixture did not attempt fuel consumption");
    modules = before;
    require(modules.powertrain()->remaining_fuel_l == before_state.remaining_fuel_l
        && modules.powertrain()->engine_rpm == before_state.engine_rpm
        && modules.powertrain()->shift_remaining_s == before_state.shift_remaining_s,
        "NPC transaction copy failed to restore powertrain state");
    require(modules.speed_after_step(speed, 8.0, 2.0, 3.0, dt) == attempted,
        "repeated NPC module step was not deterministic after rollback");
    modules = simcore_host::NpcVehicleModules(powered(), 1500.0);
    modules.set_direction(-1);
    require(modules.speed_after_step(0.0, 1.4, 1.0, 1.0, dt) == 0.0,
        "NPC reverse engagement skipped shift torque cut");
    speed = 0.0;
    for (int tick = 0; tick < 120; ++tick) speed = modules.speed_after_step(speed, 1.4, 1.0, 1.0, dt);
    require(speed > 0.3 && modules.powertrain()->selected_direction == -1,
        "NPC could not finish reverse engagement and retreat");
}
} // namespace

int main()
{
    try {
        test_inheritance_and_suspension_only_preserve_legacy_motion();
        test_shared_engine_changes_acceleration_and_consumes_real_fuel();
        test_depleted_npc_cannot_propulse_forward_or_backward();
        test_tires_limit_acceleration_braking_and_corner_speed();
        test_tire_radius_changes_npc_wheel_force();
        test_follower_uses_module_motion_and_full_tick_idle_fuel();
        test_module_snapshot_rollback_and_reverse_engagement();
        std::cout << "npc_vehicle_modules_tests: 7 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_vehicle_modules_tests: " << error.what() << '\n';
        return 1;
    }
}
