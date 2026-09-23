#include "physics/vehicle_physics.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
constexpr double dt = 1.0 / 60.0;

class MovingFlatGround final : public simcore_host::GroundQuery {
public:
    double height = 0.0;
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m) return std::nullopt;
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {0.0, 0.0, 1.0}, distance};
    }
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool close(double a, double b, double tolerance = 1e-5)
{
    return std::abs(a - b) <= tolerance;
}

VehicleParameters explicit_legacy_modules()
{
    VehicleParameters parameters;
    parameters.front_axle_contact.tire = resolved_tire_parameters(parameters, 0);
    parameters.rear_axle_contact.tire = resolved_tire_parameters(parameters, 2);
    parameters.front_axle_contact.suspension = parameters.suspension;
    parameters.rear_axle_contact.suspension = parameters.suspension;
    return parameters;
}

void require_same_state(const VehicleState& a, const VehicleState& b)
{
    require(a.speed == b.speed && a.accel == b.accel && a.rpm == b.rpm
        && a.fuel == b.fuel && a.pitch == b.pitch && a.roll == b.roll
        && a.heading == b.heading && a.yaw_rate == b.yaw_rate
        && a.east == b.east && a.north == b.north
        && a.position_enu.z == b.position_enu.z
        && a.linear_velocity_body.x == b.linear_velocity_body.x
        && a.linear_velocity_body.y == b.linear_velocity_body.y
        && a.linear_velocity_body.z == b.linear_velocity_body.z
        && a.angular_velocity_body.x == b.angular_velocity_body.x
        && a.angular_velocity_body.y == b.angular_velocity_body.y
        && a.angular_velocity_body.z == b.angular_velocity_body.z,
        "explicit legacy modules changed the deterministic body trajectory");
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& x = a.wheels[i];
        const auto& y = b.wheels[i];
        require(x.in_contact == y.in_contact && x.normal_load == y.normal_load
            && x.angular_speed == y.angular_speed && x.steering_angle == y.steering_angle
            && x.longitudinal_slip == y.longitudinal_slip && x.slip_angle == y.slip_angle
            && x.longitudinal_force == y.longitudinal_force && x.lateral_force == y.lateral_force
            && x.contact_point_enu.x == y.contact_point_enu.x
            && x.contact_point_enu.y == y.contact_point_enu.y
            && x.contact_point_enu.z == y.contact_point_enu.z,
            "explicit legacy modules changed a wheel sample");
    }
}

void test_legacy_fallback_and_module_validation()
{
    VehicleParameters p;
    require(!p.front_axle_contact.tire && !p.rear_axle_contact.suspension,
        "legacy settings must remain the default");
    require(resolved_tire_parameters(p, 0).cornering_stiffness_n_rad
            == p.front_tire_corner_stiffness_n_rad
        && resolved_tire_parameters(p, 3).cornering_stiffness_n_rad
            == p.rear_tire_corner_stiffness_n_rad,
        "legacy axle stiffness was lost");
    require(&resolved_suspension_parameters(p, 0) == &p.suspension,
        "missing suspension should reference legacy suspension");
    p = explicit_legacy_modules();
    require(valid_vehicle_parameters(p), "valid modules rejected");
    p.front_axle_contact.tire->radius_m = 0.f;
    require(!valid_vehicle_parameters(p), "zero module radius accepted");
    p = explicit_legacy_modules();
    p.rear_axle_contact.tire->friction_coefficient = std::numeric_limits<float>::quiet_NaN();
    require(!valid_vehicle_parameters(p), "nonfinite module friction accepted");
    p = explicit_legacy_modules();
    p.front_axle_contact.tire->longitudinal_stiffness_n = -1.f;
    require(!valid_vehicle_parameters(p), "negative module stiffness accepted");
    p = explicit_legacy_modules();
    p.rear_axle_contact.tire->cornering_stiffness_n_rad = 0.f;
    require(!valid_vehicle_parameters(p), "zero corner stiffness accepted");
    p = explicit_legacy_modules();
    p.front_axle_contact.tire->rolling_resistance_coefficient = -0.1f;
    require(!valid_vehicle_parameters(p), "negative rolling resistance accepted");
    p = explicit_legacy_modules();
    p.rear_axle_contact.suspension->max_compression_m = 2.f;
    require(!valid_vehicle_parameters(p), "impossible suspension travel accepted");
}

