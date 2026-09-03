#pragma once

#include "terrain/ground_query.hpp"

#include <cstddef>

namespace simcore_host {

// Six-degree presentation pose reduced to the three generalized coordinates
// owned by the ground-bound chassis solver. Heading/XY remain authoritative in
// CollisionWorld; this contact solve prevents the oriented body shell from
// passing through terrain after the tires lose support during a rollover.
struct ChassisGroundPose {
    double east_m = 0.0;
    double north_m = 0.0;
    double up_m = 0.0;
    double heading_rad = 0.0;
    double pitch_rad = 0.0;
    double roll_rad = 0.0;
    double root_up_velocity_mps = 0.0;
    double pitch_rate_rad_s = 0.0;
    double roll_rate_rad_s = 0.0;
};

struct ChassisContactBox {
    // Box centre measured from the vehicle CG along body-up.
    double center_up_offset_m = 0.0;
    double half_length_m = 0.0;
    double half_width_m = 0.0;
    double half_height_m = 0.0;
    double mass_kg = 0.0;
    double roll_inertia_kg_m2 = 0.0;
    double pitch_inertia_kg_m2 = 0.0;
    double yaw_inertia_kg_m2 = 0.0;
};

struct ChassisGroundContactResult {
    ChassisGroundPose pose;
    std::size_t contact_count = 0;
    double maximum_penetration_m = 0.0;
    double accumulated_normal_impulse_n_s = 0.0;
    bool corrected = false;
};

[[nodiscard]] ChassisGroundContactResult resolve_chassis_ground_contact(
    ChassisGroundPose pose,
    const ChassisContactBox& box,
    const GroundQuery& ground_query);

} // namespace simcore_host
