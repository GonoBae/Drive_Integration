#include "simulation_host.hpp"
#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef SIMCORE_TEST_SIGNAL_CITY_MAP_PACKAGE_PATH
#error "npc_navigation_host_tests requires SIMCORE_TEST_SIGNAL_CITY_MAP_PACKAGE_PATH"
#endif

namespace {
constexpr double dt = 1.0 / 120.0;
constexpr std::uint32_t npc_id = 1001;
const std::vector<std::uint32_t> city_loop{1010,1011,1014,3001,2020,2021,2024,3004};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct CityFixture {
    simcore_host::RuntimeMapPackage package;
    std::shared_ptr<const simcore_host::TrafficNetwork> traffic;

    CityFixture()
        : package(simcore_host::load_runtime_map_package(SIMCORE_TEST_SIGNAL_CITY_MAP_PACKAGE_PATH))
        , traffic(std::make_shared<const simcore_host::TrafficNetwork>(
              simcore_host::load_traffic_network(
                  std::filesystem::path(SIMCORE_TEST_SIGNAL_CITY_MAP_PACKAGE_PATH) / "traffic_network.json",
                  package.collision_checksum, *package.ground_query)))
    {
        require(traffic->lanes.size() == 59, "host navigation test needs the exported 59-lane Signal City");
    }
};

SimulationHostConfig base_config()
{
    SimulationHostConfig config;
    config.physics_frequency_hz = 1.0 / dt;
    config.command_timeout = std::chrono::seconds(5);
    config.hard_command_timeout = std::chrono::seconds(20);
    config.max_command_queue_age = std::chrono::seconds(2);
    config.source_id = "npc-navigation-host-test";
    config.require_client_hello = true;
    config.npc_autonomous = true;
    config.npc_max_speed_mps = 6.0;
    config.npc_count = 1;
    config.npc_route_loop = true;
    config.npc_start_offset_m = 20.0;
    return config;
}

SimulationHostConfig city_config(const CityFixture& city)
{
    auto config = base_config();
    config.map_package_checksum = city.package.collision_checksum;
    config.ground_query = city.package.ground_query;
    config.collision_world = city.package.collision_world;
    config.traffic_network = city.traffic;
    config.npc_route = city_loop;
    return config;
}

simcore_host::GroundPointEnu npc_spawn_anchor(const SimulationHostConfig& config)
{
    simcore_host::NpcLaneFollower follower;
    follower.rebuild(*config.traffic_network, *config.ground_query, config.npc_route,
        config.npc_route_loop, config.npc_start_offset_m);
    return follower.state().position_enu;
}

struct Harness {
    boost::asio::io_context ioc;
    std::uint64_t frame_count = 0;
    SimulationHost host;

    explicit Harness(SimulationHostConfig config)
        : host(ioc, std::move(config), {
            [this](const std::string& bytes) {
                simcore::Envelope frame;
                require(frame.ParseFromString(bytes) && frame.has_world_state(),
                    "public host callback must emit a parseable atomic WorldState");
                ++frame_count;
            }, {}})
    {}

    void tick()
    {
        host.start();
        require(ioc.run_one() == 1, "public fixed timer callback must execute");
        host.stop();
        (void)ioc.poll();
        ioc.restart();
    }

    NpcNavigationSnapshot navigation() const
    {
        const auto diagnostics = host.npc_navigation();
        require(diagnostics.size() == 1 && diagnostics[0].entity_id == npc_id,
            "one configured NPC must retain a stable navigation diagnostic identity");
        return diagnostics[0];
    }

    simcore_host::ObbPrism body(std::uint32_t entity_id = npc_id) const
    {
        for (const auto& entity : host.runtime_entities()) {
            if (entity.entity_id == entity_id) {
                return std::get<simcore_host::ObbPrism>(entity.collision_proxy.shape);
            }
        }
        throw std::runtime_error("navigation NPC disappeared from authoritative runtime entities");
    }
};

struct Client {
    Harness& harness;
    std::uint64_t generation = 0;
    std::string session;
    std::uint64_t sequence = 0;
    std::uint64_t timestamp = 0;

    simcore::Envelope envelope()
    {
        simcore::Envelope message;
        message.set_schema_version(simcore_host::kProtocolSchemaVersion);
        message.set_sequence(++sequence);
        message.set_source_id("navigation-test-unreal");
        message.set_session_id(session);
        message.set_map_package_checksum(harness.host.map_package_checksum());
        return message;
    }

    std::uint64_t time()
    {
        const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            SimulationHost::Clock::now().time_since_epoch()).count());
        timestamp = std::max(timestamp + 1, now);
        return timestamp;
    }

    void open()
    {
        reset("first-play");
    }

    void reset(const char* play_session)
    {
        // A new PIE session creates a new WebSocket generation and negotiates
        // Hello again; changing Play identity on an old connection is rejected.
        ++generation;
        session = "navigation-test-connection-" + std::to_string(generation);
        sequence = 0;
        auto message = envelope();
        auto* hello = message.mutable_hello();
        hello->set_build("npc-navigation-host-test");
        hello->set_schema(std::string(simcore_host::kProtocolSchemaName));
        for (const char* capability : {"world-state.v2", "control.v2", "simulation-reset.v1",
                                       "map-package-checksum.v1"}) hello->add_capabilities(capability);
        require(harness.host.handle_client_message(message.SerializeAsString(), generation)
                    == ClientMessageResult::HelloAccepted, "actual Hello must be accepted");
        message = envelope();
        auto* reset = message.mutable_simulation_reset();
        reset->set_play_session_id(play_session);
        reset->set_client_time_ns(time());
        require(harness.host.handle_client_message(message.SerializeAsString(), generation)
                    == ClientMessageResult::SimulationReset, "new Play must reset navigation as well as physics");
    }

    void control()
    {
        auto message = envelope();
        auto* control = message.mutable_control_command();
        control->set_mode(simcore::CONTROL_MODE_MANUAL);
        control->set_brake(1.0f);
        control->set_gear(simcore::VEHICLE_GEAR_DRIVE);
        control->set_client_time_ns(time());
        require(harness.host.handle_client_message(message.SerializeAsString(), generation)
                    == ClientMessageResult::ControlAccepted, "fresh control must enable navigation lifecycle");
    }
};

void test_real_package_determinism_and_lifecycle(const CityFixture& city)
{
    Harness first(city_config(city)), second(city_config(city));
    const auto initial = first.navigation();
    const auto twin = second.navigation();
    require(initial.destination_lane_id != 0 && initial.destinations_selected == 1
                && initial.route == twin.route && initial.destination_lane_id == twin.destination_lane_id,
            "identical package, entity seed and reset must choose the same reachable initial destination");
    require(initial.route.front() == 1010 && initial.route.back() == initial.destination_lane_id,
            "autonomous route must retain the actual spawn lane and end at its chosen destination");
    const auto start = first.body();
    for (int index = 0; index < 3; ++index) first.tick();
    require(first.body().center_enu.east_m == start.center_enu.east_m
                && first.navigation().destinations_selected == 1,
            "before reset/control neither movement nor destination counters may advance");
    Client client{first};
    client.open();
    client.control();
    for (int index = 0; index < 12; ++index) first.tick();
    require(first.body().center_enu.east_m > start.center_enu.east_m,
            "fresh control must move the autonomous NPC through actual timer ticks");
    client.reset("second-play");
    const auto reset = first.navigation();
    require(reset.route == initial.route && reset.destination_lane_id == initial.destination_lane_id
                && reset.destinations_selected == 1 && reset.reroutes == 0
                && reset.lane_changes_completed == 0 && !reset.changing_lane,
            "Play reset must clear autonomous progress and replay the original deterministic destination");
    require(std::hypot(first.body().center_enu.east_m-start.center_enu.east_m,
                       first.body().center_enu.north_m-start.center_enu.north_m) < 1.e-8,
            "navigation reset cannot retain a previous trip's position");
}

simcore_host::StaticObbCollider barrier(double east, double north)
{
    return {"navigation-test-barrier", simcore_host::StaticColliderSemantic::Barrier,
        {{east, north}, 0.85, std::numbers::pi / 2.0, 1.0, 0.6, 0.75}, {0.8, 0.0}};
}

class RecoverableChangeGround final : public simcore_host::GroundQuery {
public:
    explicit RecoverableChangeGround(std::shared_ptr<const simcore_host::GroundQuery> ground)
        : ground_(std::move(ground)) {}

    bool fail_change_strip = false;
    mutable std::size_t failed_queries = 0;

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const auto& point = request.origin_enu;
        // Source 1010 has its straight center/corners at N=35/34/36.
        // Lose only the changing/target strip, not the source follower's road.
        if (fail_change_strip && point.east_m > -90 && point.east_m < -65
            && point.north_m > 36.2 && point.north_m < 37.7) {
            ++failed_queries;
            return std::nullopt;
        }
        return ground_->query_down(request);
    }

private:
    std::shared_ptr<const simcore_host::GroundQuery> ground_;
};

