#include "protocol/vehicle_messages.hpp"

#include "vehicle.pb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

simcore_host::EnvelopeMetadata make_metadata()
{
    return {
        42,
        16'666'667,
        "test-cpp",
        "map-checksum",
        "play-session-test",
    };
}

void test_traffic_snapshot_is_additive_and_atomic()
{
    VehicleState state;
    state.entity_id = 1;
    simcore_host::TrafficSignalSnapshot signal;
    signal.id = 7;
    signal.group_id = 1;
    signal.aspect = simcore_host::SignalAspect::Green;
    signal.position_enu = {-8, 137, .16};
    signal.heading_deg = 90;
    signal.remaining_seconds = 11.5;
    const std::string checksum = "fnv1a64:0123456789abcdef";
    simcore::Envelope envelope;
    require(envelope.ParseFromString(simcore_host::serialize_world_state_envelope(
        state, {}, make_metadata(), simcore_host::HealthSnapshot{}, {signal}, checksum)),
        "traffic WorldState parses");
    const auto& world = envelope.world_state();
    require(world.entities_size() == 1 && world.has_health()
            && world.traffic_signals_size() == 1 && envelope.sequence() == 42,
            "traffic must share pose/health identity and sequence");
    require(world.traffic_network_checksum() == checksum
            && world.traffic_signals(0).signal_id() == 7
            && world.traffic_signals(0).controller_id() == 1
            && world.traffic_signals(0).signal_kind() == simcore::TRAFFIC_SIGNAL_KIND_VEHICLE
            && world.traffic_signals(0).aspect() == simcore::TRAFFIC_SIGNAL_GREEN
            && world.traffic_signals(0).position_enu().x() == -8
            && world.traffic_signals(0).remaining_seconds() == 11.5f,
            "signal wire semantics must roundtrip");
    auto pedestrian = signal;
    pedestrian.id = 101;
    pedestrian.kind = simcore_host::TrafficSignalKind::Pedestrian;
    simcore::Envelope pedestrian_wire;
    require(pedestrian_wire.ParseFromString(simcore_host::serialize_world_state_envelope(
                state, {}, make_metadata(), std::nullopt, {pedestrian}, checksum))
            && pedestrian_wire.world_state().traffic_signals(0).signal_kind()
                == simcore::TRAFFIC_SIGNAL_KIND_PEDESTRIAN,
            "pedestrian signal kind must roundtrip additively");
    for (const double heading : {0.0, 90.0, 359.999, 359.999999}) {
        auto boundary = signal;
        boundary.heading_deg = heading;
        simcore::Envelope rounded;
        require(rounded.ParseFromString(simcore_host::serialize_world_state_envelope(
            state, {}, make_metadata(), std::nullopt, {boundary}, checksum)),
            "heading boundary WorldState parses");
        const float wire = rounded.world_state().traffic_signals(0).heading_deg();
        require(wire >= 0.f && wire < 360.f,
                "float rounding must preserve the receiver's heading range");
        const double difference = std::abs(static_cast<double>(wire) - heading);
        require(std::min(difference, 360.0 - difference) < .0001,
                "wrapped heading must retain the same physical orientation");
    }
    for (int invalid = 0; invalid < 7; ++invalid) {
        auto bad = signal;
        auto hash = checksum;
        auto signals = std::vector<simcore_host::TrafficSignalSnapshot>{bad};
        if (invalid == 0) hash.clear();
        if (invalid == 1) signals.push_back(signal);
        if (invalid == 2) signals[0].remaining_seconds = std::numeric_limits<double>::quiet_NaN();
        if (invalid == 3) signals[0].aspect = static_cast<simcore_host::SignalAspect>(99);
        if (invalid == 4) signals[0].heading_deg = 360.0;
        if (invalid == 5) signals[0].heading_deg = -0.1;
        if (invalid == 6) signals[0].kind = static_cast<simcore_host::TrafficSignalKind>(99);
        bool rejected = false;
        try { (void)simcore_host::serialize_world_state_envelope(
            state, {}, make_metadata(), std::nullopt, signals, hash); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid traffic snapshot must not serialize");
    }

    const auto rejects = [&](std::vector<simcore_host::TrafficSignalSnapshot> signals,
                             const char* message) {
        bool rejected = false;
        try {
            (void)simcore_host::serialize_world_state_envelope(
                state, {}, make_metadata(), std::nullopt, signals, checksum);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, message);
    };
    const auto serializes = [&](std::vector<simcore_host::TrafficSignalSnapshot> signals,
                                const char* message) {
        simcore::Envelope parsed;
        require(parsed.ParseFromString(simcore_host::serialize_world_state_envelope(
                    state, {}, make_metadata(), std::nullopt, signals, checksum)), message);
        return parsed;
    };

    auto other_controller = signal;
    other_controller.id = 8;
    other_controller.controller_id = 2;
    other_controller.group_id = 2;
    const auto independent = serializes(
        {signal, other_controller},
        "different controllers may publish simultaneous permissive groups");
    require(independent.world_state().traffic_signals(0).controller_id() == 1
            && independent.world_state().traffic_signals(1).controller_id() == 2,
            "controller identity must roundtrip on every signal head");

    auto boundary = signal;
    boundary.id = 9;
    boundary.controller_id = 64;
    boundary.group_id = 4096;
    const auto upper_bounds = serializes(
        {boundary}, "maximum supported controller and group IDs must serialize");
    require(upper_bounds.world_state().traffic_signals(0).controller_id() == 64
            && upper_bounds.world_state().traffic_signals(0).group_id() == 4096,
            "controller/group upper bounds must survive protobuf serialization");

    auto long_plan_countdown = boundary;
    long_plan_countdown.remaining_seconds = simcore_host::kMaxTrafficSignalCountdownSeconds;
    const auto long_plan = serializes(
        {long_plan_countdown}, "maximum version 2 plan countdown must serialize");
    require(long_plan.world_state().traffic_signals(0).remaining_seconds() == 3600.0f,
            "one-hour plan countdown must survive protobuf serialization");
    long_plan_countdown.remaining_seconds =
        simcore_host::kMaxTrafficSignalCountdownSeconds + 0.01;
    rejects({long_plan_countdown}, "countdown beyond the plan-cycle bound must fail closed");

    auto same_controller_conflict = signal;
    same_controller_conflict.id = 10;
    same_controller_conflict.group_id = 2;
    same_controller_conflict.aspect = simcore_host::SignalAspect::Yellow;
    rejects({signal, same_controller_conflict},
            "one controller cannot publish two distinct permissive groups");

    auto same_group_conflict = signal;
    same_group_conflict.id = 11;
    same_group_conflict.aspect = simcore_host::SignalAspect::Red;
    rejects({signal, same_group_conflict},
            "heads in one controller/group must agree on aspect");
    same_group_conflict.aspect = signal.aspect;
    same_group_conflict.remaining_seconds += .01;
    rejects({signal, same_group_conflict},
            "heads in one controller/group must agree on countdown");

    auto independent_same_group = signal;
    independent_same_group.id = 12;
    independent_same_group.controller_id = 2;
    independent_same_group.aspect = simcore_host::SignalAspect::Red;
    independent_same_group.remaining_seconds += 1.0;
    (void)serializes({signal, independent_same_group},
                     "the same group ID in different controllers is independent");

    for (const std::uint32_t bad_group : {0u, 4097u}) {
        auto bad = signal;
        bad.group_id = bad_group;
        rejects({bad}, "out-of-range traffic group must fail closed");
    }
    for (const std::uint32_t bad_controller : {0u, 65u}) {
        auto bad = signal;
        bad.controller_id = bad_controller;
        rejects({bad}, "out-of-range traffic controller must fail closed");
    }
    auto duplicate_across_controllers = signal;
    duplicate_across_controllers.controller_id = 2;
    rejects({signal, duplicate_across_controllers},
            "signal IDs remain globally unique across controllers");
}

void test_world_state_envelope_roundtrip()
{
    static_assert(simcore_host::kProtocolSchemaVersion == 2);
    VehicleState state;
    state.entity_id = 7;
    state.timestamp = 123.5;
    state.lat = 40.7069;
    state.lon = -74.0095;
    state.alt = 12.0;
    state.heading = 91.f;
    state.speed = 8.5f;
    state.accel = 1.25f;
    state.east = 3.0;
    state.north = 4.0;
    state.yaw_rate = 0.2f;
    state.steering_angle = 0.3f;
    state.gear = VehicleGear::Drive;
    state.position_enu = {3.0, 4.0, 0.5};
    state.linear_velocity_body = {8.5, 0.2, 0.0};
    state.angular_velocity_body = {0.0, 0.0, 0.2};
    state.damage_percent = 37.5f;
    state.last_impact_impulse_n_s = 9125.f;
    state.damage_zone = VehicleDamageZone::Front;
    state.collision_event_sequence = 3;
    state.collision_half_length_m = 2.15f;
    state.collision_half_width_m = 0.94f;
    state.collision_half_height_m = 0.75f;
    state.wheels[0].wheel_index = 0;
    state.wheels[0].in_contact = true;
    state.wheels[0].normal_load = 3500.f;
    state.wheels[0].steering_angle = 0.3f;

    const auto bytes = simcore_host::serialize_world_state_envelope(
        state, make_metadata());

    simcore::Envelope envelope;
    require(envelope.ParseFromString(bytes), "Envelope must parse");
    require(envelope.schema_version() == simcore_host::kProtocolSchemaVersion,
            "schema version must be set");
    require(envelope.sequence() == 42, "sequence must roundtrip");
    require(envelope.simulation_time_ns() == 16'666'667,
            "simulation time must roundtrip");
    require(envelope.source_id() == "test-cpp", "source id must roundtrip");
    require(envelope.play_session_id() == "play-session-test",
            "WorldState envelope must echo its authoritative play session");
    require(envelope.has_world_state(), "payload must be WorldState");
    require(!envelope.world_state().has_health(),
            "legacy serializer callers must retain the optional Health absence");
    require(envelope.world_state().entities_size() == 1,
            "WorldState must contain one entity");

    const auto& entity = envelope.world_state().entities(0);
    require(entity.entity_id() == 7, "entity id must roundtrip");
    require(entity.entity_kind() == simcore::ENTITY_KIND_EGO_VEHICLE,
            "single-Ego compatibility path must classify the authoritative vehicle");
    require(std::abs(entity.speed() - 8.5f) < 1e-5f,
            "speed must roundtrip");
    require(entity.gear() == simcore::VEHICLE_GEAR_DRIVE,
            "gear must roundtrip");
    require(std::abs(entity.yaw_rate() - 0.2f) < 1e-6f
            && std::abs(entity.steering_angle() - 0.3f) < 1e-6f
            && std::abs(entity.angular_velocity_body().z() - 0.2) < 1e-9,
            "left-positive yaw and steering values must roundtrip exactly");
    require(entity.has_position_enu() && entity.position_enu().x() == 3.0,
            "3D ENU position must roundtrip");
    require(entity.wheels_size() == 4,
            "all four wheel states must roundtrip");
    require(entity.wheels(0).in_contact() && entity.wheels(0).normal_load() == 3500.f,
            "wheel contact state must roundtrip");
    require(std::abs(entity.wheels(0).steering_angle() - 0.3f) < 1e-6f,
            "left-positive wheel steering must roundtrip exactly");
    require(std::abs(entity.damage_percent() - 37.5f) < 1e-6f
            && std::abs(entity.last_impact_impulse_n_s() - 9125.f) < 1e-3f
            && entity.damage_zone() == simcore::VEHICLE_DAMAGE_ZONE_FRONT
            && entity.collision_event_sequence() == 3,
            "authoritative collision damage fields must roundtrip additively");
    require(entity.collision_half_length() == 2.15f
            && entity.collision_half_width() == 0.94f
            && entity.collision_half_height() == 0.75f,
            "Ego must publish the same chassis box used by the debug overlay");
}

void test_world_health_is_additive_and_atomic()
{
    using simcore_host::HealthStatus;
    constexpr std::array statuses{
        HealthStatus::AwaitingReset, HealthStatus::AwaitingControl,
        HealthStatus::Active, HealthStatus::SafeStop,
        HealthStatus::ReconnectRequired, HealthStatus::EstopLatched};
    constexpr std::array names{
        "awaiting_reset", "awaiting_control", "active", "safe_stop",
        "reconnect_required", "estop_latched"};
    VehicleState state;
    for (std::size_t index = 0; index < statuses.size(); ++index) {
        const simcore_host::HealthSnapshot health{
            statuses[index], std::numeric_limits<std::uint32_t>::max(),
            std::numeric_limits<std::uint64_t>::max(), "authority detail", true};
        simcore::Envelope envelope;
        require(envelope.ParseFromString(
                    simcore_host::serialize_world_state_envelope(
                        state, make_metadata(), health)),
                "world-health.v1 must parse without a schema bump");
        require(envelope.schema_version() == 2 && envelope.has_world_state()
                && !envelope.has_health() && envelope.sequence() == 42
                && envelope.world_state().entities_size() == 1,
                "Health must share the existing WorldState payload and sequence");
        require(envelope.world_state().has_health(),
                "a provided authority snapshot must be explicitly present");
        const auto& parsed = envelope.world_state().health();
        require(parsed.status() == names[index]
                && parsed.has_control_command()
                && parsed.tick_overrun_count()
                    == std::numeric_limits<std::uint32_t>::max()
                && parsed.last_command_age_ns()
                    == std::numeric_limits<std::uint64_t>::max()
                && parsed.message() == "authority detail",
                "all whitelisted states and full-width metrics must roundtrip");
    }

    simcore::Envelope no_command;
    require(no_command.ParseFromString(
                simcore_host::serialize_world_state_envelope(
                    state, {}, make_metadata(),
                    simcore_host::HealthSnapshot{
                        HealthStatus::AwaitingControl, 0, 999, {}, false}))
            && !no_command.world_state().health().has_control_command()
            && no_command.world_state().health().last_command_age_ns() == 0,
            "absence of an accepted command must encode age zero, never a sentinel");

    bool invalid_status_rejected = false;
    try {
        (void)simcore_host::serialize_world_state_envelope(
            state, make_metadata(),
            simcore_host::HealthSnapshot{static_cast<HealthStatus>(255)});
    } catch (const std::invalid_argument&) {
        invalid_status_rejected = true;
    }
    require(invalid_status_rejected,
            "server serialization must not emit an unknown authority status");
}

void test_control_command_envelope_maps_to_input()
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(100);
    envelope.set_simulation_time_ns(33'333'334);
    envelope.set_source_id("unreal");
    envelope.set_session_id("unreal-session-1");
    envelope.set_map_package_checksum("map-checksum");

    auto* command = envelope.mutable_control_command();
    command->set_mode(simcore::CONTROL_MODE_MANUAL);
    command->set_throttle(0.75f);
    command->set_brake(0.1f);
    command->set_steering(0.25f);
    command->set_handbrake(true);
    command->set_gear(simcore::VEHICLE_GEAR_REVERSE);
    command->set_client_time_ns(5'000'000'000);

    std::string error;
    const auto input = simcore_host::parse_control_command_envelope(
        envelope.SerializeAsString(), &error);

    require(input.has_value(), "ControlCommand must parse: " + error);
    require(input->sequence == 100 && input->source_id == "unreal" &&
            input->session_id == "unreal-session-1",
            "control envelope metadata must be exposed for ordering checks");
    require(std::abs(input->input.throttle - 0.75f) < 1e-5f,
            "throttle must map");
    require(std::abs(input->input.brake - 0.1f) < 1e-5f,
            "brake must map");
    require(std::abs(input->input.steering - 0.25f) < 1e-5f,
            "left-positive steering must map without a protocol-layer sign change");
    require(input->input.handbrake, "handbrake must map");
    require(input->input.gear == VehicleGear::Reverse, "gear must map");
}

void test_hello_envelope_roundtrip_and_capabilities()
{
    auto metadata = make_metadata();
    metadata.session_id = "server-session";
    const std::vector<std::string> capabilities{
        "world-state.v2",
        "control.v2",
        "simulation-reset.v1",
        "map-package-checksum.v1",
    };
    const auto bytes = simcore_host::serialize_hello_envelope(
        metadata, "simcore-test-build", capabilities);

    simcore::Envelope wire;
    require(wire.ParseFromString(bytes) && wire.has_hello(),
            "serialized Hello envelope must parse");
    require(wire.hello().schema() == simcore_host::kProtocolSchemaName
            && wire.hello().build() == "simcore-test-build"
            && wire.hello().capabilities_size() == 4,
            "Hello must advertise its exact schema, build, and capabilities");

    std::string error;
    const auto parsed = simcore_host::parse_client_message_envelope(bytes, &error);
    require(parsed.has_value(), "Hello must parse: " + error);
    const auto* hello = std::get_if<simcore_host::ParsedHello>(&*parsed);
    require(hello != nullptr
            && hello->sequence == metadata.sequence
            && hello->session_id == "server-session"
            && hello->map_package_checksum == metadata.map_package_checksum
            && hello->schema == simcore_host::kProtocolSchemaName
            && hello->capabilities == capabilities,
            "parsed Hello must retain envelope identity and ordered capabilities");
}

void test_runtime_entities_are_additive_and_deterministically_ordered()
{
    VehicleState ego;
    ego.entity_id = 1;
    ego.timestamp = 20.0;

    simcore_host::RuntimeEntityState pedestrian{
        2001,
        simcore_host::RuntimeEntityKind::Pedestrian,
        {
            "ped-2001",
            simcore_host::VerticalCapsule{{-4.0, 7.0}, 1.4, 0.35, 0.9},
            {0.0, -0.5},
            0.0,
            {0.7, 0.0},
        },
    };
    simcore_host::RuntimeEntityState npc{
        1001,
        simcore_host::RuntimeEntityKind::NpcVehicle,
        {
            "npc-1001",
            simcore_host::ObbPrism{
                {2.0, 3.0}, 1.0, std::numbers::pi / 2.0, 2.2, 1.0, 0.75},
            {2.0, 1.0},
            0.1,
            {0.9, 0.0},
        },
    };

    const auto bytes = simcore_host::serialize_world_state_envelope(
        ego,
        {pedestrian, npc},
        make_metadata());
    simcore::Envelope envelope;
    require(envelope.ParseFromString(bytes),
            "multi-entity WorldState must parse");
    require(envelope.world_state().entities_size() == 3,
            "runtime entities must be additive to Ego");
    const auto& wire_ego = envelope.world_state().entities(0);
    const auto& wire_npc = envelope.world_state().entities(1);
    const auto& wire_pedestrian = envelope.world_state().entities(2);
    require(wire_ego.entity_id() == 1
            && wire_npc.entity_id() == 1001
            && wire_pedestrian.entity_id() == 2001,
            "WorldState order must be Ego then ascending runtime entity ID");
    require(wire_npc.entity_kind() == simcore::ENTITY_KIND_NPC_VEHICLE
            && wire_npc.collision_half_length() == 2.2f
            && wire_npc.collision_half_width() == 1.0f
            && wire_npc.collision_half_height() == 0.75f,
            "NPC OBB metadata must roundtrip");
    require(std::abs(wire_npc.heading() - 90.0f) < 1e-5f
            && std::abs(wire_npc.linear_velocity_enu().x() - 2.0) < 1e-9
            && std::abs(wire_npc.linear_velocity_enu().y() - 1.0) < 1e-9
            && std::abs(wire_npc.linear_velocity_body().x() - 2.0) < 1e-9
            && std::abs(wire_npc.linear_velocity_body().y() - 1.0) < 1e-9
            && std::abs(wire_npc.yaw_rate() + 0.1f) < 1e-6f,
            "NPC ENU/body velocity and yaw signs must follow the shared frame contract");
    require(wire_pedestrian.entity_kind() == simcore::ENTITY_KIND_PEDESTRIAN
            && wire_pedestrian.collision_radius() == 0.35f
            && wire_pedestrian.collision_half_height() == 0.9f,
            "pedestrian capsule metadata must roundtrip");

    bool duplicate_rejected = false;
    try {
        (void)simcore_host::serialize_world_state_envelope(
            ego, {npc, npc}, make_metadata());
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "duplicate runtime entity IDs must fail closed at serialization");
}

void test_simulation_reset_envelope_maps_to_lifecycle_request()
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(101);
    envelope.set_source_id("unreal");
    envelope.set_session_id("socket-session-2");
    envelope.set_map_package_checksum("map-checksum");

    auto* reset = envelope.mutable_simulation_reset();
    reset->set_play_session_id("pie-run-42");
    reset->set_client_time_ns(5'100'000'000);

    std::string error;
    const auto message = simcore_host::parse_client_message_envelope(
        envelope.SerializeAsString(), &error);

    require(message.has_value(), "SimulationReset must parse: " + error);
    const auto* parsed = std::get_if<simcore_host::ParsedSimulationReset>(
        &*message);
    require(parsed != nullptr,
            "SimulationReset must retain its lifecycle payload type");
    require(parsed->sequence == 101
            && parsed->source_id == "unreal"
            && parsed->session_id == "socket-session-2",
            "reset envelope metadata must be exposed for validation");
    require(parsed->map_package_checksum == "map-checksum",
            "reset must carry the same map identity as control");
    require(parsed->play_session_id == "pie-run-42"
            && parsed->client_time_ns == 5'100'000'000,
            "reset must expose the stable PIE identity and client time");
}

void test_rejects_incompatible_schema_control_commands()
{
    constexpr std::array<std::uint32_t, 2> incompatible_versions{1u, 3u};
    for (const std::uint32_t version : incompatible_versions) {
        simcore::Envelope envelope;
        envelope.set_schema_version(version);
        envelope.mutable_control_command()->set_steering(0.25f);

        std::string error;
        const auto input = simcore_host::parse_control_command_envelope(
            envelope.SerializeAsString(), &error);

        require(!input.has_value(),
                "non-v2 controls must not enter the v2 physics contract");
        require(error.find("schema version") != std::string::npos,
                "schema rejection must identify the incompatible version boundary");
    }
}

void test_missing_gear_keeps_default_drive()
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.mutable_control_command()->set_throttle(0.5f);

    std::string error;
    const auto input = simcore_host::parse_control_command_envelope(
        envelope.SerializeAsString(), &error);

    require(input.has_value(), "ControlCommand must parse: " + error);
    require(input->input.gear == VehicleGear::Drive,
            "missing gear must keep VehicleInput default Drive");
}

