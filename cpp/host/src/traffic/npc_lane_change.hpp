#pragma once

#include "traffic/npc_lane_follower.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace simcore_host {

// Immutable, deterministic kinematic transition between an explicitly authored
// same-direction pair. This is trajectory planning, not a tire/force solver.
// Ground must outlive the plan, as with NpcLaneFollower. The host retains
// responsibility for signal state, swept collisions and adopting the target
// route after completion; a plan is never permission to ignore those guards.
class NpcLaneChangePlan {
public:
    [[nodiscard]] std::uint32_t source_lane_id() const noexcept { return source_id_; }
    [[nodiscard]] std::uint32_t target_lane_id() const noexcept { return target_id_; }
    [[nodiscard]] double source_begin_m() const noexcept { return begin_m_; }
    [[nodiscard]] double source_end_m() const noexcept { return end_m_; }
    [[nodiscard]] bool complete(double source_offset_m) const noexcept;

    // Begin inclusive; before begin or beyond either lane is invalid. End and
    // later offsets map continuously onto target-only geometry so a tick's
    // small overshoot and collision lookahead do not snap back to the source.
    [[nodiscard]] std::optional<double> target_offset(double source_offset_m) const;
    // lane_id/offset identify source before completion, target at/after it.
    // Heading follows the curve derivative; quintic blending gives matching
    // position/heading at both ends instead of a sideways teleport.
    [[nodiscard]] std::optional<NpcLaneSample> sample(double source_offset_m) const;

private:
    friend std::optional<NpcLaneChangePlan> plan_npc_lane_change(
        const TrafficNetwork&, const GroundQuery&, std::uint32_t, double,
        std::uint32_t, double, double, double);
    friend bool npc_lane_change_gap_safe(
        const NpcLaneChangePlan&, double, double,
        std::span<const struct NpcLaneChangeGapVehicle>, double, double);

    std::uint32_t source_id_ = 0;
    std::uint32_t target_id_ = 0;
    double begin_m_ = 0.0;
    double end_m_ = 0.0;
    double mapping_source_begin_m_ = 0.0;
    double mapping_target_begin_m_ = 0.0;
    double mapping_scale_ = 1.0;
    double body_half_length_m_ = 2.2;
    double body_half_width_m_ = 1.0;
    std::vector<GroundPointEnu> source_points_;
    std::vector<GroundPointEnu> target_points_;
    std::vector<double> source_stations_;
    std::vector<double> target_stations_;
    const GroundQuery* ground_ = nullptr;
};

// Length is a source-lane arc distance, normally max(12, 2.5*current_speed).
// A low-speed manoeuvre may use an 8..12m remaining authored window; the host
// still checks its speed, full swept body and gaps. Below 8m/above 80m fails.
// The entire blend must fit in one authored corridor, at least 5m from lane
// endpoints; straight/same-direction geometry and support are rechecked even
// for programmatically constructed (not JSON-loaded) test networks.
[[nodiscard]] std::optional<NpcLaneChangePlan> plan_npc_lane_change(
    const TrafficNetwork& network, const GroundQuery& ground,
    std::uint32_t source_lane_id, double source_offset_m,
    std::uint32_t target_lane_id, double length_m = 12.0,
    double body_half_length_m = 2.2, double body_half_width_m = 1.0);

struct NpcLaneChangeGapVehicle {
    GroundPointEnu position_enu;
    GroundPointEnu velocity_enu_mps;
    // Conservative planar extents in the target-lane frame. The caller must
    // exclude the changing vehicle itself and enlarge rotated OBBs as needed.
    double half_length_m = 2.2;
    double half_width_m = 1.0;
};

// Gap screening, not collision detection. Along the target lane require a
// 2m standstill gap + two-second headway and three-second closing TTC ahead
// and behind. Invalid numbers fail closed, including irrelevant obstacles.
// Objects on another lane or at a different elevation are excluded only after
// finite validation. The host must also sweep the actual changing OBB through
// the complete blend against ground, static and dynamic collision proxies.
[[nodiscard]] bool npc_lane_change_gap_safe(
    const NpcLaneChangePlan& plan, double source_offset_m, double speed_mps,
    std::span<const NpcLaneChangeGapVehicle> other_vehicles,
    double body_half_length_m = 2.2, double body_half_width_m = 1.0);

} // namespace simcore_host