void test_real_package_continuous_lane_change(const CityFixture& city)
{
    auto config = city_config(city);
    const auto ground = std::make_shared<RecoverableChangeGround>(city.package.ground_query);
    config.ground_query = ground;
    auto colliders = city.package.collision_world->static_colliders();
    const auto obstacle = barrier(-55.0, 35.0);
    colliders.push_back(obstacle);
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(std::move(colliders));
    Harness harness(std::move(config));
    Client client{harness};
    client.open();
    client.control();
    const auto destination = harness.navigation().destination_lane_id;
    bool began = false, intermediate = false, completed = false, ground_recovery_checked = false;
    auto previous = harness.body();
    for (int tick = 0; tick < 1080; ++tick) {
        if (tick % 60 == 0) client.control();
        harness.tick();
        const auto current = harness.body();
        const auto navigation = harness.navigation();
        began |= navigation.changing_lane;
        intermediate |= current.center_enu.north_m > 35.2 && current.center_enu.north_m < 38.1;
        require(std::hypot(current.center_enu.east_m-previous.center_enu.east_m,
                           current.center_enu.north_m-previous.center_enu.north_m) < 0.20,
                "real host lane changes must move continuously, never teleport between lanes");
        require(!simcore_host::intersect_obb_prisms(current, obstacle.shape),
                "lane-change execution must not penetrate the blocking car/barrier");
        require(navigation.destination_lane_id == destination,
                "passing an obstacle must preserve the selected destination");
        previous = current;
        if (!ground_recovery_checked && navigation.changing_lane
            && current.center_enu.north_m > 36.3 && current.center_enu.north_m < 36.6) {
            ground->fail_change_strip = true;
            require(ground->query_down({{current.center_enu.east_m, 35.0, 2.0}, 4.0}).has_value()
                        && ground->query_down({{current.center_enu.east_m, 36.0, 2.0}, 4.0}).has_value(),
                    "failure injection must preserve the source center/edge support");
            for (int failed_tick = 0; failed_tick < 8; ++failed_tick) {
                harness.tick();
                const auto frozen = harness.body();
                require(std::hypot(frozen.center_enu.east_m-current.center_enu.east_m,
                                   frozen.center_enu.north_m-current.center_enu.north_m) < 1.e-9
                            && std::abs(frozen.heading_rad-current.heading_rad) < 1.e-9,
                        "missing blend ground must freeze world pose, not advance a hidden source position");
                const auto paused = harness.navigation();
                require(paused.changing_lane && paused.lane_changes_completed == 0
                            && paused.destination_lane_id == destination,
                        "support failure must retain the uncompleted maneuver and selected destination");
            }
            require(ground->failed_queries > 0, "regression must actually exercise failed blend-ground queries");
            ground->fail_change_strip = false;
            client.control();
            harness.tick();
            previous = harness.body();
            const double resumed_step = std::hypot(previous.center_enu.east_m-current.center_enu.east_m,
                                                   previous.center_enu.north_m-current.center_enu.north_m);
            require(resumed_step > 0 && resumed_step < 0.001,
                    "restored ground must resume from the frozen pose at bounded acceleration, without a catch-up jump");
            ground_recovery_checked = true;
        }
        if (navigation.lane_changes_completed != 0) {
            require(navigation.lane_id == 1015 && !navigation.changing_lane
                        && std::abs(current.center_enu.north_m-38.3) < 0.02,
                    "completed blend must adopt the authored neighboring route and centerline");
            completed = true;
            break;
        }
    }
    require(began && intermediate && completed && ground_recovery_checked,
            "real Signal City stationary obstruction must trigger and finish a continuous safe lane change");
    client.reset("after-lane-change");
    require(harness.navigation().lane_changes_completed == 0 && !harness.navigation().changing_lane
                && harness.navigation().lane_id == 1010,
            "Play reset must cancel any retained lane-change state");
}

void test_unsafe_adjacent_rear_gap_blocks_lane_change(const CityFixture& city)
{
    auto config = city_config(city);
    const auto spawn = npc_spawn_anchor(config);
    auto colliders = city.package.collision_world->static_colliders();
    colliders.push_back(barrier(-55.0, 35.0));
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(std::move(colliders));
    config.runtime_entities.push_back({3001, simcore_host::RuntimeEntityKind::NpcVehicle,
        {"navigation-test-rear-car", simcore_host::ObbPrism{
            {spawn.east_m - 3.0, 38.3}, 0.85, std::numbers::pi/2.0, 2.2, 1.0, 0.75}, {}, 0.0, {0.8, 0.0}}});
    config.runtime_entities.push_back({3002, simcore_host::RuntimeEntityKind::NpcVehicle,
        {"navigation-test-rear-car-right", simcore_host::ObbPrism{
            {spawn.east_m - 3.0, 31.7}, 0.85, std::numbers::pi/2.0, 2.2, 1.0, 0.75}, {}, 0.0, {0.8, 0.0}}});
    Harness harness(std::move(config));
    Client client{harness};
    client.open();
    client.control();
    for (int tick = 0; tick < 240; ++tick) {
        if (tick % 60 == 0) client.control();
        harness.tick();
        const auto navigation = harness.navigation();
        require(!navigation.changing_lane && navigation.lane_changes_completed == 0,
                "insufficient rear standstill gap must prevent the maneuver, even with clear road ahead");
    }
}

void test_real_crash_near_window_end_is_passed_only_with_a_clear_legal_lane(const CityFixture& city)
{
    const auto source = std::find_if(city.traffic->lanes.begin(),city.traffic->lanes.end(),
        [](const auto& lane) { return lane.id == 1010; });
    require(source != city.traffic->lanes.end() && source->lane_changes.size() == 2,
        "crash bypass regression requires the real three-lane same-direction approach");
    const double window_end = source->lane_changes.front().source_end_m;
    for (const bool adjacent_lanes_occupied : {false,true}) {
        auto config = city_config(city);
        config.physics_frequency_hz = 60.0;
        // Only 10.5m remains: the old fixed 12m request always failed, and its
        // extra queue delay consumed still more of the legally available room.
        config.npc_start_offset_m = window_end - 10.5;
        simcore_host::RuntimeEntityState crashed;
        crashed.entity_id = 3001;
        crashed.kind = simcore_host::RuntimeEntityKind::NpcVehicle;
        crashed.collision_proxy = {"navigation-crashed-lead",
            simcore_host::ObbPrism{{-21.0,35.0},0.85,std::numbers::pi/2.0,2.2,1.0,0.75},
            {},0.0,{0.8,0.08},1500.0,2600.0,30.0};
        crashed.damage_percent = 100.0f;
        crashed.recovery_phase = static_cast<std::uint32_t>(simcore_host::ImpactRecoveryPhase::Disabled);
        crashed.collision_event_sequence = 1;
        config.runtime_entities.push_back(crashed);
        const auto crash_box = std::get<simcore_host::ObbPrism>(crashed.collision_proxy.shape);
        std::vector<simcore_host::ObbPrism> occupied;
        if (adjacent_lanes_occupied) {
            for (const double north : {38.3,31.7}) {
                const auto id = static_cast<std::uint32_t>(3002+occupied.size());
                const simcore_host::ObbPrism box{{-35.0,north},0.85,std::numbers::pi/2.0,2.2,1.0,0.75};
                config.runtime_entities.push_back({id,simcore_host::RuntimeEntityKind::NpcVehicle,
                    {"navigation-blocked-neighbour-"+std::to_string(id),box,{},0.0,{0.8,0.0}}});
                occupied.push_back(box);
            }
        }
        Harness harness(std::move(config));
        Client client{harness};
        client.open();
        client.control();
        const auto destination = harness.navigation().destination_lane_id;
        auto previous = harness.body();
        bool completed = false;
        for (int tick = 0; tick < 300; ++tick) {
            if (tick % 30 == 0) client.control();
            harness.tick();
            const auto body = harness.body();
            const auto navigation = harness.navigation();
            require(!simcore_host::intersect_obb_prisms(body,crash_box),
                "a crash bypass must never overlap the actual disabled-car collision box");
            require(std::hypot(body.center_enu.east_m-previous.center_enu.east_m,
                               body.center_enu.north_m-previous.center_enu.north_m) < 0.2,
                "a tight legal crash bypass must still move continuously rather than teleport");
            require(navigation.destination_lane_id == destination,
                "passing a disabled lead car must preserve the reachable destination");
            for (const auto& wall : city.package.collision_world->static_colliders())
                require(!simcore_host::intersect_obb_prisms(body,wall.shape),
                    "shortening a manoeuvre must not drive through a wall or curb");
            if (adjacent_lanes_occupied) {
                require(!navigation.changing_lane && navigation.lane_changes_completed == 0,
                    "an accident does not authorize cutting into either occupied neighbouring lane");
                for (const auto& neighbour : occupied)
                    require(!simcore_host::intersect_obb_prisms(body,neighbour),
                        "blocked adjacent cars must remain solid during attempted bypass");
            } else {
                if (tick == 1) require(navigation.changing_lane && navigation.search_steps == 1,
                    "a known disabled lead car must plan on its next search turn before consuming turning room");
                if (navigation.lane_changes_completed != 0) {
                    require((navigation.lane_id == 1015 || navigation.lane_id == 1016)
                        && navigation.lane_offset_m <= window_end+1.0,
                        "bypass must finish on an explicitly authored adjacent lane before the window ends");
                    completed = true;
                    break;
                }
            }
            previous = body;
        }
        if (!adjacent_lanes_occupied) require(completed,
            "a clear 10.5m authored window must let the follower pass the disabled lead car");
    }
}

