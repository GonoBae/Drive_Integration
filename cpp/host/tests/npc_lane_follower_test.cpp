#include "traffic/npc_lane_follower.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace simcore_host;

constexpr double step_seconds = 1.0 / 60.0;
constexpr double tolerance = 1.0e-7;

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

bool near(double first, double second, double epsilon = tolerance)
{
    return std::isfinite(first) && std::isfinite(second)
        && std::abs(first - second) <= epsilon;
}

void require_position(const NpcLaneSample& sample, double east, double north,
                      double up, const char* message)
{
    require(near(sample.position_enu.east_m, east)
                && near(sample.position_enu.north_m, north)
                && near(sample.position_enu.up_m, up), message);
}

TrafficLane lane(std::uint32_t id, std::vector<GroundPointEnu> points,
                 std::vector<std::uint32_t> successors = {},
                 double speed_limit_mps = 6.0, std::uint32_t group = 0,
                 bool terminal = true)
{
    TrafficLane result;
    result.id = id;
    result.width_m = 4.0;
    result.speed_limit_mps = speed_limit_mps;
    result.signal_group_id = group;
    result.terminal = terminal;
    result.points = std::move(points);
    result.successors = std::move(successors);
    return result;
}

TrafficNetwork straight_network(double length = 1000.0)
{
    TrafficNetwork result;
    std::vector<GroundPointEnu> points{{0, 0, 0}};
    for (double offset = 100.0; offset < length; offset += 100.0) {
        points.push_back({offset, 0, 0});
    }
    points.push_back({length, 0, 0});
    result.lanes.push_back(lane(10, std::move(points)));
    return result;
}

TrafficNetwork signal_network()
{
    TrafficNetwork result;
    result.lanes = {
        lane(10, {{0, 0, 0}, {30, 0, 0}}, {20}, 6.0, 1, false),
        lane(20, {{30, 0, 0}, {60, 0, 0}}, {30}, 6.0, 0, false),
        lane(30, {{60, 0, 0}, {120, 0, 0}}),
    };
    result.signals = {
        {101, 1, {30, 2, 0}, 90.0},
        {102, 1, {30, -2, 0}, 90.0},
    };
    return result;
}

TrafficNetwork v2_signal_network()
{
    TrafficNetwork result;
    result.format_version = 2;
    result.lanes = {
        lane(10, {{0, 0, 0}, {30, 0, 0}}, {20}, 6.0, 4096, false),
        lane(20, {{30, 0, 0}, {60, 0, 0}}, {30}, 6.0, 0, false),
        lane(30, {{60, 0, 0}, {120, 0, 0}}),
    };
    result.signals = {
        {101, 4096, {30, 2, 0}, 90.0, 64},
        {102, 4096, {30, -2, 0}, 90.0, 64},
    };
    TrafficSignalPlan plan;
    plan.id = 64;
    plan.groups = {4096};
    plan.phases = {
        {1000, {}, {}},
        {5000, {4096}, {}},
        {1000, {}, {4096}},
        {1000, {}, {}},
    };
    plan.cycle_ms = 8000;
    result.signal_plans.push_back(std::move(plan));
    return result;
}

std::vector<TrafficSignalSnapshot> signal_snapshots(
    SignalAspect aspect, double remaining_seconds = 100.0)
{
    // A green with the default wire countdown of zero is intentionally NOT a
    // valid green. Fixtures must supply their authoritative validity horizon.
    return {
        {101, 1, aspect, {30, 2, 0}, 90.0, remaining_seconds},
        {102, 1, aspect, {30, -2, 0}, 90.0, remaining_seconds},
    };
}

class RampGround final : public GroundQuery {
public:
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override
    {
        const double height = std::min(request.origin_enu.east_m, 3.0) * 4.0 / 3.0;
        return FlatGroundQuery(height).query_down(request);
    }
};

class SwitchableGround final : public GroundQuery {
public:
    bool available = true;

    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override
    {
        if (!available) { return std::nullopt; }
        return FlatGroundQuery{}.query_down(request);
    }
};

void require_rebuild_rejects(NpcLaneFollower& follower, const TrafficNetwork& bad_network,
                             const GroundQuery& ground, std::vector<std::uint32_t> route,
                             bool loop, const char* message, double offset = 0.0);

