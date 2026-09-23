#include "physics/powertrain_model.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace simcore_host {
namespace {
constexpr float rpm_to_rad_s = 2.f * std::numbers::pi_v<float> / 60.f;
constexpr float direction_change_speed_mps = 0.5f;

bool finite_between(float value, float minimum, float maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}
} // namespace

bool valid_powertrain_parameters(const PowertrainParameters& p)
{
    const auto& e = p.engine;
    const auto& t = p.transmission;
    const auto& d = p.drivetrain;
    const auto& f = p.fuel_tank;
    if (!finite_between(e.idle_rpm, 1.f, 100000.f)
        || !finite_between(e.max_rpm, e.idle_rpm, 100000.f) || e.max_rpm <= e.idle_rpm
        || !finite_between(e.rotational_inertia_kg_m2, 1e-5f, 1e6f)
        || !finite_between(e.response_time_s, 1e-4f, 60.f)
        || !finite_between(e.idle_fuel_lph, 0.f, 1e5f)
        || !finite_between(e.bsfc_g_per_kwh, 1.f, 1e5f)
        || e.torque_curve.size() < 2 || e.torque_curve.size() > 256
        || t.forward_ratios.empty() || t.forward_ratios.size() > 32
        || !finite_between(t.reverse_ratio, 1e-4f, 1000.f)
        || !finite_between(t.max_input_torque_nm, 1e-4f, 1e7f)
        || !finite_between(t.efficiency, 1e-4f, 1.f)
        || !finite_between(t.downshift_rpm, e.idle_rpm, e.max_rpm) || t.downshift_rpm <= e.idle_rpm
        || !finite_between(t.upshift_rpm, t.downshift_rpm, e.max_rpm)
        || t.upshift_rpm <= t.downshift_rpm || t.upshift_rpm >= e.max_rpm
        || !finite_between(t.shift_duration_s, 0.f, 10.f) || t.shift_duration_s <= 0.f
        || !finite_between(d.final_drive_ratio, 1e-4f, 1000.f)
        || !finite_between(d.front_torque_fraction, 0.f, 1.f)
        || !finite_between(d.efficiency, 1e-4f, 1.f)
        || !finite_between(f.capacity_l, 1e-5f, 1e6f)
        || !finite_between(f.initial_fuel_l, 0.f, f.capacity_l)
        || !finite_between(f.fuel_density_kg_l, 1e-3f, 10.f)) return false;
    float previous_rpm = -1.f;
    bool positive_torque = false;
    for (const auto& point : e.torque_curve) {
        if (!finite_between(point.rpm, 0.f, 100000.f) || point.rpm <= previous_rpm
            || !finite_between(point.torque_nm, 0.f, 1e7f)) return false;
        previous_rpm = point.rpm;
        positive_torque = positive_torque || point.torque_nm > 0.f;
    }
    if (!positive_torque || e.torque_curve.front().rpm > e.idle_rpm
        || e.torque_curve.back().rpm < e.max_rpm) return false;
    float previous_ratio = 1001.f;
    for (const float ratio : t.forward_ratios) {
        if (!finite_between(ratio, 1e-4f, 1000.f) || ratio >= previous_ratio) return false;
        previous_ratio = ratio;
    }
    return true;
}

float interpolate_engine_torque(const EngineParameters& e, float rpm)
{
    if (e.torque_curve.empty() || !std::isfinite(rpm)) return 0.f;
    if (rpm <= e.torque_curve.front().rpm) return e.torque_curve.front().torque_nm;
    if (rpm >= e.torque_curve.back().rpm) return e.torque_curve.back().torque_nm;
    const auto upper = std::upper_bound(e.torque_curve.begin(), e.torque_curve.end(), rpm,
        [](float value, const TorqueCurvePoint& point) { return value < point.rpm; });
    const auto& lower = *(upper - 1);
    const float alpha = (rpm - lower.rpm) / (upper->rpm - lower.rpm);
    return lower.torque_nm + alpha * (upper->torque_nm - lower.torque_nm);
}

