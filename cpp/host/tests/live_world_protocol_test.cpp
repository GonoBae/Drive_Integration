#include "protocol/vehicle_messages.hpp"
#include "simulation_host.hpp"
#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace simcore_host;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

simcore::Envelope envelope(std::uint64_t sequence) {
    simcore::Envelope result;
    result.set_schema_version(kProtocolSchemaVersion);
    result.set_sequence(sequence);
    result.set_source_id("unreal");
    result.set_session_id("connection");
    result.set_play_session_id("play");
    result.set_map_package_checksum("map");
    return result;
}

GroundQueryBatch batch(std::uint64_t id = 11) {
    GroundQueryBatch result;
    result.request_id = id;
    result.environment_epoch = 4;
    result.reference_simulation_time_ns = 900;
    result.entity_id = 1;
    result.origins = {{.15, -.23, 8.55}};
    result.query_distances_m = {32};
    result.patch_step_enu = {.5, .5, 0};
    result.world_revision = 7;
    return result;
}

GroundQueryBatch batch(const simcore::GroundQueryBatch& wire) {
    GroundQueryBatch result;
    result.request_id = wire.request_id();
    result.environment_epoch = wire.environment_epoch();
    result.reference_simulation_time_ns = wire.reference_simulation_time_ns();
    result.entity_id = wire.entity_id();
    for (const auto& origin : wire.origins_enu()) result.origins.push_back({origin.x(), origin.y(), origin.z()});
    result.max_distance_m = wire.max_distance_m();
    result.query_distances_m.assign(wire.query_distances_m().begin(), wire.query_distances_m().end());
    result.patch_step_enu = {wire.patch_step_enu().x(), wire.patch_step_enu().y(), wire.patch_step_enu().z()};
    result.world_revision = wire.world_revision();
    return result;
}

simcore::Envelope ground_reply(const GroundQueryBatch& request, std::uint64_t sequence,
    bool npc = false, double height = .75) {
    auto result = envelope(sequence);
    auto* wire = npc ? result.mutable_npc_ground_tile_result() : result.mutable_live_ground_patch_result();
    wire->set_request_id(request.request_id);
    wire->set_environment_epoch(request.environment_epoch);
    wire->set_reference_simulation_time_ns(request.reference_simulation_time_ns);
    wire->set_world_revision(request.world_revision);
    wire->set_measurement_duration_ms(22.5);
    for (std::size_t index = 0; index < kGroundPatchSamples; ++index) {
        const auto origin = LiveGroundPatch::sample_origin(request, index);
        auto* probe = wire->add_probes();
        probe->set_status(simcore::GroundProbeResult::HIT);
        probe->mutable_point_enu()->set_x(origin.east_m);
        probe->mutable_point_enu()->set_y(origin.north_m);
        probe->mutable_point_enu()->set_z(height);
        probe->mutable_normal_enu()->set_z(1);
        probe->set_surface_material_id(1);
    }
    return result;
}

simcore::Envelope obstacle_reply(const simcore::WorldObstacleRequest& request, std::uint64_t sequence) {
    auto result = envelope(sequence);
    auto* wire = result.mutable_world_obstacle_result();
    wire->set_request_id(request.request_id());
    wire->set_environment_epoch(request.environment_epoch());
    wire->set_reference_simulation_time_ns(request.reference_simulation_time_ns());
    wire->set_world_revision(request.world_revision());
    wire->set_status(1);
    return result;
}

bool parses(const simcore::Envelope& value) {
    return parse_client_message_envelope(value.SerializeAsString()).has_value();
}

void notice_wire() {
    auto wire = envelope(31);
    auto* notice = wire.mutable_ground_world_revision();
    require(!parses(wire), "zero world revision must be rejected");
    notice->set_world_revision(9);
    for (bool ready : {false, true}) {
        notice->set_measurements_ready(ready);
        const auto parsed = parse_client_message_envelope(wire.SerializeAsString());
        require(parsed && std::holds_alternative<ParsedGroundWorldRevision>(*parsed), "world notice payload dispatch");
        const auto& result = std::get<ParsedGroundWorldRevision>(*parsed);
        require(result.sequence == 31 && result.world_revision == 9 && result.measurements_ready == ready
            && result.source_id == "unreal" && result.session_id == "connection"
            && result.play_session_id == "play" && result.map_package_checksum == "map",
            "world notice preserves lifecycle and readiness");
    }
}

