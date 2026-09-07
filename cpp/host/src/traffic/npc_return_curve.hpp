#pragma once

#include "collision/collision_types.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace simcore_host {

// A cubic return curve has an implicit final control point at the authored
// route origin. The two explicit handles preserve the selected gear direction
// at both ends of the manoeuvre.
struct NpcReturnCurvePlan {
    CollisionVector2 p0;
    CollisionVector2 p1;
    CollisionVector2 p2;
    int direction = 0;
    bool valid = false;
};

inline CollisionVector2 npc_return_curve_point(
    const NpcReturnCurvePlan& curve, double progress)
{
    const double t = std::clamp(progress, 0.0, 1.0);
    const double u = 1.0 - t;
    return {
        u * u * u * curve.p0.east_m + 3.0 * u * u * t * curve.p1.east_m
            + 3.0 * u * t * t * curve.p2.east_m,
        u * u * u * curve.p0.north_m + 3.0 * u * u * t * curve.p1.north_m
            + 3.0 * u * t * t * curve.p2.north_m};
}

inline CollisionVector2 npc_return_curve_tangent(
    const NpcReturnCurvePlan& curve, double progress)
{
    const double t = std::clamp(progress, 0.0, 1.0);
    const double u = 1.0 - t;
    return {
        3.0 * u * u * (curve.p1.east_m - curve.p0.east_m)
            + 6.0 * u * t * (curve.p2.east_m - curve.p1.east_m)
            - 3.0 * t * t * curve.p2.east_m,
        3.0 * u * u * (curve.p1.north_m - curve.p0.north_m)
            + 6.0 * u * t * (curve.p2.north_m - curve.p1.north_m)
            - 3.0 * t * t * curve.p2.north_m};
}

inline NpcReturnCurvePlan make_npc_return_curve_plan(
    CollisionVector2 offset_enu_m, double actual_heading_rad,
    double nominal_heading_rad)
{
    const double distance = std::hypot(offset_enu_m.east_m, offset_enu_m.north_m);
    if (!std::isfinite(distance) || distance <= 1.0e-9
        || !std::isfinite(actual_heading_rad) || !std::isfinite(nominal_heading_rad)) {
        return {};
    }

    const double target_east = -offset_enu_m.east_m;
    const double target_north = -offset_enu_m.north_m;
    const double forward_projection = target_east * std::sin(actual_heading_rad)
        + target_north * std::cos(actual_heading_rad);
    const int direction = forward_projection >= 0.0 ? 1 : -1;
    const double signed_direction = static_cast<double>(direction);
    const CollisionVector2 start_motion{
        signed_direction * std::sin(actual_heading_rad),
        signed_direction * std::cos(actual_heading_rad)};
    const CollisionVector2 finish_motion{
        signed_direction * std::sin(nominal_heading_rad),
        signed_direction * std::cos(nominal_heading_rad)};
    const double target_bearing = std::atan2(target_east, target_north);
    const double approach_angle = std::abs(std::remainder(
        target_bearing - std::atan2(start_motion.east_m, start_motion.north_m),
        2.0 * std::numbers::pi));
    const double heading_error = std::remainder(
        nominal_heading_rad - actual_heading_rad, 2.0 * std::numbers::pi);

    // Handles must scale down with the remaining chord. The former absolute
    // 0.20 m minimum made a 5--15 cm aligned correction overshoot its endpoint,
    // form a cusp, and then appear to teleport on the final tick.
    const double turn_weight = std::clamp(
        0.65 * std::abs(std::sin(approach_angle))
            + 0.40 * std::abs(std::sin(heading_error)),
        0.0, 1.0);
    constexpr double straight_handle_ratio = 1.0 / 3.0;
    constexpr double maximum_handle_ratio = 0.45;
    const double handle = std::min(3.0, distance * std::lerp(
        straight_handle_ratio, maximum_handle_ratio, turn_weight));
    if (!std::isfinite(handle) || handle <= 1.0e-9) return {};

    return {
        offset_enu_m,
        {offset_enu_m.east_m + start_motion.east_m * handle,
            offset_enu_m.north_m + start_motion.north_m * handle},
        {-finish_motion.east_m * handle, -finish_motion.north_m * handle},
        direction,
        true};
}

