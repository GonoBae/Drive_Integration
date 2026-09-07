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
        require(traffic->lanes.size() == 58, "host navigation test needs the exported 58-lane Signal City");
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
            }, {}, {}})
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

    simcore_host::ObbPrism body() const
    {
        for (const auto& entity : host.runtime_entities()) {
            if (entity.entity_id == npc_id) {
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
        // Source 1010 still has center/corners at N=38.75/37.75/39.75.
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
    auto colliders = city.package.collision_world->static_colliders();
    colliders.push_back(barrier(-55.0, 35.0));
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(std::move(colliders));
    config.runtime_entities.push_back({3001, simcore_host::RuntimeEntityKind::NpcVehicle,
        {"navigation-test-rear-car", simcore_host::ObbPrism{
            {-88.0, 38.3}, 0.85, std::numbers::pi/2.0, 2.2, 1.0, 0.75}, {}, 0.0, {0.8, 0.0}}});
    config.runtime_entities.push_back({3002, simcore_host::RuntimeEntityKind::NpcVehicle,
        {"navigation-test-rear-car-right", simcore_host::ObbPrism{
            {-88.0, 31.7}, 0.85, std::numbers::pi/2.0, 2.2, 1.0, 0.75}, {}, 0.0, {0.8, 0.0}}});
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
                if (tick == 0) require(navigation.changing_lane,
                    "a known disabled lead car must trigger a safe plan before the remaining window is consumed");
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
    simcore_host::RuntimeEntityState crashed{3001,simcore_host::RuntimeEntityKind::NpcVehicle,
        {"navigation-wait-crashed-lead",
            simcore_host::ObbPrism{{-60.0,35.0},0.85,std::numbers::pi/2.0,2.2,1.0,0.75},
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
                simcore_host::ObbPrism{{-78.0,north},0.85,std::numbers::pi/2.0,2.2,1.0,0.75},
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
        if (tick < 60) {
            require(!navigation.changing_lane,
                "a follower cannot cut into adjacent traffic while both passing lanes are occupied");
            require(navigation.preserving_bypass_space && !navigation.lane_change_wait_reason.empty(),
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
    const std::array<double,4> half_lengths{2.20,1.75,3.65,1.10};
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
            simcore_host::ObbPrism{{0,-14.0},0.85,0.0,2.2,1.0,0.75},
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
} // namespace

int main(int argc, char** argv)
{
    try {
        const CityFixture city;
        if (argc == 2 && std::string(argv[1]) == "--delayed-crash") {
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