void test_crash_bypass_waits_for_moving_adjacent_traffic_without_consuming_turning_room(const CityFixture& city)
{
    auto config = city_config(city);
    config.physics_frequency_hz = 60.0;
    config.npc_start_offset_m = 25.0;
    const auto spawn = npc_spawn_anchor(config);
    simcore_host::RuntimeEntityState crashed{3001,simcore_host::RuntimeEntityKind::NpcVehicle,
        {"navigation-wait-crashed-lead",
            simcore_host::ObbPrism{{spawn.east_m + 20.0,35.0},0.85,std::numbers::pi/2.0,2.2,1.0,0.75},
            {},0.0,{0.8,0.08},1500.0,2600.0,30.0}};
    crashed.damage_percent = 100.0f;
    crashed.recovery_phase = static_cast<std::uint32_t>(simcore_host::ImpactRecoveryPhase::Disabled);
    crashed.collision_event_sequence = 1;
    config.runtime_entities.push_back(crashed);
    // Both passing lanes are occupied at detection, then independently moving
    // traffic clears them. This is deliberately not an initially clear plan.
    for (const double north : {38.3,31.7}) {
        const auto id = static_cast<std::uint32_t>(3002+config.runtime_entities.size());
        config.runtime_entities.push_back({id,simcore_host::RuntimeEntityKind::NpcVehicle,
            {"navigation-moving-neighbour-"+std::to_string(id),
                simcore_host::ObbPrism{{spawn.east_m + 2.0,north},0.85,std::numbers::pi/2.0,2.2,1.0,0.75},
                {4.0,0.0},0.0,{0.8,0.08},1500.0,2600.0,30.0}});
    }
    Harness harness(std::move(config));
    Client client{harness}; client.open(); client.control();
    const auto initial = harness.body();
    bool waited = false, completed = false;
    auto previous = initial;
    for (int tick = 0; tick < 720; ++tick) {
        if (tick % 30 == 0) client.control();
        harness.tick();
        const auto navigation = harness.navigation();
        const auto body = harness.body();
        require(body.center_enu.east_m >= initial.center_enu.east_m - 0.05,
            "retained forward turning room must not trigger unnecessary reversing for passing traffic");
        if (tick < 60) {
            require(!navigation.changing_lane,
                "a follower cannot cut into adjacent traffic while both passing lanes are occupied");
            require(navigation.preserving_bypass_space
                    && (navigation.search_steps == 0 || !navigation.lane_change_wait_reason.empty()),
                "a blocked merge must retain manoeuvre space and expose its bounded wait reason");
            waited = true;
        }
        for (const auto& entity : harness.host.runtime_entities()) {
            if (entity.entity_id == npc_id) continue;
            if (const auto* other = std::get_if<simcore_host::ObbPrism>(&entity.collision_proxy.shape))
                require(!simcore_host::intersect_obb_prisms(body,*other),
                    "waiting/merging after a crash must not overlap the lead or passing traffic");
        }
        require(std::hypot(body.center_enu.east_m-previous.center_enu.east_m,
                           body.center_enu.north_m-previous.center_enu.north_m) < 0.2,
            "delayed crash bypass must not teleport to manufacture turning room");
        if (navigation.lane_changes_completed != 0) { completed = true; break; }
        previous = body;
    }
    std::cout << "[NPC regression] delayed_crash completed=" << completed
              << " final_east=" << harness.body().center_enu.east_m
              << " lane=" << harness.navigation().lane_id
              << " reason=" << harness.navigation().lane_change_wait_reason << "\n";
    require(waited && completed,
        "after moving adjacent traffic clears, a crash follower must still have room to change lanes");
    require(!harness.navigation().preserving_bypass_space
        && harness.navigation().lane_change_wait_reason.empty(),
        "a completed bypass must release its retained stopping anchor and wait reason");
    require(harness.navigation().avoidance_phase == "none",
        "a completed bypass must leave reverse avoidance inactive");
    require(harness.navigation().avoidance_reverse_remaining_m == 0.0,
        "a completed bypass must clear remaining reverse distance");
    client.reset("after-delayed-crash");
    require(!harness.navigation().preserving_bypass_space
        && harness.navigation().lane_change_wait_reason.empty()
        && harness.navigation().lane_changes_completed == 0,
        "new Play must discard crash waiting anchors and diagnostics");
}

SimulationHostConfig diamond_config(bool blocked, bool red_signal = false)
{
    auto config = base_config();
    config.map_package_checksum = "fnv1a64:0123456789abcdef";
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->source_map_checksum = config.map_package_checksum;
    // Synthetic immutable fixture still supplies canonical traffic provenance
    // when signal snapshots are encoded through the real protocol serializer.
    network->checksum = "fnv1a64:1123456789abcdef";
    network->lanes = {
        {10, 3, 6, red_signal ? 1U : 0U, false, {{100,100,0},{120,100,0}}, {20,30}},
        {20, 3, 6, 0, false, {{120,100,0},{140,100,0}}, {40}},
        {30, 3, 6, 0, false, {{120,100,0},{120,102,0},{140,102,0},{140,100,0}}, {40}},
        {40, 3, 6, 0, false, {{140,100,0},{160,100,0}}, {50}},
        {50, 3, 6, 0, false, {{160,100,0},{160,120,0},{100,120,0},{100,100,0}}, {10}},
    };
    // Signals now have physical poles. Keep this red-light-only fixture on the
    // roadside, outside the 3 m lane and the NPC's swept safety envelope.
    if (red_signal) network->signals = {{1,1,{120,96,0},90}};
    config.traffic_network = std::move(network);
    config.npc_route = {10,20,40,50};
    config.npc_start_offset_m = red_signal ? 17.0 : 5.0;
    std::vector<simcore_host::StaticObbCollider> colliders;
    if (blocked) {
        auto obstacle = barrier(130,100);
        obstacle.shape.half_width_m = 0.1;
        colliders.push_back(obstacle);
    }
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(std::move(colliders));
    return config;
}

void test_future_lane_detour_and_signal_queue_distinction()
{
    Harness harness(diamond_config(true));
    const auto initial = harness.navigation();
    require(initial.destination_lane_id == 50
                && std::find(initial.route.begin(), initial.route.end(), 20) != initial.route.end(),
            "diamond fixture must initially choose its shortest direct route");
    Client client{harness};
    client.open();
    client.control();
    const auto before = harness.body();
    harness.tick();
    harness.tick(); // Detection queues work; this lone NPC receives the next search turn.
    const auto detour = harness.navigation();
    require(detour.destination_lane_id == initial.destination_lane_id && detour.reroutes == 1
                && std::find(detour.route.begin(), detour.route.end(), 20) == detour.route.end()
                && std::find(detour.route.begin(), detour.route.end(), 30) != detour.route.end(),
            "stationary future-lane blockage must cause a legal alternate route to the same destination");
    require(std::hypot(harness.body().center_enu.east_m-before.center_enu.east_m,
                       harness.body().center_enu.north_m-before.center_enu.north_m) < 0.02,
            "detour adoption cannot teleport to a new branch");

    Harness queued(diamond_config(false, true));
    Client waiting{queued};
    waiting.open();
    waiting.control();
    for (int tick = 0; tick < 144; ++tick) {
        if (tick % 60 == 0) waiting.control();
        queued.tick();
    }
    const auto stopped = queued.navigation();
    require(stopped.reroutes == 0 && stopped.lane_changes_completed == 0 && !stopped.changing_lane
                && stopped.lane_id == 10 && queued.body().center_enu.east_m <= 117.31,
            "ordinary red-light waiting is not a road closure and cannot trigger a detour");
}
void test_downed_pedestrian_blocks_future_lane_detour()
{
    auto config = diamond_config(false);
    simcore_host::RuntimeEntityState body{3001, simcore_host::RuntimeEntityKind::Pedestrian,
        {"fallen-person", simcore_host::ObbPrism{{130,100},.25,std::numbers::pi/2,.95,.35,.25},
            {},0,{.7,0},80,30,15}};
    body.pedestrian_downed = true;
    body.recovery_phase = 4;
    config.runtime_entities.push_back(body);
    Harness harness(std::move(config));
    Client client{harness}; client.open(); client.control();
    harness.tick();
    harness.tick(); // The legal detour is selected after the queued local proposal is checked.
    const auto route=harness.navigation();
    require(route.reroutes==1 && std::find(route.route.begin(),route.route.end(),20)==route.route.end(),
        "a stationary downed person must mark its future lane blocked and select the legal detour");
}

SimulationHostConfig ego_blocked_parallel_config()
{
    auto config = base_config();
    config.map_package_checksum = "fnv1a64:2223456789abcdef";
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(
        std::vector<simcore_host::StaticObbCollider>{});
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:3223456789abcdef";
    network->lanes = {
        {10,3.2,12,0,false,{{0,-50,0},{0,50,0}},{20},{{15,5,90,5,90}}},
        {20,3.2,12,0,false,{{0,50,0},{-20,50,0},{-20,-50,0},{0,-50,0}},{10}},
        {15,3.2,12,0,false,{{3.2,-50,0},{3.2,50,0}},{30}},
        {30,3.2,12,0,false,{{3.2,50,0},{20,50,0},{20,-50,0},{3.2,-50,0}},{15}},
    };
    config.traffic_network = std::move(network);
    config.npc_route = {10,20};
    config.npc_start_offset_m = 5.0;
    config.npc_max_speed_mps = 12.0;
    return config;
}