void test_straight_acceleration_speed_and_ground_anchor()
{
    const FlatGroundQuery ground;
    NpcLaneFollower follower;
    follower.rebuild(straight_network(), ground, {10});
    require(follower.state().valid && follower.state().stopped,
            "rebuild must publish a supported stationary anchor");
    require_position(follower.state(), 0, 0, 0,
                     "anchor must be ground level, not an OBB half-height offset");
    require(near(follower.route_length_m(), 1000.0), "straight route length must be metres");

    double previous_speed = 0.0;
    double previous_distance = 0.0;
    for (int index = 0; index < 600; ++index) {
        const auto& state = follower.step(step_seconds, {});
        require(state.valid && !state.safety_clamped,
                "unobstructed straight acceleration must remain supported and smooth");
        require(state.speed_mps >= previous_speed - tolerance
                    && state.speed_mps - previous_speed
                        <= follower.config().acceleration_mps2 * step_seconds + tolerance,
                "normal acceleration must obey its finite rate");
        require(state.speed_mps <= follower.config().max_speed_mps + tolerance,
                "NPC speed must not exceed its configured maximum");
        require(state.distance_travelled_m >= previous_distance,
                "forward route progress must be monotonic");
        require_position(state, state.distance_travelled_m, 0, 0,
                         "straight arc-length progress must match the ground anchor");
        require(near(state.heading_deg, 90.0), "eastbound heading must use navigation degrees");
        previous_speed = state.speed_mps;
        previous_distance = state.distance_travelled_m;
    }
    require(near(follower.state().speed_mps, follower.config().max_speed_mps),
            "unobstructed cruise must reach, not asymptotically miss, its speed target");
}

void test_polyline_and_sloped_three_dimensional_arc_length()
{
    const FlatGroundQuery ground;
    TrafficNetwork curve;
    curve.lanes.push_back(lane(10, {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {20, 10, 0}}));
    NpcLaneFollower follower;
    follower.rebuild(curve, ground, {10});
    require(near(follower.route_length_m(), 30.0),
            "a bend must sum authored segment lengths rather than endpoint distance");
    const auto first = follower.sample_ahead(5.0);
    const auto second = follower.sample_ahead(15.0);
    const auto third = follower.sample_ahead(25.0);
    require(first && second && third, "lookahead must sample each polyline segment");
    require_position(*first, 5, 0, 0, "first curve segment must interpolate by arc length");
    require_position(*second, 10, 5, 0, "middle curve segment must not cut the corner");
    require_position(*third, 15, 10, 0, "final curve segment must preserve accumulated distance");
    require(near(first->heading_deg, 90) && near(second->heading_deg, 0)
                && near(third->heading_deg, 90),
            "lookahead heading must follow its local authored segment");
    require(near(follower.state().distance_travelled_m, 0.0),
            "geometry-only lookahead must never advance the controller");

    const RampGround ramp;
    TrafficNetwork slope;
    slope.lanes.push_back(lane(10, {{0, 0, 0}, {3, 0, 4}, {3, 12, 4}}));
    // This deliberately steep mathematical fixture uses a small footprint so
    // every support ray remains within its finite vertical query range. The
    // default 2.2m vehicle front is tested separately at signals and terminals.
    NpcLaneFollowerConfig slope_config;
    slope_config.front_extent_m = 0.5;
    slope_config.half_width_m = 0.5;
    NpcLaneFollower slope_follower(slope_config);
    slope_follower.rebuild(slope, ramp, {10});
    require(near(slope_follower.route_length_m(), 17.0),
            "sloped speed and progress must use 3D length, not XY length");
    const auto uphill = slope_follower.sample_ahead(2.5);
    const auto level = slope_follower.sample_ahead(10.0);
    require(uphill && level, "sloped path must retain ground-supported lookahead");
    require_position(*uphill, 1.5, 0, 2, "half of a 3-4-5 slope must be a 2.5m sample");
    require_position(*level, 3, 5, 4, "slope length must be consumed before its next segment");
    slope_follower.reset(2.5);
    require_position(slope_follower.state(), 1.5, 0, 2,
                     "nonzero reset offset must use the same 3D route metric");
    require(near(slope_follower.state().distance_travelled_m, 0),
            "reset offset must not count as distance travelled since reset");
}

