#pragma once

#include "traffic/traffic_network.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace simcore_host {

struct NpcPlannedRoute {
    std::vector<std::uint32_t> lane_ids;
    std::uint32_t destination_lane_id = 0;
    double length_m = 0.0;
};

// Pure, deterministic routing over declared directed successors. Signals are
// intentionally absent: a temporary red is not a blocked road. This planner
// never invents a lateral connection or reverses an authored lane.
class NpcRoutePlanner {
public:
    explicit NpcRoutePlanner(const TrafficNetwork& network);

    // Returns the shortest authored polyline distance, including the current
    // lane. Equal-cost paths use ascending lane IDs. A blocked current lane is
    // retained because the vehicle is already on it; every later lane is
    // excluded. A physical obstruction on that current lane still requires the
    // follower's obstacle guard or a separately authorized lane change.
    [[nodiscard]] std::optional<NpcPlannedRoute> shortest_route(
        std::uint32_t start_lane_id, std::uint32_t destination_lane_id,
        std::span<const std::uint32_t> blocked_lanes = {}) const;

    // Reproducible selection from uncontrolled destinations reachable in both
    // directions through legal directed paths (not reversing a lane). Dead-end
    // sinks cannot strand continuously roaming demo traffic. Roads >=25m are
    // preferred over small intersection connectors. Candidates sort
    // by ID before applying the entity seed and trip counter. Current and
    // previous destinations are excluded. If excluding the previous one leaves
    // no candidate, it may be revisited; an isolated lane returns nullopt.
    [[nodiscard]] std::optional<NpcPlannedRoute> choose_destination(
        std::uint32_t start_lane_id, std::uint64_t entity_seed,
        std::uint64_t decision_index,
        std::span<const std::uint32_t> blocked_lanes = {},
        std::uint32_t previous_destination = 0) const;

private:
    struct Lane {
        double length_m = 0.0;
        std::uint32_t signal_group_id = 0;
        std::vector<std::uint32_t> successors;
    };
    std::map<std::uint32_t, Lane> lanes_;
};

} // namespace simcore_host
