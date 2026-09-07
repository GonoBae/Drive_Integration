#include "simulation_host.hpp"

#include "player_vehicle_profile.hpp"
#include "simulation_host_session_detail.hpp"

#include <boost/asio/error.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

using simcore_host::simulation_host_session_detail::
    is_log_safe_identifier;
using simcore_host::simulation_host_session_detail::
    kMaxLifecycleIdentifierBytes;

constexpr std::size_t kMaxRuntimeEntities = 255;
constexpr double kPedestrianMaximumReactionSpeedMps = 60.0;

bool finite_vector(const simcore_host::CollisionVector2& vector)
{
    return std::isfinite(vector.east_m) && std::isfinite(vector.north_m);
}

std::size_t pedestrian_entity_count(
    const std::shared_ptr<const simcore_host::TrafficNetwork>& network)
{
    if (!network) return 0;
    std::set<std::pair<std::uint32_t, std::uint32_t>> groups;
    for (const auto& signal : network->signals) {
        if (signal.kind == simcore_host::TrafficSignalKind::Pedestrian) {
            groups.emplace(signal.controller_id, signal.group_id);
        }
    }
    return groups.size() * 2;
}

void validate_managed_runtime_slots(
    const std::vector<simcore_host::RuntimeEntityState>& entities,
    std::uint32_t npc_count,
    const std::shared_ptr<const simcore_host::TrafficNetwork>& network)
{
    const auto pedestrian_count = pedestrian_entity_count(network);
    if (entities.size() + npc_count + pedestrian_count > kMaxRuntimeEntities) {
        throw std::invalid_argument("managed traffic agents exceed the runtime entity wire limit");
    }
    for (const auto& entity : entities) {
        const bool reserved_npc = entity.entity_id >= 1001
            && entity.entity_id < 1001 + npc_count;
        const bool reserved_pedestrian = entity.entity_id >= 2001
            && entity.entity_id < 2001 + pedestrian_count;
        if (reserved_npc || reserved_pedestrian
            || entity.collision_proxy.proxy_id.starts_with("lane-npc-")
            || entity.collision_proxy.proxy_id.starts_with("pedestrian-")) {
            throw std::invalid_argument(
                "managed traffic agents require free 1001+/2001+ IDs and proxy identities");
        }
    }
}

double normalize_heading(double heading_rad)
{
    constexpr double full_turn = std::numbers::pi * 2.0;
    heading_rad = std::fmod(heading_rad, full_turn);
    return heading_rad < 0.0 ? heading_rad + full_turn : heading_rad;
}

