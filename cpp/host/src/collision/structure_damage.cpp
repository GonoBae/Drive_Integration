#include "collision/structure_damage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_set>

namespace simcore_host {
namespace {

constexpr double epsilon = 1.0e-9;
constexpr double maximum_coordinate_m = 1000000.0;
constexpr double maximum_contact_impulse_n_s = 1000000.0;
constexpr double quarter_turn = std::numbers::pi / 2.0;
constexpr std::size_t maximum_window_intervals = 4096;

bool finite_coordinate(double value)
{
    return std::isfinite(value) && std::abs(value) <= maximum_coordinate_m;
}

bool finite_point(const GroundPointEnu& point)
{
    return finite_coordinate(point.east_m) && finite_coordinate(point.north_m)
        && finite_coordinate(point.up_m);
}

bool valid_id(const std::string& id)
{
    return !id.empty() && id.size() <= 128
        && std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        });
}

double heading(double radians)
{
    return std::remainder(radians, 2.0 * std::numbers::pi);
}

void validate_shape(const ObbPrism& shape)
{
    if (!finite_coordinate(shape.center_enu.east_m)
        || !finite_coordinate(shape.center_enu.north_m)
        || !finite_coordinate(shape.center_up_m) || !std::isfinite(shape.heading_rad)
        || !std::isfinite(shape.half_length_m) || shape.half_length_m <= 0.0
        || shape.half_length_m > maximum_coordinate_m
        || !std::isfinite(shape.half_width_m) || shape.half_width_m <= 0.0
        || shape.half_width_m > maximum_coordinate_m
        || !std::isfinite(shape.half_height_m) || shape.half_height_m <= 0.0
        || shape.half_height_m > maximum_coordinate_m
        || !finite_coordinate(shape.center_up_m - shape.half_height_m)
        || !finite_coordinate(shape.center_up_m + shape.half_height_m)) {
        throw std::runtime_error("structure damage target has invalid OBB geometry");
    }
}

void project_to_facade(const ObbPrism& shape, GroundPointEnu& point, GroundPointEnu& normal)
{
    const CollisionVector2 forward{std::sin(shape.heading_rad), std::cos(shape.heading_rad)};
    const CollisionVector2 right{forward.north_m, -forward.east_m};
    const double east = point.east_m - shape.center_enu.east_m;
    const double north = point.north_m - shape.center_enu.north_m;
    double local_forward = std::clamp(east * forward.east_m + north * forward.north_m,
        -shape.half_length_m, shape.half_length_m);
    double local_right = std::clamp(east * right.east_m + north * right.north_m,
        -shape.half_width_m, shape.half_width_m);
    const double normal_forward = normal.east_m * forward.east_m + normal.north_m * forward.north_m;
    const double normal_right = normal.east_m * right.east_m + normal.north_m * right.north_m;
    // The solver's body support point can lie inside the obstacle, and SAT's
    // separating normal can come from a body axis. Trace outward in that
    // direction to the first actual OBB facade, then use that facade's normal.
    const double forward_sign = normal_forward >= 0.0 ? 1.0 : -1.0;
    const double right_sign = normal_right >= 0.0 ? 1.0 : -1.0;
    const double forward_distance = std::abs(normal_forward) > epsilon
        ? std::max(0.0, (forward_sign * shape.half_length_m - local_forward) / normal_forward)
        : std::numeric_limits<double>::infinity();
    const double right_distance = std::abs(normal_right) > epsilon
        ? std::max(0.0, (right_sign * shape.half_width_m - local_right) / normal_right)
        : std::numeric_limits<double>::infinity();
    const bool forward_face = forward_distance < right_distance - epsilon
        || (std::abs(forward_distance - right_distance) <= epsilon
            && std::abs(normal_forward) >= std::abs(normal_right));
    if (forward_face) {
        local_forward = forward_sign * shape.half_length_m;
        local_right = std::clamp(local_right + forward_distance * normal_right,
            -shape.half_width_m, shape.half_width_m);
        normal = {forward_sign * forward.east_m, forward_sign * forward.north_m, 0.0};
    } else {
        local_right = right_sign * shape.half_width_m;
        local_forward = std::clamp(local_forward + right_distance * normal_forward,
            -shape.half_length_m, shape.half_length_m);
        normal = {right_sign * right.east_m, right_sign * right.north_m, 0.0};
    }
    point.east_m = shape.center_enu.east_m
        + local_forward * forward.east_m + local_right * right.east_m;
    point.north_m = shape.center_enu.north_m
        + local_forward * forward.north_m + local_right * right.north_m;
}

