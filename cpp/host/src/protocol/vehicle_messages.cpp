#include "protocol/vehicle_messages.hpp"

#include "vehicle.pb.h"

#include <limits>

namespace simcore_host {
namespace {

simcore::VehicleGear to_proto_gear(VehicleGear gear)
{
    switch (gear) {
    case VehicleGear::Neutral: return simcore::VEHICLE_GEAR_NEUTRAL;
    case VehicleGear::Drive:   return simcore::VEHICLE_GEAR_DRIVE;
    case VehicleGear::Reverse: return simcore::VEHICLE_GEAR_REVERSE;
    }
    return simcore::VEHICLE_GEAR_NEUTRAL;
}

VehicleGear to_host_gear(simcore::VehicleGear gear)
{
    switch (gear) {
    case simcore::VEHICLE_GEAR_NEUTRAL: return VehicleGear::Neutral;
    case simcore::VEHICLE_GEAR_DRIVE:   return VehicleGear::Drive;
    case simcore::VEHICLE_GEAR_REVERSE: return VehicleGear::Reverse;
    default:                            return VehicleGear::Neutral;
    }
}

void fill_entity_state(simcore::EntityState& entity, const VehicleState& state)
{
    entity.set_entity_id(state.entity_id);
    entity.set_timestamp(state.timestamp);
    entity.set_lat(state.lat);
    entity.set_lon(state.lon);
    entity.set_alt(state.alt);
    entity.set_heading(state.heading);
    entity.set_pitch(state.pitch);
    entity.set_roll(state.roll);
    entity.set_speed(state.speed);
    entity.set_accel(state.accel);
    entity.set_fuel(state.fuel);
    entity.set_rpm(state.rpm);
    entity.set_east(state.east);
    entity.set_north(state.north);
    entity.set_yaw_rate(state.yaw_rate);
    entity.set_steering_angle(state.steering_angle);
    entity.set_gear(to_proto_gear(state.gear));
}

void fill_envelope(simcore::Envelope& envelope, const EnvelopeMetadata& metadata)
{
    envelope.set_schema_version(kProtocolSchemaVersion);
    envelope.set_sequence(metadata.sequence);
    envelope.set_simulation_time_ns(metadata.simulation_time_ns);
    envelope.set_source_id(std::string(metadata.source_id));
    envelope.set_map_package_checksum(std::string(metadata.map_package_checksum));
}

void set_error(std::string* error, std::string_view message)
{
    if (error) {
        *error = std::string(message);
    }
}

} // namespace

std::string serialize_entity_state_packet(const VehicleState& state)
{
    simcore::EntityStatePacket packet;
    fill_entity_state(*packet.add_entities(), state);
    return packet.SerializeAsString();
}

std::string serialize_world_state_envelope(const VehicleState& state,
                                           const EnvelopeMetadata& metadata)
{
    simcore::Envelope envelope;
    fill_envelope(envelope, metadata);
    fill_entity_state(*envelope.mutable_world_state()->add_entities(), state);
    return envelope.SerializeAsString();
}

std::optional<VehicleInput> parse_control_command_envelope(
    std::string_view data,
    std::string* error)
{
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        set_error(error, "control packet is too large");
        return std::nullopt;
    }

    simcore::Envelope envelope;
    if (!envelope.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        set_error(error, "failed to parse protobuf Envelope");
        return std::nullopt;
    }

    if (envelope.schema_version() != kProtocolSchemaVersion) {
        set_error(error, "unsupported protobuf schema version");
        return std::nullopt;
    }

    if (!envelope.has_control_command()) {
        set_error(error, "Envelope payload is not ControlCommand");
        return std::nullopt;
    }

    const auto& command = envelope.control_command();

    VehicleInput input;
    if (command.estop() || command.mode() == simcore::CONTROL_MODE_ESTOP) {
        input.throttle = 0.f;
        input.brake = 1.f;
        input.steering = 0.f;
        input.handbrake = true;
        input.gear = VehicleGear::Drive;
        return input;
    }

    input.throttle = command.throttle();
    input.brake = command.brake();
    input.steering = command.steering();
    input.handbrake = command.handbrake();
    if (command.has_gear()) {
        input.gear = to_host_gear(command.gear());
    }

    return input;
}

} // namespace simcore_host