void validate_and_order_runtime_entities(
    std::vector<simcore_host::RuntimeEntityState>& entities)
{
    if (entities.size() > kMaxRuntimeEntities) {
        throw std::invalid_argument(
            "runtime entity count exceeds the 255-entity wire limit");
    }
    std::sort(
        entities.begin(),
        entities.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.entity_id < rhs.entity_id;
        });

    std::unordered_set<std::string> proxy_ids;
    std::uint32_t previous_entity_id = 0;
    for (const auto& entity : entities) {
        if (entity.entity_id == 0 || entity.entity_id == 1
            || entity.entity_id == previous_entity_id) {
            throw std::invalid_argument(
                "runtime entity IDs must be unique, non-zero, and not Ego ID 1");
        }
        previous_entity_id = entity.entity_id;

        const auto& proxy = entity.collision_proxy;
        if (proxy.proxy_id.empty() || !proxy_ids.insert(proxy.proxy_id).second) {
            throw std::invalid_argument(
                "runtime collision proxy IDs must be non-empty and unique");
        }
        if (!finite_vector(proxy.linear_velocity_enu_mps)
            || !std::isfinite(entity.pitch_rad) || !std::isfinite(entity.roll_rad)
            || !std::isfinite(entity.pitch_rate_rad_s) || !std::isfinite(entity.roll_rate_rad_s)
            || !std::isfinite(proxy.heading_rate_rad_s)
            || !std::isfinite(proxy.material.friction)
            || !std::isfinite(proxy.material.restitution)
            || !std::isfinite(proxy.mass_kg)
            || !std::isfinite(proxy.yaw_inertia_kg_m2)
            || !std::isfinite(proxy.maximum_linear_speed_mps)
            || proxy.material.friction < 0.0
            || proxy.material.restitution < 0.0
            || proxy.material.restitution > 1.0
            || proxy.mass_kg < 0.0
            || proxy.yaw_inertia_kg_m2 < 0.0
            || proxy.maximum_linear_speed_mps < 0.0
            || (proxy.mass_kg == 0.0
                && (proxy.yaw_inertia_kg_m2 != 0.0
                    || proxy.maximum_linear_speed_mps != 0.0))
            || (proxy.mass_kg > 0.0
                && (proxy.mass_kg < 1.0 || proxy.mass_kg > 100'000.0
                    || proxy.maximum_linear_speed_mps <= 0.0))) {
            throw std::invalid_argument(
                "runtime collision proxy contains invalid motion or material");
        }

        std::visit(
            [&](const auto& shape) {
                using Shape = std::decay_t<decltype(shape)>;
                if (!finite_vector(shape.center_enu)
                    || !std::isfinite(shape.center_up_m)) {
                    throw std::invalid_argument(
                        "runtime collision proxy contains a non-finite center");
                }
                if constexpr (std::is_same_v<Shape, simcore_host::ObbPrism>) {
                    if ((entity.kind != simcore_host::RuntimeEntityKind::NpcVehicle
                            && !(entity.kind == simcore_host::RuntimeEntityKind::Pedestrian && entity.pedestrian_downed))
                        || !std::isfinite(shape.heading_rad)
                        || !std::isfinite(shape.half_length_m)
                        || !std::isfinite(shape.half_width_m)
                        || !std::isfinite(shape.half_height_m)
                        || shape.half_length_m <= 0.0
                        || shape.half_width_m <= 0.0
                        || shape.half_height_m <= 0.0
                        || (proxy.mass_kg > 0.0
                            && (proxy.yaw_inertia_kg_m2 < 1.0
                                || proxy.yaw_inertia_kg_m2 > 100'000'000.0))) {
                        throw std::invalid_argument(
                            "NPC/downed pedestrian runtime entity requires a valid OBB prism");
                    }
                } else {
                    if (entity.kind
                            != simcore_host::RuntimeEntityKind::Pedestrian
                        || !std::isfinite(shape.radius_m)
                        || !std::isfinite(shape.half_height_m)
                        || shape.radius_m <= 0.0
                        || shape.half_height_m < shape.radius_m
                        || proxy.yaw_inertia_kg_m2 != 0.0) {
                        throw std::invalid_argument(
                            "pedestrian runtime entity requires a valid vertical capsule");
                    }
                }
            },
            proxy.shape);
    }
}

} // namespace

SimulationHost::SimulationHost(boost::asio::io_context& ioc,
                               SimulationHostConfig config,
                               SimulationHostCallbacks callbacks)
    : config_(std::move(config))
    , base_vehicle_parameters_(config_.vehicle_parameters)
    , callbacks_(std::move(callbacks))
    , physics_(config_.origin_lat, config_.origin_lon, config_.origin_alt,
               config_.spawn_heading, config_.vehicle_parameters,
               config_.ground_query, config_.collision_world)
    , simulation_clock_(config_.physics_frequency_hz)
    , control_lease_(config_.command_timeout,
                     config_.max_command_queue_age,
                     config_.hard_command_timeout)
    , timer_(ioc)
{
    if (!is_log_safe_identifier(
            config_.source_id, kMaxLifecycleIdentifierBytes)
        || !is_log_safe_identifier(
            config_.map_package_checksum, kMaxLifecycleIdentifierBytes)
        || config_.map_package_checksum.empty()
        || config_.map_package_checksum == "unset") {
        throw std::invalid_argument(
            "SimulationHost requires printable source and verified map identities");
    }
    validate_and_order_runtime_entities(config_.runtime_entities);
    validate_managed_runtime_slots(config_.runtime_entities,
        config_.npc_route.empty() ? 0U : config_.npc_count, config_.traffic_network);
    if (config_.traffic_network
        && config_.traffic_network->source_map_checksum != config_.map_package_checksum) {
        throw std::invalid_argument("traffic network must match the verified map");
    }
    initial_runtime_entities_ = config_.runtime_entities;
    runtime_entities_ = initial_runtime_entities_;
    rebuild_structure_damage();
    rebuild_lane_npc();
    rebuild_pedestrians();
    last_logged_input_ = make_safe_stop_input();
    apply_vehicle_input(last_logged_input_);
}

SimulationHost::~SimulationHost()
{
    stop();
    lifetime_token_.reset();
}

