#include "simulation_host.hpp"

#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef SIMCORE_TEST_VIRTUAL_CITY_MAP_PACKAGE_PATH
#error "npc_host_tests requires SIMCORE_TEST_VIRTUAL_CITY_MAP_PACKAGE_PATH"
#endif

namespace {

constexpr double tick_seconds = 0.001;
constexpr std::uint64_t tick_ns = 1'000'000;
constexpr std::uint32_t npc_id = 1001;
const std::vector<std::uint32_t> loop_route{260, 270, 280, 290, 200, 210, 220, 230, 240, 250};

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

bool near_value(double a, double b, double tolerance = 1e-7)
{
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance;
}

simcore::Envelope parse(const std::string& bytes)
{
    simcore::Envelope result;
    require(result.ParseFromString(bytes) && result.has_world_state()
                && result.world_state().has_health(),
            "host callback must contain a real atomic protobuf WorldState/Health");
    return result;
}

const simcore::EntityState* find_npc(const simcore::Envelope& envelope)
{
    const simcore::EntityState* result = nullptr;
    for (const auto& entity : envelope.world_state().entities()) {
        if (entity.entity_id() != npc_id) { continue; }
        require(result == nullptr, "wire must never publish duplicate NPC ID 1001");
        require(entity.entity_kind() == simcore::ENTITY_KIND_NPC_VEHICLE,
                "lane NPC must retain the runtime NPC entity kind");
        result = &entity;
    }
    return result;
}

simcore::EntityState npc(const simcore::Envelope& envelope)
{
    const auto* found = find_npc(envelope);
    require(found != nullptr, "a ground-supported, unblocked lane NPC must be published");
    return *found;
}

simcore::EntityState entity_by_id(
    const simcore::Envelope& envelope, std::uint32_t entity_id)
{
    for (const auto& entity : envelope.world_state().entities()) {
        if (entity.entity_id() == entity_id) return entity;
    }
    throw std::runtime_error("expected runtime entity ID was not published");
}

struct CityFixture {
    simcore_host::RuntimeMapPackage package;
    std::shared_ptr<const simcore_host::TrafficNetwork> traffic;

    CityFixture()
        : package(simcore_host::load_runtime_map_package(
              std::filesystem::path{SIMCORE_TEST_VIRTUAL_CITY_MAP_PACKAGE_PATH}))
        , traffic(std::make_shared<const simcore_host::TrafficNetwork>(
              simcore_host::load_traffic_network(
                  std::filesystem::path{SIMCORE_TEST_VIRTUAL_CITY_MAP_PACKAGE_PATH}
                      / "traffic_network.json",
                  package.collision_checksum, *package.ground_query)))
    {
        require(package.ground_query && package.collision_world
                    && traffic->source_map_checksum == package.collision_checksum,
                "test must load the real verified city ground/static/traffic package");
        require(traffic->lanes.size() == 25 && traffic->signals.size() == 3,
                "authored city fixture must expose the 25-lane, 3-head network");
    }
};

class RecoverableCityGround final : public simcore_host::GroundQuery {
public:
    explicit RecoverableCityGround(std::shared_ptr<const simcore_host::GroundQuery> actual)
        : actual_(std::move(actual))
    {}

    bool available = true;

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        return available ? actual_->query_down(request) : std::nullopt;
    }

private:
    std::shared_ptr<const simcore_host::GroundQuery> actual_;
};

SimulationHostConfig city_config(const CityFixture& city)
{
    SimulationHostConfig config;
    config.origin_lat = 40.7069;
    config.origin_lon = -74.0095;
    config.spawn_heading = 90.0f;
    config.physics_frequency_hz = 1.0 / tick_seconds;
    config.command_timeout = std::chrono::seconds(5);
    config.hard_command_timeout = std::chrono::seconds(20);
    config.max_command_queue_age = std::chrono::seconds(2);
    config.source_id = "npc-host-test";
    config.map_package_checksum = city.package.collision_checksum;
    config.ground_query = city.package.ground_query;
    config.collision_world = city.package.collision_world;
    config.traffic_network = city.traffic;
    config.require_client_hello = true;
    config.npc_route = loop_route;
    config.npc_route_loop = true;
    config.npc_start_offset_m = 96.0;
    config.npc_max_speed_mps = 6.0;
    return config;
}

