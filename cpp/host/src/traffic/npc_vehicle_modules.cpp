#include "traffic/npc_vehicle_modules.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace simcore_host {
namespace { constexpr double gravity = 9.80665; }

NpcVehicleModules::NpcVehicleModules(VehicleParameters parameters, double mass_kg)
    : parameters_(std::move(parameters)), mass_kg_(mass_kg),
      tire_modules_(parameters_.front_axle_contact.tire.has_value()
          || parameters_.rear_axle_contact.tire.has_value())
{
    if (!valid_vehicle_parameters(parameters_) || !std::isfinite(mass_kg_) || mass_kg_ <= 0.0)
        throw std::invalid_argument("Invalid NPC module parameters");
    if (parameters_.powertrain) powertrain_ = initial_powertrain_state(*parameters_.powertrain);
}

bool NpcVehicleModules::selected(const VehicleParameters& p)
{
    return p.powertrain || p.front_axle_contact.tire || p.rear_axle_contact.tire
        || p.front_axle_contact.suspension || p.rear_axle_contact.suspension;
}

void NpcVehicleModules::set_road_context(GroundSurfaceMaterialId material,
    double multiplier, double grade, double curvature)
{
    if (!std::isfinite(multiplier) || multiplier <= 0.0 || !std::isfinite(grade)
        || !std::isfinite(curvature)) throw std::invalid_argument("Invalid NPC module road context");
    double profile = parameters_.surface_default_friction_scale;
    switch (material) {
    case GroundSurfaceMaterialId::Asphalt: profile = parameters_.surface_asphalt_friction_scale; break;
    case GroundSurfaceMaterialId::LowFriction: profile = parameters_.surface_low_friction_scale; break;
    case GroundSurfaceMaterialId::Rough: profile = parameters_.surface_rough_friction_scale; break;
    default: break;
    }
    surface_scale_ = multiplier * profile;
    grade_ = grade;
    curvature_per_m_ = std::abs(curvature);
}

void NpcVehicleModules::set_direction(int direction)
{
    if (direction != -1 && direction != 1) throw std::invalid_argument("NPC direction must be forward or reverse");
    direction_ = direction;
}

double NpcVehicleModules::axle_grip_force(std::size_t axle, double speed) const
{
    const double load_fraction = axle == 0 ? parameters_.front_static_load_fraction
        : 1.0 - parameters_.front_static_load_fraction;
    const auto tire = resolved_tire_parameters(parameters_, axle * 2);
    const double normal_load = mass_kg_ * gravity * load_fraction / std::sqrt(1.0 + grade_ * grade_);
    const double total_grip = tire.friction_coefficient * surface_scale_ * normal_load;
    const double lateral_force = mass_kg_ * speed * speed * curvature_per_m_ * load_fraction;
    return std::sqrt(std::max(0.0, total_grip * total_grip - lateral_force * lateral_force));
}

double NpcVehicleModules::braking_limit_mps2(double requested) const
{
    if (!powertrain_ && !tire_modules_) return requested;
    // The corner-speed bound spends at most 80% on lateral force. Its circle
    // leaves sqrt(1-.8^2)=60% for stopping; use the same budget in lookahead.
    const double longitudinal_reserve = curvature_per_m_ > 1e-8 ? 0.6 : 1.0;
    const double tire_limit = longitudinal_reserve
        * (axle_grip_force(0, 0.0) + axle_grip_force(1, 0.0)) / mass_kg_;
    return std::max(1e-4, std::min({requested, tire_limit,
        static_cast<double>(parameters_.max_service_brake_n) / mass_kg_}));
}

double NpcVehicleModules::corner_speed_limit_mps() const
{
    if ((!powertrain_ && !tire_modules_) || curvature_per_m_ <= 1e-8) return 55.6;
    const double friction = std::min(resolved_tire_parameters(parameters_, 0).friction_coefficient,
        resolved_tire_parameters(parameters_, 2).friction_coefficient) * surface_scale_;
    // Leave a longitudinal reserve for braking/propulsion on a curve.
    return std::min(55.6, std::sqrt(0.8 * friction * gravity
        / (curvature_per_m_ * std::sqrt(1.0 + grade_ * grade_))));
}