void SimulationHost::start()
{
    if (running_) {
        return;
    }
    running_ = true;
    schedule_tick();
}

void SimulationHost::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;
    try {
        timer_.cancel();
    } catch (const boost::system::system_error& error) {
        std::cerr << "[Timing] timer cancellation failed: "
                  << error.what() << "\n";
    }
}

std::string SimulationHost::make_initial_world_state()
{
    return serialize_current_world_state(Clock::now());
}

std::string SimulationHost::serialize_current_world_state(Clock::time_point now)
{
    const auto health = make_health_snapshot(now);
    const auto& network = config_.traffic_network;
    const bool traffic_enabled = network && lifecycle_active_ && !estop_latched_
        && network->source_map_checksum == config_.map_package_checksum
        && health.status == simcore_host::HealthStatus::Active;
    return simcore_host::serialize_world_state_envelope(
        physics_.get_state(), runtime_entities_, make_metadata(), health,
        current_traffic_signals(traffic_enabled),
        network && !network->signals.empty()
            ? std::string_view(network->checksum) : std::string_view{},
        structure_damage_.snapshots(), active_vehicle_class_);
}

std::vector<simcore_host::TrafficSignalSnapshot> SimulationHost::current_traffic_signals(bool enabled) const
{
    auto signals = config_.traffic_network
        ? config_.traffic_network->signals_at(simulation_clock_.simulation_time_ns(), enabled)
        : std::vector<simcore_host::TrafficSignalSnapshot>{};
    structure_damage_.apply_signal_faults(signals);
    return signals;
}

void SimulationHost::rebuild_structure_damage()
{
    const auto* network = config_.traffic_network
        && config_.traffic_network->source_map_checksum == config_.map_package_checksum
        ? config_.traffic_network.get() : nullptr;
    structure_damage_.rebuild(config_.collision_world
        ? config_.collision_world->static_colliders() : std::vector<simcore_host::StaticObbCollider>{}, network);
    for (const auto& pole : structure_damage_.collision_proxies()) {
        for (const auto& entity : initial_runtime_entities_) {
            if (pole.proxy_id == entity.collision_proxy.proxy_id)
                throw std::invalid_argument("runtime entity uses a reserved signal pole collision ID");
        }
    }
}

void SimulationHost::queue_traffic_network_reload(
    std::shared_ptr<const simcore_host::TrafficNetwork> network)
{
    if (!network) return;
    std::lock_guard lock(pending_traffic_mutex_);
    pending_traffic_network_ = std::move(network);
}

void SimulationHost::apply_pending_traffic_network_reload()
{
    std::lock_guard lock(pending_traffic_mutex_);
    if (!pending_traffic_network_
        || pending_traffic_network_->source_map_checksum != config_.map_package_checksum) return;
    if (!config_.traffic_network
        || config_.traffic_network->checksum != pending_traffic_network_->checksum) {
        if (config_.physics_replay) {
            config_.physics_replay->invalidate("traffic_network_reloaded");
        }
        config_.traffic_network = std::move(pending_traffic_network_);
        rebuild_structure_damage();
        rebuild_lane_npc();
        rebuild_pedestrians();
        std::cout << "[Traffic] applied verified network checksum="
                  << config_.traffic_network->checksum << " lanes="
                  << config_.traffic_network->lanes.size() << "\n";
    }
    pending_traffic_network_.reset();
}

VehicleInput SimulationHost::make_safe_stop_input()
{
    VehicleInput input;
    input.brake = 1.f;
    input.handbrake = true;
    return input;
}

void SimulationHost::queue_map_package_reload(
    simcore_host::RuntimeMapPackage package,
    std::optional<std::vector<simcore_host::RuntimeEntityState>>
        replacement_runtime_entities)
{
    if (package.collision_checksum.empty()
        || !package.ground_query || !package.collision_world) {
        throw std::invalid_argument(
            "Queued MapPackage reload must be completely verified");
    }
    if (!is_log_safe_identifier(package.map_id, 128)
        || !is_log_safe_identifier(
            package.collision_checksum, kMaxLifecycleIdentifierBytes)) {
        throw std::invalid_argument(
            "MapPackage reload identities must be printable ASCII and bounded");
    }
    if (replacement_runtime_entities) {
        validate_and_order_runtime_entities(*replacement_runtime_entities);
        validate_managed_runtime_slots(*replacement_runtime_entities,
            config_.npc_route.empty() ? 0U : config_.npc_count, config_.traffic_network);
    }
    std::lock_guard lock(pending_map_package_mutex_);
    // Latest verified manifest wins if multiple editor bakes complete before
    // the next fixed tick consumes the queue.
    pending_map_package_ = PendingMapPackageReload{
        std::move(package),
        std::move(replacement_runtime_entities)};
}