void test_lower_successor_limit_is_anticipated_without_hard_clamp()
{
    const FlatGroundQuery ground;
    TrafficNetwork network;
    network.lanes = {
        lane(10, {{0, 0, 0}, {60, 0, 0}}, {20}, 6.0, 0, false),
        lane(20, {{60, 0, 0}, {100, 0, 0}}, {}, 1.5),
    };
    NpcLaneFollower follower;
    follower.rebuild(network, ground, {10, 20});
    bool braked_on_approach = false;
    bool entered_successor = false;
    double previous_speed = 0;
    for (int index = 0; index < 1600; ++index) {
        const auto& state = follower.step(step_seconds, {});
        require(!state.safety_clamped,
                "a known lower lane limit must be anticipated, not corrected by a hard clamp");
        require(state.speed_mps - previous_speed
                    <= follower.config().acceleration_mps2 * step_seconds + tolerance,
                "lane-limit handling must preserve bounded acceleration");
        require(previous_speed - state.speed_mps
                    <= follower.config().braking_mps2 * step_seconds + tolerance,
                "lane-limit handling must preserve bounded braking");
        if (state.lane_id == 10 && state.position_enu.east_m > 50.0
            && state.speed_mps < 5.9) {
            braked_on_approach = true;
        }
        if (state.lane_id == 20) {
            entered_successor = true;
            require(state.speed_mps <= 1.5 + tolerance,
                    "the lower speed limit must already hold on successor entry");
        }
        previous_speed = state.speed_mps;
        if (state.position_enu.east_m > 70.0) { break; }
    }
    require(braked_on_approach && entered_successor,
            "controller must brake upstream and still make progress into the slow lane");
}

void require_signal_stop(const std::vector<TrafficSignalSnapshot>& signals,
                         const char* message)
{
    const FlatGroundQuery ground;
    NpcLaneFollower follower;
    follower.rebuild(signal_network(), ground, {10, 20, 30});
    const double safe_centre = 30.0 - follower.config().front_extent_m
        - follower.config().stop_margin_m;
    for (int index = 0; index < 1200; ++index) {
        const auto& state = follower.step(step_seconds, signals);
        require(state.position_enu.east_m <= safe_centre + tolerance, message);
        require(!state.stopline_committed, "a prohibited signal must never grant commitment");
    }
    require(follower.state().stopped && near(follower.state().speed_mps, 0)
                && follower.state().stop_reason == NpcLaneStopReason::Signal, message);
    require(near(follower.state().position_enu.east_m, safe_centre, 0.02),
            "a signal stop must use front extent plus margin, not the centre stopline");
    const auto beyond_stopline = follower.sample_ahead(5.0);
    require(beyond_stopline && beyond_stopline->position_enu.east_m > 30.0,
            "lookahead is geometry-only and must not imply signal movement authorization");
}

void test_signals_fail_closed_for_aspects_missing_conflicting_and_expired_heads()
{
    require_signal_stop(signal_snapshots(SignalAspect::Red), "red must stop before the line");
    require_signal_stop(signal_snapshots(SignalAspect::Yellow), "yellow must prohibit new entry");
    require_signal_stop(signal_snapshots(SignalAspect::Unknown), "unknown must prohibit new entry");
    require_signal_stop({}, "a missing signal snapshot must fail closed");
    auto missing = signal_snapshots(SignalAspect::Green);
    missing.pop_back();
    require_signal_stop(missing, "one missing authored signal head must fail closed");
    auto conflicting = signal_snapshots(SignalAspect::Green);
    conflicting.back().aspect = SignalAspect::Red;
    require_signal_stop(conflicting, "conflicting heads must not select the permissive aspect");
    auto duplicate = signal_snapshots(SignalAspect::Green);
    duplicate.push_back(duplicate.front());
    require_signal_stop(duplicate, "duplicate signal head identity must fail closed");
    auto wrong_group = signal_snapshots(SignalAspect::Green);
    wrong_group.back().group_id = 2;
    require_signal_stop(wrong_group, "a known head with a foreign group must fail closed");
    require_signal_stop(signal_snapshots(SignalAspect::Green, 0.0),
                        "zero-duration green must not authorize a disabled or stale signal");
    require_signal_stop(signal_snapshots(SignalAspect::Green, -1.0),
                        "negative green validity must fail closed");
    require_signal_stop(signal_snapshots(SignalAspect::Green,
                                        std::numeric_limits<double>::quiet_NaN()),
                        "nonfinite green validity must fail closed");
}

