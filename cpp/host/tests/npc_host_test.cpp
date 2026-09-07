#include "simulation_host.hpp"
#include "traffic/npc_return_curve.hpp"
#include "traffic/pedestrian_impact.hpp"

#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <array>
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

void test_short_and_degenerate_npc_return_curves_are_bounded()
{
    constexpr double dt = 1.0 / 60.0;
    constexpr double maximum_chord_m = 0.9 * dt;
    constexpr double maximum_yaw_rate_rad_s = 22.0 * std::numbers::pi / 180.0;
    constexpr double maximum_yaw_acceleration_rad_s2 =
        360.0 * std::numbers::pi / 180.0;

    for (const double distance : std::array{0.05, 0.10, 0.15, 0.20}) {
        const auto curve = simcore_host::make_npc_return_curve_plan(
            {0.0, -distance}, 0.0, 0.0);
        require(curve.valid && curve.direction == 1,
            "short aligned return must create a forward curve");
        const double start_handle = std::hypot(
            curve.p1.east_m - curve.p0.east_m,
            curve.p1.north_m - curve.p0.north_m);
        require(start_handle <= 0.45 * distance + 1e-12,
            "a short curve handle must scale with its chord, never use a 0.20m minimum");

        double progress = 0.0;
        simcore_host::CollisionVector2 position{0.0, -distance};
        double body_heading = 0.0;
        double yaw_speed = 0.0;
        for (int tick = 0; tick < 100 && progress < 1.0 - 1e-12; ++tick) {
            const auto tangent = simcore_host::npc_return_curve_tangent(curve, progress);
            const double requested = std::min(1.0, progress
                + maximum_chord_m / std::max(1e-9,
                    std::hypot(tangent.east_m, tangent.north_m)));
            const auto step = simcore_host::choose_bounded_npc_return_step(curve, {
                position, progress, requested, body_heading, yaw_speed, dt,
                maximum_chord_m, maximum_yaw_rate_rad_s,
                maximum_yaw_acceleration_rad_s2});
            require(step.accepted,
                "5--20cm aligned return must advance without a cusp or failed postcondition");
            const double chord = std::hypot(step.position.east_m - position.east_m,
                step.position.north_m - position.north_m);
            require(chord <= maximum_chord_m + 1e-9
                    && step.position.north_m >= position.north_m - 1e-12
                    && step.position.north_m <= 1e-12,
                "short aligned return must remain monotonic and respect its real chord budget");
            progress = step.progress;
            position = step.position;
            body_heading = step.body_heading_rad;
            yaw_speed = step.yaw_speed_rad_s;
        }
        require(progress >= 1.0 - 1e-12
                && near_value(position.east_m, 0.0, 1e-12)
                && near_value(position.north_m, 0.0, 1e-12),
            "short aligned return must finish at the authored route pose exactly");
    }

    // A nearly reversed body can create a mathematical cusp in a single-gear
    // cubic. It may advance only while every step is physically bounded; once
    // no candidate survives twelve reductions it must hold the exact pose so
    // the runtime can discard and replan the curve.
    const auto reversed = simcore_host::make_npc_return_curve_plan(
        {0.0, -0.10}, std::numbers::pi - 1e-6, 0.0);
    require(reversed.valid, "near-180 return fixture must create a finite curve");
    double progress = 0.0;
    simcore_host::CollisionVector2 position = reversed.p0;
    double body_heading = std::numbers::pi - 1e-6;
    double yaw_speed = 0.0;
    bool safely_held = false;
    for (int tick = 0; tick < 1000 && progress < 1.0 - 1e-12; ++tick) {
        const auto tangent = simcore_host::npc_return_curve_tangent(reversed, progress);
        const double requested = std::min(1.0, progress
            + maximum_chord_m / std::max(1e-9,
                std::hypot(tangent.east_m, tangent.north_m)));
        const auto step = simcore_host::choose_bounded_npc_return_step(reversed, {
            position, progress, requested, body_heading, yaw_speed, dt,
            maximum_chord_m, maximum_yaw_rate_rad_s,
            maximum_yaw_acceleration_rad_s2});
        if (!step.accepted) {
            require(near_value(step.progress, progress, 0.0)
                    && near_value(step.position.east_m, position.east_m, 0.0)
                    && near_value(step.position.north_m, position.north_m, 0.0)
                    && near_value(step.body_heading_rad, body_heading, 0.0),
                "an exhausted near-180 candidate search must hold without hidden progress");
            safely_held = true;
            break;
        }
        const double chord = std::hypot(step.position.east_m - position.east_m,
            step.position.north_m - position.north_m);
        const double yaw_step = std::abs(std::remainder(
            step.body_heading_rad - body_heading, 2.0 * std::numbers::pi));
        require(chord <= maximum_chord_m + 1e-9
                && yaw_step <= maximum_yaw_rate_rad_s * dt + 1e-9
                && std::abs(step.yaw_speed_rad_s - yaw_speed)
                    <= maximum_yaw_acceleration_rad_s2 * dt + 1e-9,
            "near-180 return may never bypass chord, yaw-rate, or yaw-acceleration bounds");
        progress = step.progress;
        position = step.position;
        body_heading = step.body_heading_rad;
        yaw_speed = step.yaw_speed_rad_s;
    }
    require(safely_held || progress >= 1.0 - 1e-12,
        "near-180 return must either finish within every bound or request a safe replan");

    const auto forced_rejection = simcore_host::choose_bounded_npc_return_step(
        simcore_host::make_npc_return_curve_plan({0.0, -0.10}, 0.0, 0.0),
        {{0.0, -0.10}, 0.0, 1.0, 0.0, 0.0, dt, 0.0,
            maximum_yaw_rate_rad_s, maximum_yaw_acceleration_rad_s2});
    require(!forced_rejection.accepted
            && near_value(forced_rejection.progress, 0.0, 0.0)
            && near_value(forced_rejection.position.north_m, -0.10, 0.0),
        "twelve rejected chord candidates must return the unchanged pose for replanning");
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

NpcNavigationSnapshot npc_navigation(const SimulationHost& host)
{
    const auto snapshots = host.npc_navigation();
    const auto found = std::find_if(snapshots.begin(), snapshots.end(),
        [](const auto& snapshot) { return snapshot.entity_id == npc_id; });
    require(found != snapshots.end(), "managed NPC must expose its local navigation diagnostics");
    return *found;
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

void test_uncontacted_polyline_turn_does_not_create_impact_yaw(const CityFixture& city)
{
    auto config = city_config(city);
    config.physics_frequency_hz = 60.0;
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>();
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:123456789abcdef0";
    constexpr double target_heading = 240.0;
    const double target_radians = target_heading * std::numbers::pi / 180.0;
    network->lanes = {{10,3.0,6.0,0,true,
        {{100,100,0},{100.1,100,0},
         {100.1+20.0*std::sin(target_radians),100+20.0*std::cos(target_radians),0}}, {}}};
    config.traffic_network = network;
    config.npc_route = {10};
    config.npc_route_loop = false;
    config.npc_start_offset_m = 0.0;
    auto ground = config.ground_query;
    Harness harness(std::move(config));
    Client client{harness,"npc-authored-yaw",1};
    client.open("npc-authored-yaw-play");
    client.control();
    simcore_host::NpcLaneFollower reference;
    reference.rebuild(*network,*ground,{10},false,0.0);
    double previous_heading = 90.0;
    double previous_error = 150.0;
    bool passed_corner = false;
    for (int tick = 0; tick < 90; ++tick) {
        if (tick % 30 == 0) client.control();
        const auto& expected = reference.step(1.0/60.0,{});
        harness.tick();
        const auto actual = npc(harness.snapshot());
        require(actual.collision_event_sequence() == 0 && actual.damage_percent() == 0
                && actual.runtime_recovery_phase() == 0,
            "an isolated authored turn must never manufacture a crash or recovery state");
        require(near_value(actual.position_enu().x(),expected.position_enu.east_m,1e-8)
                && near_value(actual.position_enu().y(),expected.position_enu.north_m,1e-8),
            "bounded authored yaw must not truncate unrelated translation or double-integrate the NPC");
        const double delta = std::remainder(actual.heading()-previous_heading,360.0);
        require(std::abs(delta) <= 12.0001,
            "an authored tangent discontinuity must fit the finite-body angular budget before solving");
        if (expected.lane_offset_m > 0.1) {
            const double error = std::abs(std::remainder(actual.heading()-target_heading,360.0));
            require(error <= previous_error + 1e-4,
                "a contact-free yaw clamp must converge without feeding fake angular momentum back into the NPC");
            previous_error = error;
            passed_corner = true;
        }
        previous_heading = actual.heading();
    }
    if (!passed_corner || previous_error >= 1e-4) {
        throw std::runtime_error(
            "the healthy collision body must settle onto its actual route tangent after a sharp authored corner; error_deg="
                + std::to_string(previous_error));
    }
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

void test_ego_impact_holds_route_and_recovers_without_snapback(
    const CityFixture& city)
{
    auto config = city_config(city);
    constexpr double spawn_east_m = 4.36;
    constexpr double npc_route_speed_mps = 0.01;
    // Match the production cadence: the Windows timer can quantize 1ms waits
    // to ~16ms, unnecessarily turning a multi-second recovery into a long test.
    constexpr int frequency_hz = 60;
    constexpr double impact_tick_seconds = 1.0 / frequency_hz;
    config.physics_frequency_hz = frequency_hz;
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
    for (int index = 0; index < frequency_hz * 3; ++index) {
        if (index != 0 && index % frequency_hz == 0) client.control(1.0f, 0.0f);
        harness.tick();
        const auto state = npc(harness.snapshot());
        const double nominal_upper_bound = spawn_east_m
            + npc_route_speed_mps * (index + 1) * impact_tick_seconds;
        if (state.position_enu().x() > nominal_upper_bound + 0.02
            && state.speed() > npc_route_speed_mps + 0.01
            && npc_navigation(harness.host).collision_state != "driving") {
            displaced = true;
            displaced_east = state.position_enu().x();
            impact_tick = index;
            break;
        }
    }
    require(displaced,
            "real Ego throttle must transfer a meaningful impulse and displace the lane NPC");

    client.control(0.0f, 1.0f);
    harness.tick();
    const auto next = npc(harness.snapshot());
    const double nominal_next_upper_bound = spawn_east_m
        + npc_route_speed_mps * (impact_tick + 2) * impact_tick_seconds;
    require(next.position_enu().x() > nominal_next_upper_bound + 0.01
                && next.position_enu().x() >= displaced_east - 0.031,
            "collision displacement must survive the next follower pending commit without snap-back");

    const auto paused_route = npc_navigation(harness.host);
    require(paused_route.collision_state == "settling"
                && paused_route.collision_damage_percent >= 0.0
                && paused_route.collision_damage_percent < 100.0,
            "a light real impact must enter recoverable settling, not silently resume or disable");
    const double route_anchor_east = spawn_east_m + paused_route.route_distance_travelled_m;

    // Move the Ego out of the recovery corridor using actual public reverse
    // pedals. Keeping it parked against the NPC would correctly obstruct that
    // NPC's return and turn this into a permanent-obstacle test instead.
    bool reversing = true;
    client.control(1.0f, 0.0f, simcore::VEHICLE_GEAR_REVERSE);
    auto previous_navigation = paused_route;
    auto previous_entity = next;
    std::uint32_t holding_ticks = 0;
    bool saw_recovering = false;
    bool saw_longitudinal_recovery = false;
    bool recovered = false;
    for (int index = 0; index < frequency_hz * 9; ++index) {
        if (reversing && harness.host.state().position_enu.x < -0.10) {
            reversing = false;
            client.control();
        } else if (index != 0 && index % frequency_hz == 0) {
            if (reversing) client.control(1.0f, 0.0f, simcore::VEHICLE_GEAR_REVERSE);
            else client.control();
        }
        harness.tick();
        const auto navigation = npc_navigation(harness.host);
        const auto current = npc(harness.snapshot());
        require(navigation.collision_state != "disabled",
                "the low-speed light-impact fixture must remain recoverable");
        if (navigation.collision_state != "driving") {
            require(navigation.lane_id == paused_route.lane_id
                        && near_value(navigation.lane_offset_m, paused_route.lane_offset_m, 1e-9)
                        && near_value(navigation.route_distance_travelled_m,
                                      paused_route.route_distance_travelled_m, 1e-9),
                    "settling, holding and recovery must not advance a hidden nominal route");
        }
        if (navigation.collision_state == "holding") {
            ++holding_ticks;
            require(navigation.collision_hold_remaining_s >= 0.0
                        && current.position_enu().x() > route_anchor_east + 0.01,
                    "post-impact hold must preserve the displaced position, not slide to the route");
            if (previous_navigation.collision_state == "holding") {
                require(near_value(current.position_enu().x(), previous_entity.position_enu().x(), 1e-6)
                            && near_value(current.position_enu().y(), previous_entity.position_enu().y(), 1e-6)
                            && near_value(current.heading(), previous_entity.heading(), 1e-6)
                            && near_value(current.speed(), 0.0, 1e-6),
                        "a settled NPC must actually stay still throughout its collision hold");
            }
        }
        if (navigation.collision_state == "recovering") {
            saw_recovering = true;
            require(holding_ticks * impact_tick_seconds >= 1.5,
                    "recovery must follow a visible multi-second hold, not a few fixed ticks");
            if (previous_navigation.collision_state == "recovering") {
                const double displacement = std::hypot(
                    current.position_enu().x() - previous_entity.position_enu().x(),
                    current.position_enu().y() - previous_entity.position_enu().y());
                require(displacement <= 0.95 * impact_tick_seconds + 1e-6,
                        "recoverable NPC must return gradually with bounded motion, never snap back");
                if (displacement > 1e-6) {
                    const double heading = current.heading() * std::numbers::pi / 180.0;
                    const double east = current.position_enu().x() - previous_entity.position_enu().x();
                    const double north = current.position_enu().y() - previous_entity.position_enu().y();
                    const double lateral = east * std::cos(heading) - north * std::sin(heading);
                    require(std::abs(lateral) <= displacement * 0.08 + 1e-5,
                        "post-crash route recovery must move along the car body, never strafe like a UFO");
                    saw_longitudinal_recovery = true;
                }
            }
        }
        if (navigation.collision_state == "driving") {
            require(!reversing && saw_recovering && holding_ticks * impact_tick_seconds >= 1.5,
                    "NPC may resume driving only after clearing contact, holding and gradual recovery");
            require(std::abs(current.position_enu().x() - route_anchor_east) < 0.01
                        && std::abs(current.position_enu().y()) < 0.01,
                    "finished recovery must rejoin the paused route anchor, not an advanced route position");
            recovered = true;
            break;
        }
        previous_navigation = navigation;
        previous_entity = current;
    }
    if (!recovered || !saw_longitudinal_recovery) {
        throw std::runtime_error(
            "a light impact must visibly drive back and resume within the bounded run; state="
            + npc_navigation(harness.host).collision_state
            + " east=" + std::to_string(previous_entity.position_enu().x())
            + " north=" + std::to_string(previous_entity.position_enu().y())
            + " heading=" + std::to_string(previous_entity.heading()));
    }
    for (int index = 0; index < 20; ++index) { harness.tick(); }
    require(npc_navigation(harness.host).route_distance_travelled_m
                > paused_route.route_distance_travelled_m,
            "successful recovery must allow ordinary route following to resume");

    Client reset_client{harness, "finite-impact-b", 2};
    require(reset_client.open("finite-impact-pie-b")
                == ClientMessageResult::SimulationReset,
            "new PIE must reset an impacted NPC lifecycle");
    const auto reset = npc(harness.snapshot());
    const auto reset_navigation = npc_navigation(harness.host);
    require(near_value(reset.position_enu().x(), spawn_east_m, 1e-6)
                && near_value(reset.speed(), 0.0)
                && reset_navigation.collision_state == "driving"
                && near_value(reset_navigation.collision_damage_percent, 0.0)
                && near_value(reset_navigation.collision_hold_remaining_s, 0.0)
                && near_value(reset_navigation.route_distance_travelled_m, 0.0),
            "new PIE must clear finite collision offset, velocity, damage and recovery state exactly");
}

void test_lateral_impact_recovery_follows_one_driven_curve(const CityFixture& city)
{
    auto config = city_config(city);
    constexpr double lane_east_m = 4.36;
    constexpr int frequency_hz = 60;
    constexpr double tick_seconds = 1.0 / frequency_hz;
    config.physics_frequency_hz = frequency_hz;
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>();
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:623456789abcdef0";
    // The north-facing NPC exposes its side to the east-bound Ego.
    network->lanes = {
        {10,3.2,0.01,0,false,{{lane_east_m,-20,0},{lane_east_m,20,0}},{20}},
        {20,3.2,0.01,0,false,{{lane_east_m,20,0},{lane_east_m,-20,0}},{10}},
    };
    config.traffic_network = network;
    config.npc_route = {10,20};
    config.npc_route_loop = true;
    config.npc_start_offset_m = 20.0;
    config.npc_max_speed_mps = 0.01;
    config.npc_autonomous = false;
    Harness harness(std::move(config));
    const auto spawned = npc(harness.snapshot());
    require(near_value(spawned.position_enu().x(), lane_east_m, 1e-6)
                && near_value(spawned.position_enu().y(), 0.0, 1e-6)
                && near_value(spawned.heading(), 0.0, 1e-6),
            "lateral recovery fixture must expose the north-facing NPC side to Ego");

    Client client{harness, "lateral-recovery", 1};
    client.open("lateral-recovery-play");
    client.control(0.45f, 0.0f);
    bool impacted = false;
    double paused_route_distance_m = 0.0;
    for (int tick = 0; tick < frequency_hz * 3; ++tick) {
        if (tick != 0 && tick % frequency_hz == 0) client.control(0.45f, 0.0f);
        harness.tick();
        const auto current = npc(harness.snapshot());
        if (npc_navigation(harness.host).collision_state != "driving"
            && current.position_enu().x() > lane_east_m + 0.02) {
            impacted = true;
            paused_route_distance_m = npc_navigation(harness.host).route_distance_travelled_m;
            break;
        }
    }
    require(impacted, "an actual side contact must create a recoverable lateral route offset");

    client.control(1.0f, 0.0f, simcore::VEHICLE_GEAR_REVERSE);
    bool reversing = true;
    bool saw_recovering = false;
    bool recovered = false;
    int recovering_ticks = 0;
    int first_recovery_motion_tick = 0;
    int observed_gear_direction = 0;
    double recovery_start_east = 0.0;
    double recovery_start_north = 0.0;
    double recovery_start_heading = 0.0;
    auto previous = npc(harness.snapshot());
    auto previous_navigation = npc_navigation(harness.host);
    for (int tick = 0; tick < frequency_hz * 30; ++tick) {
        if (reversing && harness.host.state().position_enu.x < -0.10) {
            reversing = false;
            client.control();
        } else if (tick != 0 && tick % frequency_hz == 0) {
            if (reversing) client.control(1.0f, 0.0f, simcore::VEHICLE_GEAR_REVERSE);
            else client.control();
        }
        harness.tick();
        const auto navigation = npc_navigation(harness.host);
        const auto current = npc(harness.snapshot());
        require(navigation.collision_state != "disabled",
            "moderate lateral fixture must remain recoverable");
        if (navigation.collision_state == "recovering") {
            ++recovering_ticks;
            if (!saw_recovering) {
                recovery_start_east = current.position_enu().x();
                recovery_start_north = current.position_enu().y();
                recovery_start_heading = current.heading();
            }
            saw_recovering = true;
            const double from_recovery_start = std::hypot(
                current.position_enu().x() - recovery_start_east,
                current.position_enu().y() - recovery_start_north);
            const double heading_from_recovery_start = std::abs(std::remainder(
                (current.heading() - recovery_start_heading)
                    * std::numbers::pi / 180.0,
                2.0 * std::numbers::pi));
            if (recovering_ticks <= 120) {
				require(from_recovery_start <= 1e-9
						&& heading_from_recovery_start <= 1e-9,
					"driver boarding grace must hold the crashed vehicle pose for at least 2.0s");
            }
            if (first_recovery_motion_tick == 0
                && (from_recovery_start > 1e-8
                    || heading_from_recovery_start > 1e-8)) {
                first_recovery_motion_tick = recovering_ticks;
				require(first_recovery_motion_tick >= 121,
                    "route recovery must not depart before the 2.0s driver boarding grace");
            }
            if (previous_navigation.collision_state == "recovering") {
                const double east = current.position_enu().x() - previous.position_enu().x();
                const double north = current.position_enu().y() - previous.position_enu().y();
                const double displacement = std::hypot(east, north);
                const double heading = current.heading() * std::numbers::pi / 180.0;
                const double longitudinal = east * std::sin(heading) + north * std::cos(heading);
                const double lateral = east * std::cos(heading) - north * std::sin(heading);
                if (displacement > 0.95 * tick_seconds + 1e-5
                    || std::abs(lateral) > displacement * 0.12 + 1e-5) {
                    throw std::runtime_error(
                        "lateral recovery must move along the car body at bounded speed, never strafe; displacement="
                        + std::to_string(displacement) + " lateral=" + std::to_string(lateral)
                        + " heading=" + std::to_string(current.heading()));
                }
                if (std::abs(longitudinal) > 1e-5) {
                    const int direction = longitudinal > 0.0 ? 1 : -1;
                    if (observed_gear_direction == 0) observed_gear_direction = direction;
                    require(direction == observed_gear_direction,
                        "one recovery manoeuvre cannot flip repeatedly between drive and reverse");
                }
                const double yaw_step = std::abs(std::remainder(
                    (current.heading() - previous.heading()) * std::numbers::pi / 180.0,
                    2.0 * std::numbers::pi));
                require(yaw_step <= 22.0 * std::numbers::pi / 180.0 * tick_seconds + 1e-5,
                    "recovery curve must respect the low-speed yaw-rate limit");
            }
        }
        if (navigation.collision_state == "driving" && saw_recovering) {
            if (previous_navigation.collision_state == "recovering") {
                const double east = current.position_enu().x() - previous.position_enu().x();
                const double north = current.position_enu().y() - previous.position_enu().y();
                const double displacement = std::hypot(east, north);
                const double heading = current.heading() * std::numbers::pi / 180.0;
                const double lateral = east * std::cos(heading) - north * std::sin(heading);
                const double yaw_step = std::abs(std::remainder(
                    (current.heading() - previous.heading()) * std::numbers::pi / 180.0,
                    2.0 * std::numbers::pi));
                require(displacement <= 0.95 * tick_seconds + 1e-5
                            && std::abs(lateral) <= displacement * 0.12 + 1e-5
                            && yaw_step <= 22.0 * std::numbers::pi / 180.0 * tick_seconds + 1e-5,
                    "the recovering-to-driving transition must finish on the same bounded curve, never snap");
            }
            require(std::abs(current.position_enu().x() - lane_east_m) < 1e-6
                        && std::abs(current.position_enu().y() - paused_route_distance_m) < 1e-6
                        && std::abs(current.heading()) < 1e-5,
                "lateral recovery must finish exactly at the paused lane pose without a final snap");
            recovered = true;
            break;
        }
        previous = current;
        previous_navigation = navigation;
    }
    if (!saw_recovering || first_recovery_motion_tick == 0
        || observed_gear_direction == 0 || !recovered) {
        throw std::runtime_error(
            "lateral collision must visibly complete one body-aligned recovery curve; state="
            + npc_navigation(harness.host).collision_state
            + " east=" + std::to_string(previous.position_enu().x())
            + " north=" + std::to_string(previous.position_enu().y())
            + " heading=" + std::to_string(previous.heading())
            + " direction=" + std::to_string(observed_gear_direction)
            + " start_east=" + std::to_string(recovery_start_east)
            + " start_north=" + std::to_string(recovery_start_north)
            + " start_heading=" + std::to_string(recovery_start_heading));
    }
}

void test_compound_subthreshold_contacts_create_an_npc_dent(const CityFixture& city)
{
    auto config = city_config(city);
    config.physics_frequency_hz = 60;
    config.npc_start_offset_m = 116.0; // NPC at E=40, remote from stationary Ego.
    config.npc_max_speed_mps = 0.01;
    for (const double north : {-0.5,0.5}) {
        const auto id = static_cast<std::uint32_t>(3001+config.runtime_entities.size());
        config.runtime_entities.push_back({id,simcore_host::RuntimeEntityKind::Pedestrian,
            {"compound-contact-"+std::to_string(id),
             simcore_host::VerticalCapsule{{37.1,north},0.9,0.35,0.9},
             {30.0,0.0},0.0,{0.8,0.08},80.0,0.0,30.0}});
    }
    Harness harness(std::move(config));
    Client client{harness,"compound-npc-dent",1};
    client.open("compound-npc-dent-play");
    client.control();
    // Verify that this real solver fixture consists only of subthreshold
    // pieces, rather than accidentally passing with one large contact.
    std::vector<simcore_host::KinematicCollisionProxy> proxies;
    for (const auto& entity : harness.host.runtime_entities()) proxies.push_back(entity.collision_proxy);
    const auto probe = simcore_host::CollisionWorld{}.integrate(
        {"probe-ego",{{0,0},0.85,0,2.2,1.0,0.75},{},0,1500,2600},1.0/60.0,std::move(proxies));
    double total = 0.0, largest = 0.0;
    for (const auto& contact : probe.runtime_proxy_contacts) {
        if (contact.proxy_id != "lane-npc-1001") continue;
        total += contact.contact.accumulated_normal_impulse_n_s;
        largest = std::max(largest,contact.contact.accumulated_normal_impulse_n_s);
    }
    require(total > 2500.0 && largest < 2500.0,
        "compound dent regression must exceed the threshold only after contact accumulation");
    harness.tick();
    const auto impacted = npc(harness.snapshot());
    require(impacted.last_impact_impulse_n_s() > 2500.0f && impacted.dent_patches_size() > 0,
        "actual host must publish a dent from the episode impulse even when every component contact is below threshold");
    require(harness.host.state().damage_percent == 0,
        "remote compound NPC contact must not create Ego damage");
}

void test_severe_ego_impact_disables_npc_until_new_play(const CityFixture& city)
{
    auto config = city_config(city);
    constexpr double spawn_east_m = 40.0;
    constexpr int frequency_hz = 60;
    config.physics_frequency_hz = frequency_hz;
    config.npc_start_offset_m = 76.0 + spawn_east_m;
    config.npc_max_speed_mps = 0.01;
    Harness harness(std::move(config));
    require(near_value(npc(harness.snapshot()).position_enu().x(), spawn_east_m, 1e-6),
            "severe-impact fixture needs an unobstructed real-road acceleration run");
    Client client{harness, "severe-impact-a", 1};
    require(client.open("severe-impact-pie-a") == ClientMessageResult::SimulationReset,
            "severe-impact test must enter the actual reset/control lifecycle");
    client.control(1.0f, 0.0f);
    bool disabled = false;
    for (int index = 0; index < frequency_hz * 8; ++index) {
        if (index != 0 && index % 30 == 0) client.control(1.0f, 0.0f);
        harness.tick();
        if (npc_navigation(harness.host).collision_state == "disabled") {
            disabled = true;
            break;
        }
    }
    require(disabled,
            "actual full-throttle Ego impact after a 40m run must disable the NPC authority");
    const auto disabled_route = npc_navigation(harness.host);
    require(disabled_route.collision_damage_percent > 0.0,
            "severe impact must expose collision damage, not only a navigation stop");
    const auto first_crash = npc(harness.snapshot());
    require(first_crash.last_impact_impulse_n_s() > 2500.0f && first_crash.dent_patches_size() > 0,
        "a real severe NPC impact must carry localized dent geometry in the same WorldState");
    require(first_crash.collision_event_sequence() > 0 && first_crash.last_impact_impulse_n_s() > 0.0f
                && first_crash.damage_percent() > 0.0f
                && static_cast<int>(first_crash.damage_zone()) != 0
                && first_crash.impact_direction_enu().x() > 0.9,
        "the solved rear collision must publish persistent damage, zone, event and NPC impulse direction");

    // The struck body may continue coasting or be pushed by the braking Ego;
    // Disabled forbids self-propulsion/route recovery, not external physics.
    // This straight rear-impact fixture has no wall ahead, so residual physical
    // motion is forward. A backwards pull would be the unwanted route spring.
    client.control();
    auto previous = npc(harness.snapshot());
    double maximum_tilt_degrees = 0.0;
    for (int index = 0; index < frequency_hz * 2; ++index) {
        if (index != 0 && index % 30 == 0) client.control();
        harness.tick();
        const auto navigation = npc_navigation(harness.host);
        const auto current = npc(harness.snapshot());
        maximum_tilt_degrees = std::max({maximum_tilt_degrees,
            std::abs(static_cast<double>(current.pitch())), std::abs(static_cast<double>(current.roll()))});
        const auto support = simcore_host::impact_tumble_support({},
            current.pitch() * std::numbers::pi / 180.0, current.roll() * std::numbers::pi / 180.0);
        const auto ground = city.package.ground_query->query_down(
            {{current.position_enu().x(), current.position_enu().y(), 4.0}, 8.0});
        require(ground && current.position_enu().z() - support.support_height_m
                    >= ground->point_enu.up_m - 1e-5
                && near_value(current.collision_half_height(), support.projected_half_height_m, 1e-5)
                && near_value(current.collision_half_width(), support.projected_half_width_m, 1e-5),
            "actual impact tilt must preserve ground support and identical wire/collision envelope");
        require(navigation.collision_state == "disabled"
                    && navigation.lane_id == disabled_route.lane_id
                    && near_value(navigation.lane_offset_m, disabled_route.lane_offset_m, 1e-9)
                    && near_value(navigation.route_distance_travelled_m,
                                  disabled_route.route_distance_travelled_m, 1e-9),
                "severely damaged NPC must retain its disable latch and frozen route progress");
        require(current.position_enu().x() >= previous.position_enu().x() - 1e-5,
                "disabled NPC must not pull backwards toward the old route anchor");
        previous = current;
    }
    require(previous.position_enu().x()
                > spawn_east_m + disabled_route.route_distance_travelled_m + 0.25,
            "severe collision must leave the NPC visibly displaced from its route anchor");
    require(maximum_tilt_degrees > 0.05,
            "actual solved contact impulse must reach server-authoritative pitch/roll, not just XY offset");

    // The first impact/tilt path alone does not prove that a settled wreck is
    // still a finite body. Back away without resetting this PIE, let it settle,
    // then drive into that same disabled body for a second independent impact.
    const double reimpact_approach_east = previous.position_enu().x() - 8.0;
    client.control(1.0f, 0.0f, simcore::VEHICLE_GEAR_REVERSE);
    bool cleared_wreck = false;
    for (int index = 0; index < frequency_hz * 6; ++index) {
        if (index != 0 && index % 30 == 0)
            client.control(1.0f, 0.0f, simcore::VEHICLE_GEAR_REVERSE);
        harness.tick();
        if (harness.host.state().position_enu.x <= reimpact_approach_east) {
            cleared_wreck = true;
            break;
        }
    }
    require(cleared_wreck, "public reverse control must clear the wreck before the second impact");
    client.control();
    for (int index = 0; index < frequency_hz; ++index) {
        if (index != 0 && index % 30 == 0) client.control();
        harness.tick();
    }
    const auto settled_wreck = npc(harness.snapshot());
    require(npc_navigation(harness.host).collision_state == "disabled"
                && std::abs(settled_wreck.speed()) < 0.05,
        "second-impact fixture must begin with a stationary disabled body, not residual coasting");
    double maximum_reimpact_speed = 0.0;
    double reimpact_displacement = 0.0;
    auto previous_reimpact = settled_wreck;
    int upright_coasting_samples = 0;
    client.control(0.35f, 0.0f);
    for (int index = 0; index < frequency_hz * 8; ++index) {
        if (index != 0 && index % 30 == 0) client.control(0.35f, 0.0f);
        harness.tick();
        const auto navigation = npc_navigation(harness.host);
        const auto current = npc(harness.snapshot());
        const auto& proxy = harness.host.runtime_entities().front().collision_proxy;
        require(navigation.collision_state == "disabled"
                    && near_value(navigation.route_distance_travelled_m,
                                  disabled_route.route_distance_travelled_m, 1e-9)
                    && near_value(proxy.mass_kg, 1500.0),
            "a second collision must preserve the drive-disable latch and finite collision mass");
        if (previous_reimpact.linear_velocity_enu().x() > 0.1
            && std::abs(previous_reimpact.pitch()) < 1e-5 && std::abs(current.pitch()) < 1e-5
            && std::abs(previous_reimpact.roll()) < 1e-5 && std::abs(current.roll()) < 1e-5) {
            ++upright_coasting_samples;
            // Clear straight road and a pushing (not opposing) Ego: planar
            // momentum may decay by drag, but cannot disappear because the
            // NPC's now-upright route sweep sees the Ego at its rear bumper.
            require(current.position_enu().x() - previous_reimpact.position_enu().x()
                        >= previous_reimpact.linear_velocity_enu().x() / frequency_hz * 0.6,
                "settled wreck must retain external momentum instead of zeroing it against Ego's route blocker");
        }
        maximum_reimpact_speed = std::max(maximum_reimpact_speed,
            std::abs(static_cast<double>(current.speed())));
        reimpact_displacement = std::hypot(
            current.position_enu().x() - settled_wreck.position_enu().x(),
            current.position_enu().y() - settled_wreck.position_enu().y());
        previous_reimpact = current;
        if (reimpact_displacement > 0.35 && maximum_reimpact_speed > 0.25
            && upright_coasting_samples >= 5) break;
    }
    require(reimpact_displacement > 0.35 && maximum_reimpact_speed > 0.25,
        "a fully settled disabled NPC must move again under a second Ego impact, without a PIE reset");
    require(upright_coasting_samples >= 5,
        "the repeated collision must exercise the settled-upright path, not only the existing active-tumble bypass");
    require(npc(harness.snapshot()).collision_event_sequence() > first_crash.collision_event_sequence(),
        "a new hit on an already disabled car must reach presentation as a new collision episode");

    Client reset_client{harness, "severe-impact-b", 2};
    require(reset_client.open("severe-impact-pie-b") == ClientMessageResult::SimulationReset,
            "new PIE must reset the severely damaged NPC lifecycle");
    const auto reset_navigation = npc_navigation(harness.host);
    const auto reset = npc(harness.snapshot());
    require(reset_navigation.collision_state == "driving"
                && near_value(reset_navigation.collision_damage_percent, 0.0)
                && near_value(reset_navigation.collision_hold_remaining_s, 0.0)
                && near_value(reset_navigation.route_distance_travelled_m, 0.0)
                && reset.collision_event_sequence() == 0 && near_value(reset.damage_percent(), 0.0)
                && reset.dent_patches_size() == 0
                && near_value(reset.position_enu().x(), spawn_east_m, 1e-6)
                && near_value(reset.pitch(), 0.0) && near_value(reset.roll(), 0.0)
                && near_value(reset.speed(), 0.0),
            "new Play alone must clear the severe disable latch and restore the clean spawn");
}

void test_structure_damage_from_actual_ego_collision(const CityFixture& city)
{
    for (const bool signal_pole : {true, false}) {
        auto config = city_config(city);
        config.physics_frequency_hz = 60;
        config.npc_route.clear();
        const auto ground = city.package.ground_query->query_down({{8.0, 0.0, 2.0}, 4.0});
        require(ground.has_value(), "structure fixture requires actual supported road");
        if (signal_pole) {
            auto network = std::make_shared<simcore_host::TrafficNetwork>();
            network->source_map_checksum = config.map_package_checksum;
            network->checksum = "fnv1a64:456789abcdef0123";
            network->signals.push_back({901, 1, ground->point_enu, 90.0, 1,
                simcore_host::TrafficSignalKind::Vehicle});
            config.traffic_network = network;
        } else {
            config.traffic_network.reset();
            config.collision_world = std::make_shared<simcore_host::CollisionWorld>(
                std::vector<simcore_host::StaticObbCollider>{{"Building_Test",
                    simcore_host::StaticColliderSemantic::Wall,
                    {{8.0, 0.0}, ground->point_enu.up_m + 3.0, 0.0, 4.0, 0.3, 3.0}, {0.8, 0.0}}});
        }
        Harness harness(std::move(config));
        Client client{harness, "structure-impact-a", 1};
        require(client.open("structure-impact-pie-a") == ClientMessageResult::SimulationReset,
            "structure impact must use normal Hello/reset/control lifecycle");
        client.control(1.0f, 0.0f);
        bool damaged = false;
        for (int tick = 0; tick < 360; ++tick) {
            if (tick % 30 == 0) client.control(1.0f, 0.0f);
            harness.tick();
            const auto frame = harness.snapshot();
            if (frame.world_state().structures_size() == 1
                && frame.world_state().structures(0).damage_percent() > 0.0
                && (!signal_pole || frame.world_state().structures(0).disabled())) {
                damaged = true;
                break;
            }
        }
        require(damaged, "real Ego collision must damage a wall or disable a signal pole");
        client.control();
        for (int tick = 0; tick < 120; ++tick) {
            if (tick % 30 == 0) client.control();
            harness.tick();
        }
        const auto frame = harness.snapshot();
        const auto& damage = frame.world_state().structures(0);
        require(damage.damage_percent() > 0.0 && damage.event_sequence() > 0,
            "damage must persist in authoritative snapshots after the impact");
        if (signal_pole) {
            require(damage.signal_id() == 901 && damage.disabled() && damage.fall_angle_rad() > 1.0,
                "a broken signal must actually fall instead of merely acquiring damage text");
            require(frame.world_state().traffic_signals(0).out_of_service()
                    && frame.world_state().traffic_signals(0).aspect() == simcore::TRAFFIC_SIGNAL_RED,
                "signal outage and safe red routing permission must be published together");
        } else {
            require(damage.collider_id() == "Building_Test" && !damage.disabled(),
                "building surface damage must not erase the structural collision shell");
            require(harness.host.state().position_enu.x < 8.0,
                "damaged building must remain an actual blocking collision body");
        }
        Client reconnect{harness, "structure-impact-reconnect", 2};
        require(reconnect.open("structure-impact-pie-a") != ClientMessageResult::SimulationReset
                && harness.snapshot().world_state().structures_size() == 1,
            "same-PIE reconnect must preserve the damaged world");
        Client fresh{harness, "structure-impact-new", 3};
        require(fresh.open("structure-impact-pie-b") == ClientMessageResult::SimulationReset
                && harness.snapshot().world_state().structures_size() == 0,
            "new PIE must restore all structures and clear stale impact events");
        if (signal_pole) require(!harness.snapshot().world_state().traffic_signals(0).out_of_service(),
            "new PIE must restore signal operation");
    }
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

void test_slow_npc_pedestrian_contact_retains_standing_body(const CityFixture& city)
{
    auto config = city_config(city);
    config.physics_frequency_hz = 60;
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>();
    config.traffic_network.reset();
    config.npc_route.clear();
    constexpr std::uint32_t npc_id=9101, pedestrian_id=9201;
    // Use direct finite bodies instead of a route-following pedestrian. The old
    // fixture combined collision with pedestrian signal-entry policy and could
    // correctly decide not to start walking before ever reaching the NPC.
    // Here a 0.2m/s closing speed produces an actual mild pair impulse now.
    config.runtime_entities = {
        {npc_id,simcore_host::RuntimeEntityKind::NpcVehicle,{"weak-pair-npc",
            simcore_host::ObbPrism{{110,0},.85,std::numbers::pi/2,2.2,1.0,.75},
            {.2,0},0,{.8,.05},1500,2850,8}},
        {pedestrian_id,simcore_host::RuntimeEntityKind::Pedestrian,{"weak-pair-ped",
            simcore_host::VerticalCapsule{{112.45,0},.9,.35,.9},
            {},0,{.75,0},80,0,60}}
    };
    Harness harness(std::move(config));
    Client client{harness,"npc-ped-body",1};
    client.open("npc-ped-body-play");
    client.control(); // Ego stays at the origin, over 100m away from this collision.
    std::uint32_t hit_id = 0;
    for (int index=0; index<120; ++index) {
        if (index % 30 == 0) client.control();
        harness.tick();
        const auto frame = harness.snapshot();
        for (const auto& entity : frame.world_state().entities()) {
            if (entity.entity_id()!=pedestrian_id
                || entity.position_enu().x() <= 112.45+1e-4) continue;
            require(!entity.pedestrian_downed() && !entity.pedestrian_airborne()
                && entity.collision_radius() > .3 && near_value(entity.collision_half_height(), .9, 1e-5)
                && entity.speed() > 0.0 && entity.speed() < .4,
                "a crawling finite NPC must transfer bounded momentum without turning the pedestrian into a ragdoll");
            hit_id=entity.entity_id();
            break;
        }
        if (hit_id) break;
    }
    require(hit_id != 0, "direct finite pedestrian and NPC bodies must physically exchange momentum");
    require(harness.host.state().damage_percent == 0,
        "NPC/ped pair impulse must never be attributed to remote Ego damage");
    for (int index=0; index<60; ++index) {
        if (index % 30 == 0) client.control();
        harness.tick();
        const auto entity=entity_by_id(harness.snapshot(),hit_id);
        require(!entity.pedestrian_downed() && !entity.pedestrian_airborne()
            && entity.collision_radius() > .3 && near_value(entity.position_enu().z(), .9, 1e-5),
            "continued gentle contact must not become a delayed airborne launch or low-body collider");
    }
    Client reset{harness,"npc-ped-body-reset",2};
    reset.open("npc-ped-body-new-play");
    const auto standing=entity_by_id(harness.snapshot(),hit_id);
    require(!standing.pedestrian_downed() && !standing.pedestrian_airborne()
        && standing.collision_radius() > .3 && standing.collision_event_sequence() == 0,
        "new Play must restore the standing capsule and clear all body state");
}

void test_strong_ego_pedestrian_impact_publishes_immediate_low_body(const CityFixture& city)
{
    auto config = city_config(city);
    config.physics_frequency_hz = 60;
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>();
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->format_version = 2;
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:123456789abcdef0";
    // The first managed pedestrian waits at East=7.55, North=0 while Ego
    // accelerates eastward. No route-following NPC or WALK motion causes the hit.
    network->signals = {{101,1,{8,0,0},0,1,simcore_host::TrafficSignalKind::Pedestrian},
                        {102,1,{8,8,0},180,1,simcore_host::TrafficSignalKind::Pedestrian}};
    network->signal_plans = {{1,0,{1},{{10000,{},{}}},10000}};
    config.traffic_network = network;
    config.npc_route.clear();
    Harness harness(std::move(config));
    Client client{harness,"strong-ped-body",1};
    client.open("strong-ped-body-play");
    bool hit = false;
    for (int index=0; index<60*4; ++index) {
        if (index % 30 == 0) client.control(1.0f,0.0f);
        harness.tick();
        const auto entity = entity_by_id(harness.snapshot(),2001);
        if (entity.collision_event_sequence() == 0) continue;
        if (entity.last_impact_impulse_n_s() < simcore_host::PedestrianImpactState::knockdown_impulse_n_s) {
            require(!entity.pedestrian_downed(), "a small first solver impulse must not prematurely knock down the person");
            continue;
        }
        require(entity.pedestrian_downed() && entity.collision_radius() == 0
            && entity.collision_half_length() > 0 && entity.pitch() > 0,
            "the first sufficiently strong impact frame must already publish the tilted body instead of a capsule");
        hit = true;
        break;
    }
    require(hit, "real accelerated Ego contact must exceed the pedestrian balance threshold");
    client.control();
    bool landed = false;
    for (int index=0; index<60*4; ++index) {
        if (index % 30 == 0) client.control();
        harness.tick();
        const auto entity = entity_by_id(harness.snapshot(),2001);
        require(entity.position_enu().z() - entity.collision_half_height() >= -1e-5,
            "falling body collider must remain above authoritative ground");
        if (entity.pedestrian_downed() && !entity.pedestrian_airborne()
            && entity.collision_half_height() < .3) {
            require(entity.collision_radius() == 0 && entity.collision_half_length() > .9,
                "landing must retain a horizontal body collider, not an upright ghost");
            landed = true;
            break;
        }
    }
    require(landed, "strong contact retains signed rotation and a low landed body without unconditional launch");
    Client reset{harness,"strong-ped-body-reset",2};
    reset.open("strong-ped-body-new-play");
    const auto standing = entity_by_id(harness.snapshot(),2001);
    require(!standing.pedestrian_downed() && !standing.pedestrian_airborne()
        && standing.collision_radius() > .3 && standing.collision_event_sequence() == 0,
        "new Play clears strong impact trajectory and restores the standing capsule");
}

void test_fast_ego_pedestrian_impact_keeps_vehicle_momentum(const CityFixture& city)
{
    auto config = city_config(city);
    config.physics_frequency_hz = 60;
    config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
    config.collision_world = std::make_shared<const simcore_host::CollisionWorld>();
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->format_version = 2;
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:123456789abcdef0";
    network->signals = {{101,1,{80,0,0},0,1,simcore_host::TrafficSignalKind::Pedestrian},
                        {102,1,{80,8,0},180,1,simcore_host::TrafficSignalKind::Pedestrian}};
    network->signal_plans = {{1,0,{1},{{10000,{},{}}},10000}};
    config.traffic_network = network;
    config.npc_route.clear();
    Harness harness(std::move(config));
    Client client{harness,"fast-ped-body",1};
    client.open("fast-ped-body-play");
    double previous_speed = 0.0;
    double impact_speed = 0.0;
    double impact_east = 0.0;
    bool hit = false;
    for (int index=0; index<60*20; ++index) {
        if (index % 30 == 0) client.control(1.0f,0.0f);
        harness.tick();
        const auto frame = harness.snapshot();
        const auto ego = entity_by_id(frame,1);
        const auto pedestrian = entity_by_id(frame,2001);
        if (pedestrian.collision_event_sequence() != 0) {
            impact_speed = previous_speed;
            impact_east = ego.position_enu().x();
            require(impact_speed >= 15.0 && impact_speed <= 30.0,
                "fast pedestrian regression must actually reach 15-30m/s before impact");
            require(ego.speed() > impact_speed * .8,
                "an 80kg pedestrian must not stop a 1500kg car on first finite-mass impact");
            hit = true;
            break;
        }
        previous_speed = ego.speed();
    }
    require(hit, "fast real-host fixture must collide with the managed standing pedestrian");
    for (int index=0; index<60; ++index) {
        if (index % 30 == 0) client.control(1.0f,0.0f);
        harness.tick();
        const auto ego = entity_by_id(harness.snapshot(),1);
        require(ego.speed() > impact_speed * .65,
            "post-impact overlap and pedestrian reaction caps must not brake the continuing car to a stop");
    }
    require(entity_by_id(harness.snapshot(),1).position_enu().x() > impact_east + 10.0,
        "the vehicle must continue through the contact area rather than remain stuck to the pedestrian");
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
    const auto facing_matches_intent = [&](std::uint32_t id, auto from, auto to) {
        const auto pedestrian = entity_by_id(harness.snapshot(), id);
        const double heading = pedestrian.heading() * std::numbers::pi / 180.0;
        const double east = to.east_m - from.east_m;
        const double north = to.north_m - from.north_m;
        const double alignment=(east * std::sin(heading) + north * std::cos(heading))
            / std::hypot(east, north);
        return alignment > 0.999;
    };
    // Physical separation from the sidewalk/poles can perturb the first actual
    // displacement. Rendering orientation follows the authored crossing intent.
    require(facing_matches_intent(2001, vehicle_group_one[0].position_enu,
                                      vehicle_group_one[1].position_enu)
                && facing_matches_intent(2002, vehicle_group_one[1].position_enu,
                                             vehicle_group_one[0].position_enu),
        "both opposing capsules must publish walking-forward heading rather than a constant zero yaw");
}

void test_pedestrians_require_enough_walk_time_for_new_crossing(const CityFixture& city)
{
    const auto make_network = [&](std::uint32_t offset_ms) {
        auto network = std::make_shared<simcore_host::TrafficNetwork>();
        network->format_version = 2;
        network->source_map_checksum = city.package.collision_checksum;
        network->checksum = "fnv1a64:123456789abcdef0";
        network->signals = {{101,1,{100,100,0},0,1,simcore_host::TrafficSignalKind::Pedestrian},
                            {102,1,{100,103,0},180,1,simcore_host::TrafficSignalKind::Pedestrian}};
        // The 3m trip takes 2.22s. After one crossing, the remaining 0.78s
        // of WALK must not authorize an opposing trip into the next RED.
        network->signal_plans = {{1,offset_ms,{1},{{3000,{1},{}},{4000,{},{}}},7000}};
        return network;
    };
    const auto make_config = [&](const std::shared_ptr<simcore_host::TrafficNetwork>& network) {
        auto config = city_config(city);
        config.physics_frequency_hz = 60;
        config.npc_route.clear();
        config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
        config.collision_world = std::make_shared<const simcore_host::CollisionWorld>();
        config.traffic_network = network;
        return config;
    };
    {
        Harness late(make_config(make_network(2000)));
        Client client{late,"pedestrian-late-walk",1};
        client.open("pedestrian-late-walk-play");
        client.control();
        const auto before = late.snapshot();
        late.tick();
        const auto after = late.snapshot();
        require(after.world_state().traffic_signals(0).aspect() == simcore::TRAFFIC_SIGNAL_GREEN,
            "late-entry regression must exercise a currently GREEN pedestrian signal");
        for (std::uint32_t id : {2001u,2002u}) {
            require(near_value(entity_by_id(before,id).position_enu().y(),
                               entity_by_id(after,id).position_enu().y()),
                "remaining WALK shorter than a full trip must not start waiting pedestrians");
        }
    }
    for (const bool shorten_walk_after_departure : {false,true}) {
        auto network = make_network(0);
        Harness harness(make_config(network));
        Client client{harness,"pedestrian-clear-walk",1};
        client.open("pedestrian-clear-walk-play");
        client.control();
        harness.tick();
        require(entity_by_id(harness.snapshot(),2001).position_enu().y() > 100.0,
            "enough remaining WALK must authorize a new crossing");
        if (shorten_walk_after_departure) {
            // Advance the authored signal phase after departure to verify the
            // clearance rule independently of the initial entry-time check.
            network->signal_plans.front().offset_ms = 2500;
        }
        bool reached_other_side = false;
        for (int tick = 1; tick <= 186; ++tick) {
            if (tick % 30 == 0) client.control();
            harness.tick();
            const auto frame = harness.snapshot();
            const auto first = entity_by_id(frame,2001);
            const auto second = entity_by_id(frame,2002);
            const bool at_end = near_value(first.position_enu().y(),103.0,1e-6)
                && near_value(second.position_enu().y(),100.0,1e-6);
            if (reached_other_side) {
                require(at_end,
                    "arrival must wait on the sidewalk instead of restarting in the short remaining WALK");
            }
            reached_other_side = reached_other_side || at_end;
            if (shorten_walk_after_departure && at_end) {
                require(frame.world_state().traffic_signals(0).aspect() == simcore::TRAFFIC_SIGNAL_RED,
                    "an already-started crossing must finish safely even after its signal becomes RED");
                break;
            }
        }
        require(reached_other_side,
            "both pedestrians must complete an authorized crossing without stopping in the road");
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--return-curve") {
            test_short_and_degenerate_npc_return_curves_are_bounded();
            std::cout << "npc_host_test: bounded return curve checks passed\n";
            return 0;
        }
        const CityFixture city;
        if (argc == 2 && std::string_view(argv[1]) == "--pedestrian-walk") {
            test_pedestrian_pair_waits_for_and_moves_on_walk(city);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--pedestrian-timing") {
            test_pedestrians_require_enough_walk_time_for_new_crossing(city);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--pedestrian-impact") {
            test_fast_ego_pedestrian_impact_keeps_vehicle_momentum(city);
            test_strong_ego_pedestrian_impact_publishes_immediate_low_body(city);
            test_slow_npc_pedestrian_contact_retains_standing_body(city);
            std::cout << "npc_host_test: pedestrian impact checks passed\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--lateral-recovery") {
            test_lateral_impact_recovery_follows_one_driven_curve(city);
            std::cout << "npc_host_test: lateral recovery checks passed\n";
            return 0;
        }
        test_real_city_spawn_and_exactly_one_npc_advance(city);
        test_uncontacted_polyline_turn_does_not_create_impact_yaw(city);
        test_new_play_resets_but_same_play_reconnect_keeps_progress(city);
        test_soft_timeout_freezes_and_fresh_control_resumes(city);
        test_static_guard_and_map_traffic_reload_remove_then_recreate(city);
        test_ego_overlap_defers_spawn_and_actual_reverse_input_releases_it(city);
        test_reload_runtime_replacements_preserve_the_reserved_npc_slot(city);
        test_ground_recovery_materializes_at_route_anchor_without_teleport_velocity(city);
        test_four_lane_npcs_use_stable_ids_and_spaced_offsets(city);
        test_ego_impact_holds_route_and_recovers_without_snapback(city);
        test_compound_subthreshold_contacts_create_an_npc_dent(city);
        test_severe_ego_impact_disables_npc_until_new_play(city);
        test_structure_damage_from_actual_ego_collision(city);
        test_ego_impact_pushes_bounded_finite_pedestrian(city);
        test_slow_npc_pedestrian_contact_retains_standing_body(city);
        test_strong_ego_pedestrian_impact_publishes_immediate_low_body(city);
        test_fast_ego_pedestrian_impact_keeps_vehicle_momentum(city);
        test_pedestrian_pair_waits_for_and_moves_on_walk(city);
        test_pedestrians_require_enough_walk_time_for_new_crossing(city);
        std::cout << "npc_host_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_host_test: " << error.what() << '\n';
        return 1;
    }
}