struct Harness {
    boost::asio::io_context ioc;
    std::vector<simcore::Envelope> frames;
    std::vector<std::string> close_reasons;
    SimulationHost host;

    explicit Harness(SimulationHostConfig config)
        : host(ioc, std::move(config), {
              [this](const std::string& bytes) { frames.push_back(parse(bytes)); },
              {},
              [this](const std::string& reason) { close_reasons.push_back(reason); }})
    {}

    simcore::Envelope snapshot() { return parse(host.make_initial_world_state()); }

    void tick()
    {
        // Exercise the real public timer callback, not run_tick/private state.
        // Map installation consumes a callback without publishing an old-session
        // frame, so count timer completions rather than assuming one frame/tick.
        host.start();
        require(ioc.run_one() == 1, "one host fixed-timer callback must run");
        host.stop();
        (void)ioc.poll();
        ioc.restart();
    }
};

struct Client {
    Harness& harness;
    std::string session;
    std::uint64_t generation;
    std::uint64_t sequence = 0;
    std::uint64_t client_time_ns = 0;

    simcore::Envelope envelope()
    {
        simcore::Envelope result;
        result.set_schema_version(simcore_host::kProtocolSchemaVersion);
        result.set_sequence(++sequence);
        result.set_source_id("npc-test-unreal-client");
        result.set_session_id(session);
        result.set_map_package_checksum(harness.host.map_package_checksum());
        return result;
    }

    std::uint64_t time()
    {
        const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            SimulationHost::Clock::now().time_since_epoch()).count());
        client_time_ns = std::max(client_time_ns + 1, now);
        return client_time_ns;
    }

    ClientMessageResult open(std::string_view play_session)
    {
        auto greeting = envelope();
        auto* hello = greeting.mutable_hello();
        hello->set_build("npc-host-test");
        hello->set_schema(std::string(simcore_host::kProtocolSchemaName));
        for (const char* capability : {"world-state.v2", "control.v2", "simulation-reset.v1",
                                       "map-package-checksum.v1"}) {
            hello->add_capabilities(capability);
        }
        require(harness.host.handle_client_message(greeting.SerializeAsString(), generation)
                    == ClientMessageResult::HelloAccepted,
                "test client must negotiate its actual Hello before reset/control");
        auto reset = envelope();
        reset.mutable_simulation_reset()->set_play_session_id(std::string(play_session));
        reset.mutable_simulation_reset()->set_client_time_ns(time());
        return harness.host.handle_client_message(reset.SerializeAsString(), generation);
    }

    void control(float throttle = 0.0f, float brake = 1.0f,
                 simcore::VehicleGear gear = simcore::VEHICLE_GEAR_DRIVE,
                 SimulationHost::Clock::time_point received_at = SimulationHost::Clock::now())
    {
        auto message = envelope();
        auto* command = message.mutable_control_command();
        command->set_mode(simcore::CONTROL_MODE_MANUAL);
        command->set_throttle(throttle);
        command->set_brake(brake);
        command->set_gear(gear);
        command->set_client_time_ns(time());
        require(harness.host.handle_client_message(message.SerializeAsString(), generation, received_at)
                    == ClientMessageResult::ControlAccepted,
                "fresh ordered public control must arm or recover the lane-NPC lifecycle");
    }
};