void contact_patch(const ObbPrism& wall, const ObbPrism* body, double penetration,
    GroundPointEnu& point, const GroundPointEnu& normal, double& half_width, double& half_height)
{
    // The previous point-only policy drew the same radial stamp for a bumper,
    // a side panel and a grazing corner. Clip the actual vehicle support band
    // against the impacted facade and use its horizontal contact footprint.
    half_width = 0.35;
    half_height = 0.30;
    if (!body) return;
    try { validate_shape(*body); }
    catch (const std::runtime_error&) { return; }
    const CollisionVector2 tangent{-normal.north_m, normal.east_m};
    const CollisionVector2 forward{std::sin(body->heading_rad), std::cos(body->heading_rad)};
    const CollisionVector2 right{forward.north_m, -forward.east_m};
    std::array<CollisionVector2, 4> corners;
    std::array<double, 4> depth;
    double minimum_depth = std::numeric_limits<double>::infinity();
    constexpr std::array<std::array<double, 2>, 4> signs{{{1, 1}, {1, -1}, {-1, -1}, {-1, 1}}};
    for (std::size_t index = 0; index < corners.size(); ++index) {
        corners[index] = {
            body->center_enu.east_m + signs[index][0] * body->half_length_m * forward.east_m
                + signs[index][1] * body->half_width_m * right.east_m,
            body->center_enu.north_m + signs[index][0] * body->half_length_m * forward.north_m
                + signs[index][1] * body->half_width_m * right.north_m};
        depth[index] = (corners[index].east_m - point.east_m) * normal.east_m
            + (corners[index].north_m - point.north_m) * normal.north_m;
        minimum_depth = std::min(minimum_depth, depth[index]);
    }
    const double support_band = 0.08 + (std::isfinite(penetration)
        ? std::clamp(penetration, 0.0, 0.25) : 0.0);
    const double cut_depth = minimum_depth + support_band;
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    auto include = [&](const CollisionVector2& corner) {
        const double along = (corner.east_m - wall.center_enu.east_m) * tangent.east_m
            + (corner.north_m - wall.center_enu.north_m) * tangent.north_m;
        low = std::min(low, along);
        high = std::max(high, along);
    };
    for (std::size_t index = 0; index < corners.size(); ++index) {
        const std::size_t next = (index + 1) % corners.size();
        if (depth[index] <= cut_depth) include(corners[index]);
        if ((depth[index] < cut_depth) != (depth[next] < cut_depth)) {
            const double fraction = (cut_depth - depth[index]) / (depth[next] - depth[index]);
            include({corners[index].east_m + fraction * (corners[next].east_m - corners[index].east_m),
                corners[index].north_m + fraction * (corners[next].north_m - corners[index].north_m)});
        }
    }
    const CollisionVector2 wall_forward{std::sin(wall.heading_rad), std::cos(wall.heading_rad)};
    const CollisionVector2 wall_right{wall_forward.north_m, -wall_forward.east_m};
    const double wall_extent = wall.half_length_m * std::abs(tangent.east_m * wall_forward.east_m
        + tangent.north_m * wall_forward.north_m)
        + wall.half_width_m * std::abs(tangent.east_m * wall_right.east_m + tangent.north_m * wall_right.north_m);
    low = std::clamp(low, -wall_extent, wall_extent);
    high = std::clamp(high, -wall_extent, wall_extent);
    const double centre = 0.5 * (low + high);
    const double old_centre = (point.east_m - wall.center_enu.east_m) * tangent.east_m
        + (point.north_m - wall.center_enu.north_m) * tangent.north_m;
    point.east_m += (centre - old_centre) * tangent.east_m;
    point.north_m += (centre - old_centre) * tangent.north_m;
    half_width = std::clamp(0.5 * (high - low), 0.03, 3.0);
    const double bottom = std::max(wall.center_up_m - wall.half_height_m,
        body->center_up_m - body->half_height_m);
    const double top = std::min(wall.center_up_m + wall.half_height_m,
        body->center_up_m + body->half_height_m);
    if (top > bottom) {
        point.up_m = 0.5 * (bottom + top);
        half_height = std::clamp(0.5 * (top - bottom), 0.03, 3.0);
    }
}

} // namespace

