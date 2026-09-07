#include "runtime_collision_reaction.hpp"

#include "collision/vehicle_dent.hpp"
#include "traffic/npc_return_curve.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

namespace simcore_host {
namespace {

double estimate_return_curve_length(const CollisionVector2& p0,
    const CollisionVector2& p1,
    const CollisionVector2& p2, double begin = 0.0)
{
    const NpcReturnCurvePlan curve{p0, p1, p2, 1, true};
    constexpr int samples = 32;
    auto previous = npc_return_curve_point(curve, begin);
    double length = 0.0;
    for (int index = 1; index <= samples; ++index) {
        const double t = begin + (1.0 - begin) * index / samples;
        const auto point = npc_return_curve_point(curve, t);
        length += std::hypot(point.east_m - previous.east_m,
            point.north_m - previous.north_m);
        previous = point;
    }
    return length;
}

} // namespace

void RuntimeCollisionReaction::reset_return_path()
{
    vehicle_return_path_initialized = false;
    vehicle_return_direction = 0;
    vehicle_return_progress = 0.0;
    vehicle_return_p0 = {};
    vehicle_return_p1 = {};
    vehicle_return_p2 = {};
}

const char* RuntimeCollisionReaction::phase_name(ImpactRecoveryPhase phase)
{
    using Phase = ImpactRecoveryPhase;
    switch (phase) {
    case Phase::Driving: return "driving";
    case Phase::Settling: return "settling";
    case Phase::Holding: return "holding";
    case Phase::Recovering: return "recovering";
    case Phase::Disabled: return "disabled";
    }
    return "disabled";
}

void RuntimeCollisionReaction::update(
    const KinematicCollisionProxy& actual, double dt, bool enabled,
    bool attitude_settled)
{
    using Phase = ImpactRecoveryPhase;
    const auto previous = recovery.phase();
    const bool driving = recovery.allows_driving();
    const auto velocity = driving ? actual.linear_velocity_enu_mps : velocity_enu_mps;
    const double yaw_rate = driving ? actual.heading_rate_rad_s : heading_rate_rad_s;
    recovery.tick(dt, std::hypot(velocity.east_m, velocity.north_m) < 0.05
        && std::abs(yaw_rate) < 0.02 && attitude_settled, enabled);
    const auto phase = recovery.phase();
    if (previous != Phase::Recovering && phase == Phase::Recovering) {
        return_departure_grace_remaining_s = 2.0;
    }
    if ((previous == Phase::Driving || previous == Phase::Recovering)
        && (phase == Phase::Settling || phase == Phase::Holding || phase == Phase::Disabled)) {
        // Stop the route motor, not physical momentum. Coast from the actual
        // post-impact velocity while the route anchor remains stationary.
        velocity_enu_mps = actual.linear_velocity_enu_mps;
        heading_rate_rad_s = actual.heading_rate_rad_s;
        return_speed_mps = 0.0;
        return_yaw_speed_rad_s = 0.0;
        return_departure_grace_remaining_s = 0.0;
        reset_return_path();
    }
    if (phase != reported_phase) {
        std::cout << "[Collision] entity=" << actual.proxy_id
                  << " state=" << phase_name(phase)
                  << " damage=" << recovery.damage_percent()
                  << " hold_s=" << recovery.hold_remaining_seconds() << "\n";
        reported_phase = phase;
    }
}

void RuntimeCollisionReaction::publish(RuntimeEntityState& entity,
    const std::vector<CollisionContact>& contacts)
{
    const auto event = recovery.event_sequence();
    const double impulse = recovery.last_impact_impulse_n_s();
    if (event != 0 && (event != presented_event_sequence
        || impulse > presented_impact_impulse_n_s + 1e-6)) {
        const CollisionContact* strongest = nullptr;
        for (const auto& contact : contacts) {
            if (contact.collider_id == entity.collision_proxy.proxy_id
                && contact.accumulated_normal_impulse_n_s > 0.0
                && (!strongest || contact.accumulated_normal_impulse_n_s
                    > strongest->accumulated_normal_impulse_n_s)) strongest = &contact;
        }
        if (strongest) {
            impact_direction_enu = {-strongest->normal_enu.east_m,
                -strongest->normal_enu.north_m};
            if (const auto* body = std::get_if<ObbPrism>(&entity.collision_proxy.shape)) {
                if (entity.kind == RuntimeEntityKind::NpcVehicle) {
                    auto dent_contact = *strongest;
                    dent_contact.accumulated_normal_impulse_n_s = impulse;
                    record_vehicle_dent(dent_patches,*body,dent_contact,true);
                }
                const double east = strongest->contact_point_enu.east_m - body->center_enu.east_m;
                const double north = strongest->contact_point_enu.north_m - body->center_enu.north_m;
                const double forward = east * std::sin(body->heading_rad) + north * std::cos(body->heading_rad);
                const double right = east * std::cos(body->heading_rad) - north * std::sin(body->heading_rad);
                damage_zone = std::abs(forward) / body->half_length_m >= std::abs(right) / body->half_width_m
                    ? (forward >= 0.0 ? VehicleDamageZone::Front : VehicleDamageZone::Rear)
                    : (right >= 0.0 ? VehicleDamageZone::Right : VehicleDamageZone::Left);
            }
            presented_event_sequence = event;
            presented_impact_impulse_n_s = impulse;
        }
    }
    entity.damage_percent = static_cast<float>(recovery.damage_percent());
    entity.last_impact_impulse_n_s = static_cast<float>(impulse);
    entity.damage_zone = damage_zone;
    entity.collision_event_sequence = event;
    entity.recovery_phase = static_cast<std::uint32_t>(recovery.phase());
    entity.impact_direction_enu = impact_direction_enu;
    entity.dent_patches = dent_patches;
}

void RuntimeCollisionReaction::integrate_offset(double dt,
    double damping_per_second,
    bool kinematic_vehicle_return, double nominal_heading_rad)
{
    using Phase = ImpactRecoveryPhase;
    const auto phase = recovery.phase();
    if (phase == Phase::Holding) {
        velocity_enu_mps = {};
        heading_rate_rad_s = 0.0;
        return;
    }
    if (kinematic_vehicle_return && phase == Phase::Recovering
        && return_departure_grace_remaining_s > 0.0) {
        // Do not make the car glide away while its presented driver is still
        // reversing the exit sequence. This is a stationary boarding pause,
        // so residual solver velocities are intentionally cleared.
        velocity_enu_mps = {};
        heading_rate_rad_s = 0.0;
        return_speed_mps = 0.0;
        return_yaw_speed_rad_s = 0.0;
        return_departure_grace_remaining_s = std::max(
            0.0, return_departure_grace_remaining_s - dt);
        return;
    }
    if (std::hypot(velocity_enu_mps.east_m, velocity_enu_mps.north_m) < 0.05)
        velocity_enu_mps = {};
    if (std::abs(heading_rate_rad_s) < 0.02) heading_rate_rad_s = 0.0;
    const double decay = std::exp(-damping_per_second * dt);
    const double integrated_time = (1.0 - decay) / damping_per_second;
    offset_enu_m.east_m += velocity_enu_mps.east_m * integrated_time;
    offset_enu_m.north_m += velocity_enu_mps.north_m * integrated_time;
    heading_offset_rad = std::remainder(heading_offset_rad
        + heading_rate_rad_s * integrated_time, 2.0 * std::numbers::pi);
    velocity_enu_mps.east_m *= decay;
    velocity_enu_mps.north_m *= decay;
    heading_rate_rad_s *= decay;

    if (phase != Phase::Recovering && phase != Phase::Driving) return;
    if (kinematic_vehicle_return && phase == Phase::Recovering) {
        // Follow one fixed cubic whose start/end tangents are the car body and
        // authored lane headings. Unlike radial offset shrinkage, every return
        // displacement is along a continuous driven curve, including lateral
        // and yaw-only collision offsets.
        const double distance = std::hypot(
            offset_enu_m.east_m, offset_enu_m.north_m);
        const double heading_error = std::remainder(
            -heading_offset_rad, 2.0 * std::numbers::pi);
        if (distance <= 1.0e-9 && std::abs(heading_error) <= 1.0e-9) {
            offset_enu_m = {};
            heading_offset_rad = 0.0;
            return_speed_mps = 0.0;
            return_yaw_speed_rad_s = 0.0;
            reset_return_path();
            return;
        }

        if (!vehicle_return_path_initialized) {
            const double actual_heading = nominal_heading_rad + heading_offset_rad;
            const auto plan = make_npc_return_curve_plan(
                offset_enu_m, actual_heading, nominal_heading_rad);
            if (!plan.valid) {
                // A yaw-only or degenerate pose cannot be corrected by one
                // physically driven cubic. Hold it for a later safe replan.
                return_speed_mps = 0.0;
                return_yaw_speed_rad_s = 0.0;
                reset_return_path();
                return;
            }
            vehicle_return_direction = plan.direction;
            vehicle_return_p0 = plan.p0;
            vehicle_return_p1 = plan.p1;
            vehicle_return_p2 = plan.p2;
            vehicle_return_progress = 0.0;
            return_speed_mps = 0.0;
            return_yaw_speed_rad_s = 0.0;
            vehicle_return_path_initialized = true;
        }

        constexpr double return_acceleration_mps2 = 0.55;
        constexpr double maximum_return_speed_mps = 0.9;
        const double remaining = estimate_return_curve_length(
            vehicle_return_p0, vehicle_return_p1,
            vehicle_return_p2, vehicle_return_progress);
        const double desired_speed = std::min(maximum_return_speed_mps,
            std::sqrt(std::max(0.0, 2.0 * return_acceleration_mps2 * remaining)));
        return_speed_mps += std::clamp(
            desired_speed - return_speed_mps,
            -return_acceleration_mps2 * dt, return_acceleration_mps2 * dt);

        const NpcReturnCurvePlan curve{
            vehicle_return_p0, vehicle_return_p1,
            vehicle_return_p2, vehicle_return_direction, true};
        const auto tangent = npc_return_curve_tangent(
            curve, vehicle_return_progress);
        const double tangent_length = std::max(1.0e-6,
            std::hypot(tangent.east_m, tangent.north_m));
        double candidate_progress = std::min(1.0,
            vehicle_return_progress
                + return_speed_mps * dt / tangent_length);

        constexpr double maximum_yaw_rate_rad_s =
            22.0 * std::numbers::pi / 180.0;
        constexpr double maximum_yaw_acceleration_rad_s2 =
            360.0 * std::numbers::pi / 180.0;
        const double old_heading = nominal_heading_rad + heading_offset_rad;
        const auto step = choose_bounded_npc_return_step(curve, {
            offset_enu_m,
            vehicle_return_progress,
            candidate_progress,
            old_heading,
            return_yaw_speed_rad_s,
            dt,
            return_speed_mps * dt,
            maximum_yaw_rate_rad_s,
            maximum_yaw_acceleration_rad_s2});
        if (!step.accepted) {
            // All twelve candidates failed a real movement/yaw postcondition.
            // Do not apply the last invalid candidate: hold and plan again from
            // this exact pose on the following tick.
            return_speed_mps = 0.0;
            return_yaw_speed_rad_s = 0.0;
            reset_return_path();
            return;
        }
        candidate_progress = step.progress;
        return_yaw_speed_rad_s = step.yaw_speed_rad_s;
        offset_enu_m = step.position;
        heading_offset_rad = std::remainder(
            step.body_heading_rad - nominal_heading_rad,
            2.0 * std::numbers::pi);
        vehicle_return_progress = step.progress;
        if (candidate_progress >= 1.0 - 1.0e-12) {
            offset_enu_m = {};
            heading_offset_rad = 0.0;
            velocity_enu_mps = {};
            heading_rate_rad_s = 0.0;
            return_speed_mps = 0.0;
            return_yaw_speed_rad_s = 0.0;
            reset_return_path();
        }
        return;
    }
    // A bounded, accelerating low-speed recovery replaces exponential snapback.
    // Driving handles only sub-threshold contact corrections with the same cap.
    const double distance = std::hypot(offset_enu_m.east_m, offset_enu_m.north_m);
    constexpr double acceleration = 0.25;
    return_speed_mps = std::min({0.35,
        return_speed_mps + acceleration * dt, std::sqrt(2.0 * acceleration * distance)});
    const double step = std::min(distance, return_speed_mps * dt);
    if (distance > 0.0) {
        const double scale = (distance - step) / distance;
        offset_enu_m.east_m *= scale;
        offset_enu_m.north_m *= scale;
    }
    constexpr double angular_acceleration = 5.0 * std::numbers::pi / 180.0;
    const double angle = std::abs(heading_offset_rad);
    return_yaw_speed_rad_s = std::min({8.0 * std::numbers::pi / 180.0,
        return_yaw_speed_rad_s + angular_acceleration * dt,
        std::sqrt(2.0 * angular_acceleration * angle)});
    heading_offset_rad -= std::copysign(
        std::min(angle, return_yaw_speed_rad_s * dt), heading_offset_rad);
}

void RuntimeCollisionReaction::finish_recovery()
{
    if (recovery.phase() == ImpactRecoveryPhase::Recovering
        && offset_enu_m.east_m == 0.0 && offset_enu_m.north_m == 0.0
        && heading_offset_rad == 0.0
        && velocity_enu_mps.east_m == 0.0 && velocity_enu_mps.north_m == 0.0
        && heading_rate_rad_s == 0.0) {
        recovery.finish_recovery();
        return_speed_mps = 0.0;
        return_yaw_speed_rad_s = 0.0;
        return_departure_grace_remaining_s = 0.0;
        reset_return_path();
    }
}

} // namespace simcore_host