void test_stationary_ego_triggers_early_safe_bypass_and_reachable_destination()
{
    Harness harness(ego_blocked_parallel_config());
    const auto initial = harness.navigation();
    require(initial.destination_lane_id == 20 && initial.destinations_selected == 1,
        "parallel-road fixture must initially select the source loop destination");
    require(harness.body().center_enu.north_m < -44.0,
        "stationary Ego fixture must begin beyond the obsolete 40m lookahead");
    Client client{harness}; client.open(); client.control();
    bool began = false, completed = false, passed_ego = false;
    for (int tick = 0; tick < 1200; ++tick) {
        if (tick % 60 == 0) client.control();
        harness.tick();
        const auto navigation = harness.navigation();
        const auto body = harness.body();
        const simcore_host::ObbPrism ego{{0,0},0.85,0.0,2.15,1.0,0.75};
        require(!simcore_host::intersect_obb_prisms(body,ego),
            "NPC bypass of a player-blocked lane must never clip through the Ego vehicle");
        began |= navigation.changing_lane;
        if (navigation.lane_changes_completed != 0) {
            require(navigation.lane_id == 15 && navigation.destination_lane_id == 30
                    && navigation.destinations_selected == 2 && navigation.reroutes == 1,
                "an unreachable dedicated lane must select a reachable alternate destination instead of waiting forever");
            completed = true;
        }
        passed_ego |= completed && body.center_enu.north_m > 5.0;
        if (passed_ego) break;
    }
    require(began && completed && passed_ego,
        "a stationary Ego 40-80m ahead must trigger a legal lane change that actually passes it");
}

class InterruptedManeuverGround final : public simcore_host::GroundQuery {
public:
    bool lose_change_support = false;
    mutable std::size_t failed_queries = 0;

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (lose_change_support && request.origin_enu.east_m > 0.1) {
            ++failed_queries;
            return std::nullopt;
        }
        return flat_.query_down(request);
    }

private:
    simcore_host::FlatGroundQuery flat_;
};

SimulationHostConfig interrupted_maneuver_config(
    const std::shared_ptr<simcore_host::CollisionWorld>& world,
    const std::shared_ptr<InterruptedManeuverGround>& ground)
{
    auto config = ego_blocked_parallel_config();
    config.physics_frequency_hz = 60.0;
    config.npc_start_offset_m = 32.0;
    config.npc_max_speed_mps = 2.0;
    config.collision_world = world;
    config.ground_query = ground;
    auto network = std::make_shared<simcore_host::TrafficNetwork>(*config.traffic_network);
    // Prefer the existing right-hand change. A late obstruction there leaves
    // a fully authored left-hand alternative once the source anchor is reached.
    network->lanes.front().lane_changes.push_back({25,5,90,5,90});
    network->lanes.push_back({25,3.2,12,0,false,{{-3.2,-50,0},{-3.2,50,0}},{40}});
    network->lanes.push_back({40,3.2,12,0,false,
        {{-3.2,50,0},{-25,50,0},{-25,-50,0},{-3.2,-50,0}},{25}});
    config.traffic_network = std::move(network);
    return config;
}

struct InterruptedManeuverFixture {
    // Only the test retains mutable ownership. As in the removable-wall
    // fixture, replace collider contents between stopped/drained timer ticks;
    // Barrier colliders have no independently retained breakable-wall proxy.
    std::shared_ptr<simcore_host::CollisionWorld> world =
        std::make_shared<simcore_host::CollisionWorld>();
    std::shared_ptr<InterruptedManeuverGround> ground =
        std::make_shared<InterruptedManeuverGround>();
    SimulationHostConfig config = interrupted_maneuver_config(world, ground);
    Harness harness{config};
    Client client{harness};
    simcore_host::ObbPrism anchor;
    NpcNavigationSnapshot anchor_navigation;
    std::optional<simcore_host::NpcLaneChangePlan> original_curve;

    InterruptedManeuverFixture()
    {
        client.open();
        client.control();
    }

    void tick()
    {
        const auto before = harness.body();
        client.control();
        harness.tick();
        const auto body = harness.body();
        require(std::hypot(body.center_enu.east_m-before.center_enu.east_m,
                          body.center_enu.north_m-before.center_enu.north_m) < 0.10
                    && std::abs(std::remainder(body.heading_rad-before.heading_rad,
                        2.0*std::numbers::pi)) < 0.10,
            "interrupted maneuvers must preserve continuous physical position and heading");
        for (const auto& collider : world->static_colliders())
            require(!simcore_host::intersect_obb_prisms(body, collider.shape),
                "interrupted maneuver recovery must never penetrate a newly introduced blocker");
        const simcore_host::ObbPrism ego{{0,0},0.85,0.0,2.15,1.0,0.75};
        require(!simcore_host::intersect_obb_prisms(body, ego)
                    && harness.navigation().collision_damage_percent == 0.0,
            "interrupted maneuver recovery must remain clear of the original stationary Ego");
    }

    void begin_change(double minimum_east_m = 0.18, double maximum_progress_m = 4.0)
    {
        for (int index = 0; index < 480; ++index) {
            const auto before = harness.navigation();
            const auto body = harness.body();
            tick();
            const auto after = harness.navigation();
            if (!before.changing_lane && after.changing_lane) {
                anchor = body;
                anchor_navigation = before;
                original_curve = simcore_host::plan_npc_lane_change(
                    *config.traffic_network, *ground, before.lane_id,
                    before.lane_offset_m, 15, 12.0);
                require(original_curve.has_value(),
                    "fixture must reproduce the adopted low-speed twelve-metre change");
            }
            if (after.changing_lane && harness.body().center_enu.east_m > minimum_east_m) {
                require(original_curve.has_value()
                            && after.lane_offset_m-anchor_navigation.lane_offset_m < maximum_progress_m,
                    "inject the interruption only after visible lateral motion at its intended curve station");
                require_on_original_curve();
                return;
            }
        }
        require(false, "fixture must enter its preferred right-hand maneuver before interruption");
    }

    void require_on_original_curve() const
    {
        const auto sample = original_curve->sample(harness.navigation().lane_offset_m);
        const auto body = harness.body();
        require(sample.has_value()
                    && std::hypot(body.center_enu.east_m-sample->position_enu.east_m,
                                  body.center_enu.north_m-sample->position_enu.north_m) < 1.e-6
                    && std::abs(body.heading_rad
                        -sample->heading_deg*std::numbers::pi/180.0) < 1.e-6,
            "recovery must retrace the retained curve rather than snap to the hidden source centerline");
    }

    simcore_host::StaticObbCollider front_blocker() const
    {
        const auto sample = original_curve->sample(harness.navigation().lane_offset_m + 3.5);
        require(sample.has_value(), "front blocker must lie on the already active curve");
        auto obstacle = barrier(sample->position_enu.east_m, sample->position_enu.north_m);
        obstacle.shape.heading_rad = sample->heading_deg*std::numbers::pi/180.0;
        obstacle.shape.half_length_m = 0.35;
        obstacle.shape.half_width_m = 0.60;
        require(!simcore_host::intersect_obb_prisms(harness.body(), obstacle.shape),
            "introducing the new front obstacle must not itself create contact");
        return obstacle;
    }

    void replace_obstacles(std::vector<simcore_host::StaticObbCollider> obstacles)
    {
        *world = simcore_host::CollisionWorld(std::move(obstacles));
    }
};

class InterruptedBypassGround final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        auto hit = flat_.query_down(request);
        if (hit) hit->surface_material_id = request.origin_enu.east_m <= 6.0
                && request.origin_enu.east_m >= -4.8
            ? simcore_host::GroundSurfaceMaterialId::Asphalt
            : simcore_host::GroundSurfaceMaterialId::Rough;
        return hit;
    }
private:
    simcore_host::FlatGroundQuery flat_;
};

void test_interrupted_local_bypass_returns_without_crossing_centerline()
{
    auto config = ego_blocked_parallel_config();
    config.physics_frequency_hz = 60.0;
    config.npc_start_offset_m = 32.0;
    config.npc_max_speed_mps = 2.0;
    config.ground_query = std::make_shared<InterruptedBypassGround>();
    auto network = std::make_shared<simcore_host::TrafficNetwork>(*config.traffic_network);
    for (auto& lane : network->lanes) lane.lane_changes.clear();
    network->lanes.push_back({40,3.2,6,0,true,{{-3.2,50,0},{-3.2,-50,0}},{}});
    config.traffic_network = network;
    const auto world = std::make_shared<simcore_host::CollisionWorld>();
    config.collision_world = world;
    Harness harness(config);
    Client client{harness}; client.open(); client.control();
    bool injected = false, reversed = false, returned = false, completed = false;
    double anchor_distance = 0.0;
    simcore_host::ObbPrism anchor;
    for (int tick = 0; tick < 2300; ++tick) {
        const auto before = harness.navigation();
        const auto previous = harness.body();
        client.control();
        harness.tick();
        const auto after = harness.navigation();
        const auto body = harness.body();
        require(std::hypot(body.center_enu.east_m-previous.center_enu.east_m,
                          body.center_enu.north_m-previous.center_enu.north_m) < 0.1,
            "interrupted local bypass must remain continuous in both directions");
        for (const double front : {-body.half_length_m, body.half_length_m})
            for (const double side : {-body.half_width_m, body.half_width_m})
                require(body.center_enu.east_m+std::sin(body.heading_rad)*front
                            +std::cos(body.heading_rad)*side >= -1.6-1.e-6,
                    "the actual rotated body must not cross the two-way road centerline while recovering");
        for (const auto& collider : world->static_colliders())
            require(!simcore_host::intersect_obb_prisms(body, collider.shape),
                "local bypass recovery must not penetrate its new persistent blocker");
        const simcore_host::ObbPrism ego{{0,0},0.85,0.0,2.15,1.0,0.75};
        require(!simcore_host::intersect_obb_prisms(body, ego)
                    && after.collision_damage_percent == 0.0,
            "local recovery must remain clear of the original stationary Ego");
        if (!injected && !before.local_bypass_active && after.local_bypass_active) {
            anchor = previous;
            anchor_distance = before.route_distance_travelled_m;
        }
        if (!injected && after.local_bypass_active && body.center_enu.east_m > 0.2) {
            require(after.route_distance_travelled_m-anchor_distance < 4.0,
                "local recovery fixture must interrupt within the six-metre return allowance");
            auto wall = barrier(2.0, body.center_enu.north_m+3.5);
            wall.shape.heading_rad = 0.0;
            wall.shape.half_length_m = 0.2;
            wall.shape.half_width_m = 4.0;
            require(!simcore_host::intersect_obb_prisms(body, wall.shape),
                "introducing the local blocker must not create an initial contact");
            *world = simcore_host::CollisionWorld({wall});
            injected = true;
        }
        if (injected && !returned) {
            reversed |= after.route_distance_travelled_m < before.route_distance_travelled_m-1.e-9;
            if (!after.local_bypass_active) {
                require(reversed && after.local_bypasses_completed == 0
                            && after.lane_changes_completed == 0
                            && std::hypot(body.center_enu.east_m-anchor.center_enu.east_m,
                                          body.center_enu.north_m-anchor.center_enu.north_m) < 0.03,
                    "local bypass may be discarded only at its physically recovered source anchor");
                returned = true;
                *world = simcore_host::CollisionWorld{};
            }
        }
        if (returned && after.local_bypasses_completed != 0) {
            completed = true;
            break;
        }
    }
    require(injected && reversed && returned && completed,
        "an interrupted local bypass must safely return, replan and finish after the new obstruction clears");
}

