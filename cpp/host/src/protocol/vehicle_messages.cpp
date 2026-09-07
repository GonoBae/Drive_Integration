#include "protocol/vehicle_messages.hpp"

#include "coordinates/geo_transform.hpp"

#include "vehicle.pb.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
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

void fill_dent_patches(simcore::EntityState& entity, const std::vector<VehicleDentPatch>& patches)
{
    if (patches.size() > kMaximumVehicleDentPatches)
        throw std::invalid_argument("vehicle dent patch budget exceeded");
    for (const auto& patch : patches) {
        if (!valid_vehicle_dent(patch)) throw std::invalid_argument("invalid vehicle dent patch");
        auto* wire = entity.add_dent_patches();
        wire->set_forward(patch.forward);
        wire->set_left(patch.left);
        wire->set_inward_forward(patch.inward_forward);
        wire->set_inward_left(patch.inward_left);
        wire->set_radius_m(patch.radius_m);
        wire->set_depth_m(patch.depth_m);
    }
}

void fill_entity_state(simcore::EntityState& entity, const VehicleState& state,
                       RuntimeVehicleClass vehicle_class)
{
    if (static_cast<unsigned>(vehicle_class)
        > static_cast<unsigned>(RuntimeVehicleClass::Motorcycle)) {
        throw std::invalid_argument("invalid Ego vehicle class");
    }
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
    entity.set_runtime_vehicle_class(
        static_cast<simcore::RuntimeVehicleClass>(vehicle_class));

    entity.set_damage_percent(state.damage_percent);
    entity.set_last_impact_impulse_n_s(state.last_impact_impulse_n_s);
    entity.set_damage_zone(to_proto_damage_zone(state.damage_zone));
    entity.set_collision_event_sequence(state.collision_event_sequence);
    fill_dent_patches(entity,state.dent_patches);
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
    const float pitch_degrees = static_cast<float>(
        state.pitch_rad * 180.0 / std::numbers::pi);
    const float roll_degrees = static_cast<float>(
        state.roll_rad * 180.0 / std::numbers::pi);
    if (!std::isfinite(pitch_degrees) || !std::isfinite(roll_degrees)
        || !std::isfinite(state.pitch_rate_rad_s)
        || !std::isfinite(state.roll_rate_rad_s)) {
        throw std::invalid_argument("runtime body attitude and rates must be finite");
    }
    entity.set_entity_id(state.entity_id);
    entity.set_timestamp(timestamp);
    entity.set_entity_kind(to_proto_kind(state.kind));
    entity.set_gear(simcore::VEHICLE_GEAR_NEUTRAL);

    const double direction_length = std::hypot(state.impact_direction_enu.east_m,
        state.impact_direction_enu.north_m);
    if (!std::isfinite(state.heading_rad) || !std::isfinite(state.damage_percent)
        || state.damage_percent < 0.0f || state.damage_percent > 100.0f
        || !std::isfinite(state.last_impact_impulse_n_s)
        || state.last_impact_impulse_n_s < 0.0f || state.last_impact_impulse_n_s > 1e8f
        || static_cast<unsigned>(state.damage_zone) > static_cast<unsigned>(VehicleDamageZone::Underbody)
        || state.recovery_phase > 4 || state.turn_indicator > 2
        || static_cast<unsigned>(state.vehicle_class)
            > static_cast<unsigned>(RuntimeVehicleClass::Motorcycle)
        || (state.kind != RuntimeEntityKind::NpcVehicle
            && state.horn_event_sequence != 0)
        || (state.kind != RuntimeEntityKind::NpcVehicle
            && state.vehicle_class != RuntimeVehicleClass::Unspecified)
        || !std::isfinite(state.vertical_velocity_mps) || std::abs(state.vertical_velocity_mps) > 100.0
        || (state.pedestrian_airborne && !state.pedestrian_downed)
        || (state.kind != RuntimeEntityKind::Pedestrian && state.pedestrian_downed)
        || !std::isfinite(direction_length)
        || (direction_length != 0.0 && std::abs(direction_length - 1.0) > .001)) {
        throw std::invalid_argument("invalid runtime impact presentation state");
    }
    entity.set_damage_percent(state.damage_percent);
    entity.set_last_impact_impulse_n_s(state.last_impact_impulse_n_s);
    entity.set_damage_zone(to_proto_damage_zone(state.damage_zone));
    entity.set_collision_event_sequence(state.collision_event_sequence);
    entity.set_runtime_recovery_phase(static_cast<simcore::RuntimeRecoveryPhase>(state.recovery_phase));
    entity.set_pedestrian_downed(state.pedestrian_downed);
    entity.set_pedestrian_airborne(state.pedestrian_airborne);
    entity.set_turn_indicator(static_cast<simcore::TurnIndicator>(state.turn_indicator));
    entity.set_horn_event_sequence(state.horn_event_sequence);
    entity.set_runtime_vehicle_class(
        static_cast<simcore::RuntimeVehicleClass>(state.vehicle_class));
    if (state.kind == RuntimeEntityKind::Pedestrian && !state.dent_patches.empty())
        throw std::invalid_argument("pedestrian cannot carry vehicle dent patches");
    fill_dent_patches(entity,state.dent_patches);
    // Omit the empty message to preserve the pre-impact/legacy wire encoding.
    if (direction_length != 0.0) fill_vector(entity.mutable_impact_direction_enu(), state.impact_direction_enu);

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
                heading_rad = normalize_heading(state.heading_rad);
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
    entity.set_pitch(pitch_degrees);
    entity.set_roll(roll_degrees);
    entity.set_speed(static_cast<float>(std::hypot(
        proxy.linear_velocity_enu_mps.east_m,
        proxy.linear_velocity_enu_mps.north_m)));
    fill_vector(entity.mutable_position_enu(), center, center_up_m);
    fill_vector(
        entity.mutable_linear_velocity_enu(),
        proxy.linear_velocity_enu_mps, state.vertical_velocity_mps);

    const double sin_heading = std::sin(heading_rad);
    const double cos_heading = std::cos(heading_rad);
    auto* body_velocity = entity.mutable_linear_velocity_body();
    body_velocity->set_x(
        proxy.linear_velocity_enu_mps.east_m * sin_heading
        + proxy.linear_velocity_enu_mps.north_m * cos_heading);
    body_velocity->set_y(
        -proxy.linear_velocity_enu_mps.east_m * cos_heading
        + proxy.linear_velocity_enu_mps.north_m * sin_heading);
    body_velocity->set_z(state.vertical_velocity_mps);
    if (state.pitch_rad != 0.0 || state.roll_rad != 0.0) {
        const auto enu_from_body =
            GeoTransformAdapter::canonical_attitude_to_map_enu_from_base_link(
                {heading_rad, state.pitch_rad, state.roll_rad});
        const CoordinateQuaternion body_from_enu{
            -enu_from_body.x, -enu_from_body.y, -enu_from_body.z,
            enu_from_body.w};
        const auto velocity_body = GeoTransformAdapter::rotate_polar_vector(
            body_from_enu,
            {proxy.linear_velocity_enu_mps.east_m,
             proxy.linear_velocity_enu_mps.north_m, state.vertical_velocity_mps});
        body_velocity->set_x(velocity_body.x);
        body_velocity->set_y(velocity_body.y);
        body_velocity->set_z(velocity_body.z);
    }

    auto* body_angular_velocity = entity.mutable_angular_velocity_body();
    // Preserve the exact flat, zero-attitude-rate wire output (including
    // omitted X/Y fields). The canonical navigation yaw rate is left-positive;
    // collision proxy heading remains clockwise-positive and double precision.
    const double canonical_yaw_rate = -proxy.heading_rate_rad_s;
    body_angular_velocity->set_z(canonical_yaw_rate);
    if (state.pitch_rad != 0.0 || state.roll_rad != 0.0
        || state.pitch_rate_rad_s != 0.0 || state.roll_rate_rad_s != 0.0) {
        // Same yaw->pitch->roll Euler-to-FLU mapping as VehiclePhysics. Euler
        // rates themselves are not a body angular-velocity vector when tilted.
        const double pitch_sine = std::sin(state.pitch_rad);
        const double pitch_cosine = std::cos(state.pitch_rad);
        const double roll_sine = std::sin(state.roll_rad);
        const double roll_cosine = std::cos(state.roll_rad);
        body_angular_velocity->set_x(state.roll_rate_rad_s
            + canonical_yaw_rate * pitch_sine);
        body_angular_velocity->set_y(-state.pitch_rate_rad_s * roll_cosine
            + canonical_yaw_rate * roll_sine * pitch_cosine);
        body_angular_velocity->set_z(state.pitch_rate_rad_s * roll_sine
            + canonical_yaw_rate * roll_cosine * pitch_cosine);
    }
    entity.set_yaw_rate(static_cast<float>(body_angular_velocity->z()));
}

