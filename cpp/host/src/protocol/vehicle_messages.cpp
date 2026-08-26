#include "protocol/vehicle_messages.hpp"

#include "vehicle.pb.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <type_traits>

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
    entity.set_entity_kind(simcore::ENTITY_KIND_EGO_VEHICLE);

    auto fill_vector = [](simcore::Vector3d* target, const Vector3State& source) {
        target->set_x(source.x);
        target->set_y(source.y);
        target->set_z(source.z);
    };
    fill_vector(entity.mutable_position_enu(), state.position_enu);
    fill_vector(entity.mutable_linear_velocity_body(), state.linear_velocity_body);
    fill_vector(entity.mutable_angular_velocity_body(), state.angular_velocity_body);
    const double heading_rad = static_cast<double>(state.heading)
        * std::numbers::pi / 180.0;
    const double sin_heading = std::sin(heading_rad);
    const double cos_heading = std::cos(heading_rad);
    auto* velocity_enu = entity.mutable_linear_velocity_enu();
    velocity_enu->set_x(
        state.linear_velocity_body.x * sin_heading
        - state.linear_velocity_body.y * cos_heading);
    velocity_enu->set_y(
        state.linear_velocity_body.x * cos_heading
        + state.linear_velocity_body.y * sin_heading);
    velocity_enu->set_z(state.linear_velocity_body.z);
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
    envelope.set_play_session_id(std::string(metadata.play_session_id));
}

simcore::EntityKind to_proto_kind(RuntimeEntityKind kind)
{
    switch (kind) {
    case RuntimeEntityKind::NpcVehicle:
        return simcore::ENTITY_KIND_NPC_VEHICLE;
    case RuntimeEntityKind::Pedestrian:
        return simcore::ENTITY_KIND_PEDESTRIAN;
    }
    return simcore::ENTITY_KIND_UNSPECIFIED;
}

double normalize_heading(double heading_rad)
{
    constexpr double full_turn = std::numbers::pi * 2.0;
    heading_rad = std::fmod(heading_rad, full_turn);
    return heading_rad < 0.0 ? heading_rad + full_turn : heading_rad;
}

void fill_vector(simcore::Vector3d* target, const Vector3State& source)
{
    target->set_x(source.x);
    target->set_y(source.y);
    target->set_z(source.z);
}

void fill_vector(simcore::Vector3d* target,
                 const simcore_host::CollisionVector2& source,
                 double up = 0.0)
{
    target->set_x(source.east_m);
    target->set_y(source.north_m);
    target->set_z(up);
}

void fill_runtime_entity_state(simcore::EntityState& entity,
                               const RuntimeEntityState& state,
                               double timestamp)
{
    entity.set_entity_id(state.entity_id);
    entity.set_timestamp(timestamp);
    entity.set_entity_kind(to_proto_kind(state.kind));
    entity.set_gear(simcore::VEHICLE_GEAR_NEUTRAL);

    const KinematicCollisionProxy& proxy = state.collision_proxy;
    double heading_rad = 0.0;
    double center_up_m = 0.0;
    CollisionVector2 center;
    std::visit(
        [&](const auto& shape) {
            using Shape = std::decay_t<decltype(shape)>;
            center = shape.center_enu;
            center_up_m = shape.center_up_m;
            if constexpr (std::is_same_v<Shape, ObbPrism>) {
                heading_rad = normalize_heading(shape.heading_rad);
                entity.set_collision_half_length(
                    static_cast<float>(shape.half_length_m));
                entity.set_collision_half_width(
                    static_cast<float>(shape.half_width_m));
                entity.set_collision_half_height(
                    static_cast<float>(shape.half_height_m));
            } else {
                entity.set_collision_radius(
                    static_cast<float>(shape.radius_m));
                entity.set_collision_half_height(
                    static_cast<float>(shape.half_height_m));
            }
        },
        proxy.shape);

    entity.set_east(center.east_m);
    entity.set_north(center.north_m);
    entity.set_alt(center_up_m);
    entity.set_heading(static_cast<float>(
        heading_rad * 180.0 / std::numbers::pi));
    entity.set_speed(static_cast<float>(std::hypot(
        proxy.linear_velocity_enu_mps.east_m,
        proxy.linear_velocity_enu_mps.north_m)));
    // Protocol body yaw is FLU left-positive, while collision heading rate is
    // navigation clockwise-positive.
    entity.set_yaw_rate(static_cast<float>(-proxy.heading_rate_rad_s));
    fill_vector(entity.mutable_position_enu(), center, center_up_m);
    fill_vector(
        entity.mutable_linear_velocity_enu(),
        proxy.linear_velocity_enu_mps);

    const double sin_heading = std::sin(heading_rad);
    const double cos_heading = std::cos(heading_rad);
    auto* body_velocity = entity.mutable_linear_velocity_body();
    body_velocity->set_x(
        proxy.linear_velocity_enu_mps.east_m * sin_heading
        + proxy.linear_velocity_enu_mps.north_m * cos_heading);
    body_velocity->set_y(
        -proxy.linear_velocity_enu_mps.east_m * cos_heading
        + proxy.linear_velocity_enu_mps.north_m * sin_heading);
    body_velocity->set_z(0.0);
    auto* body_angular_velocity = entity.mutable_angular_velocity_body();
    body_angular_velocity->set_z(-proxy.heading_rate_rad_s);
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
    return serialize_world_state_envelope(state, {}, metadata);
}

std::string serialize_world_state_envelope(
    const VehicleState& state,
    const std::vector<RuntimeEntityState>& runtime_entities,
    const EnvelopeMetadata& metadata)
{
    simcore::Envelope envelope;
    fill_envelope(envelope, metadata);
    auto* world_state = envelope.mutable_world_state();
    fill_entity_state(*world_state->add_entities(), state);

    std::vector<const RuntimeEntityState*> ordered_entities;
    ordered_entities.reserve(runtime_entities.size());
    for (const auto& entity : runtime_entities) {
        if (entity.entity_id == 0 || entity.entity_id == state.entity_id) {
            throw std::invalid_argument(
                "runtime entity ID must be non-zero and distinct from Ego");
        }
        ordered_entities.push_back(&entity);
    }
    std::sort(
        ordered_entities.begin(),
        ordered_entities.end(),
        [](const RuntimeEntityState* lhs, const RuntimeEntityState* rhs) {
            return lhs->entity_id < rhs->entity_id;
        });
    for (std::size_t index = 1; index < ordered_entities.size(); ++index) {
        if (ordered_entities[index - 1]->entity_id
            == ordered_entities[index]->entity_id) {
            throw std::invalid_argument("runtime entity IDs must be unique");
        }
    }
    for (const RuntimeEntityState* entity : ordered_entities) {
        fill_runtime_entity_state(
            *world_state->add_entities(), *entity, state.timestamp);
    }
    return envelope.SerializeAsString();
}

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
