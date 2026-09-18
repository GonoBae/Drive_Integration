#pragma once

#include "traffic/npc_lane_follower.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace simcore_host {
class NpcLocalRoadIndex;
class NpcLocalBypassSearch;

// Cache with the immutable network snapshot. A search must receive the index
// built from its network; the index owns its copied road segments.
[[nodiscard]] std::shared_ptr<const NpcLocalRoadIndex> make_npc_local_road_index(
    const TrafficNetwork& network);

struct NpcLocalBypassDimensions {
    double half_length_m = 2.2;
    double half_width_m = 1.0;
    double minimum_turn_radius_m = 4.8;
};

// A short offset-and-return trajectory on the existing route. Same-flow lane
// markings are not collision boundaries, but opposing-flow space is forbidden.
// Classified right-side room can be used without crossing the centreline. The original follower
// retains route progress and right of way throughout the manoeuvre.
class NpcLocalBypassPlan {
public:
    [[nodiscard]] std::optional<NpcLaneSample> sample(double travelled_m) const;
    [[nodiscard]] bool complete(double travelled_m) const noexcept;
    [[nodiscard]] double begin_m() const noexcept { return begin_m_; }
    [[nodiscard]] double end_m() const noexcept { return begin_m_ + return_begin_m_ + transition_m_; }
    [[nodiscard]] double lateral_offset_m() const noexcept { return lateral_m_; }
    [[nodiscard]] double route_speed_limit_mps() const noexcept { return route_speed_limit_mps_; }
    [[nodiscard]] double extra_stop_margin_m() const noexcept;
    [[nodiscard]] bool borrows_opposing_space() const noexcept { return borrows_opposing_space_; }
    [[nodiscard]] double expected_duration_seconds() const noexcept { return expected_duration_s_; }
    [[nodiscard]] std::uint32_t turn_signal_intent(double travelled_m) const noexcept;

private:
    friend class NpcLocalBypassSearch;
    [[nodiscard]] std::optional<NpcLaneSample> geometry(double distance_m) const;
    [[nodiscard]] bool opposing_footprint(const NpcLaneSample& sample) const;

    NpcLaneFollower reference_;
    const GroundQuery* ground_ = nullptr;
    std::shared_ptr<const NpcLocalRoadIndex> road_index_;
    NpcLocalBypassDimensions dimensions_;
    double begin_m_ = 0.0;
    double lateral_m_ = 0.0;
    double transition_m_ = 0.0;
    double return_begin_m_ = 0.0;
    double route_speed_limit_mps_ = 3.0;
    double expected_duration_s_ = 0.0;
    bool borrows_opposing_space_ = false;
};

enum class NpcLocalBypassSearchStatus { Pending, Found, Exhausted };

struct NpcLocalBypassSearchProgress {
    NpcLocalBypassSearchStatus status;
    std::size_t samples_checked;
};

// Each work unit is one full supported trajectory sample or one predicted
// collision callback. Both passes can yield within a candidate. This bounds
// work count, not wall time: GroundQuery and the callback must also be bounded.
// Ground must outlive this search and any resulting plan. The caller discards
// stale searches when its anchor, map or world-generation policy requires it,
// and rechecks the accepted path against the live world before using it.
class NpcLocalBypassSearch {
public:
    void begin(const TrafficNetwork& network, const GroundQuery& ground,
        const NpcLaneFollower& follower, double last_blocked_distance_m,
        const NpcLocalBypassDimensions& dimensions,
        std::shared_ptr<const NpcLocalRoadIndex> road_index = {});
    [[nodiscard]] NpcLocalBypassSearchProgress advance(
        const std::function<bool(const NpcLaneSample&, double)>& clear,
        std::size_t max_samples = 8);
    [[nodiscard]] NpcLocalBypassSearchStatus status() const noexcept { return status_; }
    [[nodiscard]] const std::optional<NpcLocalBypassPlan>& plan() const noexcept { return plan_; }
    [[nodiscard]] std::size_t total_samples_checked() const noexcept { return total_samples_checked_; }

private:
    enum class Phase { Candidate, InitialSample, Geometry, Prediction };
    bool prepare_candidate();

    NpcLocalBypassSearchStatus status_ = NpcLocalBypassSearchStatus::Exhausted;
    Phase phase_ = Phase::Candidate;
    NpcLaneFollower reference_;
    const GroundQuery* ground_ = nullptr;
    std::shared_ptr<const NpcLocalRoadIndex> road_index_;
    NpcLocalBypassDimensions dimensions_;
    double last_blocked_distance_m_ = 0.0;
    std::size_t next_candidate_ = 0;
    NpcLocalBypassPlan candidate_;
    double length_ = 0.0;
    int sample_count_ = 0;
    int sample_index_ = 0;
    std::size_t prediction_index_ = 0;
    double maximum_stretch_ = 1.0;
    std::optional<NpcLaneSample> previous_;
    std::vector<NpcLaneSample> samples_;
    std::optional<NpcLocalBypassPlan> plan_;
    std::size_t total_samples_checked_ = 0;
};

// At most 60 deterministic candidates / 60m lookahead. The callback must
// check the complete body against static, dynamic and reserved trajectories;
// time is a conservative short prediction horizon, not permission to ignore
// an object after that horizon. The same callback is used for live rechecks.
[[nodiscard]] std::optional<NpcLocalBypassPlan> plan_npc_local_bypass(
    const TrafficNetwork& network, const GroundQuery& ground,
    const NpcLaneFollower& follower, double last_blocked_distance_m,
    const NpcLocalBypassDimensions& dimensions,
    const std::function<bool(const NpcLaneSample&, double)>& clear);

} // namespace simcore_host
