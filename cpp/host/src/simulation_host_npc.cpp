#include "simulation_host.hpp"
#include "player_vehicle_profile.hpp"
#include "traffic/pedestrian_profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <type_traits>

namespace {
constexpr std::uint32_t kFirstLaneNpcId = 1001;
constexpr std::uint32_t kFirstPedestrianId = 2001;
constexpr double kScanStepM = 0.25;
constexpr double kPedestrianCrossingClearanceSeconds = 0.2;
// Keep waiting/crossing capsules clear of their signal pole and of the
// opposing pedestrian track: 0.35m body radius + 0.14m pole half-width plus
// separation margin.
constexpr double kPedestrianLaneOffsetM = 0.60;
constexpr double kPedestrianMaximumReactionSpeedMps = 60.0;
constexpr double kNpcNavigationDecisionPeriodS = 0.25;
constexpr double kNpcOvertakeDecisionDelayS = 0.75;
constexpr double kNpcEscapeMaximumRetreatM = 6.0;
constexpr double kNpcAccidentEscapeMaximumRetreatM = 16.0;
constexpr double kNpcEscapeRearReserveM = 1.0;
constexpr double kNpcEscapeMaximumReverseSpeedMps = 1.4;
constexpr double kNpcEscapeReverseAccelerationMps2 = 1.0;
constexpr double kNpcNominalMaximumYawRateRadS = 2.6;
constexpr double kNpcNominalMaximumYawAccelerationRadS2 = 10.0;
constexpr std::array<double,4> kNpcNominalMinimumTurnRadiiM{4.8,4.0,8.5,2.6};

struct NpcSearchSliceMeasurement {
    SimulationHost::Clock::time_point started;
    double& maximum_ms;

    ~NpcSearchSliceMeasurement()
    {
        maximum_ms = std::max(maximum_ms, std::chrono::duration<double,std::milli>(
            SimulationHost::Clock::now() - started).count());
    }
};

struct NpcVehicleProfileDefinition {
    const char* name;
    double half_length_m;
    double half_width_m;
    double half_height_m;
    double mass_kg;
    double yaw_inertia_kg_m2;
    double maximum_reaction_speed_mps;
    double speed_scale;
    double acceleration_mps2;
    double braking_mps2;
    double tumble_contact_below_cg_m;
    double ground_clearance_m = 0.10;
};

// Deterministic ID-order fleet. These are reduced kinematic/collision
// profiles, not a claim that the NPCs run the Ego's full tire-force model.
constexpr std::array<NpcVehicleProfileDefinition, 4> kNpcVehicleProfiles{{
    {"sedan",      2.20, 1.00, 0.75, 1500.0,  2600.0, 30.0, 1.00, 1.50, 3.00, 0.35},
    {"compact",    1.75, 0.86, 0.70, 1050.0,  1450.0, 32.0, 1.08, 1.90, 3.40, 0.32},
    // SharedWheelMesh tread radius is 32.055 cm before the truck's 1.22 scale.
    // Body minimum -26 cm minus tire bottom (-28 - 32.055*1.22) cm = 41.1071 cm.
    {"truck",      3.10, 1.05, 0.965, 6200.0, 14500.0, 20.0, 0.72, 0.85, 2.20, 0.65, 0.411071},
    {"motorcycle", 1.10, 0.42, 0.68,  240.0,   210.0, 36.0, 1.12, 2.30, 4.00, 0.38},
}};

simcore_host::NpcHornMotionContext horn_motion_context(
    simcore_host::NpcLaneStopReason reason)
{
    using Context = simcore_host::NpcHornMotionContext;
    using Reason = simcore_host::NpcLaneStopReason;
    switch (reason) {
    case Reason::None: return Context::Normal;
    case Reason::Blocked: return Context::ObstructionStop;
    case Reason::Signal: return Context::TrafficControlStop;
    case Reason::Uninitialized:
    case Reason::Disabled:
    case Reason::Terminal:
    case Reason::NoGround:
    case Reason::InvalidInput:
        return Context::Inactive;
    }
    return Context::Inactive;
}

void apply_pedestrian_body_state(simcore_host::RuntimeEntityState& entity,
    const simcore_host::PedestrianImpactState& body)
{
    const auto center = std::visit([](const auto& shape) { return shape.center_enu; }, entity.collision_proxy.shape);
    entity.pedestrian_downed = body.downed();
    entity.pedestrian_airborne = body.airborne();
    entity.vertical_velocity_mps = body.vertical_velocity_mps();
    entity.pitch_rad = body.pitch_rad();
    entity.pitch_rate_rad_s = body.pitch_rate_rad_s();
    entity.roll_rad = entity.roll_rate_rad_s = 0.0;
    entity.collision_proxy.supported_vehicle_id = body.supported_vehicle_id();
    entity.collision_proxy.tire_support_candidate = body.downed() && !body.airborne()
        && body.supported_vehicle_id().empty() && std::abs(body.pitch_rad()) > 1.35;
    if (body.downed()) {
        const auto support = body.support();
        entity.heading_rad = body.heading_rad();
        entity.collision_proxy.shape = simcore_host::ObbPrism{center, body.center_up_m(),
            body.heading_rad(), support.projected_half_length_m,
            support.projected_half_width_m, support.projected_half_height_m};
        const auto& dimensions = body.dimensions();
        entity.collision_proxy.yaw_inertia_kg_m2 = dimensions.mass_kg
            * (dimensions.half_height_m * dimensions.half_height_m
                + dimensions.half_width_m * dimensions.half_width_m) / 3.0;
    } else {
        const auto profile = simcore_host::pedestrian_profile(entity.entity_id);
        entity.collision_proxy.shape = simcore_host::VerticalCapsule{
            center, body.center_up_m(), profile.radius_m, profile.half_height_m};
        entity.collision_proxy.yaw_inertia_kg_m2 = 0.0;
    }
    entity.collision_proxy.heading_rate_rad_s = 0.0;
}

simcore_host::ObbPrism ego_shape(
    const VehicleState& ego, const VehicleParameters& parameters)
{
    const double heading = ego.heading * std::numbers::pi / 180.0;
    const double offset = parameters.collision_body_center_forward_offset_m;
    return {{ego.position_enu.x + std::sin(heading) * offset,
             ego.position_enu.y + std::cos(heading) * offset},
        ego.position_enu.z - parameters.cg_height_m
            + parameters.collision_body_ground_clearance_m + ego.collision_half_height_m,
        heading, ego.collision_half_length_m, ego.collision_half_width_m,
        ego.collision_half_height_m};
}

std::vector<simcore_host::PedestrianVehicleSurface> pedestrian_vehicle_surfaces(
    const VehicleState& ego, const std::vector<simcore_host::RuntimeEntityState>& entities,
    const VehicleParameters& parameters)
{
    std::vector<simcore_host::PedestrianVehicleSurface> result;
    if (std::abs(ego.pitch) < 20.0 && std::abs(ego.roll) < 20.0
        && ego.collision_half_height_m > 0.0) {
        result.push_back({"ego", ego_shape(ego, parameters)});
    }
    for (const auto& entity : entities) {
        if (entity.kind != simcore_host::RuntimeEntityKind::NpcVehicle
            || std::abs(entity.pitch_rad) > 0.35 || std::abs(entity.roll_rad) > 0.35) continue;
        if (const auto* shape = std::get_if<simcore_host::ObbPrism>(&entity.collision_proxy.shape))
            result.push_back({entity.collision_proxy.proxy_id, *shape});
    }
    return result;
}

simcore_host::ObbPrism npc_shape(const simcore_host::NpcLaneSample& sample,
    double half_length_m, double half_width_m, double half_height_m,
    double ground_clearance_m = 0.10)
{
    // Baked ground is the tire plane. Each profile keeps its authored body
    // clearance; NPC ModelRoot already centres the body in the horizontal plane.
    return {{sample.position_enu.east_m, sample.position_enu.north_m},
            sample.position_enu.up_m + half_height_m + ground_clearance_m,
            sample.heading_deg * std::numbers::pi / 180.0,
            half_length_m, half_width_m, half_height_m};
}

simcore_host::RuntimeEntityState npc_entity(
    std::uint32_t entity_id, const simcore_host::NpcLaneSample& sample,
    std::uint32_t vehicle_profile_index,
    double half_length_m, double half_width_m, double half_height_m,
    double mass_kg, double yaw_inertia_kg_m2, double maximum_reaction_speed_mps,
    double ground_clearance_m)
{
    simcore_host::RuntimeEntityState result{entity_id,
            simcore_host::RuntimeEntityKind::NpcVehicle,
            {"lane-npc-" + std::to_string(entity_id),
             npc_shape(sample, half_length_m, half_width_m, half_height_m, ground_clearance_m), {}, 0.0,
             {0.8, 0.08}, mass_kg, yaw_inertia_kg_m2,
             maximum_reaction_speed_mps}};
    constexpr std::array classes{
        simcore_host::RuntimeVehicleClass::Sedan,
        simcore_host::RuntimeVehicleClass::Compact,
        simcore_host::RuntimeVehicleClass::Truck,
        simcore_host::RuntimeVehicleClass::Motorcycle};
    result.vehicle_class = vehicle_profile_index < classes.size()
        ? classes[vehicle_profile_index]
        : simcore_host::RuntimeVehicleClass::Unspecified;
    return result;
}

std::optional<simcore_host::GroundPointEnu> supported_point(
    const simcore_host::GroundQuery& ground,
    const simcore_host::GroundPointEnu& authored)
{
    const auto hit = ground.query_down({
        {authored.east_m, authored.north_m, authored.up_m + 2.0}, 4.0});
    if (!hit || !std::isfinite(hit->point_enu.east_m)
        || !std::isfinite(hit->point_enu.north_m)
        || !std::isfinite(hit->point_enu.up_m)
        || hit->normal_enu.up_m < 0.5) {
        return std::nullopt;
    }
    return hit->point_enu;
}

simcore_host::RuntimeEntityState pedestrian_entity(
    std::uint32_t entity_id, const simcore_host::GroundPointEnu& ground_point,
    double heading_rad)
{
    const auto profile = simcore_host::pedestrian_profile(entity_id);
    const simcore_host::VerticalCapsule capsule{
        {ground_point.east_m, ground_point.north_m},
        ground_point.up_m + profile.half_height_m,
        profile.radius_m, profile.half_height_m};
    simcore_host::RuntimeEntityState result{entity_id, simcore_host::RuntimeEntityKind::Pedestrian,
            {"pedestrian-" + std::to_string(entity_id), capsule, {}, 0.0,
             {0.75, 0.0}, profile.mass_kg, 0.0,
             kPedestrianMaximumReactionSpeedMps}};
    result.heading_rad = heading_rad;
    return result;
}

double signed_heading_delta(double to_rad, double from_rad)
{
    return std::remainder(to_rad - from_rad, 2.0 * std::numbers::pi);
}

simcore_host::CollisionVector2 shape_center(
    const simcore_host::KinematicProxyShape& shape)
{
    return std::visit(
        [](const auto& value) { return value.center_enu; }, shape);
}

void offset_shape(
    simcore_host::KinematicProxyShape& shape,
    const simcore_host::CollisionVector2& offset)
{
    std::visit(
        [&](auto& value) {
            value.center_enu.east_m += offset.east_m;
            value.center_enu.north_m += offset.north_m;
        },
        shape);
}

void clamp_vector_magnitude(
    simcore_host::CollisionVector2& value, double maximum)
{
    const double magnitude = std::hypot(value.east_m, value.north_m);
    if (magnitude > maximum) {
        const double scale = maximum / magnitude;
        value.east_m *= scale;
        value.north_m *= scale;
    }
}

bool overlaps(const simcore_host::ObbPrism& body,
              const simcore_host::KinematicProxyShape& other)
{
    return std::visit([&](const auto& shape) {
        if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, simcore_host::ObbPrism>) {
            // Cheap conservative broad phase before the oriented narrow phase.
            const double reach = body.half_length_m + body.half_width_m
                + shape.half_length_m + shape.half_width_m;
            if (std::abs(body.center_enu.east_m - shape.center_enu.east_m) > reach
                || std::abs(body.center_enu.north_m - shape.center_enu.north_m) > reach) return false;
            return simcore_host::intersect_obb_prisms(body, shape).has_value();
        } else {
            return simcore_host::intersect_obb_vertical_capsule(body, shape).has_value();
        }
    }, other);
}
} // namespace