void test_green_resume_and_front_crossing_commitment_survives_yellow()
{
    const FlatGroundQuery ground;
    NpcLaneFollower follower;
    follower.rebuild(signal_network(), ground, {10, 20, 30});
    const auto red = signal_snapshots(SignalAspect::Red);
    const auto green = signal_snapshots(SignalAspect::Green);
    const auto yellow = signal_snapshots(SignalAspect::Yellow);
    for (int index = 0; index < 1200; ++index) { follower.step(step_seconds, red); }
    const double stopped_at = follower.state().position_enu.east_m;
    follower.step(1.0, green);
    require(follower.state().position_enu.east_m > stopped_at
                && follower.state().speed_mps > 0,
            "fresh green must resume a stationary signal hold");

    follower.reset(27.3);
    for (int index = 0; index < 300 && !follower.state().stopline_committed; ++index) {
        follower.step(step_seconds, green);
    }
    require(follower.state().stopline_committed
                && follower.state().committed_lane_id == 10,
            "front crossing on green must remember the controlled approach lane");
    require(follower.state().position_enu.east_m < 30.0
                && follower.state().position_enu.east_m + follower.config().front_extent_m
                    >= 30.0 - tolerance,
            "commitment must use the front crossing, not wait for the centre crossing");
    bool crossed_on_commitment = false;
    bool cleared_connector = false;
    for (int index = 0; index < 1200; ++index) {
        const auto& state = follower.step(step_seconds, yellow);
        require(state.stop_reason != NpcLaneStopReason::Signal,
                "yellow must not revoke a legitimately committed crossing");
        if (state.lane_id == 20) {
            crossed_on_commitment = true;
            require(state.stopline_committed,
                    "green commitment must survive throughout its immediate connector");
        }
        if (state.lane_id == 30) {
            cleared_connector = true;
            require(!state.stopline_committed && state.committed_lane_id == 0,
                    "commitment must expire after its immediate uncontrolled connector");
            break;
        }
    }
    require(crossed_on_commitment && cleared_connector,
            "committed vehicle must clear the controlled crossing instead of stopping inside it");
}

void test_v2_maximum_controller_and_group_drive_the_real_follower_path()
{
    const FlatGroundQuery ground;
    const auto network = v2_signal_network();
    NpcLaneFollower follower;
    follower.rebuild(network, ground, {10, 20, 30});
    const auto red = network.signals_at(0);
    require(red.size() == 2 && red[0].group_id == 4096
                && red[0].controller_id == 64 && red[0].aspect == SignalAspect::Red,
            "version 2 runtime snapshots must retain maximum bounded group/controller IDs");
    for (int index = 0; index < 1200; ++index) { follower.step(step_seconds, red); }
    const double stopped_at = follower.state().position_enu.east_m;
    require(follower.state().stopped
                && follower.state().stop_reason == NpcLaneStopReason::Signal,
            "version 2 red must stop the actual NPC follower at its controlled approach");

    const auto green = network.signals_at(1000000000ULL);
    require(green.size() == 2 && green[0].aspect == SignalAspect::Green
                && green[0].remaining_seconds == 5.0,
            "version 2 data-driven green must provide a finite authorization horizon");
    follower.step(1.0, green);
    require(follower.state().position_enu.east_m > stopped_at
                && follower.state().speed_mps > 0,
            "the NPC follower must resume on a valid v2 high-group/high-controller green");

    auto bad_group = network;
    bad_group.lanes[0].signal_group_id = 4097;
    NpcLaneFollower rejected;
    require_rebuild_rejects(rejected, bad_group, ground, {10, 20, 30}, false,
                            "NPC must reject v2 route groups above the runtime bound");
    auto bad_controller = network;
    bad_controller.signals[0].controller_id = 65;
    require_rebuild_rejects(rejected, bad_controller, ground, {10, 20, 30}, false,
                            "NPC must reject v2 signal controllers above the wire bound");
    auto unsupported = network;
    unsupported.format_version = 3;
    require_rebuild_rejects(rejected, unsupported, ground, {10, 20, 30}, false,
                            "NPC must reject unsupported traffic network versions");
}

