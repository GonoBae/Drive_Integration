#include "physics/vehicle_parameters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

AxleTireParameters resolved_tire_parameters(
    const VehicleParameters& parameters, std::size_t wheel_index)
{
    const auto& axle = wheel_index < 2
        ? parameters.front_axle_contact : parameters.rear_axle_contact;
    if (axle.tire) return *axle.tire;
    return {parameters.tire_radius_m, parameters.tire_friction,
        parameters.tire_longitudinal_stiffness_n,
        wheel_index < 2 ? parameters.front_tire_corner_stiffness_n_rad
                        : parameters.rear_tire_corner_stiffness_n_rad,
        parameters.rolling_resistance_coeff};
}

const simcore_host::SuspensionParameters& resolved_suspension_parameters(
    const VehicleParameters& parameters, std::size_t wheel_index)
{
    const auto& axle = wheel_index < 2
        ? parameters.front_axle_contact : parameters.rear_axle_contact;
    return axle.suspension ? *axle.suspension : parameters.suspension;
}

namespace {
bool valid_axle_contact_parameters(const AxleContactParameters& axle)
{
    if (axle.tire) {
        const auto& tire = *axle.tire;
        if (!std::isfinite(tire.radius_m) || tire.radius_m <= 0.f
            || !std::isfinite(tire.friction_coefficient) || tire.friction_coefficient <= 0.f
            || !std::isfinite(tire.longitudinal_stiffness_n) || tire.longitudinal_stiffness_n <= 0.f
            || !std::isfinite(tire.cornering_stiffness_n_rad) || tire.cornering_stiffness_n_rad <= 0.f
            || !std::isfinite(tire.rolling_resistance_coefficient) || tire.rolling_resistance_coefficient < 0.f) {
            return false;
        }
    }
    return !axle.suspension
        || simcore_host::valid_suspension_parameters(*axle.suspension);
}
} // namespace