void test_interrupted_maneuver_retraces_anchor_and_replans()
{
    InterruptedManeuverFixture fixture;
    fixture.begin_change();
    fixture.replace_obstacles({fixture.front_blocker()});
    bool reversed = false, returned_to_anchor = false, passed = false;
    double reverse_distance = 0.0;
    for (int tick = 0; tick < 1600; ++tick) {
        const auto before = fixture.harness.navigation();
        fixture.tick();
        const auto after = fixture.harness.navigation();
        const auto body = fixture.harness.body();
        const double retreat = before.route_distance_travelled_m-after.route_distance_travelled_m;
        if (!returned_to_anchor && retreat > 1.e-9) {
            reversed = true;
            reverse_distance += retreat;
            require(retreat <= 1.4/60.0 + 1.e-8,
                "interrupted maneuver recovery must respect the bounded reverse station speed");
            require(after.avoidance_reverse_remaining_m <= 0.5 + 1.e-8,
                "each interrupted-maneuver reverse proposal must remain at most half a metre");
            if (after.changing_lane) fixture.require_on_original_curve();
        }
        if (!returned_to_anchor && !after.changing_lane) {
            require(reversed && reverse_distance > 0.5
                        && after.lane_changes_completed == 0 && after.local_bypasses_completed == 0
                        && std::hypot(body.center_enu.east_m-fixture.anchor.center_enu.east_m,
                                      body.center_enu.north_m-fixture.anchor.center_enu.north_m) < 0.03,
                "an interrupted plan may be discarded only after physically returning to its begin anchor");
            returned_to_anchor = true;
        }
        if (returned_to_anchor && after.lane_changes_completed != 0) {
            require(after.lane_id == 25 && body.center_enu.east_m < -3.1,
                "replanning must adopt the clear authored left lane after the right corridor becomes blocked");
            passed = body.center_enu.north_m > 5.0;
            if (passed) break;
        }
    }
    require(reversed && returned_to_anchor && reverse_distance < 6.0 && passed,
        "a new persistent blocker must cause a real bounded retreat, replan and completed pass");
}

void test_interrupted_maneuver_waits_for_rear_clearance_and_resumes()
{
    InterruptedManeuverFixture fixture;
    fixture.begin_change();
    const auto front = fixture.front_blocker();
    fixture.replace_obstacles({front});
    int stopped_ticks = 0;
    for (int tick = 0; tick < 180 && stopped_ticks < 3; ++tick) {
        const auto before = fixture.harness.navigation();
        fixture.tick();
        const auto after = fixture.harness.navigation();
        stopped_ticks = std::abs(after.route_distance_travelled_m
            -before.route_distance_travelled_m) < 1.e-9 ? stopped_ticks+1 : 0;
        require(after.changing_lane && after.route_distance_travelled_m
                    >= before.route_distance_travelled_m-1.e-9,
            "rear-space injection must occur after stopping but before recovery begins");
    }
    require(stopped_ticks == 3, "new front blocker must stop the active maneuver");
    const auto stopped = fixture.harness.body();
    const auto stopped_navigation = fixture.harness.navigation();
    auto rear = barrier(stopped.center_enu.east_m, stopped.center_enu.north_m);
    rear.collider_id = "interrupted-maneuver-rear-barrier";
    rear.shape.heading_rad = stopped.heading_rad;
    rear.shape.half_length_m = 0.35;
    rear.shape.half_width_m = 1.2;
    const double separation = stopped.half_length_m + rear.shape.half_length_m + 0.15;
    rear.shape.center_enu.east_m -= std::sin(stopped.heading_rad)*separation;
    rear.shape.center_enu.north_m -= std::cos(stopped.heading_rad)*separation;
    require(!simcore_host::intersect_obb_prisms(stopped, rear.shape),
        "rear blocker must occupy retreat space without intersecting the stopped body");
    fixture.replace_obstacles({front, rear});
    bool reported_rear_block = false;
    for (int tick = 0; tick < 120; ++tick) {
        fixture.tick();
        const auto after = fixture.harness.navigation();
        const auto body = fixture.harness.body();
        reported_rear_block |= after.lane_change_wait_reason == "maneuver_recovery_rear_occupied";
        require(after.changing_lane && after.lane_changes_completed == 0
                    && after.destination_lane_id == stopped_navigation.destination_lane_id
                    && std::hypot(body.center_enu.east_m-stopped.center_enu.east_m,
                                  body.center_enu.north_m-stopped.center_enu.north_m) < 1.e-9
                    && std::abs(body.heading_rad-stopped.heading_rad) < 1.e-9,
            "occupied rear space must retain the unfinished plan and its exact stopped world pose");
    }
    require(reported_rear_block, "unsafe maneuver retreat must report occupied rear space");
    fixture.replace_obstacles({rear});
    bool completed = false;
    for (int tick = 0; tick < 720; ++tick) {
        const auto before = fixture.harness.navigation();
        fixture.tick();
        const auto after = fixture.harness.navigation();
        require(after.route_distance_travelled_m >= before.route_distance_travelled_m-1.e-9,
            "removing the front blocker must resume forward travel instead of a stale reverse");
        if (after.changing_lane) fixture.require_on_original_curve();
        if (after.lane_changes_completed != 0) {
            require(after.lane_id == 15 && after.destination_lane_id == stopped_navigation.destination_lane_id,
                "a cleared front corridor must complete the original right-hand plan");
            completed = true;
            break;
        }
    }
    require(completed, "clearing only the front blocker must release the stopped maneuver");
}

void test_temporary_maneuver_interruptions_do_not_reverse()
{
    InterruptedManeuverFixture fixture;
    fixture.begin_change();
    fixture.replace_obstacles({fixture.front_blocker()});
    for (int tick = 0; tick < 18; ++tick) {
        const auto before = fixture.harness.navigation();
        fixture.tick();
        const auto after = fixture.harness.navigation();
        require(after.changing_lane && after.avoidance_phase != "backing"
                    && after.route_distance_travelled_m >= before.route_distance_travelled_m-1.e-9,
            "a brief obstruction must retain the current maneuver without starting recovery");
    }
    fixture.replace_obstacles({});
    fixture.ground->lose_change_support = true;
    const auto frozen = fixture.harness.body();
    for (int tick = 0; tick < 90; ++tick) {
        fixture.tick();
        const auto after = fixture.harness.navigation();
        const auto body = fixture.harness.body();
        require(after.changing_lane && after.avoidance_phase != "backing"
                    && after.lane_changes_completed == 0
                    && std::hypot(body.center_enu.east_m-frozen.center_enu.east_m,
                                  body.center_enu.north_m-frozen.center_enu.north_m) < 1.e-9,
            "ground loss alone must not be mistaken for a persistent physical maneuver obstruction");
    }
    require(fixture.ground->failed_queries > 0,
        "ground-loss regression must actually reject support for the active curve");
    fixture.ground->lose_change_support = false;
    bool completed = false;
    for (int tick = 0; tick < 720; ++tick) {
        const auto before = fixture.harness.navigation();
        fixture.tick();
        const auto after = fixture.harness.navigation();
        require(after.route_distance_travelled_m >= before.route_distance_travelled_m-1.e-9,
            "restoring curve support must resume forward progress without spurious retreat");
        if (after.lane_changes_completed != 0) {
            completed = after.lane_id == 15;
            break;
        }
    }
    require(completed, "temporary stop and support loss must still allow the original maneuver to finish");
}