void test_large_steps_red_and_green_expiration_do_not_tunnel()
{
    const FlatGroundQuery ground;
    NpcLaneFollower follower;
    follower.rebuild(signal_network(), ground, {10, 20, 30});
    const double safe_centre = 30.0 - follower.config().front_extent_m
        - follower.config().stop_margin_m;
    const auto red = signal_snapshots(SignalAspect::Red);
    for (int index = 0; index < 4; ++index) {
        const auto& state = follower.step(10.0, red);
        require(state.position_enu.east_m <= safe_centre + tolerance
                    && !state.stopline_committed,
                "maximum valid dt must not tunnel through a red stopline");
    }
    require(follower.state().stopped, "substepped large dt must settle at a red stop");
    follower.reset();
    follower.step(10.0, signal_snapshots(SignalAspect::Green, 0.1));
    require(follower.state().position_enu.east_m <= safe_centre + tolerance
                && !follower.state().stopline_committed,
            "green expiring inside a large dt must not authorize its remaining substeps");
}

void test_terminal_stop_and_blocked_centre_distance_semantics()
{
    const FlatGroundQuery ground;
    NpcLaneFollower follower;
    follower.rebuild(straight_network(20.0), ground, {10});
    for (int index = 0; index < 1200; ++index) { follower.step(step_seconds, {}); }
    const double clearance = follower.config().front_extent_m + follower.config().stop_margin_m;
    require(follower.state().stopped && near(follower.state().speed_mps, 0)
                && follower.state().stop_reason == NpcLaneStopReason::Terminal,
            "explicit route terminal must stop instead of wrapping or extrapolating");
    require(near(follower.state().position_enu.east_m, 20.0 - clearance, 0.02),
            "terminal stop must keep the front, not merely the centre, inside the route");

    follower.rebuild(straight_network(), ground, {10});
    constexpr double obstacle_boundary = 12.0;
    for (int index = 0; index < 1200; ++index) {
        const double centre_distance = obstacle_boundary - follower.state().position_enu.east_m;
        const auto& state = follower.step(step_seconds, {}, true, centre_distance);
        require(state.position_enu.east_m <= obstacle_boundary - clearance + tolerance,
                "blocked distance must subtract front extent and margin exactly once");
    }
    require(follower.state().stopped && follower.state().stop_reason == NpcLaneStopReason::Blocked,
            "a persistent route obstacle must publish a blocked stop");
    require(near(follower.state().position_enu.east_m, obstacle_boundary - clearance, 0.02),
            "blocked distance is measured from the current centre, not the initial anchor");
    const double blocked_at = follower.state().position_enu.east_m;
    follower.step(1.0, {}, true, std::nullopt);
    require(follower.state().position_enu.east_m > blocked_at,
            "nullopt must remove the obstacle hold and allow acceleration");
    const double late_obstacle_at = follower.state().position_enu.east_m;
    follower.step(step_seconds, {}, true, 0.0);
    require(near(follower.state().position_enu.east_m, late_obstacle_at)
                && near(follower.state().speed_mps, 0)
                && follower.state().safety_clamped,
            "a late zero-distance obstacle must stop in place, never reverse or pass it");
}