void StructureDamageRuntime::rebuild(
    const std::vector<StaticObbCollider>& colliders, const TrafficNetwork* traffic)
{
    std::vector<Target> next;
    std::unordered_set<std::string> used_ids;
    for (const auto& collider : colliders) {
        if (!valid_id(collider.collider_id) || collider.collider_id.starts_with("signal-pole-")
            || !used_ids.insert(collider.collider_id).second) {
            throw std::runtime_error("structure damage target has duplicate or reserved collider ID");
        }
        if (collider.semantic != StaticColliderSemantic::Wall) { continue; }
        validate_shape(collider.shape);
        Target target;
        target.state.collider_id = collider.collider_id;
        target.state.kind = StructureKind::Building;
        target.state.base_position_enu = {collider.shape.center_enu.east_m,
            collider.shape.center_enu.north_m,
            collider.shape.center_up_m - collider.shape.half_height_m};
        target.state.heading_rad = heading(collider.shape.heading_rad);
        target.authored_heading_rad = target.state.heading_rad;
        target.authored_base_position = target.state.base_position_enu;
        target.building_shape = collider.shape;
        target.building_shape.heading_rad = target.authored_heading_rad;
        target.top_up_m = collider.shape.center_up_m + collider.shape.half_height_m;
        next.push_back(std::move(target));
        if (next.size() > maximum_targets) {
            throw std::runtime_error("structure damage target count exceeds 256");
        }
    }
    if (traffic) {
        if (traffic->signals.size() > 32) {
            throw std::runtime_error("structure damage signal count exceeds 32");
        }
        for (const auto& signal : traffic->signals) {
            const std::string id = "signal-pole-" + std::to_string(signal.id);
            if (signal.id == 0 || signal.group_id == 0 || signal.controller_id == 0
                || !used_ids.insert(id).second || !finite_point(signal.position_enu)
                || !finite_coordinate(signal.position_enu.up_m + pole_height_m)
                || !std::isfinite(signal.heading_deg)
                || (signal.kind != TrafficSignalKind::Vehicle
                    && signal.kind != TrafficSignalKind::Pedestrian)) {
                throw std::runtime_error("structure damage target has invalid/duplicate signal metadata");
            }
            Target target;
            target.state.collider_id = id;
            target.state.kind = StructureKind::SignalPole;
            target.state.signal_id = signal.id;
            target.state.base_position_enu = signal.position_enu;
            target.state.heading_rad = heading(std::remainder(signal.heading_deg, 360.0)
                * std::numbers::pi / 180.0);
            target.state.fall_direction_enu = {std::sin(target.state.heading_rad),
                std::cos(target.state.heading_rad)};
            target.authored_heading_rad = target.state.heading_rad;
            target.authored_base_position = target.state.base_position_enu;
            target.controller_id = signal.controller_id;
            target.top_up_m = signal.position_enu.up_m + pole_height_m;
            next.push_back(std::move(target));
            if (next.size() > maximum_targets) {
                throw std::runtime_error("structure damage target count exceeds 256");
            }
        }
    }
    std::sort(next.begin(), next.end(), [](const Target& left, const Target& right) {
        return left.state.collider_id < right.state.collider_id;
    });
    targets_ = std::move(next);
    motion_enabled_ = true;
    refresh_caches();
}