void separated_ground_payloads() {
    for (bool npc : {false, true}) {
        const auto original = ground_reply(batch(), 40, npc);
        const auto parsed = parse_client_message_envelope(original.SerializeAsString());
        require(parsed && std::holds_alternative<ParsedGroundQueryResult>(*parsed), "live ground payload dispatch");
        const auto& result = std::get<ParsedGroundQueryResult>(*parsed);
        require(result.live_patch && result.npc_tile == npc && result.result.world_revision == 7
            && result.result.probes.size() == 625, "NPC and ego flags/revision must stay separate");
        auto malformed = original;
        auto* wire = npc ? malformed.mutable_npc_ground_tile_result() : malformed.mutable_live_ground_patch_result();
        wire->set_world_revision(0);
        require(!parses(malformed), "live ground requires a nonzero world revision");
        wire->set_world_revision(7);
        wire->mutable_probes()->RemoveLast();
        require(!parses(malformed), "partial 624-probe patch must be rejected");
        *wire->add_probes() = original.live_ground_patch_result().probes_size()
            ? original.live_ground_patch_result().probes(0) : original.npc_ground_tile_result().probes(0);
        *wire->add_probes() = wire->probes(0);
        require(!parses(malformed), "626-probe patch must be rejected before allocation growth");
    }

    auto diagnostics = envelope(42);
    auto* result = diagnostics.mutable_ground_query_result();
    result->set_request_id(1); result->set_environment_epoch(2);
    result->add_probes()->set_status(simcore::GroundProbeResult::NO_HIT);
    auto parsed = parse_client_message_envelope(diagnostics.SerializeAsString());
    require(parsed && !std::get<ParsedGroundQueryResult>(*parsed).live_patch
        && !std::get<ParsedGroundQueryResult>(*parsed).npc_tile, "diagnostics retain the original dispatch");
    result->set_world_revision(1);
    require(!parses(diagnostics), "diagnostics cannot inject a live world revision");
    result->set_world_revision(0);
    result->add_boundaries()->set_cell_index(1);
    require(!parses(diagnostics), "diagnostics cannot carry live boundaries");

    auto maximal = ground_reply(batch(), 43, true);
    auto* live = maximal.mutable_npc_ground_tile_result();
    // Fill every double, so the bound does not accidentally depend on proto3
    // omitting zero-valued fields in a flat fixture.
    for (auto& probe : *live->mutable_probes()) {
        probe.mutable_normal_enu()->set_x(.1);
        probe.mutable_normal_enu()->set_y(.1);
        probe.mutable_normal_enu()->set_z(std::sqrt(.98));
    }
    for (int index = 0; index < 128; ++index) {
        auto* boundary = live->add_boundaries();
        boundary->set_cell_index(index);
        boundary->mutable_point_enu()->set_x(.13);
        boundary->mutable_point_enu()->set_y(.17);
        boundary->mutable_point_enu()->set_z(.2);
        boundary->mutable_normal_enu()->set_x(.6);
        boundary->mutable_normal_enu()->set_y(.8);
    }
    require(maximal.ByteSizeLong() < 64 * 1024 && parses(maximal),
        "625 probes plus 128 boundaries fit the unchanged 64 KiB WebSocket bound");
    live->add_boundaries()->set_cell_index(129);
    require(!parses(maximal), "129 boundaries exceed the bounded wire contract");
}

