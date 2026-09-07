#include "traffic/npc_lane_change.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using namespace simcore_host;

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

bool near(double first, double second, double tolerance = 1.0e-7)
{
    return std::isfinite(first) && std::isfinite(second)
        && std::abs(first - second) <= tolerance;
}

TrafficNetwork parallel_network()
{
    TrafficNetwork result;
    TrafficLane first;
    first.id = 10;
    first.width_m = 2.4;
    first.speed_limit_mps = 6.0;
    first.points = {{0, 0, 0}, {100, 0, 0}};
    first.lane_changes = {{20, 10, 90, 10, 90}};
    TrafficLane second = first;
    second.id = 20;
    second.points = {{0, 2.45, 0}, {100, 2.45, 0}};
    second.lane_changes = {{10, 10, 90, 10, 90}};
    result.lanes = {first, second};
    return result;
}

class MissingMiddleGround final : public GroundQuery {
public:
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override
    {
        if (request.origin_enu.north_m > 1.0 && request.origin_enu.north_m < 1.5) {
            return std::nullopt;
        }
        return FlatGroundQuery{}.query_down(request);
    }
};

class MissingWheelGround final : public GroundQuery {
public:
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override
    {
        if (request.origin_enu.north_m < -0.5) { return std::nullopt; }
        return FlatGroundQuery{}.query_down(request);
    }
};

class MutableGround final : public GroundQuery {
public:
    bool available = true;
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override
    {
        return available ? FlatGroundQuery{}.query_down(request) : std::nullopt;
    }
};

class ThrowingGround final : public GroundQuery {
public:
    std::optional<GroundHit> query_down(const GroundQueryRequest&) const override
    {
        throw std::runtime_error("ground unavailable");
    }
};

void test_continuous_quintic_trajectory_and_completion()
{
    const FlatGroundQuery ground;
    const auto plan = plan_npc_lane_change(parallel_network(), ground, 10, 20, 20, 20);
    require(plan.has_value(), "authored, supported, parallel change must plan");
    require(plan->source_lane_id() == 10 && plan->target_lane_id() == 20
                && near(plan->source_begin_m(), 20) && near(plan->source_end_m(), 40),
            "plan must expose deterministic source/target stations");
    const auto start = plan->sample(20);
    const auto middle = plan->sample(30);
    const auto end = plan->sample(40);
    require(start && middle && end, "complete blend must remain supported");
    require(near(start->position_enu.east_m, 20) && near(start->position_enu.north_m, 0)
                && near(start->heading_deg, 90) && start->lane_id == 10,
            "start must exactly match source position, heading and identity");
    require(near(middle->position_enu.north_m, 1.225)
                && middle->heading_deg > 70 && middle->heading_deg < 90,
            "midpoint must smoothly move laterally with curve-tangent heading");
    require(near(end->position_enu.east_m, 40) && near(end->position_enu.north_m, 2.45)
                && near(end->heading_deg, 90) && end->lane_id == 20
                && near(end->lane_offset_m, 40),
            "end must exactly match target position, heading and station");
    require(!plan->complete(39.999) && plan->complete(40),
            "completion must happen at the authored end, not a premature tolerance");
    const auto just_started = plan->sample(20.001);
    const auto nearly_done = plan->sample(39.999);
    require(just_started && nearly_done
                && near(just_started->heading_deg, 90, 1.0e-5)
                && near(nearly_done->heading_deg, 90, 1.0e-5),
            "heading derivative must join without endpoint yaw jumps");
    const auto overshoot = plan->sample(40.1);
    const auto lookahead = plan->sample(52.0);
    require(overshoot && lookahead && near(overshoot->position_enu.north_m, 2.45)
                && near(lookahead->position_enu.east_m, 52)
                && near(*plan->target_offset(52), 52),
            "overshoot and post-completion lookahead must continue on target geometry");
    require(!plan->sample(19.0) && !plan->sample(101.0)
                && !plan->target_offset(std::numeric_limits<double>::quiet_NaN())
                && !plan->complete(std::numeric_limits<double>::infinity()),
            "invalid stations must not extrapolate beyond the lane");
    const auto repeated = plan_npc_lane_change(parallel_network(), ground, 10, 20, 20, 20);
    require(repeated && repeated->sample(27.5)->heading_deg == plan->sample(27.5)->heading_deg
                && repeated->sample(27.5)->position_enu.north_m
                    == plan->sample(27.5)->position_enu.north_m,
            "same plan inputs must reproduce samples bit exactly");
}