void SimulationHost::rebuild_lane_npc()
{
    lane_npcs_.clear();
    npc_search_entity_id_ = 0;
    npc_search_after_entity_id_ = 0;
    npc_route_planner_.reset();
    npc_local_road_index_.reset();
    std::erase_if(runtime_entities_, [](const auto& entity) {
        return entity.collision_proxy.proxy_id.starts_with("lane-npc-");
    });
    if (config_.npc_route.empty()) return;
    if (!config_.traffic_network || !config_.ground_query
        || config_.traffic_network->source_map_checksum != config_.map_package_checksum) return;
    if (config_.npc_autonomous) {
        npc_route_planner_.emplace(*config_.traffic_network);
        npc_local_road_index_ = simcore_host::make_npc_local_road_index(*config_.traffic_network);
    }

    const std::vector<const std::vector<std::uint32_t>*> routes =
        config_.npc_alternate_route.empty()
            ? std::vector<const std::vector<std::uint32_t>*>{&config_.npc_route}
            : std::vector<const std::vector<std::uint32_t>*>{
                &config_.npc_route, &config_.npc_alternate_route};
    for (std::uint32_t index = 0; index < config_.npc_count; ++index) {
        try {
            const auto* route = routes[index % routes.size()];
            const double requested_offset = config_.npc_start_offset_m
                + static_cast<double>(index / routes.size()) * config_.npc_spacing_m;
            LaneNpcRuntime runtime;
            runtime.entity_id = kFirstLaneNpcId + index;
            const auto& profile = kNpcVehicleProfiles[index % kNpcVehicleProfiles.size()];
            runtime.vehicle_profile_index = index % kNpcVehicleProfiles.size();
            runtime.vehicle_profile_name = profile.name;
            runtime.body_half_length_m = profile.half_length_m;
            runtime.body_half_width_m = profile.half_width_m;
            runtime.body_half_height_m = profile.half_height_m;
            runtime.body_ground_clearance_m = profile.ground_clearance_m;
            runtime.mass_kg = profile.mass_kg;
            runtime.yaw_inertia_kg_m2 = profile.yaw_inertia_kg_m2;
            runtime.maximum_reaction_speed_mps = profile.maximum_reaction_speed_mps;
            runtime.tumble_dimensions = {profile.half_length_m, profile.half_width_m,
                profile.half_height_m, profile.mass_kg, profile.tumble_contact_below_cg_m};
            simcore_host::NpcLaneFollowerConfig settings;
            settings.max_speed_mps = config_.npc_max_speed_mps * profile.speed_scale;
            settings.acceleration_mps2 = profile.acceleration_mps2;
            settings.braking_mps2 = profile.braking_mps2;
            settings.front_extent_m = profile.half_length_m;
            settings.half_width_m = profile.half_width_m;
            runtime.follower = simcore_host::NpcLaneFollower(settings);
            runtime.follower.rebuild(*config_.traffic_network, *config_.ground_query,
                                     *route, config_.npc_route_loop, 0.0);
            runtime.start_offset_m = config_.npc_route_loop
                ? std::fmod(requested_offset, runtime.follower.route_length_m())
                : requested_offset;
            runtime.follower.reset(runtime.start_offset_m);
            runtime.navigation_route = *route;
            if (config_.npc_autonomous) choose_npc_destination(runtime);
            lane_npcs_.push_back(std::move(runtime));
        } catch (const std::exception& error) {
            std::cerr << "[NPC] entity=" << (kFirstLaneNpcId + index)
                      << " disabled: " << error.what() << "\n";
        }
    }

    for (auto& npc : lane_npcs_) {
        if (!lane_npc_blocked_distance(npc, 0.0)) {
            if (const auto anchor = npc.follower.sample_ahead(0.0)) {
                runtime_entities_.push_back(npc_entity(npc.entity_id, *anchor,
                    npc.vehicle_profile_index,
                    npc.body_half_length_m, npc.body_half_width_m, npc.body_half_height_m,
                    npc.mass_kg, npc.yaw_inertia_kg_m2, npc.maximum_reaction_speed_mps,
                    npc.body_ground_clearance_m));
            }
        }
        std::cout << "[NPC] route ready entity=" << npc.entity_id
                  << " length_m=" << npc.follower.route_length_m()
                  << " start_m=" << npc.start_offset_m
                  << " profile=" << npc.vehicle_profile_name
                  << " max_speed_mps=" << npc.follower.config().max_speed_mps << "\n";
    }
}

std::vector<NpcNavigationSnapshot> SimulationHost::npc_navigation() const
{
    std::vector<NpcNavigationSnapshot> result;
    for (const auto& npc : lane_npcs_) {
        const char* avoidance_phase = "none";
        switch (npc.avoidance_phase) {
        case LaneNpcRuntime::AvoidancePhase::None: break;
        case LaneNpcRuntime::AvoidancePhase::WaitingForStop:
            avoidance_phase = "waiting_for_stop"; break;
        case LaneNpcRuntime::AvoidancePhase::Backing:
            avoidance_phase = "backing"; break;
        case LaneNpcRuntime::AvoidancePhase::ReadyToPass:
            avoidance_phase = "ready_to_pass"; break;
        }
        result.push_back({npc.entity_id, npc.follower.state().lane_id,
            npc.destination_lane_id, npc.destinations_selected, npc.reroutes,
            npc.lane_changes_completed, npc.lane_change.has_value(), npc.navigation_route,
            simcore_host::RuntimeCollisionReaction::phase_name(npc.reaction.recovery.phase()), npc.reaction.recovery.damage_percent(),
            npc.reaction.recovery.hold_remaining_seconds(), npc.follower.state().lane_offset_m,
            npc.follower.state().distance_travelled_m, npc.lane_change_wait_reason,
            npc.avoidance_stop_distance_m.has_value(), avoidance_phase,
            npc.avoidance_reverse_remaining_m, npc.vehicle_profile_name,
            npc.local_bypass.has_value(),npc.local_bypasses_completed,
            npc.local_bypass ? npc.local_bypass->lateral_offset_m() : 0.0,
            npc.search_steps,npc.next_reverse_probe_m,
            npc.local_search_samples,npc.local_search.has_value(),npc.local_search_max_slice_ms,
            npc.local_search_max_scene_ms,npc.local_search_scene_shapes});
    }
    return result;
}

void SimulationHost::clear_npc_avoidance_state(LaneNpcRuntime& npc)
{
    npc.avoidance_stop_distance_m.reset();
    npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::None;
    npc.avoidance_reverse_remaining_m = 0.0;
    npc.avoidance_reverse_speed_mps = 0.0;
    npc.avoidance_reverse_total_m = 0.0;
    npc.maneuver_recovery_active = false;
    npc.maneuver_obstruction_seconds = 0.0;
    npc.maneuver_recovery_anchor_m = 0.0;
    npc.lane_change_wait_reason.clear();
    npc.local_search.reset();
    npc.navigation_search_requested = false;
    npc.next_reverse_probe_m = 0.0;
}

bool SimulationHost::choose_npc_destination(LaneNpcRuntime& npc)
{
    if (!npc_route_planner_ || npc.lane_change || npc.local_bypass) return false;
    const auto plan = npc_route_planner_->choose_destination(
        npc.follower.state().lane_id, npc.entity_id, npc.destinations_selected,
        {}, npc.destination_lane_id);
    if (!plan) return false;
    try { npc.follower.reroute(*config_.traffic_network, plan->lane_ids); }
    catch (const std::invalid_argument&) { return false; }
    npc.navigation_route = plan->lane_ids;
    npc.destination_lane_id = plan->destination_lane_id;
    npc.navigation_search_requested = false;
    npc.next_reverse_probe_m = 0.0;
    clear_npc_avoidance_state(npc);
    ++npc.destinations_selected;
    std::cout << "[NPC] destination entity=" << npc.entity_id
              << " lane=" << npc.destination_lane_id
              << " trip=" << npc.destinations_selected << "\n";
    return true;
}

std::optional<simcore_host::NpcLaneSample> SimulationHost::sample_lane_npc(
    const LaneNpcRuntime& npc, double distance_m) const
{
    if (npc.local_bypass) {
        return npc.local_bypass->sample(npc.follower.state().distance_travelled_m+distance_m);
    }
    if (npc.lane_change) {
        return npc.lane_change->sample(npc.follower.state().lane_offset_m + distance_m);
    }
    return npc.follower.sample_ahead(distance_m);
}

bool SimulationHost::npc_sample_blocked(const LaneNpcRuntime& npc,
    const simcore_host::NpcLaneSample& sample, bool stationary_only,
    const simcore_host::ObbPrism* collision_shape, bool ignore_ego,
    std::uint32_t* accident_entity_id) const
{
    if (accident_entity_id) *accident_entity_id = 0;
    const auto ego = physics_.get_state();
    const auto params = simcore_host::make_player_vehicle_parameters(
        config_.vehicle_parameters, active_vehicle_class_);
    const auto player_shape = ego_shape(ego, params);
    const auto candidate = collision_shape ? *collision_shape : npc_shape(sample,
        npc.body_half_length_m, npc.body_half_width_m, npc.body_half_height_m,
        npc.body_ground_clearance_m);
    // A red-light queue is not a closed road. Only static colliders and a
    // stationary Ego/independent proxy can invalidate a future route lane.
    if (!ignore_ego && (!stationary_only || std::hypot(ego.linear_velocity_body.x, ego.linear_velocity_body.y) < 0.3)
        && overlaps(candidate, player_shape)) {
        if (accident_entity_id && (ego.damage_percent > 0.0f || ego.collision_event_sequence != 0))
            *accident_entity_id = ego.entity_id;
        return true;
    }
    if (config_.collision_world) {
        for (const auto& collider : config_.collision_world->static_colliders()) {
            if (overlaps(candidate, collider.shape)) return true;
        }
    }
    for (const auto& structure : structure_damage_.collision_proxies()) {
        if (overlaps(candidate, structure.shape)) return true;
    }
    for (const auto& entity : runtime_entities_) {
        if (entity.entity_id == npc.entity_id) continue;
        const auto& proxy = entity.collision_proxy;
        if (ignore_ego && proxy.mass_kg > 0.0) continue; // external motion goes to finite pair solver
        if (stationary_only) {
            if (std::hypot(proxy.linear_velocity_enu_mps.east_m, proxy.linear_velocity_enu_mps.north_m) >= 0.3)
                continue;
            // Normal queues/walkers are transient, but a crashed NPC or downed
            // person is a persistent blocked lane and must participate in detours.
            if ((proxy.proxy_id.starts_with("lane-npc-")
                    || entity.kind == simcore_host::RuntimeEntityKind::Pedestrian)
                && entity.recovery_phase == 0 && !entity.pedestrian_downed) continue;
        }
        if (overlaps(candidate, proxy.shape)) {
            if (accident_entity_id && (entity.pedestrian_downed
                    || entity.recovery_phase != 0 || entity.damage_percent > 0.0f
                    || entity.collision_event_sequence != 0)) {
                *accident_entity_id = entity.entity_id;
            }
            return true;
        }
    }
    // Earlier agents in this deterministic tick order reserve their next pose.
    for (const auto& other : lane_npcs_) {
        if (!ignore_ego && !stationary_only && other.entity_id != npc.entity_id && other.pending
            && overlaps(candidate, other.pending->collision_proxy.shape)) return true;
    }
    return false;
}

std::optional<double> SimulationHost::lane_npc_blocked_distance(
    const LaneNpcRuntime& npc, double lookahead_m, bool stationary_only,
    bool route_end_is_blocker) const
{
    const auto obstacle = lane_npc_obstacle(npc, lookahead_m, stationary_only, route_end_is_blocker);
    return obstacle ? std::optional<double>(obstacle->distance_m) : std::nullopt;
}

std::optional<SimulationHost::NpcRouteObstacle> SimulationHost::lane_npc_obstacle(
    const LaneNpcRuntime& npc, double lookahead_m, bool stationary_only,
    bool route_end_is_blocker) const
{
    for (double distance = 0.0; distance <= lookahead_m + 1e-9; distance += kScanStepM) {
        const auto sample = sample_lane_npc(npc, distance);
        if (!sample) return stationary_only || !route_end_is_blocker ? std::nullopt
            : std::optional<NpcRouteObstacle>(NpcRouteObstacle{
                distance + npc.follower.config().front_extent_m, 0});
        std::uint32_t accident_entity_id = 0;
        if (npc_sample_blocked(npc, *sample, stationary_only, nullptr, false, &accident_entity_id)) {
            return NpcRouteObstacle{std::max(0.0, distance - kScanStepM)
                + npc.follower.config().front_extent_m, accident_entity_id};
        }
    }
    return std::nullopt;
}

std::optional<simcore_host::NpcLaneSample> SimulationHost::sample_lane_npc_behind(
    const LaneNpcRuntime& npc, double distance_m) const
{
    // The source follower still owns lane/stopline progress during a lateral
    // maneuver. Validate its same-lane boundary, but sweep the actual curve.
    const auto source = npc.follower.sample_behind(distance_m);
    if (!source) return std::nullopt;
    if (npc.local_bypass) {
        const double station = npc.follower.state().distance_travelled_m - distance_m;
        if (station >= npc.local_bypass->begin_m()) return npc.local_bypass->sample(station);
    } else if (npc.lane_change) {
        const double station = npc.follower.state().lane_offset_m - distance_m;
        if (station >= npc.lane_change->source_begin_m()) return npc.lane_change->sample(station);
    }
    // The rear reserve may extend before the plan's beginning. Its zero-offset
    // endpoint joins this same source lane continuously; no boundary is crossed.
    return source;
}

bool SimulationHost::npc_reverse_path_clear(
    const LaneNpcRuntime& npc, double distance_m) const
{
    if (!std::isfinite(distance_m) || distance_m < 0.0) return false;
    const int samples = std::max(1,
        static_cast<int>(std::ceil(distance_m / kScanStepM)));
    for (int index = 1; index <= samples; ++index) {
        const double behind = distance_m * static_cast<double>(index) / samples;
        const auto sample = sample_lane_npc_behind(npc, behind);
        if (!sample || npc_sample_blocked(npc, *sample)) return false;
    }
    return true;
}