void obstacle_wire() {
    simcore::WorldObstacleRequest request;
    request.set_request_id(8); request.set_environment_epoch(3);
    request.set_reference_simulation_time_ns(1234); request.set_world_revision(9);
    const auto original = obstacle_reply(request, 50);
    for (unsigned status : {1u, 2u, 3u}) {
        auto value = original;
        value.mutable_world_obstacle_result()->set_status(status);
        const auto parsed = parse_client_message_envelope(value.SerializeAsString());
        require(parsed && std::holds_alternative<ParsedWorldObstacleResult>(*parsed), "obstacle status dispatch");
        const auto& result = std::get<ParsedWorldObstacleResult>(*parsed);
        require(static_cast<unsigned>(result.result.status) == status && result.result.colliders.empty()
            && result.result.world_revision == 9 && result.result.request_id == 8
            && result.result.reference_simulation_time_ns == 1234 && result.play_session_id == "play",
            "complete-empty and incomplete-empty obstacle snapshots stay distinguishable");
    }
    for (unsigned status : {0u, 4u}) {
        auto value = original; value.mutable_world_obstacle_result()->set_status(status);
        require(!parses(value), "unknown obstacle status rejected");
    }
    auto value = original;
    auto* wire = value.mutable_world_obstacle_result();
    for (int index = 0; index < 128; ++index) {
        auto* collider = wire->add_colliders();
        collider->set_collider_id("measured-wall-" + std::to_string(index));
        collider->set_semantic(index % 3);
        collider->mutable_centre_enu()->set_x(.1); collider->mutable_centre_enu()->set_y(.2);
        collider->mutable_centre_enu()->set_z(.3); collider->set_heading_rad(.4);
        collider->mutable_half_extent_enu()->set_x(.5); collider->mutable_half_extent_enu()->set_y(.6);
        collider->mutable_half_extent_enu()->set_z(.7);
        collider->set_friction(.8); collider->set_restitution(.1);
    }
    require(parses(value) && value.ByteSizeLong() < 64 * 1024, "128 obstacles fit bounded wire");
    for (unsigned status : {2u, 3u}) {
        wire->set_status(status);
        require(!parses(value), "incomplete obstacle result cannot contain partial geometry");
    }
    wire->set_status(1);
    *wire->add_colliders() = wire->colliders(0);
    require(!parses(value), "129 obstacles rejected");
    wire->mutable_colliders()->RemoveLast();
    for (const std::string id : {std::string{}, std::string(129, 'x')}) {
        wire->mutable_colliders(0)->set_collider_id(id);
        require(!parses(value), "empty or oversized collider identifier rejected");
    }
    wire->mutable_colliders(0)->set_collider_id("wall");
    wire->mutable_colliders(0)->set_semantic(3);
    require(!parses(value), "unknown collider semantic rejected");
    wire->mutable_colliders(0)->set_semantic(0);
    wire->set_world_revision(0);
    require(!parses(value), "obstacle result requires a world revision");
}

void simultaneous_world_requests() {
    const auto ego = batch(31), npc = batch(77);
    auto shadow = batch(12);
    shadow.world_revision = 0; shadow.patch_step_enu = {}; shadow.query_distances_m = {10};
    const LiveObstacleRequest obstacles{18, 4, 900, 7, {1, 2, 3}, {6, 7, 8}};
    VehiclePhysics physics(0, 0, 0, 0);
    EnvelopeMetadata metadata{9, 900, "server", "map", "play", "connection"};
    const auto bytes = serialize_world_state_envelope(physics.get_state(), {}, metadata, {}, {}, {}, {},
        RuntimeVehicleClass::Sedan, &shadow, &ego, &obstacles, &npc);
    simcore::Envelope envelope;
    require(envelope.ParseFromString(bytes), "combined world parse");
    const auto& world = envelope.world_state();
    require(world.entities_size() == 1 && world.has_ground_query_shadow() && world.has_live_ground_patch()
        && world.has_world_obstacles() && world.has_npc_ground_tile(), "all four requests coexist with player state");
    require(world.ground_query_shadow().request_id() == 12 && world.ground_query_shadow().world_revision() == 0
        && world.live_ground_patch().request_id() == 31 && world.live_ground_patch().world_revision() == 7
        && world.world_obstacles().request_id() == 18 && world.world_obstacles().world_revision() == 7
        && world.npc_ground_tile().request_id() == 77 && world.npc_ground_tile().world_revision() == 7,
        "field 6/7/8/9 must not overwrite or confuse independent request identities");
    require(world.world_obstacles().half_extent_enu().y() == 7
        && world.npc_ground_tile().patch_step_enu().x() == .5
        && world.npc_ground_tile().query_distances_m(0) == 32,
        "obstacle region and NPC lattice parameters survive serialization");
}

