#include "traffic/npc_route_planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace simcore_host {
namespace {

double distance(const GroundPointEnu& a, const GroundPointEnu& b)
{
    return std::hypot(std::hypot(b.east_m - a.east_m, b.north_m - a.north_m),
                      b.up_m - a.up_m);
}

std::uint64_t mix(std::uint64_t value)
{
    // SplitMix64 finalizer, using defined unsigned wraparound, not global RNG.
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

} // namespace

NpcRoutePlanner::NpcRoutePlanner(const TrafficNetwork& network)
{
    if (network.lanes.empty() || network.lanes.size() > 256) {
        throw std::invalid_argument("NPC routing requires 1..256 authored lanes");
    }
    std::map<std::uint32_t, const TrafficLane*> authored;
    for (const auto& source : network.lanes) {
        if (source.id == 0 || source.points.size() < 2 || source.points.size() > 2048
            || source.successors.size() > 256 || !authored.emplace(source.id, &source).second) {
            throw std::invalid_argument("NPC routing has invalid lane IDs or geometry bounds");
        }
        Lane lane;
        lane.signal_group_id = source.signal_group_id;
        lane.successors = source.successors;
        std::sort(lane.successors.begin(), lane.successors.end());
        lane.successors.erase(std::unique(lane.successors.begin(), lane.successors.end()),
                              lane.successors.end());
        for (std::size_t point = 1; point < source.points.size(); ++point) {
            const auto& a = source.points[point - 1];
            const auto& b = source.points[point];
            const double length = distance(a, b);
            if (!std::isfinite(length) || length < 0.05 || length > 100.0
                || std::hypot(b.east_m - a.east_m, b.north_m - a.north_m) < 1e-8) {
                throw std::invalid_argument("NPC routing has a degenerate/nonfinite lane segment");
            }
            lane.length_m += length;
        }
        lanes_.emplace(source.id, std::move(lane));
    }
    for (const auto& [id, source] : authored) {
        for (const auto successor : lanes_.at(id).successors) {
            const auto found = authored.find(successor);
            if (found == authored.end()
                || distance(source->points.back(), found->second->points.front()) > 0.15000001
                || (source->signal_group_id != 0 && found->second->signal_group_id != 0)) {
                throw std::invalid_argument("NPC routing has a disconnected or illegal successor");
            }
        }
    }
}

std::optional<NpcPlannedRoute> NpcRoutePlanner::shortest_route(
    std::uint32_t start_lane_id, std::uint32_t destination_lane_id,
    std::span<const std::uint32_t> blocked_lanes) const
{
    if (!lanes_.contains(start_lane_id) || !lanes_.contains(destination_lane_id)) {
        return std::nullopt;
    }
    const std::set<std::uint32_t> blocked(blocked_lanes.begin(), blocked_lanes.end());
    if (start_lane_id != destination_lane_id && blocked.contains(destination_lane_id)) {
        return std::nullopt;
    }
    std::map<std::uint32_t, double> cost;
    std::map<std::uint32_t, std::uint32_t> predecessor;
    std::set<std::uint32_t> visited;
    cost[start_lane_id] = lanes_.at(start_lane_id).length_m;
    while (visited.size() < lanes_.size()) {
        std::uint32_t next = 0;
        double best = std::numeric_limits<double>::infinity();
        // std::map ID ordering gives a stable tie-break independent of JSON
        // order, allocator behavior or unordered-map implementations.
        for (const auto& [id, value] : cost) {
            if (!visited.contains(id) && value < best) { next = id; best = value; }
        }
        if (next == 0) { return std::nullopt; }
        if (next == destination_lane_id) {
            NpcPlannedRoute result;
            result.destination_lane_id = destination_lane_id;
            result.length_m = best;
            for (auto id = next;; id = predecessor.at(id)) {
                result.lane_ids.push_back(id);
                if (id == start_lane_id) { break; }
            }
            std::reverse(result.lane_ids.begin(), result.lane_ids.end());
            return result;
        }
        visited.insert(next);
        for (const auto successor : lanes_.at(next).successors) {
            if (blocked.contains(successor) || visited.contains(successor)) { continue; }
            const double candidate = best + lanes_.at(successor).length_m;
            const auto old = cost.find(successor);
            if (old == cost.end() || candidate < old->second) {
                cost[successor] = candidate;
                predecessor[successor] = next;
            }
        }
    }
    return std::nullopt;
}

std::optional<NpcPlannedRoute> NpcRoutePlanner::choose_destination(
    std::uint32_t start_lane_id, std::uint64_t entity_seed,
    std::uint64_t decision_index, std::span<const std::uint32_t> blocked_lanes,
    std::uint32_t previous_destination) const
{
    std::vector<NpcPlannedRoute> candidates;
    std::optional<NpcPlannedRoute> previous;
    for (const auto& [id, lane] : lanes_) {
        if (id == start_lane_id || lane.signal_group_id != 0 || lane.successors.empty()
            || !shortest_route(id, start_lane_id, blocked_lanes)) { continue; }
        if (auto route = shortest_route(start_lane_id, id, blocked_lanes)) {
            if (id == previous_destination) { previous = std::move(route); }
            else { candidates.push_back(std::move(*route)); }
        }
    }
    if (candidates.empty()) { return previous; }
    // Prefer road segments over short intersection connectors. Legacy tiny
    // test/demo loops still have usable destinations when no long road exists.
    if (std::any_of(candidates.begin(), candidates.end(), [&](const NpcPlannedRoute& route) {
            return lanes_.at(route.destination_lane_id).length_m >= 25.0;
        })) {
        std::erase_if(candidates, [&](const NpcPlannedRoute& route) {
            return lanes_.at(route.destination_lane_id).length_m < 25.0;
        });
    }
    const auto value = mix(entity_seed ^ mix(decision_index + 0x9e3779b97f4a7c15ULL));
    return candidates[static_cast<std::size_t>(value % candidates.size())];
}

} // namespace simcore_host