void test_disabled_missing_ground_and_invalid_inputs_freeze_safely()
{
    SwitchableGround ground;
    NpcLaneFollower follower;
    follower.rebuild(straight_network(), ground, {10});
    follower.step(2.0, {});
    const double before_disable = follower.state().distance_travelled_m;
    for (int index = 0; index < 5; ++index) { follower.step(10.0, {}, false); }
    require(near(follower.state().distance_travelled_m, before_disable)
                && near(follower.state().speed_mps, 0)
                && follower.state().stop_reason == NpcLaneStopReason::Disabled,
            "disabled traffic must freeze progress and publish zero speed immediately");
    follower.step(step_seconds, {});
    require(follower.state().distance_travelled_m > before_disable,
            "re-enabling must resume from the frozen anchor without a catch-up jump");

    ground.available = false;
    const auto before_ground_loss = follower.state();
    follower.step(1.0, {});
    require(near(follower.state().distance_travelled_m, before_ground_loss.distance_travelled_m)
                && near(follower.state().speed_mps, 0)
                && follower.state().stop_reason == NpcLaneStopReason::NoGround,
            "missing ground must not fall back to authored or z=0 movement");
    require_position(follower.state(), before_ground_loss.position_enu.east_m,
                     before_ground_loss.position_enu.north_m, before_ground_loss.position_enu.up_m,
                     "ground loss must preserve the last confirmed anchor");
    require(!follower.sample_ahead(1.0), "unsupported lookahead must fail closed");
    ground.available = true;
    follower.step(step_seconds, {});
    require(follower.state().distance_travelled_m > before_ground_loss.distance_travelled_m,
            "restored ground must allow a bounded restart");

    const double invalid_values[] = {-1.0, std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::quiet_NaN()};
    for (double invalid : invalid_values) {
        const auto before = follower.state();
        follower.step(invalid, {});
        require(near(follower.state().distance_travelled_m, before.distance_travelled_m)
                    && near(follower.state().speed_mps, 0)
                    && follower.state().stop_reason == NpcLaneStopReason::InvalidInput,
                "invalid dt must fail closed without poisoning route progress");
        follower.step(step_seconds, {});
        const auto before_distance = follower.state();
        follower.step(step_seconds, {}, true, invalid);
        require(near(follower.state().distance_travelled_m, before_distance.distance_travelled_m)
                    && near(follower.state().speed_mps, 0)
                    && follower.state().stop_reason == NpcLaneStopReason::InvalidInput,
                "invalid obstacle distance must freeze rather than masquerade as no obstacle");
    }
    const double before_oversize = follower.state().distance_travelled_m;
    follower.step(10.0001, {});
    require(near(follower.state().distance_travelled_m, before_oversize)
                && follower.state().stop_reason == NpcLaneStopReason::InvalidInput,
            "dt larger than ten seconds must be rejected, not silently truncated");
    follower.step(0.0, {});
    require(near(follower.state().distance_travelled_m, before_oversize),
            "zero dt must not advance the anchor");
}