bool valid_vehicle_parameters(const VehicleParameters& parameters)
{
    const std::array<float, 59> finite_values{
        parameters.mass_kg,
        parameters.wheelbase_m,
        parameters.max_steering_angle_rad,
        parameters.steering_rate_rad_s,
        parameters.steering_return_rate_rad_s,
        parameters.max_drive_force_n,
        parameters.max_reverse_force_n,
        parameters.max_drive_power_w,
        parameters.drive_force_rise_rate_n_per_s,
        parameters.drive_force_fall_rate_n_per_s,
        parameters.max_service_brake_n,
        parameters.max_handbrake_force_n,
        parameters.rolling_resistance_coeff,
        parameters.drivetrain_drag_n_per_mps,
        parameters.drag_coefficient,
        parameters.frontal_area_m2,
        parameters.air_density_kg_m3,
        parameters.tire_radius_m,
        parameters.final_drive_ratio,
        parameters.drive_gear_ratio,
        parameters.reverse_gear_ratio,
        parameters.max_forward_speed_mps,
        parameters.max_reverse_speed_mps,
        parameters.idle_rpm,
        parameters.max_rpm,
        parameters.fuel_rate_percent_s,
        parameters.front_track_m,
        parameters.rear_track_m,
        parameters.cg_height_m,
        parameters.front_static_load_fraction,
        parameters.front_drive_torque_fraction,
        parameters.front_service_brake_fraction,
        parameters.yaw_inertia_kg_m2,
        parameters.pitch_inertia_kg_m2,
        parameters.roll_inertia_kg_m2,
        parameters.attitude_spring_n_m_rad,
        parameters.attitude_damping_n_m_s_rad,
        parameters.front_tire_corner_stiffness_n_rad,
        parameters.rear_tire_corner_stiffness_n_rad,
        parameters.tire_longitudinal_stiffness_n,
        parameters.tire_friction,
        parameters.surface_default_friction_scale,
        parameters.surface_asphalt_friction_scale,
        parameters.surface_low_friction_scale,
        parameters.surface_rough_friction_scale,
        parameters.lateral_grip_priority,
        parameters.traction_control_slip_target,
        parameters.traction_control_full_cut_slip,
        parameters.wheel_inertia_kg_m2,
        parameters.wheel_free_spin_damping_n_m_s,
        parameters.low_speed_slip_reference_mps,
        parameters.collision_body_overhang_m,
        parameters.collision_body_side_padding_m,
        parameters.collision_body_half_height_m,
        parameters.collision_body_center_forward_offset_m,
        parameters.collision_body_ground_clearance_m,
        parameters.chassis_shell_center_up_offset_m,
        parameters.chassis_shell_half_height_m,
        parameters.suspension.rest_length_m,
    };
    if (!std::all_of(finite_values.begin(), finite_values.end(),
                     [](float value) { return std::isfinite(value); })) {
        return false;
    }

    return parameters.mass_kg > 0.f
        && parameters.wheelbase_m > 0.f
        && parameters.max_steering_angle_rad > 0.f
        && parameters.max_steering_angle_rad < std::numbers::pi_v<float> * 0.5f
        && parameters.steering_rate_rad_s > 0.f
        && parameters.steering_return_rate_rad_s > 0.f
        && parameters.max_drive_force_n >= 0.f
        && parameters.max_reverse_force_n >= 0.f
        && parameters.max_drive_power_w > 0.f
        && parameters.drive_force_rise_rate_n_per_s > 0.f
        && parameters.drive_force_fall_rate_n_per_s > 0.f
        && parameters.max_service_brake_n >= 0.f
        && parameters.max_handbrake_force_n >= 0.f
        && parameters.rolling_resistance_coeff >= 0.f
        && parameters.drivetrain_drag_n_per_mps >= 0.f
        && parameters.drag_coefficient >= 0.f
        && parameters.frontal_area_m2 > 0.f
        && parameters.air_density_kg_m3 > 0.f
        && parameters.tire_radius_m > 0.f
        && parameters.final_drive_ratio > 0.f
        && parameters.drive_gear_ratio > 0.f
        && parameters.reverse_gear_ratio > 0.f
        && parameters.max_forward_speed_mps > 0.f
        && parameters.max_reverse_speed_mps > 0.f
        && parameters.idle_rpm > 0.f
        && parameters.max_rpm >= parameters.idle_rpm
        && parameters.fuel_rate_percent_s >= 0.f
        && parameters.front_track_m > 0.f
        && parameters.rear_track_m > 0.f
        && parameters.cg_height_m > 0.f
        && parameters.front_static_load_fraction > 0.f
        && parameters.front_static_load_fraction < 1.f
        && parameters.front_drive_torque_fraction >= 0.f
        && parameters.front_drive_torque_fraction <= 1.f
        && parameters.front_service_brake_fraction >= 0.f
        && parameters.front_service_brake_fraction <= 1.f
        && parameters.yaw_inertia_kg_m2 > 0.f
        && parameters.pitch_inertia_kg_m2 > 0.f
        && parameters.roll_inertia_kg_m2 > 0.f
        // v4 retains these two serialized fields for compatibility, but the
        // four-corner model does not permit a synthetic body-to-ground spring.
        && parameters.attitude_spring_n_m_rad == 0.f
        && parameters.attitude_damping_n_m_s_rad == 0.f
        && parameters.front_tire_corner_stiffness_n_rad > 0.f
        && parameters.rear_tire_corner_stiffness_n_rad > 0.f
        && parameters.tire_longitudinal_stiffness_n > 0.f
        && parameters.tire_friction > 0.f
        && parameters.surface_default_friction_scale > 0.f
        && parameters.surface_default_friction_scale <= 4.f
        && parameters.surface_asphalt_friction_scale > 0.f
        && parameters.surface_asphalt_friction_scale <= 4.f
        && parameters.surface_low_friction_scale > 0.f
        && parameters.surface_low_friction_scale <= 4.f
        && parameters.surface_rough_friction_scale > 0.f
        && parameters.surface_rough_friction_scale <= 4.f
        && parameters.lateral_grip_priority > 0.f
        && parameters.lateral_grip_priority <= 1.f
        && parameters.traction_control_slip_target >= 0.f
        && parameters.traction_control_full_cut_slip
            > parameters.traction_control_slip_target
        && parameters.traction_control_full_cut_slip <= 1.f
        && parameters.wheel_inertia_kg_m2 > 0.f
        && parameters.wheel_free_spin_damping_n_m_s >= 0.f
        && parameters.low_speed_slip_reference_mps > 0.f
        && parameters.collision_body_overhang_m > 0.f
        && parameters.collision_body_side_padding_m > 0.f
        && parameters.collision_body_half_height_m > 0.f
        && std::abs(parameters.collision_body_center_forward_offset_m)
            < parameters.wheelbase_m * 0.5f + parameters.collision_body_overhang_m
        && parameters.collision_body_ground_clearance_m >= 0.f
        && parameters.chassis_shell_half_height_m > 0.f
        && std::abs(parameters.chassis_shell_center_up_offset_m)
            < parameters.collision_body_half_height_m * 2.f
        && simcore_host::valid_suspension_parameters(parameters.suspension)
        && valid_axle_contact_parameters(parameters.front_axle_contact)
        && valid_axle_contact_parameters(parameters.rear_axle_contact)
        && (!parameters.powertrain || simcore_host::valid_powertrain_parameters(*parameters.powertrain));
}