void test_interrupted_maneuver_beyond_retreat_limit_waits_and_resumes()
{
    InterruptedManeuverFixture fixture;
    fixture.begin_change(1.9, 9.0);
    require(fixture.harness.navigation().lane_offset_m
                -fixture.anchor_navigation.lane_offset_m > 6.0,
        "retreat-limit fixture must interrupt after the anchor is more than six metres behind");
    fixture.replace_obstacles({fixture.front_blocker()});
    int stopped_ticks = 0;
    for (int tick = 0; tick < 180 && stopped_ticks < 3; ++tick) {
        const auto before = fixture.harness.navigation();
        fixture.tick();
        const auto after = fixture.harness.navigation();
        require(after.changing_lane && after.route_distance_travelled_m
                    >= before.route_distance_travelled_m-1.e-9,
            "a distant maneuver anchor must not authorize an over-budget retreat");
        stopped_ticks = std::abs(after.route_distance_travelled_m
            -before.route_distance_travelled_m) < 1.e-9 ? stopped_ticks+1 : 0;
    }
    require(stopped_ticks == 3, "late front blocker must stop the unfinished maneuver");
    const auto stopped = fixture.harness.body();
    bool reported_limit = false;
    for (int tick = 0; tick < 120; ++tick) {
        fixture.tick();
        const auto after = fixture.harness.navigation();
        const auto body = fixture.harness.body();
        reported_limit |= after.lane_change_wait_reason == "maneuver_recovery_retreat_limit";
        require(after.changing_lane && after.lane_changes_completed == 0
                    && after.avoidance_phase != "backing"
                    && std::hypot(body.center_enu.east_m-stopped.center_enu.east_m,
                                  body.center_enu.north_m-stopped.center_enu.north_m) < 1.e-9,
            "exceeding the retreat budget must preserve the stopped pose and unfinished plan");
    }
    require(reported_limit, "an unreachable recovery anchor must expose the explicit retreat-limit reason");
    fixture.replace_obstacles({});
    bool completed = false;
    for (int tick = 0; tick < 720; ++tick) {
        const auto before = fixture.harness.navigation();
        fixture.tick();
        const auto after = fixture.harness.navigation();
        require(after.route_distance_travelled_m >= before.route_distance_travelled_m-1.e-9,
            "clearing a late blocker must resume the retained curve without exceeding the reverse budget");
        if (after.changing_lane) fixture.require_on_original_curve();
        if (after.lane_changes_completed != 0) {
            completed = after.lane_id == 15;
            break;
        }
    }
    require(completed, "removing an over-budget maneuver obstruction must permit ordinary completion");
}

SimulationHostConfig route_end_only_parallel_config()
{
    auto config = base_config();
    config.map_package_checksum = "fnv1a64:4223456789abcdef";
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(
        std::vector<simcore_host::StaticObbCollider>{});
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:5223456789abcdef";
    network->lanes = {
        {10,3.2,4,0,false,{{100,-10,0},{100,10,0}},{20},{{15,2,17,2,17}}},
        {20,3.2,4,0,false,{{100,10,0},{90,10,0},{90,-10,0},{100,-10,0}},{10}},
        {15,3.2,4,0,false,{{103.2,-10,0},{103.2,10,0}},{30}},
        {30,3.2,4,0,false,{{103.2,10,0},{113.2,10,0},{113.2,-10,0},{103.2,-10,0}},{15}},
    };
    config.traffic_network = std::move(network);
    config.npc_route = {10,20};
    config.npc_start_offset_m = 5.0;
    return config;
}

void test_nearby_route_end_is_not_a_physical_obstruction()
{
    Harness harness(route_end_only_parallel_config());
    const auto initial = harness.navigation();
    require(initial.lane_id == 10 && initial.destination_lane_id == 20,
        "route-end fixture must begin on its short source trip");
    Client client{harness}; client.open(); client.control();
    for (int tick = 0; tick < 120; ++tick) {
        if (tick != 0 && tick % 60 == 0) client.control();
        harness.tick();
    }
    const auto navigation = harness.navigation();
    require(!navigation.changing_lane && navigation.lane_changes_completed == 0
            && navigation.reroutes == 0 && navigation.destination_lane_id == 20,
        "a normal route end inside 80m must not be treated as a blocker or replace the destination");
}

SimulationHostConfig profile_fleet_config()
{
    auto config = base_config();
    config.map_package_checksum = "fnv1a64:6223456789abcdef";
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(
        std::vector<simcore_host::StaticObbCollider>{});
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:7223456789abcdef";
    network->lanes = {
        {10,4.0,12,0,false,{{100,0,0},{200,0,0},{300,0,0}},{20}},
        {20,4.0,12,0,false,{{300,0,0},{300,100,0},{300,200,0}},{30}},
        {30,4.0,12,0,false,{{300,200,0},{200,200,0},{100,200,0}},{40}},
        {40,4.0,12,0,false,{{100,200,0},{100,100,0},{100,0,0}},{10}},
    };
    config.traffic_network = std::move(network);
    config.npc_route = {10,20,30,40};
    config.npc_start_offset_m = 20.0;
    config.npc_count = 4;
    config.npc_spacing_m = 100.0;
    config.npc_max_speed_mps = 6.0;
    return config;
}

void test_deterministic_mixed_fleet_profiles_drive_distinct_host_physics()
{
    Harness harness(profile_fleet_config());
    const auto navigation = harness.host.npc_navigation();
    require(navigation.size() == 4,
        "four requested NPCs must produce four deterministic mixed-fleet profiles");
    const std::array<std::string,4> profile_names{
        "sedan","compact","truck","motorcycle"};
    const std::array<simcore_host::RuntimeVehicleClass,4> classes{
        simcore_host::RuntimeVehicleClass::Sedan,
        simcore_host::RuntimeVehicleClass::Compact,
        simcore_host::RuntimeVehicleClass::Truck,
        simcore_host::RuntimeVehicleClass::Motorcycle};
    const std::array<double,4> half_lengths{2.20,1.75,3.10,1.10};
    const std::array<double,4> masses{1500.0,1050.0,6200.0,240.0};
    for (std::size_t index=0; index<4; ++index) {
        require(navigation[index].entity_id == npc_id + index
                    && navigation[index].vehicle_profile == profile_names[index],
            "NPC ID order must cycle sedan/compact/truck/motorcycle reproducibly");
        const auto entity = std::find_if(harness.host.runtime_entities().begin(),
            harness.host.runtime_entities().end(), [&](const auto& candidate) {
                return candidate.entity_id == npc_id + index;
            });
        require(entity != harness.host.runtime_entities().end()
                    && entity->vehicle_class == classes[index],
            "authoritative runtime state must publish the assigned vehicle class");
        const auto& shape = std::get<simcore_host::ObbPrism>(entity->collision_proxy.shape);
        require(std::abs(shape.half_length_m-half_lengths[index]) < 1.e-9
                    && std::abs(entity->collision_proxy.mass_kg-masses[index]) < 1.e-9,
            "each vehicle class must own its collision extent and inertial mass");
        if (classes[index] == simcore_host::RuntimeVehicleClass::Truck) {
            require(std::abs(shape.half_width_m - 1.05) < 1.e-9
                        && std::abs(shape.half_height_m - 0.965) < 1.e-9
                        && std::abs(shape.center_up_m - shape.half_height_m - 0.411071) < 1.e-6,
                "NPC truck must match the authored body width, roof and underbody clearance");
        }
    }
    Client client{harness}; client.open(); client.control();
    for (int tick=0; tick<60; ++tick) {
        if (tick != 0 && tick%30 == 0) client.control();
        harness.tick();
    }
    const auto moved = harness.host.npc_navigation();
    require(moved[3].route_distance_travelled_m > moved[1].route_distance_travelled_m
                && moved[1].route_distance_travelled_m > moved[0].route_distance_travelled_m
                && moved[0].route_distance_travelled_m > moved[2].route_distance_travelled_m,
        "motorcycle/compact/sedan/truck acceleration profiles must produce distinct route motion");
}

void test_close_blocker_triggers_bounded_reverse_then_safe_pass()
{
    auto config = ego_blocked_parallel_config();
    config.npc_start_offset_m = 43.5; // 6.5m centre distance from stationary Ego.
    config.npc_max_speed_mps = 4.0;
    Harness harness(std::move(config));
    Client client{harness}; client.open(); client.control();
    const double initial_north = harness.body().center_enu.north_m;
    double minimum_north = initial_north;
    bool backing = false, completed = false, passed = false;
    for (int tick=0; tick<1500; ++tick) {
        if (tick != 0 && tick%60 == 0) client.control();
        harness.tick();
        const auto body = harness.body();
        const auto navigation = harness.navigation();
        minimum_north = std::min(minimum_north, body.center_enu.north_m);
        backing |= navigation.avoidance_phase == "backing";
        completed |= navigation.lane_changes_completed != 0;
        passed |= completed && body.center_enu.north_m > 5.0;
        const simcore_host::ObbPrism ego{{0,0},0.85,0.0,2.15,1.0,0.75};
        require(!simcore_host::intersect_obb_prisms(body,ego),
            "reverse-and-pass local planning must never overlap the stationary Ego");
        require(initial_north-body.center_enu.north_m <= 6.05,
            "escape reverse must remain inside its explicit six-metre bound");
        if (passed) break;
    }
    require(backing && minimum_north < initial_north-0.5 && completed && passed,
        "an NPC too close to steer must back up, adopt an authored adjacent lane and pass");
}

