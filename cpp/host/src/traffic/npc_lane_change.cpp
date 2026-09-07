#include "traffic/npc_lane_change.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>

namespace simcore_host {
namespace {

constexpr double epsilon = 1.0e-7;
constexpr double sample_spacing_m = 0.5;
constexpr double minimum_direction_dot = 0.984807753012208; // ten degrees

bool finite(const GroundPointEnu& p)
{
    return std::isfinite(p.east_m) && std::isfinite(p.north_m)
        && std::isfinite(p.up_m);
}

GroundPointEnu add(const GroundPointEnu& a, const GroundPointEnu& b)
{
    return {a.east_m + b.east_m, a.north_m + b.north_m, a.up_m + b.up_m};
}

GroundPointEnu subtract(const GroundPointEnu& a, const GroundPointEnu& b)
{
    return {a.east_m - b.east_m, a.north_m - b.north_m, a.up_m - b.up_m};
}

GroundPointEnu scale(const GroundPointEnu& p, double factor)
{
    return {p.east_m * factor, p.north_m * factor, p.up_m * factor};
}

double dot(const GroundPointEnu& a, const GroundPointEnu& b)
{
    return a.east_m * b.east_m + a.north_m * b.north_m + a.up_m * b.up_m;
}

double length(const GroundPointEnu& p)
{
    return std::hypot(std::hypot(p.east_m, p.north_m), p.up_m);
}

std::optional<std::vector<double>> stations(
    const TrafficLane& lane, double minimum_half_width_m = 1.0)
{
    if (lane.points.size() < 2 || lane.points.size() > 2048
        || !std::isfinite(minimum_half_width_m) || minimum_half_width_m <= 0.0
        || !std::isfinite(lane.width_m) || lane.width_m < 2.0 * minimum_half_width_m
        || lane.width_m > 8.0) {
        return std::nullopt;
    }
    std::vector<double> result{0.0};
    for (std::size_t i = 0; i < lane.points.size(); ++i) {
        if (!finite(lane.points[i])) { return std::nullopt; }
        if (i == 0) { continue; }
        const auto delta = subtract(lane.points[i], lane.points[i - 1]);
        const double segment = length(delta);
        if (segment < 0.05 || segment > 100.0
            || std::hypot(delta.east_m, delta.north_m) < epsilon) {
            return std::nullopt;
        }
        result.push_back(result.back() + segment);
    }
    return result;
}

struct CurveSample {
    GroundPointEnu position;
    GroundPointEnu tangent; // d(position) / d(3D station)
};

std::optional<CurveSample> interpolate(
    const std::vector<GroundPointEnu>& points,
    const std::vector<double>& offsets, double station)
{
    if (!std::isfinite(station) || offsets.size() < 2
        || points.size() != offsets.size() || station < -epsilon
        || station > offsets.back() + epsilon) {
        return std::nullopt;
    }
    station = std::clamp(station, 0.0, offsets.back());
    const auto upper = std::upper_bound(offsets.begin(), offsets.end(), station);
    const auto index = std::min<std::size_t>(
        static_cast<std::size_t>(std::distance(offsets.begin(), upper)), offsets.size() - 1);
    const double span = offsets[index] - offsets[index - 1];
    const auto tangent = scale(subtract(points[index], points[index - 1]), 1.0 / span);
    return CurveSample{
        add(points[index - 1], scale(tangent, station - offsets[index - 1])), tangent};
}

std::optional<GroundPointEnu> supported(const GroundQuery& ground, const GroundPointEnu& p,
                                       double maximum_height_difference_m = 0.2)
{
    if (!finite(p)) { return std::nullopt; }
    try {
        const auto hit = ground.query_down({{p.east_m, p.north_m, p.up_m + 2.0}, 4.0});
        if (!hit || !finite(hit->point_enu) || !finite(hit->normal_enu)
            || !std::isfinite(hit->distance_m) || hit->distance_m < 0.0
            || hit->distance_m > 4.0 || hit->normal_enu.up_m < 0.5
            || std::abs(hit->point_enu.east_m - p.east_m) > 0.01
            || std::abs(hit->point_enu.north_m - p.north_m) > 0.01
            || std::abs(hit->point_enu.up_m - p.up_m) > maximum_height_difference_m) {
            return std::nullopt;
        }
        return hit->point_enu;
    } catch (...) { return std::nullopt; }
}

bool footprint_supported(const GroundQuery& ground, const NpcLaneSample& sample,
    double half_length_m, double half_width_m)
{
    const double radians = sample.heading_deg * std::numbers::pi / 180.0;
    const GroundPointEnu forward{std::sin(radians), std::cos(radians), 0.0};
    const GroundPointEnu side{std::cos(radians), -std::sin(radians), 0.0};
    for (const double longitudinal : {-half_length_m, half_length_m}) {
        for (const double lateral : {-half_width_m, half_width_m}) {
            const auto point = add(sample.position_enu,
                                   add(scale(forward, longitudinal), scale(side, lateral)));
            if (!supported(ground, point, 0.5)) { return false; }
        }
    }
    return true;
}

struct Projection {
    double station = 0.0;
    double lateral_distance = 0.0;
    double vertical_distance = 0.0;
    GroundPointEnu tangent;
};

// Segment endpoint projection is deliberately extrapolated only on the first
// and last segments. Vehicles just before/after the authored lane must still
// count for a rear/front safety gap instead of disappearing at its boundary.
std::optional<Projection> project(
    const std::vector<GroundPointEnu>& points,
    const std::vector<double>& offsets, const GroundPointEnu& position)
{
    if (points.size() < 2 || points.size() != offsets.size()) { return std::nullopt; }
    std::optional<Projection> best;
    double best_distance_squared = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < points.size(); ++i) {
        const auto delta = subtract(points[i], points[i - 1]);
        const double horizontal_squared = delta.east_m * delta.east_m
            + delta.north_m * delta.north_m;
        if (horizontal_squared < epsilon) { return std::nullopt; }
        const auto from_start = subtract(position, points[i - 1]);
        double t = (from_start.east_m * delta.east_m + from_start.north_m * delta.north_m)
            / horizontal_squared;
        if (i != 1) { t = std::max(0.0, t); }
        if (i + 1 != points.size()) { t = std::min(1.0, t); }
        const auto projected = add(points[i - 1], scale(delta, t));
        const auto separation = subtract(position, projected);
        const double distance_squared = separation.east_m * separation.east_m
            + separation.north_m * separation.north_m;
        if (distance_squared >= best_distance_squared) { continue; }
        best_distance_squared = distance_squared;
        const double segment = offsets[i] - offsets[i - 1];
        best = Projection{offsets[i - 1] + t * segment,
                          std::sqrt(distance_squared), std::abs(separation.up_m),
                          scale(delta, 1.0 / segment)};
    }
    return best;
}

} // namespace

