#include "traffic/npc_route_planner.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace simcore_host;

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

TrafficLane lane(std::uint32_t id, std::vector<GroundPointEnu> points,
                 std::vector<std::uint32_t> successors = {})
{
    TrafficLane result;
    result.id = id;
    result.points = std::move(points);
    result.successors = std::move(successors);
    result.terminal = result.successors.empty();
    result.width_m = 4.0;
    result.speed_limit_mps = 6.0;
    return result;
}

TrafficNetwork network()
{
    TrafficNetwork result;
    result.lanes = {
        lane(10, {{0, 0, 0}, {20, 0, 0}}, {30, 20}),
        lane(20, {{20, 0, 0}, {40, 0, 0}}, {40}),
        lane(30, {{20, 0, 0}, {30, 6, 0}, {40, 0, 0}}, {40}),
        lane(40, {{40, 0, 0}, {70, 0, 0}}, {60, 50}),
        lane(50, {{70, 0, 0}, {70, 30, 0}, {0, 30, 0}, {0, 0, 0}}, {10}),
        lane(60, {{70, 0, 0}, {100, 0, 0}}),
    };
    return result;
}

void test_shortest_successor_route_and_closure_exclusion()
{
    const NpcRoutePlanner planner(network());
    const auto direct = planner.shortest_route(10, 40);
    require(direct && direct->lane_ids == std::vector<std::uint32_t>{10, 20, 40}
                && std::abs(direct->length_m - 70.0) < 1e-9
                && direct->destination_lane_id == 40,
            "shortest route must use physical directed lane length, not successor order");
    const std::uint32_t first_closed[] = {20};
    const auto detour = planner.shortest_route(10, 40, first_closed);
    require(detour && detour->lane_ids == std::vector<std::uint32_t>{10, 30, 40}
                && detour->length_m > direct->length_m,
            "a road closure must choose a connected alternative instead of a lane jump");
    const std::uint32_t all_closed[] = {20, 30};
    require(!planner.shortest_route(10, 40, all_closed),
            "no legal alternative must return no route, never drive through the closure");
    const std::uint32_t goal_closed[] = {40};
    require(!planner.shortest_route(10, 40, goal_closed), "blocked destination must be unreachable");
    const std::uint32_t occupied_closed[] = {10};
    require(planner.shortest_route(10, 40, occupied_closed).has_value(),
            "the already occupied lane remains the route prefix, not a teleport target");
    require(!planner.shortest_route(60, 10), "a terminal lane must not be driven backwards");
    require(!planner.shortest_route(999, 40) && !planner.shortest_route(10, 999),
            "unknown endpoints must return no route");
    const auto same = planner.shortest_route(10, 10);
    require(same && same->lane_ids == std::vector<std::uint32_t>{10},
            "current-lane destination remains an explicit one-lane plan");
}

void test_destinations_are_repeatable_and_cannot_strand_roaming_traffic()
{
    const auto original = network();
    auto reordered = original;
    std::reverse(reordered.lanes.begin(), reordered.lanes.end());
    for (auto& item : reordered.lanes) { std::reverse(item.successors.begin(), item.successors.end()); }
    const NpcRoutePlanner first(original);
    const NpcRoutePlanner second(reordered);
    bool saw_40 = false;
    bool saw_50 = false;
    for (std::uint64_t trip = 0; trip < 64; ++trip) {
        const auto a = first.choose_destination(10, 1001, trip);
        const auto b = second.choose_destination(10, 1001, trip);
        require(a && b && a->lane_ids == b->lane_ids && a->length_m == b->length_m,
                "identical seed/trip must reproduce selection independent of JSON ordering");
        require(a->destination_lane_id == 40 || a->destination_lane_id == 50,
                "prefer long roads and exclude current lane, connectors and terminal sinks");
        saw_40 = saw_40 || a->destination_lane_id == 40;
        saw_50 = saw_50 || a->destination_lane_id == 50;
    }
    require(saw_40 && saw_50, "successive trips must not be a hardcoded single destination");
    const auto avoid_previous = first.choose_destination(10, 1001, 0, {}, 40);
    require(avoid_previous && avoid_previous->destination_lane_id == 50,
            "a distinct reachable long destination should replace the previous one");
    require(!first.choose_destination(60, 1001, 0),
            "an isolated terminal must not manufacture a destination");
    const std::uint32_t cut_return[] = {50};
    require(!first.choose_destination(10, 1001, 0, cut_return),
            "a closure cutting every return path must not strand roaming vehicles");
}

void test_equal_cost_ties_and_invalid_graphs()
{
    auto tied = network();
    tied.lanes[2].points = tied.lanes[1].points;
    const NpcRoutePlanner planner(tied);
    require(planner.shortest_route(10, 40)->lane_ids == std::vector<std::uint32_t>{10, 20, 40},
            "equal costs must choose a stable ascending lane-ID tie break");
    auto broken = network();
    broken.lanes.front().successors.push_back(999);
    bool rejected = false;
    try { const NpcRoutePlanner invalid(broken); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "unknown directed successors must be rejected");
    broken = network();
    broken.lanes[1].points.front().east_m += 1.0;
    rejected = false;
    try { const NpcRoutePlanner invalid(broken); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "declared successor must not bypass endpoint continuity checks");
}

} // namespace

int main()
{
    try {
        test_shortest_successor_route_and_closure_exclusion();
        test_destinations_are_repeatable_and_cannot_strand_roaming_traffic();
        test_equal_cost_ties_and_invalid_graphs();
        std::cout << "npc_route_planner_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_route_planner_test: " << error.what() << '\n';
        return 1;
    }
}