void test_real_city_spawn_and_exactly_one_npc_advance(const CityFixture& city)
{
    Harness harness(city_config(city));
    const auto initial = harness.snapshot();
    const auto spawned = npc(initial);
    const auto ground = city.package.ground_query->query_down({{20.0, 0.0, 2.0}, 4.0});
    require(ground.has_value(), "real city spawn must have authoritative ground");
    require(near_value(spawned.position_enu().x(), 20.0)
                && near_value(spawned.position_enu().y(), 0.0)
                && near_value(spawned.position_enu().z(), ground->point_enu.up_m + 0.85)
                && near_value(spawned.heading(), 90.0)
                && near_value(spawned.collision_half_length(), 2.2, 1e-6)
                && near_value(spawned.collision_half_width(), 1.0)
                && near_value(spawned.collision_half_height(), 0.75),
            "route 260 offset 96 must spawn ENU(20,0) with the one authoritative OBB/clearance");
    require(initial.world_state().health().status() == "awaiting_reset" && near_value(spawned.speed(), 0),
            "spawn publication must not bypass the reset/control lifecycle gate");
    for (int i = 0; i < 3; ++i) { harness.tick(); }
    require(near_value(npc(harness.snapshot()).position_enu().x(), 20.0),
            "NPC must remain stationary before application lifecycle/control");

    Client client{harness, "motion-a", 1};
    require(client.open("motion-pie") == ClientMessageResult::SimulationReset,
            "new play must reset the actual host world");
    client.control();
    simcore_host::NpcLaneFollower reference;
    reference.rebuild(*city.traffic, *city.package.ground_query, loop_route, true, 96.0);
    for (std::uint64_t index = 0; index < 40; ++index) {
        const auto& expected = reference.step(tick_seconds, city.traffic->signals_at(index * tick_ns));
        const auto previous_count = harness.frames.size();
        harness.tick();
        require(harness.frames.size() == previous_count + 1,
                "one ordinary host tick must publish exactly one WorldState");
        const auto& frame = harness.frames.back();
        const auto actual = npc(frame);
        require(frame.simulation_time_ns() == (index + 1) * tick_ns,
                "test must compare actual fixed-step host time, not wall-clock estimates");
        require(near_value(actual.position_enu().x(), expected.position_enu.east_m, 1e-9)
                    && near_value(actual.position_enu().y(), expected.position_enu.north_m, 1e-9)
                    && near_value(actual.speed(), expected.speed_mps, 1e-6),
                "lane NPC must advance exactly once, not again through generic runtime integration");
        require(frame.world_state().health().status() == "active",
                "motion comparison requires genuine active control authority");
    }
    require(npc(harness.snapshot()).position_enu().x() > 20.0,
            "active host ticks must actually move the NPC");
}

void test_new_play_resets_but_same_play_reconnect_keeps_progress(const CityFixture& city)
{
    Harness harness(city_config(city));
    Client first{harness, "lifecycle-a", 1};
    require(first.open("lifecycle-pie-a") == ClientMessageResult::SimulationReset,
            "first play must initialize NPC lifecycle");
    first.control();
    for (int i = 0; i < 20; ++i) { harness.tick(); }
    const auto before = harness.snapshot();
    const double east = npc(before).position_enu().x();
    require(east > 20.0, "reconnect fixture must have nonzero authoritative progress");

    Client reconnected{harness, "lifecycle-b", 2};
    require(reconnected.open("lifecycle-pie-a") == ClientMessageResult::DuplicateSimulationReset,
            "same PlaySession on a new Hello connection must keep the world");
    const auto kept = harness.snapshot();
    require(kept.simulation_time_ns() == before.simulation_time_ns()
                && near_value(npc(kept).position_enu().x(), east),
            "reconnect must preserve NPC progress and the existing simulation clock");
    for (int i = 0; i < 4; ++i) { harness.tick(); }
    const auto waiting = harness.snapshot();
    require(waiting.world_state().health().status() == "awaiting_control"
                && near_value(npc(waiting).position_enu().x(), east) && near_value(npc(waiting).speed(), 0.0),
            "reconnected NPC must freeze until fresh control re-arms authority");
    reconnected.control();
    for (int i = 0; i < 4; ++i) { harness.tick(); }
    require(npc(harness.snapshot()).position_enu().x() > east,
            "fresh reconnect control must restart from the kept anchor");

    Client new_play{harness, "lifecycle-c", 3};
    require(new_play.open("lifecycle-pie-b") == ClientMessageResult::SimulationReset,
            "new PlaySession must intentionally reset the world");
    const auto reset = harness.snapshot();
    require(reset.simulation_time_ns() == 0 && near_value(npc(reset).position_enu().x(), 20.0)
                && near_value(npc(reset).speed(), 0),
            "new play must restore NPC start offset, speed and simulation time");
}