bool SimulationHost::begin_npc_escape_reverse(
    LaneNpcRuntime& npc, double obstruction_distance_m, std::uint32_t accident_entity_id)
{
    const auto& state = npc.follower.state();
    const double retreat_limit = accident_entity_id != 0
        ? kNpcAccidentEscapeMaximumRetreatM : kNpcEscapeMaximumRetreatM;
    if (!std::isfinite(obstruction_distance_m) || obstruction_distance_m < 0.0
        || !npc.follower.can_retreat_from_current_lane() || npc.lane_change || npc.local_bypass
        || npc.avoidance_reverse_total_m >= retreat_limit) {
        npc.next_reverse_probe_m = 0.0;
        return false;
    }
    // Retained forward turning room is preferable to reversing while an
    // adjacent moving car clears. Reverse only a genuinely close blockage.
    if (obstruction_distance_m >= npc.follower.config().front_extent_m + 8.0) {
        npc.next_reverse_probe_m = 0.0;
        return false;
    }
    const auto lane = std::find_if(config_.traffic_network->lanes.begin(),
        config_.traffic_network->lanes.end(),
        [&](const auto& value) { return value.id == state.lane_id; });
    if (lane == config_.traffic_network->lanes.end()) return false;

    const double maximum = std::min(std::max(0.0,state.lane_offset_m-kNpcEscapeRearReserveM),
        retreat_limit - npc.avoidance_reverse_total_m);
    // Create turning room in short increments. A complete future bypass is
    // not a prerequisite for a safe reverse; replan after every half metre.
    npc.avoidance_stop_distance_m = state.distance_travelled_m;
    if (state.speed_mps > 0.05) {
        npc.next_reverse_probe_m = 0.5;
        npc.lane_change_wait_reason = "braking_for_reverse_search";
        npc.navigation_search_requested = true;
        return false;
    }
    const double requested = std::min(0.5, maximum);
    if (requested < 0.05) {
        npc.next_reverse_probe_m = 0.0;
        npc.lane_change_wait_reason = "reverse_limit_reached";
        return false;
    }
    if (!npc_reverse_path_clear(npc, requested + kNpcEscapeRearReserveM)) {
        npc.next_reverse_probe_m = 0.0;
        npc.lane_change_wait_reason = "reverse_path_occupied";
        return false;
    }
    npc.local_search.reset();
    npc.navigation_search_requested = false;
    npc.next_reverse_probe_m = 0.0;
    npc.avoidance_reverse_remaining_m = requested;
    npc.avoidance_reverse_speed_mps = 0.0;
    npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::WaitingForStop;
    npc.lane_change_wait_reason = "braking_for_reverse";
    std::cout << "[NPC] escape-reverse planned entity=" << npc.entity_id
              << " retreat_m=" << requested
              << " accident_entity=" << accident_entity_id
              << " obstacle_m=" << obstruction_distance_m << "\n";
    return true;
}

bool SimulationHost::try_npc_lane_change(LaneNpcRuntime& npc, double retreat_m, bool commit)
{
    auto state = npc.follower.state();
    const auto reject = [&](const char* reason) {
        npc.lane_change_wait_reason = reason;
        return false;
    };
    if (!npc_route_planner_ || npc.lane_change || npc.local_bypass) return reject("navigation_inactive");
    if (state.stopline_committed && retreat_m <= 0.0) return reject("intersection_committed");
    if (retreat_m > 0.0) {
        const auto behind = npc.follower.sample_behind(retreat_m);
        if (!behind) return reject("reverse_outside_current_lane");
        static_cast<simcore_host::NpcLaneSample&>(state) = *behind;
        state.speed_mps = 0.0;
    }
    if (std::hypot(npc.reaction.offset_enu_m.east_m, npc.reaction.offset_enu_m.north_m) > 0.15)
        return reject("recovering_collision_offset");
    const auto& network = *config_.traffic_network;
    const auto lane = std::find_if(network.lanes.begin(), network.lanes.end(),
        [&](const auto& value) { return value.id == state.lane_id; });
    if (lane == network.lanes.end() || lane->lane_changes.empty())
        return reject("no_authored_adjacent_lane");
    if (state.stopline_committed) {
        if (!npc.follower.can_retreat_from_current_lane()) return reject("intersection_committed");
        double lane_length = 0.0;
        for (std::size_t point = 1; point < lane->points.size(); ++point) {
            const auto& a = lane->points[point - 1];
            const auto& b = lane->points[point];
            lane_length += std::hypot(std::hypot(b.east_m-a.east_m,b.north_m-a.north_m),b.up_m-a.up_m);
        }
        if (state.lane_offset_m + npc.follower.config().front_extent_m
                + npc.follower.config().stop_margin_m > lane_length) {
            return reject("reverse_not_behind_stopline");
        }
    }
    npc.lane_change_wait_reason = "outside_change_window";
    bool rejected_gap = false, rejected_sweep = false, rejected_route = false;
    std::vector<simcore_host::NpcLaneChangeGapVehicle> neighbours;
    const auto ego = physics_.get_state();
    const double ego_heading = ego.heading * std::numbers::pi / 180.0;
    const auto parameters = simcore_host::make_player_vehicle_parameters(
        config_.vehicle_parameters, active_vehicle_class_);
    const auto player_shape = ego_shape(ego, parameters);
    const double lane_heading = state.heading_deg * std::numbers::pi / 180.0;
    const auto projected_extents = [&](double heading, double half_length, double half_width) {
        const double c = std::abs(std::cos(heading - lane_heading));
        const double s = std::abs(std::sin(heading - lane_heading));
        return std::pair{c * half_length + s * half_width,
                         s * half_length + c * half_width};
    };
    const auto ego_extents = projected_extents(ego_heading,
        ego.collision_half_length_m, ego.collision_half_width_m);
    neighbours.push_back({{player_shape.center_enu.east_m, player_shape.center_enu.north_m,
                          player_shape.center_up_m},
        {std::sin(ego_heading) * ego.linear_velocity_body.x
             - std::cos(ego_heading) * ego.linear_velocity_body.y,
         std::cos(ego_heading) * ego.linear_velocity_body.x
             + std::sin(ego_heading) * ego.linear_velocity_body.y},
        ego_extents.first, ego_extents.second});
    for (const auto& entity : runtime_entities_) {
        if (entity.entity_id == npc.entity_id) continue;
        std::visit([&](const auto& shape) {
            double half_length = 0.35, half_width = 0.35;
            if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, simcore_host::ObbPrism>) {
                const auto extents = projected_extents(shape.heading_rad,
                    shape.half_length_m, shape.half_width_m);
                half_length = extents.first; half_width = extents.second;
            }
            neighbours.push_back({{shape.center_enu.east_m, shape.center_enu.north_m,
                shape.center_up_m}, {entity.collision_proxy.linear_velocity_enu_mps.east_m,
                    entity.collision_proxy.linear_velocity_enu_mps.north_m, 0.0},
                half_length, half_width});
        }, entity.collision_proxy.shape);
    }
    for (const auto& change : lane->lane_changes) {
        // Adoption validates the actual post-tick station. Leave enough room
        // for one complete fixed step, including its acceleration, not just epsilon.
        const double tick = 1.0 / config_.physics_frequency_hz;
        const double end_margin = config_.npc_max_speed_mps * tick
            + npc.follower.config().acceleration_mps2 * tick * tick + 0.05;
        const double available = change.source_end_m - state.lane_offset_m - end_margin;
        const double minimum_length = std::max(8.0, state.speed_mps * 2.5);
        const double preferred_length = std::min(std::max(12.0, minimum_length), available);
        if (preferred_length < minimum_length) continue;
        // Do not demand 12m from a legal 8..12m low-speed window. A shorter
        // alternative also lets a stopped follower clear a nearby crash, but
        // never discards the 2.5s speed requirement or swept-body validation.
        for (int choice = 0; choice < 2; ++choice) {
            if (choice == 1 && preferred_length - minimum_length < 1e-6) break;
            const double length = choice == 0 ? preferred_length : minimum_length;
            const auto plan = simcore_host::plan_npc_lane_change(network, *config_.ground_query,
                state.lane_id, state.lane_offset_m, change.target_lane_id, length,
                npc.body_half_length_m, npc.body_half_width_m);
            if (!plan || plan->source_end_m() + end_margin > change.source_end_m) continue;
            if (!simcore_host::npc_lane_change_gap_safe(
                    *plan, state.lane_offset_m, state.speed_mps, neighbours,
                    npc.body_half_length_m, npc.body_half_width_m)) {
                rejected_gap = true;
                continue;
            }
            auto route = npc_route_planner_->shortest_route(
                change.target_lane_id, npc.destination_lane_id);
            bool replaced_destination = false;
            if (!route) {
                // Dedicated turn lanes do not always reach the originally
                // selected destination. A stationary blocker must not make the
                // NPC wait forever in the source lane: choose a deterministic
                // destination reachable from the clear adjacent lane instead.
                const std::array<std::uint32_t, 1> blocked_source_lane{state.lane_id};
                route = npc_route_planner_->choose_destination(
                    change.target_lane_id, npc.entity_id,
                    npc.destinations_selected, blocked_source_lane,
                    npc.destination_lane_id);
                replaced_destination = route.has_value();
            }
            if (!route) { rejected_route = true; continue; }
            bool clear = true;
            // Swept corridor, including the exact endpoint: reject a parked
            // car, pedestrian or wall anywhere along the whole manoeuvre.
            const int samples = static_cast<int>(std::ceil(length / kScanStepM));
            for (int index = 0; index <= samples; ++index) {
                const auto sample = plan->sample(plan->source_begin_m() + length * index / samples);
                if (!sample || npc_sample_blocked(npc, *sample)) { clear = false; break; }
                // Reserve other active manoeuvres, including swaps into each other.
                for (const auto& other : lane_npcs_) {
                    if (!other.lane_change || other.entity_id == npc.entity_id) continue;
                    for (double other_offset = other.follower.state().lane_offset_m;
                         other_offset <= other.lane_change->source_end_m(); other_offset += 1.0) {
                        const auto other_sample = other.lane_change->sample(other_offset);
                        if (other_sample && overlaps(
                                npc_shape(*sample, npc.body_half_length_m,
                                    npc.body_half_width_m, npc.body_half_height_m, npc.body_ground_clearance_m),
                                npc_shape(*other_sample, other.body_half_length_m,
                                    other.body_half_width_m, other.body_half_height_m, other.body_ground_clearance_m))) {
                            clear = false; break;
                        }
                    }
                    if (!clear) break;
                }
                if (!clear) break;
            }
            if (!clear) { rejected_sweep = true; continue; }
            if (!commit) return true;
            npc.lane_change = *plan;
            npc.lane_change_fault_reported = false;
            npc.lane_change_route = route->lane_ids;
            if (replaced_destination) {
                npc.destination_lane_id = route->destination_lane_id;
                ++npc.destinations_selected;
                ++npc.reroutes;
            }
            clear_npc_avoidance_state(npc);
            std::cout << "[NPC] lane-change begin entity=" << npc.entity_id
                      << " from=" << state.lane_id << " to=" << change.target_lane_id
                      << (replaced_destination ? " revised_destination=" : "")
                      << (replaced_destination ? std::to_string(npc.destination_lane_id) : "")
                      << "\n";
            return true;
        }
    }
    if (rejected_sweep) return reject("swept_corridor_occupied");
    if (rejected_gap) return reject("adjacent_traffic_gap");
    if (rejected_route) return reject("destination_unreachable");
    return false;
}

SimulationHost::NpcLocalScene SimulationHost::npc_local_obstacles(
    const LaneNpcRuntime& npc) const
{
    NpcLocalScene result;
    auto minimum=npc.follower.state().position_enu,maximum=minimum;
    const auto include=[&](const std::optional<simcore_host::NpcLaneSample>& sample) {
        if (!sample) return;
        minimum.east_m=std::min(minimum.east_m,sample->position_enu.east_m);
        minimum.north_m=std::min(minimum.north_m,sample->position_enu.north_m);
        maximum.east_m=std::max(maximum.east_m,sample->position_enu.east_m);
        maximum.north_m=std::max(maximum.north_m,sample->position_enu.north_m);
    };
    for (double s=0.0;s<=60.0;s+=2.0) include(npc.follower.sample_ahead(s));
    for (double s=0.0;s<=16.0;s+=2.0) include(npc.follower.sample_behind(s));
    const simcore_host::ObbPrism region{{(minimum.east_m+maximum.east_m)*.5,
        (minimum.north_m+maximum.north_m)*.5},npc.follower.state().position_enu.up_m+5.0,0.0,
        (maximum.north_m-minimum.north_m)*.5+11.0,
        (maximum.east_m-minimum.east_m)*.5+11.0,15.0};
    const auto append=[&](const simcore_host::KinematicProxyShape& shape) {
        if (overlaps(region,shape)) result.occupied.push_back(shape);
    };
    const auto add_motion=[&](const simcore_host::KinematicProxyShape& shape,
        const simcore_host::CollisionVector2& velocity,double yaw_rate,
        double center_of_mass_offset_m=0.0) {
        append(shape);
        const double speed=std::hypot(velocity.east_m,velocity.north_m);
        if (speed<0.1 && std::abs(yaw_rate)<0.02) return;
        const double route_heading=npc.follower.state().heading_deg*std::numbers::pi/180.0;
        const double approaching=velocity.east_m*std::sin(route_heading)+velocity.north_m*std::cos(route_heading);
        // A brief excursion across a marking must finish before oncoming
        // traffic arrives, not merely survive the next three seconds. Local
        // plans using opposite-flow space are bounded to this full horizon.
        const bool oncoming=approaching< -0.5*speed;
        // One conservative swept OBB per moving body, cached once per search.
        // Rotation reserves its circumscribed footprint, including a stopped
        // truck whose yaw recovery swings its corners into the available gap.
        std::visit([&](const auto& value) {
            double half_length,half_width;
            const double direction=speed>0.1 ? std::atan2(velocity.east_m,velocity.north_m) : 0.0;
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>,simcore_host::ObbPrism>) {
                const double angle=value.heading_rad-direction;
                half_length=std::abs(std::cos(angle))*value.half_length_m
                    +std::abs(std::sin(angle))*value.half_width_m;
                half_width=std::abs(std::sin(angle))*value.half_length_m
                    +std::abs(std::cos(angle))*value.half_width_m;
                // This sweep is centred on the current OBB, not the CG. Its
                // offset centre can move by twice the offset during a turn.
                if (std::abs(yaw_rate)>=0.02) half_length=half_width=
                    std::hypot(value.half_length_m,value.half_width_m)
                        +2.0*std::abs(center_of_mass_offset_m);
            } else half_length=half_width=value.radius_m;
            for (int seconds=oncoming ? 1 : 0;seconds<=(oncoming ? 30 : 0);++seconds) {
                const double horizon=oncoming ? seconds : 3.0;
                const simcore_host::ObbPrism swept{{value.center_enu.east_m+velocity.east_m*horizon*.5,
                    value.center_enu.north_m+velocity.north_m*horizon*.5},value.center_up_m,direction,
                    half_length+speed*horizon*.5,half_width,value.half_height_m};
                if (!overlaps(region,swept)) continue;
                if (oncoming) result.approaching_sweeps[seconds].push_back(swept);
                else append(swept);
            }
        },shape);
    };
    if (config_.collision_world) for (const auto& collider:config_.collision_world->static_colliders()) append(collider.shape);
    for (const auto& structure:structure_damage_.collision_proxies()) append(structure.shape);
    const auto ego=physics_.get_state();
    const auto parameters=simcore_host::make_player_vehicle_parameters(config_.vehicle_parameters,active_vehicle_class_);
    const double heading=ego.heading*std::numbers::pi/180.0;
    add_motion(ego_shape(ego, parameters),{std::sin(heading)*ego.linear_velocity_body.x
            -std::cos(heading)*ego.linear_velocity_body.y,
        std::cos(heading)*ego.linear_velocity_body.x+std::sin(heading)*ego.linear_velocity_body.y},
        ego.angular_velocity_body.z,parameters.collision_body_center_forward_offset_m);
    for (const auto& entity : runtime_entities_) {
        if (entity.entity_id!=npc.entity_id) add_motion(entity.collision_proxy.shape,
            entity.collision_proxy.linear_velocity_enu_mps,entity.collision_proxy.heading_rate_rad_s);
    }
    for (const auto& other : lane_npcs_) {
        if (other.entity_id!=npc.entity_id && other.pending) append(other.pending->collision_proxy.shape);
        if (other.entity_id==npc.entity_id || (!other.local_bypass && !other.lane_change)) continue;
        const double remaining=other.local_bypass
            ? other.local_bypass->end_m()-other.follower.state().distance_travelled_m
            : other.lane_change->source_end_m()-other.follower.state().lane_offset_m;
        for (double distance=0.0;distance<=remaining;distance+=0.5) {
            const auto reserved=sample_lane_npc(other,distance);
            if (reserved) append(npc_shape(*reserved,other.body_half_length_m+0.12,
                    other.body_half_width_m+0.12,other.body_half_height_m,other.body_ground_clearance_m));
        }
    }
    return result;
}