double NpcVehicleModules::resistance_force(double speed) const
{
    const double rolling = parameters_.front_static_load_fraction
        * resolved_tire_parameters(parameters_, 0).rolling_resistance_coefficient
        + (1.0 - parameters_.front_static_load_fraction)
            * resolved_tire_parameters(parameters_, 2).rolling_resistance_coefficient;
    const double grade_force = direction_ * mass_kg_ * gravity * grade_ / std::sqrt(1.0 + grade_ * grade_);
    if (speed <= 1e-6) return std::max(0.0, grade_force);
    return mass_kg_ * gravity * rolling + grade_force
        + 0.5 * parameters_.air_density_kg_m3 * parameters_.drag_coefficient
            * parameters_.frontal_area_m2 * speed * speed;
}

double NpcVehicleModules::speed_after_step(double speed, double desired,
    double acceleration_limit, double braking_limit, double dt)
{
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.1 || !std::isfinite(speed)
        || !std::isfinite(desired) || speed < 0.0 || desired < 0.0)
        throw std::invalid_argument("Invalid NPC module integration step");
    if (!powertrain_ && !tire_modules_) {
        const double rate = desired >= speed ? acceleration_limit : braking_limit;
        return speed + std::clamp(desired - speed, -rate * dt, rate * dt);
    }
    const double braking = braking_limit_mps2(braking_limit);
    const double requested_acceleration = std::clamp((desired - speed) / dt, -braking, acceleration_limit);
    const double resistance = resistance_force(speed);
    const double demanded_force = std::max(0.0, mass_kg_ * requested_acceleration + resistance);
    const bool brake = desired < speed - 1e-6 || desired <= 1e-6;
    double drive_force = 0.0;
    if (powertrain_) {
        const auto& p = *parameters_.powertrain;
        const auto& state = *powertrain_;
        const double ratio = direction_ < 0 ? p.transmission.reverse_ratio
            : p.transmission.forward_ratios[state.forward_gear - 1];
        const double full_torque = std::min(interpolate_engine_torque(p.engine, state.engine_rpm),
            p.transmission.max_input_torque_nm) * ratio * p.drivetrain.final_drive_ratio
            * p.transmission.efficiency * p.drivetrain.efficiency;
        const double front_radius = resolved_tire_parameters(parameters_, 0).radius_m;
        const double rear_radius = resolved_tire_parameters(parameters_, 2).radius_m;
        const double full_force = full_torque * (p.drivetrain.front_torque_fraction / front_radius
            + (1.0 - p.drivetrain.front_torque_fraction) / rear_radius);
        const float throttle = brake ? 0.f : static_cast<float>(std::clamp(demanded_force / std::max(1e-6, full_force), 0.0, 1.0));
        advance_powertrain(p, *powertrain_, {throttle, direction_, static_cast<float>(direction_ * speed),
            static_cast<float>(direction_ * speed / front_radius), static_cast<float>(direction_ * speed / rear_radius), brake},
            static_cast<float>(dt));
        for (std::size_t axle = 0; axle < 2; ++axle) {
            const double force = direction_ * powertrain_->axle_drive_torque_nm[axle]
                / resolved_tire_parameters(parameters_, axle * 2).radius_m;
            drive_force += std::min(std::max(0.0, force), axle_grip_force(axle, speed));
        }
    } else if (!brake) {
        for (std::size_t axle = 0; axle < 2; ++axle) {
            const double split = axle == 0 ? parameters_.front_drive_torque_fraction
                : 1.0 - parameters_.front_drive_torque_fraction;
            drive_force += std::min(demanded_force * split, axle_grip_force(axle, speed));
        }
    }
    const double brake_force = brake ? std::min({braking * mass_kg_,
        axle_grip_force(0, speed) + axle_grip_force(1, speed),
        std::max(0.0, -requested_acceleration * mass_kg_ - resistance)}) : 0.0;
    const double acceleration = std::min(acceleration_limit,
        (drive_force - brake_force - resistance) / mass_kg_);
    double next = std::max(0.0, speed + acceleration * dt);
    if (desired < speed) next = std::max(desired, next);
    else if (acceleration > 0.0) next = std::min(desired, next);
    return next;
}

void NpcVehicleModules::idle(double dt)
{
    if (!powertrain_) return;
    while (dt > 1e-9) {
        const double h = std::min(dt, 1.0 / 60.0);
        advance_powertrain(*parameters_.powertrain, *powertrain_, {0.f, 0, 0.f, 0.f, 0.f, true}, static_cast<float>(h));
        dt -= h;
    }
}

double NpcVehicleModules::fuel_percent() const
{
    return powertrain_ ? 100.0 * powertrain_->remaining_fuel_l / parameters_.powertrain->fuel_tank.capacity_l : 100.0;
}
} // namespace simcore_host