void test_soft_timeout_freezes_and_fresh_control_resumes(const CityFixture& city)
{
    auto config = city_config(city);
    config.command_timeout = std::chrono::milliseconds(30);
    Harness harness(std::move(config));
    Client client{harness, "soft-a", 1};
    require(client.open("soft-pie") == ClientMessageResult::SimulationReset,
            "soft-timeout test must initialize lifecycle");
    for (int i = 0; i < 5; ++i) { client.control(); harness.tick(); }
    const double before = npc(harness.snapshot()).position_enu().x();
    require(before > 20.0, "soft-timeout fixture must first move the actual NPC");
    // The public transport timestamp models a packet queued for 90ms, within
    // this fixture's 2s queue bound but beyond its 30ms control timeout. No
    // private clock, physics state or NPC pose is injected to induce the stop.
    client.control(0.0f, 1.0f, simcore::VEHICLE_GEAR_DRIVE,
                   SimulationHost::Clock::now() - std::chrono::milliseconds(90));
    harness.tick();
    const auto stopped = harness.snapshot();
    require(stopped.world_state().health().status() == "safe_stop"
                && near_value(npc(stopped).position_enu().x(), before) && near_value(npc(stopped).speed(), 0),
            "applied soft SafeStop must freeze NPC progress and zero wire velocity in that tick");
    for (int i = 0; i < 5; ++i) { harness.tick(); }
    require(near_value(npc(harness.snapshot()).position_enu().x(), before)
                && harness.close_reasons.empty(),
            "soft timeout must retain the connection and never advance a paused NPC");
    client.control();
    harness.tick();
    require(npc(harness.snapshot()).position_enu().x() > before,
            "fresh same-owner control must resume NPC motion without a new play/reset");
}

void test_static_guard_and_map_traffic_reload_remove_then_recreate(const CityFixture& city)
{
    auto config = city_config(city);
    auto colliders = city.package.collision_world->static_colliders();
    // Explicit in-memory guard fixture, not a fabricated on-disk bake: the real
    // package was loaded above; this extra wall is local to this host instance.
    colliders.push_back({"npc-test-front-wall", simcore_host::StaticColliderSemantic::Wall,
                         {{23.0, 0.0}, 1.0, 0.0, 2.0, 0.2, 1.0}, {0.8, 0.0}});
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>(std::move(colliders));
    Harness harness(std::move(config));
    Client client{harness, "reload-a", 1};
    require(client.open("reload-pie-a") == ClientMessageResult::SimulationReset,
            "static guard test must initialize lifecycle");
    client.control();
    for (int i = 0; i < 20; ++i) { harness.tick(); }
    const auto held = npc(harness.snapshot());
    require(near_value(held.position_enu().x(), 20.0) && near_value(held.speed(), 0.0),
            "full-OBB static lookahead plus margin must hold NPC before the wall");

    // Public tick-boundary installation is tested with immutable in-memory
    // identity variants of the loaded package. No source map/manifest/traffic
    // file is edited and this is not a claim that a second disk bake occurred.
    auto replacement = city.package;
    replacement.map_id = "npc-test-city-identity-2";
    replacement.collision_checksum = "fnv1a64:1111111111111111";
    harness.host.queue_map_package_reload(replacement);
    harness.tick();
    const auto mismatched = harness.snapshot();
    require(harness.host.map_package_checksum() == replacement.collision_checksum
                && !find_npc(mismatched) && !harness.close_reasons.empty(),
            "new map identity must remove an old-topology NPC and fence the old connection");
    require(mismatched.world_state().health().status() == "awaiting_reset",
            "map reload must retire motion authority before NPC reconstruction");
    for (const auto& signal : mismatched.world_state().traffic_signals()) {
        require(signal.aspect() == simcore::TRAFFIC_SIGNAL_RED,
                "old-map signal data must remain all-red during checksum mismatch");
    }

    auto foreign = std::make_shared<simcore_host::TrafficNetwork>(*city.traffic);
    foreign->checksum = "fnv1a64:2222222222222222";
    harness.host.queue_traffic_network_reload(foreign);
    harness.tick();
    require(!find_npc(harness.snapshot()), "mismatched traffic candidate must not respawn an NPC");

    auto matching = std::make_shared<simcore_host::TrafficNetwork>(*city.traffic);
    matching->source_map_checksum = replacement.collision_checksum;
    matching->checksum = "fnv1a64:3333333333333333";
    harness.host.queue_traffic_network_reload(matching);
    harness.tick();
    const auto rebuilt = harness.snapshot();
    require(rebuilt.world_state().traffic_network_checksum() == matching->checksum
                && near_value(npc(rebuilt).position_enu().x(), 20.0) && near_value(npc(rebuilt).speed(), 0),
            "matching traffic must reconstruct the cleared NPC at its original stationary offset");
    Client after_reload{harness, "reload-b", 2};
    require(after_reload.open("reload-pie-b") == ClientMessageResult::SimulationReset,
            "reloaded map must require a fresh application lifecycle");
    after_reload.control();
    for (int i = 0; i < 5; ++i) { harness.tick(); }
    require(npc(harness.snapshot()).position_enu().x() > 20.0,
            "the removed test wall must allow a newly authorized NPC to resume");
}