void test_explicit_legacy_modules_are_bitwise_equivalent()
{
    for (const bool single_track : {false, true}) {
        VehicleParameters legacy;
        auto modules = explicit_legacy_modules();
        legacy.single_track = modules.single_track = single_track;
        VehiclePhysics a(37.0, 127.0, 0.0, 0.f, legacy);
        VehiclePhysics b(37.0, 127.0, 0.0, 0.f, modules);
        for (int tick = 0; tick < 900; ++tick) {
            VehicleInput input;
            input.throttle = tick < 360 ? 0.55f : 0.f;
            input.steering = tick >= 180 && tick < 420 ? 0.08f : 0.f;
            input.brake = tick >= 500 && tick < 620 ? 0.5f : 0.f;
            input.handbrake = tick >= 630 && tick < 670;
            if (tick >= 700) {
                input.gear = VehicleGear::Reverse;
                input.throttle = 0.3f;
            }
            a.set_input(input);
            b.set_input(input);
            require_same_state(a.update(dt), b.update(dt));
        }
        a.reset();
        b.reset();
        require_same_state(a.get_state(), b.get_state());
    }
}

void test_suspension_modules_act_on_only_the_selected_axle()
{
    const auto baseline = explicit_legacy_modules();
    auto soft_front = baseline;
    soft_front.front_axle_contact.suspension->spring_rate_n_per_m *= 0.5f;
    auto firm_rear = baseline;
    firm_rear.rear_axle_contact.suspension->spring_rate_n_per_m *= 1.5f;
    VehiclePhysics a(0, 0, 0, 0, baseline);
    VehiclePhysics b(0, 0, 0, 0, soft_front);
    VehiclePhysics c(0, 0, 0, 0, firm_rear);
    const auto sa = a.update(dt);
    const auto sb = b.update(dt);
    const auto sc = c.update(dt);
    const auto da = a.get_wheel_contact_support_diagnostics();
    const auto db = b.get_wheel_contact_support_diagnostics();
    const auto dc = c.get_wheel_contact_support_diagnostics();
    for (std::size_t i = 0; i < 4; ++i) {
        const double force = da[i].suspension_normal_force_n;
        require(close(db[i].suspension_normal_force_n, force * (i < 2 ? 0.5 : 1.0), 0.002),
            "front spring replacement leaked to rear axle or was ignored");
        require(close(dc[i].suspension_normal_force_n, force * (i < 2 ? 1.0 : 1.5), 0.002),
            "rear spring replacement leaked to front axle or was ignored");
    }
    require(sb.position_enu.z < sa.position_enu.z && sc.position_enu.z > sa.position_enu.z,
        "soft and firm springs did not change sprung-body response");
    require(sb.pitch != sa.pitch && sc.pitch != sa.pitch,
        "axle spring replacement did not produce a pitch response");
}

void test_tire_cornering_stiffness_is_axle_specific()
{
    auto baseline = explicit_legacy_modules();
    auto soft_front = baseline;
    soft_front.front_axle_contact.tire->cornering_stiffness_n_rad *= 0.5f;
    VehiclePhysics a(0, 0, 0, 0, baseline);
    VehiclePhysics b(0, 0, 0, 0, soft_front);
    VehicleInput input;
    input.throttle = 0.3f;
    a.set_input(input);
    b.set_input(input);
    for (int i = 0; i < 240; ++i) require_same_state(a.update(dt), b.update(dt));
    input.steering = 0.001f;
    a.set_input(input);
    b.set_input(input);
    const auto sa = a.update(dt);
    const auto sb = b.update(dt);
    require(std::abs(sa.wheels[0].lateral_force) > 0.1f, "test did not generate lateral force");
    for (std::size_t i = 0; i < 4; ++i) {
        require(close(sb.wheels[i].lateral_force,
            sa.wheels[i].lateral_force * (i < 2 ? 0.5 : 1.0), 0.005),
            "front tire stiffness did not remain axle specific");
    }
}