void StructureDamageRuntime::reset()
{
    motion_enabled_ = true;
    for (auto& target : targets_) {
        const auto original = target;
        target = {};
        target.state.collider_id = original.state.collider_id;
        target.state.kind = original.state.kind;
        target.state.signal_id = original.state.signal_id;
        target.state.base_position_enu = original.authored_base_position;
        target.state.heading_rad = original.authored_heading_rad;
        target.state.fall_direction_enu = {std::sin(target.state.heading_rad),
            std::cos(target.state.heading_rad)};
        target.authored_heading_rad = original.authored_heading_rad;
        target.authored_base_position = original.authored_base_position;
        target.building_shape = original.building_shape;
        target.top_up_m = original.top_up_m;
        target.controller_id = original.controller_id;
    }
    refresh_caches();
}

void StructureDamageRuntime::record_contacts(
    const std::vector<CollisionContact>& contacts, double impact_height_m, const ObbPrism* impacting_body)
{
    if (!finite_coordinate(impact_height_m)) { return; }
    for (const auto& contact : contacts) {
        if (!std::isfinite(contact.accumulated_normal_impulse_n_s)
            || contact.accumulated_normal_impulse_n_s <= 0.0
            || !finite_coordinate(contact.contact_point_enu.east_m)
            || !finite_coordinate(contact.contact_point_enu.north_m)
            || !std::isfinite(contact.normal_enu.east_m)
            || !std::isfinite(contact.normal_enu.north_m)) { continue; }
        const double normal_length = std::hypot(contact.normal_enu.east_m, contact.normal_enu.north_m);
        if (!std::isfinite(normal_length) || normal_length <= epsilon) { continue; }
        auto found = std::lower_bound(targets_.begin(), targets_.end(), contact.collider_id,
            [](const Target& target, const std::string& id) { return target.state.collider_id < id; });
        if (found == targets_.end() || found->state.collider_id != contact.collider_id
            || (found->state.kind == StructureKind::SignalPole && found->state.disabled)) { continue; }
        const double impulse = std::min(contact.accumulated_normal_impulse_n_s,
            maximum_contact_impulse_n_s);
        found->pending_impulse_n_s = std::min(maximum_contact_impulse_n_s,
            found->pending_impulse_n_s + impulse);
        if (impulse >= found->pending_peak_impulse_n_s) {
            found->pending_peak_impulse_n_s = impulse;
            found->pending_point = {contact.contact_point_enu.east_m, contact.contact_point_enu.north_m,
                std::clamp(impact_height_m, found->state.base_position_enu.up_m, found->top_up_m)};
            found->pending_normal = {contact.normal_enu.east_m / normal_length,
                contact.normal_enu.north_m / normal_length, 0.0};
            if (found->state.kind == StructureKind::Building) {
                project_to_facade(found->building_shape, found->pending_point, found->pending_normal);
                contact_patch(found->building_shape, impacting_body, contact.maximum_penetration_m,
                    found->pending_point, found->pending_normal,
                    found->pending_half_width_m, found->pending_half_height_m);
            }
        }
    }
}