void test_invalid_topology_corridors_and_ground_fail_closed()
{
    const FlatGroundQuery ground;
    auto network = parallel_network();
    require(!plan_npc_lane_change(network, ground, 10, 20, 10)
                && !plan_npc_lane_change(network, ground, 10, 20, 99)
                && !plan_npc_lane_change(network, ground, 10, 20, 20, 7.99)
                && !plan_npc_lane_change(network, ground, 10, 20, 20, 81)
                && !plan_npc_lane_change(network, ground, 10, 9.9, 20)
                && !plan_npc_lane_change(network, ground, 10, 80, 20)
                && !plan_npc_lane_change(network, ground, 10,
                    std::numeric_limits<double>::quiet_NaN(), 20),
            "only bounded authored source/target transitions may plan");
    network.lanes[0].lane_changes.clear();
    require(!plan_npc_lane_change(network, ground, 10, 20, 20),
            "mere adjacency must not authorize a lane change");
    network = parallel_network();
    network.lanes[1].points = {{100, 2.45, 0}, {0, 2.45, 0}};
    require(!plan_npc_lane_change(network, ground, 10, 20, 20),
            "opposing traffic lane must not be a lane-change destination");
    network = parallel_network();
    network.lanes[1].signal_group_id = 1;
    require(!plan_npc_lane_change(network, ground, 10, 20, 20),
            "lane change must not bypass a differently controlled approach");
    network = parallel_network();
    network.lanes[0].lane_changes[0].source_begin_m = 2;
    require(!plan_npc_lane_change(network, ground, 10, 20, 20),
            "authored corridor inside endpoint buffer must fail closed");
    network = parallel_network();
    network.lanes[1].points = {{0, 8, 0}, {100, 8, 0}};
    require(!plan_npc_lane_change(network, ground, 10, 20, 20),
            "non-adjacent lane across multiple lanes must not plan");
    network = parallel_network();
    network.lanes[1].points[1].east_m = std::numeric_limits<double>::infinity();
    require(!plan_npc_lane_change(network, ground, 10, 20, 20),
            "non-finite geometry must fail closed");
    require(!plan_npc_lane_change(parallel_network(), MissingMiddleGround{}, 10, 20, 20)
                && !plan_npc_lane_change(parallel_network(), MissingWheelGround{}, 10, 20, 20)
                && !plan_npc_lane_change(parallel_network(), FlatGroundQuery{0.5}, 10, 20, 20)
                && !plan_npc_lane_change(parallel_network(), ThrowingGround{}, 10, 20, 20),
            "centre path and full wheel footprint must both have ground support");
    MutableGround mutable_ground;
    const auto plan = plan_npc_lane_change(parallel_network(), mutable_ground, 10, 20, 20);
    require(plan.has_value(), "initial mutable ground is supported");
    mutable_ground.available = false;
    require(!plan->sample(25), "live sample must recheck ground, not trust old planning support");
}

void test_low_speed_length_fits_a_short_remaining_authored_window()
{
    const FlatGroundQuery ground;
    auto network = parallel_network();
    const auto short_plan = plan_npc_lane_change(network, ground, 10, 81, 20, 8);
    require(short_plan && near(short_plan->source_end_m(),89)
                && near(short_plan->sample(81)->position_enu.north_m,0)
                && near(short_plan->sample(89)->position_enu.north_m,2.45),
        "an 8m low-speed change must fit completely in the remaining authored window");
    require(!plan_npc_lane_change(network, ground, 10, 81, 20, 12)
                && !plan_npc_lane_change(network, ground, 10, 83, 20, 8),
        "a short manoeuvre must not extend the authored lane-change boundary");
}

void test_authored_station_mapping_survives_different_approach_tapers()
{
    const FlatGroundQuery ground;
    auto network = parallel_network();
    const double source_taper = std::hypot(15.0, 0.75);
    const double target_taper = std::hypot(15.0, 3.2);
    network.lanes[0].points = {{0, 0, 0}, {15, 0.75, 0}, {85, 0.75, 0}, {100, 0, 0}};
    network.lanes[1].points = {{0, 0, 0}, {15, 3.2, 0}, {85, 3.2, 0}, {100, 0, 0}};
    network.lanes[0].lane_changes = {
        {20, source_taper + 5, source_taper + 65, target_taper + 5, target_taper + 65}};
    const double begin = source_taper + 20;
    const auto plan = plan_npc_lane_change(network, ground, 10, begin, 20, 12);
    require(plan && near(*plan->target_offset(begin), target_taper + 20),
            "station mapping must account for longer target-lane entry taper");
    const auto start = plan->sample(begin);
    const auto finish = plan->sample(begin + 12);
    require(start && finish && near(start->position_enu.east_m, 35)
                && near(finish->position_enu.east_m, 47)
                && near(finish->position_enu.north_m, 3.2)
                && near(finish->lane_offset_m, target_taper + 32),
            "target adoption must preserve world position rather than source arc station");
}