void test_damper_modules_change_bump_settling()
{
    auto low = explicit_legacy_modules();
    auto high = low;
    low.front_axle_contact.suspension->damper_rate_n_s_per_m *= 0.3f;
    low.rear_axle_contact.suspension->damper_rate_n_s_per_m *= 0.3f;
    high.front_axle_contact.suspension->damper_rate_n_s_per_m *= 1.5f;
    high.rear_axle_contact.suspension->damper_rate_n_s_per_m *= 1.5f;
    auto ground = std::make_shared<MovingFlatGround>();
    VehiclePhysics a(0, 0, 0, 0, low, ground);
    VehiclePhysics b(0, 0, 0, 0, high, ground);
    for (int tick = 0; tick < 600; ++tick) { a.update(dt); b.update(dt); }
    const double rest_a = a.get_state().position_enu.z;
    const double rest_b = b.get_state().position_enu.z;
    double late_heave_energy_a = 0.0;
    double late_heave_energy_b = 0.0;
    for (int tick = 0; tick < 240; ++tick) {
        // A one-centimetre road pulse excites travel without hitting a stop.
        // Both vehicles receive exactly the same samples and fixed time step.
        ground->height = tick < 6 ? 0.01 : 0.0;
        const auto sa = a.update(dt);
        const auto sb = b.update(dt);
        if (tick >= 90) {
            late_heave_energy_a += std::pow(sa.position_enu.z - rest_a, 2);
            late_heave_energy_b += std::pow(sb.position_enu.z - rest_b, 2);
        }
    }
    require(late_heave_energy_a > 1e-10
        && late_heave_energy_b < late_heave_energy_a * 0.25,
        "firmer dampers did not dissipate the road-pulse oscillation faster");

    auto baseline = explicit_legacy_modules();
    auto front_damped = baseline;
    front_damped.front_axle_contact.suspension->damper_rate_n_s_per_m *= 2.f;
    ground->height = 0.0;
    VehiclePhysics c(0, 0, 0, 0, baseline, ground);
    VehiclePhysics d(0, 0, 0, 0, front_damped, ground);
    ground->height = 0.001;
    c.update(dt);
    d.update(dt);
    const auto dc = c.get_wheel_contact_support_diagnostics();
    const auto dd = d.get_wheel_contact_support_diagnostics();
    for (std::size_t i = 0; i < 4; ++i) {
        const double expected_delta = i < 2 ? 3500.0 * 0.001 / dt : 0.0;
        require(close(dd[i].suspension_normal_force_n - dc[i].suspension_normal_force_n,
            expected_delta, 0.01), "front damper affected rear axle or did not use compression velocity");
    }
}

void test_axle_force_cap_droop_and_compression_stops()
{
    auto capped = explicit_legacy_modules();
    capped.front_axle_contact.suspension->max_force_n = 1000.f;
    VehiclePhysics a(0, 0, 0, 0, capped);
    a.update(dt);
    const auto supports = a.get_wheel_contact_support_diagnostics();
    require(supports[0].suspension_normal_force_n == 1000.f
        && supports[1].suspension_normal_force_n == 1000.f
        && supports[2].suspension_normal_force_n > 3000.f,
        "front suspension force cap leaked to the rear or was ignored");

    auto droop = explicit_legacy_modules();
    droop.front_axle_contact.suspension->rest_length_m = 0.2f;
    droop.front_axle_contact.suspension->max_extension_m = 0.f;
    VehiclePhysics b(0, 0, 0, 0, droop);
    const auto start = b.get_state();
    require(!start.wheels[0].in_contact && !start.wheels[1].in_contact
        && start.wheels[2].in_contact && start.wheels[3].in_contact,
        "front full-droop contact limit was not independent");

    auto stops = explicit_legacy_modules();
    stops.front_axle_contact.tire->radius_m = 0.4f;
    stops.front_axle_contact.suspension->rest_length_m = 0.4f;
    stops.front_axle_contact.suspension->max_compression_m = 0.05f;
    stops.rear_axle_contact.tire->radius_m = 0.3f;
    stops.rear_axle_contact.suspension->rest_length_m = 0.3f;
    stops.rear_axle_contact.suspension->max_compression_m = 0.2f;
    VehiclePhysics c(0, 0, 0, 0, stops);
    for (int tick = 0; tick < 180; ++tick) {
        const auto state = tick == 0 ? c.get_state() : c.update(dt);
        const double pitch = state.pitch * std::numbers::pi / 180.0;
        const double roll = state.roll * std::numbers::pi / 180.0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& suspension = resolved_suspension_parameters(stops, i);
            const double x = i < 2 ? stops.wheelbase_m * (1.0 - stops.front_static_load_fraction)
                : -stops.wheelbase_m * stops.front_static_load_fraction;
            const double y = (i % 2 == 0 ? -0.5 : 0.5) * (i < 2 ? stops.front_track_m : stops.rear_track_m);
            const double mount_z = state.position_enu.z + x * std::sin(pitch)
                - y * std::sin(roll) * std::cos(pitch);
            const double stop_clearance = mount_z - resolved_tire_parameters(stops, i).radius_m
                - (suspension.rest_length_m - suspension.max_compression_m) * std::cos(pitch) * std::cos(roll);
            require(stop_clearance >= -2e-5,
                "axle-specific tire radius or compression stop penetrated flat ground");
        }
    }
}