void SimulationHost::publish_current_state()
{
    const auto state = physics_.get_state();
    if (callbacks_.broadcast_world_state) {
        callbacks_.broadcast_world_state(
            serialize_current_world_state(Clock::now()));
    }
    if (callbacks_.publish_observer_state) {
        callbacks_.publish_observer_state(
            simcore_host::serialize_entity_state_packet(
                state, active_vehicle_class_));
    }
}

std::vector<simcore_host::KinematicCollisionProxy>
SimulationHost::make_runtime_collision_snapshot() const
{
    std::vector<simcore_host::KinematicCollisionProxy> proxies;
    proxies.reserve(runtime_entities_.size());
    for (const auto& entity : runtime_entities_) {
        proxies.push_back(entity.collision_proxy);
        const auto npc = std::find_if(lane_npcs_.begin(), lane_npcs_.end(),
            [&](const auto& candidate) { return candidate.entity_id == entity.entity_id; });
        if (npc != lane_npcs_.end() && npc->pending) {
            auto& shape = std::get<simcore_host::ObbPrism>(proxies.back().shape);
            const auto& accepted = std::get<simcore_host::ObbPrism>(npc->pending->collision_proxy.shape);
            // Planar solver still starts at the tick-start XY/yaw, but must
            // use the accepted attitude envelope and grounded height. Its
            // resolved proxy is also the final authoritative wire geometry.
            shape.center_up_m = accepted.center_up_m;
            shape.half_length_m = accepted.half_length_m;
            shape.half_width_m = accepted.half_width_m;
            shape.half_height_m = accepted.half_height_m;
        }
        const auto pedestrian = std::find_if(pedestrians_.begin(), pedestrians_.end(),
            [&](const auto& candidate) { return candidate.entity_id == entity.entity_id; });
        if (pedestrian != pedestrians_.end() && pedestrian->pending) {
            const auto center = std::visit([](const auto& shape) { return shape.center_enu; }, proxies.back().shape);
            const auto velocity = proxies.back().linear_velocity_enu_mps;
            proxies.back() = pedestrian->pending->collision_proxy;
            proxies.back().linear_velocity_enu_mps = velocity;
            std::visit([&](auto& shape) { shape.center_enu = center; }, proxies.back().shape);
        }
    }
    const auto& structures = structure_damage_.collision_proxies();
    proxies.insert(proxies.end(), structures.begin(), structures.end());
    return proxies;
}