bool SimulationHost::try_npc_local_bypass(LaneNpcRuntime& npc)
{
    if (!config_.traffic_network || !config_.ground_query || npc.local_bypass || npc.lane_change
        || std::hypot(npc.reaction.offset_enu_m.east_m,npc.reaction.offset_enu_m.north_m)>0.15) {
        npc.local_search.reset();
        return false;
    }
    const auto& reference=npc.follower;
    constexpr std::array<double,4> turn_radii{4.8,4.0,8.5,2.6};
    const simcore_host::NpcLocalBypassDimensions dimensions{npc.body_half_length_m,npc.body_half_width_m,
        turn_radii[std::min<std::size_t>(npc.vehicle_profile_index,turn_radii.size()-1)]};
    npc.avoidance_stop_distance_m=reference.state().distance_travelled_m;
    if (npc.local_search && std::abs(npc.local_search_anchor_m
            -reference.state().distance_travelled_m)>0.05) npc.local_search.reset();
    if (reference.state().speed_mps>0.05) {
        npc.navigation_search_requested=true;
        npc.lane_change_wait_reason="braking_for_local_search";
        return false;
    }
    const auto started=Clock::now();
    // Include preparation, incremental work, final live revalidation and every
    // early return. The sample-count bound is not a wall-clock time bound.
    const NpcSearchSliceMeasurement measure_slice{started, npc.local_search_max_slice_ms};
    if (!npc.local_search) {
        // Locate only the first obstacle cluster. The road index is shared by
        // all searches in this map generation, rather than rebuilt per probe.
        double last_blocked=-1.0;
        for (double distance=0.0;distance<=40.0;distance+=0.5) {
            const auto sample=reference.sample_ahead(distance);
            if (!sample) break;
            if (npc_sample_blocked(npc,*sample)) last_blocked=distance;
            else if (last_blocked>=0.0 && distance-last_blocked>=2.0) break;
        }
        if (last_blocked<0.0) return false;
        npc.local_search.emplace();
        npc.local_search_anchor_m=reference.state().distance_travelled_m;
        npc.local_search_observation_s=kNpcNavigationDecisionPeriodS;
        npc.local_search->begin(*config_.traffic_network,*config_.ground_query,
            reference,last_blocked,dimensions,npc_local_road_index_);
    }
    const auto scene_started=Clock::now();
    const auto scene=npc_local_obstacles(npc);
    npc.local_search_max_scene_ms=std::max(npc.local_search_max_scene_ms,
        std::chrono::duration<double,std::milli>(Clock::now()-scene_started).count());
    npc.local_search_scene_shapes=scene.occupied.size();
    for (const auto& horizon:scene.approaching_sweeps)
        npc.local_search_scene_shapes+=horizon.size();
    const auto is_clear=[&](const auto& sample,double prediction_seconds) {
            const auto body=npc_shape(sample,npc.body_half_length_m+0.12,npc.body_half_width_m+0.12,
                npc.body_half_height_m,npc.body_ground_clearance_m);
            const auto clear=[&](const auto& occupied) {
                return std::none_of(occupied.begin(),occupied.end(),[&](const auto& other) { return overlaps(body,other); });
            };
            const auto horizon=static_cast<std::size_t>(std::clamp(std::ceil(prediction_seconds),0.0,30.0));
            return clear(scene.occupied) && clear(scene.approaching_sweeps[horizon]);
        };
    // One fleet-wide round-robin turn consumes at most 48 geometry/prediction
    // samples, including within a candidate; a failed candidate cannot hog a tick.
    const auto progress=npc.local_search->advance(is_clear,39);
    npc.local_search_samples+=progress.samples_checked;
    if (progress.status==simcore_host::NpcLocalBypassSearchStatus::Pending) {
        npc.navigation_search_requested=true;
        npc.lane_change_wait_reason="local_search_in_progress";
        return false;
    }
    const auto plan=npc.local_search->plan();
    npc.local_search.reset();
    if (!plan) {
        npc.lane_change_wait_reason="local_road_or_swept_path_unavailable";
        return false;
    }
    // Earlier search samples may span several ticks. Revalidate the initial
    // stopping envelope against this tick's scene before adopting. Subsequent
    // motion still uses the live braking-distance sweep on every physics tick.
    for (double distance=0.0;distance<=2.0;distance+=kScanStepM) {
        ++npc.local_search_samples;
        const auto sample=plan->sample(plan->begin_m()+distance);
        if (!sample || !is_clear(*sample,0.0)) {
            npc.lane_change_wait_reason="local_scene_changed";
            return false;
        }
    }
    if (plan->borrows_opposing_space()) {
        const auto signals=current_traffic_signals(true);
        const auto has_time=[&](std::uint32_t lane_id,double entry_distance) {
            const auto lane=std::find_if(config_.traffic_network->lanes.begin(),config_.traffic_network->lanes.end(),
                [&](const auto& value) { return value.id==lane_id; });
            if (lane==config_.traffic_network->lanes.end()) return false;
            if (lane->signal_group_id==0 || (reference.state().stopline_committed
                && lane_id==reference.state().committed_lane_id)) return true;
            const double speed=plan->route_speed_limit_mps();
            const double acceleration=reference.config().acceleration_mps2;
            const double distance=std::max(0.0,entry_distance-npc.body_half_length_m);
            const double ramp_distance=speed*speed/(2.0*acceleration);
            const double entry_seconds=(distance<=ramp_distance ? std::sqrt(2.0*distance/acceleration)
                : speed/acceleration+(distance-ramp_distance)/speed)+0.2;
            // Only entry must fit the green. After lawful nose entry, the
            // follower's existing commitment permits clearing the junction.
            for (const auto& signal:signals) {
                if (signal.group_id==lane->signal_group_id)
                    return signal.aspect==simcore_host::SignalAspect::Green
                        && signal.remaining_seconds>entry_seconds;
                if (signal.left_group_id==lane->signal_group_id)
                    return signal.left_aspect==simcore_host::SignalAspect::Green
                        && signal.left_remaining_seconds>entry_seconds;
            }
            return false;
        };
        auto previous=reference.state().lane_id;
        for (double s=0.0;s<=plan->end_m()-plan->begin_m()+npc.body_half_length_m;s+=0.5) {
            const auto sample=reference.sample_ahead(s);
            if (!sample) break;
            if (sample->lane_id!=previous && !has_time(previous,s)) {
                npc.lane_change_wait_reason="local_return_waits_for_signal_window";
                return false;
            }
            previous=sample->lane_id;
        }
    }
    npc.local_bypass=*plan;
    npc.lane_change_fault_reported=false;
    clear_npc_avoidance_state(npc);
    std::cout << "[NPC] local-bypass begin entity=" << npc.entity_id
              << " lane=" << reference.state().lane_id
              << " offset_m=" << plan->lateral_offset_m()
              << " length_m=" << plan->end_m()-plan->begin_m()
              << " min_radius_m=" << dimensions.minimum_turn_radius_m
              << " max_search_slice_ms=" << std::max(npc.local_search_max_slice_ms,
                    std::chrono::duration<double,std::milli>(Clock::now()-started).count())
              << " max_scene_ms=" << npc.local_search_max_scene_ms
              << " scene_shapes=" << npc.local_search_scene_shapes << "\n";
    return true;
}

bool SimulationHost::try_npc_route_detour(LaneNpcRuntime& npc)
{
    if (!npc_route_planner_ || npc.follower.state().stopline_committed) return false;
    std::vector<std::uint32_t> blocked;
    for (double distance=2.0;distance<=80.0;distance+=1.0) {
        const auto sample=npc.follower.sample_ahead(distance);
        if (!sample) break;
        if (sample->lane_id!=npc.follower.state().lane_id
            && npc_sample_blocked(npc,*sample,true)
            && std::find(blocked.begin(),blocked.end(),sample->lane_id)==blocked.end())
            blocked.push_back(sample->lane_id);
    }
    if (blocked.empty()) return false;
    const auto plan=npc_route_planner_->shortest_route(
        npc.follower.state().lane_id,npc.destination_lane_id,blocked);
    if (!plan) return false;
    try { npc.follower.reroute(*config_.traffic_network,plan->lane_ids); }
    catch (const std::invalid_argument&) { return false; }
    npc.navigation_route=plan->lane_ids;
    clear_npc_avoidance_state(npc);
    ++npc.reroutes;
    std::cout << "[NPC] detour entity=" << npc.entity_id
              << " destination=" << npc.destination_lane_id
              << " excluded_lane=" << blocked.front() << "\n";
    return true;
}

void SimulationHost::update_npc_maneuver_recovery(LaneNpcRuntime& npc)
{
    const auto& state = npc.follower.state();
    if (!npc.maneuver_recovery_active && (!state.stopped || state.speed_mps > 0.05
            || state.stop_reason != simcore_host::NpcLaneStopReason::Blocked)) {
        npc.maneuver_obstruction_seconds = 0.0;
        if (npc.lane_change_wait_reason.starts_with("maneuver_recovery_"))
            npc.lane_change_wait_reason.clear();
        return;
    }
    if (npc.navigation_cooldown_s > 0.0) return;
    npc.navigation_cooldown_s = kNpcNavigationDecisionPeriodS;
    if (!sample_lane_npc(npc, 0.0)) return;
    const double beginning = npc.local_bypass ? npc.local_bypass->begin_m()
        : state.distance_travelled_m - state.lane_offset_m + npc.lane_change->source_begin_m();
    const double returned_distance = state.distance_travelled_m - beginning;
    // Keep observing the same blocker while backing away. A horizon based on
    // the shrinking distance to the anchor could lose a 16m accident blocker
    // halfway through recovery and repeatedly restart the failed forward plan.
    const double lookahead = npc.maneuver_recovery_active
        ? kNpcAccidentEscapeMaximumRetreatM + npc.follower.config().front_extent_m + 2.0 : 12.0;
    const auto observed = lane_npc_obstacle(npc, lookahead, false, false);
    const auto persistent = observed && observed->accident_entity_id != 0 ? observed
        : lane_npc_obstacle(npc, lookahead, true, false);
    // A static object farther down the route does not turn the healthy queue
    // at our bumper into a failed maneuver. Only the first actual blocker can
    // start recovery, and a signal/support/disabled stop never starts it.
    const bool first_is_persistent = observed && persistent
        && std::abs(observed->distance_m - persistent->distance_m) < kScanStepM * 0.5;
    if (!first_is_persistent) {
        if (npc.maneuver_recovery_active) clear_npc_avoidance_state(npc);
        npc.maneuver_obstruction_seconds = 0.0;
        if (npc.lane_change_wait_reason.starts_with("maneuver_recovery_"))
            npc.lane_change_wait_reason.clear();
        return;
    }
    if (!npc.maneuver_recovery_active) {
        // The first stopped observation establishes an anchor in time; it is
        // not evidence that the preceding polling interval was also stopped.
        if (npc.maneuver_obstruction_seconds < kNpcOvertakeDecisionDelayS) {
            npc.maneuver_obstruction_seconds += kNpcNavigationDecisionPeriodS;
            return;
        }
    }
    const double retreat_limit = persistent->accident_entity_id != 0
        ? kNpcAccidentEscapeMaximumRetreatM : kNpcEscapeMaximumRetreatM;
    const double remaining = npc.maneuver_recovery_active
        ? state.distance_travelled_m - npc.maneuver_recovery_anchor_m : returned_distance;
    const auto wait = [&](const char* reason) {
        npc.lane_change_wait_reason = reason;
        if (npc.maneuver_recovery_active) {
            npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::WaitingForStop;
            npc.avoidance_reverse_remaining_m = 0.0;
            npc.avoidance_reverse_speed_mps = 0.0;
            npc.avoidance_stop_distance_m = state.distance_travelled_m;
        }
    };
    if (remaining > 1.e-6 && (!npc.follower.can_retreat_from_current_lane()
            || !npc.follower.sample_behind(remaining + kNpcEscapeRearReserveM))) {
        wait("maneuver_recovery_outside_lane");
        return;
    }
    if (remaining + npc.avoidance_reverse_total_m > retreat_limit + 1.e-6) {
        wait("maneuver_recovery_retreat_limit");
        return;
    }
    if (std::hypot(npc.reaction.offset_enu_m.east_m, npc.reaction.offset_enu_m.north_m) > 0.15) {
        wait("maneuver_recovery_collision_offset");
        return;
    }
    if (!npc.maneuver_recovery_active) {
        npc.maneuver_recovery_active = true;
        npc.maneuver_recovery_anchor_m = beginning;
        npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::WaitingForStop;
        npc.avoidance_reverse_speed_mps = 0.0;
        npc.avoidance_stop_distance_m = state.distance_travelled_m;
    }
    if (remaining <= 1.e-6) {
        // Only the shared zero-offset beginning permits dropping a plan. The
        // follower remains at the physically reached anchor; no reset/reroute
        // is used to manufacture a pose or to reverse across a lane boundary.
        const double reversed = npc.avoidance_reverse_total_m;
        npc.lane_change.reset();
        npc.local_bypass.reset();
        npc.lane_change_route.clear();
        clear_npc_avoidance_state(npc);
        npc.avoidance_reverse_total_m = reversed;
        npc.navigation_cooldown_s = 0.0;
        npc.local_search_cooldown_s = 0.0;
        npc.navigation_search_requested = true;
        npc.lane_change_wait_reason = "maneuver_recovery_replanning";
        return;
    }
    if (npc.avoidance_phase == LaneNpcRuntime::AvoidancePhase::Backing) return;
    npc.avoidance_stop_distance_m = state.distance_travelled_m;
    if (!state.stopped || state.speed_mps > 0.05) return;
    const double requested = std::min(0.5, remaining);
    if (!npc_reverse_path_clear(npc, requested + kNpcEscapeRearReserveM)) {
        npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::WaitingForStop;
        npc.avoidance_reverse_speed_mps = 0.0;
        npc.lane_change_wait_reason = "maneuver_recovery_rear_occupied";
        return;
    }
    npc.avoidance_reverse_remaining_m = requested;
    npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::Backing;
    npc.lane_change_wait_reason = "maneuver_recovery_reversing";
}

