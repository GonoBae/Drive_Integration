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

    auto fill_vector = [](simcore::Vector3d* target, const Vector3State& source) {
        target->set_x(source.x);
        target->set_y(source.y);
        target->set_z(source.z);
    };
    fill_vector(entity.mutable_position_enu(), state.position_enu);
    fill_vector(entity.mutable_linear_velocity_body(), state.linear_velocity_body);
    fill_vector(entity.mutable_angular_velocity_body(), state.angular_velocity_body);
    for (const auto& source_wheel : state.wheels) {
        auto* wheel = entity.add_wheels();
        wheel->set_wheel_index(source_wheel.wheel_index);
        wheel->set_in_contact(source_wheel.in_contact);
        wheel->set_steering_angle(source_wheel.steering_angle);
        wheel->set_angular_speed(source_wheel.angular_speed);
        wheel->set_normal_load(source_wheel.normal_load);
        wheel->set_longitudinal_slip(source_wheel.longitudinal_slip);
        wheel->set_slip_angle(source_wheel.slip_angle);
        wheel->set_longitudinal_force(source_wheel.longitudinal_force);
        wheel->set_lateral_force(source_wheel.lateral_force);
        fill_vector(wheel->mutable_contact_point_enu(), source_wheel.contact_point_enu);
        fill_vector(wheel->mutable_contact_normal_enu(), source_wheel.contact_normal_enu);
    }
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

std::optional<ParsedControlCommand> parse_control_command_envelope(
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

    ParsedControlCommand parsed;
    parsed.sequence = envelope.sequence();
    parsed.source_id = envelope.source_id();
    parsed.session_id = envelope.session_id();
    parsed.map_package_checksum = envelope.map_package_checksum();
    parsed.client_time_ns = command.client_time_ns();
    parsed.estop = command.estop() || command.mode() == simcore::CONTROL_MODE_ESTOP;
    VehicleInput& input = parsed.input;
    if (parsed.estop) {
        input.throttle = 0.f;
        input.brake = 1.f;
        input.steering = 0.f;
        input.handbrake = true;
        input.gear = VehicleGear::Drive;
        return parsed;
    }

    input.throttle = command.throttle();
    input.brake = command.brake();
    input.steering = command.steering();
    input.handbrake = command.handbrake();
    if (command.has_gear()) {
        input.gear = to_host_gear(command.gear());
    }

    return parsed;
}

} // namespace simcore_host
