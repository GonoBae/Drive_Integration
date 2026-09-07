#pragma once

#include <algorithm>
#include <cmath>

namespace simcore_host {
// Positive canonical roll raises the left side, so a left turn needs negative
// roll. The equilibrium follows lateral acceleration, not steering input alone.
inline float motorcycle_equilibrium_roll(float speed_mps, float left_yaw_rate_rad_s)
{
    constexpr float gravity = 9.80665f;
    constexpr float maximum_lean = 0.872664626f; // 50 degrees
    return std::clamp(-std::atan(speed_mps * left_yaw_rate_rad_s / gravity),
        -maximum_lean, maximum_lean);
}

// A bounded rider balance actuator; it cannot hold an airborne or crashed bike.
// Tire forces still determine speed/yaw. This adds no lateral grip or yaw force.
inline float motorcycle_balance_torque(float target_roll, float roll, float roll_rate,
    float contact_roll_torque, float inertia, float mass, float cg_height,
    bool both_axles_supported, bool rider_attached)
{
    if (!both_axles_supported || !rider_attached || std::abs(roll) > 1.134464f)
        return 0.f; // 65 degrees is outside balance recovery
    constexpr float response_rad_s = 6.f;
    const float desired_acceleration = response_rad_s * response_rad_s * (target_roll - roll)
        - 2.f * response_rad_s * roll_rate;
    const float limit = 2.f * mass * 9.80665f * cg_height;
    return std::clamp(inertia * desired_acceleration - contact_roll_torque, -limit, limit);
}
}