void StructureDamageRuntime::accept_resolved_proxies(const std::vector<KinematicCollisionProxy>& proxies)
{
    for (auto& target : targets_) {
        if (target.state.kind != StructureKind::SignalPole) continue;
        const auto resolved = std::find_if(proxies.begin(), proxies.end(), [&](const auto& p) {
            return p.proxy_id == target.state.collider_id;
        });
        if (resolved == proxies.end() || !resolved->breakaway_released) continue;
        const auto previous = std::find_if(proxies_.begin(), proxies_.end(), [&](const auto& p) {
            return p.proxy_id == target.state.collider_id;
        });
        const auto* shape = std::get_if<ObbPrism>(&resolved->shape);
        if (previous == proxies_.end() || !shape) continue;
        const auto& old = std::get<ObbPrism>(previous->shape);
        if (!finite_coordinate(shape->center_enu.east_m) || !finite_coordinate(shape->center_enu.north_m)
            || !std::isfinite(shape->heading_rad)
            || !std::isfinite(resolved->linear_velocity_enu_mps.east_m)
            || !std::isfinite(resolved->linear_velocity_enu_mps.north_m)
            || !std::isfinite(resolved->heading_rate_rad_s)) continue;
        const double yaw = heading(shape->heading_rad - old.heading_rad);
        const auto old_direction = target.state.fall_direction_enu;
        target.state.fall_direction_enu = {
            old_direction.east_m * std::cos(yaw) + old_direction.north_m * std::sin(yaw),
            old_direction.north_m * std::cos(yaw) - old_direction.east_m * std::sin(yaw)};
        target.state.heading_rad = heading(target.state.heading_rad + yaw);
        // Rotation is around the prism centre, so reconstruct the translated
        // base from that centre and the newly rotated hinge direction.
        const double reach = 0.5 * pole_height_m * std::sin(target.state.fall_angle_rad);
        target.state.base_position_enu.east_m = shape->center_enu.east_m
            - target.state.fall_direction_enu.east_m * reach;
        target.state.base_position_enu.north_m = shape->center_enu.north_m
            - target.state.fall_direction_enu.north_m * reach;
        target.breakaway_released = true;
        target.linear_velocity_enu_mps = resolved->linear_velocity_enu_mps;
        target.heading_rate_rad_s = resolved->heading_rate_rad_s;
    }
    refresh_caches();
}

void StructureDamageRuntime::update_window(Target& target, double dt, double impulse)
{
    if (dt == 0.0 && impulse == 0.0) { return; }
    if (dt == 0.0 && !target.window.empty() && target.window.back().duration_seconds == 0.0) {
        target.window.back().impulse_n_s += impulse;
    } else {
        target.window.push_back({dt, impulse});
    }
    target.window_duration_seconds += dt;
    target.window_impulse_n_s += impulse;
    double excess = std::max(0.0, target.window_duration_seconds - contact_window_seconds);
    while (!target.window.empty() && excess > epsilon) {
        auto& oldest = target.window.front();
        if (oldest.duration_seconds <= excess + epsilon) {
            excess = std::max(0.0, excess - oldest.duration_seconds);
            target.window_duration_seconds -= oldest.duration_seconds;
            target.window_impulse_n_s -= oldest.impulse_n_s;
            target.window.pop_front();
        } else {
            const double removed = oldest.impulse_n_s * excess / oldest.duration_seconds;
            oldest.duration_seconds -= excess;
            oldest.impulse_n_s -= removed;
            target.window_duration_seconds -= excess;
            target.window_impulse_n_s -= removed;
            excess = 0.0;
        }
    }
    // Even an unreasonable sub-microsecond caller cannot grow history forever.
    while (target.window.size() > maximum_window_intervals) {
        const auto oldest = target.window.front();
        target.window.pop_front();
        target.window.front().duration_seconds += oldest.duration_seconds;
        target.window.front().impulse_n_s += oldest.impulse_n_s;
    }
    target.window_impulse_n_s = std::max(0.0, target.window_impulse_n_s);
}