void fill_structure_state(simcore::StructureState& target,
                          const StructureDamageSnapshot& state)
{
    const auto bounded_point = [](const GroundPointEnu& point) {
        return std::isfinite(point.east_m) && std::isfinite(point.north_m)
            && std::isfinite(point.up_m) && std::abs(point.east_m) <= 1e6
            && std::abs(point.north_m) <= 1e6 && std::abs(point.up_m) <= 1e6;
    };
    const auto& normal = state.impact_normal_enu;
    const auto& direction = state.fall_direction_enu;
    const bool valid_normal = bounded_point(normal)
        && std::abs(normal.east_m * normal.east_m
                    + normal.north_m * normal.north_m
                    + normal.up_m * normal.up_m - 1.0) <= .001;
    const bool valid_direction = std::isfinite(direction.east_m)
        && std::isfinite(direction.north_m)
        && std::abs(direction.east_m * direction.east_m
                    + direction.north_m * direction.north_m - 1.0) <= .001;
    const bool building = state.kind == StructureKind::Building;
    const bool pole = state.kind == StructureKind::SignalPole;
    if (state.collider_id.empty() || state.collider_id.size() > 128
        || !std::all_of(state.collider_id.begin(), state.collider_id.end(),
                       [](unsigned char c) { return c >= 33 && c <= 126; })
        || (!building && !pole)
        || !std::isfinite(state.damage_percent) || state.damage_percent <= 0.0
        || static_cast<float>(state.damage_percent) <= 0.0f
        || state.damage_percent > 100.0 || state.event_sequence == 0
        || !std::isfinite(state.impact_half_width_m) || state.impact_half_width_m < 0.0 || state.impact_half_width_m > 3.0
        || !std::isfinite(state.impact_half_height_m) || state.impact_half_height_m < 0.0 || state.impact_half_height_m > 3.0
        || !std::isfinite(state.impact_severity) || state.impact_severity < 0.0 || state.impact_severity > 1.0
        || !bounded_point(state.impact_point_enu) || !bounded_point(state.base_position_enu)
        || !valid_normal || !std::isfinite(state.heading_rad)
        || std::abs(state.heading_rad) > 2.0 * std::numbers::pi
        || !std::isfinite(state.fall_angle_rad) || state.fall_angle_rad < 0.0
        || state.fall_angle_rad > std::numbers::pi / 2.0
        || !std::isfinite(direction.east_m) || !std::isfinite(direction.north_m)
        || std::abs(direction.east_m) > 1.001 || std::abs(direction.north_m) > 1.001
        || (building && (state.signal_id != 0 || state.fall_angle_rad != 0.0 || state.disabled))
        || (pole && (state.signal_id == 0 || !valid_direction
                     || (state.fall_angle_rad > 0.0 && !state.disabled)))) {
        throw std::invalid_argument("invalid structure damage snapshot");
    }
    target.set_collider_id(state.collider_id);
    target.set_kind(building ? simcore::STRUCTURE_KIND_BUILDING
                             : simcore::STRUCTURE_KIND_SIGNAL_POLE);
    target.set_signal_id(state.signal_id);
    target.set_damage_percent(static_cast<float>(state.damage_percent));
    target.set_event_sequence(state.event_sequence);
    target.set_impact_half_width_m(static_cast<float>(state.impact_half_width_m));
    target.set_impact_half_height_m(static_cast<float>(state.impact_half_height_m));
    target.set_impact_severity(static_cast<float>(state.impact_severity));
    const auto vector = [](simcore::Vector3d* output, const GroundPointEnu& point) {
        output->set_x(point.east_m); output->set_y(point.north_m); output->set_z(point.up_m);
    };
    vector(target.mutable_impact_point_enu(), state.impact_point_enu);
    vector(target.mutable_impact_normal_enu(), normal);
    vector(target.mutable_base_position_enu(), state.base_position_enu);
    target.set_heading_rad(state.heading_rad);
    target.set_fall_angle_rad(static_cast<float>(state.fall_angle_rad));
    fill_vector(target.mutable_fall_direction_enu(), direction);
    target.set_disabled(state.disabled);
}

} // namespace