void test_ego_overlap_defers_spawn_and_actual_reverse_input_releases_it(const CityFixture& city)
{
    auto config = city_config(city);
    const double ego_half_length = config.vehicle_parameters.wheelbase_m * 0.5 + 0.8;
    // First lane starts at E=-76. Put NPC only 1mm inside the initial Ego's
    // combined OBB extents, so real reverse pedals can clear the overlap in a
    // short timer run; neither Ego nor NPC pose is set through a test hook.
    config.npc_start_offset_m = 76.0 + ego_half_length + 2.2 - 0.001;
    Harness harness(std::move(config));
    require(!find_npc(harness.snapshot()),
            "constructor must defer NPC materialization inside the actual Ego footprint");
    Client client{harness, "ego-release-a", 1};
    require(client.open("ego-release-pie") == ClientMessageResult::SimulationReset,
            "Ego overlap test must initialize lifecycle");
    require(!find_npc(harness.snapshot()), "new play must not bypass a blocked NPC spawn");
    client.control(1.0f, 0.0f, simcore::VEHICLE_GEAR_REVERSE);
    bool released = false;
    for (int index = 0; index < 500; ++index) {
        if (index != 0 && index % 40 == 0) {
            client.control(1.0f, 0.0f, simcore::VEHICLE_GEAR_REVERSE);
        }
        harness.tick();
        const auto frame = harness.snapshot();
        const auto* spawned = find_npc(frame);
        if (!spawned) { continue; }
        const auto ego = harness.host.state();
        const simcore_host::ObbPrism ego_shape{
            {ego.position_enu.x, ego.position_enu.y},
            ego.position_enu.z - VehicleParameters{}.cg_height_m + 0.85,
            ego.heading * std::numbers::pi / 180.0, ego_half_length,
            VehicleParameters{}.front_track_m * 0.5 + 0.15, 0.75};
        const simcore_host::ObbPrism npc_shape{
            {spawned->position_enu().x(), spawned->position_enu().y()},
            spawned->position_enu().z(), spawned->heading() * std::numbers::pi / 180.0,
            spawned->collision_half_length(), spawned->collision_half_width(),
            spawned->collision_half_height()};
        require(ego.position_enu.x < -0.001
                    && !simcore_host::intersect_obb_prisms(npc_shape, ego_shape),
                "NPC may appear only after real reverse motion clears Ego, without overlap");
        released = true;
        break;
    }
    require(released, "real reverse pedal input must clear and release the deferred NPC spawn");
}

simcore_host::RuntimeEntityState remote_entity(std::uint32_t id, std::string proxy_id)
{
    return {id, simcore_host::RuntimeEntityKind::NpcVehicle,
            {std::move(proxy_id),
             simcore_host::ObbPrism{{1000.0 + static_cast<double>(id), 1000.0},
                                    0.85, 0.0, 2.2, 1.0, 0.75},
             {}, 0.0, {0.8, 0.0}}};
}

void test_reload_runtime_replacements_preserve_the_reserved_npc_slot(const CityFixture& city)
{
    std::vector<std::vector<simcore_host::RuntimeEntityState>> invalid_replacements;
    invalid_replacements.push_back({remote_entity(1001, "test-other-proxy")});
    invalid_replacements.push_back({remote_entity(2001, "lane-npc-1001")});
    std::vector<simcore_host::RuntimeEntityState> full_runtime;
    for (std::uint32_t index = 0; index < 255; ++index) {
        full_runtime.push_back(remote_entity(2000 + index, "test-slot-" + std::to_string(index)));
    }
    invalid_replacements.push_back(std::move(full_runtime));

    Harness harness(city_config(city));
    auto candidate = city.package;
    candidate.map_id = "npc-test-reserved-slot";
    candidate.collision_checksum = "fnv1a64:4444444444444444";
    for (const auto& replacement : invalid_replacements) {
        bool constructor_rejected = false;
        try {
            auto config = city_config(city);
            config.runtime_entities = replacement;
            Harness invalid(std::move(config));
        } catch (const std::invalid_argument&) {
            constructor_rejected = true;
        }
        require(constructor_rejected,
                "initial runtime entities must reserve ID 1001, its proxy identity and one wire slot");

        bool reload_rejected = false;
        try {
            harness.host.queue_map_package_reload(candidate, replacement);
        } catch (const std::invalid_argument&) {
            reload_rejected = true;
        }
        require(reload_rejected,
                "reload replacements must enforce the same NPC ID/proxy/capacity reservation as construction");
        harness.tick();
        const auto unchanged = harness.snapshot();
        require(harness.host.map_package_checksum() == city.package.collision_checksum
                    && near_value(npc(unchanged).position_enu().x(), 20.0)
                    && unchanged.world_state().entities_size() == 2
                    && harness.close_reasons.empty(),
                "rejected runtime replacement must not queue a map swap, replace the NPC or consume its slot");
    }
}