void StructureDamageRuntime::tick(double dt_seconds, bool enabled)
{
    if (!enabled || !std::isfinite(dt_seconds) || dt_seconds < 0.0 || dt_seconds > 10.0) {
        motion_enabled_ = false;
        for (auto& target : targets_) {
            target.pending_impulse_n_s = 0.0;
            target.pending_peak_impulse_n_s = 0.0;
        }
        refresh_caches();
        return;
    }
    motion_enabled_ = true;
    for (auto& target : targets_) {
        const double impulse = target.pending_impulse_n_s;
        target.pending_impulse_n_s = 0.0;
        target.pending_peak_impulse_n_s = 0.0;
        update_window(target, dt_seconds, impulse);
        if (impulse > 0.0) {
            target.quiet_seconds = 0.0;
            target.episode_peak_impulse_n_s = std::max({target.episode_peak_impulse_n_s,
                target.window_impulse_n_s, impulse});
            const bool pole = target.state.kind == StructureKind::SignalPole;
            const double onset = pole ? pole_damage_onset_impulse_n_s : building_damage_onset_impulse_n_s;
            const double full = pole ? pole_collapse_impulse_n_s : building_full_damage_impulse_n_s;
            const double episode_damage = 100.0 * std::clamp(
                (target.episode_peak_impulse_n_s - onset) / (full - onset), 0.0, 1.0);
            const double added = std::max(0.0, episode_damage - target.episode_damage_percent);
            if (added > epsilon && (!pole || target.state.damage_percent < 100.0)) {
                target.state.damage_percent = std::min(100.0, target.state.damage_percent + added);
                target.state.impact_point_enu = target.pending_point;
                target.state.impact_normal_enu = target.pending_normal;
                if (!pole) {
                    target.state.impact_half_width_m = target.pending_half_width_m;
                    target.state.impact_half_height_m = target.pending_half_height_m;
                    target.state.impact_severity = episode_damage / 100.0;
                }
                if (!target.episode_has_event) {
                    if (target.state.event_sequence < std::numeric_limits<std::uint32_t>::max()) {
                        ++target.state.event_sequence;
                    }
                    target.episode_has_event = true;
                }
                if (pole && target.state.damage_percent + epsilon >= 100.0) {
                    target.state.damage_percent = 100.0;
                    target.state.disabled = true;
                    target.breakaway_released = true;
                    target.state.fall_direction_enu = {-target.pending_normal.east_m,
                        -target.pending_normal.north_m};
                    // J*h / I for a uniform 180kg pole hinged at its base.
                    constexpr double inertia = pole_mass_kg * pole_height_m * pole_height_m / 3.0;
                    const double arm = std::clamp(target.pending_point.up_m
                        - target.state.base_position_enu.up_m, 0.25, pole_height_m);
                    target.fall_rate_rad_s = std::clamp(target.episode_peak_impulse_n_s * arm / inertia,
                        0.35, 3.0);
                }
            }
            target.episode_damage_percent = episode_damage;
        } else {
            target.quiet_seconds = std::min(contact_window_seconds, target.quiet_seconds + dt_seconds);
            if (target.quiet_seconds + epsilon >= contact_window_seconds) {
                target.episode_peak_impulse_n_s = 0.0;
                target.episode_damage_percent = 0.0;
                target.episode_has_event = false;
            }
        }
        if (target.breakaway_released && dt_seconds > 0.0) {
            const double speed = std::hypot(target.linear_velocity_enu_mps.east_m,
                target.linear_velocity_enu_mps.north_m);
            const double next_speed = std::max(0.0, speed - 0.45 * 9.81 * dt_seconds);
            if (speed > epsilon) {
                target.linear_velocity_enu_mps.east_m *= next_speed / speed;
                target.linear_velocity_enu_mps.north_m *= next_speed / speed;
            }
            target.heading_rate_rad_s *= std::exp(-3.0 * dt_seconds);
        }
        if (target.state.kind == StructureKind::SignalPole && target.state.disabled
            && target.state.fall_angle_rad < quarter_turn && dt_seconds > 0.0) {
            const int steps = std::max(1, static_cast<int>(std::ceil(dt_seconds * 240.0)));
            const double step = dt_seconds / steps;
            for (int index = 0; index < steps; ++index) {
                const double acceleration = 3.0 * 9.81 / (2.0 * pole_height_m)
                    * std::sin(target.state.fall_angle_rad) - 0.4 * target.fall_rate_rad_s;
                target.fall_rate_rad_s = std::clamp(target.fall_rate_rad_s + acceleration * step, 0.0, 5.0);
                target.state.fall_angle_rad = std::min(quarter_turn,
                    target.state.fall_angle_rad + target.fall_rate_rad_s * step);
                if (target.state.fall_angle_rad >= quarter_turn) {
                    target.fall_rate_rad_s = 0.0;
                    break;
                }
            }
        }
    }
    refresh_caches();
}