std::string serialize_entity_state_packet(
    const VehicleState& state,
    RuntimeVehicleClass ego_vehicle_class)
{
    simcore::EntityStatePacket packet;
    fill_entity_state(*packet.add_entities(), state, ego_vehicle_class);
    return packet.SerializeAsString();
}

std::string serialize_world_state_envelope(const VehicleState& state,
                                           const EnvelopeMetadata& metadata,
                                           std::optional<HealthSnapshot> health,
                                           RuntimeVehicleClass ego_vehicle_class)
{
    return serialize_world_state_envelope(
        state, {}, metadata, health, {}, {}, {}, ego_vehicle_class);
}

std::string serialize_world_state_envelope(
    const VehicleState& state,
    const std::vector<RuntimeEntityState>& runtime_entities,
    const EnvelopeMetadata& metadata,
    std::optional<HealthSnapshot> health,
    const std::vector<TrafficSignalSnapshot>& traffic_signals,
    std::string_view traffic_network_checksum,
    const std::vector<StructureDamageSnapshot>& structures,
    RuntimeVehicleClass ego_vehicle_class)
{
    simcore::Envelope envelope;
    fill_envelope(envelope, metadata);
    auto* world_state = envelope.mutable_world_state();
    fill_entity_state(*world_state->add_entities(), state, ego_vehicle_class);
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
        struct PermittedGroups {
            std::set<std::uint32_t> groups;
            bool has_vehicle_head = false;
        };
        std::map<std::uint32_t, PermittedGroups> permitted_groups_by_controller;
        std::map<std::pair<std::uint32_t, std::uint32_t>,
                 std::pair<std::uint32_t, double>> group_states;
        for (const auto& signal : traffic_signals) {
            const auto aspect = static_cast<std::uint32_t>(signal.aspect);
            const auto signal_kind = static_cast<std::uint32_t>(signal.kind);
            if (signal.id == 0 || signal.group_id < 1 || signal.group_id > 4096
                || signal.controller_id < 1 || signal.controller_id > 64
                || aspect > 3 || signal_kind < 1 || signal_kind > 2
                || (signal.out_of_service
                    && (signal.aspect != SignalAspect::Red || signal.remaining_seconds != 0.0))
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
                auto& permitted = permitted_groups_by_controller[signal.controller_id];
                permitted.groups.insert(signal.group_id);
                permitted.has_vehicle_head = permitted.has_vehicle_head || signal.kind == TrafficSignalKind::Vehicle;
                if (permitted.groups.size() > 1 && permitted.has_vehicle_head) {
                    throw std::invalid_argument("conflicting traffic groups must never proceed together");
                }
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
            target->set_out_of_service(signal.out_of_service);
        }
    } else if (!traffic_network_checksum.empty()) {
        throw std::invalid_argument("traffic checksum without signal heads");
    }

    if (structures.size() > 256) {
        throw std::invalid_argument("structure snapshot exceeds 256 entries");
    }
    std::set<std::string> structure_ids;
    std::set<std::uint32_t> pole_signal_ids;
    for (const auto& structure : structures) {
        if (!structure_ids.insert(structure.collider_id).second) {
            throw std::invalid_argument("duplicate structure collider ID");
        }
        fill_structure_state(*world_state->add_structures(), structure);
        if (structure.kind == StructureKind::SignalPole) {
            const auto signal = std::find_if(traffic_signals.begin(), traffic_signals.end(),
                [&](const auto& value) { return value.id == structure.signal_id; });
            if (signal == traffic_signals.end()
                || signal->out_of_service != structure.disabled
                || !pole_signal_ids.insert(structure.signal_id).second) {
                throw std::invalid_argument("damaged pole must match one current signal head");
            }
        }
    }
    for (const auto& signal : traffic_signals) {
        if (!signal.out_of_service) continue;
        if (!pole_signal_ids.contains(signal.id)
            || std::any_of(traffic_signals.begin(), traffic_signals.end(),
                [&](const auto& other) {
                    return other.controller_id == signal.controller_id
                        && (other.aspect != SignalAspect::Red || other.remaining_seconds != 0.0);
                })) {
            throw std::invalid_argument("broken head requires damaged pole and all-red controller");
        }
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
