#include "protocol/vehicle_messages.hpp"

#include "vehicle.pb.h"

#include <limits>
#include <utility>

namespace simcore_host {
namespace {

VehicleGear to_host_gear(simcore::VehicleGear gear)
{
    switch (gear) {
    case simcore::VEHICLE_GEAR_NEUTRAL: return VehicleGear::Neutral;
    case simcore::VEHICLE_GEAR_DRIVE:   return VehicleGear::Drive;
    case simcore::VEHICLE_GEAR_REVERSE: return VehicleGear::Reverse;
    default:                            return VehicleGear::Neutral;
    }
}

void set_error(std::string* error, std::string_view message)
{
    if (error) {
        *error = std::string(message);
    }
}

} // namespace

std::optional<ParsedClientMessage> parse_client_message_envelope(
    std::string_view data,
    std::string* error)
{
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        set_error(error, "client packet is too large");
        return std::nullopt;
    }

    simcore::Envelope envelope;
    if (!envelope.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        set_error(error, "failed to parse protobuf Envelope");
        return std::nullopt;
    }

    if (envelope.schema_version() != kProtocolSchemaVersion) {
        const std::string message =
            "unsupported protobuf schema version: expected "
            + std::to_string(kProtocolSchemaVersion)
            + ", got " + std::to_string(envelope.schema_version());
        set_error(error, message);
        return std::nullopt;
    }

    if (envelope.has_control_command()) {
        const auto& command = envelope.control_command();

        ParsedControlCommand parsed;
        parsed.sequence = envelope.sequence();
        parsed.source_id = envelope.source_id();
        parsed.session_id = envelope.session_id();
        parsed.map_package_checksum = envelope.map_package_checksum();
        parsed.client_time_ns = command.client_time_ns();
        parsed.estop = command.estop()
            || command.mode() == simcore::CONTROL_MODE_ESTOP;
        VehicleInput& input = parsed.input;
        if (parsed.estop) {
            input.throttle = 0.f;
            input.brake = 1.f;
            input.steering = 0.f;
            input.handbrake = true;
            input.gear = VehicleGear::Drive;
            return ParsedClientMessage{std::move(parsed)};
        }

        input.throttle = command.throttle();
        input.brake = command.brake();
        input.steering = command.steering();
        input.handbrake = command.handbrake();
        if (command.has_gear()) {
            input.gear = to_host_gear(command.gear());
        }

        return ParsedClientMessage{std::move(parsed)};
    }

    if (envelope.has_hello()) {
        const auto& hello = envelope.hello();
        ParsedHello parsed;
        parsed.sequence = envelope.sequence();
        parsed.source_id = envelope.source_id();
        parsed.session_id = envelope.session_id();
        parsed.map_package_checksum = envelope.map_package_checksum();
        parsed.build = hello.build();
        parsed.schema = hello.schema();
        parsed.capabilities.assign(
            hello.capabilities().begin(), hello.capabilities().end());
        return ParsedClientMessage{std::move(parsed)};
    }

    if (envelope.has_simulation_reset()) {
        const auto& reset = envelope.simulation_reset();
        ParsedSimulationReset parsed;
        parsed.sequence = envelope.sequence();
        parsed.client_time_ns = reset.client_time_ns();
        parsed.source_id = envelope.source_id();
        parsed.session_id = envelope.session_id();
        parsed.map_package_checksum = envelope.map_package_checksum();
        parsed.play_session_id = reset.play_session_id();
        return ParsedClientMessage{std::move(parsed)};
    }

    set_error(error, "Envelope payload is not a supported client message");
    return std::nullopt;
}

std::optional<ParsedControlCommand> parse_control_command_envelope(
    std::string_view data,
    std::string* error)
{
    auto message = parse_client_message_envelope(data, error);
    if (!message) {
        return std::nullopt;
    }
    if (auto* command = std::get_if<ParsedControlCommand>(&*message)) {
        return std::move(*command);
    }
    set_error(error, "Envelope payload is not ControlCommand");
    return std::nullopt;
}

} // namespace simcore_host