void SimulationHost::update_npc_navigation(LaneNpcRuntime& npc, double dt_seconds)
{
    if (!npc_route_planner_) return;
    npc.navigation_cooldown_s = std::max(0.0, npc.navigation_cooldown_s - dt_seconds);
    npc.local_search_cooldown_s=std::max(0.0,npc.local_search_cooldown_s-dt_seconds);
    npc.local_search_observation_s=std::max(0.0,npc.local_search_observation_s-dt_seconds);
    const bool search_turn = npc.navigation_search_requested && npc_search_entity_id_ == npc.entity_id;
    if (search_turn) npc.navigation_cooldown_s = 0.0;
    if (npc.lane_change || npc.local_bypass) {
        update_npc_maneuver_recovery(npc);
        return;
    }
    if (npc.local_search) {
        // Continue only the selected NPC's bounded work. Do not rerun the
        // 80m navigation scan or authored lane search for every continuation.
        if (!search_turn) return;
        if (npc.local_search_observation_s<=0.0) {
            npc.local_search_observation_s=kNpcNavigationDecisionPeriodS;
            if (!lane_npc_obstacle(npc,40.0,false,false)) {
                clear_npc_avoidance_state(npc);
                return;
            }
        }
        ++npc.search_steps;
        npc.navigation_search_requested=false;
        if (try_npc_local_bypass(npc) || npc.local_search) return;
        npc.navigation_cooldown_s=kNpcNavigationDecisionPeriodS;
        npc.local_search_cooldown_s=1.0;
        const auto nearby=lane_npc_obstacle(npc,12.0,true,false);
        if (nearby) begin_npc_escape_reverse(npc,nearby->distance_m,nearby->accident_entity_id);
        return;
    }
    const auto persistent_obstacle = [&](const std::optional<NpcRouteObstacle>& first) {
        // Sliding accident victims remain the same hazard as their residual
        // velocity crosses the ordinary stationary-vehicle threshold.
        return first && first->accident_entity_id != 0 ? first
            : lane_npc_obstacle(npc, 80.0, true, false);
    };
    if (npc.avoidance_phase == LaneNpcRuntime::AvoidancePhase::Backing) {
        if (npc.navigation_cooldown_s == 0.0) {
            npc.navigation_cooldown_s = kNpcNavigationDecisionPeriodS;
            if (!persistent_obstacle(lane_npc_obstacle(npc, 80.0, false, false))) {
                clear_npc_avoidance_state(npc);
            }
        }
        return;
    }
    if (npc.avoidance_phase == LaneNpcRuntime::AvoidancePhase::WaitingForStop) {
        if (npc.navigation_cooldown_s > 0.0) return;
        npc.navigation_cooldown_s = kNpcNavigationDecisionPeriodS;
        if (!persistent_obstacle(lane_npc_obstacle(npc, 80.0, false, false))) {
            clear_npc_avoidance_state(npc);
            return;
        }
        if (npc.follower.state().speed_mps <= 0.05) {
            if (npc_reverse_path_clear(npc,
                    npc.avoidance_reverse_remaining_m + kNpcEscapeRearReserveM)) {
                npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::Backing;
                npc.lane_change_wait_reason = "reversing_for_turn_space";
            } else {
                npc.lane_change_wait_reason = "reverse_path_occupied";
            }
        }
        return;
    }
    if (npc.follower.destination_reached() || npc.destination_lane_id == 0) {
        if (npc.navigation_cooldown_s == 0.0) {
            choose_npc_destination(npc);
            npc.navigation_cooldown_s = kNpcNavigationDecisionPeriodS;
        }
        return;
    }
    if (npc.navigation_cooldown_s > 0.0) return;
    npc.navigation_cooldown_s = kNpcNavigationDecisionPeriodS;
    // Detect a stopped Ego while there is still room for a complete authored
    // manoeuvre. Forty metres was too late when the blocker sat just beyond an
    // intersection approach or a destination-specific lane split.
    const auto observed = lane_npc_obstacle(npc, 80.0, false, false);
    const auto obstruction = observed ? std::optional<double>(observed->distance_m) : std::nullopt;
    npc.obstruction_seconds = observed
        ? npc.obstruction_seconds + kNpcNavigationDecisionPeriodS : 0.0;
    const auto persistent = observed ? persistent_obstacle(observed) : std::nullopt;
    const bool persistent_obstruction = persistent.has_value();
    // A closure on a future lane can often be avoided by connectivity alone.
    // Resolve that before spending ticks on a local swept-path search.
    const auto first_blocked_sample=persistent
        ? npc.follower.sample_ahead(std::max(0.0,persistent->distance_m
            -npc.follower.config().front_extent_m+kScanStepM)) : std::nullopt;
    if (first_blocked_sample && first_blocked_sample->lane_id!=npc.follower.state().lane_id
        && try_npc_route_detour(npc)) return;
    if (!observed) {
        clear_npc_avoidance_state(npc);
    }
    npc.avoidance_stop_distance_m.reset();
    const auto& state = npc.follower.state();
    const auto lane = std::find_if(config_.traffic_network->lanes.begin(),config_.traffic_network->lanes.end(),
        [&](const auto& value) { return value.id == state.lane_id; });
    const bool can_still_change = lane != config_.traffic_network->lanes.end();
    if (obstruction && can_still_change) {
        // The ordinary 0.5m queue gap leaves no collision-free steering arc.
        // Do not spend the last 8m of clearance waiting for an occupied adjacent
        // lane: retain a forward-only stopping anchor until a full plan is clear.
        npc.avoidance_stop_distance_m = state.distance_travelled_m
            + std::max(0.0, *obstruction - npc.follower.config().front_extent_m - 8.0);
    }
    // A known crash/wall is not a red-light queue. Waiting two polling periods
    // can consume the last legal change window before trying the same plan.
    if (persistent_obstruction || npc.obstruction_seconds >= kNpcOvertakeDecisionDelayS) {
        const auto previous_reason = npc.lane_change_wait_reason;
        if (npc.local_search_cooldown_s <= 0.0) npc.navigation_search_requested = true;
        if (search_turn) {
            npc.navigation_search_requested = false;
            ++npc.search_steps;
            npc.local_search_cooldown_s = 1.0;
            if (npc.next_reverse_probe_m > 0.0 && persistent_obstruction && obstruction) {
                if (begin_npc_escape_reverse(npc,*obstruction,observed->accident_entity_id)) return;
            } else if (try_npc_lane_change(npc)) {
                npc.next_reverse_probe_m = 0.0;
                npc.obstruction_seconds=0.0;
                return;
            } else {
                // At bumper distance, create the first half-metre of space
                // promptly rather than exhausting all forward candidates first.
                if (persistent_obstruction && obstruction && npc.avoidance_reverse_total_m<0.05
                    && *obstruction<npc.follower.config().front_extent_m+2.0
                    && begin_npc_escape_reverse(npc,*obstruction,observed->accident_entity_id)) return;
                if (try_npc_local_bypass(npc)) {
                    npc.obstruction_seconds=0.0;
                    return;
                }
                if (npc.local_search || npc.navigation_search_requested) return;
                if (persistent_obstruction && obstruction)
                    begin_npc_escape_reverse(npc,*obstruction,observed->accident_entity_id);
            }
        }
        if (npc.next_reverse_probe_m > 0.0)
            npc.avoidance_stop_distance_m = state.distance_travelled_m;
        if (npc.lane_change_wait_reason != previous_reason) {
            std::cout << "[NPC] bypass wait entity=" << npc.entity_id
                      << " accident_entity=" << (observed ? observed->accident_entity_id : 0)
                      << " lane=" << state.lane_id << " station_m=" << state.lane_offset_m
                      << " reason=" << npc.lane_change_wait_reason
                      << " retaining_turn_space=" << npc.avoidance_stop_distance_m.has_value() << "\n";
        }
    } else {
        npc.lane_change_wait_reason.clear();
    }
}

