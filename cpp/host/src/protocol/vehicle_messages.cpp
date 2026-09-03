#include "protocol/vehicle_messages.hpp"

#include "vehicle.pb.h"

#include <algorithm>
#include <cmath>
#include <map>
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

simcore::VehicleDamageZone to_proto_damage_zone(VehicleDamageZone zone)
{
    switch (zone) {
    case VehicleDamageZone::None: return simcore::VEHICLE_DAMAGE_ZONE_NONE;
    case VehicleDamageZone::Front: return simcore::VEHICLE_DAMAGE_ZONE_FRONT;
    case VehicleDamageZone::Rear: return simcore::VEHICLE_DAMAGE_ZONE_REAR;
    case VehicleDamageZone::Left: return simcore::VEHICLE_DAMAGE_ZONE_LEFT;
    case VehicleDamageZone::Right: return simcore::VEHICLE_DAMAGE_ZONE_RIGHT;
    case VehicleDamageZone::Roof: return simcore::VEHICLE_DAMAGE_ZONE_ROOF;
    case VehicleDamageZone::Underbody:
        return simcore::VEHICLE_DAMAGE_ZONE_UNDERBODY;
    }
    return simcore::VEHICLE_DAMAGE_ZONE_NONE;
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

    entity.set_damage_percent(state.damage_percent);
    entity.set_last_impact_impulse_n_s(state.last_impact_impulse_n_s);
    entity.set_damage_zone(to_proto_damage_zone(state.damage_zone));
    entity.set_collision_event_sequence(state.collision_event_sequence);
    entity.set_collision_half_length(state.collision_half_length_m);
    entity.set_collision_half_width(state.collision_half_width_m);
    entity.set_collision_half_height(state.collision_half_height_m);

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
    envelope.set_session_id(std::string(metadata.session_id));
}

const char* health_status_name(HealthStatus status)
{
    switch (status) {
    case HealthStatus::AwaitingReset: return "awaiting_reset";
    case HealthStatus::AwaitingControl: return "awaiting_control";
    case HealthStatus::Active: return "active";
    case HealthStatus::SafeStop: return "safe_stop";
    case HealthStatus::ReconnectRequired: return "reconnect_required";
    case HealthStatus::EstopLatched: return "estop_latched";
    }
    throw std::invalid_argument("unrecognized authoritative Health status");
}

void fill_health(simcore::Health& health, const HealthSnapshot& snapshot)
{
    health.set_status(health_status_name(snapshot.status));
    health.set_tick_overrun_count(snapshot.tick_overrun_count);
    health.set_last_command_age_ns(
        snapshot.has_control_command ? snapshot.last_command_age_ns : 0);
    health.set_message(std::string(snapshot.message));
    health.set_has_control_command(snapshot.has_control_command);
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

} // namespace

std::string serialize_entity_state_packet(const VehicleState& state)
{
    simcore::EntityStatePacket packet;
    fill_entity_state(*packet.add_entities(), state);
    return packet.SerializeAsString();
}

std::string serialize_world_state_envelope(const VehicleState& state,
                                           const EnvelopeMetadata& metadata,
                                           std::optional<HealthSnapshot> health)
{
    return serialize_world_state_envelope(state, {}, metadata, health);
}