bool NpcLaneChangePlan::complete(double source_offset_m) const noexcept
{
    return ground_ != nullptr && std::isfinite(source_offset_m)
        && source_offset_m >= end_m_;
}

std::optional<double> NpcLaneChangePlan::target_offset(double source_offset_m) const
{
    if (!ground_ || source_stations_.empty() || target_stations_.empty()
        || !std::isfinite(source_offset_m) || source_offset_m < begin_m_ - epsilon
        || source_offset_m > source_stations_.back() + epsilon) {
        return std::nullopt;
    }
    const double station = mapping_target_begin_m_
        + (source_offset_m - mapping_source_begin_m_) * mapping_scale_;
    if (!std::isfinite(station) || station < -epsilon
        || station > target_stations_.back() + epsilon) {
        return std::nullopt;
    }
    return std::clamp(station, 0.0, target_stations_.back());
}

std::optional<NpcLaneSample> NpcLaneChangePlan::sample(double source_offset_m) const
{
    const auto target_station = target_offset(source_offset_m);
    if (!target_station) { return std::nullopt; }
    const auto target = interpolate(target_points_, target_stations_, *target_station);
    if (!target) { return std::nullopt; }
    auto position = target->position;
    auto derivative = target->tangent;
    const bool done = complete(source_offset_m);
    if (!done) {
        const auto source = interpolate(source_points_, source_stations_, source_offset_m);
        if (!source) { return std::nullopt; }
        const double span = end_m_ - begin_m_;
        const double t = std::clamp((source_offset_m - begin_m_) / span, 0.0, 1.0);
        const double blend = t * t * t * (10.0 + t * (-15.0 + 6.0 * t));
        const double derivative_blend = 30.0 * t * t * (1.0 - t) * (1.0 - t) / span;
        const auto separation = subtract(target->position, source->position);
        position = add(source->position, scale(separation, blend));
        derivative = add(
            add(scale(source->tangent, 1.0 - blend),
                scale(target->tangent, mapping_scale_ * blend)),
            scale(separation, derivative_blend));
    }
    const auto support = supported(*ground_, position);
    if (!support || !finite(derivative)
        || std::hypot(derivative.east_m, derivative.north_m) < epsilon) {
        return std::nullopt;
    }
    double heading = std::atan2(derivative.east_m, derivative.north_m)
        * 180.0 / std::numbers::pi;
    if (heading < 0.0) { heading += 360.0; }
    const NpcLaneSample result{*support, heading, done ? target_id_ : source_id_,
                               0, done ? *target_station : source_offset_m};
    if (!footprint_supported(*ground_, result,
            body_half_length_m_, body_half_width_m_)) { return std::nullopt; }
    return result;
}

