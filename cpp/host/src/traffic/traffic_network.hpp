#pragma once

#include "terrain/ground_query.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace simcore_host {

enum class SignalAspect : std::uint32_t {
    Unknown = 0,
    Red = 1,
    Yellow = 2,
    Green = 3,
};

enum class TrafficSignalKind : std::uint32_t {
    Vehicle = 1,
    Pedestrian = 2,
};

// Authored, same-direction parallel window. Stations are 3D polyline arc
// lengths, not world coordinates; both ends exclude intersections/merge fans.
struct TrafficLaneChange {
    std::uint32_t target_lane_id = 0;
    double source_begin_m = 0.0;
    double source_end_m = 0.0;
    double target_begin_m = 0.0;
    double target_end_m = 0.0;
};

struct TrafficLane {
    std::uint32_t id = 0;
    double width_m = 0.0;
    double speed_limit_mps = 0.0;
    // Controls entry beyond this lane's endpoint, not travel along the lane.
    std::uint32_t signal_group_id = 0;
    bool terminal = false;
    std::vector<GroundPointEnu> points;
    std::vector<std::uint32_t> successors;
    // Additive, optional JSON metadata. Old v1/v2 packages retain no changes.
    std::vector<TrafficLaneChange> lane_changes;
};

struct TrafficSignal {
    std::uint32_t id = 0;
    std::uint32_t group_id = 0;
    GroundPointEnu position_enu;
    double heading_deg = 0.0;
    // Version 2 names the owning signal plan. Version 1 uses legacy controller 1.
    // Kept last so existing version-1 aggregate fixtures remain source compatible.
    std::uint32_t controller_id = 1;
    // Version 1 and omitted legacy values are vehicle heads.
    TrafficSignalKind kind = TrafficSignalKind::Vehicle;
};

struct TrafficSignalSnapshot {
    std::uint32_t id = 0;
    std::uint32_t group_id = 0;
    SignalAspect aspect = SignalAspect::Unknown;
    GroundPointEnu position_enu;
    double heading_deg = 0.0;
    // Time until this signal's aspect changes. Disabled signals remain red
    // indefinitely and report zero rather than a misleading finite countdown.
    double remaining_seconds = 0.0;
    // Kept last for compatibility with existing six-field aggregate fixtures.
    std::uint32_t controller_id = 1;
    TrafficSignalKind kind = TrafficSignalKind::Vehicle;
    bool out_of_service = false;
};

// format_version 2 stores phase timing in the network instead of relying on
// the legacy two-group program. Groups absent from both lists are red.
struct TrafficSignalPhase {
    std::uint32_t duration_ms = 0;
    std::vector<std::uint32_t> green_groups;
    std::vector<std::uint32_t> yellow_groups;
};

struct TrafficSignalPlan {
    std::uint32_t id = 0;
    // At simulation time zero the plan is this far into its cycle.
    std::uint32_t offset_ms = 0;
    std::vector<std::uint32_t> groups;
    std::vector<TrafficSignalPhase> phases;
    std::uint64_t cycle_ms = 0;
};

struct TrafficNetwork {
    std::uint32_t format_version = 1;
    std::string source_map_checksum;
    // FNV-1a over exact file bytes, independent of the map's collision checksum.
    std::string checksum;
    // Canonical ID order makes snapshots and subsequent routing deterministic.
    std::vector<TrafficLane> lanes;
    std::vector<TrafficSignal> signals;
    // Empty for format_version 1, whose exact 30-second/two-group behavior is
    // retained for existing packages. Required and data-driven in version 2.
    std::vector<TrafficSignalPlan> signal_plans;

    [[nodiscard]] std::vector<TrafficSignalSnapshot> signals_at(
        std::uint64_t elapsed_ns, bool enabled = true) const;
};

// Stable startup diagnostic for the active signal program. Version 1 keeps
// its legacy 30-second label; version 2 reports every controller's authored
// cycle and offset so independent plans cannot be mistaken for one program.
[[nodiscard]] std::string describe_signal_timing(const TrafficNetwork& network);

// Loads the complete immutable network or throws std::runtime_error. JSON
// schema/types, resource bounds, topology, stopline groups and sampled lane
// footprints are validated before a caller can install it in a running world.
[[nodiscard]] TrafficNetwork load_traffic_network(
    const std::filesystem::path& path,
    const std::string& expected_map_checksum,
    const GroundQuery& ground);

} // namespace simcore_host