std::string serialize_world_state_envelope(
    const VehicleState& state,
    const std::vector<RuntimeEntityState>& runtime_entities,
    const EnvelopeMetadata& metadata,
    std::optional<HealthSnapshot> health,
    const std::vector<TrafficSignalSnapshot>& traffic_signals,
    std::string_view traffic_network_checksum)
{
    simcore::Envelope envelope;
    fill_envelope(envelope, metadata);
    auto* world_state = envelope.mutable_world_state();
    fill_entity_state(*world_state->add_entities(), state);
    if (health) {
        fill_health(*world_state->mutable_health(), *health);
    }
    if (!traffic_signals.empty()) {
        if (traffic_signals.size() > 32 || traffic_network_checksum.size() != 24
            || !traffic_network_checksum.starts_with("fnv1a64:")
            || !std::all_of(traffic_network_checksum.begin() + 8,
                            traffic_network_checksum.end(), [](char c) {
                                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                            })) {
            throw std::invalid_argument("traffic snapshot needs <=32 heads and verified checksum");
        }
        world_state->set_traffic_network_checksum(std::string(traffic_network_checksum));
        std::vector<std::uint32_t> ids;
        std::map<std::uint32_t, std::uint32_t> permitted_groups_by_controller;
        std::map<std::pair<std::uint32_t, std::uint32_t>,
                 std::pair<std::uint32_t, double>> group_states;
        for (const auto& signal : traffic_signals) {
            const auto aspect = static_cast<std::uint32_t>(signal.aspect);
            const auto signal_kind = static_cast<std::uint32_t>(signal.kind);
            if (signal.id == 0 || signal.group_id < 1 || signal.group_id > 4096
                || signal.controller_id < 1 || signal.controller_id > 64
                || aspect > 3 || signal_kind < 1 || signal_kind > 2
                || !std::isfinite(signal.heading_deg)
                || signal.heading_deg < 0 || signal.heading_deg >= 360
                || !std::isfinite(signal.remaining_seconds)
                || signal.remaining_seconds < 0
                || signal.remaining_seconds > kMaxTrafficSignalCountdownSeconds
                || !std::isfinite(signal.position_enu.east_m)
                || !std::isfinite(signal.position_enu.north_m)
                || !std::isfinite(signal.position_enu.up_m)
                || std::abs(signal.position_enu.east_m) > 1e6
                || std::abs(signal.position_enu.north_m) > 1e6
                || std::abs(signal.position_enu.up_m) > 1e6
                || std::find(ids.begin(), ids.end(), signal.id) != ids.end()) {
                throw std::invalid_argument("invalid or duplicate traffic signal snapshot");
            }
            ids.push_back(signal.id);
            if (aspect == 2 || aspect == 3) {
                auto& permitted_group = permitted_groups_by_controller[signal.controller_id];
                if (permitted_group != 0 && permitted_group != signal.group_id) {
                    throw std::invalid_argument("conflicting traffic groups must never proceed together");
                }
                permitted_group = signal.group_id;
            }
            const auto group_key = std::pair{signal.controller_id, signal.group_id};
            const auto existing_group = group_states.find(group_key);
            if (existing_group != group_states.end()
                && (existing_group->second.first != aspect
                    || std::abs(existing_group->second.second
                                - signal.remaining_seconds) > .001)) {
                throw std::invalid_argument("signal heads in one group must agree");
            }
            group_states[group_key] = {aspect, signal.remaining_seconds};
            auto* target = world_state->add_traffic_signals();
            target->set_signal_id(signal.id);
            target->set_group_id(signal.group_id);
            target->set_aspect(static_cast<simcore::TrafficSignalAspect>(aspect));
            target->mutable_position_enu()->set_x(signal.position_enu.east_m);
            target->mutable_position_enu()->set_y(signal.position_enu.north_m);
            target->mutable_position_enu()->set_z(signal.position_enu.up_m);
            // A valid double just below 360 may round up in the float wire
            // field. Keep the receiver's [0, 360) contract across conversion.
            const float wire_heading = static_cast<float>(signal.heading_deg);
            target->set_heading_deg(wire_heading >= 360.f ? 0.f : wire_heading);
            target->set_remaining_seconds(static_cast<float>(signal.remaining_seconds));
            target->set_controller_id(signal.controller_id);
            target->set_signal_kind(static_cast<simcore::TrafficSignalKind>(signal.kind));
        }
    } else if (!traffic_network_checksum.empty()) {
        throw std::invalid_argument("traffic checksum without signal heads");
    }

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

std::string serialize_hello_envelope(
    const EnvelopeMetadata& metadata,
    std::string_view build,
    const std::vector<std::string>& capabilities)
{
    if (build.empty() || build.size() > 128) {
        throw std::invalid_argument("Hello build must contain 1 to 128 bytes");
    }
    if (capabilities.empty() || capabilities.size() > 32) {
        throw std::invalid_argument(
            "Hello must advertise between 1 and 32 capabilities");
    }

    simcore::Envelope envelope;
    fill_envelope(envelope, metadata);
    auto* hello = envelope.mutable_hello();
    hello->set_build(std::string(build));
    hello->set_schema(std::string(kProtocolSchemaName));
    for (const auto& capability : capabilities) {
        if (capability.empty() || capability.size() > 128) {
            throw std::invalid_argument(
                "Hello capability must contain 1 to 128 bytes");
        }
        hello->add_capabilities(capability);
    }
    return envelope.SerializeAsString();
}

} // namespace simcore_host