void SimulationHost::prepare_lane_npc(double dt_seconds, Clock::time_point now)
{
    const bool enabled = lifecycle_active_ && !estop_latched_
        && make_health_snapshot(now).status == simcore_host::HealthStatus::Active;
    const auto signals = current_traffic_signals(enabled);

    // One expensive proposal for the entire fleet per tick, round-robin.
    // Work-count scheduling is deterministic and leaves the I/O loop a chance
    // to accept fresh input between reverse candidates, even when no route fits.
    npc_search_entity_id_ = 0;
    std::uint32_t first_requested = 0;
    if (enabled) for (const auto& npc : lane_npcs_) {
        if (!npc.navigation_search_requested || npc.lane_change || npc.local_bypass
            || !npc.reaction.recovery.allows_driving()) continue;
        if (first_requested == 0 || npc.entity_id < first_requested) first_requested = npc.entity_id;
        if (npc.entity_id > npc_search_after_entity_id_
            && (npc_search_entity_id_ == 0 || npc.entity_id < npc_search_entity_id_))
            npc_search_entity_id_ = npc.entity_id;
    }
    if (npc_search_entity_id_ == 0) npc_search_entity_id_ = first_requested;
    if (npc_search_entity_id_ != 0) npc_search_after_entity_id_ = npc_search_entity_id_;

    for (auto& npc : lane_npcs_) {
        npc.pending.reset();
        npc.nominal_pending.reset();
        auto current = std::find_if(runtime_entities_.begin(), runtime_entities_.end(),
            [&](const auto& entity) { return entity.entity_id == npc.entity_id; });
        if (current == runtime_entities_.end()) {
            if (lane_npc_blocked_distance(npc, 0.0)) continue;
            const auto anchor = sample_lane_npc(npc, 0.0);
            if (!anchor) continue;
            runtime_entities_.push_back(npc_entity(npc.entity_id, *anchor,
                npc.vehicle_profile_index,
                npc.body_half_length_m, npc.body_half_width_m, npc.body_half_height_m,
                npc.mass_kg, npc.yaw_inertia_kg_m2, npc.maximum_reaction_speed_mps,
                npc.body_ground_clearance_m));
            current = std::prev(runtime_entities_.end());
        }

        const auto previous_tumble = npc.tumble;
        npc.tumble.tick(dt_seconds, npc.tumble_dimensions, enabled);
        npc.reaction.update(current->collision_proxy, dt_seconds, enabled,
            npc.tumble.settled());
        if (npc.tumble.overturned() || npc.tumble.invalid_input()) {
            npc.reaction.recovery.disable_driving();
        }
        npc.reaction.publish(*current, physics_.get_last_collision_contacts());
        const bool drive = npc.reaction.recovery.allows_driving();
        if (enabled && drive) update_npc_navigation(npc, dt_seconds);
        current->npc_local_bypass_active=npc.local_bypass.has_value();

        const auto& settings = npc.follower.config();
        // Save before either forward or reverse motion. A failed curve sample
        // must not leave hidden follower progress behind the frozen world pose.
        const auto before_change = npc.lane_change || npc.local_bypass
            ? std::optional<simcore_host::NpcLaneFollower>(npc.follower) : std::nullopt;
        const double before_reverse_remaining = npc.avoidance_reverse_remaining_m;
        const double before_reverse_total = npc.avoidance_reverse_total_m;
        bool reversing_for_space = false;
        if (enabled && drive
            && npc.avoidance_phase == LaneNpcRuntime::AvoidancePhase::Backing) {
            const double desired_speed = std::min(kNpcEscapeMaximumReverseSpeedMps,
                std::sqrt(std::max(0.0, 2.0 * kNpcEscapeReverseAccelerationMps2
                    * npc.avoidance_reverse_remaining_m)));
            npc.avoidance_reverse_speed_mps += std::clamp(
                desired_speed - npc.avoidance_reverse_speed_mps,
                -kNpcEscapeReverseAccelerationMps2 * dt_seconds,
                kNpcEscapeReverseAccelerationMps2 * dt_seconds);
            const double retreat = std::min(npc.avoidance_reverse_remaining_m,
                npc.avoidance_reverse_speed_mps * dt_seconds);
            if (retreat > 0.0
                && npc_reverse_path_clear(npc, retreat + kNpcEscapeRearReserveM)
                && npc.follower.retreat_for_obstacle(retreat)) {
                reversing_for_space = true;
                npc.avoidance_reverse_remaining_m = std::max(0.0,
                    npc.avoidance_reverse_remaining_m - retreat);
                npc.avoidance_reverse_total_m += retreat;
                if (npc.maneuver_recovery_active)
                    npc.avoidance_stop_distance_m = npc.follower.state().distance_travelled_m;
                else npc.avoidance_stop_distance_m.reset();
                if (npc.avoidance_reverse_remaining_m <= 1.0e-6) {
                    npc.avoidance_reverse_remaining_m = 0.0;
                    npc.avoidance_reverse_speed_mps = 0.0;
                    npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::ReadyToPass;
                    npc.navigation_cooldown_s = 0.0;
                    npc.lane_change_wait_reason = npc.maneuver_recovery_active
                        ? "maneuver_recovery_reversing" : "reverse_complete_replanning";
                }
            } else {
                npc.avoidance_reverse_speed_mps = 0.0;
                npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::WaitingForStop;
                npc.navigation_cooldown_s = 0.0;
                npc.lane_change_wait_reason = npc.maneuver_recovery_active
                    ? "maneuver_recovery_rear_occupied" : "reverse_path_occupied";
            }
        }
        const double speed = npc.follower.state().speed_mps;
        const double lookahead = std::ceil((speed * speed / (2.0 * settings.braking_mps2)
            + speed * dt_seconds + settings.front_extent_m + settings.stop_margin_m + 2.0)
            / kScanStepM) * kScanStepM;
        const auto physical_obstacle = drive
            ? lane_npc_obstacle(npc, lookahead) : std::nullopt;
        const auto physical_blocked = physical_obstacle
            ? std::optional<double>(physical_obstacle->distance_m) : std::nullopt;
        auto blocked = physical_blocked;
        if (drive && ((!npc.lane_change && !npc.local_bypass) || npc.maneuver_recovery_active)
            && npc.avoidance_stop_distance_m) {
            const double reserved_stop = std::max(0.0, *npc.avoidance_stop_distance_m
                - npc.follower.state().distance_travelled_m)
                + settings.front_extent_m + settings.stop_margin_m;
            if (!blocked || reserved_stop < *blocked) blocked = reserved_stop;
        }

        simcore_host::NpcHornObservation horn_observation;
        horn_observation.enabled = enabled && drive
            && npc.avoidance_phase != LaneNpcRuntime::AvoidancePhase::Backing;
        horn_observation.motion_context = horn_motion_context(
            npc.follower.state().stop_reason);
        horn_observation.speed_mps = speed;
        // The blocker velocity is not part of the route occupancy query. Own
        // forward speed is a conservative closing estimate; event hysteresis
        // prevents one continuously occupied cell from retriggering.
        horn_observation.closing_speed_mps = speed;
        if (physical_obstacle) {
            horn_observation.nonresponsive_obstacle_id = physical_obstacle->accident_entity_id;
        }
        if (physical_blocked) {
            horn_observation.obstacle_clearance_m = std::max(0.0,
                *physical_blocked - settings.front_extent_m);
        }
        if (npc.follower.state().stop_reason
                == simcore_host::NpcLaneStopReason::Blocked) {
            // A healthy queue vehicle is filtered by stationary_only. The
            // longer scan covers the 8m steering reserve retained before a
            // crashed vehicle while a safe adjacent-lane gap opens.
            const double persistent_lookahead = npc.avoidance_stop_distance_m
                ? std::max(12.0, lookahead) : lookahead;
            const auto observed = lane_npc_obstacle(npc, persistent_lookahead, false, false);
            const auto persistent = observed && observed->accident_entity_id != 0 ? observed
                : lane_npc_obstacle(npc, persistent_lookahead, true, false);
            if (persistent) {
                horn_observation.persistent_obstruction = true;
                if (persistent->accident_entity_id != 0) {
                    horn_observation.nonresponsive_obstacle_id = persistent->accident_entity_id;
                }
                if (!horn_observation.obstacle_clearance_m) {
                    horn_observation.obstacle_clearance_m = std::max(0.0,
                        persistent->distance_m - settings.front_extent_m);
                }
            }
        }
        const auto& horn = npc.horn.step(dt_seconds, horn_observation);
        current->horn_event_sequence = horn.event_sequence;

        const auto& next = reversing_for_space
            ? npc.follower.state()
            : npc.follower.step(dt_seconds, signals, enabled && drive, blocked,
                npc.local_bypass ? npc.local_bypass->route_speed_limit_mps() : 55.6,
                npc.local_bypass ? npc.local_bypass->extra_stop_margin_m() : 0.0);
        if (!enabled || !next.valid) {
            current->collision_proxy.linear_velocity_enu_mps = {};
            current->collision_proxy.heading_rate_rad_s = 0.0;
            npc.reaction.nominal_velocity_enu_mps = {};
            npc.reaction.nominal_heading_rate_rad_s = 0.0;
            continue;
        }

        const auto& before = std::get<simcore_host::ObbPrism>(current->collision_proxy.shape);
        const auto old_offset = npc.reaction.offset_enu_m;
        const double old_heading_offset = npc.reaction.heading_offset_rad;
        const auto displayed = sample_lane_npc(npc, 0.0);
        const auto freeze_failed_change = [&] {
            if (before_change) npc.follower = *before_change;
            if (reversing_for_space) {
                npc.avoidance_reverse_remaining_m = before_reverse_remaining;
                npc.avoidance_reverse_total_m = before_reverse_total;
                npc.avoidance_reverse_speed_mps = 0.0;
                npc.avoidance_phase = LaneNpcRuntime::AvoidancePhase::WaitingForStop;
                npc.avoidance_stop_distance_m = npc.follower.state().distance_travelled_m;
            }
            npc.follower.step(0.0, signals, false);
            current->collision_proxy.linear_velocity_enu_mps = {};
            current->collision_proxy.heading_rate_rad_s = 0.0;
            npc.reaction.nominal_velocity_enu_mps = {};
            npc.reaction.nominal_heading_rate_rad_s = 0.0;
            if (!npc.lane_change_fault_reported) {
                std::cerr << "[NPC] lane-change safety stop entity=" << npc.entity_id << "\n";
                npc.lane_change_fault_reported = true;
            }
        };
        if (!displayed) {
            freeze_failed_change();
            continue;
        }
        if (npc.local_bypass && npc.local_bypass->complete(next.distance_travelled_m)) {
            npc.local_bypass.reset();
            ++npc.local_bypasses_completed;
            npc.navigation_cooldown_s=1.0;
            std::cout << "[NPC] local-bypass complete entity=" << npc.entity_id
                      << " lane=" << next.lane_id << "\n";
        }
        if (npc.lane_change && npc.lane_change->complete(next.lane_offset_m)) {
            const auto target_offset = npc.lane_change->target_offset(next.lane_offset_m);
            if (target_offset && npc.follower.complete_lane_change(
                    *config_.traffic_network, npc.lane_change_route, *target_offset)) {
                npc.navigation_route = npc.lane_change_route;
                npc.lane_change.reset();
                npc.lane_change_route.clear();
                ++npc.lane_changes_completed;
                npc.navigation_cooldown_s = 6.0;
                std::cout << "[NPC] lane-change complete entity=" << npc.entity_id
                          << " lane=" << npc.follower.state().lane_id << "\n";
            } else {
                // Never advance a hidden source progress while rendering the
                // target, nor snap sideways to recover from a rejected adoption.
                freeze_failed_change();
                continue;
            }
        }
        npc.nominal_pending = npc_entity(npc.entity_id, *displayed,
            npc.vehicle_profile_index,
            npc.body_half_length_m, npc.body_half_width_m, npc.body_half_height_m,
            npc.mass_kg, npc.yaw_inertia_kg_m2, npc.maximum_reaction_speed_mps,
            npc.body_ground_clearance_m);
        npc.nominal_pending->horn_event_sequence = horn.event_sequence;
        npc.nominal_pending->npc_local_bypass_active=npc.local_bypass.has_value();
        auto& planned_shape = std::get<simcore_host::ObbPrism>(
            npc.nominal_pending->collision_proxy.shape);
        const simcore_host::CollisionVector2 nominal_before{
            before.center_enu.east_m-old_offset.east_m,
            before.center_enu.north_m-old_offset.north_m};
        const double nominal_movement=std::hypot(planned_shape.center_enu.east_m-nominal_before.east_m,
            planned_shape.center_enu.north_m-nominal_before.north_m);
        const double previous_nominal_heading = before.heading_rad - old_heading_offset;
        const double desired_heading_delta = signed_heading_delta(
            planned_shape.heading_rad, previous_nominal_heading);
        const double desired_heading_rate = std::clamp(
            desired_heading_delta / dt_seconds,
            -kNpcNominalMaximumYawRateRadS, kNpcNominalMaximumYawRateRadS);
        const double previous_heading_rate = std::clamp(
            npc.reaction.nominal_heading_rate_rad_s,
            -kNpcNominalMaximumYawRateRadS, kNpcNominalMaximumYawRateRadS);
        double smoothed_heading_rate = previous_heading_rate + std::clamp(
            desired_heading_rate - previous_heading_rate,
            -kNpcNominalMaximumYawAccelerationRadS2 * dt_seconds,
            kNpcNominalMaximumYawAccelerationRadS2 * dt_seconds);
        if (smoothed_heading_rate * desired_heading_delta < 0.0) {
            smoothed_heading_rate = 0.0;
        }
        double smoothed_heading_delta = smoothed_heading_rate * dt_seconds;
        if (std::abs(smoothed_heading_delta) > std::abs(desired_heading_delta)) {
            smoothed_heading_delta = desired_heading_delta;
        }
        // Route steering requires travel, including during a near-stationary
        // restart. Collision angular momentum is handled separately below.
        const double minimum_turn_radius=kNpcNominalMinimumTurnRadiiM[
            std::min<std::size_t>(npc.vehicle_profile_index,kNpcNominalMinimumTurnRadiiM.size()-1)];
        const double distance_yaw_budget=nominal_movement<=1.e-6 ? 0.0 : nominal_movement/minimum_turn_radius;
        smoothed_heading_delta=std::clamp(smoothed_heading_delta,-distance_yaw_budget,distance_yaw_budget);
        // The route centre still advances authoritatively, while yaw rate and
        // yaw acceleration remain car-like even at a polyline vertex.
        planned_shape.heading_rad = std::remainder(
            previous_nominal_heading + smoothed_heading_delta,
            2.0 * std::numbers::pi);
        if (drive && enabled) {
            if (npc.local_bypass) {
                npc.nominal_pending->turn_indicator=npc.local_bypass->turn_signal_intent(next.distance_travelled_m);
                if (npc.nominal_pending->turn_indicator==0)
                    npc.nominal_pending->turn_indicator=npc.follower.turn_signal_intent(std::max(12.0,speed*2.5));
            } else if (npc.lane_change) {
                const auto target = npc.lane_change->sample(npc.lane_change->source_end_m());
                const auto source = npc.follower.sample_ahead(
                    std::max(0.0, npc.lane_change->source_end_m() - next.lane_offset_m));
                if (target && source) {
                    const double heading = source->heading_deg * std::numbers::pi / 180.0;
                    const double left = -(target->position_enu.east_m - source->position_enu.east_m) * std::cos(heading)
                        + (target->position_enu.north_m - source->position_enu.north_m) * std::sin(heading);
                    npc.nominal_pending->turn_indicator = left > 0.05 ? 1 : left < -0.05 ? 2 : 0;
                }
            } else npc.nominal_pending->turn_indicator=npc.follower.turn_signal_intent(std::max(12.0,speed*2.5));
        }
        npc.reaction.publish(*npc.nominal_pending, physics_.get_last_collision_contacts());
        const auto& nominal_after = std::get<simcore_host::ObbPrism>(
            npc.nominal_pending->collision_proxy.shape);
        npc.reaction.nominal_velocity_enu_mps = {
            (nominal_after.center_enu.east_m - nominal_before.east_m) / dt_seconds,
            (nominal_after.center_enu.north_m - nominal_before.north_m) / dt_seconds};
        npc.reaction.nominal_heading_rate_rad_s = signed_heading_delta(
            nominal_after.heading_rad,
            before.heading_rad - old_heading_offset) / dt_seconds;

        npc.reaction.integrate_offset(dt_seconds,
            simcore_host::RuntimeCollisionReaction::kDefaultVelocityDampingPerSecond, true, nominal_after.heading_rad);
        clamp_vector_magnitude(
            npc.reaction.velocity_enu_mps, npc.maximum_reaction_speed_mps);

        npc.pending = *npc.nominal_pending;
        auto& after = std::get<simcore_host::ObbPrism>(npc.pending->collision_proxy.shape);
        after.center_enu.east_m += npc.reaction.offset_enu_m.east_m;
        after.center_enu.north_m += npc.reaction.offset_enu_m.north_m;
        after.heading_rad = std::remainder(
            after.heading_rad + npc.reaction.heading_offset_rad,
            2.0 * std::numbers::pi);
        const auto apply_tumble_pose = [&] {
            const auto support = simcore_host::impact_tumble_support(npc.tumble_dimensions,
                npc.tumble.pitch_rad(), npc.tumble.roll_rad());
            after.half_length_m = support.projected_half_length_m;
            after.half_width_m = support.projected_half_width_m;
            after.half_height_m = support.projected_half_height_m;
            npc.pending->pitch_rad = npc.tumble.pitch_rad();
            npc.pending->roll_rad = npc.tumble.roll_rad();
            npc.pending->pitch_rate_rad_s = npc.tumble.pitch_rate_rad_s();
            npc.pending->roll_rate_rad_s = npc.tumble.roll_rate_rad_s();
        };
        apply_tumble_pose();
        if (!drive || npc.tumble.active() || std::hypot(old_offset.east_m, old_offset.north_m) > 0.001) {
            const double move = std::hypot(after.center_enu.east_m - before.center_enu.east_m,
                                          after.center_enu.north_m - before.center_enu.north_m);
            const double rotation = signed_heading_delta(after.heading_rad, before.heading_rad);
            const double pitch_delta = signed_heading_delta(npc.tumble.pitch_rad(), previous_tumble.pitch_rad());
            const double roll_delta = signed_heading_delta(npc.tumble.roll_rad(), previous_tumble.roll_rad());
            const double tilt_rotation = std::max(std::abs(pitch_delta), std::abs(roll_delta));
            const int samples = std::max(1, static_cast<int>(std::ceil(
                std::max({move / 0.1, std::abs(rotation) / 0.05, tilt_rotation / 0.05}))));
            bool clear = true;
            const auto old_support = simcore_host::impact_tumble_support(npc.tumble_dimensions,
                previous_tumble.pitch_rad(), previous_tumble.roll_rad());
            for (int index = 1; index <= samples; ++index) {
                const double t = static_cast<double>(index) / samples;
                const auto ground = supported_point(*config_.ground_query,
                    {before.center_enu.east_m + (after.center_enu.east_m - before.center_enu.east_m) * t,
                     before.center_enu.north_m + (after.center_enu.north_m - before.center_enu.north_m) * t,
                     before.center_up_m - old_support.support_height_m - npc.body_ground_clearance_m});
                if (!ground) { clear = false; break; }
                const auto support = simcore_host::impact_tumble_support(npc.tumble_dimensions,
                    previous_tumble.pitch_rad() + pitch_delta * t,
                    previous_tumble.roll_rad() + roll_delta * t);
                const simcore_host::ObbPrism candidate{{ground->east_m, ground->north_m},
                    ground->up_m + support.support_height_m + npc.body_ground_clearance_m,
                    before.heading_rad + rotation * t, support.projected_half_length_m,
                    support.projected_half_width_m, support.projected_half_height_m};
                // External impulse motion must reach the finite-mass solver,
                // even after the first crash has settled upright. Treating Ego
                // as an immovable route obstacle here erased the next impulse
                // and made a disabled/holding car appear welded to the road.
                // Slow, self-commanded route recovery still checks Ego normally.
                const auto phase = npc.reaction.recovery.phase();
                const bool external_motion = npc.tumble.active()
                    || phase == simcore_host::ImpactRecoveryPhase::Settling
                    || phase == simcore_host::ImpactRecoveryPhase::Disabled;
                if (move + std::abs(rotation) + tilt_rotation > 1e-9
                    && npc_sample_blocked(npc, {*ground,
                        candidate.heading_rad * 180.0 / std::numbers::pi}, false, &candidate,
                        external_motion)) {
                    clear = false; break;
                }
                if (index == samples) after.center_up_m = candidate.center_up_m;
            }
            if (!clear) {
                after = before;
                npc.reaction.offset_enu_m = {
                    before.center_enu.east_m - nominal_after.center_enu.east_m,
                    before.center_enu.north_m - nominal_after.center_enu.north_m};
                npc.reaction.heading_offset_rad = signed_heading_delta(
                    before.heading_rad, nominal_after.heading_rad);
                npc.reaction.velocity_enu_mps = {};
                npc.reaction.heading_rate_rad_s = 0.0;
                npc.reaction.return_speed_mps = 0.0;
                npc.reaction.return_yaw_speed_rad_s = 0.0;
                npc.reaction.reset_return_path();
                npc.tumble = previous_tumble;
                npc.tumble.stop_motion();
                apply_tumble_pose();
            }
        }
        npc.reaction.finish_recovery();
        npc.reaction.publish(*npc.pending, physics_.get_last_collision_contacts());
        auto& motion = current->collision_proxy;
        motion.linear_velocity_enu_mps = {
            (after.center_enu.east_m - before.center_enu.east_m) / dt_seconds,
            (after.center_enu.north_m - before.center_enu.north_m) / dt_seconds};
        motion.heading_rate_rad_s = signed_heading_delta(
            after.heading_rad, before.heading_rad) / dt_seconds;
        npc.pending->collision_proxy.linear_velocity_enu_mps = motion.linear_velocity_enu_mps;
        npc.pending->collision_proxy.heading_rate_rad_s = motion.heading_rate_rad_s;
        npc.nominal_pending->collision_proxy.linear_velocity_enu_mps =
            npc.reaction.nominal_velocity_enu_mps;
        npc.nominal_pending->collision_proxy.heading_rate_rad_s =
            npc.reaction.nominal_heading_rate_rad_s;
    }
}

