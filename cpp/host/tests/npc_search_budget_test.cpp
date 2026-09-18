#include "simulation_host.hpp"
#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class NarrowRoadGround final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (std::abs(request.origin_enu.east_m) > 1.6
            || request.origin_enu.north_m < -20.0 || request.origin_enu.north_m > 400.0)
            return std::nullopt;
        return flat_.query_down(request);
    }
private:
    simcore_host::FlatGroundQuery flat_;
};

SimulationHostConfig blocked_fleet_config()
{
    SimulationHostConfig config;
    config.physics_frequency_hz = 60.0;
    // Keep production lease/queue limits: this test must not hide starvation
    // behind the multi-second timeouts used by long navigation scenarios.
    config.command_timeout = std::chrono::milliseconds(250);
    config.hard_command_timeout = std::chrono::milliseconds(1000);
    config.max_command_queue_age = std::chrono::milliseconds(100);
    config.require_client_hello = true;
    config.source_id = "npc-search-budget-test";
    config.map_package_checksum = "fnv1a64:9123456789abcdef";
    config.ground_query = std::make_shared<const NarrowRoadGround>();
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:a123456789abcdef";
    network->lanes = {
        {10, 3.2, 6.0, 0, false, {{0,100,0},{0,150,0},{0,200,0},{0,250,0},{0,300,0},{0,350,0}}, {20}},
        // Reciprocal connectivity gives the autonomous planner a reachable
        // return trip; terminal-only routes intentionally have no destination.
        {20, 3.2, 6.0, 0, false, {{0,350,0},{0,300,0},{0,250,0},{0,200,0},{0,150,0},{0,100,0}}, {10}},
    };
    config.traffic_network = network;
    config.npc_route = {10,20};
    config.npc_route_loop = true;
    config.npc_autonomous = true;
    config.npc_count = 3;
    config.npc_start_offset_m = 20.0;
    config.npc_spacing_m = 80.0;
    for (int index = 0; index < 3; ++index) {
        // Each NPC has room for short safe reverses. A disabled vehicle spans
        // the whole supported road, so a forward bypass must still be rejected
        // while the incremental planner shares work across the fleet.
        const simcore_host::ObbPrism body{{0.0, 125.0 + index * 80.0},
            0.85, 1.5707963267948966, 3.0, 0.70, 0.75};
        simcore_host::RuntimeEntityState wreck{static_cast<std::uint32_t>(3001 + index),
            simcore_host::RuntimeEntityKind::NpcVehicle,
            {"budget-wreck-" + std::to_string(index), body, {}, 0.0, {0.8, 0.0}, 1500.0, 2600.0, 30.0}};
        wreck.damage_percent = 100;
        wreck.collision_event_sequence = 1;
        wreck.recovery_phase = 4;
        config.runtime_entities.push_back(wreck);
    }
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>();
    return config;
}

struct Harness {
    boost::asio::io_context ioc;
    std::string health;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    std::uint64_t timestamp = 0;
    SimulationHost host;

    Harness() : Harness(blocked_fleet_config()) {}

    explicit Harness(SimulationHostConfig config) : host(ioc, std::move(config), {
        [this](const std::string& bytes) {
            simcore::Envelope frame;
            require(frame.ParseFromString(bytes) && frame.has_world_state(), "host must publish atomic world state");
            health = frame.world_state().health().status();
        }, {}, {}}) {}

    simcore::Envelope envelope()
    {
        simcore::Envelope result;
        result.set_schema_version(simcore_host::kProtocolSchemaVersion);
        result.set_source_id("search-budget-unreal");
        result.set_session_id("search-budget-connection-" + std::to_string(generation));
        result.set_sequence(++sequence);
        result.set_map_package_checksum(host.map_package_checksum());
        return result;
    }