void SimulationHost::advance_runtime_entities(double dt_seconds)
{
    const auto& resolved_proxies = physics_.get_last_resolved_dynamic_proxies();
    auto contacts = physics_.get_last_collision_contacts();
    // Pair impulses were solved inside Ego's bounded collision microsteps;
    // consume them without advancing a proxy again or adding Ego damage.
    for (const auto& pair : physics_.get_last_runtime_proxy_contacts()) {
        auto contact = pair.contact;
        contact.collider_id = pair.proxy_id;
        contact.normal_enu.east_m *= -1.0;
        contact.normal_enu.north_m *= -1.0;
        contacts.push_back(std::move(contact));
    }
    const auto resolved_proxy = [&](std::string_view proxy_id) {
        return std::lower_bound(
            resolved_proxies.begin(), resolved_proxies.end(), proxy_id,
            [](const auto& proxy, std::string_view searched_id) {
                return proxy.proxy_id < searched_id;
            });
    };
    const auto normal_impulse = [&](std::string_view proxy_id) {
        double impulse = 0.0;
        for (const auto& contact : contacts) {
            if (contact.collider_id == proxy_id)
                impulse += contact.accumulated_normal_impulse_n_s;
        }
        return impulse;
    };
    const auto clamp_vector = [](simcore_host::CollisionVector2& value,
                                 double maximum) {
        const double magnitude = std::hypot(value.east_m, value.north_m);
        if (magnitude > maximum) {
            value.east_m *= maximum / magnitude;
            value.north_m *= maximum / magnitude;
        }
    };
    const auto proxy_center = [](const simcore_host::KinematicProxyShape& shape) {
        return std::visit(
            [](const auto& value) { return value.center_enu; }, shape);
    };

    for (auto& entity : runtime_entities_) {
        const auto lane_npc = std::find_if(lane_npcs_.begin(), lane_npcs_.end(),
            [&](const LaneNpcRuntime& candidate) {
                return candidate.entity_id == entity.entity_id;
            });
        if (lane_npc != lane_npcs_.end()) {
            if (lane_npc->pending) {
                entity = *lane_npc->pending;
                const auto resolved = resolved_proxy(entity.collision_proxy.proxy_id);
                if (resolved != resolved_proxies.end()
                    && resolved->proxy_id == entity.collision_proxy.proxy_id) {
                    entity.collision_proxy = *resolved;
                }
                if (lane_npc->nominal_pending) {
                    const auto& nominal = std::get<simcore_host::ObbPrism>(
                        lane_npc->nominal_pending->collision_proxy.shape);
                    auto& actual = std::get<simcore_host::ObbPrism>(
                        entity.collision_proxy.shape);
                    lane_npc->reaction.offset_enu_m = {
                        actual.center_enu.east_m - nominal.center_enu.east_m,
                        actual.center_enu.north_m - nominal.center_enu.north_m};
                    // Keep physical coasting separate from commanded route/
                    // recovery motion. Feeding the latter back as an impulse
                    // turns the return into an accelerating rubber band.
                    lane_npc->reaction.velocity_enu_mps.east_m +=
                        entity.collision_proxy.linear_velocity_enu_mps.east_m
                            - lane_npc->pending->collision_proxy.linear_velocity_enu_mps.east_m;
                    lane_npc->reaction.velocity_enu_mps.north_m +=
                        entity.collision_proxy.linear_velocity_enu_mps.north_m
                            - lane_npc->pending->collision_proxy.linear_velocity_enu_mps.north_m;
                    lane_npc->reaction.heading_offset_rad = std::remainder(
                        actual.heading_rad - nominal.heading_rad,
                        2.0 * std::numbers::pi);
                    // Numerical/authored yaw-rate bounds are not external
                    // torque. Only a real contact may add reaction momentum.
                    if (normal_impulse(entity.collision_proxy.proxy_id) > 0.0) {
                        lane_npc->reaction.heading_rate_rad_s +=
                            entity.collision_proxy.heading_rate_rad_s
                            - lane_npc->pending->collision_proxy.heading_rate_rad_s;
                    }
                    clamp_vector(lane_npc->reaction.velocity_enu_mps,
                                 lane_npc->maximum_reaction_speed_mps);
                }
                lane_npc->reaction.recovery.record_contact(
                    normal_impulse(entity.collision_proxy.proxy_id), entity.collision_proxy.mass_kg);
                const auto& body = std::get<simcore_host::ObbPrism>(entity.collision_proxy.shape);
                for (const auto& contact : contacts) {
                    if (contact.collider_id == entity.collision_proxy.proxy_id) {
                        // Solver normal points NPC -> Ego; NPC receives the
                        // opposite impulse at the assumed bumper contact height.
                        lane_npc->tumble.apply_contact(contact.accumulated_normal_impulse_n_s,
                            {-contact.normal_enu.east_m, -contact.normal_enu.north_m},
                            body.heading_rad, lane_npc->tumble_dimensions);
                    }
                }
            }
            continue;
        }
        const auto pedestrian = std::find_if(pedestrians_.begin(), pedestrians_.end(),
            [&](const PedestrianRuntime& candidate) {
                return candidate.entity_id == entity.entity_id;
            });
        if (pedestrian != pedestrians_.end()) {
            if (pedestrian->pending) {
                entity = *pedestrian->pending;
                const auto resolved = resolved_proxy(entity.collision_proxy.proxy_id);
                if (resolved != resolved_proxies.end()
                    && resolved->proxy_id == entity.collision_proxy.proxy_id) {
                    entity.collision_proxy = *resolved;
                }
                if (pedestrian->nominal_pending) {
                    const auto nominal = proxy_center(
                        pedestrian->nominal_pending->collision_proxy.shape);
                    const auto actual = proxy_center(entity.collision_proxy.shape);
                    pedestrian->reaction.offset_enu_m = {
                        actual.east_m - nominal.east_m,
                        actual.north_m - nominal.north_m};
                    pedestrian->reaction.velocity_enu_mps.east_m +=
                        entity.collision_proxy.linear_velocity_enu_mps.east_m
                            - pedestrian->pending->collision_proxy.linear_velocity_enu_mps.east_m;
                    pedestrian->reaction.velocity_enu_mps.north_m +=
                        entity.collision_proxy.linear_velocity_enu_mps.north_m
                            - pedestrian->pending->collision_proxy.linear_velocity_enu_mps.north_m;
                    clamp_vector(pedestrian->reaction.velocity_enu_mps,
                                 kPedestrianMaximumReactionSpeedMps);
                }
                pedestrian->reaction.recovery.record_contact(
                    normal_impulse(entity.collision_proxy.proxy_id), entity.collision_proxy.mass_kg);
            }
            continue;
        }
        auto& proxy = entity.collision_proxy;
        const auto resolved = resolved_proxy(proxy.proxy_id);
        if (resolved != resolved_proxies.end()
            && resolved->proxy_id == proxy.proxy_id) {
            proxy = *resolved;
            continue;
        }
        std::visit(
            [&](auto& shape) {
                shape.center_enu.east_m +=
                    proxy.linear_velocity_enu_mps.east_m * dt_seconds;
                shape.center_enu.north_m +=
                    proxy.linear_velocity_enu_mps.north_m * dt_seconds;
                using Shape = std::decay_t<decltype(shape)>;
                if constexpr (std::is_same_v<Shape, simcore_host::ObbPrism>) {
                    shape.heading_rad = normalize_heading(
                        shape.heading_rad
                        + proxy.heading_rate_rad_s * dt_seconds);
                }
            },
            proxy.shape);
    }
    finish_runtime_impact_contacts(contacts);
}