void test_rear_tire_grip_limits_propulsion()
{
    auto grippy = explicit_legacy_modules();
    auto slippery = grippy;
    slippery.rear_axle_contact.tire->friction_coefficient = 0.18f;
    VehiclePhysics a(0, 0, 0, 0, grippy);
    VehiclePhysics b(0, 0, 0, 0, slippery);
    VehicleInput input;
    input.throttle = 1.f;
    a.set_input(input);
    b.set_input(input);
    VehicleState sa, sb;
    for (int tick = 0; tick < 180; ++tick) {
        sa = a.update(dt);
        sb = b.update(dt);
    }
    require(sa.speed > sb.speed * 1.5f, "rear compound did not limit RWD traction");
    require(resolved_tire_parameters(slippery, 0).friction_coefficient
        == resolved_tire_parameters(grippy, 0).friction_coefficient,
        "rear compound overwrote front grip");
}

void test_mixed_radii_drive_angular_speed_and_rpm()
{
    auto p = explicit_legacy_modules();
    p.front_axle_contact.tire->radius_m = 0.28f;
    p.rear_axle_contact.tire->radius_m = 0.36f;
    // Preserve nominal mount-to-ground clearance while changing wheel size.
    p.front_axle_contact.suspension->rest_length_m += 0.04f;
    p.rear_axle_contact.suspension->rest_length_m -= 0.04f;
    VehiclePhysics physics(0, 0, 0, 0, p);
    VehicleInput input;
    input.throttle = 0.4f;
    physics.set_input(input);
    VehicleState state;
    for (int tick = 0; tick < 420; ++tick) state = physics.update(dt);
    require(state.speed > 3.f, "mixed-radius vehicle did not drive");
    const double front_ratio = state.wheels[0].angular_speed * 0.28 / state.speed;
    const double rear_ratio = state.wheels[2].angular_speed * 0.36 / state.speed;
    require(std::abs(front_ratio - 1.0) < 0.06 && std::abs(rear_ratio - 1.0) < 0.06,
        "wheel angular velocity did not use its own tire radius");
    const float expected_rpm = std::clamp(std::abs(state.speed)
        / (2.f * std::numbers::pi_v<float> * 0.36f) * 60.f
        * p.drive_gear_ratio * p.final_drive_ratio, p.idle_rpm, p.max_rpm);
    require(state.rpm == expected_rpm, "RWD RPM did not use rear module radius");
    require(close(state.wheels[0].angular_speed, state.wheels[1].angular_speed, 0.001)
        && close(state.wheels[2].angular_speed, state.wheels[3].angular_speed, 0.001),
        "same-axle tires lost straight-line symmetry");
}

void test_rolling_resistance_module_affects_coastdown()
{
    auto low = explicit_legacy_modules();
    low.front_axle_contact.tire->rolling_resistance_coefficient = 0.f;
    low.rear_axle_contact.tire->rolling_resistance_coefficient = 0.f;
    auto high = low;
    high.rear_axle_contact.tire->rolling_resistance_coefficient = 0.12f;
    VehiclePhysics a(0, 0, 0, 0, low);
    VehiclePhysics b(0, 0, 0, 0, high);
    VehicleInput input;
    input.throttle = 0.5f;
    a.set_input(input);
    b.set_input(input);
    for (int tick = 0; tick < 180; ++tick) { a.update(dt); b.update(dt); }
    const float start_a = a.get_state().speed;
    const float start_b = b.get_state().speed;
    input.throttle = 0.f;
    input.gear = VehicleGear::Neutral;
    a.set_input(input);
    b.set_input(input);
    for (int tick = 0; tick < 120; ++tick) { a.update(dt); b.update(dt); }
    require(start_b - b.get_state().speed > start_a - a.get_state().speed + 0.4f,
        "rear rolling resistance did not affect coastdown");
}
} // namespace

int main()
{
    try {
        test_legacy_fallback_and_module_validation();
        test_explicit_legacy_modules_are_bitwise_equivalent();
        test_suspension_modules_act_on_only_the_selected_axle();
        test_tire_cornering_stiffness_is_axle_specific();
        test_damper_modules_change_bump_settling();
        test_axle_force_cap_droop_and_compression_stops();
        test_rear_tire_grip_limits_propulsion();
        test_mixed_radii_drive_angular_speed_and_rpm();
        test_rolling_resistance_module_affects_coastdown();
        std::cout << "axle_contact_physics_tests: 9 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "axle_contact_physics_tests: " << error.what() << '\n';
        return 1;
    }
}
