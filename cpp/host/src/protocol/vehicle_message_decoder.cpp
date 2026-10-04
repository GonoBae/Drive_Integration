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
        const auto requested_class = static_cast<unsigned>(
            reset.requested_vehicle_class());
        if (requested_class
            > static_cast<unsigned>(RuntimeVehicleClass::Motorcycle)) {
            set_error(error, "SimulationReset has an invalid requested vehicle class");
            return std::nullopt;
        }
        parsed.requested_vehicle_class =
            static_cast<RuntimeVehicleClass>(requested_class);
        parsed.requested_loadout_id = reset.requested_loadout_id();
        if (!valid_vehicle_loadout_id(parsed.requested_loadout_id)) {
            set_error(error, "SimulationReset has an invalid loadout ID");
            return std::nullopt;
        }
        return ParsedClientMessage{std::move(parsed)};
    }

    if (envelope.has_world_obstacle_result()) {
        const auto& wire=envelope.world_obstacle_result();
        if (!wire.request_id() || !wire.environment_epoch() || !wire.world_revision()
            || wire.status()<1 || wire.status()>3 || wire.colliders_size()>kMaximumLiveObstacleColliders
            || (wire.status()!=1 && wire.colliders_size()!=0)) return std::nullopt;
        ParsedWorldObstacleResult parsed{envelope.sequence(),envelope.source_id(),envelope.session_id(),
            envelope.play_session_id(),envelope.map_package_checksum(),
            {wire.request_id(),wire.environment_epoch(),wire.reference_simulation_time_ns(),wire.world_revision(),
                static_cast<LiveObstacleStatus>(wire.status()),{}}};
        for (const auto& c:wire.colliders()) {
            if (c.semantic()>2 || c.collider_id().empty() || c.collider_id().size()>128) return std::nullopt;
            parsed.result.colliders.push_back({c.collider_id(),static_cast<StaticColliderSemantic>(c.semantic()),
                {{c.centre_enu().x(),c.centre_enu().y()},c.centre_enu().z(),c.heading_rad(),
                    c.half_extent_enu().x(),c.half_extent_enu().y(),c.half_extent_enu().z()},
                {c.friction(),c.restitution()}});
        }
        return ParsedClientMessage{std::move(parsed)};
    }
    if (envelope.has_ground_world_revision()) {
        const auto& wire = envelope.ground_world_revision();
        if (!wire.world_revision()) { set_error(error,"Zero world revision"); return std::nullopt; }
        return ParsedClientMessage{ParsedGroundWorldRevision{envelope.sequence(),wire.world_revision(),
            envelope.source_id(),envelope.session_id(),envelope.play_session_id(),envelope.map_package_checksum(),
            wire.measurements_ready()}};
    }
    if (envelope.has_ground_query_result() || envelope.has_live_ground_patch_result() || envelope.has_npc_ground_tile_result()) {
        const bool npc=envelope.has_npc_ground_tile_result();
        const bool live = envelope.has_live_ground_patch_result() || npc;
        const auto& wire = npc ? envelope.npc_ground_tile_result() : live ? envelope.live_ground_patch_result() : envelope.ground_query_result();
        const auto limit = live ? kGroundPatchSamples : kMaxCapturedGroundProbes;
        if (wire.probes_size() > limit || (live && wire.probes_size() != kGroundPatchSamples)) {
            set_error(error, "Ground query result has invalid probe count");
            return std::nullopt;
        }
        if (wire.boundaries_size() > kMaxGroundCellBoundaries
            || (!live && wire.boundaries_size() != 0)) {
            set_error(error, "Ground query result has invalid boundary count");
            return std::nullopt;
        }
        ParsedGroundQueryResult parsed;
        parsed.live_patch = live;
        parsed.npc_tile = npc;
        parsed.sequence = envelope.sequence();
        parsed.source_id = envelope.source_id();
        parsed.session_id = envelope.session_id();
        parsed.play_session_id = envelope.play_session_id();
        parsed.map_package_checksum = envelope.map_package_checksum();
        parsed.result.request_id = wire.request_id();
        parsed.result.environment_epoch = wire.environment_epoch();
        parsed.result.reference_simulation_time_ns = wire.reference_simulation_time_ns();
        parsed.result.measurement_duration_ms = wire.measurement_duration_ms();
        parsed.result.world_revision = wire.world_revision();
        parsed.result.probes.reserve(wire.probes_size());
        for (const auto& p : wire.probes()) {
            if (p.status() < 1 || p.status() > 3) return std::nullopt;
            parsed.result.probes.push_back({static_cast<GroundProbeStatus>(p.status()),
                {p.point_enu().x(),p.point_enu().y(),p.point_enu().z()},
                {p.normal_enu().x(),p.normal_enu().y(),p.normal_enu().z()}, p.surface_material_id()});
        }
        parsed.result.boundaries.reserve(wire.boundaries_size());
        for (const auto& b : wire.boundaries()) {
            parsed.result.boundaries.push_back({b.cell_index(),
                {b.point_enu().x(),b.point_enu().y(),b.point_enu().z()},
                {b.normal_enu().x(),b.normal_enu().y(),b.normal_enu().z()}});
        }
        if (!GroundQueryShadow::valid_result(parsed.result, limit, live)) {
            set_error(error, "Invalid ground query result");
            return std::nullopt;
        }
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