    std::uint64_t time()
    {
        const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            SimulationHost::Clock::now().time_since_epoch()).count());
        timestamp = std::max(timestamp + 1, now);
        return timestamp;
    }

    void play()
    {
        ++generation;
        sequence = 0;
        auto message = envelope();
        auto* hello = message.mutable_hello();
        hello->set_build("npc-search-budget-test");
        hello->set_schema(std::string(simcore_host::kProtocolSchemaName));
        for (const char* capability : {"world-state.v2", "control.v2", "simulation-reset.v1", "map-package-checksum.v1"})
            hello->add_capabilities(capability);
        require(host.handle_client_message(message.SerializeAsString(), generation) == ClientMessageResult::HelloAccepted,
            "new connection Hello must be accepted");
        message = envelope();
        message.mutable_simulation_reset()->set_play_session_id("budget-play-" + std::to_string(generation));
        message.mutable_simulation_reset()->set_client_time_ns(time());
        require(host.handle_client_message(message.SerializeAsString(), generation) == ClientMessageResult::SimulationReset,
            "new Play must reset pending searches");
    }

    void tick()
    {
        auto message = envelope();
        auto* control = message.mutable_control_command();
        control->set_mode(simcore::CONTROL_MODE_MANUAL);
        control->set_brake(1.f);
        control->set_gear(simcore::VEHICLE_GEAR_DRIVE);
        control->set_client_time_ns(time());
        require(host.handle_client_message(message.SerializeAsString(), generation) == ClientMessageResult::ControlAccepted,
            "every tick must accept fresh control within production lease limits");
        host.start();
        require(ioc.run_one() == 1, "fixed simulation timer must run");
        host.stop();
        (void)ioc.poll();
        ioc.restart();
        require(health == "active", "incremental NPC work must not put fresh control into SafeStop");
    }
};

std::vector<std::uint32_t> exercise_searches(Harness& harness)
{
    auto previous = harness.host.npc_navigation();
    require(previous.size() == 3, "fixture must create all three vehicle profiles");
    for (const auto& npc : previous)
        require(npc.destination_lane_id != 0 && npc.search_steps == 0
                && npc.local_search_samples == 0 && !npc.local_search_pending
                && npc.next_reverse_probe_m == 0.0 && npc.avoidance_phase == "none"
                && npc.avoidance_reverse_remaining_m == 0.0,
            "new Play must discard pending searches, work counters and reverse state");
    std::vector<std::uint32_t> order;
    std::array<bool, 3> saw_pending{}, resumed_pending{};
    std::array<int, 3> deferred_pending_ticks{};
    bool pending_at_reset = false;
    double maximum_tick_ms = 0.0;
    // Two runs in main take 180--240 ticks in total. Stop with live pending
    // work rather than waiting for all 60 candidates or a full retreat limit.
    for (int tick = 0; tick < 120; ++tick) {
        const auto started = SimulationHost::Clock::now();
        harness.tick();
        maximum_tick_ms = std::max(maximum_tick_ms,
            std::chrono::duration<double, std::milli>(SimulationHost::Clock::now() - started).count());
        const auto current = harness.host.npc_navigation();
        require(current.size() == previous.size(), "incremental work must preserve every NPC identity");
        std::uint64_t fleet_steps = 0, fleet_samples = 0;
        for (std::size_t index = 0; index < current.size(); ++index) {
            const auto& before = previous[index];
            const auto& after = current[index];
            require(after.entity_id == 1001 + index && after.search_steps >= before.search_steps
                    && after.local_search_samples >= before.local_search_samples,
                "search identities and cumulative work counters must remain stable");
            const auto steps = after.search_steps - before.search_steps;
            const auto samples = after.local_search_samples - before.local_search_samples;
            fleet_steps += steps;
            fleet_samples += samples;
            if (steps != 0) order.push_back(after.entity_id);
            require(samples == 0 || steps == 1,
                "a deferred NPC must not consume local-search samples outside its fleet turn");
            saw_pending[index] = saw_pending[index] || after.local_search_pending;
            resumed_pending[index] = resumed_pending[index] || (before.local_search_pending && samples != 0);
            if (before.local_search_pending && after.local_search_pending && steps == 0)
                ++deferred_pending_ticks[index];
            else deferred_pending_ticks[index] = 0;
            require(deferred_pending_ticks[index] <= 3,
                "a continuously pending NPC must receive another turn within one fleet round");
            require(!after.local_bypass_active && !after.changing_lane
                    && after.local_bypasses_completed == 0 && after.lane_changes_completed == 0,
                "short reverses must not authorize an unsupported lateral escape around the road-spanning wreck");

            const auto& entities = harness.host.runtime_entities();
            const auto entity = std::find_if(entities.begin(), entities.end(),
                [&](const auto& value) { return value.entity_id == after.entity_id; });
            require(entity != entities.end(), "every budgeted NPC must retain its physical body");
            const auto* body = std::get_if<simcore_host::ObbPrism>(&entity->collision_proxy.shape);
            require(body && std::abs(body->center_enu.east_m) < 0.05,
                "a failed bypass may retreat along its lane but must not slide sideways off the narrow road");
            for (const auto& other : entities) {
                if (other.entity_id == after.entity_id) continue;
                if (const auto* obstacle = std::get_if<simcore_host::ObbPrism>(&other.collision_proxy.shape))
                    require(!simcore_host::intersect_obb_prisms(*body, *obstacle),
                        "budgeted planning and short reverse motion must not overlap another vehicle");
            }
        }
        require(fleet_steps <= 1, "the entire fleet may execute at most one expensive search step per tick");
        require(fleet_samples <= 48,
            "the whole fleet must stay within 48 geometry/prediction/immediate-recheck samples per tick");
        previous = current;
        const bool all_progressed = std::all_of(resumed_pending.begin(), resumed_pending.end(),
            [](bool value) { return value; });
        const bool still_pending = std::any_of(current.begin(), current.end(),
            [](const auto& npc) { return npc.local_search_pending; });
        if (tick >= 89 && all_progressed && still_pending) {
            pending_at_reset = true;
            break;
        }
    }
    require(pending_at_reset,
        "the short fixture must leave a real incremental search pending immediately before new Play");
    for (std::size_t index = 0; index < previous.size(); ++index) {
        require(saw_pending[index] && resumed_pending[index]
                && previous[index].search_steps >= 2 && previous[index].local_search_samples > 0,
            "every NPC must begin and resume real bounded work without starvation");
        std::cout << "search-budget entity=" << previous[index].entity_id
                  << " steps=" << previous[index].search_steps
                  << " samples=" << previous[index].local_search_samples
                  << " max_search_slice_ms=" << previous[index].local_search_max_slice_ms
                  << " pending=" << previous[index].local_search_pending << '\n';
    }
    // Informational only: includes the real timer wait and is not a flaky
    // machine-speed pass/fail condition. The enforced bound is work per tick.
    std::cout << "search-budget max_tick_ms=" << maximum_tick_ms << " (including timer wait)\n";
    return order;
}

