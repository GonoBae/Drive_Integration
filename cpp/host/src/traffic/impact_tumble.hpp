#pragma once

#include "collision/collision_types.hpp"

namespace simcore_host {

struct ImpactTumbleDimensions {
    double half_length_m = 2.2;
    double half_width_m = 1.0;
    double half_height_m = 0.75;
    double mass_kg = 1500.0;
    double contact_below_cg_m = 0.35;
};

struct ImpactTumbleSupport {
    double support_height_m = 0.0;
    double projected_half_length_m = 0.0;
    double projected_half_width_m = 0.0;
    double projected_half_height_m = 0.0;
    bool valid = false;
};

// Exact projection of the tilted box into its yaw-aligned horizontal frame.
// Placing its centre at ground + support_height keeps every corner above a
// locally horizontal ground plane, including at 90/180 degrees. The envelope
// is conservative collision geometry, not an independently simulated body.
[[nodiscard]] ImpactTumbleSupport impact_tumble_support(
    const ImpactTumbleDimensions& dimensions, double pitch_rad, double roll_rad) noexcept;

// Ground-constrained rigid-box rocking, not full six-DOF vehicle dynamics:
// collision angular impulses, gravity from the box's support-height potential,
// edge-pivot inertia, angular drag and energy loss when another face lands.
// There is no artificial flip command, upright spring, flight or auto-righting.
// Pitch is nose-up positive; roll is left-up positive; heading is North=0/CW+.
class ImpactTumbleState {
public:
    // Direction is the impulse APPLIED TO THIS ACTOR. CollisionContact normals
    // point obstacle->Ego, so an impacted NPC receives the negative normal.
    // Positive impulse directions are normalized; zero impulse is a no-op.
    void apply_contact(double normal_impulse_n_s, CollisionVector2 impulse_direction_enu,
                       double heading_rad, const ImpactTumbleDimensions& dimensions) noexcept;
    // Finite dt in [0,10]. Pausing preserves the pose and angular velocity.
    // Invalid input freezes the last finite pose and latches invalid_input().
    void tick(double dt_seconds, const ImpactTumbleDimensions& dimensions,
              bool enabled = true) noexcept;
    // Collision rejection can stop angular motion without uprighting the actor
    // or releasing its route-disable latch. Gravity can act again on next tick.
    void stop_motion() noexcept;
    void reset() noexcept;

    [[nodiscard]] double pitch_rad() const noexcept { return pitch_rad_; }
    [[nodiscard]] double roll_rad() const noexcept { return roll_rad_; }
    [[nodiscard]] double pitch_rate_rad_s() const noexcept { return pitch_rate_rad_s_; }
    [[nodiscard]] double roll_rate_rad_s() const noexcept { return roll_rate_rad_s_; }
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool settled() const noexcept;
    // Latches after the body up-axis is more than 60 degrees from vertical.
    // A toppled actor must not be pulled back onto its route by navigation.
    [[nodiscard]] bool overturned() const noexcept { return overturned_; }
    [[nodiscard]] bool invalid_input() const noexcept { return invalid_input_; }

private:
    void fail_closed() noexcept;
    double pitch_rad_ = 0.0;
    double roll_rad_ = 0.0;
    double pitch_rate_rad_s_ = 0.0;
    double roll_rate_rad_s_ = 0.0;
    bool overturned_ = false;
    bool invalid_input_ = false;
};

} // namespace simcore_host