std::optional<NpcLaneChangePlan> plan_npc_lane_change(
    const TrafficNetwork& network, const GroundQuery& ground,
    std::uint32_t source_lane_id, double source_offset_m,
    std::uint32_t target_lane_id, double length_m,
    double body_half_length_m, double body_half_width_m)
{
    if (source_lane_id == 0 || source_lane_id == target_lane_id
        || !std::isfinite(source_offset_m) || !std::isfinite(length_m)
        || length_m < 8.0 || length_m > 80.0
        || !std::isfinite(body_half_length_m) || body_half_length_m <= 0.0
        || body_half_length_m > 10.0 || !std::isfinite(body_half_width_m)
        || body_half_width_m <= 0.0 || body_half_width_m > 4.0
        || (network.format_version != 1 && network.format_version != 2)
        || network.lanes.empty() || network.lanes.size() > 256) {
        return std::nullopt;
    }
    const TrafficLane* source = nullptr;
    const TrafficLane* target = nullptr;
    std::set<std::uint32_t> seen;
    for (const auto& lane : network.lanes) {
        if (lane.id == 0 || !seen.insert(lane.id).second) { return std::nullopt; }
        if (lane.id == source_lane_id) { source = &lane; }
        if (lane.id == target_lane_id) { target = &lane; }
    }
    if (!source || !target || source->lane_changes.size() > 8
        || source->signal_group_id != target->signal_group_id) {
        return std::nullopt;
    }
    const auto source_stations = stations(*source, body_half_width_m);
    const auto target_stations = stations(*target, body_half_width_m);
    if (!source_stations || !target_stations) { return std::nullopt; }
    const TrafficLaneChange* allowed = nullptr;
    for (const auto& candidate : source->lane_changes) {
        if (candidate.target_lane_id != target_lane_id) { continue; }
        if (!std::isfinite(candidate.source_begin_m) || !std::isfinite(candidate.source_end_m)
            || !std::isfinite(candidate.target_begin_m) || !std::isfinite(candidate.target_end_m)
            || candidate.source_end_m - candidate.source_begin_m < 8.0
            || candidate.target_end_m - candidate.target_begin_m < 8.0
            || candidate.source_begin_m < 5.0 || candidate.target_begin_m < 5.0
            || candidate.source_end_m > source_stations->back() - 5.0
            || candidate.target_end_m > target_stations->back() - 5.0) {
            return std::nullopt;
        }
        if (source_offset_m >= candidate.source_begin_m - epsilon
            && source_offset_m + length_m <= candidate.source_end_m + epsilon) {
            if (allowed) { return std::nullopt; }
            allowed = &candidate;
        }
    }
    if (!allowed) { return std::nullopt; }

    NpcLaneChangePlan plan;
    plan.source_id_ = source_lane_id;
    plan.target_id_ = target_lane_id;
    plan.begin_m_ = source_offset_m;
    plan.end_m_ = source_offset_m + length_m;
    plan.mapping_source_begin_m_ = allowed->source_begin_m;
    plan.mapping_target_begin_m_ = allowed->target_begin_m;
    plan.mapping_scale_ = (allowed->target_end_m - allowed->target_begin_m)
        / (allowed->source_end_m - allowed->source_begin_m);
    plan.body_half_length_m_ = body_half_length_m;
    plan.body_half_width_m_ = body_half_width_m;
    if (plan.mapping_scale_ < 0.95 || plan.mapping_scale_ > 1.05) { return std::nullopt; }
    plan.source_points_ = source->points;
    plan.target_points_ = target->points;
    plan.source_stations_ = *source_stations;
    plan.target_stations_ = *target_stations;
    plan.ground_ = &ground;

    const auto count = static_cast<std::size_t>(std::ceil(length_m / sample_spacing_m));
    for (std::size_t i = 0; i <= count; ++i) {
        const double station = source_offset_m + length_m * static_cast<double>(i)
            / static_cast<double>(count);
        const auto target_station = plan.target_offset(station);
        const auto source_sample = interpolate(source->points, *source_stations, station);
        const auto target_sample = target_station
            ? interpolate(target->points, *target_stations, *target_station) : std::nullopt;
        if (!source_sample || !target_sample
            || dot(source_sample->tangent, target_sample->tangent) < minimum_direction_dot) {
            return std::nullopt;
        }
        const auto separation = subtract(target_sample->position, source_sample->position);
        const double longitudinal = dot(separation, source_sample->tangent);
        const double lateral = std::sqrt(std::max(0.0, dot(separation, separation)
                                                       - longitudinal * longitudinal));
        const double expected_gap = (source->width_m + target->width_m) * 0.5;
        if (std::abs(longitudinal) > 0.5 || std::abs(separation.up_m) > 0.1
            || lateral < expected_gap - 0.1 || lateral > expected_gap + 0.75) {
            return std::nullopt;
        }
        const auto sample = plan.sample(station);
        if (!sample) { return std::nullopt; }
    }
    return plan;
}

