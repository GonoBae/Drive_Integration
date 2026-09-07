#pragma once

#include "traffic/traffic_network.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace simcore_host {

struct NpcLaneFollowerConfig {
    double max_speed_mps = 6.0;
    double acceleration_mps2 = 1.5;
    double braking_mps2 = 3.0;
    // Distance from the centre anchor to the front of the collision OBB.
    double front_extent_m = 2.2;
    double half_width_m = 1.0;
    double stop_margin_m = 0.5;
};

enum class NpcLaneStopReason {
    None,
    Uninitialized,
    Disabled,
    Signal,
    Blocked,
    Terminal,
    NoGround,
    InvalidInput,
};

struct NpcLaneSample {
    // Ground-level centre projection, NOT the OBB's vertical centre. The host
    // adds its collision half-height and chassis clearance exactly once.
    GroundPointEnu position_enu;
    double heading_deg = 0.0; // Navigation convention: north=0, east=90.
    std::uint32_t lane_id = 0;
    std::size_t route_index = 0;
    double lane_offset_m = 0.0;
};

struct NpcLaneFollowerState : NpcLaneSample {
    double speed_mps = 0.0; // Three-dimensional authored polyline arc length/s.
    double distance_travelled_m = 0.0; // Since reset, including complete laps.
    bool valid = false; // A confirmed ground-supported anchor exists.
    bool stopped = true;
    NpcLaneStopReason stop_reason = NpcLaneStopReason::Uninitialized;
    // Set when the front crossed an entry stopline on green; retained through
    // its immediate uncontrolled connector. Later yellow/red does not revoke it.
    bool stopline_committed = false;
    std::uint32_t committed_lane_id = 0;
    // Last step used a fail-closed position/speed clamp (e.g. a late red inside
    // braking distance). Normal acceleration/braking is bounded; hard safety,
    // disabled, missing ground and invalid input take precedence over comfort.
    bool safety_clamped = false;
};

// Pure deterministic kinematic route controller: no sockets, clock, SDK,
// threads, random selection, collision ownership or Ego physics dependencies.
class NpcLaneFollower {
public:
    explicit NpcLaneFollower(NpcLaneFollowerConfig config = {});

    // Copies lane geometry and signal identity. ground must outlive the next
    // rebuild/clear; its owner should retain the package's shared_ptr. Adjacent
    // endpoints within the loader's 0.15m tolerance are joined continuously.
    // Invalid topology/config/start offsets throw invalid_argument and leave
    // this follower cleared, never driving an old topology after a failed swap.
    // An open route must end at an explicit terminal unless destination_end is
    // explicitly requested for a planned trip. A loop validates last->first.
    void rebuild(const TrafficNetwork& network, const GroundQuery& ground,
                 std::vector<std::uint32_t> route_lane_ids,
                 bool loop = false, double start_offset_m = 0.0,
                 bool destination_end = false);
    // Install a new legal suffix beginning at the CURRENT lane. Position,
    // offset, speed, total travelled distance and valid stopline commitment
    // survive. A committed entry cannot switch its immediate connector. Invalid
    // requests throw invalid_argument and leave this follower unchanged.
    void reroute(const TrafficNetwork& network,
                 std::vector<std::uint32_t> route_lane_ids,
                 bool loop = false, bool destination_end = true);
    // Only the host's completed, collision-checked lateral blend may call this.
    // Requires an authored same-direction lane-change window with matching arc
    // stations, no active stopline commitment and supported target geometry.
    // Preserves speed/travelled distance; false leaves all state unchanged.
    [[nodiscard]] bool complete_lane_change(
        const TrafficNetwork& network, std::vector<std::uint32_t> target_route,
        double target_lane_offset_m, bool destination_end = true);
    void reset(double start_offset_m = 0.0);
    void clear() noexcept;

    // Signals are authoritative at the START of dt. Green is used only for its
    // reported remaining_seconds; expiration inside a large step fails to red
    // until the caller supplies a fresh snapshot. Missing/conflicting heads,
    // yellow, unknown, red and disabled signal snapshots all prohibit entry.
    // enabled=false freezes route progress and immediately publishes speed=0.
    // blocked_distance_m is from the CURRENT CENTRE along the route to an
    // obstacle boundary; front_extent+stop_margin are subtracted here. nullopt
    // means no obstacle, not infinity. Invalid dt (outside [0,10]s)/distance
    // freezes the anchor. Large valid dt is internally substepped at <=1/60s.
    const NpcLaneFollowerState& step(
        double dt_seconds, std::span<const TrafficSignalSnapshot> signals,
        bool enabled = true,
        std::optional<double> blocked_distance_m = std::nullopt);

    [[nodiscard]] const NpcLaneFollowerState& state() const noexcept { return state_; }
    [[nodiscard]] const NpcLaneFollowerConfig& config() const noexcept { return config_; }
    [[nodiscard]] double route_length_m() const noexcept { return route_length_m_; }
    [[nodiscard]] bool destination_reached() const noexcept {
        return destination_end_ && state_.stopped
            && state_.stop_reason == NpcLaneStopReason::Terminal;
    }
    // Geometry/ground-only lookahead for the host's collision guard; this does
    // not authorize movement through signals or mutate controller progress.
    [[nodiscard]] std::optional<NpcLaneSample> sample_ahead(double distance_m) const;
    // Host-authorized low-speed escape only. This moves a stopped follower
    // backwards on its current authored lane without crossing a lane boundary
    // or an active stop-line commitment. The host must sweep the body against
    // rear traffic/static collision before calling it. It is deliberately not
    // a graph edge and therefore cannot invent reverse travel through a junction.
    [[nodiscard]] std::optional<NpcLaneSample> sample_behind(double distance_m) const;
    [[nodiscard]] bool retreat_for_obstacle(double distance_m);

private:
    struct RouteLane {
        std::uint32_t id = 0;
        std::uint32_t signal_group_id = 0;
        double speed_limit_mps = 0.0;
        double start_m = 0.0;
        double length_m = 0.0;
        std::vector<GroundPointEnu> points;
        std::vector<double> point_offsets_m;
    };
    struct SignalIdentity {
        std::uint32_t id = 0;
        std::uint32_t group_id = 0;
    };

    [[nodiscard]] std::optional<NpcLaneSample> sample_at(double progress_m) const;
    [[nodiscard]] bool green(std::uint32_t group_id,
                             std::span<const TrafficSignalSnapshot> signals,
                             double elapsed_seconds) const;
    void update_commitment(double old_progress, double new_progress,
                           std::span<const TrafficSignalSnapshot> signals,
                           double elapsed_seconds);
    void stop(NpcLaneStopReason reason, bool safety_clamped) noexcept;

    NpcLaneFollowerConfig config_;
    std::vector<RouteLane> route_;
    std::vector<SignalIdentity> signal_identities_;
    const GroundQuery* ground_ = nullptr;
    bool loop_ = false;
    bool destination_end_ = false;
    double route_length_m_ = 0.0;
    double progress_m_ = 0.0;
    double reset_progress_m_ = 0.0;
    double committed_stopline_m_ = -1.0;
    double committed_until_m_ = -1.0;
    std::uint32_t committed_lane_id_ = 0;
    NpcLaneFollowerState state_;
};

} // namespace simcore_host
