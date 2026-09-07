#pragma once

#include "physics/suspension_model.hpp"

// Configuration and validation shared by file loading and runtime profiles.
struct VehicleParameters {
    // Runtime motorcycle profile. Paired wire slots share each physical axle
    // contact; the default four-wheel configuration remains unchanged.
    bool single_track = false;
    float mass_kg                 = 1500.f;
    float wheelbase_m             = 2.70f;
    float max_steering_angle_rad  = 0.61086524f; // 35 degrees
    float steering_rate_rad_s     = 1.80f;
    float steering_return_rate_rad_s = 2.20f;
    float max_drive_force_n       = 6000.f;
    float max_reverse_force_n     = 3600.f;
    float max_drive_power_w       = 100000.f;
    // The accelerator requests force through a finite-response powertrain.
    // Rise and fall rates prevent keyboard input from becoming an impulse.
    float drive_force_rise_rate_n_per_s = 18000.f;
    float drive_force_fall_rate_n_per_s = 36000.f;
    float max_service_brake_n     = 18000.f;
    float max_handbrake_force_n   = 22000.f;
    float rolling_resistance_coeff = 0.015f;
    float drivetrain_drag_n_per_mps = 45.f;
    float drag_coefficient        = 0.29f;
    float frontal_area_m2         = 2.20f;
    float air_density_kg_m3       = 1.225f;
    float tire_radius_m           = 0.32f;
    float final_drive_ratio       = 3.42f;
    float drive_gear_ratio        = 3.50f;
    float reverse_gear_ratio      = 3.20f;
    float max_forward_speed_mps   = 50.f;
    float max_reverse_speed_mps   = 8.f;
    float idle_rpm                = 800.f;
    float max_rpm                 = 7000.f;
    float fuel_rate_percent_s     = 0.004f;
    float front_track_m           = 1.58f;
    float rear_track_m            = 1.58f;
    float cg_height_m             = 0.55f;
    // Static front-axle normal-load share. This also defines the CG's
    // longitudinal position: distance from CG to rear axle = share * wheelbase.
    float front_static_load_fraction = 0.55f;
    // Fraction of driveline force sent to the front axle (0=RWD, 1=FWD).
    float front_drive_torque_fraction = 0.f;
    // Fraction of service-brake force assigned to the front axle.
    float front_service_brake_fraction = 0.65f;
    float yaw_inertia_kg_m2       = 2850.f;
    float pitch_inertia_kg_m2     = 2200.f;
    float roll_inertia_kg_m2      = 700.f;
    // Reserved v4 compatibility fields. Both must remain zero: the four
    // suspension springs/dampers provide all physical pitch/roll stiffness and
    // damping, with no synthetic body-to-ground attitude spring.
    float attitude_spring_n_m_rad = 0.f;
    float attitude_damping_n_m_s_rad = 0.f;
    // Per-tire linear cornering stiffness. Axle-specific values calibrate the
    // handling balance; normal load and friction still bound each tire force.
    float front_tire_corner_stiffness_n_rad = 60000.f;
    float rear_tire_corner_stiffness_n_rad = 50000.f;
    float tire_longitudinal_stiffness_n = 90000.f;
    float tire_friction           = 1.05f;
    // Vehicle/tire-specific tuning layered over the terrain-authored
    // multiplier in GroundHit. Unknown future material IDs use default scale.
    float surface_default_friction_scale = 1.0f;
    float surface_asphalt_friction_scale = 1.0f;
    float surface_low_friction_scale = 1.0f;
    float surface_rough_friction_scale = 1.0f;
    // Lateral force is allocated first. Longitudinal drive/brake force may
    // only use the friction-circle reserve left after this fraction.
    float lateral_grip_priority   = 0.95f;
    float traction_control_slip_target = 0.08f;
    float traction_control_full_cut_slip = 0.12f;
    float wheel_inertia_kg_m2     = 1.8f;
    float wheel_free_spin_damping_n_m_s = 3.6f;
    // Regularizes slip angle near zero speed without disabling lateral grip.
    float low_speed_slip_reference_mps = 1.5f;
    // Authoritative collision shell dimensions. Defaults retain the validated
    // sedan contract; selectable profiles may replace them at a reset fence.
    float collision_body_overhang_m = 0.80f;
    float collision_body_side_padding_m = 0.15f;
    float collision_body_half_height_m = 0.75f;
    float chassis_shell_center_up_offset_m = 0.335f;
    float chassis_shell_half_height_m = 0.625f;
    simcore_host::SuspensionParameters suspension;
};

[[nodiscard]] bool valid_vehicle_parameters(
    const VehicleParameters& parameters);