struct NpcReturnStepRequest {
    CollisionVector2 current_position;
    double current_progress = 0.0;
    double requested_progress = 0.0;
    double current_body_heading_rad = 0.0;
    double current_yaw_speed_rad_s = 0.0;
    double dt_seconds = 0.0;
    double maximum_chord_m = 0.0;
    double maximum_yaw_rate_rad_s = 0.0;
    double maximum_yaw_acceleration_rad_s2 = 0.0;
};

struct NpcReturnStepResult {
    bool accepted = false;
    double progress = 0.0;
    CollisionVector2 position;
    double body_heading_rad = 0.0;
    double yaw_speed_rad_s = 0.0;
};

inline NpcReturnStepResult choose_bounded_npc_return_step(
    const NpcReturnCurvePlan& curve, const NpcReturnStepRequest& request)
{
    NpcReturnStepResult result;
    result.progress = request.current_progress;
    result.position = request.current_position;
    result.body_heading_rad = request.current_body_heading_rad;
    result.yaw_speed_rad_s = request.current_yaw_speed_rad_s;
    if (!curve.valid || curve.direction == 0
        || !std::isfinite(request.dt_seconds) || request.dt_seconds <= 0.0
        || !std::isfinite(request.maximum_chord_m) || request.maximum_chord_m < 0.0
        || !std::isfinite(request.maximum_yaw_rate_rad_s)
        || !std::isfinite(request.maximum_yaw_acceleration_rad_s2)) {
        return result;
    }

    const double begin = std::clamp(request.current_progress, 0.0, 1.0);
    double candidate = std::clamp(request.requested_progress, begin, 1.0);
    constexpr double tangent_epsilon = 1.0e-9;
    constexpr double condition_epsilon = 1.0e-9;
    const auto nonzero_tangent = [&](double progress) {
        const auto tangent = npc_return_curve_tangent(curve, progress);
        return std::isfinite(tangent.east_m) && std::isfinite(tangent.north_m)
            && std::hypot(tangent.east_m, tangent.north_m) > tangent_epsilon;
    };
    if (candidate <= begin || !nonzero_tangent(begin)) return result;

    for (int iteration = 0; iteration < 12; ++iteration) {
        const auto position = npc_return_curve_point(curve, candidate);
        const auto tangent = npc_return_curve_tangent(curve, candidate);
        const double tangent_length = std::hypot(tangent.east_m, tangent.north_m);
        const double midpoint = 0.5 * (begin + candidate);
        const double chord = std::hypot(position.east_m - request.current_position.east_m,
            position.north_m - request.current_position.north_m);
        const double motion_heading = std::atan2(tangent.east_m, tangent.north_m);
        const double body_heading = std::remainder(motion_heading
            + (curve.direction < 0 ? std::numbers::pi : 0.0),
            2.0 * std::numbers::pi);
        const double yaw_step = std::remainder(
            body_heading - request.current_body_heading_rad,
            2.0 * std::numbers::pi);
        const double yaw_speed = yaw_step / request.dt_seconds;
        const bool valid = std::isfinite(position.east_m)
            && std::isfinite(position.north_m)
            && std::isfinite(tangent_length) && tangent_length > tangent_epsilon
            && nonzero_tangent(midpoint)
            && chord <= request.maximum_chord_m + condition_epsilon
            && std::isfinite(body_heading) && std::isfinite(yaw_speed)
            && std::abs(yaw_step)
                <= request.maximum_yaw_rate_rad_s * request.dt_seconds
                    + condition_epsilon
            && std::abs(yaw_speed - request.current_yaw_speed_rad_s)
                <= request.maximum_yaw_acceleration_rad_s2 * request.dt_seconds
                    + condition_epsilon;
        if (valid) {
            result.accepted = true;
            result.progress = candidate;
            result.position = position;
            result.body_heading_rad = body_heading;
            result.yaw_speed_rad_s = yaw_speed;
            return result;
        }
        candidate = 0.5 * (begin + candidate);
    }

    // Returning the unchanged state is intentional. The caller can discard
    // this curve and replan next tick; it must never apply the twelfth invalid
    // candidate merely because the reduction budget was exhausted.
    return result;
}

} // namespace simcore_host
