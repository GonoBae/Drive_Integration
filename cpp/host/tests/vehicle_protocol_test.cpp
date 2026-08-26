#include "protocol/vehicle_messages.hpp"

#include "vehicle.pb.h"

#include <array>
#include <cmath>
#include <iostream>
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
        test_runtime_entities_are_additive_and_deterministically_ordered();
        test_control_command_envelope_maps_to_input();
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