void test_dense_scene_reports_complete_search_cost_within_sample_budget()
{
    constexpr std::uint64_t barrier_count = 800;
    auto config = blocked_fleet_config();
    config.npc_count = 1;
    std::vector<simcore_host::StaticObbCollider> barriers;
    barriers.reserve(barrier_count);
    for (std::uint64_t index = 0; index < barrier_count; ++index) {
        const auto column = index % 10;
        const double east = (column < 5 ? -1.0 : 1.0)
            * (4.0 + 0.65 * static_cast<double>(column % 5));
        const double north = 108.0 + 0.8 * static_cast<double>(index / 10);
        // Hundreds of distinct shapes lie inside the local search region but
        // outside both the supported road and the original transverse wreck.
        // Scene assembly must inspect them even though they cannot unblock or
        // intersect the NPC's existing forward/reverse corridor.
        barriers.push_back({"dense-search-barrier-" + std::to_string(index),
            simcore_host::StaticColliderSemantic::Barrier,
            {{east, north}, 0.85, 0.0, 0.20, 0.20, 0.75}, {0.8, 0.0}});
    }
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(std::move(barriers));
    Harness harness(std::move(config));
    harness.play();
    auto diagnostics = harness.host.npc_navigation();
    require(diagnostics.size() == 1 && diagnostics.front().entity_id == 1001,
        "dense-scene fixture must retain one deterministic NPC and its original wreck");
    auto previous = diagnostics.front();
    require(previous.local_search_scene_shapes == 0 && previous.local_search_max_scene_ms == 0.0
                && previous.local_search_max_slice_ms == 0.0,
        "new Play must begin with empty scene-construction diagnostics");
    bool saw_dense_pending_scene = false, resumed_pending = false;
    std::uint64_t largest_scene = 0;
    double maximum_tick_ms = 0.0;
    for (int tick = 0; tick < 72; ++tick) {
        const auto started = SimulationHost::Clock::now();
        // This also verifies fresh commands remain accepted and health stays
        // active with the ordinary 250ms lease throughout dense scene work.
        harness.tick();
        maximum_tick_ms = std::max(maximum_tick_ms,
            std::chrono::duration<double, std::milli>(SimulationHost::Clock::now() - started).count());
        diagnostics = harness.host.npc_navigation();
        require(diagnostics.size() == 1, "dense scene assembly must preserve the configured NPC");
        const auto& after = diagnostics.front();
        require(after.entity_id == previous.entity_id && after.search_steps >= previous.search_steps
                    && after.local_search_samples >= previous.local_search_samples,
            "dense scene assembly must preserve identity and cumulative search counters");
        const auto steps = after.search_steps - previous.search_steps;
        const auto samples = after.local_search_samples - previous.local_search_samples;
        require(steps <= 1 && samples <= 48 && (samples == 0 || steps == 1),
            "dense scene work must retain the per-tick 48-sample geometry/prediction/revalidation budget");
        require(std::isfinite(after.local_search_max_scene_ms)
                    && std::isfinite(after.local_search_max_slice_ms)
                    && after.local_search_max_scene_ms >= previous.local_search_max_scene_ms
                    && after.local_search_max_slice_ms >= previous.local_search_max_slice_ms
                    && after.local_search_max_slice_ms >= after.local_search_max_scene_ms,
            "complete search timing must include finite scene assembly cost and retain cumulative maxima");
        largest_scene = std::max(largest_scene, after.local_search_scene_shapes);
        if (after.local_search_pending && after.local_search_scene_shapes >= barrier_count) {
            saw_dense_pending_scene = true;
            require(after.local_search_max_scene_ms > 0.0 && after.local_search_samples > 0,
                "pending dense searches must report actual scene assembly time and checked samples");
        }
        resumed_pending = resumed_pending || (previous.local_search_pending && samples != 0);
        require(!after.local_bypass_active && !after.changing_lane
                    && after.local_bypasses_completed == 0 && after.lane_changes_completed == 0
                    && after.collision_damage_percent == 0.0,
            "off-road scene density must not manufacture a bypass or collision around the original wreck");
        previous = after;
    }
    require(saw_dense_pending_scene && resumed_pending && previous.search_steps >= 2,
        "dense-scene regression must assemble and resume real pending search work");
    // Times are observational; only workload accounting and timer containment
    // are assertions, so the regression has no machine-dependent speed cutoff.
    std::cout << "dense-search barriers=" << barrier_count
              << " scene_shapes=" << largest_scene
              << " steps=" << previous.search_steps
              << " samples=" << previous.local_search_samples
              << " max_scene_ms=" << previous.local_search_max_scene_ms
              << " max_complete_slice_ms=" << previous.local_search_max_slice_ms
              << " max_tick_ms=" << maximum_tick_ms << '\n';
    harness.play();
    const auto reset = harness.host.npc_navigation().front();
    require(reset.local_search_scene_shapes == 0 && reset.local_search_max_scene_ms == 0.0
                && reset.local_search_max_slice_ms == 0.0 && reset.local_search_samples == 0,
        "Play reset must discard dense-scene metrics together with pending search work");
}

