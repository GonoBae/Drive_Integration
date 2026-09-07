#include "player_vehicle_profile.hpp"

#include <stdexcept>

namespace simcore_host {
namespace {

void scale_suspension(VehicleParameters& result, const VehicleParameters& sedan)
{
    const float scale = result.mass_kg / sedan.mass_kg;
    result.suspension.spring_rate_n_per_m = sedan.suspension.spring_rate_n_per_m * scale;
    result.suspension.damper_rate_n_s_per_m = sedan.suspension.damper_rate_n_s_per_m * scale;
    result.suspension.max_force_n = sedan.suspension.max_force_n * scale;
}

} // namespace

const char* runtime_vehicle_class_name(RuntimeVehicleClass vehicle_class)
{
    switch (vehicle_class) {
    case RuntimeVehicleClass::Unspecified: return "unspecified";
    case RuntimeVehicleClass::Sedan: return "sedan";
    case RuntimeVehicleClass::Compact: return "compact";
    case RuntimeVehicleClass::Truck: return "truck";
    case RuntimeVehicleClass::Motorcycle: return "motorcycle";
    }
    return "invalid";
}

VehicleParameters make_player_vehicle_parameters(
    const VehicleParameters& sedan,
    RuntimeVehicleClass vehicle_class)
{
    if (!valid_vehicle_parameters(sedan)) {
        throw std::invalid_argument("configured sedan profile is invalid");
    }
    if (vehicle_class == RuntimeVehicleClass::Unspecified) {
        vehicle_class = RuntimeVehicleClass::Sedan;
    }

    VehicleParameters result = sedan;
    switch (vehicle_class) {
    case RuntimeVehicleClass::Sedan:
        break;
    case RuntimeVehicleClass::Compact:
        result.mass_kg = 1050.f;
        result.wheelbase_m = 2.133f;
        result.max_steering_angle_rad = 0.6632251f; // 38 degrees
        result.max_drive_force_n = 5200.f;
        result.max_reverse_force_n = 3100.f;
        result.max_drive_power_w = 80000.f;
        result.max_service_brake_n = 14500.f;
        result.max_handbrake_force_n = 16500.f;
        result.drag_coefficient = 0.31f;
        result.frontal_area_m2 = 1.90f;
        result.tire_radius_m = 0.275f;
        result.max_forward_speed_mps = 45.f;
        result.front_track_m = 1.422f;
        result.rear_track_m = 1.422f;
        result.cg_height_m = 0.48f;
        result.yaw_inertia_kg_m2 = 1450.f;
        result.pitch_inertia_kg_m2 = 1250.f;
        result.roll_inertia_kg_m2 = 450.f;
        result.front_tire_corner_stiffness_n_rad = 45000.f;
        result.rear_tire_corner_stiffness_n_rad = 39000.f;
        result.tire_longitudinal_stiffness_n = 68000.f;
        result.wheel_inertia_kg_m2 = 1.2f;
        result.collision_body_overhang_m = 0.6835f;
        result.collision_body_side_padding_m = 0.149f;
        result.collision_body_half_height_m = 0.70f;
        result.chassis_shell_center_up_offset_m = 0.30f;
        result.chassis_shell_half_height_m = 0.56f;
        scale_suspension(result, sedan);
        break;
    case RuntimeVehicleClass::Truck:
        result.mass_kg = 6200.f;
        result.wheelbase_m = 3.85f;
        result.max_steering_angle_rad = 0.4886922f; // 28 degrees
        result.steering_rate_rad_s = 1.25f;
        result.steering_return_rate_rad_s = 1.55f;
        result.max_drive_force_n = 18000.f;
        result.max_reverse_force_n = 11000.f;
        result.max_drive_power_w = 180000.f;
        result.drive_force_rise_rate_n_per_s = 30000.f;
        result.drive_force_fall_rate_n_per_s = 50000.f;
        result.max_service_brake_n = 65000.f;
        result.max_handbrake_force_n = 72000.f;
        result.drivetrain_drag_n_per_mps = 100.f;
        result.drag_coefficient = 0.58f;
        result.frontal_area_m2 = 7.2f;
        result.tire_radius_m = 0.390f;
        result.max_forward_speed_mps = 28.f;
        result.max_reverse_speed_mps = 6.f;
        result.front_track_m = 1.90f;
        result.rear_track_m = 1.90f;
        result.cg_height_m = 0.75f;
        result.front_static_load_fraction = 0.58f;
        result.yaw_inertia_kg_m2 = 14500.f;
        result.pitch_inertia_kg_m2 = 12000.f;
        result.roll_inertia_kg_m2 = 4500.f;
        result.front_tire_corner_stiffness_n_rad = 180000.f;
        result.rear_tire_corner_stiffness_n_rad = 160000.f;
        result.tire_longitudinal_stiffness_n = 260000.f;
        result.tire_friction = 0.95f;
        result.wheel_inertia_kg_m2 = 5.5f;
        result.wheel_free_spin_damping_n_m_s = 8.0f;
        result.collision_body_overhang_m = 1.725f;
        result.collision_body_side_padding_m = 0.27f;
        result.collision_body_half_height_m = 1.25f;
        result.chassis_shell_center_up_offset_m = 0.54f;
        result.chassis_shell_half_height_m = 1.08f;
        scale_suspension(result, sedan);
        break;
    case RuntimeVehicleClass::Motorcycle:
        result.single_track = true;
        result.mass_kg = 240.f;
        result.wheelbase_m = 1.81f;
        result.max_steering_angle_rad = 0.5585054f; // 32 degrees
        result.steering_rate_rad_s = 2.4f;
        result.steering_return_rate_rad_s = 2.8f;
        result.max_drive_force_n = 1800.f;
        result.max_reverse_force_n = 900.f;
        result.max_drive_power_w = 55000.f;
        result.drive_force_rise_rate_n_per_s = 8000.f;
        result.drive_force_fall_rate_n_per_s = 12000.f;
        result.max_service_brake_n = 3200.f;
        result.max_handbrake_force_n = 3600.f;
        result.drivetrain_drag_n_per_mps = 12.f;
        result.drag_coefficient = 0.55f;
        result.frontal_area_m2 = 0.70f;
        result.tire_radius_m = 0.336f;
        result.max_forward_speed_mps = 50.f;
        result.front_track_m = 0.60f;
        result.rear_track_m = 0.60f;
        result.cg_height_m = 0.42f;
        result.front_static_load_fraction = 0.82f / 1.81f;
        result.front_drive_torque_fraction = 0.f;
        result.yaw_inertia_kg_m2 = 210.f;
        result.pitch_inertia_kg_m2 = 180.f;
        result.roll_inertia_kg_m2 = 70.f;
        result.front_tire_corner_stiffness_n_rad = 12000.f;
        result.rear_tire_corner_stiffness_n_rad = 10500.f;
        result.tire_longitudinal_stiffness_n = 18000.f;
        result.tire_friction = 1.10f;
        result.wheel_inertia_kg_m2 = 0.55f;
        result.wheel_free_spin_damping_n_m_s = 1.4f;
        result.collision_body_overhang_m = 0.195f;
        result.collision_body_side_padding_m = 0.12f;
        result.collision_body_half_height_m = 0.68f;
        result.chassis_shell_center_up_offset_m = 0.20f;
        result.chassis_shell_half_height_m = 0.36f;
        scale_suspension(result, sedan);
        result.suspension.rest_length_m = result.cg_height_m - result.tire_radius_m + 0.12f;
        result.suspension.max_compression_m = 0.16f;
        break;
    case RuntimeVehicleClass::Unspecified:
        break;
    default:
        throw std::invalid_argument("unsupported player vehicle class");
    }
    if (!valid_vehicle_parameters(result)) {
        throw std::invalid_argument("selected player vehicle profile is invalid");
    }
    return result;
}

} // namespace simcore_host