simcore_host::EnvelopeMetadata SimulationHost::make_metadata()
{
    return {
        message_sequence_++,
        simulation_clock_.simulation_time_ns(),
        config_.source_id,
        config_.map_package_checksum,
        active_play_session_id_,
    };
}

simcore_host::HealthSnapshot SimulationHost::make_health_snapshot(
    Clock::time_point now) const
{
    using simcore_host::HealthStatus;
    simcore_host::HealthSnapshot snapshot;
    snapshot.tick_overrun_count = simulation_clock_.overrun_count();
    snapshot.has_control_command = control_lease_.has_control_command();
    if (snapshot.has_control_command) {
        snapshot.last_command_age_ns = static_cast<std::uint64_t>(
            std::max(std::chrono::nanoseconds::zero(),
                     control_lease_.command_age(now)).count());
    }

    // Status follows already-applied control authority, not elapsed time alone.
    // In particular, receipt after the soft deadline can recover immediately;
    // the HUD must never inhibit the fresh command needed for that recovery.
    if (estop_latched_) {
        snapshot.status = HealthStatus::EstopLatched;
        snapshot.message = "Emergency stop latched; server restart required";
    } else if (!lifecycle_active_) {
        snapshot.status = HealthStatus::AwaitingReset;
        snapshot.message = "SafeStop applied; waiting for simulation reset";
    } else if (control_lease_.requires_reconnect()) {
        snapshot.status = HealthStatus::ReconnectRequired;
        snapshot.message = "SafeStop applied; control lease retired; reconnect required";
    } else if (!snapshot.has_control_command) {
        snapshot.status = HealthStatus::AwaitingControl;
        snapshot.message = "SafeStop applied; waiting for first control command";
    } else if (control_lease_.safe_stop_active()) {
        snapshot.status = HealthStatus::SafeStop;
        snapshot.message = "Command timeout; fresh ordered control can recover";
    } else {
        snapshot.status = HealthStatus::Active;
        snapshot.message = "Control lease active";
    }
    return snapshot;
}

void SimulationHost::schedule_tick()
{
    timer_.expires_at(simulation_clock_.next_deadline());
    std::weak_ptr<int> lifetime = lifetime_token_;
    timer_.async_wait([this, lifetime](boost::system::error_code ec) {
        if (lifetime.expired() || ec == boost::asio::error::operation_aborted) {
            return;
        }
        if (ec) {
            std::cerr << "[Timing] timer error: " << ec.message() << "\n";
            running_ = false;
            return;
        }

        run_tick();
        if (running_) {
            schedule_tick();
        }
    });
}