SimulationHostConfig removable_wall_config(
    const std::shared_ptr<simcore_host::CollisionWorld>& world)
{
    auto config = blocked_fleet_config();
    config.npc_count = 1;
    config.runtime_entities.clear();
    config.collision_world = world;
    return config;
}

struct RemovableWallFixture {
    // Retain the mutable owner only in this fixture. The production host sees
    // its usual const view, and replacement occurs only after tick() has
    // stopped/drained the executor. Barrier avoids separate breakable-wall
    // proxies: removing this collider must remove the entire test obstacle.
    std::shared_ptr<simcore_host::CollisionWorld> world =
        std::make_shared<simcore_host::CollisionWorld>(
            std::vector<simcore_host::StaticObbCollider>{{
                "removable-search-barrier", simcore_host::StaticColliderSemantic::Barrier,
                {{0.0, 124.0}, 0.85, 1.5707963267948966, 3.0, 0.70, 0.75}, {0.8, 0.0}}});
    Harness harness{removable_wall_config(world)};
};

void test_removed_blocker_releases_avoidance(bool remove_during_pending)
{
    RemovableWallFixture fixture;
    auto& harness = fixture.harness;
    harness.play();
    const auto initial = harness.host.npc_navigation().front();
    const auto checksum = harness.host.map_package_checksum();
    const auto generation = harness.generation;
    bool saw_backing = false, ready_to_remove = false;
    NpcNavigationSnapshot before_removal;
    for (int tick = 0; tick < 150; ++tick) {
        harness.tick();
        const auto navigation = harness.host.npc_navigation();
        require(navigation.size() == 1, "removable-wall fixture must retain its single NPC");
        before_removal = navigation.front();
        saw_backing = saw_backing || before_removal.avoidance_phase == "backing";
        require(!before_removal.local_bypass_active && !before_removal.changing_lane,
            "the road-spanning barrier must not permit a lateral escape before removal");
        if (saw_backing && before_removal.avoidance_phase == "ready_to_pass"
            && before_removal.local_search_pending == remove_during_pending) {
            ready_to_remove = true;
            break;
        }
    }
    require(ready_to_remove && before_removal.avoidance_reverse_remaining_m == 0.0
            && before_removal.route_distance_travelled_m
                < initial.route_distance_travelled_m - 0.45,
        "fixture must finish a real half-metre reverse before removing its blocker");
    if (remove_during_pending)
        require(before_removal.local_search_samples > 0 && before_removal.preserving_bypass_space,
            "pending-removal regression must start with real search work and a retained stop anchor");

    // Do not reconnect, reset Play, reload the map, or replace the shared
    // pointer. Only the obstacle disappears between two quiescent ticks.
    *fixture.world = simcore_host::CollisionWorld{};
    require(fixture.world->static_collider_count() == 0,
        "fixture must remove its only static obstacle without a map generation change");
    bool released = false;
    int release_tick = 0;
    double previous_forward_distance = before_removal.route_distance_travelled_m;
    for (int tick = 1; tick <= 60; ++tick) {
        harness.tick();
        const auto navigation = harness.host.npc_navigation();
        require(navigation.size() == 1, "obstacle removal must preserve the existing NPC");
        const auto& after = navigation.front();
        require(harness.generation == generation && harness.host.map_package_checksum() == checksum
                && after.entity_id == before_removal.entity_id
                && after.destinations_selected == before_removal.destinations_selected
                && after.destination_lane_id == before_removal.destination_lane_id
                && after.route == before_removal.route
                && after.search_steps >= before_removal.search_steps
                && after.local_search_samples >= before_removal.local_search_samples,
            "avoidance cancellation must preserve the same Play, map, route and cumulative diagnostics");
        require(!after.local_bypass_active && !after.changing_lane
                && after.local_bypasses_completed == 0 && after.lane_changes_completed == 0,
            "a vanished blocker must not cause adoption or completion of an unnecessary bypass");
        const bool idle = !after.local_search_pending && after.avoidance_phase == "none"
            && after.avoidance_reverse_remaining_m == 0.0 && after.next_reverse_probe_m == 0.0
            && !after.preserving_bypass_space;
        if (idle && !released) {
            released = true;
            release_tick = tick;
        }
        if (tick == 24)
            require(released,
                "vanished obstacle must release pending work, reverse state and stop anchor within 0.4 simulated seconds");
        if (released) {
            require(idle && after.lane_change_wait_reason.empty(),
                "cleared avoidance must remain idle without stale waiting or reverse state");
            require(after.route_distance_travelled_m >= previous_forward_distance - 1.0e-9,
                "normal resumption must move forward rather than continue a stale reverse");
        }
        previous_forward_distance = after.route_distance_travelled_m;
        const auto& entities = harness.host.runtime_entities();
        const auto entity = std::find_if(entities.begin(), entities.end(),
            [&](const auto& value) { return value.entity_id == after.entity_id; });
        require(entity != entities.end(), "resuming NPC must retain its physical body");
        const auto* body = std::get_if<simcore_host::ObbPrism>(&entity->collision_proxy.shape);
        require(body && std::abs(body->center_enu.east_m) < 0.05
                && after.collision_damage_percent == before_removal.collision_damage_percent,
            "cancelled search must resume on the original lane without lateral escape or collision damage");
    }
    require(released && previous_forward_distance > before_removal.route_distance_travelled_m + 0.25,
        "clearing the stop anchor must produce actual forward travel, not only idle diagnostics");
    std::cout << "removed-blocker phase=" << (remove_during_pending ? "pending" : "ready_to_pass")
              << " release_ticks=" << release_tick << '\n';
}
} // namespace

int main()
{
    try {
        Harness harness;
        harness.play();
        const auto first = exercise_searches(harness);
        harness.play();
        require(exercise_searches(harness) == first,
            "new Play must discard pending work and reproduce the deterministic fleet turn order");
        test_removed_blocker_releases_avoidance(true);
        test_removed_blocker_releases_avoidance(false);
        test_dense_scene_reports_complete_search_cost_within_sample_budget();
        std::cout << "npc_search_budget_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_search_budget_tests: " << error.what() << '\n';
        return 1;
    }
}