void SimulationHost::finish_runtime_impact_contacts(
    const std::vector<simcore_host::CollisionContact>& contacts)
{
    for (auto& entity : runtime_entities_) {
        const bool hit = std::any_of(contacts.begin(), contacts.end(), [&](const auto& contact) {
            return contact.collider_id == entity.collision_proxy.proxy_id
                && contact.accumulated_normal_impulse_n_s > 0.0;
        });
        if (!hit) continue;
        if (auto npc = std::find_if(lane_npcs_.begin(), lane_npcs_.end(),
                [&](const auto& value) { return value.entity_id == entity.entity_id; }); npc != lane_npcs_.end()) {
            // Consume this tick's solved impulses before its WorldState, rather
            // than waiting for movement to stop or for the next prepare pass.
            npc->reaction.update(entity.collision_proxy, 0.0, true, npc->tumble.settled());
            npc->reaction.publish(entity, contacts);
            if (!npc->reaction.recovery.allows_driving()) entity.turn_indicator = 0;
        } else if (auto pedestrian = std::find_if(pedestrians_.begin(), pedestrians_.end(),
                [&](const auto& value) { return value.entity_id == entity.entity_id; }); pedestrian != pedestrians_.end()) {
            pedestrian->reaction.update(entity.collision_proxy, 0.0, true, pedestrian->body.settled());
            pedestrian->reaction.publish(entity, contacts);
            const auto center = shape_center(entity.collision_proxy.shape);
            const double center_up = std::visit([](const auto& shape) { return shape.center_up_m; }, entity.collision_proxy.shape);
            const auto ground = supported_point(*config_.ground_query, {center.east_m, center.north_m,
                pedestrian->start.up_m});
            if (ground) {
                simcore_host::PedestrianImpactGeometry geometry;
                const auto strongest = std::max_element(contacts.begin(), contacts.end(), [&](const auto& a, const auto& b) {
                    const double ja = a.collider_id == entity.collision_proxy.proxy_id ? a.accumulated_normal_impulse_n_s : -1.0;
                    const double jb = b.collider_id == entity.collision_proxy.proxy_id ? b.accumulated_normal_impulse_n_s : -1.0;
                    return ja < jb;
                });
                double closest = std::numeric_limits<double>::infinity();
                if (strongest != contacts.end()) {
                    const auto parameters = simcore_host::make_player_vehicle_parameters(
                        config_.vehicle_parameters, active_vehicle_class_);
                    for (const auto& vehicle : pedestrian_vehicle_surfaces(
                            physics_.get_state(), runtime_entities_, parameters)) {
                        const double east = strongest->contact_point_enu.east_m - vehicle.shape.center_enu.east_m;
                        const double north = strongest->contact_point_enu.north_m - vehicle.shape.center_enu.north_m;
                        const double forward = east * std::sin(vehicle.shape.heading_rad) + north * std::cos(vehicle.shape.heading_rad);
                        const double side = east * std::cos(vehicle.shape.heading_rad) - north * std::sin(vehicle.shape.heading_rad);
                        const double distance = std::hypot(std::max(0.0, std::abs(forward) - vehicle.shape.half_length_m),
                            std::max(0.0, std::abs(side) - vehicle.shape.half_width_m));
                        if (distance > 0.75 || distance >= closest) continue;
                        closest = distance;
                        const bool bumper = std::abs(forward) / vehicle.shape.half_length_m
                            >= std::abs(side) / vehicle.shape.half_width_m;
                        geometry.contact_up_m = vehicle.shape.center_up_m - vehicle.shape.half_height_m
                            + (bumper ? 0.25 : 0.40) * 2.0 * vehicle.shape.half_height_m;
                    }
                }
                pedestrian->body.contact(entity.collision_event_sequence, entity.last_impact_impulse_n_s,
                    entity.impact_direction_enu, center_up, ground->up_m, geometry);
                if (pedestrian->body.downed()) apply_pedestrian_body_state(entity, pedestrian->body);
            }
        }
    }
}

void SimulationHost::rebuild_pedestrians()
{
    pedestrians_.clear();
    std::erase_if(runtime_entities_, [](const auto& entity) {
        return entity.collision_proxy.proxy_id.starts_with("pedestrian-");
    });
    if (!config_.traffic_network || !config_.ground_query
        || config_.traffic_network->source_map_checksum != config_.map_package_checksum) return;

    using SignalKey = std::pair<std::uint32_t, std::uint32_t>;
    std::map<SignalKey, std::vector<const simcore_host::TrafficSignal*>> crossings;
    for (const auto& signal : config_.traffic_network->signals) {
        if (signal.kind == simcore_host::TrafficSignalKind::Pedestrian) {
            crossings[{signal.controller_id, signal.group_id}].push_back(&signal);
        }
    }

    std::uint32_t next_id = kFirstPedestrianId;
    for (auto& [identity, heads] : crossings) {
        if (heads.size() != 2) continue; // Loader already rejects this; remain fail-closed.
        std::sort(heads.begin(), heads.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->id < rhs->id;
        });
        const double dx = heads[1]->position_enu.east_m - heads[0]->position_enu.east_m;
        const double dy = heads[1]->position_enu.north_m - heads[0]->position_enu.north_m;
        const double length = std::hypot(dx, dy);
        if (length < 2.0) continue;
        const double offset_east = -dy / length * kPedestrianLaneOffsetM;
        const double offset_north = dx / length * kPedestrianLaneOffsetM;
        for (int side : {1, -1}) {
            PedestrianRuntime pedestrian;
            pedestrian.entity_id = next_id++;
            pedestrian.body.configure(pedestrian.entity_id);
            pedestrian.controller_id = identity.first;
            pedestrian.group_id = identity.second;
            pedestrian.start = heads[0]->position_enu;
            pedestrian.end = heads[1]->position_enu;
            pedestrian.start.east_m += offset_east * side;
            pedestrian.start.north_m += offset_north * side;
            pedestrian.end.east_m += offset_east * side;
            pedestrian.end.north_m += offset_north * side;
            pedestrian.progress = side > 0 ? 0.0 : 1.0;
            pedestrian.direction = side > 0 ? 1 : -1;
            const auto authored = pedestrian.progress == 0.0
                ? pedestrian.start : pedestrian.end;
            const auto ground = supported_point(*config_.ground_query, authored);
            if (!ground) continue;
            runtime_entities_.push_back(pedestrian_entity(pedestrian.entity_id, *ground,
                std::atan2(dx * pedestrian.direction, dy * pedestrian.direction)));
            pedestrians_.push_back(std::move(pedestrian));
        }
    }
    std::cout << "[Pedestrian] crossings=" << crossings.size()
              << " entities=" << pedestrians_.size() << "\n";
}