void test_ground_recovery_materializes_at_route_anchor_without_teleport_velocity(const CityFixture& city)
{
    auto ground = std::make_shared<RecoverableCityGround>(city.package.ground_query);
    auto config = city_config(city);
    config.ground_query = ground;
    Harness harness(std::move(config));
    require(near_value(npc(harness.snapshot()).position_enu().x(), 20.0),
            "recovery fixture must begin with the configured non-origin spawn");

    // Only the public query result changes. Reset must remove the old runtime
    // NPC when authoritative support is missing, without installing its default
    // (zero-initialized) controller state as a replacement entity.
    ground->available = false;
    Client client{harness, "ground-recovery-a", 1};
    require(client.open("ground-recovery-pie") == ClientMessageResult::SimulationReset,
            "ground recovery fixture must use the actual lifecycle reset path");
    require(!find_npc(harness.snapshot()), "no-ground reset must defer, not spawn an NPC at the origin");
    client.control();
    for (int index = 0; index < 3; ++index) {
        harness.tick();
        require(!find_npc(harness.snapshot()), "NPC must stay absent while ground queries have no hit");
    }

    ground->available = true;
    harness.tick();
    auto recovered = npc(harness.snapshot());
    require(recovered.position_enu().x() >= 20.0
                && recovered.position_enu().x() < 20.0001
                && near_value(recovered.position_enu().y(), 0.0)
                && recovered.speed() > 0.0f && recovered.speed() < 0.01f
                && std::abs(recovered.linear_velocity_enu().x()) < 0.01
                && std::abs(recovered.linear_velocity_enu().y()) < 0.01,
            "ground recovery must spawn at ENU(20,0), never derive huge velocity from a default origin");
    for (int index = 0; index < 10; ++index) {
        const auto before = recovered;
        harness.tick();
        recovered = npc(harness.snapshot());
        require(near_value(recovered.position_enu().x() - before.position_enu().x(),
                     recovered.linear_velocity_enu().x() * tick_seconds, 1e-9)
                    && recovered.speed() - before.speed() <= 1.5 * tick_seconds + 1e-6,
                "recovered NPC must use the ordinary one-tick chord and finite acceleration");
    }
}

void test_four_lane_npcs_use_stable_ids_and_spaced_offsets(const CityFixture& city)
{
    auto config = city_config(city);
    config.npc_count = 4;
    config.npc_spacing_m = 120.0;
    Harness harness(std::move(config));
    std::vector<std::uint32_t> ids;
    for (const auto& entity : harness.host.runtime_entities()) {
        if (entity.kind == simcore_host::RuntimeEntityKind::NpcVehicle) {
            ids.push_back(entity.entity_id);
        }
    }
    require(ids == std::vector<std::uint32_t>{1001, 1002, 1003, 1004},
            "four configured lane NPCs must publish stable contiguous identities");
    std::vector<simcore_host::CollisionVector2> centres;
    for (const auto& entity : harness.host.runtime_entities()) {
        if (entity.kind != simcore_host::RuntimeEntityKind::NpcVehicle) continue;
        const auto& body = std::get<simcore_host::ObbPrism>(entity.collision_proxy.shape);
        for (const auto& previous : centres) {
            require(std::hypot(body.center_enu.east_m - previous.east_m,
                               body.center_enu.north_m - previous.north_m) > 10.0,
                    "same-route NPC spawn offsets must not overlap");
        }
        centres.push_back(body.center_enu);
    }
}