bool npc_lane_change_gap_safe(
    const NpcLaneChangePlan& plan, double source_offset_m, double speed_mps,
    std::span<const NpcLaneChangeGapVehicle> other_vehicles,
    double body_half_length_m, double body_half_width_m)
{
    const auto station = plan.target_offset(source_offset_m);
    if (!station || !std::isfinite(speed_mps) || speed_mps < 0.0 || speed_mps > 55.6
        || !std::isfinite(body_half_length_m) || body_half_length_m <= 0.0
        || body_half_length_m > 10.0 || !std::isfinite(body_half_width_m)
        || body_half_width_m <= 0.0 || body_half_width_m > 4.0
        || other_vehicles.size() > 4096) {
        return false;
    }
    const auto target = interpolate(plan.target_points_, plan.target_stations_, *station);
    if (!target) { return false; }
    for (const auto& other : other_vehicles) {
        if (!finite(other.position_enu) || !finite(other.velocity_enu_mps)
            || !std::isfinite(other.half_length_m) || other.half_length_m <= 0.0
            || other.half_length_m > 50.0 || !std::isfinite(other.half_width_m)
            || other.half_width_m <= 0.0 || other.half_width_m > 20.0
            || length(other.velocity_enu_mps) > 150.0) {
            return false;
        }
        const auto projection = project(plan.target_points_, plan.target_stations_, other.position_enu);
        if (!projection) { return false; }
        if (projection->vertical_distance > 2.5
            || projection->lateral_distance > body_half_width_m + other.half_width_m + 0.25) {
            continue;
        }
        const double along = projection->station - *station;
        const double gap = std::abs(along) - body_half_length_m - other.half_length_m;
        const double other_speed = dot(other.velocity_enu_mps, projection->tangent);
        if (along >= 0.0) {
            if (gap < 2.0 + 1.25 * speed_mps
                || (speed_mps > other_speed && gap < 3.0 * (speed_mps - other_speed))) {
                return false;
            }
        } else {
            if (gap < 2.0 + 1.25 * std::max(0.0, other_speed)
                || (other_speed > speed_mps && gap < 3.0 * (other_speed - speed_mps))) {
                return false;
            }
        }
    }
    return true;
}

} // namespace simcore_host
