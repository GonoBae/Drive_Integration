#include "simulation_host.hpp"

#include <algorithm>
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
constexpr double kPedestrianSpeedMps = 1.35;
constexpr double kPedestrianRadiusM = 0.35;
constexpr double kPedestrianHalfHeightM = 0.9;
constexpr double kPedestrianLaneOffsetM = 0.45;
constexpr double kNpcMassKg = 1500.0;
constexpr double kNpcYawInertiaKgM2 = 2600.0;
constexpr double kNpcMaximumReactionSpeedMps = 30.0;
constexpr double kPedestrianMassKg = 80.0;
constexpr double kPedestrianMaximumReactionSpeedMps = 15.0;
constexpr double kReactionVelocityDampingPerSecond = 2.5;
constexpr double kReactionOffsetRecoveryPerSecond = 0.8;
constexpr double kReactionHeadingRecoveryPerSecond = 1.2;
constexpr double kNpcMaximumReactionOffsetM = 8.0;
constexpr double kPedestrianMaximumReactionOffsetM = 6.0;
constexpr std::uint32_t kReactionHoldTicks = 18;

simcore_host::ObbPrism npc_shape(const simcore_host::NpcLaneSample& sample)
{
    // Baked ground is the wheel plane. Collision box bottom has 10cm clearance.
    return {{sample.position_enu.east_m, sample.position_enu.north_m},
            sample.position_enu.up_m + 0.85,
            sample.heading_deg * std::numbers::pi / 180.0, 2.2, 1.0, 0.75};
}

simcore_host::RuntimeEntityState npc_entity(
    std::uint32_t entity_id, const simcore_host::NpcLaneSample& sample)
{
    return {entity_id, simcore_host::RuntimeEntityKind::NpcVehicle,
            {"lane-npc-" + std::to_string(entity_id), npc_shape(sample), {}, 0.0,
             {0.8, 0.08}, kNpcMassKg, kNpcYawInertiaKgM2,
             kNpcMaximumReactionSpeedMps}};
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
    std::uint32_t entity_id, const simcore_host::GroundPointEnu& ground_point)
{
    const simcore_host::VerticalCapsule capsule{
        {ground_point.east_m, ground_point.north_m},
        ground_point.up_m + kPedestrianHalfHeightM,
        kPedestrianRadiusM, kPedestrianHalfHeightM};
    return {entity_id, simcore_host::RuntimeEntityKind::Pedestrian,
            {"pedestrian-" + std::to_string(entity_id), capsule, {}, 0.0,
             {0.75, 0.0}, kPedestrianMassKg, 0.0,
             kPedestrianMaximumReactionSpeedMps}};
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
    std::erase_if(runtime_entities_, [](const auto& entity) {
        return entity.collision_proxy.proxy_id.starts_with("lane-npc-");
    });
    if (config_.npc_route.empty()) return;
    if (!config_.traffic_network || !config_.ground_query
        || config_.traffic_network->source_map_checksum != config_.map_package_checksum) return;

    const std::vector<const std::vector<std::uint32_t>*> routes =
        config_.npc_alternate_route.empty()
            ? std::vector<const std::vector<std::uint32_t>*>{&config_.npc_route}
            : std::vector<const std::vector<std::uint32_t>*>{
                &config_.npc_route, &config_.npc_alternate_route};
    simcore_host::NpcLaneFollowerConfig settings;
    settings.max_speed_mps = config_.npc_max_speed_mps;

    for (std::uint32_t index = 0; index < config_.npc_count; ++index) {
        try {
            const auto* route = routes[index % routes.size()];
            const double requested_offset = config_.npc_start_offset_m
                + static_cast<double>(index / routes.size()) * config_.npc_spacing_m;
            LaneNpcRuntime runtime;
            runtime.entity_id = kFirstLaneNpcId + index;
            runtime.follower = simcore_host::NpcLaneFollower(settings);
            runtime.follower.rebuild(*config_.traffic_network, *config_.ground_query,
                                     *route, config_.npc_route_loop, 0.0);
            runtime.start_offset_m = config_.npc_route_loop
                ? std::fmod(requested_offset, runtime.follower.route_length_m())
                : requested_offset;
            runtime.follower.reset(runtime.start_offset_m);
            lane_npcs_.push_back(std::move(runtime));
        } catch (const std::exception& error) {
            std::cerr << "[NPC] entity=" << (kFirstLaneNpcId + index)
                      << " disabled: " << error.what() << "\n";
        }
    }

    for (auto& npc : lane_npcs_) {
        if (!lane_npc_blocked_distance(npc, 0.0)) {
            if (const auto anchor = npc.follower.sample_ahead(0.0)) {
                runtime_entities_.push_back(npc_entity(npc.entity_id, *anchor));
            }
        }
        std::cout << "[NPC] route ready entity=" << npc.entity_id
                  << " length_m=" << npc.follower.route_length_m()
                  << " start_m=" << npc.start_offset_m
                  << " max_speed_mps=" << config_.npc_max_speed_mps << "\n";
    }
}

