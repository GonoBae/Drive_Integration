#include "simulation_host.hpp"

#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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
    bool estop = false)
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(sequence);
    envelope.set_source_id("unreal-test");
    envelope.set_session_id(std::string(session_id));
    envelope.set_map_package_checksum("test-map");
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
    std::uint64_t client_time_ns = 1)
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(sequence);
    envelope.set_source_id("unreal-test");
    envelope.set_session_id(std::string(connection_session_id));
    envelope.set_map_package_checksum("test-map");
    auto* reset = envelope.mutable_simulation_reset();
    reset->set_play_session_id(std::string(play_session_id));
    reset->set_client_time_ns(client_time_ns);
    return envelope.SerializeAsString();
}

std::uint64_t simulation_time_ns(SimulationHost& host)
{
    simcore::Envelope envelope;
    require(envelope.ParseFromString(host.make_initial_world_state()),
            "host state envelope must parse");
    return envelope.simulation_time_ns();
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
    SimulationHost host(
        ioc,
        std::move(config),
        {
            {},
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
    require(host.handle_client_message(
                make_control_message(
                    "soft-socket", 3, 60'000'002, 1.f),
                1)
                == ClientMessageResult::ControlAccepted,
            "fresh control must recover the retained session after SafeStop");
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
    require(host.handle_client_message(
                make_control_message(
                    "hard-socket", 3, 120'000'002, 1.f),
                1)
                == ClientMessageResult::Rejected,
            "hard timeout must permanently fence the retired session");
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
    require(host.handle_client_message(
                make_reset_message("emergency-socket", "pie-after-estop", 2, 2), 1)
                == ClientMessageResult::Rejected,
            "pre-reset E-stop must latch strongly enough to reject a later reset");
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