void require_rebuild_rejects(NpcLaneFollower& follower, const TrafficNetwork& bad_network,
                             const GroundQuery& ground, std::vector<std::uint32_t> route,
                             bool loop, const char* message, double offset)
{
    follower.rebuild(straight_network(), ground, {10});
    follower.step(1.0, {});
    bool rejected = false;
    try {
        follower.rebuild(bad_network, ground, std::move(route), loop, offset);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
    require(!follower.state().valid && near(follower.state().speed_mps, 0)
                && near(follower.route_length_m(), 0) && !follower.sample_ahead(0),
            "failed route swap must clear the old topology and supported anchor");
}

void test_bad_routes_reject_and_clear_including_open_and_loop_closure()
{
    const FlatGroundQuery ground;
    NpcLaneFollower follower;
    require_rebuild_rejects(follower, straight_network(), ground, {}, false,
                           "an empty route must be rejected");
    require_rebuild_rejects(follower, straight_network(), ground, {999}, false,
                           "an unknown lane ID must be rejected");
    auto nonadjacent = signal_network();
    require_rebuild_rejects(follower, nonadjacent, ground, {10, 30}, false,
                           "route IDs must follow declared directed successors");
    auto disconnected = signal_network();
    disconnected.lanes[1].points[0].east_m = 31.0;
    require_rebuild_rejects(follower, disconnected, ground, {10, 20, 30}, false,
                           "successor declaration must not hide disconnected endpoint geometry");
    require_rebuild_rejects(follower, signal_network(), ground, {10, 20}, false,
                           "an open route must end at an explicit terminal");
    require_rebuild_rejects(follower, signal_network(), ground, {10, 20, 30}, true,
                           "a loop must validate its directed last-to-first closure");
    auto bad_closure = signal_network();
    bad_closure.lanes.back().terminal = false;
    bad_closure.lanes.back().successors = {10};
    require_rebuild_rejects(follower, bad_closure, ground, {10, 20, 30}, true,
                           "a declared loop successor must still geometrically close");
    require_rebuild_rejects(follower, straight_network(), ground, {10}, false,
                           "negative start offsets must be rejected", -1.0);
    require_rebuild_rejects(follower, straight_network(), ground, {10}, false,
                           "nonfinite start offsets must be rejected",
                           std::numeric_limits<double>::quiet_NaN());
}

bool same_state(const NpcLaneFollowerState& first, const NpcLaneFollowerState& second)
{
    return first.position_enu.east_m == second.position_enu.east_m
        && first.position_enu.north_m == second.position_enu.north_m
        && first.position_enu.up_m == second.position_enu.up_m
        && first.heading_deg == second.heading_deg && first.lane_id == second.lane_id
        && first.route_index == second.route_index && first.lane_offset_m == second.lane_offset_m
        && first.speed_mps == second.speed_mps
        && first.distance_travelled_m == second.distance_travelled_m
        && first.valid == second.valid && first.stopped == second.stopped
        && first.stop_reason == second.stop_reason
        && first.stopline_committed == second.stopline_committed
        && first.committed_lane_id == second.committed_lane_id
        && first.safety_clamped == second.safety_clamped;
}

void test_loop_lap_distance_and_exact_deterministic_reset()
{
    const FlatGroundQuery ground;
    TrafficNetwork square;
    square.lanes = {
        lane(10, {{0, 0, 0}, {20, 0, 0}}, {20}, 6.0, 0, false),
        lane(20, {{20, 0, 0}, {20, 20, 0}}, {30}, 6.0, 0, false),
        lane(30, {{20, 20, 0}, {0, 20, 0}}, {40}, 6.0, 0, false),
        lane(40, {{0, 20, 0}, {0, 0, 0}}, {10}, 6.0, 0, false),
    };
    NpcLaneFollower follower;
    follower.rebuild(square, ground, {10, 20, 30, 40}, true);
    require(near(follower.route_length_m(), 80), "loop length must include all four directed lanes");
    const auto wrapped = follower.sample_ahead(85.0);
    require(wrapped && wrapped->lane_id == 10, "loop lookahead must wrap geometrically");
    require_position(*wrapped, 5, 0, 0, "wrapped lookahead must retain excess arc distance");

    std::vector<NpcLaneFollowerState> first_run;
    for (int index = 0; index < 5; ++index) { first_run.push_back(follower.step(10.0, {})); }
    require(follower.state().distance_travelled_m > 2.0 * follower.route_length_m(),
            "travelled distance must accumulate complete laps instead of wrapping to lane offset");
    follower.reset();
    require(near(follower.state().distance_travelled_m, 0)
                && near(follower.state().speed_mps, 0) && !follower.state().stopline_committed,
            "reset must remove speed, elapsed distance and signal commitment");
    for (const auto& expected : first_run) {
        require(same_state(follower.step(10.0, {}), expected),
                "identical reset and step sequence must reproduce every published state bit exactly");
    }
    follower.clear();
    require(!follower.state().valid && follower.state().stopped
                && follower.state().stop_reason == NpcLaneStopReason::Uninitialized
                && near(follower.route_length_m(), 0),
            "clear must erase route and publish the uninitialized stop state");
    require(!follower.sample_ahead(0), "cleared follower must not expose stale geometry");
    require(!follower.step(step_seconds, {}).valid, "cleared follower must not move without a rebuild");
}

} // namespace

int main()
{
    try {
        test_straight_acceleration_speed_and_ground_anchor();
        test_polyline_and_sloped_three_dimensional_arc_length();
        test_lower_successor_limit_is_anticipated_without_hard_clamp();
        test_signals_fail_closed_for_aspects_missing_conflicting_and_expired_heads();
        test_green_resume_and_front_crossing_commitment_survives_yellow();
        test_v2_maximum_controller_and_group_drive_the_real_follower_path();
        test_large_steps_red_and_green_expiration_do_not_tunnel();
        test_terminal_stop_and_blocked_centre_distance_semantics();
        test_disabled_missing_ground_and_invalid_inputs_freeze_safely();
        test_bad_routes_reject_and_clear_including_open_and_loop_closure();
        test_loop_lap_distance_and_exact_deterministic_reset();
        std::cout << "npc_lane_follower_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_lane_follower_test: " << error.what() << '\n';
        return 1;
    }
}
