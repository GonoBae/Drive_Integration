#include "simulation_host.hpp"

#include <boost/asio/error.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

constexpr std::size_t kMaxLifecycleIdentifierBytes = 256;
constexpr std::size_t kMaxTrackedPlaySessions = 4096;
constexpr std::size_t kMaxRuntimeEntities = 255;

bool finite_vector(const simcore_host::CollisionVector2& vector)
{
    return std::isfinite(vector.east_m) && std::isfinite(vector.north_m);
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
            || !std::isfinite(proxy.heading_rate_rad_s)
            || !std::isfinite(proxy.material.friction)
            || !std::isfinite(proxy.material.restitution)
            || proxy.material.friction < 0.0
            || proxy.material.restitution < 0.0
            || proxy.material.restitution > 1.0) {
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
                    if (entity.kind
                            != simcore_host::RuntimeEntityKind::NpcVehicle
                        || !std::isfinite(shape.heading_rad)
                        || !std::isfinite(shape.half_length_m)
                        || !std::isfinite(shape.half_width_m)
                        || !std::isfinite(shape.half_height_m)
                        || shape.half_length_m <= 0.0
                        || shape.half_width_m <= 0.0
                        || shape.half_height_m <= 0.0) {
                        throw std::invalid_argument(
                            "NPC runtime entity requires a valid OBB prism");
                    }
                } else {
                    if (entity.kind
                            != simcore_host::RuntimeEntityKind::Pedestrian
                        || !std::isfinite(shape.radius_m)
                        || !std::isfinite(shape.half_height_m)
                        || shape.radius_m <= 0.0
                        || shape.half_height_m < shape.radius_m) {
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
    if (config_.map_package_checksum.empty()
        || config_.map_package_checksum == "unset") {
        throw std::invalid_argument(
            "SimulationHost requires a verified map package checksum");
    }
    validate_and_order_runtime_entities(config_.runtime_entities);
    initial_runtime_entities_ = config_.runtime_entities;
    runtime_entities_ = initial_runtime_entities_;
    last_logged_input_ = make_safe_stop_input();
    physics_.set_input(last_logged_input_);
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

ClientMessageResult SimulationHost::handle_client_message(
    std::string_view message,
    std::uint64_t connection_generation,
    Clock::time_point received_at)
{
    std::string error;
    auto parsed = simcore_host::parse_client_message_envelope(message, &error);
    if (!parsed) {
        std::cerr << "[Input] " << error << "\n";
        return ClientMessageResult::Rejected;
    }

    if (const auto* command =
            std::get_if<simcore_host::ParsedControlCommand>(&*parsed)) {
        return handle_control_command(
            *command, received_at, connection_generation);
    }
    return handle_simulation_reset(
        std::get<simcore_host::ParsedSimulationReset>(*parsed),
        connection_generation);
}

ClientMessageResult SimulationHost::handle_control_command(
    const simcore_host::ParsedControlCommand& command,
    Clock::time_point received_at,
    std::uint64_t connection_generation)
{
    if (command.map_package_checksum != config_.map_package_checksum) {
        std::cerr << "[Input] map package checksum mismatch expected="
                  << config_.map_package_checksum << " received="
                  << command.map_package_checksum << "\n";
        return ClientMessageResult::Rejected;
    }
    if (command.source_id.size() > kMaxLifecycleIdentifierBytes
        || command.session_id.size() > kMaxLifecycleIdentifierBytes) {
        std::cerr << "[Input] rejected: identifier exceeds 256 bytes\n";
        return ClientMessageResult::Rejected;
    }
    // E-stop outranks lifecycle ownership. A valid emergency request remains
    // effective even if it arrives from a socket that no longer owns motion.
    if (command.estop) {
        estop_latched_ = true;
        last_logged_input_ = make_safe_stop_input();
        physics_.set_input(last_logged_input_);
        std::cerr << "[Safety] E-stop latched by source="
                  << command.source_id << " session=" << command.session_id << "\n";
        return ClientMessageResult::EmergencyStopLatched;
    }
    // A checksum-valid SimulationReset is the lifecycle handshake that binds a
    // controller connection to the authoritative play session. Ordinary motion
    // input must never acquire a lease before that handshake; E-stop remains the
    // sole pre-reset exception above so an uninitialized client can still stop.
    if (!lifecycle_active_) {
        std::cerr << "[Input] rejected command before simulation reset"
                  << " source=" << command.source_id
                  << " session=" << command.session_id
                  << " generation=" << connection_generation << "\n";
        return ClientMessageResult::Rejected;
    }
    if (command.source_id != active_controller_source_id_
        || command.session_id != active_connection_session_id_
        || (active_connection_generation_ != 0
            && connection_generation != active_connection_generation_)) {
        std::cerr << "[Input] rejected command outside active lifecycle connection"
                  << " source=" << command.source_id
                  << " session=" << command.session_id
                  << " generation=" << connection_generation << "\n";
        return ClientMessageResult::Rejected;
    }
    if (estop_latched_) {
        std::cerr << "[Safety] E-stop is latched; restart is required to reset\n";
        return ClientMessageResult::Rejected;
    }

    const bool was_safe_stopped = control_lease_.safe_stop_active();
    const auto decision = control_lease_.accept(
        {command.source_id, command.session_id, command.sequence,
         command.client_time_ns},
        received_at);
    if (decision != ControlLeaseDecision::Accepted) {
        std::cerr << "[Input] rejected source=" << command.source_id
                  << " session=" << command.session_id
                  << " sequence=" << command.sequence << ": "
                  << control_lease_decision_message(decision) << "\n";
        return ClientMessageResult::Rejected;
    }
    if (was_safe_stopped) {
        std::cout << "[Safety] control lease armed source="
                  << command.source_id << " session=" << command.session_id << "\n";
    }

    physics_.set_input(command.input);
    const bool changed = std::abs(last_logged_input_.throttle - command.input.throttle) > 0.001f
        || std::abs(last_logged_input_.brake - command.input.brake) > 0.001f
        || std::abs(last_logged_input_.steering - command.input.steering) > 0.001f
        || last_logged_input_.handbrake != command.input.handbrake
        || last_logged_input_.gear != command.input.gear;
    if (changed) {
        last_logged_input_ = command.input;
        last_control_change_time_ = received_at;
        ++control_change_revision_;
        std::cout << "[Input] change revision=" << control_change_revision_
                  << " sequence=" << command.sequence
                  << " throttle=" << command.input.throttle
                  << " brake=" << command.input.brake
                  << " steering=" << command.input.steering
                  << " handbrake=" << command.input.handbrake << "\n";
    }
    return ClientMessageResult::ControlAccepted;
}

ClientMessageResult SimulationHost::handle_simulation_reset(
    const simcore_host::ParsedSimulationReset& reset,
    std::uint64_t connection_generation)
{
    if (reset.map_package_checksum != config_.map_package_checksum) {
        std::cerr << "[Lifecycle] reset rejected: map package checksum mismatch"
                  << " expected=" << config_.map_package_checksum
                  << " received=" << reset.map_package_checksum << "\n";
        return ClientMessageResult::Rejected;
    }
    if (reset.source_id.empty() || reset.session_id.empty()
        || reset.play_session_id.empty() || reset.sequence == 0
        || reset.client_time_ns == 0) {
        std::cerr << "[Lifecycle] reset rejected: missing source, connection session, "
                     "play session, sequence, or client time\n";
        return ClientMessageResult::Rejected;
    }
    if (reset.source_id.size() > kMaxLifecycleIdentifierBytes
        || reset.session_id.size() > kMaxLifecycleIdentifierBytes
        || reset.play_session_id.size() > kMaxLifecycleIdentifierBytes) {
        std::cerr << "[Lifecycle] reset rejected: identifier exceeds 256 bytes\n";
        return ClientMessageResult::Rejected;
    }
    if (estop_latched_) {
        std::cerr << "[Safety] E-stop is latched; simulation reset rejected and "
                     "server restart is required\n";
        return ClientMessageResult::Rejected;
    }

    const std::string play_key = make_play_session_key(
        reset.source_id, reset.play_session_id);

    // WebSocket connection generations are issued by the server at accept.
    // They provide ordering that random session GUIDs cannot: once a newer
    // socket owns lifecycle state, no frame from an older socket may mutate it.
    if (lifecycle_active_ && connection_generation != 0
        && active_connection_generation_ != 0) {
        if (connection_generation < active_connection_generation_) {
            std::cerr << "[Lifecycle] stale connection generation rejected source="
                      << reset.source_id << " generation="
                      << connection_generation << " active_generation="
                      << active_connection_generation_ << "\n";
            return ClientMessageResult::Rejected;
        }
        if (connection_generation == active_connection_generation_) {
            const bool same_identity =
                reset.source_id == active_controller_source_id_
                && reset.session_id == active_connection_session_id_
                && reset.play_session_id == active_play_session_id_;
            if (!same_identity) {
                std::cerr << "[Lifecycle] identity changed within one connection generation\n";
                return ClientMessageResult::Rejected;
            }
            if (reset.sequence < lifecycle_highest_sequence_
                || reset.client_time_ns < lifecycle_last_client_time_ns_) {
                std::cerr << "[Lifecycle] reset ordering regressed within active connection\n";
                return ClientMessageResult::Rejected;
            }
            if (reset.sequence == lifecycle_highest_sequence_) {
                return ClientMessageResult::DuplicateSimulationReset;
            }

            lifecycle_highest_sequence_ = reset.sequence;
            lifecycle_last_client_time_ns_ = reset.client_time_ns;
            return ClientMessageResult::DuplicateSimulationReset;
        }
        if (reset.session_id == active_connection_session_id_) {
            std::cerr << "[Lifecycle] a newer socket reused the active session_id\n";
            return ClientMessageResult::Rejected;
        }
    }

    const bool was_seen = seen_play_sessions_.contains(play_key);
    if (was_seen) {
        if (play_key != active_play_session_key_) {
            std::cerr << "[Lifecycle] stale play-session reset rejected source="
                      << reset.source_id << " play_session="
                      << reset.play_session_id << "\n";
            return ClientMessageResult::Rejected;
        }

        const auto reconnect = control_lease_.reset_for_reconnect(
            reset.source_id, reset.session_id);
        if (reconnect == ControlLeaseResetDecision::RetiredSession) {
            std::cerr << "[Lifecycle] reset from retired connection rejected source="
                      << reset.source_id << " session=" << reset.session_id << "\n";
            return ClientMessageResult::Rejected;
        }
        if (reconnect == ControlLeaseResetDecision::Ready) {
            last_logged_input_ = make_safe_stop_input();
            physics_.set_input(last_logged_input_);
            std::cout << "[Lifecycle] PIE reconnect accepted without physics reset source="
                      << reset.source_id << " play_session="
                      << reset.play_session_id << " session="
                      << reset.session_id << " generation="
                      << connection_generation << "\n";
        }
        lifecycle_active_ = true;
        active_controller_source_id_ = reset.source_id;
        active_connection_session_id_ = reset.session_id;
        active_connection_generation_ = connection_generation;
        active_play_session_id_ = reset.play_session_id;
        lifecycle_highest_sequence_ = reset.sequence;
        lifecycle_last_client_time_ns_ = reset.client_time_ns;
        return ClientMessageResult::DuplicateSimulationReset;
    }

    if (seen_play_sessions_.size() >= kMaxTrackedPlaySessions) {
        std::cerr << "[Lifecycle] reset rejected: play-session history limit reached; "
                     "restart the server\n";
        return ClientMessageResult::Rejected;
    }

    const auto reset_session = control_lease_.begin_new_simulation(
        reset.source_id, reset.session_id);
    if (reset_session == ControlLeaseResetDecision::RetiredSession) {
        std::cerr << "[Lifecycle] new play session used a retired connection\n";
        return ClientMessageResult::Rejected;
    }

    physics_.reset();
    runtime_entities_ = initial_runtime_entities_;
    last_logged_input_ = make_safe_stop_input();
    physics_.set_input(last_logged_input_);
    simulation_clock_.reset_elapsed();
    last_reported_overrun_count_ = 0;
    last_overrun_log_time_ = Clock::time_point::min();
    last_control_change_time_ = Clock::time_point::min();
    control_change_revision_ = 0;
    reported_control_change_revision_ = 0;
    seen_play_sessions_.insert(play_key);
    active_play_session_key_ = play_key;
    active_play_session_id_ = reset.play_session_id;
    active_controller_source_id_ = reset.source_id;
    active_connection_session_id_ = reset.session_id;
    active_connection_generation_ = connection_generation;
    lifecycle_highest_sequence_ = reset.sequence;
    lifecycle_last_client_time_ns_ = reset.client_time_ns;
    lifecycle_active_ = true;

    std::cout << "[Lifecycle] simulation reset source=" << reset.source_id
              << " play_session=" << reset.play_session_id
              << " session=" << reset.session_id << " generation="
              << connection_generation << "\n";
    publish_current_state();
    return ClientMessageResult::SimulationReset;
}

std::string SimulationHost::make_initial_world_state()
{
    return simcore_host::serialize_world_state_envelope(
        physics_.get_state(), runtime_entities_, make_metadata());
}

VehicleInput SimulationHost::make_safe_stop_input()
{
    VehicleInput input;
    input.brake = 1.f;
    input.handbrake = true;
    return input;
}

std::string SimulationHost::make_play_session_key(
    std::string_view source_id,
    std::string_view play_session_id)
{
    std::string key;
    key.reserve(source_id.size() + play_session_id.size() + 1);
    key.append(source_id);
    key.push_back('\0');
    key.append(play_session_id);
    return key;
}

void SimulationHost::publish_current_state()
{
    const auto state = physics_.get_state();
    if (callbacks_.broadcast_world_state) {
        callbacks_.broadcast_world_state(
            simcore_host::serialize_world_state_envelope(
                state, runtime_entities_, make_metadata()));
    }
    if (callbacks_.publish_observer_state) {
        callbacks_.publish_observer_state(
            simcore_host::serialize_entity_state_packet(state));
    }
}

std::vector<simcore_host::KinematicCollisionProxy>
SimulationHost::make_runtime_collision_snapshot() const
{
    std::vector<simcore_host::KinematicCollisionProxy> proxies;
    proxies.reserve(runtime_entities_.size());
    for (const auto& entity : runtime_entities_) {
        proxies.push_back(entity.collision_proxy);
    }
    return proxies;
}

void SimulationHost::advance_runtime_entities(double dt_seconds)
{
    for (auto& entity : runtime_entities_) {
        auto& proxy = entity.collision_proxy;
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
    if (control_lease_.update_timeout(started_at)) {
        const auto command_age = control_lease_.command_age(started_at);
        const auto command_age_ms = std::chrono::duration<double, std::milli>(
            command_age).count();
        last_logged_input_ = make_safe_stop_input();
        physics_.set_input(last_logged_input_);
        std::cerr << "[Safety] command timeout age_ms=" << command_age_ms
                  << " source=" << control_lease_.active_source_id()
                  << " session=" << control_lease_.active_session_id()
                  << "; SafeStop applied; lease retained\n";
    }
    if (control_lease_.update_hard_timeout(started_at)) {
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
    const auto state = physics_.update(
        dt_seconds, make_runtime_collision_snapshot());
    advance_runtime_entities(dt_seconds);
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
            simcore_host::serialize_world_state_envelope(
                state, runtime_entities_, make_metadata()));
    }
    if (callbacks_.publish_observer_state) {
        callbacks_.publish_observer_state(
            simcore_host::serialize_entity_state_packet(state));
    }
}
