#include "protocol/vehicle_messages.hpp"

#include "vehicle.pb.h"

#include <array>
#include <cmath>
#include <iostream>
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
    require(envelope.has_world_state(), "payload must be WorldState");
    require(envelope.world_state().entities_size() == 1,
            "WorldState must contain one entity");

    const auto& entity = envelope.world_state().entities(0);
    require(entity.entity_id() == 7, "entity id must roundtrip");
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
        test_control_command_envelope_maps_to_input();
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