void test_ego_impact_pushes_finite_npc_without_next_tick_snapback(
    const CityFixture& city)
{
    auto config = city_config(city);
    constexpr double spawn_east_m = 4.36;
    constexpr double npc_route_speed_mps = 0.01;
    config.npc_start_offset_m = 76.0 + spawn_east_m;
    config.npc_max_speed_mps = npc_route_speed_mps;
    Harness harness(std::move(config));
    const auto initial = npc(harness.snapshot());
    require(near_value(initial.position_enu().x(), spawn_east_m, 1e-6),
            "finite-impact fixture must spawn the NPC just beyond Ego's OBB");
    const auto& runtime = harness.host.runtime_entities().front().collision_proxy;
    require(near_value(runtime.mass_kg, 1500.0)
                && runtime.yaw_inertia_kg_m2 > 0.0
                && near_value(runtime.maximum_linear_speed_mps, 30.0),
            "managed NPC must opt into bounded finite-mass collision response");

    Client client{harness, "finite-impact-a", 1};
    require(client.open("finite-impact-pie-a") == ClientMessageResult::SimulationReset,
            "finite NPC impact test must initialize lifecycle");
    client.control(1.0f, 0.0f);
    bool displaced = false;
    double displaced_east = spawn_east_m;
    int impact_tick = 0;
    for (int index = 0; index < 3000; ++index) {
        if (index != 0 && index % 500 == 0) client.control(1.0f, 0.0f);
        harness.tick();
        const auto state = npc(harness.snapshot());
        const double nominal_upper_bound = spawn_east_m
            + npc_route_speed_mps * (index + 1) * tick_seconds;
        if (state.position_enu().x() > nominal_upper_bound + 0.02
            && state.speed() > npc_route_speed_mps + 0.01) {
            displaced = true;
            displaced_east = state.position_enu().x();
            impact_tick = index;
            break;
        }
    }
    require(displaced,
            "real Ego throttle must transfer finite impulse and displace the lane NPC");

    client.control(0.0f, 1.0f);
    harness.tick();
    const auto next = npc(harness.snapshot());
    const double nominal_next_upper_bound = spawn_east_m
        + npc_route_speed_mps * (impact_tick + 2) * tick_seconds;
    require(next.position_enu().x() > nominal_next_upper_bound + 0.01
                && next.position_enu().x() >= displaced_east - 0.031,
            "collision displacement must survive the next follower pending commit without snap-back");

    Client reset_client{harness, "finite-impact-b", 2};
    require(reset_client.open("finite-impact-pie-b")
                == ClientMessageResult::SimulationReset,
            "new PIE must reset an impacted NPC lifecycle");
    const auto reset = npc(harness.snapshot());
    require(near_value(reset.position_enu().x(), spawn_east_m, 1e-6)
                && near_value(reset.speed(), 0.0),
            "new PIE must clear finite collision offset and velocity exactly");
}

void test_ego_impact_pushes_bounded_finite_pedestrian(
    const CityFixture& city)
{
    auto config = city_config(city);
    config.npc_route.clear();
    config.traffic_network.reset();
    constexpr std::uint32_t pedestrian_id = 3001;
    constexpr double spawn_east_m = 2.51;
    const auto ground = city.package.ground_query->query_down(
        {{spawn_east_m, 0.0, 2.0}, 4.0});
    require(ground.has_value(),
            "finite pedestrian impact fixture requires authoritative ground");
    config.runtime_entities.push_back({
        pedestrian_id,
        simcore_host::RuntimeEntityKind::Pedestrian,
        {"finite-ped-test",
         simcore_host::VerticalCapsule{
             {spawn_east_m, 0.0}, ground->point_enu.up_m + 0.9, 0.35, 0.9},
         {}, 0.0, {0.75, 0.0}, 80.0, 0.0, 15.0}});
    Harness harness(std::move(config));
    Client client{harness, "finite-ped-a", 1};
    require(client.open("finite-ped-pie-a") == ClientMessageResult::SimulationReset,
            "finite pedestrian impact test must initialize lifecycle");
    client.control(1.0f, 0.0f);

    bool pushed = false;
    for (int index = 0; index < 2000; ++index) {
        if (index != 0 && index % 500 == 0) client.control(1.0f, 0.0f);
        harness.tick();
        const auto pedestrian = entity_by_id(harness.snapshot(), pedestrian_id);
        const double speed = std::hypot(
            pedestrian.linear_velocity_enu().x(),
            pedestrian.linear_velocity_enu().y());
        if (pedestrian.position_enu().x() > spawn_east_m + 0.01
            && speed > 0.05) {
            require(speed <= 15.0 + 1e-5,
                    "authoritative pedestrian reaction must respect its speed bound");
            pushed = true;
            break;
        }
    }
    require(pushed,
            "real Ego throttle must push the finite pedestrian authority");

    Client reset_client{harness, "finite-ped-b", 2};
    require(reset_client.open("finite-ped-pie-b")
                == ClientMessageResult::SimulationReset,
            "new PIE must reset an impacted pedestrian lifecycle");
    const auto reset = entity_by_id(harness.snapshot(), pedestrian_id);
    require(near_value(reset.position_enu().x(), spawn_east_m, 1e-6)
                && near_value(reset.speed(), 0.0),
            "new PIE must clear pedestrian collision displacement and velocity");
}

