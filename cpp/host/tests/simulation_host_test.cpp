#include "simulation_host.hpp"

#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string make_control_message(
    std::string_view session_id = "session-1",
    std::uint64_t sequence = 1,
    std::uint64_t client_time_ns = 1,
    float throttle = 0.5f,
    bool estop = false,
    std::string_view map_package_checksum = "test-map",
    std::string_view source_id = "unreal-test")
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(sequence);
    envelope.set_source_id(std::string(source_id));
    envelope.set_session_id(std::string(session_id));
    envelope.set_map_package_checksum(std::string(map_package_checksum));
    auto* command = envelope.mutable_control_command();
    command->set_mode(estop
        ? simcore::CONTROL_MODE_ESTOP
        : simcore::CONTROL_MODE_MANUAL);
    command->set_throttle(throttle);
    command->set_estop(estop);
    command->set_client_time_ns(client_time_ns);
    return envelope.SerializeAsString();
}

std::string make_reset_message(
    std::string_view connection_session_id,
    std::string_view play_session_id,
    std::uint64_t sequence = 1,
    std::uint64_t client_time_ns = 1,
    std::string_view map_package_checksum = "test-map",
    std::string_view source_id = "unreal-test")
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(sequence);
    envelope.set_source_id(std::string(source_id));
    envelope.set_session_id(std::string(connection_session_id));
    envelope.set_map_package_checksum(std::string(map_package_checksum));
    auto* reset = envelope.mutable_simulation_reset();
    reset->set_play_session_id(std::string(play_session_id));
    reset->set_client_time_ns(client_time_ns);
    return envelope.SerializeAsString();
}

std::string make_hello_message(
    std::string_view session_id = "hello-session",
    std::uint64_t sequence = 1,
    std::string_view map_package_checksum = "test-map",
    bool include_all_required_capabilities = true,
    std::string_view source_id = "unreal-test",
    std::string_view build = "drive-integration-test",
    std::string_view additional_capability = {})
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(sequence);
    envelope.set_source_id(std::string(source_id));
    envelope.set_session_id(std::string(session_id));
    envelope.set_map_package_checksum(std::string(map_package_checksum));
    auto* hello = envelope.mutable_hello();
    hello->set_build(std::string(build));
    hello->set_schema(std::string(simcore_host::kProtocolSchemaName));
    hello->add_capabilities("world-state.v2");
    hello->add_capabilities("control.v2");
    hello->add_capabilities("simulation-reset.v1");
    if (include_all_required_capabilities) {
        hello->add_capabilities("map-package-checksum.v1");
    }
    if (!additional_capability.empty()) {
        hello->add_capabilities(std::string(additional_capability));
    }
    return envelope.SerializeAsString();
}

std::uint64_t simulation_time_ns(SimulationHost& host)
{
    simcore::Envelope envelope;
    require(envelope.ParseFromString(host.make_initial_world_state()),
            "host state envelope must parse");
    return envelope.simulation_time_ns();
}

simcore::Health parse_world_health(const std::string& bytes)
{
    simcore::Envelope envelope;
    require(envelope.ParseFromString(bytes) && envelope.has_world_state()
            && envelope.world_state().has_health() && !envelope.has_health(),
            "every host WorldState must carry atomic authoritative Health");
    return envelope.world_state().health();
}

simcore::Health health_snapshot(SimulationHost& host)
{
    return parse_world_health(host.make_initial_world_state());
}

void run_for(boost::asio::io_context& ioc,
             SimulationHost& host,
             std::chrono::milliseconds duration)
{
    boost::asio::steady_timer stopper(ioc);
    stopper.expires_after(duration);
    stopper.async_wait([&](const boost::system::error_code&) { host.stop(); });
    host.start();
    ioc.run();
    ioc.restart();
}

void run_one_tick(boost::asio::io_context& ioc, SimulationHost& host)
{
    host.start();
    require(ioc.run_one() == 1,
            "one fixed-tick callback must execute");
    host.stop();
    (void)ioc.poll();
    ioc.restart();
}

simcore_host::RuntimeMapPackage make_runtime_map_package(
    std::string checksum,
    double ground_height_m,
    std::size_t static_collider_count)
{
    const simcore_host::GroundTriangle first{
        "reload-ground",
        {{{-100.0, -100.0, ground_height_m},
          {100.0, -100.0, ground_height_m},
          {100.0, 100.0, ground_height_m}}}};
    const simcore_host::GroundTriangle second{
        "reload-ground",
        {{{-100.0, -100.0, ground_height_m},
          {100.0, 100.0, ground_height_m},
          {-100.0, 100.0, ground_height_m}}}};
    auto ground = std::make_shared<simcore_host::MapPackageGroundQuery>(
        std::vector<simcore_host::GroundTriangle>{first, second});

    std::vector<simcore_host::StaticObbCollider> colliders;
    for (std::size_t index = 0; index < static_collider_count; ++index) {
        colliders.push_back({
            "reload-wall-" + std::to_string(index),
            simcore_host::StaticColliderSemantic::Wall,
            {{80.0 + static_cast<double>(index), 80.0},
             ground_height_m + 1.0, 0.0, 0.25, 0.25, 1.0},
            {0.8, 0.0},
        });
    }
    auto collision = std::make_shared<simcore_host::CollisionWorld>(
        std::move(colliders));
    return {
        "reload-test",
        std::move(checksum),
        std::move(ground),
        std::move(collision),
    };
}

SimulationHostConfig make_test_config()
{
    SimulationHostConfig config;
    config.origin_lat = 40.7069;
    config.origin_lon = -74.0095;
    config.physics_frequency_hz = 1000.0;
    config.command_timeout = std::chrono::milliseconds(250);
    config.max_command_queue_age = std::chrono::milliseconds(100);
    config.source_id = "host-test";
    config.map_package_checksum = "test-map";
    return config;
}

SimulationHostConfig make_runtime_entity_test_config()
{
    auto config = make_test_config();
    config.runtime_entities = {
        {
            2001,
            simcore_host::RuntimeEntityKind::Pedestrian,
            {
                "ped-2001",
                simcore_host::VerticalCapsule{{-4.0, 7.0}, 0.9, 0.35, 0.9},
                {0.0, -0.25},
                0.0,
                {0.7, 0.0},
            },
        },
        {
            1001,
            simcore_host::RuntimeEntityKind::NpcVehicle,
            {
                "npc-1001",
                simcore_host::ObbPrism{{6.0, 10.0}, 0.75, 0.0, 2.2, 1.0, 0.75},
                {0.5, 0.0},
                0.1,
                {0.9, 0.0},
            },
        },
    };
    return config;
}