PowertrainState initial_powertrain_state(const PowertrainParameters& p)
{
    if (!valid_powertrain_parameters(p)) throw std::invalid_argument("Invalid modular powertrain parameters");
    PowertrainState state;
    state.remaining_fuel_l = p.fuel_tank.initial_fuel_l;
    state.engine_rpm = state.remaining_fuel_l > 0.0 ? p.engine.idle_rpm : 0.f;
    return state;
}

void advance_powertrain(const PowertrainParameters& p, PowertrainState& state,
    const PowertrainInput& input, float dt)
{
    if (!std::isfinite(dt) || dt <= 0.f || dt > 0.1f
        || !std::isfinite(input.throttle) || !std::isfinite(input.vehicle_speed_mps)
        || !std::isfinite(input.front_wheel_angular_speed_rad_s)
        || !std::isfinite(input.rear_wheel_angular_speed_rad_s)
        || input.direction < -1 || input.direction > 1
        || state.forward_gear < 1 || state.forward_gear > p.transmission.forward_ratios.size()
        || state.pending_forward_gear < 1 || state.pending_forward_gear > p.transmission.forward_ratios.size()) {
        throw std::invalid_argument("Invalid modular powertrain step");
    }
    const auto& e = p.engine;
    const auto& t = p.transmission;
    const auto& d = p.drivetrain;
    state.axle_drive_torque_nm = {};
    state.combustion_torque_nm = 0.f;
    state.transmission_input_torque_nm = 0.f;
    state.crank_power_kw = 0.f;
    state.fuel_flow_lph = 0.0;
    state.shifting = false;
    const bool wrong_direction = input.direction != 0
        && input.direction * input.vehicle_speed_mps < -direction_change_speed_mps;
    state.drive_inhibited = input.torque_inhibited || wrong_direction;

    if (state.selected_direction != input.direction) {
        state.selected_direction = input.direction;
        state.forward_gear = state.pending_forward_gear = 1;
        state.shift_remaining_s = input.direction == 0 ? 0.f : t.shift_duration_s;
    }
    if (state.shift_remaining_s > 0.f) {
        state.shifting = true;
        state.shift_remaining_s = std::max(0.f, state.shift_remaining_s - dt);
        if (state.shift_remaining_s <= 0.f) state.forward_gear = state.pending_forward_gear;
    }

    const float wheel_speed = std::abs(input.front_wheel_angular_speed_rad_s
        * d.front_torque_fraction + input.rear_wheel_angular_speed_rad_s
        * (1.f - d.front_torque_fraction));
    float ratio = input.direction < 0 ? t.reverse_ratio : t.forward_ratios[state.forward_gear - 1];
    float shaft_rpm = wheel_speed * ratio * d.final_drive_ratio / rpm_to_rad_s;
    if (input.direction > 0 && !state.shifting && !state.drive_inhibited) {
        std::size_t next = state.forward_gear;
        if (shaft_rpm >= t.upshift_rpm && next < t.forward_ratios.size()) {
            const float projected = shaft_rpm * t.forward_ratios[next] / ratio;
            if (projected > t.downshift_rpm) ++next;
        } else if (shaft_rpm < t.downshift_rpm && next > 1) {
            const float projected = shaft_rpm * t.forward_ratios[next - 2] / ratio;
            if (projected < t.upshift_rpm) --next;
        }
        if (next != state.forward_gear) {
            state.pending_forward_gear = next;
            state.shift_remaining_s = t.shift_duration_s;
            state.shifting = true;
        }
    }

    const bool fueled = state.remaining_fuel_l > 0.0;
    const bool engaged = input.direction != 0 && !state.drive_inhibited && !state.shifting;
    const bool rpm_limiter = engaged && shaft_rpm >= e.max_rpm;
    const float throttle_target = !fueled || state.drive_inhibited || state.shifting || rpm_limiter
        ? 0.f : std::clamp(input.throttle, 0.f, 1.f);
    const float throttle_alpha = -std::expm1(-dt / e.response_time_s);
    state.filtered_throttle += (throttle_target - state.filtered_throttle) * throttle_alpha;
    // Safety gates cut output immediately, not after the response filter decays.
    const float combustion = fueled && !state.drive_inhibited && !state.shifting && !rpm_limiter
        ? state.filtered_throttle * interpolate_engine_torque(e, state.engine_rpm) : 0.f;
    const float old_omega = state.engine_rpm * rpm_to_rad_s;
    const float idle_omega = e.idle_rpm * rpm_to_rad_s;
    const float max_omega = e.max_rpm * rpm_to_rad_s;
    const float target_omega = !fueled ? 0.f : engaged
        ? std::clamp(shaft_rpm * rpm_to_rad_s, idle_omega, max_omega)
        : idle_omega + (state.shifting || state.drive_inhibited ? 0.f : state.filtered_throttle)
            * (max_omega - idle_omega);

    // A reduced slipping-clutch model, not a torque converter. The engine
    // follows the shaft through finite rotational inertia. Positive rotor
    // acceleration is paid out of combustion torque; released rotor energy
    // during a shift is dissipated rather than added to wheel propulsion.
    const float synchronizing_damping = std::max(1.f, interpolate_engine_torque(e, state.engine_rpm))
        / idle_omega;
    const float desired_delta_omega = (target_omega - old_omega)
        * (dt * synchronizing_damping) / (e.rotational_inertia_kg_m2 + dt * synchronizing_damping);
    const float idle_governor_torque = fueled && old_omega < idle_omega
        ? interpolate_engine_torque(e, e.idle_rpm) : 0.f;
    const float deceleration_torque = std::max(1.f, interpolate_engine_torque(e, state.engine_rpm));
    const float delta_omega = std::clamp(desired_delta_omega,
        -deceleration_torque * dt / e.rotational_inertia_kg_m2,
        (combustion + idle_governor_torque) * dt / e.rotational_inertia_kg_m2);
    const float next_omega = std::clamp(old_omega + delta_omega, 0.f, max_omega);
    const float mean_omega = (old_omega + next_omega) * 0.5f;
    const float rotor_acceleration_torque = std::max(0.f,
        (next_omega - old_omega) * e.rotational_inertia_kg_m2 / dt);
    state.engine_rpm = next_omega / rpm_to_rad_s;
    state.combustion_torque_nm = combustion;
    state.crank_power_kw = combustion * mean_omega / 1000.f;
    const float available_drive_torque = std::max(0.f, combustion - rotor_acceleration_torque);
    // Prevent a lagging engine from delivering torque at a faster shaft and
    // inventing mechanical power. Below idle, clutch slip dissipates the excess.
    const float power_ratio = mean_omega / std::max(mean_omega,
        wheel_speed * ratio * d.final_drive_ratio + 1e-6f);
    state.transmission_input_torque_nm = engaged && fueled && !rpm_limiter
        ? std::min(t.max_input_torque_nm, available_drive_torque * power_ratio) : 0.f;

    if (fueled) {
        // g/kWh * kW = g/hour, then grams -> kilograms -> litres.
        const double requested_flow_lph = e.idle_fuel_lph
            + e.bsfc_g_per_kwh * static_cast<double>(state.crank_power_kw)
                / (1000.0 * p.fuel_tank.fuel_density_kg_l);
        const double requested_litres = requested_flow_lph * dt / 3600.0;
        const double fuel_fraction = requested_litres > 0.0
            ? std::min(1.0, state.remaining_fuel_l / requested_litres) : 1.0;
        state.fuel_flow_lph = requested_flow_lph * fuel_fraction;
        state.remaining_fuel_l = std::max(0.0, state.remaining_fuel_l - requested_litres);
        state.combustion_torque_nm *= static_cast<float>(fuel_fraction);
        state.transmission_input_torque_nm *= static_cast<float>(fuel_fraction);
        state.crank_power_kw *= static_cast<float>(fuel_fraction);
        // The final partly-fueled tick supplies its time-averaged torque.
        // Rotor acceleration receives that same fraction of combustion work.
        if (fuel_fraction < 1.0 && next_omega > old_omega) {
            state.engine_rpm = (old_omega + (next_omega - old_omega)
                * static_cast<float>(fuel_fraction)) / rpm_to_rad_s;
        }
    }
    const float total_axle_torque = input.direction * state.transmission_input_torque_nm
        * ratio * d.final_drive_ratio * t.efficiency * d.efficiency;
    state.axle_drive_torque_nm = {total_axle_torque * d.front_torque_fraction,
        total_axle_torque * (1.f - d.front_torque_fraction)};
}

} // namespace simcore_host