void test_estop_maps_to_safe_brake()
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    auto* command = envelope.mutable_control_command();
    command->set_mode(simcore::CONTROL_MODE_ESTOP);
    command->set_throttle(1.0f);

    std::string error;
    const auto input = simcore_host::parse_control_command_envelope(
        envelope.SerializeAsString(), &error);

    require(input.has_value(), "E-stop command must parse: " + error);
    require(input->input.throttle == 0.f, "E-stop must clear throttle");
    require(input->input.brake == 1.f, "E-stop must apply full brake");
    require(input->input.handbrake, "E-stop must apply handbrake");
}

void test_rejects_non_envelope_payload()
{
    std::string error;
    const auto input = simcore_host::parse_control_command_envelope(
        "{\"throttle\":1.0}", &error);

    require(!input.has_value(), "JSON text must not be accepted");
    require(!error.empty(), "parse failure must explain why");
}

} // namespace

int main()
{
    try {
        test_world_state_envelope_roundtrip();
        test_traffic_snapshot_is_additive_and_atomic();
        test_world_health_is_additive_and_atomic();
        test_runtime_entities_are_additive_and_deterministically_ordered();
        test_control_command_envelope_maps_to_input();
        test_hello_envelope_roundtrip_and_capabilities();
        test_simulation_reset_envelope_maps_to_lifecycle_request();
        test_rejects_incompatible_schema_control_commands();
        test_missing_gear_keeps_default_drive();
        test_estop_maps_to_safe_brake();
        test_rejects_non_envelope_payload();
        std::cout << "vehicle_protocol_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vehicle_protocol_tests: " << error.what() << '\n';
        return 1;
    }
}