void SimulationHost::prepare_pedestrians(double dt_seconds, Clock::time_point now)
{
    if (!config_.traffic_network || !config_.ground_query) return;
    const bool enabled = lifecycle_active_ && !estop_latched_
        && make_health_snapshot(now).status == simcore_host::HealthStatus::Active;
    const auto signals = current_traffic_signals(enabled);

    for (auto& pedestrian : pedestrians_) {
        pedestrian.pending.reset();
        pedestrian.nominal_pending.reset();
        auto current = std::find_if(runtime_entities_.begin(), runtime_entities_.end(),
            [&](const auto& entity) { return entity.entity_id == pedestrian.entity_id; });
        if (current == runtime_entities_.end()) continue;
        if (!enabled) {
            current->collision_proxy.linear_velocity_enu_mps = {};
            pedestrian.reaction.nominal_velocity_enu_mps = {};
            continue;
        }
        pedestrian.reaction.update(current->collision_proxy, dt_seconds, enabled,
            pedestrian.body.settled()
                || pedestrian.reaction.recovery.phase() == simcore_host::ImpactRecoveryPhase::Recovering);
        pedestrian.reaction.publish(*current, physics_.get_last_collision_contacts());

        if (!pedestrian.body.downed()) {
            // Mild contacts retain the capsule, but their impulse episode still
            // expires. Otherwise old resting pressure can trigger a delayed fall.
            const double standing_ground = std::visit([](const auto& shape) {
                return shape.center_up_m - shape.half_height_m;
            }, current->collision_proxy.shape);
            pedestrian.body.tick(dt_seconds, standing_ground, false);
            if (!pedestrian.body.ready_to_walk()) {
                current->collision_proxy.linear_velocity_enu_mps = {};
                pedestrian.reaction.nominal_velocity_enu_mps = {};
                continue; // Keep the authoritative anchor still during the get-up/balance pose.
            }
        }

        if (pedestrian.body.downed()) {
            const auto before_proxy = current->collision_proxy;
            const auto before_center = shape_center(before_proxy.shape);
            const auto old_body = pedestrian.body;
            const auto old_offset = pedestrian.reaction.offset_enu_m;
            const auto anchor = supported_point(*config_.ground_query, {
                pedestrian.start.east_m + (pedestrian.end.east_m - pedestrian.start.east_m) * pedestrian.progress,
                pedestrian.start.north_m + (pedestrian.end.north_m - pedestrian.start.north_m) * pedestrian.progress,
                pedestrian.start.up_m});
            if (!anchor) continue;
            pedestrian.nominal_pending = pedestrian_entity(pedestrian.entity_id, *anchor, current->heading_rad);
            pedestrian.reaction.nominal_velocity_enu_mps = {};
            pedestrian.reaction.nominal_heading_rate_rad_s = 0.0;
            // Airborne momentum is not ground friction; applying the common
            // road damping here repeatedly dragged a struck person into the car.
            pedestrian.reaction.integrate_offset(dt_seconds,
                pedestrian.body.airborne() ? 0.12 : simcore_host::RuntimeCollisionReaction::kDefaultVelocityDampingPerSecond);
            const simcore_host::CollisionVector2 target{anchor->east_m + pedestrian.reaction.offset_enu_m.east_m,
                anchor->north_m + pedestrian.reaction.offset_enu_m.north_m};
            const auto ground = supported_point(*config_.ground_query,
                {target.east_m, target.north_m, anchor->up_m});
            const bool recovering = pedestrian.reaction.recovery.phase() == simcore_host::ImpactRecoveryPhase::Recovering;
            if (ground) {
                const auto parameters = simcore_host::make_player_vehicle_parameters(
                    config_.vehicle_parameters, active_vehicle_class_);
                const auto vehicle_surfaces = pedestrian_vehicle_surfaces(
                    physics_.get_state(), runtime_entities_, parameters);
                pedestrian.body.tick(dt_seconds, ground->up_m, recovering, true, target, vehicle_surfaces);
            }
            pedestrian.pending = *pedestrian.nominal_pending;
            std::visit([&](auto& shape) { shape.center_enu = target; }, pedestrian.pending->collision_proxy.shape);
            apply_pedestrian_body_state(*pedestrian.pending, pedestrian.body);
            bool blocked = !ground;
            if (config_.collision_world) {
                const auto& candidate = pedestrian.pending->collision_proxy.shape;
                for (const auto& wall : config_.collision_world->static_colliders())
                    if (overlaps(wall.shape, candidate)) { blocked = true; break; }
            }
            // Physics handles impacts with moving finite bodies; only automatic
            // get-up/return is forbidden through an occupied corridor.
            if (recovering) {
                for (const auto& other : runtime_entities_) {
                    if (other.entity_id == pedestrian.entity_id) continue;
                    if (const auto* box = std::get_if<simcore_host::ObbPrism>(&other.collision_proxy.shape))
                        if (overlaps(*box, pedestrian.pending->collision_proxy.shape)) blocked = true;
                }
            }
            if (blocked) {
                pedestrian.body = old_body;
                pedestrian.reaction.offset_enu_m = old_offset;
                pedestrian.reaction.velocity_enu_mps = {};
                pedestrian.pending->collision_proxy = before_proxy;
                apply_pedestrian_body_state(*pedestrian.pending, pedestrian.body);
            }
            const auto accepted_center = shape_center(pedestrian.pending->collision_proxy.shape);
            const simcore_host::CollisionVector2 velocity{
                (accepted_center.east_m - before_center.east_m) / dt_seconds,
                (accepted_center.north_m - before_center.north_m) / dt_seconds};
            current->collision_proxy.linear_velocity_enu_mps = velocity;
            pedestrian.pending->collision_proxy.linear_velocity_enu_mps = velocity;
            pedestrian.nominal_pending->collision_proxy.linear_velocity_enu_mps = {};
            if (!pedestrian.body.downed()) pedestrian.reaction.finish_recovery();
            pedestrian.reaction.publish(*pedestrian.pending, physics_.get_last_collision_contacts());
            continue;
        }

        const double length = std::hypot(
            pedestrian.end.east_m - pedestrian.start.east_m,
            pedestrian.end.north_m - pedestrian.start.north_m);
        const auto profile = simcore_host::pedestrian_profile(pedestrian.entity_id);
        const double required_walk_seconds = length / profile.walking_speed_mps
            + kPedestrianCrossingClearanceSeconds;
        const bool can_begin_crossing = std::any_of(signals.begin(), signals.end(), [&](const auto& signal) {
            return signal.kind == simcore_host::TrafficSignalKind::Pedestrian
                && signal.controller_id == pedestrian.controller_id
                && signal.group_id == pedestrian.group_id
                && signal.aspect == simcore_host::SignalAspect::Green
                // Zero on GREEN means the plan never changes this aspect.
                && (signal.remaining_seconds == 0.0
                    || signal.remaining_seconds >= required_walk_seconds);
        });
        // A new trip must fit wholly inside WALK, including an opposing return
        // after arrival. Someone already crossing still clears the road on RED.
        const bool advance_crossing = pedestrian.reaction.recovery.allows_driving()
            && (pedestrian.crossing || can_begin_crossing);
        const double next_progress = advance_crossing
            ? std::clamp(
                pedestrian.progress + pedestrian.direction * profile.walking_speed_mps
                    * dt_seconds / length,
                0.0, 1.0)
            : pedestrian.progress;
        simcore_host::GroundPointEnu authored{
            pedestrian.start.east_m
                + (pedestrian.end.east_m - pedestrian.start.east_m) * next_progress,
            pedestrian.start.north_m
                + (pedestrian.end.north_m - pedestrian.start.north_m) * next_progress,
            pedestrian.start.up_m
                + (pedestrian.end.up_m - pedestrian.start.up_m) * next_progress};
        const auto ground = supported_point(*config_.ground_query, authored);
        if (!ground) {
            pedestrian.crossing = false;
            current->collision_proxy.linear_velocity_enu_mps = {};
            continue;
        }

        const auto& before = std::get<simcore_host::VerticalCapsule>(
            current->collision_proxy.shape);
        const auto old_offset = pedestrian.reaction.offset_enu_m;
        pedestrian.nominal_pending = pedestrian_entity(pedestrian.entity_id, *ground,
            std::atan2((pedestrian.end.east_m - pedestrian.start.east_m) * pedestrian.direction,
                (pedestrian.end.north_m - pedestrian.start.north_m) * pedestrian.direction));
        pedestrian.reaction.publish(*pedestrian.nominal_pending,
            physics_.get_last_collision_contacts());
        const auto& nominal_after = std::get<simcore_host::VerticalCapsule>(
            pedestrian.nominal_pending->collision_proxy.shape);
        const simcore_host::CollisionVector2 nominal_before{
            before.center_enu.east_m - old_offset.east_m,
            before.center_enu.north_m - old_offset.north_m};
        pedestrian.reaction.nominal_velocity_enu_mps = {
            (nominal_after.center_enu.east_m - nominal_before.east_m) / dt_seconds,
            (nominal_after.center_enu.north_m - nominal_before.north_m) / dt_seconds};

        pedestrian.reaction.integrate_offset(dt_seconds);
        clamp_vector_magnitude(
            pedestrian.reaction.velocity_enu_mps,
            kPedestrianMaximumReactionSpeedMps);

        pedestrian.pending = *pedestrian.nominal_pending;
        auto& after = std::get<simcore_host::VerticalCapsule>(
            pedestrian.pending->collision_proxy.shape);
        after.center_enu.east_m += pedestrian.reaction.offset_enu_m.east_m;
        after.center_enu.north_m += pedestrian.reaction.offset_enu_m.north_m;
        const auto freeze_recovery_motion = [&] {
            pedestrian.reaction.offset_enu_m = old_offset;
            pedestrian.reaction.velocity_enu_mps = {};
            pedestrian.reaction.nominal_velocity_enu_mps = {};
            pedestrian.reaction.return_speed_mps = 0.0;
            pedestrian.reaction.return_yaw_speed_rad_s = 0.0;
            current->collision_proxy.linear_velocity_enu_mps = {};
            pedestrian.pending->collision_proxy = current->collision_proxy;
            // Keep a stationary pending proxy so external collision impulses
            // still reach advance_runtime_entities() while recovery is blocked.
            auto& frozen_anchor = std::get<simcore_host::VerticalCapsule>(
                pedestrian.nominal_pending->collision_proxy.shape);
            frozen_anchor = before;
            frozen_anchor.center_enu.east_m -= old_offset.east_m;
            frozen_anchor.center_enu.north_m -= old_offset.north_m;
            pedestrian.nominal_pending->collision_proxy.linear_velocity_enu_mps = {};
        };
        const auto displaced_ground = supported_point(
            *config_.ground_query,
            {after.center_enu.east_m, after.center_enu.north_m,
             nominal_after.center_up_m - profile.half_height_m});
        if (displaced_ground) {
            after.center_up_m = displaced_ground->up_m + profile.half_height_m;
        } else {
            freeze_recovery_motion();
            continue; // Keep the displaced pose/progress; never teleport to the crossing.
        }
        if (!pedestrian.reaction.recovery.allows_driving()
            || std::hypot(old_offset.east_m, old_offset.north_m) > 0.001) {
            const auto ego = physics_.get_state();
            const auto params = simcore_host::make_player_vehicle_parameters(
                config_.vehicle_parameters, active_vehicle_class_);
            const auto player_shape = ego_shape(ego, params);
            const auto blocked = [&](const simcore_host::VerticalCapsule& candidate) {
                const auto overlaps_proxy = [&](const simcore_host::KinematicProxyShape& shape) {
                    return std::visit([&](const auto& obstacle) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(obstacle)>,
                                simcore_host::ObbPrism>) {
                            return simcore_host::intersect_obb_vertical_capsule(
                                obstacle, candidate).has_value();
                        } else {
                            return std::abs(obstacle.center_up_m - candidate.center_up_m)
                                    < obstacle.half_height_m + candidate.half_height_m
                                && std::hypot(obstacle.center_enu.east_m - candidate.center_enu.east_m,
                                              obstacle.center_enu.north_m - candidate.center_enu.north_m)
                                    < obstacle.radius_m + candidate.radius_m;
                        }
                    }, shape);
                };
                const auto phase = pedestrian.reaction.recovery.phase();
                const bool external_motion = phase == simcore_host::ImpactRecoveryPhase::Settling
                    || phase == simcore_host::ImpactRecoveryPhase::Disabled;
                if (!external_motion
                    && simcore_host::intersect_obb_vertical_capsule(player_shape, candidate)) return true;
                if (config_.collision_world) {
                    for (const auto& collider : config_.collision_world->static_colliders()) {
                        if (simcore_host::intersect_obb_vertical_capsule(collider.shape, candidate))
                            return true;
                    }
                }
                for (const auto& other : runtime_entities_) {
                    if (other.entity_id != pedestrian.entity_id
                        && overlaps_proxy(other.collision_proxy.shape)) return true;
                }
                for (const auto& structure : structure_damage_.collision_proxies()) {
                    if (overlaps_proxy(structure.shape)) return true;
                }
                for (const auto& other : lane_npcs_) {
                    if (other.pending && overlaps_proxy(other.pending->collision_proxy.shape)) return true;
                }
                for (const auto& other : pedestrians_) {
                    if (other.entity_id != pedestrian.entity_id && other.pending
                        && overlaps_proxy(other.pending->collision_proxy.shape)) return true;
                }
                return false;
            };
            const double move = std::hypot(after.center_enu.east_m - before.center_enu.east_m,
                                          after.center_enu.north_m - before.center_enu.north_m);
            const int samples = std::max(1, static_cast<int>(std::ceil(move / 0.1)));
            bool clear = true;
            for (int index = 1; index <= samples; ++index) {
                const double t = static_cast<double>(index) / samples;
                auto candidate = before;
                candidate.center_enu.east_m += (after.center_enu.east_m - before.center_enu.east_m) * t;
                candidate.center_enu.north_m += (after.center_enu.north_m - before.center_enu.north_m) * t;
                const auto candidate_ground = supported_point(*config_.ground_query,
                    {candidate.center_enu.east_m, candidate.center_enu.north_m,
                     before.center_up_m - profile.half_height_m});
                if (!candidate_ground) { clear = false; break; }
                candidate.center_up_m = candidate_ground->up_m + profile.half_height_m;
                if (blocked(candidate)) { clear = false; break; }
            }
            if (!clear) {
                freeze_recovery_motion();
                continue; // No progress or finish_recovery through an occupied return path.
            }
        }
        const auto& accepted_after = std::get<simcore_host::VerticalCapsule>(
            pedestrian.pending->collision_proxy.shape);
        pedestrian.reaction.finish_recovery();
        pedestrian.reaction.publish(*pedestrian.pending,
            physics_.get_last_collision_contacts());
        const simcore_host::CollisionVector2 velocity{
            (accepted_after.center_enu.east_m - before.center_enu.east_m) / dt_seconds,
            (accepted_after.center_enu.north_m - before.center_enu.north_m) / dt_seconds};
        current->collision_proxy.linear_velocity_enu_mps = velocity;
        pedestrian.pending->collision_proxy.linear_velocity_enu_mps = velocity;
        pedestrian.nominal_pending->collision_proxy.linear_velocity_enu_mps =
            pedestrian.reaction.nominal_velocity_enu_mps;
        pedestrian.progress = next_progress;
        if (advance_crossing) {
            pedestrian.crossing = true;
        }
        if (advance_crossing
            && (next_progress <= 0.0 || next_progress >= 1.0)) {
            pedestrian.crossing = false;
            pedestrian.direction = -pedestrian.direction;
        }
    }
}