void one_tick(boost::asio::io_context& ioc, SimulationHost& host) {
    host.start();
    require(ioc.run_one() == 1, "host tick did not run");
    host.stop(); ioc.poll(); ioc.restart();
}

simcore::Envelope world(SimulationHost& host) {
    simcore::Envelope result;
    require(result.ParseFromString(host.make_initial_world_state()), "host initial world parse");
    return result;
}

void host_lifecycle_and_joint_readiness() {
    boost::asio::io_context ioc;
    SimulationHostConfig config;
    config.source_id = "server"; config.map_package_checksum = "map";
    config.require_client_hello = true; config.live_ground = true; config.live_obstacles = true;
    config.ground_query = std::make_shared<FlatGroundQuery>(4.0);
    // Deliberately wrong file collision at spawn must not influence accepted live physics.
    config.collision_world = std::make_shared<CollisionWorld>(std::vector<StaticObbCollider>{
        {"file-only-wall", StaticColliderSemantic::Wall, {{0, 0}, 1, 0, 30, 30, 3}, {.8, 0}}});
    SimulationHost host(ioc, config, {});
    auto hello = envelope(1);
    auto* h = hello.mutable_hello(); h->set_build("test"); h->set_schema("simcore-envelope-v2");
    for (const auto* capability : {"world-state.v2", "control.v2", "simulation-reset.v1", "map-package-checksum.v1"})
        h->add_capabilities(capability);
    h->add_capabilities("ground-patch.v2");
    require(host.handle_client_message(hello.SerializeAsString(), 1) == ClientMessageResult::Rejected,
        "v2-only Hello cannot bypass world revision fencing");
    h->add_capabilities("ground-patch.v3");
    require(host.handle_client_message(hello.SerializeAsString(), 1) == ClientMessageResult::Rejected,
        "live obstacle mode requires explicit obstacle capability");
    h->add_capabilities("world-obstacles.v1");
    require(host.handle_client_message(hello.SerializeAsString(), 1) == ClientMessageResult::HelloAccepted,
        "joint live-world capability exchange");
    auto reset = envelope(2);
    reset.mutable_simulation_reset()->set_play_session_id("play");
    reset.mutable_simulation_reset()->set_client_time_ns(1);
    require(host.handle_client_message(reset.SerializeAsString(), 1) == ClientMessageResult::SimulationReset,
        "joint live-world reset");
    require(!world(host).world_state().has_world_obstacles(), "no obstacle request before a world revision");
    auto notice = envelope(3);
    notice.mutable_ground_world_revision()->set_world_revision(1);
    notice.mutable_ground_world_revision()->set_measurements_ready(true);
    require(host.handle_client_message(notice.SerializeAsString(), 1) == ClientMessageResult::GroundWorldRevisionAccepted,
        "initial revision accepted");
    auto initial = world(host);
    require(initial.world_state().has_live_ground_patch() && initial.world_state().has_world_obstacles(),
        "ground and obstacle measurements requested together");
    auto ground = ground_reply(batch(initial.world_state().live_ground_patch()), 4);
    auto obstacles = obstacle_reply(initial.world_state().world_obstacles(), 5);
    for (int field = 0; field < 5; ++field) {
        auto invalid = obstacles; invalid.set_sequence(1000);
        if (field == 0) invalid.set_session_id("retired");
        if (field == 1) invalid.set_play_session_id("old-play");
        if (field == 2) invalid.set_map_package_checksum("old-map");
        if (field == 3) invalid.set_source_id("other-client");
        if (field == 4) invalid.mutable_world_obstacle_result()->set_world_revision(2);
        require(host.handle_client_message(invalid.SerializeAsString(), 1) == ClientMessageResult::Rejected,
            "obstacle result must be fenced without poisoning sequence");
    }
    require(host.handle_client_message(obstacles.SerializeAsString(), 2) == ClientMessageResult::Rejected,
        "different connection generation rejected");
    const auto before = host.state();
    require(host.handle_client_message(ground.SerializeAsString(), 1) == ClientMessageResult::GroundQueryResultAccepted,
        "joint mode accepts matching ground patch");
    one_tick(ioc, host);
    require(host.state().position_enu.z == before.position_enu.z,
        "ground alone cannot advance player through an unmeasured obstacle world");
    require(host.handle_client_message(obstacles.SerializeAsString(), 1) == ClientMessageResult::WorldObstacleResultAccepted,
        "complete empty obstacle result is usable free space");
    require(host.handle_client_message(obstacles.SerializeAsString(), 1) == ClientMessageResult::Rejected,
        "duplicate obstacle result cannot reuse accepted sequence");
    one_tick(ioc, host);
    require(host.state().position_enu.z > 1.2 && host.state().position_enu.z < 2.0
        && std::abs(host.state().position_enu.x) < .01 && std::abs(host.state().position_enu.y) < .01,
        "player uses live height and empty measured collision, not contradictory file world");
    require(!world(host).world_state().health().has_control_command(),
        "revision and measurement messages must not arm control lease");

    auto command = envelope(6);
    const auto control_time = SimulationHost::Clock::now() - std::chrono::milliseconds(50);
    command.mutable_control_command()->set_client_time_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
        control_time.time_since_epoch()).count());
    command.mutable_control_command()->set_mode(simcore::CONTROL_MODE_MANUAL);
    require(host.handle_client_message(command.SerializeAsString(), 1, control_time) == ClientMessageResult::ControlAccepted,
        "control establishes an independently aged lease");
    notice.set_sequence(7);
    require(host.handle_client_message(notice.SerializeAsString(), 1) == ClientMessageResult::GroundWorldRevisionAccepted,
        "same ready revision heartbeat accepted");
    require(world(host).world_state().health().last_command_age_ns() >= 50'000'000,
        "world heartbeat must not refresh an existing control lease");

    const auto stable = host.state();
    notice.set_sequence(8); notice.mutable_ground_world_revision()->set_world_revision(2);
    notice.mutable_ground_world_revision()->set_measurements_ready(false);
    require(host.handle_client_message(notice.SerializeAsString(), 1) == ClientMessageResult::GroundWorldRevisionAccepted,
        "unready world edit invalidates live caches immediately");
    one_tick(ioc, host);
    require(host.state().position_enu.z == stable.position_enu.z
        && !world(host).world_state().has_world_obstacles(), "unready revision freezes player without file fallback");
    auto old = obstacles; old.set_sequence(1000);
    require(host.handle_client_message(old.SerializeAsString(), 1) == ClientMessageResult::Rejected,
        "old obstacle revision cannot resurrect an invalidated world");
    notice.set_sequence(9); notice.mutable_ground_world_revision()->set_world_revision(1);
    notice.mutable_ground_world_revision()->set_measurements_ready(true);
    require(host.handle_client_message(notice.SerializeAsString(), 1) == ClientMessageResult::Rejected,
        "world revision cannot decrease");
    notice.mutable_ground_world_revision()->set_world_revision(3);
    require(host.handle_client_message(notice.SerializeAsString(), 1) == ClientMessageResult::GroundWorldRevisionAccepted,
        "new ready revision resumes fresh requests");
    const auto refreshed = world(host);
    require(refreshed.world_state().world_obstacles().world_revision() == 3
        && refreshed.world_state().live_ground_patch().world_revision() == 3
        && refreshed.world_state().world_obstacles().environment_epoch()
            > initial.world_state().world_obstacles().environment_epoch(),
        "world edit advances request epochs for both providers");
}
}

int main() {
    try {
        notice_wire(); separated_ground_payloads(); obstacle_wire(); simultaneous_world_requests();
        host_lifecycle_and_joint_readiness();
        std::cout << "live world protocol and host tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