void test_close_blocker_does_not_reverse_into_rear_vehicle()
{
    auto config = ego_blocked_parallel_config();
    config.npc_start_offset_m = 43.5;
    config.npc_max_speed_mps = 4.0;
    config.runtime_entities.push_back({3001,simcore_host::RuntimeEntityKind::NpcVehicle,
        {"navigation-rear-blocker",
            simcore_host::ObbPrism{{0,-12.0},0.85,0.0,2.2,1.0,0.75},
            {},0.0,{0.8,0.08},1500.0,2600.0,30.0}});
    Harness harness(std::move(config));
    Client client{harness}; client.open(); client.control();
    const double initial_north = harness.body().center_enu.north_m;
    bool reported_rear_block = false;
    for (int tick=0; tick<300; ++tick) {
        if (tick != 0 && tick%60 == 0) client.control();
        harness.tick();
        const auto navigation = harness.navigation();
        reported_rear_block |= navigation.lane_change_wait_reason == "reverse_path_occupied";
        require(harness.body().center_enu.north_m >= initial_north-0.05,
            "a local escape planner must not manufacture room through a rear vehicle");
        require(navigation.lane_changes_completed == 0,
            "an unsafe close manoeuvre must remain stopped until a real rear/adjacent gap exists");
    }
    require(reported_rear_block,
        "blocked reverse space must be visible in bounded planner diagnostics");
}

void test_close_blocker_uses_only_the_available_retreat_needed_for_a_safe_pass()
{
    auto config = ego_blocked_parallel_config();
    config.npc_start_offset_m = 43.5;
    config.npc_max_speed_mps = 4.0;
    config.runtime_entities.push_back({3001,simcore_host::RuntimeEntityKind::NpcVehicle,
        {"navigation-limited-rear-gap",
            simcore_host::ObbPrism{{0,-14.0},0.85,0.0,2.2,1.0,0.75},
            {},0.0,{0.8,0.08},1500.0,2600.0,30.0}});
    Harness harness(std::move(config));
    Client client{harness}; client.open(); client.control();
    const double initial_north = harness.body().center_enu.north_m;
    const simcore_host::ObbPrism rear{{0,-14.0},0.85,0.0,2.2,1.0,0.75};
    const simcore_host::ObbPrism ego{{0,0},0.85,0.0,2.15,1.0,0.75};
    bool backing = false, passed = false;
    for (int tick = 0; tick < 1700; ++tick) {
        if (tick != 0 && tick % 60 == 0) client.control();
        harness.tick();
        const auto body = harness.body();
        const auto navigation = harness.navigation();
        backing |= navigation.avoidance_phase == "backing";
        require(!simcore_host::intersect_obb_prisms(body, rear)
                && !simcore_host::intersect_obb_prisms(body, ego),
            "minimum-space escape must stay clear of both front and rear vehicles");
        require(initial_north - body.center_enu.north_m <= 2.05,
            "planner must not demand the old full six-metre retreat when less room suffices");
        passed = navigation.lane_changes_completed != 0 && body.center_enu.north_m > 5.0;
        if (passed) break;
    }
    require(backing && passed,
        "an occupied six-metre rear envelope must not veto a shorter collision-free escape");
}