void test_authoritative_health_initial_reset_active_and_nonnegative_age()
{
    boost::asio::io_context ioc;
    std::string last_published;
    SimulationHost host(ioc, make_test_config(), {
        [&](const std::string& bytes) { last_published = bytes; }, {}, {}});
    const auto initial = health_snapshot(host);
    require(initial.status() == "awaiting_reset"
            && !initial.has_control_command()
            && initial.last_command_age_ns() == 0
            && initial.tick_overrun_count() == 0,
            "initial authority must be reset-gated with explicitly absent command age");
    require(host.handle_client_message(
                make_reset_message("health-a", "health-pie-a", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "health test must open lifecycle");
    const auto reset = parse_world_health(last_published);
    require(reset.status() == "awaiting_control" && !reset.has_control_command()
            && reset.last_command_age_ns() == 0,
            "immediate reset publication must report waiting, never active");
    require(host.handle_client_message(
                make_control_message("health-a", 2, 2, 0.f), 1,
                SimulationHost::Clock::now() - std::chrono::milliseconds(10))
                == ClientMessageResult::ControlAccepted,
            "health test must accept ordinary control");
    const auto active = health_snapshot(host);
    require(active.status() == "active" && active.has_control_command()
            && active.last_command_age_ns() >= 10'000'000,
            "active authority must carry measured server receive age");
    require(host.handle_client_message(
                make_control_message("health-a", 3, 2'000'000'002, 0.f), 1,
                SimulationHost::Clock::now() + std::chrono::seconds(1))
                == ClientMessageResult::ControlAccepted,
            "health age clamp fixture must accept a synthetic future receive timestamp");
    const auto clamped = health_snapshot(host);
    require(clamped.status() == "active" && clamped.has_control_command()
            && clamped.last_command_age_ns() == 0,
            "a future receive timestamp must clamp age instead of unsigned wrapping");
    require(host.handle_client_message(
                make_control_message("health-a", 3, 2'000'000'002, 1.f), 1)
                == ClientMessageResult::Rejected,
            "stale command must be rejected without mutating authority");
    require(health_snapshot(host).status() == "active",
            "rejected control must not change Health authority");
    require(host.handle_client_message(
                make_reset_message("health-b", "health-pie-b", 1, 1), 2)
                == ClientMessageResult::SimulationReset,
            "fresh PIE must reset the accepted-command history");
    const auto next_play = parse_world_health(last_published);
    require(next_play.status() == "awaiting_control"
            && !next_play.has_control_command() && next_play.last_command_age_ns() == 0,
            "new PIE must not inherit the prior play's command age or active status");
}

void test_health_reports_tick_overruns_and_reset_clears_metrics()
{
    boost::asio::io_context ioc;
    std::string last_published;
    SimulationHost host(ioc, make_test_config(), {
        [&](const std::string& bytes) { last_published = bytes; }, {}, {}});
    // Deliberately miss the 1 ms fixture deadline before running one callback.
    // This tests the real clock counter without relying on machine load.
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
    run_one_tick(ioc, host);
    const auto tick = parse_world_health(last_published);
    require(tick.tick_overrun_count() > 0 && tick.status() == "awaiting_reset",
            "tick Health must expose the actual fixed-step overrun counter");
    require(health_snapshot(host).tick_overrun_count() == tick.tick_overrun_count(),
            "initial-connect and tick Health must agree on overrun metrics");
    require(host.handle_client_message(
                make_reset_message("metric-a", "metric-pie", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "metric fixture must reset simulation");
    require(parse_world_health(last_published).tick_overrun_count() == 0,
            "new simulation publication must clear the previous run's overruns");
}

void test_required_hello_gates_lifecycle_and_control()
{
    boost::asio::io_context ioc;
    auto config = make_test_config();
    config.require_client_hello = true;
    SimulationHost host(ioc, std::move(config), {});

    require(host.handle_client_message(
                make_reset_message("hello-session", "hello-pie", 2, 2), 10)
                == ClientMessageResult::Rejected,
            "production handshake must reject reset before Hello");
    require(host.handle_client_message(
                make_hello_message("hello-session", 1, "test-map", false), 10)
                == ClientMessageResult::Rejected,
            "Hello missing a required capability must fail closed");
    require(host.handle_client_message(
                make_hello_message("hello-session", 1), 10)
                == ClientMessageResult::HelloAccepted,
            "complete client Hello must open the application handshake");
    require(host.handle_client_message(
                make_hello_message("hello-session", 1), 10)
                == ClientMessageResult::DuplicateHello,
            "retransmitted Hello on one socket must be idempotent");
    require(host.handle_client_message(
                make_reset_message("hello-session", "hello-pie", 1, 1), 10)
                == ClientMessageResult::Rejected,
            "Reset must not reuse the accepted Hello sequence");
    require(host.handle_client_message(
                make_reset_message("hello-session", "hello-pie", 2, 2), 10)
                == ClientMessageResult::SimulationReset,
            "checksum-valid reset must proceed after Hello");
    require(host.handle_client_message(
                make_reset_message("hello-session", "hello-pie", 2, 2), 10)
                == ClientMessageResult::DuplicateSimulationReset,
            "an exact same-sequence Reset retransmission must be idempotent");
    require(host.handle_client_message(
                make_control_message("hello-session", 2, 2, 0.25f), 10)
                == ClientMessageResult::Rejected,
            "Control must not reuse an accepted Reset sequence");
    require(host.handle_client_message(
                make_control_message("hello-session", 3, 3, 0.25f), 10)
                == ClientMessageResult::ControlAccepted,
            "control must proceed only after Hello and lifecycle reset");

    simcore::Envelope server_hello;
    require(server_hello.ParseFromString(host.make_initial_hello())
            && server_hello.has_hello()
            && server_hello.hello().schema() == simcore_host::kProtocolSchemaName,
            "connect frame must be a server Hello with the canonical schema");
    bool has_world_health = false;
    for (const auto& capability : server_hello.hello().capabilities()) {
        has_world_health = has_world_health || capability == "world-health.v1";
    }
    require(has_world_health,
            "server Hello must advertise optional atomic world-health.v1 support");
}

void test_hello_binds_identity_and_sequence_without_rejection_poisoning()
{
    boost::asio::io_context ioc;
    auto config = make_test_config();
    config.require_client_hello = true;
    SimulationHost host(ioc, std::move(config), {});
    constexpr std::uint64_t generation = 77;

    require(host.handle_client_message(
                make_hello_message(
                    "bound-session", 10, "test-map", true, "bound-source"),
                generation)
                == ClientMessageResult::HelloAccepted,
            "first Hello must bind source and session to its generation");
    require(host.handle_client_message(
                make_hello_message(
                    "bound-session", 9, "test-map", true, "bound-source"),
                generation)
                == ClientMessageResult::Rejected,
            "a duplicate Hello sequence must not regress");
    require(host.handle_client_message(
                make_hello_message(
                    "other-session", 11, "test-map", true, "bound-source"),
                generation)
                == ClientMessageResult::Rejected,
            "one generation must not change its Hello identity");
    require(host.handle_client_message(
                make_hello_message(
                    "bound-session", 11, "test-map", true, "bound-source"),
                generation)
                == ClientMessageResult::DuplicateHello,
            "a same-identity non-regressing Hello must remain idempotent");
    require(host.handle_client_message(
                make_hello_message(
                    "bound-session", 100, "test-map", true,
                    "bound-source", "changed-build"),
                generation)
                == ClientMessageResult::Rejected,
            "a duplicate Hello must not renegotiate its accepted build");
    require(host.handle_client_message(
                make_hello_message(
                    "bound-session", 100, "test-map", true,
                    "bound-source", "drive-integration-test",
                    "changed-capability.v1"),
                generation)
                == ClientMessageResult::Rejected,
            "a duplicate Hello must not renegotiate its capability set");

    require(host.handle_client_message(
                make_reset_message(
                    "bound-session", "bound-play", 10, 10,
                    "test-map", "bound-source"),
                generation)
                == ClientMessageResult::Rejected,
            "Reset must not regress below the latest Hello sequence");
    require(host.handle_client_message(
                make_reset_message(
                    "wrong-session", "bound-play", 100, 100,
                    "test-map", "bound-source"),
                generation)
                == ClientMessageResult::Rejected,
            "a high-sequence wrong-identity Reset must be rejected");
    require(host.handle_client_message(
                make_reset_message(
                    "bound-session", "bound-play", 12, 12,
                    "test-map", "bound-source"),
                generation)
                == ClientMessageResult::SimulationReset,
            "rejected high sequence must not poison a later valid Reset");

    require(host.handle_client_message(
                make_control_message(
                    "bound-session", 100, 100, 0.5f, false,
                    "wrong-map", "bound-source"),
                generation)
                == ClientMessageResult::Rejected,
            "checksum rejection must not advance Hello connection ordering");
    require(host.handle_client_message(
                make_control_message(
                    "bound-session", 13, 13, 0.25f, false,
                    "test-map", "bound-source"),
                generation)
                == ClientMessageResult::ControlAccepted,
            "valid lower control must survive a rejected high sequence");
    require(host.handle_client_message(
                make_control_message(
                    "wrong-session", 200, 200, 0.f, true,
                    "test-map", "bound-source"),
                generation)
                == ClientMessageResult::Rejected,
            "post-Hello E-stop must still match the bound connection identity");
    require(host.handle_client_message(
                make_control_message(
                    "bound-session", 14, 14, 0.f, false,
                    "test-map", "bound-source"),
                generation)
                == ClientMessageResult::ControlAccepted,
            "rejected mismatched E-stop must not latch or poison ordering");
    require(host.handle_client_message(
                make_hello_message(
                    "bound-session", 13, "test-map", true, "bound-source"),
                generation)
                == ClientMessageResult::Rejected,
            "Hello retransmission must compare against accepted Reset/Control ordering");
    require(host.handle_client_message(
                make_hello_message(
                    "bound-session", 14, "test-map", true, "bound-source"),
                generation)
                == ClientMessageResult::Rejected,
            "Hello must not reuse the sequence of a different accepted payload");
    require(host.handle_client_message(
                make_hello_message(
                    "bound-session", 15, "test-map", true, "bound-source"),
                generation)
                == ClientMessageResult::DuplicateHello,
            "an exact negotiated Hello identity may retransmit at a newer sequence");
}

void test_log_exposed_client_identifiers_require_printable_ascii()
{
    boost::asio::io_context ioc;
    auto config = make_test_config();
    config.require_client_hello = true;
    SimulationHost host(ioc, std::move(config), {});

    require(host.handle_client_message(
                make_hello_message(
                    "safe-session", 1, "test-map", true,
                    "source\nforged"),
                1)
                == ClientMessageResult::Rejected,
            "Hello source must reject log-forging newlines");
    require(host.handle_client_message(
                make_hello_message(
                    "session\tforged", 1, "test-map", true,
                    "safe-source"),
                2)
                == ClientMessageResult::Rejected,
            "Hello session must reject non-printable ASCII");
    require(host.handle_client_message(
                make_hello_message(
                    "safe-session", 1, "test-map", true,
                    "safe-source", "build\rforged"),
                3)
                == ClientMessageResult::Rejected,
            "Hello build must reject carriage returns");
    require(host.handle_client_message(
                make_hello_message(
                    "safe-session", 1, "test-map", true,
                    "safe-source", "safe-build", "telemetry.v1\nforged"),
                4)
                == ClientMessageResult::Rejected,
            "Hello capability must reject log-forging newlines");

    constexpr std::uint64_t valid_generation = 5;
    require(host.handle_client_message(
                make_hello_message(
                    "safe-session", 1, "test-map", true, "safe-source"),
                valid_generation)
                == ClientMessageResult::HelloAccepted,
            "printable Hello identity must remain valid");
    require(host.handle_client_message(
                make_reset_message(
                    "safe-session", "play\nforged", 100, 100,
                    "test-map", "safe-source"),
                valid_generation)
                == ClientMessageResult::Rejected,
            "play-session ID must reject log-forging newlines");
    require(host.handle_client_message(
                make_reset_message(
                    "safe-session", "safe-play", 2, 2,
                    "test-map", "safe-source"),
                valid_generation)
                == ClientMessageResult::SimulationReset,
            "rejected unprintable high sequence must not poison ordering");

    boost::asio::io_context estop_ioc;
    auto estop_config = make_test_config();
    estop_config.require_client_hello = true;
    SimulationHost estop_host(estop_ioc, std::move(estop_config), {});
    require(estop_host.handle_client_message(
                make_control_message(
                    "emergency\nsession", 1, 1, 0.f, true),
                9)
                == ClientMessageResult::Rejected,
            "pre-Hello E-stop identifiers must still be log-safe");
    require(estop_host.handle_client_message(
                make_control_message(
                    "emergency-session", 1, 1, 0.f, true),
                9)
                == ClientMessageResult::EmergencyStopLatched,
            "printable pre-Hello E-stop must remain available for safety");
}

double runtime_entity_east(
    const simcore_host::RuntimeEntityState& entity)
{
    return std::visit(
        [](const auto& shape) { return shape.center_enu.east_m; },
        entity.collision_proxy.shape);
}

void test_runtime_entity_lifecycle_and_world_state_order()
{
    boost::asio::io_context ioc;
    SimulationHost host(ioc, make_runtime_entity_test_config(), {});
    require(host.runtime_entities().size() == 2
            && host.runtime_entities()[0].entity_id == 1001
            && host.runtime_entities()[1].entity_id == 2001,
            "host must normalize runtime entities into stable numeric ID order");

    simcore::Envelope initial;
    require(initial.ParseFromString(host.make_initial_world_state())
            && initial.world_state().entities_size() == 3
            && initial.world_state().entities(0).entity_id() == 1
            && initial.world_state().entities(1).entity_id() == 1001
            && initial.world_state().entities(2).entity_id() == 2001,
            "initial WorldState must publish Ego then deterministic runtime order");

    require(host.handle_client_message(
                make_reset_message("socket-a", "pie-runtime-a", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "runtime lifecycle test must initialize its first PIE");
    const double spawn_npc_east = runtime_entity_east(host.runtime_entities()[0]);
    const double spawn_pedestrian_north = std::visit(
        [](const auto& shape) { return shape.center_enu.north_m; },
        host.runtime_entities()[1].collision_proxy.shape);
    run_for(ioc, host, std::chrono::milliseconds(25));
    const double moved_npc_east = runtime_entity_east(host.runtime_entities()[0]);
    const double moved_pedestrian_north = std::visit(
        [](const auto& shape) { return shape.center_enu.north_m; },
        host.runtime_entities()[1].collision_proxy.shape);
    require(moved_npc_east > spawn_npc_east
            && moved_pedestrian_north < spawn_pedestrian_north,
            "runtime proxy authority must advance each entity exactly once per tick");

    require(host.handle_client_message(
                make_reset_message("socket-b", "pie-runtime-a", 1, 2), 2)
                == ClientMessageResult::DuplicateSimulationReset,
            "same-PIE reconnect must preserve runtime entity continuity");
    require(runtime_entity_east(host.runtime_entities()[0]) == moved_npc_east,
            "same-PIE reconnect must not rewind runtime entities");

    require(host.handle_client_message(
                make_reset_message("socket-c", "pie-runtime-b", 1, 3), 3)
                == ClientMessageResult::SimulationReset,
            "new PIE must reset runtime entity authority");
    require(runtime_entity_east(host.runtime_entities()[0]) == spawn_npc_east,
            "new PIE must restore the NPC spawn exactly");
    const double reset_pedestrian_north = std::visit(
        [](const auto& shape) { return shape.center_enu.north_m; },
        host.runtime_entities()[1].collision_proxy.shape);
    require(reset_pedestrian_north == spawn_pedestrian_north,
            "new PIE must restore the pedestrian spawn exactly");

    simcore::Envelope reset_world;
    require(reset_world.ParseFromString(host.make_initial_world_state())
            && reset_world.play_session_id() == "pie-runtime-b"
            && reset_world.world_state().entities(1).position_enu().x()
                == spawn_npc_east,
            "reset WorldState and collision authority must expose the same spawn");
}

void test_host_supplies_runtime_proxy_to_ego_collision()
{
    boost::asio::io_context ioc;
    auto config = make_test_config();
    config.collision_world =
        std::make_shared<simcore_host::CollisionWorld>();
    config.runtime_entities = {
        {
            1001,
            simcore_host::RuntimeEntityKind::NpcVehicle,
            {
                "blocking-npc",
                simcore_host::ObbPrism{
                    {0.0, 4.0}, 0.75, 0.0, 2.2, 1.0, 0.75},
                {},
                0.0,
                {0.9, 0.0},
            },
        },
    };
    SimulationHost host(ioc, std::move(config), {});
    require(host.handle_client_message(
                make_reset_message("socket-collision", "pie-collision", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "proxy collision test must initialize lifecycle");
    run_for(ioc, host, std::chrono::milliseconds(8));
    require(host.state().north < -0.05,
            "host tick must supply runtime proxies to Ego collision integration");
}

void test_soft_timeout_retains_session_and_fresh_control_recovers()
{
    boost::asio::io_context ioc;
    auto config = make_test_config();
    config.command_timeout = std::chrono::milliseconds(20);
    config.hard_command_timeout = std::chrono::milliseconds(500);
    int close_requests = 0;
    std::string last_published;
    SimulationHost host(
        ioc,
        std::move(config),
        {
            [&](const std::string& bytes) { last_published = bytes; },
            {},
            [&](const std::string&) { ++close_requests; },
        });

    require(host.handle_client_message(
                make_reset_message("soft-socket", "soft-pie", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "soft-timeout test must initialize lifecycle");
    require(host.handle_client_message(
                make_control_message("soft-socket", 2, 2, 1.f), 1)
                == ClientMessageResult::ControlAccepted,
            "soft-timeout test must arm the original session");

    run_for(ioc, host, std::chrono::milliseconds(60));
    require(close_requests == 0,
            "soft timeout must apply SafeStop without closing the socket");
    const auto stopped = parse_world_health(last_published);
    require(stopped.status() == "safe_stop" && stopped.has_control_command()
            && stopped.last_command_age_ns() > 20'000'000,
            "tick Health must report the applied soft stop with accepted-command age");
    require(host.handle_client_message(
                make_control_message(
                    "soft-socket", 3, 60'000'002, 1.f),
                1)
                == ClientMessageResult::ControlAccepted,
            "fresh control must recover the retained session after SafeStop");
    require(health_snapshot(host).status() == "active",
            "fresh control must immediately clear the authoritative soft-stop status");
}

void test_hard_timeout_retires_session_and_requests_reconnect_once()
{
    boost::asio::io_context ioc;
    auto config = make_test_config();
    config.command_timeout = std::chrono::milliseconds(20);
    config.hard_command_timeout = std::chrono::milliseconds(80);
    int close_requests = 0;
    SimulationHost host(
        ioc,
        std::move(config),
        {
            {},
            {},
            [&](const std::string&) { ++close_requests; },
        });

    require(host.handle_client_message(
                make_reset_message("hard-socket", "hard-pie", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "hard-timeout test must initialize lifecycle");
    require(host.handle_client_message(
                make_control_message("hard-socket", 2, 2, 1.f), 1)
                == ClientMessageResult::ControlAccepted,
            "hard-timeout test must arm the original session");

    run_for(ioc, host, std::chrono::milliseconds(120));
    require(close_requests == 1,
            "hard timeout must request exactly one reconnect close");
    const auto retired = health_snapshot(host);
    require(retired.status() == "reconnect_required"
            && retired.has_control_command()
            && retired.last_command_age_ns() > 80'000'000,
            "hard retirement must be distinct from recoverable SafeStop in Health");
    require(host.handle_client_message(
                make_control_message(
                    "hard-socket", 3, 120'000'002, 1.f),
                1)
                == ClientMessageResult::Rejected,
            "hard timeout must permanently fence the retired session");
    require(health_snapshot(host).status() == "reconnect_required",
            "a rejected retired command must not turn Health active");
    require(host.handle_client_message(
                make_reset_message("hard-new", "hard-pie", 1, 1), 2)
                == ClientMessageResult::DuplicateSimulationReset,
            "fresh socket must reconnect without resetting the same PIE");
    const auto reconnected = health_snapshot(host);
    require(reconnected.status() == "awaiting_control"
            && !reconnected.has_control_command()
            && reconnected.last_command_age_ns() == 0,
            "same-PIE reconnect must drop the retired command's age and status");
    require(host.handle_client_message(
                make_control_message("hard-new", 2, 2, 0.f), 2)
                == ClientMessageResult::ControlAccepted
            && health_snapshot(host).status() == "active",
            "fresh reconnect command must arm the authoritative Health state");
}

void test_host_requires_verified_map_identity()
{
    boost::asio::io_context ioc;
    for (const std::string checksum : {std::string{}, std::string{"unset"}}) {
        auto config = make_test_config();
        config.map_package_checksum = checksum;
        bool rejected = false;
        try {
            SimulationHost host(ioc, std::move(config), {});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected,
                "host must reject an empty or placeholder map identity");
    }
}

void test_map_reload_applies_at_tick_boundary_and_requires_fresh_handshake()
{
    boost::asio::io_context ioc;
    int close_requests = 0;
    std::string close_reason;
    SimulationHost host(
        ioc,
        make_test_config(),
        {
            {},
            {},
            [&](const std::string& reason) {
                ++close_requests;
                close_reason = reason;
            },
        });
    require(host.handle_client_message(
                make_reset_message("reload-old", "reload-pie-old", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "reload fixture must establish the old lifecycle");
    require(host.handle_client_message(
                make_control_message("reload-old", 2, 2, 1.f), 1)
                == ClientMessageResult::ControlAccepted,
            "reload fixture must arm old-map control");
    run_for(ioc, host, std::chrono::milliseconds(8));

    simcore::Envelope before_envelope;
    require(before_envelope.ParseFromString(host.make_initial_world_state()),
            "pre-reload WorldState must parse");
    const auto before_state = host.state();
    auto replacement = make_runtime_map_package("reload-map-b", 3.0, 1);
    std::vector<simcore_host::RuntimeEntityState> replacement_entities{
        {
            2001,
            simcore_host::RuntimeEntityKind::Pedestrian,
            {
                "reload-pedestrian",
                simcore_host::VerticalCapsule{{5.0, 5.0}, 3.9, 0.35, 0.9},
                {},
                0.0,
                {0.7, 0.0},
            },
        },
    };
    host.queue_map_package_reload(
        std::move(replacement),
        std::move(replacement_entities));

    require(host.map_package_checksum() == "test-map"
            && host.state().north == before_state.north
            && close_requests == 0,
            "queueing must not mutate physics before a fixed-tick boundary");
    run_one_tick(ioc, host);

    require(host.map_package_checksum() == "reload-map-b"
            && host.static_collider_count() == 1,
            "one tick must atomically install ground, static collision, and identity");
    const auto reset_state = host.state();
    require(reset_state.east == 0.0 && reset_state.north == 0.0
            && reset_state.speed == 0.f
            && reset_state.position_enu.z > 3.4,
            "map replacement must reset the vehicle onto the new ground");
    require(host.runtime_entities().size() == 1
            && std::visit(
                [](const auto& shape) { return shape.center_up_m; },
                host.runtime_entities()[0].collision_proxy.shape) == 3.9,
            "reload must replace demo entity spawn heights for the new ground");
    require(simulation_time_ns(host) == 0,
            "reload boundary must restart simulation time without an immediate extra tick");
    require(close_requests == 1
            && close_reason == "map package reloaded; reconnect required",
            "reload must close the old checksum lifecycle exactly once");

    simcore::Envelope after_envelope;
    require(after_envelope.ParseFromString(host.make_initial_world_state())
            && after_envelope.map_package_checksum() == "reload-map-b"
            && after_envelope.play_session_id().empty()
            && after_envelope.sequence() > before_envelope.sequence(),
            "new connect snapshot must advertise the new identity without sequence rollback");
    const auto& reloaded_health = after_envelope.world_state().health();
    require(after_envelope.world_state().has_health()
            && reloaded_health.status() == "awaiting_reset"
            && !reloaded_health.has_control_command()
            && reloaded_health.last_command_age_ns() == 0
            && reloaded_health.tick_overrun_count() == 0,
            "map swap must publish fresh Health without old-map command or timing metrics");
    require(host.handle_client_message(
                make_reset_message("stale", "stale-pie", 1, 10), 2)
                == ClientMessageResult::Rejected,
            "old-checksum lifecycle frames must remain fenced after reload");
    require(host.handle_client_message(
                make_control_message(
                    "reload-new", 1, 11, 1.f, false, "reload-map-b"),
                3)
                == ClientMessageResult::Rejected,
            "new-checksum control must remain reset-gated");
    require(host.handle_client_message(
                make_reset_message(
                    "reload-new", "reload-pie-new", 1, 12, "reload-map-b"),
                3)
                == ClientMessageResult::SimulationReset,
            "fresh play and connection identities must handshake on the new map");
    require(host.handle_client_message(
                make_control_message(
                    "reload-new", 2, 13, 0.f, false, "reload-map-b"),
                3)
                == ClientMessageResult::ControlAccepted,
            "control must recover after the new checksum reset handshake");
    require(health_snapshot(host).status() == "active",
            "new-map control must restore active Health after the fresh reset");
}

void test_map_reload_queue_is_latest_verified_candidate_wins()
{
    boost::asio::io_context ioc;
    int close_requests = 0;
    SimulationHost host(
        ioc,
        make_test_config(),
        {{}, {}, [&](const std::string&) { ++close_requests; }});

    host.queue_map_package_reload(
        make_runtime_map_package("reload-map-b", 2.0, 1));
    host.queue_map_package_reload(
        make_runtime_map_package("reload-map-c", 4.0, 2));
    require(host.map_package_checksum() == "test-map"
            && host.static_collider_count() == 0,
            "multiple candidates must remain pending before the tick");
    run_one_tick(ioc, host);

    require(host.map_package_checksum() == "reload-map-c"
            && host.static_collider_count() == 2
            && host.state().position_enu.z > 4.4
            && close_requests == 1,
            "latest verified candidate must replace every map resource exactly once");
}

void test_new_pie_resets_pose_clock_and_control_lease()
{
    boost::asio::io_context ioc;
    int world_messages = 0;
    int observer_messages = 0;
    SimulationHost host(
        ioc,
        make_test_config(),
        {
            [&](const std::string&) { ++world_messages; },
            [&](const std::string&) { ++observer_messages; },
            {},
        });

    require(host.handle_client_message(
                make_reset_message("old-socket", "pie-run-a", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "old PIE reset must initialize its lifecycle before control");
    require(host.handle_client_message(
                make_control_message("old-socket", 2, 2, 1.f), 1)
                == ClientMessageResult::ControlAccepted,
            "old PIE control must be accepted after reset");
    run_for(ioc, host, std::chrono::milliseconds(40));
    require(host.state().speed > 0.f && host.state().north > 0.0,
            "test setup must move the authoritative vehicle");
    require(simulation_time_ns(host) > 0,
            "test setup must advance authoritative simulation time");
    const int world_before_reset = world_messages;
    const int observer_before_reset = observer_messages;

    require(host.handle_client_message(
                make_reset_message("new-socket", "pie-run-b", 1, 3), 2)
                == ClientMessageResult::SimulationReset,
            "a new PIE identity must reset the authoritative simulation");

    const auto reset = host.state();
    require(reset.east == 0.0 && reset.north == 0.0
            && reset.speed == 0.f && reset.position_enu.z > 0.0,
            "new PIE reset must restore the configured vehicle spawn");
    require(simulation_time_ns(host) == 0,
            "new PIE reset must restart simulation time");
    simcore::Envelope reset_world;
    require(reset_world.ParseFromString(host.make_initial_world_state())
            && reset_world.play_session_id() == "pie-run-b"
            && reset_world.world_state().entities(0).timestamp() > 0.0,
            "reset snapshot must echo its play identity with a live timestamp");
    require(world_messages == world_before_reset + 1
            && observer_messages == observer_before_reset + 1,
            "reset must immediately publish one matching authoritative snapshot");

    require(host.handle_client_message(
                make_control_message("old-socket", 3, 4, 1.f), 1)
                == ClientMessageResult::Rejected,
            "a delayed command from the previous PIE must remain retired");
    require(host.handle_client_message(
                make_control_message("new-socket", 2, 5, 0.f), 2)
                == ClientMessageResult::ControlAccepted,
            "the reset-initiating socket must acquire control immediately");
}

void test_pre_reset_ordinary_control_is_rejected()
{
    boost::asio::io_context ioc;
    SimulationHost host(ioc, make_test_config(), {});
    const auto before = host.state();

    require(host.handle_client_message(
                make_control_message("socket-a", 1, 1, 1.f), 1)
                == ClientMessageResult::Rejected,
            "ordinary control must be rejected before a checksum-valid reset");
    const auto after = host.state();
    require(after.east == before.east && after.north == before.north
            && after.speed == before.speed,
            "pre-reset control rejection must not mutate vehicle state");

    require(host.handle_client_message(
                make_reset_message("socket-a", "pie-a", 1, 2), 1)
                == ClientMessageResult::SimulationReset,
            "checksum-valid reset must open the lifecycle gate");
    require(host.handle_client_message(
                make_control_message("socket-a", 2, 3, 1.f), 1)
                == ClientMessageResult::ControlAccepted,
            "ordinary control must be accepted after its lifecycle reset");
}

void test_pre_reset_estop_is_latched()
{
    boost::asio::io_context ioc;
    SimulationHost host(ioc, make_test_config(), {});

    require(host.handle_client_message(
                make_control_message("emergency-socket", 1, 1, 1.f, true), 1)
                == ClientMessageResult::EmergencyStopLatched,
            "checksum-valid E-stop must remain available before reset");
    const auto stopped = health_snapshot(host);
    require(stopped.status() == "estop_latched" && !stopped.has_control_command()
            && stopped.last_command_age_ns() == 0,
            "E-stop Health must outrank the absent lifecycle without inventing a command age");
    require(host.handle_client_message(
                make_reset_message("emergency-socket", "pie-after-estop", 2, 2), 1)
                == ClientMessageResult::Rejected,
            "pre-reset E-stop must latch strongly enough to reject a later reset");
    require(health_snapshot(host).status() == "estop_latched",
            "a rejected reset must not clear the emergency authority state");
    host.queue_map_package_reload(make_runtime_map_package("estop-map", 0.0, 0));
    run_one_tick(ioc, host);
    require(health_snapshot(host).status() == "estop_latched",
            "map reload must not mask the process-lifetime E-stop latch");
}

void test_same_pie_reconnect_is_deduplicated_without_physics_reset()
{
    boost::asio::io_context ioc;
    int world_messages = 0;
    SimulationHost host(
        ioc,
        make_test_config(),
        {
            [&](const std::string&) { ++world_messages; },
            {},
            {},
        });

    require(host.handle_client_message(
                make_reset_message("socket-a", "pie-run-a", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "first reset for a PIE identity must be applied");
    require(host.handle_client_message(
                make_control_message("socket-a", 2, 2, 1.f), 1)
                == ClientMessageResult::ControlAccepted,
            "first PIE socket must acquire control");
    run_for(ioc, host, std::chrono::milliseconds(40));

    const auto before_reconnect = host.state();
    const auto time_before_reconnect = simulation_time_ns(host);
    const int world_before_reconnect = world_messages;
    require(before_reconnect.speed > 0.f && before_reconnect.north > 0.0,
            "test setup must move before reconnect");

    require(host.handle_client_message(
                make_reset_message("socket-b", "pie-run-a", 1, 3), 2)
                == ClientMessageResult::DuplicateSimulationReset,
            "retransmitted reset from the same PIE must be deduplicated");

    const auto after_reconnect = host.state();
    require(after_reconnect.east == before_reconnect.east
            && after_reconnect.north == before_reconnect.north
            && after_reconnect.speed == before_reconnect.speed,
            "same-PIE reconnect must not move the vehicle back to spawn");
    require(simulation_time_ns(host) == time_before_reconnect,
            "same-PIE reconnect must not rewind simulation time");
    require(world_messages == world_before_reconnect,
            "deduplicated reset must not publish a false spawn snapshot");
    require(host.handle_client_message(
                make_control_message("socket-a", 3, 4, 1.f), 1)
                == ClientMessageResult::Rejected,
            "the disconnected socket must be fenced off during handover");
    require(host.handle_client_message(
                make_control_message("socket-b", 2, 5, 1.f), 2)
                == ClientMessageResult::ControlAccepted,
            "same-PIE reconnect must hand control to the new socket immediately");
}

void test_estop_latch_cannot_be_cleared_by_pie_reset()
{
    boost::asio::io_context ioc;
    SimulationHost host(ioc, make_test_config(), {});

    require(host.handle_client_message(
                make_control_message("socket-a", 1, 1, 1.f, true), 1)
                == ClientMessageResult::EmergencyStopLatched,
            "test setup must latch E-stop");
    require(host.handle_client_message(
                make_reset_message("socket-b", "pie-run-after-estop", 1, 2), 2)
                == ClientMessageResult::Rejected,
            "PIE reset must not weaken the process-lifetime E-stop latch");
    require(host.handle_client_message(
                make_control_message("socket-b", 2, 3, 1.f), 2)
                == ClientMessageResult::Rejected,
            "control must remain rejected after a reset attempt under E-stop");
}

void test_older_connection_generation_cannot_mutate_the_active_pie()
{
    boost::asio::io_context ioc;
    SimulationHost host(ioc, make_test_config(), {});

    require(host.handle_client_message(
                make_reset_message("socket-current", "pie-current", 1, 10), 2)
                == ClientMessageResult::SimulationReset,
            "current generation reset must initialize the test PIE");
    require(host.handle_client_message(
                make_control_message("socket-current", 2, 11, 1.f), 2)
                == ClientMessageResult::ControlAccepted,
            "current generation control must arm the vehicle");
    run_for(ioc, host, std::chrono::milliseconds(40));
    const auto before = host.state();
    const auto time_before = simulation_time_ns(host);

    require(host.handle_client_message(
                make_reset_message("socket-old", "never-seen-old-pie", 1, 12), 1)
                == ClientMessageResult::Rejected,
            "an older connection generation must not reset with a fresh-looking ID");
    const auto after = host.state();
    require(after.east == before.east && after.north == before.north
            && after.speed == before.speed
            && simulation_time_ns(host) == time_before,
            "stale reset rejection must be side-effect free");
    require(host.handle_client_message(
                make_control_message("socket-old", 2, 13, 1.f), 1)
                == ClientMessageResult::Rejected,
            "control from an older generation must be fenced off");
    require(host.handle_client_message(
                make_control_message("socket-current", 3, 14, 0.f), 2)
                == ClientMessageResult::ControlAccepted,
            "stale traffic must not damage the current control lease");
}

void test_retired_reset_candidate_is_rejected_without_retiring_current_owner()
{
    boost::asio::io_context ioc;
    SimulationHost host(ioc, make_test_config(), {});

    require(host.handle_client_message(
                make_reset_message("socket-a", "pie-a", 1, 1), 1)
                == ClientMessageResult::SimulationReset,
            "first PIE reset must succeed");
    require(host.handle_client_message(
                make_control_message("socket-a", 2, 2, 0.f), 1)
                == ClientMessageResult::ControlAccepted,
            "first socket must become the owner");
    require(host.handle_client_message(
                make_reset_message("socket-b", "pie-a", 1, 3), 2)
                == ClientMessageResult::DuplicateSimulationReset,
            "new socket must reconnect the same PIE");

    const auto before = host.state();
    const auto time_before = simulation_time_ns(host);
    require(host.handle_client_message(
                make_reset_message("socket-a", "pie-b", 1, 4), 3)
                == ClientMessageResult::Rejected,
            "a retired session must not be reused as a new-PIE candidate");
    const auto after = host.state();
    require(after.east == before.east && after.north == before.north
            && simulation_time_ns(host) == time_before,
            "retired-candidate rejection must not reset physics or time");
    require(host.handle_client_message(
                make_control_message("socket-b", 2, 5, 0.f), 2)
                == ClientMessageResult::ControlAccepted,
            "the current owner must remain usable after candidate rejection");
}

void test_same_generation_reset_is_idempotent_and_session_reuse_is_rejected()
{
    boost::asio::io_context ioc;
    SimulationHost host(ioc, make_test_config(), {});
    const auto reset = make_reset_message("socket-a", "pie-a", 1, 1);

    require(host.handle_client_message(reset, 1)
                == ClientMessageResult::SimulationReset,
            "first reset must succeed");
    run_for(ioc, host, std::chrono::milliseconds(10));
    const auto time_before = simulation_time_ns(host);
    require(host.handle_client_message(reset, 1)
                == ClientMessageResult::DuplicateSimulationReset,
            "an identical reset frame must be idempotent");
    require(simulation_time_ns(host) == time_before,
            "an identical reset must not rewind simulation time");
    require(host.handle_client_message(
                make_reset_message("socket-a", "pie-a", 1, 2), 2)
                == ClientMessageResult::Rejected,
            "a newer socket generation must not reuse the prior session ID");
    require(host.handle_client_message(
                make_control_message("socket-a", 2, 3, 0.f), 1)
                == ClientMessageResult::ControlAccepted,
            "rejected session reuse must leave the prior generation active");
}

void test_traffic_lifecycle_and_map_reload()
{
    boost::asio::io_context ioc;
    auto config = make_test_config();
    auto network = std::make_shared<simcore_host::TrafficNetwork>();
    network->source_map_checksum = config.map_package_checksum;
    network->checksum = "fnv1a64:0123456789abcdef";
    network->signals = {{1, 1, {0, 0, 0}, 90}, {2, 2, {2, 2, 0}, 180}};
    config.traffic_network = network;
    SimulationHost host(ioc, config, {});
    const auto snapshot = [&] {
        simcore::Envelope envelope;
        require(envelope.ParseFromString(host.make_initial_world_state()), "traffic host wire parses");
        return envelope;
    };
    auto before = snapshot();
    require(before.world_state().traffic_signals_size() == 2
            && before.world_state().traffic_signals(0).aspect() == simcore::TRAFFIC_SIGNAL_RED,
            "pre-reset traffic must be present but red");
    require(host.handle_client_message(make_reset_message("traffic-a", "traffic-pie-a"), 1)
                == ClientMessageResult::SimulationReset, "traffic reset accepted");
    require(host.handle_client_message(make_control_message("traffic-a", 2, 2, 0), 1)
                == ClientMessageResult::ControlAccepted, "traffic control accepted");
    run_one_tick(ioc, host);
    auto active = snapshot();
    const auto expected = network->signals_at(active.simulation_time_ns());
    require(active.world_state().traffic_signals(0).remaining_seconds()
                == static_cast<float>(expected[0].remaining_seconds),
            "signal phase must use same envelope simulation clock as pose");
    require(host.handle_client_message(make_reset_message("traffic-b", "traffic-pie-b"), 2)
                == ClientMessageResult::SimulationReset, "new PIE resets traffic clock");
    require(snapshot().simulation_time_ns() == 0
            && snapshot().world_state().traffic_signals(0).aspect() == simcore::TRAFFIC_SIGNAL_RED,
            "new PIE starts all red at zero");
    host.queue_map_package_reload(make_runtime_map_package("traffic-new-map", 0, 0));
    run_one_tick(ioc, host);
    require(host.map_package_checksum() == "traffic-new-map", "traffic must not block terrain hot reload");
    require(snapshot().world_state().traffic_signals(0).aspect() == simcore::TRAFFIC_SIGNAL_RED
            && snapshot().world_state().traffic_signals(0).remaining_seconds() == 0,
            "old authored traffic remains fail-safe red against changed ground");
    auto replacement = std::make_shared<simcore_host::TrafficNetwork>(*network);
    replacement->source_map_checksum = "traffic-new-map";
    replacement->checksum = "fnv1a64:abcdef0123456789";
    host.queue_traffic_network_reload(replacement);
    require(snapshot().world_state().traffic_network_checksum() == network->checksum,
            "queued network must not change published state before tick");
    run_one_tick(ioc, host);
    require(snapshot().world_state().traffic_network_checksum() == replacement->checksum,
            "matching network must install without process restart");
    host.queue_traffic_network_reload(network);
    run_one_tick(ioc, host);
    require(snapshot().world_state().traffic_network_checksum() == replacement->checksum,
            "foreign network cannot replace matching network");
    require(host.handle_client_message(make_reset_message(
        "traffic-c", "traffic-pie-c", 1, 1, "traffic-new-map"), 3)
            == ClientMessageResult::SimulationReset, "map reload allows fresh PIE");
    require(host.handle_client_message(make_control_message(
        "traffic-c", 2, 2, 0, true, "traffic-new-map"), 3)
            == ClientMessageResult::EmergencyStopLatched, "traffic test EStop accepted");
    require(snapshot().world_state().traffic_signals(0).remaining_seconds() == 0,
            "EStop must force indefinite all red");
}

void test_estop_outranks_lifecycle_generation_ownership()
{
    boost::asio::io_context ioc;
    SimulationHost host(ioc, make_test_config(), {});
    require(host.handle_client_message(
                make_reset_message("socket-current", "pie-current", 1, 1), 2)
                == ClientMessageResult::SimulationReset,
            "current lifecycle owner must be established");

    require(host.handle_client_message(
                make_control_message("socket-stale", 2, 2, 1.f, true), 1)
                == ClientMessageResult::EmergencyStopLatched,
            "valid E-stop must outrank stale lifecycle ownership");
    require(host.handle_client_message(
                make_control_message("socket-current", 2, 3, 1.f), 2)
                == ClientMessageResult::Rejected,
            "active owner must remain blocked after out-of-owner E-stop");
}

} // namespace

int main()
{
    using namespace std::chrono_literals;
    boost::asio::io_context ioc;
    int world_messages = 0;
    int observer_messages = 0;
    int close_requests = 0;
    std::string last_world_message;
    std::string last_observer_message;

    SimulationHost host(
        ioc,
        {
            40.7069, -74.0095, 0.0, 0.f, 1000.0,
            250ms, 100ms, "host-test", "test-map",
        },
        {
            [&](const std::string& message) {
                ++world_messages;
                last_world_message = message;
            },
            [&](const std::string& message) {
                ++observer_messages;
                last_observer_message = message;
            },
            [&](const std::string&) { ++close_requests; },
        });

    simcore::Envelope initial;
    require(initial.ParseFromString(host.make_initial_world_state()),
            "initial state envelope must parse");
    require(initial.map_package_checksum() == "test-map",
            "initial WorldState must advertise the verified map identity");
    require(initial.world_state().entities(0).wheels_size() == 4,
            "initial state must expose all four wheels");
    require(host.handle_client_message(
                make_reset_message("session-1", "main-pie", 1, 1))
                == ClientMessageResult::SimulationReset,
            "main smoke must initialize lifecycle before control");
    require(host.handle_control_message(
                make_control_message("session-1", 2, 2))
                == ControlMessageResult::Accepted,
            "valid control message must be accepted by the host core");

    boost::asio::steady_timer stopper(ioc);
    stopper.expires_after(8ms);
    stopper.async_wait([&](const boost::system::error_code&) { host.stop(); });
    host.start();
    ioc.run();

    require(!host.running(), "stop must terminate the fixed-step loop");
    require(world_messages > 0, "fixed-step loop must publish world state");
    require(observer_messages == world_messages,
            "each tick must publish matching direct and observer state");
    require(close_requests == 0, "fresh control must not close connections");

    simcore::Envelope world;
    require(world.ParseFromString(last_world_message) && world.has_world_state(),
            "published world message must be a protobuf WorldState envelope");

    simcore::EntityStatePacket observer;
    require(observer.ParseFromString(last_observer_message)
                && observer.entities_size() == 1,
            "optional observer must preserve the frozen EntityStatePacket format");
    test_host_requires_verified_map_identity();
    test_traffic_lifecycle_and_map_reload();
    test_authoritative_health_initial_reset_active_and_nonnegative_age();
    test_health_reports_tick_overruns_and_reset_clears_metrics();
    test_required_hello_gates_lifecycle_and_control();
    test_hello_binds_identity_and_sequence_without_rejection_poisoning();
    test_log_exposed_client_identifiers_require_printable_ascii();
    test_map_reload_applies_at_tick_boundary_and_requires_fresh_handshake();
    test_map_reload_queue_is_latest_verified_candidate_wins();
    test_pre_reset_ordinary_control_is_rejected();
    test_pre_reset_estop_is_latched();
    test_new_pie_resets_pose_clock_and_control_lease();
    test_same_pie_reconnect_is_deduplicated_without_physics_reset();
    test_estop_latch_cannot_be_cleared_by_pie_reset();
    test_older_connection_generation_cannot_mutate_the_active_pie();
    test_retired_reset_candidate_is_rejected_without_retiring_current_owner();
    test_same_generation_reset_is_idempotent_and_session_reuse_is_rejected();
    test_estop_outranks_lifecycle_generation_ownership();
    test_runtime_entity_lifecycle_and_world_state_order();
    test_host_supplies_runtime_proxy_to_ego_collision();
    test_soft_timeout_retains_session_and_fresh_control_recovers();
    test_hard_timeout_retires_session_and_requests_reconnect_once();
    std::cout << "simulation_host_tests: all tests passed\n";
    return 0;
}