void SimulationHost::run_tick()
{
    const auto started_at = Clock::now();
    if (apply_pending_map_package_reload(started_at)) {
        return;
    }
    apply_pending_traffic_network_reload();
    if (control_lease_.update_timeout(started_at)) {
        const auto command_age = control_lease_.command_age(started_at);
        const auto command_age_ms = std::chrono::duration<double, std::milli>(
            command_age).count();
        last_logged_input_ = make_safe_stop_input();
        apply_vehicle_input(last_logged_input_);
        if (config_.physics_replay) {
            config_.physics_replay->event(simcore_host::PhysicsReplayEvent::SafeStop);
        }
        std::cerr << "[Safety] command timeout age_ms=" << command_age_ms
                  << " source=" << control_lease_.active_source_id()
                  << " session=" << control_lease_.active_session_id()
                  << "; SafeStop applied; lease retained\n";
    }
    if (control_lease_.update_hard_timeout(started_at)) {
        if (config_.physics_replay) {
            config_.physics_replay->event(simcore_host::PhysicsReplayEvent::HardTimeout);
        }
        const auto command_age = control_lease_.command_age(started_at);
        const auto command_age_ms = std::chrono::duration<double, std::milli>(
            command_age).count();
        std::cerr << "[Safety] hard command timeout age_ms=" << command_age_ms
                  << " source=" << control_lease_.active_source_id()
                  << " session=" << control_lease_.active_session_id()
                  << "; lease retired and reconnect required\n";
        if (callbacks_.close_control_connections) {
            callbacks_.close_control_connections(
                "control lease hard timeout; reconnect required");
        }
    }

    const double dt_seconds = 1.0 / config_.physics_frequency_hz;
    const bool structures_enabled = lifecycle_active_ && !estop_latched_
        && make_health_snapshot(started_at).status == simcore_host::HealthStatus::Active;
    // Consume last tick's solved contacts before taking this tick's collision
    // snapshot. Wire pose and the pole's actual collision geometry stay atomic.
    structure_damage_.tick(dt_seconds, structures_enabled);
    prepare_lane_npc(dt_seconds, started_at);
    prepare_pedestrians(dt_seconds, started_at);
    auto collision_snapshot = make_runtime_collision_snapshot();
    // Copy only in capture mode; ordinary ticks still transfer one snapshot.
    const auto recorded_proxies = config_.physics_replay
        ? collision_snapshot : std::vector<simcore_host::KinematicCollisionProxy>{};
    const auto state = physics_.update(dt_seconds, std::move(collision_snapshot));
    if (config_.physics_replay) {
        config_.physics_replay->tick(applied_input_, recorded_proxies, state);
    }
    advance_runtime_entities(dt_seconds);
    if (structures_enabled) {
        const simcore_host::ObbPrism impact_body{
            {state.position_enu.x, state.position_enu.y}, state.position_enu.z,
            state.heading * std::numbers::pi / 180.0,
            state.collision_half_length_m, state.collision_half_width_m, state.collision_half_height_m};
        structure_damage_.record_contacts(physics_.get_last_collision_contacts(), state.position_enu.z, &impact_body);
        for (const auto& pair : physics_.get_last_runtime_proxy_contacts()) {
            if (!pair.proxy_id.starts_with("signal-pole-")) continue;
            auto contact = pair.contact;
            contact.collider_id = pair.proxy_id;
            contact.normal_enu.east_m *= -1.0;
            contact.normal_enu.north_m *= -1.0;
            double impact_height = state.position_enu.z;
            for (const auto& proxy : physics_.get_last_resolved_dynamic_proxies()) {
                if (proxy.proxy_id == pair.contact.collider_id) {
                    impact_height = std::visit([](const auto& shape) { return shape.center_up_m; }, proxy.shape);
                    break;
                }
            }
            structure_damage_.record_contacts({contact}, impact_height);
        }
        structure_damage_.accept_resolved_proxies(physics_.get_last_resolved_dynamic_proxies());
    }
    if (reported_control_change_revision_ != control_change_revision_) {
        const auto command_to_tick = std::chrono::duration<double, std::milli>(
            started_at - last_control_change_time_).count();
        reported_control_change_revision_ = control_change_revision_;
        std::cout << "[Latency] input revision=" << control_change_revision_
                  << " applied_to_tick_ms=" << command_to_tick << "\n";
    }

    simulation_clock_.advance(Clock::now());
    const std::uint32_t current_overrun_count =
        simulation_clock_.overrun_count();
    const bool has_unreported_overruns =
        current_overrun_count != last_reported_overrun_count_;
    const bool overrun_log_interval_elapsed =
        last_overrun_log_time_ == Clock::time_point::min()
        || started_at - last_overrun_log_time_ >= std::chrono::seconds(1);
    if (has_unreported_overruns && overrun_log_interval_elapsed) {
        const std::uint32_t delta =
            current_overrun_count - last_reported_overrun_count_;
        last_reported_overrun_count_ = current_overrun_count;
        last_overrun_log_time_ = started_at;
        std::cerr << "[Timing] tick overruns total="
                  << current_overrun_count << " delta=" << delta << "\n";
    }

    if (callbacks_.broadcast_world_state) {
        callbacks_.broadcast_world_state(
            serialize_current_world_state(started_at));
    }
    if (callbacks_.publish_observer_state) {
        callbacks_.publish_observer_state(
            simcore_host::serialize_entity_state_packet(
                state, active_vehicle_class_));
    }
}