void test_downed_pedestrian_near_real_stopline(bool rear_occupied, bool crashed_vehicle = false)
{
    const CityFixture city;
    auto config = city_config(city);
    config.physics_frequency_hz = 60.0;
    config.npc_max_speed_mps = 4.0;
    const auto source = std::find_if(city.traffic->lanes.begin(), city.traffic->lanes.end(),
        [](const auto& lane) { return lane.id == 1010; });
    require(source != city.traffic->lanes.end() && source->lane_changes.size() == 2,
        "injured-pedestrian regression requires the actual three-lane city approach");
    // Reproduce the queue beyond the authored lane-change window, not an
    // obstacle on an unlimited synthetic straight. The remaining 8m blend
    // requires more room than the former generic six-metre retreat cap.
    double source_length = 0.0;
    for (std::size_t point = 1; point < source->points.size(); ++point) {
        const auto& a = source->points[point-1];
        const auto& b = source->points[point];
        source_length += std::hypot(std::hypot(b.east_m-a.east_m,b.north_m-a.north_m),b.up_m-a.up_m);
    }
    config.npc_start_offset_m = source_length - 3.5;
    const auto spawn = npc_spawn_anchor(config);
    const simcore_host::ObbPrism pedestrian{
        {spawn.east_m + (crashed_vehicle ? 5.4 : 4.0), spawn.north_m},
        crashed_vehicle ? 0.85 : 0.3,std::numbers::pi/2.0,
        crashed_vehicle ? 2.2 : 0.95,crashed_vehicle ? 1.0 : 0.35,crashed_vehicle ? 0.75 : 0.25};
    simcore_host::RuntimeEntityState injured{3001,crashed_vehicle
        ? simcore_host::RuntimeEntityKind::NpcVehicle : simcore_host::RuntimeEntityKind::Pedestrian,
        {"navigation-stopline-accident-victim",pedestrian,{},0.0,{0.7,0.0},
            crashed_vehicle ? 1500.0 : 80.0,crashed_vehicle ? 2600.0 : 30.0,15.0}};
    injured.pedestrian_downed = !crashed_vehicle;
    injured.damage_percent = 100.0f;
    injured.recovery_phase = static_cast<std::uint32_t>(simcore_host::ImpactRecoveryPhase::Disabled);
    config.runtime_entities.push_back(injured);
    const simcore_host::ObbPrism rear{
        {spawn.east_m - 5.4, spawn.north_m},0.85,std::numbers::pi/2.0,2.2,1.0,0.75};
    if (rear_occupied) {
        config.runtime_entities.push_back({3002,simcore_host::RuntimeEntityKind::NpcVehicle,
            {"navigation-stopline-rear-blocker",rear,{},0.0,{0.8,0.0},1500.0,2600.0,30.0}});
    }
    // Keep the exported lanes/stopline and initial all-red. A longer green
    // isolates the escape from waiting an additional complete signal cycle.
    auto traffic = std::make_shared<simcore_host::TrafficNetwork>(*city.traffic);
    // Keep an authored clear lane deterministic and provide only that lane's
    // movement with a long green after an initial all-red. This isolates the
    // geometric escape from waiting a second complete junction cycle.
    for (auto& lane : traffic->lanes) {
        if (lane.id == 1010) std::stable_sort(lane.lane_changes.begin(),lane.lane_changes.end(),
            [](const auto& a,const auto& b) {
                return (a.target_lane_id == 1015) > (b.target_lane_id == 1015);
            });
    }
    const auto passing_lane = std::find_if(traffic->lanes.begin(),traffic->lanes.end(),
        [](const auto& lane) { return lane.id == 1015; });
    require(passing_lane != traffic->lanes.end(), "real protected passing lane must exist");
    for (auto& plan : traffic->signal_plans) {
        if (std::find(plan.groups.begin(),plan.groups.end(),passing_lane->signal_group_id)
            != plan.groups.end()) {
            plan.phases = {{2000,{},{}},{20000,{source->signal_group_id},{}},
                {2000,{},{}},{60000,{passing_lane->signal_group_id},{}}};
            plan.offset_ms = 0;
            plan.cycle_ms = 84000;
        }
    }
    config.traffic_network = traffic;
    Harness harness(std::move(config));
    Client client{harness}; client.open(); client.control();
    const auto initial = harness.body();
    auto previous = initial;
    double maximum_retreat = 0.0;
    bool backing = false, passed = false, rejected_rear = false;
    std::uint32_t horns = 0;
    const int maximum_ticks = rear_occupied ? 900 : 2400;
    for (int tick = 0; tick < maximum_ticks; ++tick) {
        if (tick % 30 == 0) client.control();
        harness.tick();
        const auto body = harness.body();
        const auto navigation = harness.navigation();
        maximum_retreat = std::max(maximum_retreat, initial.center_enu.east_m - body.center_enu.east_m);
        backing |= navigation.avoidance_phase == "backing";
        rejected_rear |= navigation.lane_change_wait_reason == "reverse_path_occupied";
        for (const auto& entity : harness.host.runtime_entities()) {
            if (entity.entity_id == npc_id) horns = entity.horn_event_sequence;
        }
        require(horns == 0,
            "an already injured pedestrian must receive no obstruction or TTC horns");
        require(maximum_retreat <= 16.05,
            "injured-pedestrian escape must stay inside its sixteen-metre cap");
        require(!simcore_host::intersect_obb_prisms(body,pedestrian),
            "making space for an escape must never touch the injured pedestrian");
        require(std::hypot(body.center_enu.east_m-previous.center_enu.east_m,
                           body.center_enu.north_m-previous.center_enu.north_m) < 0.2,
            "stopline escape must reverse and steer continuously, never teleport");
        for (const auto& wall : city.package.collision_world->static_colliders())
            require(!simcore_host::intersect_obb_prisms(body,wall.shape),
                "an extended retreat must not use a sidewalk or cut through street furniture");
        if (tick < 120) require(body.center_enu.east_m + body.half_length_m <= -18.0 + 0.05,
            "the initial red signal still forbids entering the intersection");
        if (rear_occupied) {
            require(maximum_retreat < 0.05 && !navigation.changing_lane
                    && !simcore_host::intersect_obb_prisms(body,rear),
                "a downed pedestrian cannot authorize reversing into a queued rear vehicle");
        } else if ((navigation.lane_changes_completed != 0 || navigation.local_bypasses_completed != 0)
            && body.center_enu.east_m > pedestrian.center_enu.east_m + 3.5) {
            passed = true;
            break;
        }
        previous = body;
    }
    std::cout << "[NPC regression] downed_stopline rear_occupied=" << rear_occupied
              << " crashed_vehicle=" << crashed_vehicle
              << " retreat_m=" << maximum_retreat << " passed=" << passed
              << " horns=" << horns << " lane=" << harness.navigation().lane_id
              << " wait=" << harness.navigation().lane_change_wait_reason << "\n";
    if (rear_occupied) {
        require(rejected_rear && !backing && horns == 0,
            "a blocked rear gap must remain safe and silent for the injured person");
    } else {
        require(backing && maximum_retreat > 0.0 && maximum_retreat<=16.0 && passed,
            "a stopline queue must reverse only for real turning room, then actually pass the injured person");
    }
}
void test_actual_curve_truck_passes_without_an_authored_neighbour(bool close_blocker = false)
{
    const CityFixture city;
    auto config=city_config(city);
    config.physics_frequency_hz=60.0;
    config.npc_route={3001,2020,2021,2024,3004,1010,1011,1014};
    config.npc_start_offset_m=17.5126;
    config.npc_spacing_m=35.0;
    config.npc_count=3;
    simcore_host::NpcLaneFollower reference;
    reference.rebuild(*city.traffic,*city.package.ground_query,config.npc_route,true,87.5126);
    const simcore_host::NpcRoutePlanner route_planner(*city.traffic);
    const auto autonomous_route=route_planner.choose_destination(3001,1003,0);
    require(autonomous_route.has_value(),"truck fixture must use its actual deterministic destination");
    reference.reroute(*city.traffic,autonomous_route->lane_ids);
    require(reference.state().lane_id==3001,"truck regression must use the actual reported lane and station");
    const auto obstacle_sample=reference.sample_ahead(close_blocker ? 6.0 : 16.0);
    require(obstacle_sample.has_value(),"the real curved road needs enough forward support");
    const simcore_host::ObbPrism obstacle{{obstacle_sample->position_enu.east_m,obstacle_sample->position_enu.north_m},
        obstacle_sample->position_enu.up_m+.85,obstacle_sample->heading_deg*std::numbers::pi/180.0,2.2,1.0,.75};
    for (const bool closed_road : {false,true}) {
        if (close_blocker) break;
        auto barrier_box=obstacle;
        if (closed_road) barrier_box.half_width_m=10.0;
        const auto started=SimulationHost::Clock::now();
        const auto plan=simcore_host::plan_npc_local_bypass(*city.traffic,*city.package.ground_query,
            reference,22.0,{3.10,1.05,8.5},[&](const auto& sample,double) {
                const simcore_host::ObbPrism body{{sample.position_enu.east_m,sample.position_enu.north_m},
                    sample.position_enu.up_m+1.376071,sample.heading_deg*std::numbers::pi/180.0,3.22,1.17,.965};
                if (simcore_host::intersect_obb_prisms(body,barrier_box)) return false;
                return std::none_of(city.package.collision_world->static_colliders().begin(),
                    city.package.collision_world->static_colliders().end(),[&](const auto& collider) {
                        const double reach=body.half_length_m+body.half_width_m
                            +collider.shape.half_length_m+collider.shape.half_width_m;
                        if (std::abs(body.center_enu.east_m-collider.shape.center_enu.east_m)>reach
                            || std::abs(body.center_enu.north_m-collider.shape.center_enu.north_m)>reach) return false;
                        return simcore_host::intersect_obb_prisms(body,collider.shape).has_value();
                    });
            });
        std::cout << "[NPC regression] v8_local_search closed_road=" << closed_road
                  << " found=" << plan.has_value() << " search_ms="
                  << std::chrono::duration<double,std::milli>(SimulationHost::Clock::now()-started).count() << "\n";
        require(closed_road ? !plan.has_value() : plan.has_value(),
            "the actual curved-road search must distinguish a parked car from a road-spanning solid barrier");
        if (plan) {
            require(!plan->borrows_opposing_space(),
                "the real-curve truck must keep its bypass on the permitted side of the centreline");
            for (double s=plan->begin_m();s<plan->end_m();s+=0.01)
                require(plan->sample(s).has_value(),
                    "centreline checks must not introduce holes between planned samples at curved lane joints");
        }
    }
    simcore_host::RuntimeEntityState crashed{3001,simcore_host::RuntimeEntityKind::NpcVehicle,
        {"local-bypass-curve-crash",obstacle,{},0,{.8,0},1500,2600,30}};
    crashed.damage_percent=100;
    crashed.collision_event_sequence=1;
    crashed.recovery_phase=4;
    config.runtime_entities.push_back(crashed);
    Harness harness(std::move(config));
    Client client{harness}; client.open(); client.control();
    auto previous=harness.body(1003);
    bool began=false,completed=false;
    bool backed=false;
    int first_reverse_tick=-1;
    double minimum_station=87.5126;
    double maximum_tick_ms=0.0;
    for (int tick=0;tick<(close_blocker ? 3600 : 1800);++tick) {
        if (tick%30==0) client.control();
        const auto started=SimulationHost::Clock::now();
        harness.tick();
        maximum_tick_ms=std::max(maximum_tick_ms,
            std::chrono::duration<double,std::milli>(SimulationHost::Clock::now()-started).count());
        const auto body=harness.body(1003);
        const auto navigation=harness.host.npc_navigation();
        const auto truck=std::find_if(navigation.begin(),navigation.end(),[](const auto& state) { return state.entity_id==1003; });
        require(truck!=navigation.end() && truck->vehicle_profile=="truck","the local host path must drive the real truck profile");
        began|=truck->local_bypass_active;
        if (!began && truck->lane_id==3001)
            minimum_station=std::min(minimum_station,truck->lane_offset_m);
        if (truck->avoidance_phase=="backing") {
            backed=true;
            if (first_reverse_tick<0) first_reverse_tick=tick;
        }
        require(std::hypot(body.center_enu.east_m-previous.center_enu.east_m,
            body.center_enu.north_m-previous.center_enu.north_m)<.2,"truck local path must remain continuous");
        require(!simcore_host::intersect_obb_prisms(body,obstacle),"truck must never clip its full hull through the blocked lead car");
        for (const auto& collider:city.package.collision_world->static_colliders())
            require(!simcore_host::intersect_obb_prisms(body,collider.shape),"truck local path must avoid the real curb and wall collision boxes");
        if (truck->local_bypasses_completed!=0) { completed=true; break; }
        previous=body;
    }
    const auto navigation=harness.host.npc_navigation();
    const auto truck=std::find_if(navigation.begin(),navigation.end(),[](const auto& state) { return state.entity_id==1003; });
    std::cout << "[NPC regression] local_curve_truck began=" << began << " completed=" << completed
              << " max_tick_ms=" << maximum_tick_ms << " wait=" << truck->lane_change_wait_reason
              << " lane=" << truck->lane_id << " station_m=" << truck->lane_offset_m
              << " first_reverse_tick=" << first_reverse_tick
              << " retreat_m=" << 87.5126-minimum_station << "\n";
    require(began && completed,"the reported real-curve truck must pass and return without an authored adjacent lane");
    if (close_blocker) {
        require(backed && first_reverse_tick<90,
            "a bumper-distance truck must promptly make rear space rather than wait for a complete forward path");
        require(minimum_station<87.5126-0.4 && minimum_station>=87.5126-16.05,
            "close-curve reverse must move physically and respect the bounded accident retreat allowance");
    }
}
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "--interrupted-local-bypass") {
            test_interrupted_local_bypass_returns_without_crossing_centerline();
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--interrupted-maneuver") {
            test_interrupted_maneuver_retraces_anchor_and_replans();
            test_interrupted_maneuver_waits_for_rear_clearance_and_resumes();
            test_temporary_maneuver_interruptions_do_not_reverse();
            test_interrupted_maneuver_beyond_retreat_limit_waits_and_resumes();
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--close-curve-truck") {
            test_actual_curve_truck_passes_without_an_authored_neighbour(true);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--local-bypass") {
            test_actual_curve_truck_passes_without_an_authored_neighbour();
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--crashed-stopline") {
            test_downed_pedestrian_near_real_stopline(false,true);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--downed-stopline") {
            test_downed_pedestrian_near_real_stopline(false);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--downed-rear-blocked") {
            test_downed_pedestrian_near_real_stopline(true);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--delayed-crash") {
            const CityFixture city;
            test_crash_bypass_waits_for_moving_adjacent_traffic_without_consuming_turning_room(city);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--mixed-fleet") {
            test_deterministic_mixed_fleet_profiles_drive_distinct_host_physics();
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--close-escape") {
            test_close_blocker_triggers_bounded_reverse_then_safe_pass();
            test_close_blocker_does_not_reverse_into_rear_vehicle();
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--limited-close-escape") {
            test_close_blocker_uses_only_the_available_retreat_needed_for_a_safe_pass();
            return 0;
        }
        const CityFixture city;
        test_real_package_determinism_and_lifecycle(city);
        test_real_package_continuous_lane_change(city);
        test_unsafe_adjacent_rear_gap_blocks_lane_change(city);
        test_real_crash_near_window_end_is_passed_only_with_a_clear_legal_lane(city);
        test_future_lane_detour_and_signal_queue_distinction();
        test_downed_pedestrian_blocks_future_lane_detour();
        test_stationary_ego_triggers_early_safe_bypass_and_reachable_destination();
        test_nearby_route_end_is_not_a_physical_obstruction();
        std::cout << "npc_navigation_host_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_navigation_host_test: " << error.what() << '\n';
        return 1;
    }
}