void StructureDamageRuntime::refresh_caches()
{
    snapshots_.clear();
    proxies_.clear();
    for (const auto& target : targets_) {
        const auto& state = target.state;
        if (state.damage_percent > 0.0) { snapshots_.push_back(state); }
        if (state.kind != StructureKind::SignalPole) { continue; }
        const double sine = std::sin(state.fall_angle_rad);
        const double cosine = std::max(0.0, std::cos(state.fall_angle_rad));
        constexpr double half_height = pole_height_m / 2.0;
        const double reach = half_height * sine;
        const double lift = pole_half_width_m * sine;
        ObbPrism shape;
        shape.center_enu = {state.base_position_enu.east_m + state.fall_direction_enu.east_m * reach,
            state.base_position_enu.north_m + state.fall_direction_enu.north_m * reach};
        shape.center_up_m = state.base_position_enu.up_m + half_height * cosine + lift;
        // Visual head facing stays authored. Only the conservative prism's
        // long horizontal axis follows the fall direction.
        shape.heading_rad = state.disabled
            ? heading(std::atan2(state.fall_direction_enu.east_m, state.fall_direction_enu.north_m))
            : state.heading_rad;
        shape.half_length_m = pole_half_width_m * cosine + half_height * sine;
        shape.half_width_m = pole_half_width_m;
        shape.half_height_m = half_height * cosine + pole_half_width_m * sine;
        KinematicCollisionProxy proxy;
        proxy.proxy_id = state.collider_id;
        proxy.shape = shape;
        proxy.material = {0.65, 0.05};
        proxy.mass_kg = pole_mass_kg;
        proxy.yaw_inertia_kg_m2 = pole_mass_kg
            * (shape.half_length_m * shape.half_length_m + shape.half_width_m * shape.half_width_m) / 3.0;
        proxy.maximum_linear_speed_mps = 60.0;
        proxy.breakaway_released = target.breakaway_released;
        if (!target.breakaway_released) {
            // Match the existing episode-aware damage profile. Pressure only
            // spends its current 250ms peak, not an unlimited lifetime budget.
            const double threshold = pole_damage_onset_impulse_n_s
                + (100.0 - state.damage_percent + target.episode_damage_percent) / 100.0
                    * (pole_collapse_impulse_n_s - pole_damage_onset_impulse_n_s);
            proxy.breakaway_impulse_n_s = std::max(1e-6, threshold - target.episode_peak_impulse_n_s);
        }
        proxy.linear_velocity_enu_mps = motion_enabled_ ? target.linear_velocity_enu_mps : CollisionVector2{};
        proxy.heading_rate_rad_s = motion_enabled_ ? target.heading_rate_rad_s : 0.0;
        proxy.tire_support_candidate = state.disabled && state.fall_angle_rad >= 80.0 * std::numbers::pi / 180.0;
        proxies_.push_back(std::move(proxy));
    }
}

void StructureDamageRuntime::apply_signal_faults(std::vector<TrafficSignalSnapshot>& signals) const
{
    for (auto& signal : signals) {
        for (const auto& target : targets_) {
            if (target.state.kind != StructureKind::SignalPole || !target.state.disabled) { continue; }
            // A broken physical head does not damage the intersection timer.
            // Other heads keep their evaluated phase; only this lens is dark.
            if (signal.id == target.state.signal_id) {
                signal.aspect = SignalAspect::Red;
                signal.remaining_seconds = 0.0;
                signal.out_of_service = true;
            }
        }
    }
}

} // namespace simcore_host
