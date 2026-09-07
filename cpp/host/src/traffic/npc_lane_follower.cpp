#include "traffic/npc_lane_follower.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>

namespace simcore_host {
namespace {

constexpr double epsilon = 1e-8;
constexpr double stop_epsilon_m = 1e-5;

bool finite(const GroundPointEnu& p)
{
    return std::isfinite(p.east_m) && std::isfinite(p.north_m) && std::isfinite(p.up_m);
}

double distance(const GroundPointEnu& a, const GroundPointEnu& b)
{
    return std::hypot(std::hypot(b.east_m - a.east_m, b.north_m - a.north_m),
                      b.up_m - a.up_m);
}

void require(bool condition, const char* message)
{
    if (!condition) { throw std::invalid_argument(message); }
}

// A one-step stopping envelope for semi-implicit arc-length integration:
// v_next*h + (v_next^2 - limit^2)/(2*b) <= available distance.
double braking_envelope(double distance_m, double limit, double brake, double h)
{
    const double bh = brake * h;
    return std::max(0.0, std::sqrt(limit * limit + bh * bh
                                 + 2.0 * brake * std::max(0.0, distance_m)) - bh);
}

} // namespace

NpcLaneFollower::NpcLaneFollower(NpcLaneFollowerConfig config) : config_(config)
{
    require(std::isfinite(config_.max_speed_mps) && config_.max_speed_mps > 0.0
                && config_.max_speed_mps <= 55.6,
            "NPC maximum speed must be finite in (0,55.6] m/s");
    require(std::isfinite(config_.acceleration_mps2) && config_.acceleration_mps2 > 0.0
                && config_.acceleration_mps2 <= 20.0
                && std::isfinite(config_.braking_mps2) && config_.braking_mps2 > 0.0
                && config_.braking_mps2 <= 20.0,
            "NPC acceleration/braking must be finite in (0,20] m/s2");
    require(std::isfinite(config_.front_extent_m) && config_.front_extent_m > 0.0
                && config_.front_extent_m <= 10.0
                && std::isfinite(config_.half_width_m) && config_.half_width_m > 0.0
                && config_.half_width_m <= 4.0
                && std::isfinite(config_.stop_margin_m) && config_.stop_margin_m >= 0.0
                && config_.stop_margin_m <= 10.0,
            "NPC extent/stop margin must be finite and bounded");
}

void NpcLaneFollower::clear() noexcept
{
    route_.clear();
    signal_identities_.clear();
    ground_ = nullptr;
    loop_ = false;
    destination_end_ = false;
    route_length_m_ = 0.0;
    progress_m_ = 0.0;
    reset_progress_m_ = 0.0;
    committed_stopline_m_ = -1.0;
    committed_until_m_ = -1.0;
    committed_lane_id_ = 0;
    state_ = {};
}

void NpcLaneFollower::rebuild(const TrafficNetwork& network, const GroundQuery& ground,
                            std::vector<std::uint32_t> route_lane_ids,
                            bool loop, double start_offset_m, bool destination_end)
{
    clear();
    try {
		require((network.format_version == 1 || network.format_version == 2)
					&& !network.lanes.empty()
					&& network.lanes.size() <= 256,
				"NPC requires a bounded traffic network version 1 or 2");
        require(!route_lane_ids.empty() && route_lane_ids.size() <= 1024,
                "NPC route must contain 1..1024 lane occurrences");
        std::map<std::uint32_t, const TrafficLane*> by_id;
        for (const auto& lane : network.lanes) {
            require(lane.id != 0 && by_id.emplace(lane.id, &lane).second,
                    "NPC network has a zero or duplicate lane ID");
        }
        std::vector<const TrafficLane*> selected;
        std::set<std::uint32_t> groups;
        for (const auto id : route_lane_ids) {
            const auto found = by_id.find(id);
            require(found != by_id.end(), "NPC route references an unknown lane");
            const auto& lane = *found->second;
            require(lane.points.size() >= 2 && lane.points.size() <= 2048
                        && std::isfinite(lane.width_m) && lane.width_m >= 2.0
                        && lane.width_m <= 8.0
                        && config_.half_width_m * 2.0 <= lane.width_m + epsilon
                        && std::isfinite(lane.speed_limit_mps) && lane.speed_limit_mps > 0.0
						&& lane.speed_limit_mps <= 55.6 && lane.signal_group_id <= 4096
                        && lane.terminal == lane.successors.empty()
                        && !(lane.terminal && lane.signal_group_id != 0),
                    "NPC route lane geometry/width/limit/control is invalid");
            for (std::size_t p = 0; p < lane.points.size(); ++p) {
                require(finite(lane.points[p]), "NPC route has a non-finite point");
                if (p == 0) { continue; }
                const auto& a = lane.points[p - 1];
                const auto& b = lane.points[p];
                require(distance(a, b) >= 0.05 && distance(a, b) <= 100.0
                            && std::hypot(b.east_m - a.east_m, b.north_m - a.north_m) > epsilon,
                        "NPC route has a degenerate or unbounded segment");
            }
            selected.push_back(&lane);
            if (lane.signal_group_id != 0) { groups.insert(lane.signal_group_id); }
        }
        auto connected = [&](const TrafficLane& from, const TrafficLane& to) {
            require(std::find(from.successors.begin(), from.successors.end(), to.id)
                        != from.successors.end()
                        && distance(from.points.back(), to.points.front()) <= 0.15 + epsilon
                        && !(from.signal_group_id != 0 && to.signal_group_id != 0),
                    "NPC route has a disconnected or controlled-to-controlled successor");
        };
        for (std::size_t i = 1; i < selected.size(); ++i) {
            connected(*selected[i - 1], *selected[i]);
        }
        require(!(loop && destination_end), "NPC destination route cannot loop");
        if (loop) { connected(*selected.back(), *selected.front()); }
        else { require(selected.back()->terminal || destination_end,
                       "NPC open route must end at an explicit terminal"); }

        require(network.signals.size() <= 32, "NPC network has too many signal heads");
        std::set<std::uint32_t> signal_ids;
        for (const auto& signal : network.signals) {
			require(signal.id != 0 && signal.group_id >= 1 && signal.group_id <= 4096
						&& signal.controller_id >= 1 && signal.controller_id <= 64
						&& signal_ids.insert(signal.id).second,
                    "NPC network has an invalid or duplicate signal head");
            if (groups.contains(signal.group_id)) {
                signal_identities_.push_back({signal.id, signal.group_id});
            }
        }
        for (const auto group : groups) {
            require(std::any_of(signal_identities_.begin(), signal_identities_.end(),
                                [group](const auto& head) { return head.group_id == group; }),
                    "NPC controlled route requires authored signal heads");
        }
        for (std::size_t i = 0; i < selected.size(); ++i) {
            const auto& source = *selected[i];
            RouteLane lane;
            lane.id = source.id;
            lane.signal_group_id = source.signal_group_id;
            lane.speed_limit_mps = source.speed_limit_mps;
            lane.start_m = route_length_m_;
            lane.points = source.points;
            // Preserve the preceding authored stopline, but remove sub-15cm
            // loader-tolerated endpoint gaps without teleporting the anchor.
            if (i > 0 || loop) {
                lane.points.front() = selected[(i + selected.size() - 1) % selected.size()]->points.back();
            }
            lane.point_offsets_m.push_back(0.0);
            for (std::size_t p = 1; p < lane.points.size(); ++p) {
                const auto& a = lane.points[p - 1];
                const auto& b = lane.points[p];
                require(std::hypot(b.east_m - a.east_m, b.north_m - a.north_m) > epsilon,
                        "NPC joined route segment has no planar heading");
                lane.length_m += distance(a, b);
                lane.point_offsets_m.push_back(lane.length_m);
            }
            route_length_m_ += lane.length_m;
            route_.push_back(std::move(lane));
        }
        require(route_length_m_ > config_.front_extent_m + config_.stop_margin_m,
                "NPC route is shorter than its front extent/stop margin");
        ground_ = &ground;
        loop_ = loop;
        destination_end_ = destination_end;
        reset(start_offset_m);
    } catch (...) {
        clear();
        throw;
    }
}

void NpcLaneFollower::reroute(const TrafficNetwork& network,
                             std::vector<std::uint32_t> route_lane_ids,
                             bool loop, bool destination_end)
{
    require(state_.valid && !route_.empty() && ground_ != nullptr,
            "NPC reroute requires a supported current anchor");
    require(!route_lane_ids.empty() && route_lane_ids.front() == state_.lane_id,
            "NPC reroute must begin with its current lane");
    const auto& current = route_[state_.route_index];
    if (state_.stopline_committed && current.id == state_.committed_lane_id) {
        const std::size_t next = (state_.route_index + 1) % route_.size();
        require(route_lane_ids.size() >= 2 && route_lane_ids[1] == route_[next].id,
                "NPC reroute cannot change an already committed connector");
    }
    NpcLaneFollower replacement(config_);
    replacement.rebuild(network, *ground_, std::move(route_lane_ids), loop, 0.0, destination_end);
    const auto& rebuilt = replacement.route_.front();
    require(rebuilt.signal_group_id == current.signal_group_id
                && rebuilt.points.size() == current.points.size()
                && distance(rebuilt.points.front(), current.points.front()) <= 0.15 + epsilon,
            "NPC reroute cannot replace the occupied lane geometry/control");
    for (std::size_t point = 1; point < current.points.size(); ++point) {
        require(distance(rebuilt.points[point], current.points[point]) <= epsilon,
                "NPC reroute cannot move the occupied lane geometry");
    }
    // The old predecessor may have closed a loader-tolerated <15cm join gap.
    // Keep that exact occupied geometry so replanning cannot snap the anchor.
    replacement.route_.front().points = current.points;
    replacement.route_.front().point_offsets_m = current.point_offsets_m;
    replacement.route_.front().length_m = current.length_m;
    replacement.route_length_m_ = 0.0;
    for (auto& lane : replacement.route_) {
        lane.start_m = replacement.route_length_m_;
        replacement.route_length_m_ += lane.length_m;
    }
    replacement.progress_m_ = state_.lane_offset_m;
    require(loop || replacement.progress_m_ <= replacement.route_length_m_
                - config_.front_extent_m - config_.stop_margin_m + epsilon,
            "NPC reroute destination lies behind the current usable anchor");
    replacement.reset_progress_m_ = replacement.progress_m_ - state_.distance_travelled_m;
    if (state_.stopline_committed) {
        replacement.committed_stopline_m_ = committed_stopline_m_ - progress_m_
            + replacement.progress_m_;
        replacement.committed_until_m_ = committed_until_m_ - progress_m_
            + replacement.progress_m_;
        replacement.committed_lane_id_ = committed_lane_id_;
    }
    const auto anchor = replacement.sample_at(replacement.progress_m_);
    require(anchor && distance(anchor->position_enu, state_.position_enu) <= epsilon,
            "NPC reroute must preserve its ground-supported anchor exactly");
    replacement.state_ = state_;
    static_cast<NpcLaneSample&>(replacement.state_) = *anchor;
    if (replacement.state_.stop_reason == NpcLaneStopReason::Terminal
        && replacement.progress_m_ < replacement.route_length_m_
            - config_.front_extent_m - config_.stop_margin_m - stop_epsilon_m) {
        replacement.state_.stop_reason = NpcLaneStopReason::None;
    }
    *this = std::move(replacement);
}

bool NpcLaneFollower::complete_lane_change(
    const TrafficNetwork& network, std::vector<std::uint32_t> target_route,
    double target_lane_offset_m, bool destination_end)
{
    try {
        require(state_.valid && !route_.empty() && ground_ != nullptr
                    && !state_.stopline_committed && !target_route.empty()
                    && target_route.front() != state_.lane_id
                    && std::isfinite(target_lane_offset_m),
                "NPC lateral completion has invalid state or destination");
        const auto source = std::find_if(network.lanes.begin(), network.lanes.end(),
            [&](const TrafficLane& lane) { return lane.id == state_.lane_id; });
        require(source != network.lanes.end(), "NPC lateral source lane is unknown");
        const auto window = std::find_if(source->lane_changes.begin(), source->lane_changes.end(),
            [&](const TrafficLaneChange& change) {
                if (change.target_lane_id != target_route.front()
                    || !std::isfinite(change.source_begin_m) || !std::isfinite(change.source_end_m)
                    || !std::isfinite(change.target_begin_m) || !std::isfinite(change.target_end_m)
                    || change.source_begin_m < 0.0 || change.target_begin_m < 0.0
                    || change.source_end_m <= change.source_begin_m
                    || change.target_end_m <= change.target_begin_m
                    || state_.lane_offset_m < change.source_begin_m - epsilon
                    || state_.lane_offset_m > change.source_end_m + epsilon) { return false; }
                const double t = (state_.lane_offset_m - change.source_begin_m)
                    / (change.source_end_m - change.source_begin_m);
                const double expected = change.target_begin_m
                    + t * (change.target_end_m - change.target_begin_m);
                return std::abs(target_lane_offset_m - expected) <= 0.1;
            });
        require(window != source->lane_changes.end(), "NPC lateral completion lacks an authored window");
        NpcLaneFollower replacement(config_);
        replacement.rebuild(network, *ground_, std::move(target_route), false,
                            target_lane_offset_m, destination_end);
        require(replacement.state_.valid && replacement.state_.route_index == 0
                    && replacement.route_.front().signal_group_id == source->signal_group_id,
                "NPC lateral completion changed control identity or lacks ground");
        const double heading_delta = std::remainder(
            replacement.state_.heading_deg - state_.heading_deg, 360.0);
        require(std::abs(heading_delta) <= 15.0,
                "NPC lateral completion cannot enter an opposing lane");
        const auto target_sample = static_cast<const NpcLaneSample&>(replacement.state_);
        replacement.reset_progress_m_ = replacement.progress_m_ - state_.distance_travelled_m;
        replacement.state_ = state_;
        static_cast<NpcLaneSample&>(replacement.state_) = target_sample;
        replacement.state_.stopline_committed = false;
        replacement.state_.committed_lane_id = 0;
        if (replacement.state_.stop_reason == NpcLaneStopReason::Terminal) {
            replacement.state_.stop_reason = NpcLaneStopReason::None;
        }
        *this = std::move(replacement);
        return true;
    } catch (const std::invalid_argument&) { return false; }
}

void NpcLaneFollower::reset(double start_offset_m)
{
    try {
        require(!route_.empty() && ground_ != nullptr, "NPC reset requires a rebuilt route");
        require(std::isfinite(start_offset_m) && start_offset_m >= 0.0
                    && (loop_ ? start_offset_m < route_length_m_
                              : start_offset_m <= route_length_m_ - config_.front_extent_m
                                                       - config_.stop_margin_m + epsilon),
                "NPC start offset is outside the usable route");
        for (const auto& lane : route_) {
            if (lane.signal_group_id != 0 && start_offset_m >= lane.start_m
                && start_offset_m < lane.start_m + lane.length_m) {
                require(start_offset_m <= lane.start_m + lane.length_m
                            - config_.front_extent_m - config_.stop_margin_m + epsilon,
                        "NPC reset cannot spawn beyond a controlled stopping margin");
            }
        }
        progress_m_ = reset_progress_m_ = start_offset_m;
        committed_stopline_m_ = committed_until_m_ = -1.0;
        committed_lane_id_ = 0;
        state_ = {};
        if (const auto sample = sample_at(progress_m_)) {
            static_cast<NpcLaneSample&>(state_) = *sample;
            state_.valid = true;
            state_.stop_reason = NpcLaneStopReason::None;
        } else {
            state_.stop_reason = NpcLaneStopReason::NoGround;
        }
    } catch (...) {
        clear();
        throw;
    }
}

std::optional<NpcLaneSample> NpcLaneFollower::sample_at(double progress_m) const
{
    if (route_.empty() || ground_ == nullptr || !std::isfinite(progress_m) || progress_m < 0.0) {
        return std::nullopt;
    }
    const double local = loop_ ? std::fmod(progress_m, route_length_m_)
                               : std::min(progress_m, route_length_m_);
    auto found = std::upper_bound(route_.begin(), route_.end(), local,
                                 [](double s, const RouteLane& lane) { return s < lane.start_m; });
    const std::size_t index = found == route_.begin() ? 0
        : static_cast<std::size_t>(std::distance(route_.begin(), found) - 1);
    const auto& lane = route_[index];
    const double offset = std::clamp(local - lane.start_m, 0.0, lane.length_m);
    const auto after = std::upper_bound(lane.point_offsets_m.begin(), lane.point_offsets_m.end(), offset);
    const std::size_t p = std::min(lane.points.size() - 2,
        static_cast<std::size_t>(std::distance(lane.point_offsets_m.begin(), after) - 1));
    const auto& a = lane.points[p];
    const auto& b = lane.points[p + 1];
    const double t = std::clamp((offset - lane.point_offsets_m[p])
                                  / (lane.point_offsets_m[p + 1] - lane.point_offsets_m[p]), 0.0, 1.0);
    NpcLaneSample sample;
    sample.position_enu = {a.east_m + (b.east_m - a.east_m) * t,
                           a.north_m + (b.north_m - a.north_m) * t,
                           a.up_m + (b.up_m - a.up_m) * t};
    const double heading = std::atan2(b.east_m - a.east_m, b.north_m - a.north_m);
    sample.heading_deg = std::fmod(heading * 180.0 / std::numbers::pi + 360.0, 360.0);
    if (sample.heading_deg >= 360.0) { sample.heading_deg = 0.0; }
    sample.lane_id = lane.id;
    sample.route_index = index;
    sample.lane_offset_m = offset;
    auto query = [&](double east, double north, bool centre) -> std::optional<GroundHit> {
        try {
            const auto hit = ground_->query_down({{east, north, sample.position_enu.up_m + 2.0}, 4.0});
            if (!hit || !finite(hit->point_enu) || !finite(hit->normal_enu)
                || !std::isfinite(hit->distance_m) || hit->distance_m < 0.0 || hit->distance_m > 4.0
                || hit->normal_enu.up_m < 0.5
                || std::abs(hit->point_enu.east_m - east) > 0.01
                || std::abs(hit->point_enu.north_m - north) > 0.01
                || (centre && std::abs(hit->point_enu.up_m - sample.position_enu.up_m) > 0.20)) {
                return std::nullopt;
            }
            return hit;
        } catch (...) { return std::nullopt; }
    };
    const auto centre = query(sample.position_enu.east_m, sample.position_enu.north_m, true);
    if (!centre) { return std::nullopt; }
    const double forward_e = std::sin(heading);
    const double forward_n = std::cos(heading);
    for (const double longitudinal : {-config_.front_extent_m, config_.front_extent_m}) {
        for (const double side : {-config_.half_width_m, config_.half_width_m}) {
            if (!query(sample.position_enu.east_m + forward_e * longitudinal - forward_n * side,
                       sample.position_enu.north_m + forward_n * longitudinal + forward_e * side,
                       false)) { return std::nullopt; }
        }
    }
    sample.position_enu.up_m = centre->point_enu.up_m;
    return sample;
}

std::optional<NpcLaneSample> NpcLaneFollower::sample_ahead(double distance_m) const
{
    if (!std::isfinite(distance_m) || distance_m < 0.0) { return std::nullopt; }
    return sample_at(progress_m_ + distance_m);
}

std::optional<NpcLaneSample> NpcLaneFollower::sample_behind(double distance_m) const
{
    if (!state_.valid || !std::isfinite(distance_m) || distance_m < 0.0
        || distance_m > state_.lane_offset_m + epsilon) {
        return std::nullopt;
    }
    const auto sample = sample_at(progress_m_ - distance_m);
    if (!sample || sample->lane_id != state_.lane_id
        || sample->route_index != state_.route_index) {
        return std::nullopt;
    }
    return sample;
}

bool NpcLaneFollower::retreat_for_obstacle(double distance_m)
{
    if (!state_.valid || !state_.stopped || state_.stopline_committed
        || !std::isfinite(distance_m) || distance_m < 0.0) {
        return false;
    }
    const auto sample = sample_behind(distance_m);
    if (!sample) { return false; }
    progress_m_ -= distance_m;
    static_cast<NpcLaneSample&>(state_) = *sample;
    state_.speed_mps = 0.0;
    state_.distance_travelled_m = progress_m_ - reset_progress_m_;
    state_.stopped = true;
    state_.stop_reason = NpcLaneStopReason::Blocked;
    state_.safety_clamped = false;
    return true;
}

bool NpcLaneFollower::green(std::uint32_t group_id,
                            std::span<const TrafficSignalSnapshot> signals,
                            double elapsed_seconds) const
{
    for (const auto& sample : signals) {
        if (sample.group_id == group_id
            && std::none_of(signal_identities_.begin(), signal_identities_.end(),
                            [&](const auto& expected) {
                                return expected.id == sample.id && expected.group_id == group_id;
                            })) { return false; }
    }
    bool found_group = false;
    for (const auto& expected : signal_identities_) {
        if (expected.group_id != group_id) { continue; }
        std::size_t matches = 0;
        for (const auto& signal : signals) {
            if (signal.id != expected.id) { continue; }
            ++matches;
            if (signal.group_id != group_id) { return false; }
            if (signal.out_of_service) {
                if (signal.aspect != SignalAspect::Red || signal.remaining_seconds != 0.0) {
                    return false;
                }
                continue;
            }
            found_group = true;
            if (signal.aspect != SignalAspect::Green
                || !std::isfinite(signal.remaining_seconds)
                || signal.remaining_seconds <= elapsed_seconds + epsilon) { return false; }
        }
        if (matches != 1) { return false; }
    }
    return found_group;
}

void NpcLaneFollower::update_commitment(double old_progress, double new_progress,
                                      std::span<const TrafficSignalSnapshot> signals,
                                      double elapsed_seconds)
{
    const double base = loop_ ? std::floor(old_progress / route_length_m_) * route_length_m_ : 0.0;
    for (std::size_t lap = 0; lap < (loop_ ? 2U : 1U); ++lap) {
        for (std::size_t i = 0; i < route_.size(); ++i) {
            const auto& lane = route_[i];
            if (lane.signal_group_id == 0) { continue; }
            const double line = base + static_cast<double>(lap) * route_length_m_
                                + lane.start_m + lane.length_m;
            if (old_progress + config_.front_extent_m < line - epsilon
                && new_progress + config_.front_extent_m >= line - epsilon
                && green(lane.signal_group_id, signals, elapsed_seconds)) {
                committed_stopline_m_ = line;
                committed_lane_id_ = lane.id;
                committed_until_m_ = line + route_[(i + 1) % route_.size()].length_m;
            }
        }
    }
}

void NpcLaneFollower::stop(NpcLaneStopReason reason, bool safety_clamped) noexcept
{
    state_.speed_mps = 0.0;
    state_.stopped = true;
    state_.stop_reason = reason;
    state_.safety_clamped = state_.safety_clamped || safety_clamped;
}

const NpcLaneFollowerState& NpcLaneFollower::step(
    double dt_seconds, std::span<const TrafficSignalSnapshot> signals,
    bool enabled, std::optional<double> blocked_distance_m)
{
    state_.safety_clamped = false;
    if (route_.empty()) { stop(NpcLaneStopReason::Uninitialized, false); return state_; }
    if (!std::isfinite(dt_seconds) || dt_seconds < 0.0 || dt_seconds > 10.0
        || signals.size() > 64
        || (blocked_distance_m && (!std::isfinite(*blocked_distance_m) || *blocked_distance_m < 0.0))) {
        stop(NpcLaneStopReason::InvalidInput, true);
        return state_;
    }
    if (!enabled) { stop(NpcLaneStopReason::Disabled, true); return state_; }
    if (const auto anchor = sample_at(progress_m_)) {
        static_cast<NpcLaneSample&>(state_) = *anchor;
        state_.valid = true;
    } else {
        state_.valid = false;
        stop(NpcLaneStopReason::NoGround, true);
        return state_;
    }
    const double clearance = config_.front_extent_m + config_.stop_margin_m;
    const double obstacle_stop = blocked_distance_m
        ? progress_m_ + std::max(0.0, *blocked_distance_m - clearance)
        : std::numeric_limits<double>::infinity();
    double elapsed = 0.0;
    while (elapsed < dt_seconds - epsilon) {
        double h = std::min({dt_seconds - elapsed, 1.0 / 60.0,
                             0.1 / config_.max_speed_mps});
        for (const auto& signal : signals) {
            const double until_change = signal.remaining_seconds - elapsed;
            if (signal.aspect == SignalAspect::Green && std::isfinite(until_change)
                && until_change > epsilon) { h = std::min(h, until_change); }
        }
        double stop_at = obstacle_stop;
        NpcLaneStopReason reason = std::isfinite(stop_at) ? NpcLaneStopReason::Blocked
                                                        : NpcLaneStopReason::None;
        if (!loop_ && route_length_m_ - clearance < stop_at) {
            stop_at = route_length_m_ - clearance;
            reason = NpcLaneStopReason::Terminal;
        }
        double desired = std::min(config_.max_speed_mps, route_[state_.route_index].speed_limit_mps);
        const double base = loop_ ? std::floor(progress_m_ / route_length_m_) * route_length_m_ : 0.0;
        for (std::size_t lap = 0; lap < (loop_ ? 2U : 1U); ++lap) {
            for (const auto& lane : route_) {
                const double start = base + static_cast<double>(lap) * route_length_m_ + lane.start_m;
                const double end = start + lane.length_m;
                if (end < progress_m_ - epsilon) { continue; }
                const double upcoming_limit = std::min(config_.max_speed_mps, lane.speed_limit_mps);
                if (start > progress_m_ + epsilon && upcoming_limit < desired) {
                    desired = std::min(desired, braking_envelope(start - progress_m_,
                        upcoming_limit, config_.braking_mps2, h));
                }
                if (lane.signal_group_id != 0 && end > committed_stopline_m_ + epsilon
                    && !green(lane.signal_group_id, signals, elapsed)) {
                    const double target = std::max(progress_m_, end - clearance);
                    if (target < stop_at) { stop_at = target; reason = NpcLaneStopReason::Signal; }
                }
            }
        }
        const double remaining = stop_at - progress_m_;
        if (std::isfinite(stop_at)) {
            desired = std::min(desired, braking_envelope(remaining, 0.0, config_.braking_mps2, h));
        }
        const double old_speed = state_.speed_mps;
        const double rate = desired >= old_speed ? config_.acceleration_mps2 : config_.braking_mps2;
        double next_speed = old_speed + std::clamp(desired - old_speed, -rate * h, rate * h);
        double advance = next_speed * h;
        bool at_stop = false;
        if (std::isfinite(stop_at) && (advance >= remaining || remaining <= stop_epsilon_m)) {
            advance = std::max(0.0, remaining);
            next_speed = 0.0;
            at_stop = true;
            if (old_speed > config_.braking_mps2 * h + epsilon) { state_.safety_clamped = true; }
        }
        const auto next = sample_at(progress_m_ + advance);
        if (!next) { stop(NpcLaneStopReason::NoGround, true); break; }
        update_commitment(progress_m_, progress_m_ + advance, signals, elapsed);
        progress_m_ += advance;
        static_cast<NpcLaneSample&>(state_) = *next;
        state_.speed_mps = next_speed;
        state_.distance_travelled_m = progress_m_ - reset_progress_m_;
        state_.stopped = next_speed <= epsilon;
        state_.stop_reason = at_stop ? reason : NpcLaneStopReason::None;
        state_.stopline_committed = committed_lane_id_ != 0 && progress_m_ < committed_until_m_ - epsilon;
        state_.committed_lane_id = state_.stopline_committed ? committed_lane_id_ : 0;
        elapsed += h;
        if (at_stop) { break; }
    }
    return state_;
}

} // namespace simcore_host