void test_gap_screening_front_rear_closing_speed_and_invalid_values()
{
    const FlatGroundQuery ground;
    const auto plan = plan_npc_lane_change(parallel_network(), ground, 10, 40, 20, 20);
    require(plan && npc_lane_change_gap_safe(*plan, 40, 6, {}),
            "unoccupied supported target lane must allow gap screening");
    auto vehicle = [](double east, double north, double speed) {
        return NpcLaneChangeGapVehicle{{east, north, 0.85}, {speed, 0, 0}, 2.2, 1.0};
    };
    std::vector<NpcLaneChangeGapVehicle> others{vehicle(50, 2.45, 6)};
    require(!npc_lane_change_gap_safe(*plan, 40, 6, others),
            "close lead vehicle must prohibit cutting into its rear");
    others = {vehicle(70, 2.45, 6)};
    require(npc_lane_change_gap_safe(*plan, 40, 6, others),
            "ample front gap must allow a same-speed target-lane vehicle");
    others = {vehicle(60, 2.45, 0)};
    require(!npc_lane_change_gap_safe(*plan, 40, 6, others),
            "closing TTC must reject stationary vehicle even beyond nominal headway");
    others = {vehicle(30, 2.45, 6)};
    require(!npc_lane_change_gap_safe(*plan, 40, 6, others),
            "near following vehicle must reject a cut-in");
    others = {vehicle(5, 2.45, 6)};
    require(npc_lane_change_gap_safe(*plan, 40, 6, others),
            "distant same-speed follower must permit change");
    others = {vehicle(5, 2.45, 20)};
    require(!npc_lane_change_gap_safe(*plan, 40, 6, others),
            "fast approaching follower must reject otherwise large gap");
    others = {vehicle(40, -4, 6)};
    require(npc_lane_change_gap_safe(*plan, 40, 6, others),
            "unrelated adjacent/opposite lane traffic is not a target-lane gap");
    others = {vehicle(40, 2.45, 6)};
    others[0].position_enu.up_m = 10;
    require(npc_lane_change_gap_safe(*plan, 40, 6, others),
            "vertically separated overpass traffic must not close a ground-level gap");
    others[0].velocity_enu_mps.east_m = std::numeric_limits<double>::quiet_NaN();
    require(!npc_lane_change_gap_safe(*plan, 40, 6, others)
                && !npc_lane_change_gap_safe(*plan, 40, -1, {})
                && !npc_lane_change_gap_safe(*plan, 40,
                    std::numeric_limits<double>::infinity(), {}),
            "invalid object or subject motion must fail closed");
    others = {vehicle(-5, 2.45, 30)};
    require(!npc_lane_change_gap_safe(*plan, 40, 6, others),
            "fast traffic just before target lane start must remain visible to gap guard");
}

void test_vehicle_profile_extents_drive_lane_and_gap_validation()
{
    const FlatGroundQuery ground;
    auto network = parallel_network();
    require(!plan_npc_lane_change(network,ground,10,40,20,12,3.65,1.22),
        "a truck profile must not be planned through a lane narrower than its body");
    network.lanes[0].width_m = network.lanes[1].width_m = 3.2;
    network.lanes[1].points = {{0,3.2,0},{100,3.2,0}};
    const auto truck = plan_npc_lane_change(network,ground,10,40,20,12,3.65,1.22);
    require(truck.has_value(),
        "the same truck profile must plan once the authored lane supports its footprint");
    const std::vector<NpcLaneChangeGapVehicle> lead{
        {{55,3.2,0.85},{6,0,0},2.2,1.0}};
    require(npc_lane_change_gap_safe(*truck,40,6,lead,2.2,1.0)
            && !npc_lane_change_gap_safe(*truck,40,6,lead,3.65,1.22),
        "a gap safe for a sedan must remain unsafe for a longer truck profile");
}

} // namespace

int main()
{
    try {
        test_continuous_quintic_trajectory_and_completion();
        test_invalid_topology_corridors_and_ground_fail_closed();
        test_low_speed_length_fits_a_short_remaining_authored_window();
        test_authored_station_mapping_survives_different_approach_tapers();
        test_gap_screening_front_rear_closing_speed_and_invalid_values();
        test_vehicle_profile_extents_drive_lane_and_gap_validation();
        std::cout << "npc_lane_change_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_lane_change_test: " << error.what() << '\n';
        return 1;
    }
}