bool SimulationHost::apply_pending_map_package_reload(
    Clock::time_point tick_started_at)
{
    std::optional<PendingMapPackageReload> pending;
    {
        std::lock_guard lock(pending_map_package_mutex_);
        pending.swap(pending_map_package_);
    }
    if (!pending
        || pending->package.collision_checksum == config_.map_package_checksum) {
        return false;
    }

    const std::string previous_checksum = config_.map_package_checksum;
    if (config_.physics_replay) {
        config_.physics_replay->invalidate("map_package_reloaded");
    }
    // The follower's ground pointer must be retired before releasing the map.
    lane_npcs_.clear();
    pedestrians_.clear();
    physics_.replace_environment(
        pending->package.ground_query,
        pending->package.collision_world);
    config_.ground_query = std::move(pending->package.ground_query);
    config_.collision_world = std::move(pending->package.collision_world);
    config_.map_package_checksum =
        std::move(pending->package.collision_checksum);

    if (pending->replacement_runtime_entities) {
        initial_runtime_entities_ =
            std::move(*pending->replacement_runtime_entities);
    }
    runtime_entities_ = initial_runtime_entities_;
    last_logged_input_ = make_safe_stop_input();
    apply_vehicle_input(last_logged_input_);
    control_lease_.reset_for_new_simulation();
    simulation_clock_.restart(tick_started_at);
    last_reported_overrun_count_ = 0;
    last_overrun_log_time_ = Clock::time_point::min();
    last_control_change_time_ = Clock::time_point::min();
    control_change_revision_ = 0;
    reported_control_change_revision_ = 0;

    // The same PIE identity must be allowed to establish a fresh lifecycle on
    // its new WebSocket session. The old socket is closed below, so its retired
    // session cannot re-arm control after this reset.
    // Keep retired play identities permanently fenced. The client creates a
    // fresh PlaySessionId when it observes the new local manifest identity.
    active_play_session_key_.clear();
    active_play_session_id_.clear();
    active_controller_source_id_.clear();
    active_connection_session_id_.clear();
    active_connection_generation_ = 0;
    lifecycle_highest_sequence_ = 0;
    lifecycle_last_client_time_ns_ = 0;
    lifecycle_active_ = false;
    hello_connection_states_.clear();
    rebuild_structure_damage();
    rebuild_lane_npc();
    rebuild_pedestrians();

    std::cout << "[MapReload] applied at tick boundary map_id="
              << pending->package.map_id << " previous=" << previous_checksum
              << " current=" << config_.map_package_checksum
              << " ground_format="
              << simcore_host::ground_payload_kind_name(
                     pending->package.ground_diagnostics.payload_kind)
              << " ground_samples="
              << pending->package.ground_diagnostics.sample_count
              << " ground_cells="
		       << (pending->package.ground_diagnostics.payload_kind
		                   != simcore_host::GroundPayloadKind::TriangleCsvV1
                      ? pending->package.ground_diagnostics.cell_count
                      : pending->package.ground_diagnostics.spatial_cell_count)
              << "; vehicle and lifecycle reset\n";

    // Do not send a new-checksum frame down an old-checksum lifecycle: the
    // client intentionally treats that as incompatible. Closing first forces a
    // new connection session, whose connect snapshot advertises the new map.
    if (callbacks_.close_control_connections) {
        callbacks_.close_control_connections(
            "map package reloaded; reconnect required");
    }
    return true;
}
