#pragma once

#include "protocol/vehicle_messages.hpp"
#include "traffic/impact_recovery.hpp"

namespace simcore_host {

// Shared collision response for lane vehicles and pedestrians. The host owns
// route/ground queries; this state owns impact momentum, return motion, and
// damage presentation across their prepare/resolve/publish phases.
struct RuntimeCollisionReaction {
    static constexpr double kDefaultVelocityDampingPerSecond = 2.5;

    static const char* phase_name(ImpactRecoveryPhase phase);
    void reset_return_path();
    void update(const KinematicCollisionProxy& actual, double dt, bool enabled,
        bool attitude_settled = true);
    void publish(RuntimeEntityState& entity,
        const std::vector<CollisionContact>& contacts);
    void integrate_offset(double dt,
        double damping_per_second = kDefaultVelocityDampingPerSecond,
        bool kinematic_vehicle_return = false, double nominal_heading_rad = 0.0);
    void finish_recovery();

    CollisionVector2 offset_enu_m;
    CollisionVector2 velocity_enu_mps;
    CollisionVector2 nominal_velocity_enu_mps;
    double heading_offset_rad = 0.0;
    double heading_rate_rad_s = 0.0;
    double nominal_heading_rate_rad_s = 0.0;
    ImpactRecoveryState recovery;
    double return_speed_mps = 0.0;
    double return_yaw_speed_rad_s = 0.0;
    // The UE driver presentation needs this fixed interval to close the
    // door and sit down before the NPC begins its self-driven return.
    double return_departure_grace_remaining_s = 0.0;
    // A recoverable vehicle follows one fixed body-aligned cubic back to
    // its paused route pose. Keeping the plan stable prevents per-tick
    // forward/reverse flips and unconstrained lateral translation.
    bool vehicle_return_path_initialized = false;
    int vehicle_return_direction = 0;
    double vehicle_return_progress = 0.0;
    CollisionVector2 vehicle_return_p0;
    CollisionVector2 vehicle_return_p1;
    CollisionVector2 vehicle_return_p2;
    std::uint32_t presented_event_sequence = 0;
    double presented_impact_impulse_n_s = 0.0;
    CollisionVector2 impact_direction_enu;
    VehicleDamageZone damage_zone = VehicleDamageZone::None;
    std::vector<VehicleDentPatch> dent_patches;
    ImpactRecoveryPhase reported_phase = ImpactRecoveryPhase::Driving;
};

} // namespace simcore_host