std::optional<double> SimulationHost::lane_npc_blocked_distance(
    const LaneNpcRuntime& npc, double lookahead_m) const
{
    const auto ego = physics_.get_state();
    const auto& params = config_.vehicle_parameters;
    const simcore_host::ObbPrism ego_shape{
        {ego.position_enu.x, ego.position_enu.y},
        ego.position_enu.z - params.cg_height_m + 0.85,
        ego.heading * std::numbers::pi / 180.0,
        params.wheelbase_m * 0.5 + 0.8,
        std::max(params.front_track_m, params.rear_track_m) * 0.5 + 0.15, 0.75};
    for (double distance = 0.0; distance <= lookahead_m + 1e-9; distance += kScanStepM) {
        const auto sample = npc.follower.sample_ahead(distance);
        if (!sample) return distance + npc.follower.config().front_extent_m;
        const auto candidate = npc_shape(*sample);
        bool blocked = overlaps(candidate, ego_shape);
        if (!blocked && config_.collision_world) {
            for (const auto& collider : config_.collision_world->static_colliders()) {
                if (overlaps(candidate, collider.shape)) { blocked = true; break; }
            }
        }
        if (!blocked) {
            for (const auto& entity : runtime_entities_) {
                if (entity.entity_id != npc.entity_id
                    && overlaps(candidate, entity.collision_proxy.shape)) {
                    blocked = true;
                    break;
                }
            }
        }
        if (blocked) {
            return std::max(0.0, distance - kScanStepM)
                + npc.follower.config().front_extent_m;
        }
    }
    return std::nullopt;
}