void test_pedestrian_pair_waits_for_and_moves_on_walk(const CityFixture& city)
{
    auto traffic = std::make_shared<simcore_host::TrafficNetwork>(*city.traffic);
    traffic->format_version = 2;
    simcore_host::TrafficSignalPlan plan;
    plan.id = 1;
    plan.groups = {1, 2};
    plan.phases = {{10000, {1}, {}}};
    plan.cycle_ms = 10000;
    traffic->signal_plans = {plan};
    std::vector<simcore_host::TrafficSignal> vehicle_group_one;
    for (auto& signal : traffic->signals) {
        signal.controller_id = 1;
        if (signal.group_id == 1) vehicle_group_one.push_back(signal);
    }
    require(vehicle_group_one.size() == 2,
            "real city fixture must expose two group-one endpoints for the crossing test");
    for (std::size_t index = 0; index < vehicle_group_one.size(); ++index) {
        auto pedestrian_head = vehicle_group_one[index];
        pedestrian_head.id = static_cast<std::uint32_t>(101 + index);
        pedestrian_head.kind = simcore_host::TrafficSignalKind::Pedestrian;
        traffic->signals.push_back(pedestrian_head);
    }

    auto config = city_config(city);
    config.npc_route.clear();
    config.traffic_network = traffic;
    Harness harness(std::move(config));
    const auto position = [&](std::uint32_t id) {
        const auto found = std::find_if(harness.host.runtime_entities().begin(),
            harness.host.runtime_entities().end(),
            [&](const auto& entity) { return entity.entity_id == id; });
        require(found != harness.host.runtime_entities().end()
                    && found->kind == simcore_host::RuntimeEntityKind::Pedestrian,
                "paired pedestrian signal heads must create two capsule authorities");
        return std::get<simcore_host::VerticalCapsule>(found->collision_proxy.shape).center_enu;
    };
    const auto first_before = position(2001);
    const auto second_before = position(2002);
    Client client{harness, "pedestrian-walk", 1};
    require(client.open("pedestrian-walk-pie") == ClientMessageResult::SimulationReset,
            "pedestrian test must enter the normal lifecycle");
    client.control();
    harness.tick();
    const auto first_after = position(2001);
    const auto second_after = position(2002);
    require(std::hypot(first_after.east_m - first_before.east_m,
                       first_after.north_m - first_before.north_m) > 0.0001
                && std::hypot(second_after.east_m - second_before.east_m,
                              second_after.north_m - second_before.north_m) > 0.0001,
            "both opposing pedestrians must begin crossing on authoritative WALK");
}

} // namespace

int main()
{
    try {
        const CityFixture city;
        test_real_city_spawn_and_exactly_one_npc_advance(city);
        test_new_play_resets_but_same_play_reconnect_keeps_progress(city);
        test_soft_timeout_freezes_and_fresh_control_resumes(city);
        test_static_guard_and_map_traffic_reload_remove_then_recreate(city);
        test_ego_overlap_defers_spawn_and_actual_reverse_input_releases_it(city);
        test_reload_runtime_replacements_preserve_the_reserved_npc_slot(city);
        test_ground_recovery_materializes_at_route_anchor_without_teleport_velocity(city);
        test_four_lane_npcs_use_stable_ids_and_spaced_offsets(city);
        test_ego_impact_pushes_finite_npc_without_next_tick_snapback(city);
        test_ego_impact_pushes_bounded_finite_pedestrian(city);
        test_pedestrian_pair_waits_for_and_moves_on_walk(city);
        std::cout << "npc_host_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_host_test: " << error.what() << '\n';
        return 1;
    }
}