void SimulationHost::prepare_lane_npc(double dt_seconds, Clock::time_point now)
{
    const bool enabled = lifecycle_active_ && !estop_latched_
        && make_health_snapshot(now).status == simcore_host::HealthStatus::Active;
    const auto signals = config_.traffic_network
        ? config_.traffic_network->signals_at(
            simulation_clock_.simulation_time_ns(), enabled)
        : std::vector<simcore_host::TrafficSignalSnapshot>{};

    for (auto& npc : lane_npcs_) {
        npc.pending.reset();
        npc.nominal_pending.reset();
        auto current = std::find_if(runtime_entities_.begin(), runtime_entities_.end(),
            [&](const auto& entity) { return entity.entity_id == npc.entity_id; });
        if (current == runtime_entities_.end()) {
            if (lane_npc_blocked_distance(npc, 0.0)) continue;
            const auto anchor = npc.follower.sample_ahead(0.0);
            if (!anchor) continue;
            runtime_entities_.push_back(npc_entity(npc.entity_id, *anchor));
            current = std::prev(runtime_entities_.end());
        }

        const auto& settings = npc.follower.config();
        const double speed = npc.follower.state().speed_mps;
        const double lookahead = std::ceil((speed * speed / (2.0 * settings.braking_mps2)
            + speed * dt_seconds + settings.front_extent_m + settings.stop_margin_m + 2.0)
            / kScanStepM) * kScanStepM;
        const auto& next = npc.follower.step(
            dt_seconds, signals, enabled, lane_npc_blocked_distance(npc, lookahead));
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
        npc.nominal_pending = npc_entity(npc.entity_id, next);
        const auto& nominal_after = std::get<simcore_host::ObbPrism>(
            npc.nominal_pending->collision_proxy.shape);
        const simcore_host::CollisionVector2 nominal_before{
            before.center_enu.east_m - old_offset.east_m,
            before.center_enu.north_m - old_offset.north_m};
        npc.reaction.nominal_velocity_enu_mps = {
            (nominal_after.center_enu.east_m - nominal_before.east_m) / dt_seconds,
            (nominal_after.center_enu.north_m - nominal_before.north_m) / dt_seconds};
        npc.reaction.nominal_heading_rate_rad_s = signed_heading_delta(
            nominal_after.heading_rad,
            before.heading_rad - old_heading_offset) / dt_seconds;

        const double velocity_decay = std::exp(
            -kReactionVelocityDampingPerSecond * dt_seconds);
        npc.reaction.velocity_enu_mps.east_m *= velocity_decay;
        npc.reaction.velocity_enu_mps.north_m *= velocity_decay;
        npc.reaction.heading_rate_rad_s *= velocity_decay;
        npc.reaction.offset_enu_m.east_m +=
            npc.reaction.velocity_enu_mps.east_m * dt_seconds;
        npc.reaction.offset_enu_m.north_m +=
            npc.reaction.velocity_enu_mps.north_m * dt_seconds;
        npc.reaction.heading_offset_rad = signed_heading_delta(
            npc.reaction.heading_offset_rad
                + npc.reaction.heading_rate_rad_s * dt_seconds,
            0.0);
        if (npc.reaction.hold_ticks > 0) {
            --npc.reaction.hold_ticks;
        } else {
            const double offset_decay = std::exp(
                -kReactionOffsetRecoveryPerSecond * dt_seconds);
            const double heading_decay = std::exp(
                -kReactionHeadingRecoveryPerSecond * dt_seconds);
            npc.reaction.offset_enu_m.east_m *= offset_decay;
            npc.reaction.offset_enu_m.north_m *= offset_decay;
            npc.reaction.heading_offset_rad *= heading_decay;
        }
        clamp_vector_magnitude(
            npc.reaction.velocity_enu_mps, kNpcMaximumReactionSpeedMps);
        clamp_vector_magnitude(
            npc.reaction.offset_enu_m, kNpcMaximumReactionOffsetM);

        npc.pending = *npc.nominal_pending;
        auto& after = std::get<simcore_host::ObbPrism>(npc.pending->collision_proxy.shape);
        after.center_enu.east_m += npc.reaction.offset_enu_m.east_m;
        after.center_enu.north_m += npc.reaction.offset_enu_m.north_m;
        after.heading_rad = std::remainder(
            after.heading_rad + npc.reaction.heading_offset_rad,
            2.0 * std::numbers::pi);
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
            runtime_entities_.push_back(pedestrian_entity(pedestrian.entity_id, *ground));
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
    const auto signals = config_.traffic_network->signals_at(
        simulation_clock_.simulation_time_ns(), enabled);

    for (auto& pedestrian : pedestrians_) {
        pedestrian.pending.reset();
        pedestrian.nominal_pending.reset();
        auto current = std::find_if(runtime_entities_.begin(), runtime_entities_.end(),
            [&](const auto& entity) { return entity.entity_id == pedestrian.entity_id; });
        if (current == runtime_entities_.end()) continue;
        const bool walk = std::any_of(signals.begin(), signals.end(), [&](const auto& signal) {
            return signal.kind == simcore_host::TrafficSignalKind::Pedestrian
                && signal.controller_id == pedestrian.controller_id
                && signal.group_id == pedestrian.group_id
                && signal.aspect == simcore_host::SignalAspect::Green;
        });
        if (!enabled) {
            current->collision_proxy.linear_velocity_enu_mps = {};
            pedestrian.reaction.nominal_velocity_enu_mps = {};
            continue;
        }

        const double length = std::hypot(
            pedestrian.end.east_m - pedestrian.start.east_m,
            pedestrian.end.north_m - pedestrian.start.north_m);
        const bool advance_crossing = pedestrian.reaction.hold_ticks == 0
            && (pedestrian.crossing || walk);
        const double next_progress = advance_crossing
            ? std::clamp(
                pedestrian.progress + pedestrian.direction * kPedestrianSpeedMps
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
        pedestrian.nominal_pending = pedestrian_entity(pedestrian.entity_id, *ground);
        const auto& nominal_after = std::get<simcore_host::VerticalCapsule>(
            pedestrian.nominal_pending->collision_proxy.shape);
        const simcore_host::CollisionVector2 nominal_before{
            before.center_enu.east_m - old_offset.east_m,
            before.center_enu.north_m - old_offset.north_m};
        pedestrian.reaction.nominal_velocity_enu_mps = {
            (nominal_after.center_enu.east_m - nominal_before.east_m) / dt_seconds,
            (nominal_after.center_enu.north_m - nominal_before.north_m) / dt_seconds};

        const double velocity_decay = std::exp(
            -kReactionVelocityDampingPerSecond * dt_seconds);
        pedestrian.reaction.velocity_enu_mps.east_m *= velocity_decay;
        pedestrian.reaction.velocity_enu_mps.north_m *= velocity_decay;
        pedestrian.reaction.offset_enu_m.east_m +=
            pedestrian.reaction.velocity_enu_mps.east_m * dt_seconds;
        pedestrian.reaction.offset_enu_m.north_m +=
            pedestrian.reaction.velocity_enu_mps.north_m * dt_seconds;
        if (pedestrian.reaction.hold_ticks > 0) {
            --pedestrian.reaction.hold_ticks;
        } else {
            const double offset_decay = std::exp(
                -kReactionOffsetRecoveryPerSecond * dt_seconds);
            pedestrian.reaction.offset_enu_m.east_m *= offset_decay;
            pedestrian.reaction.offset_enu_m.north_m *= offset_decay;
        }
        clamp_vector_magnitude(
            pedestrian.reaction.velocity_enu_mps,
            kPedestrianMaximumReactionSpeedMps);
        clamp_vector_magnitude(
            pedestrian.reaction.offset_enu_m,
            kPedestrianMaximumReactionOffsetM);

        pedestrian.pending = *pedestrian.nominal_pending;
        auto& after = std::get<simcore_host::VerticalCapsule>(
            pedestrian.pending->collision_proxy.shape);
        after.center_enu.east_m += pedestrian.reaction.offset_enu_m.east_m;
        after.center_enu.north_m += pedestrian.reaction.offset_enu_m.north_m;
        const auto displaced_ground = supported_point(
            *config_.ground_query,
            {after.center_enu.east_m, after.center_enu.north_m,
             nominal_after.center_up_m - kPedestrianHalfHeightM});
        if (displaced_ground) {
            after.center_up_m = displaced_ground->up_m + kPedestrianHalfHeightM;
        } else {
            pedestrian.reaction.offset_enu_m = {};
            pedestrian.reaction.velocity_enu_mps = {};
            pedestrian.pending = *pedestrian.nominal_pending;
        }
        const auto& accepted_after = std::get<simcore_host::VerticalCapsule>(
            pedestrian.pending->collision_proxy.shape);
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
