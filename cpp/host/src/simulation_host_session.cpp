#include "simulation_host.hpp"

#include "player_vehicle_profile.hpp"
#include "simulation_host_session_detail.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <utility>

namespace {

using simcore_host::simulation_host_session_detail::
    is_log_safe_identifier;
using simcore_host::simulation_host_session_detail::
    kMaxLifecycleIdentifierBytes;

constexpr std::size_t kMaxTrackedPlaySessions = 4096;
constexpr std::size_t kMaxHelloConnections = 4096;

bool advances_hello_sequence(ClientMessageResult result)
{
    return result == ClientMessageResult::ControlAccepted
        || result == ClientMessageResult::EmergencyStopLatched
        || result == ClientMessageResult::SimulationReset
        || result == ClientMessageResult::DuplicateSimulationReset;
}

bool has_capability(const std::vector<std::string>& capabilities,
                    std::string_view required)
{
    return std::find(capabilities.begin(), capabilities.end(), required)
        != capabilities.end();
}

void append_fingerprint_u64(std::string& output, std::uint64_t value)
{
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffu));
    }
}

void append_fingerprint_string(std::string& output, std::string_view value)
{
    append_fingerprint_u64(output, value.size());
    output.append(value);
}

void append_fingerprint_float(std::string& output, float value)
{
    // Signed zero has one control meaning even though IEEE-754 has two bit
    // encodings. Every other value retains its exact parsed representation.
    if (value == 0.0f) {
        value = 0.0f;
    }
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<char>((bits >> shift) & 0xffu));
    }
}

std::string make_hello_negotiation_fingerprint(
    const simcore_host::ParsedHello& hello)
{
    std::vector<std::string_view> capabilities;
    capabilities.reserve(hello.capabilities.size());
    for (const auto& capability : hello.capabilities) {
        capabilities.push_back(capability);
    }
    std::sort(capabilities.begin(), capabilities.end());

    std::string fingerprint;
    append_fingerprint_string(fingerprint, hello.build);
    append_fingerprint_string(fingerprint, hello.schema);
    append_fingerprint_string(fingerprint, hello.map_package_checksum);
    append_fingerprint_u64(fingerprint, capabilities.size());
    for (const auto capability : capabilities) {
        append_fingerprint_string(fingerprint, capability);
    }
    return fingerprint;
}

std::string make_hello_payload_fingerprint(
    const simcore_host::ParsedHello& hello,
    std::string_view negotiation_fingerprint)
{
    std::string fingerprint;
    append_fingerprint_string(fingerprint, hello.source_id);
    append_fingerprint_string(fingerprint, hello.session_id);
    append_fingerprint_string(fingerprint, negotiation_fingerprint);
    return fingerprint;
}

std::string make_control_payload_fingerprint(
    const simcore_host::ParsedControlCommand& command)
{
    std::string fingerprint;
    append_fingerprint_string(fingerprint, command.source_id);
    append_fingerprint_string(fingerprint, command.session_id);
    append_fingerprint_string(fingerprint, command.map_package_checksum);
    append_fingerprint_u64(fingerprint, command.client_time_ns);
    fingerprint.push_back(command.estop ? '\x01' : '\x00');
    append_fingerprint_float(fingerprint, command.input.throttle);
    append_fingerprint_float(fingerprint, command.input.brake);
    append_fingerprint_float(fingerprint, command.input.steering);
    fingerprint.push_back(command.input.handbrake ? '\x01' : '\x00');
    fingerprint.push_back(static_cast<char>(command.input.gear));
    return fingerprint;
}

std::string make_reset_payload_fingerprint(
    const simcore_host::ParsedSimulationReset& reset)
{
    std::string fingerprint;
    append_fingerprint_string(fingerprint, reset.source_id);
    append_fingerprint_string(fingerprint, reset.session_id);
    append_fingerprint_string(fingerprint, reset.map_package_checksum);
    append_fingerprint_string(fingerprint, reset.play_session_id);
    append_fingerprint_u64(fingerprint, reset.client_time_ns);
    fingerprint.push_back(static_cast<char>(reset.requested_vehicle_class));
    return fingerprint;
}

} // namespace

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

    if (const auto* hello =
            std::get_if<simcore_host::ParsedHello>(&*parsed)) {
        return handle_hello(*hello, connection_generation);
    }

    const auto* command =
        std::get_if<simcore_host::ParsedControlCommand>(&*parsed);
    const auto hello_state = hello_connection_states_.find(
        connection_generation);
    if (config_.require_client_hello
        && (connection_generation == 0
            || hello_state == hello_connection_states_.end())) {
        // Emergency stop is the only control permitted before application
        // negotiation so a partially initialized client can still make motion
        // safe. Ordinary reset/control fail closed.
        if (command == nullptr || !command->estop) {
            std::cerr << "[Hello] rejected client payload before capability exchange"
                      << " generation=" << connection_generation << "\n";
            return ClientMessageResult::Rejected;
        }
    }

    const auto* reset = command == nullptr
        ? std::get_if<simcore_host::ParsedSimulationReset>(&*parsed)
        : nullptr;
    const std::string& source_id = command != nullptr
        ? command->source_id
        : reset->source_id;
    const std::string& session_id = command != nullptr
        ? command->session_id
        : reset->session_id;
    const std::uint64_t sequence = command != nullptr
        ? command->sequence
        : reset->sequence;
    const auto payload_kind = command != nullptr
        ? ClientPayloadKind::ControlCommand
        : ClientPayloadKind::SimulationReset;
    const std::string payload_fingerprint = command != nullptr
        ? make_control_payload_fingerprint(*command)
        : make_reset_payload_fingerprint(*reset);
    if (hello_state != hello_connection_states_.end()
        && (source_id != hello_state->second.source_id
            || session_id != hello_state->second.session_id
            || sequence < hello_state->second.highest_sequence)) {
        // Do not echo untrusted identity bytes. The detailed validation in the
        // payload handler runs only after this connection-bound comparison.
        std::cerr << "[Hello] rejected identity/order mismatch generation="
                  << connection_generation << "\n";
        return ClientMessageResult::Rejected;
    }
    if (hello_state != hello_connection_states_.end()
        && sequence == hello_state->second.highest_sequence
        && (payload_kind != hello_state->second.last_payload_kind
            || payload_fingerprint
                != hello_state->second.last_payload_fingerprint)) {
        std::cerr << "[Hello] rejected sequence reuse with different payload"
                  << " generation=" << connection_generation << "\n";
        return ClientMessageResult::Rejected;
    }

    ClientMessageResult result = ClientMessageResult::Rejected;
    if (command != nullptr) {
        result = handle_control_command(
            *command, received_at, connection_generation);
    } else {
        result = handle_simulation_reset(*reset, connection_generation);
    }
    // Commit ordering only after all identity, checksum, lifecycle, lease, and
    // safety checks accept the frame. A rejected high sequence must not poison
    // this connection and prevent a later valid lower sequence.
    if (hello_state != hello_connection_states_.end()
        && advances_hello_sequence(result)) {
        hello_state->second.highest_sequence = std::max(
            hello_state->second.highest_sequence, sequence);
        hello_state->second.last_payload_kind = payload_kind;
        hello_state->second.last_payload_fingerprint = payload_fingerprint;
    }
    return result;
}

ClientMessageResult SimulationHost::handle_hello(
    const simcore_host::ParsedHello& hello,
    std::uint64_t connection_generation)
{
    if (connection_generation == 0 || hello.sequence == 0
        || !is_log_safe_identifier(
            hello.source_id, kMaxLifecycleIdentifierBytes)
        || !is_log_safe_identifier(
            hello.session_id, kMaxLifecycleIdentifierBytes)
        || !is_log_safe_identifier(hello.build, 128)
        || !is_log_safe_identifier(
            hello.map_package_checksum, kMaxLifecycleIdentifierBytes)
        || hello.map_package_checksum != config_.map_package_checksum
        || hello.schema != simcore_host::kProtocolSchemaName
        || hello.capabilities.empty() || hello.capabilities.size() > 32) {
        std::cerr << "[Hello] rejected invalid identity/schema generation="
                  << connection_generation << "\n";
        return ClientMessageResult::Rejected;
    }

    std::unordered_set<std::string> unique_capabilities;
    for (const auto& capability : hello.capabilities) {
        if (!is_log_safe_identifier(capability, 128)
            || !unique_capabilities.insert(capability).second) {
            std::cerr << "[Hello] rejected invalid or duplicate capability\n";
            return ClientMessageResult::Rejected;
        }
    }
    constexpr std::string_view required_capabilities[] = {
        "world-state.v2",
        "control.v2",
        "simulation-reset.v1",
        "map-package-checksum.v1",
    };
    for (const auto required : required_capabilities) {
        if (!has_capability(hello.capabilities, required)) {
            std::cerr << "[Hello] rejected missing capability=" << required << "\n";
            return ClientMessageResult::Rejected;
        }
    }

    const std::string negotiation_fingerprint =
        make_hello_negotiation_fingerprint(hello);
    const std::string payload_fingerprint = make_hello_payload_fingerprint(
        hello, negotiation_fingerprint);
    const auto existing = hello_connection_states_.find(connection_generation);
    if (existing != hello_connection_states_.end()) {
        if (hello.source_id != existing->second.source_id
            || hello.session_id != existing->second.session_id
            || negotiation_fingerprint
                != existing->second.negotiated_hello_fingerprint
            || hello.sequence < existing->second.highest_sequence) {
            std::cerr << "[Hello] rejected duplicate identity/order mismatch"
                      << " generation=" << connection_generation << "\n";
            return ClientMessageResult::Rejected;
        }
        if (hello.sequence == existing->second.highest_sequence
            && (existing->second.last_payload_kind
                    != ClientPayloadKind::Hello
                || payload_fingerprint
                    != existing->second.last_payload_fingerprint)) {
            std::cerr << "[Hello] rejected sequence reuse with different payload"
                      << " generation=" << connection_generation << "\n";
            return ClientMessageResult::Rejected;
        }
        existing->second.highest_sequence = hello.sequence;
        existing->second.last_payload_kind = ClientPayloadKind::Hello;
        existing->second.last_payload_fingerprint = payload_fingerprint;
        return ClientMessageResult::DuplicateHello;
    }
    if (hello_connection_states_.size() >= kMaxHelloConnections) {
        const auto oldest = std::min_element(
            hello_connection_states_.begin(),
            hello_connection_states_.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });
        if (oldest != hello_connection_states_.end()) {
            hello_connection_states_.erase(oldest);
        }
    }
    hello_connection_states_.emplace(
        connection_generation,
        HelloConnectionState{
            hello.source_id,
            hello.session_id,
            negotiation_fingerprint,
            hello.sequence,
            ClientPayloadKind::Hello,
            payload_fingerprint});
    std::cout << "[Hello] client accepted build=" << hello.build
              << " generation=" << connection_generation
              << " capabilities=" << hello.capabilities.size() << "\n";
    return ClientMessageResult::HelloAccepted;
}

ClientMessageResult SimulationHost::handle_control_command(
    const simcore_host::ParsedControlCommand& command,
    Clock::time_point received_at,
    std::uint64_t connection_generation)
{
    if (!is_log_safe_identifier(
            command.source_id, kMaxLifecycleIdentifierBytes, true)
        || !is_log_safe_identifier(
            command.session_id, kMaxLifecycleIdentifierBytes, true)
        || !is_log_safe_identifier(
            command.map_package_checksum, kMaxLifecycleIdentifierBytes)) {
        std::cerr << "[Input] rejected: identifier is not printable ASCII or exceeds its limit\n";
        return ClientMessageResult::Rejected;
    }
    if (command.map_package_checksum != config_.map_package_checksum) {
        std::cerr << "[Input] map package checksum mismatch expected="
                  << config_.map_package_checksum << " received="
                  << command.map_package_checksum << "\n";
        return ClientMessageResult::Rejected;
    }
    // E-stop outranks lifecycle ownership. A valid emergency request remains
    // effective even if it arrives from a socket that no longer owns motion.
    if (command.estop) {
        estop_latched_ = true;
        last_logged_input_ = make_safe_stop_input();
        apply_vehicle_input(last_logged_input_);
        if (config_.physics_replay) {
            config_.physics_replay->event(simcore_host::PhysicsReplayEvent::EmergencyStop);
        }
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

    apply_vehicle_input(command.input);
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
    if (!is_log_safe_identifier(
            reset.source_id, kMaxLifecycleIdentifierBytes)
        || !is_log_safe_identifier(
            reset.session_id, kMaxLifecycleIdentifierBytes)
        || !is_log_safe_identifier(
            reset.play_session_id, kMaxLifecycleIdentifierBytes)
        || !is_log_safe_identifier(
            reset.map_package_checksum, kMaxLifecycleIdentifierBytes)) {
        std::cerr << "[Lifecycle] reset rejected: identifier is not printable ASCII or exceeds its limit\n";
        return ClientMessageResult::Rejected;
    }
    if (reset.map_package_checksum != config_.map_package_checksum) {
        std::cerr << "[Lifecycle] reset rejected: map package checksum mismatch"
                  << " expected=" << config_.map_package_checksum
                  << " received=" << reset.map_package_checksum << "\n";
        return ClientMessageResult::Rejected;
    }
    if (reset.sequence == 0
        || reset.client_time_ns == 0) {
        std::cerr << "[Lifecycle] reset rejected: missing source, connection session, "
                     "play session, sequence, or client time\n";
        return ClientMessageResult::Rejected;
    }
    if (estop_latched_) {
        std::cerr << "[Safety] E-stop is latched; simulation reset rejected and "
                     "server restart is required\n";
        return ClientMessageResult::Rejected;
    }

    const std::string play_key = make_play_session_key(
        reset.source_id, reset.play_session_id);
    const simcore_host::RuntimeVehicleClass requested_class =
        reset.requested_vehicle_class == simcore_host::RuntimeVehicleClass::Unspecified
        ? simcore_host::RuntimeVehicleClass::Sedan
        : reset.requested_vehicle_class;

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
            if (requested_class != active_vehicle_class_) {
                std::cerr << "[Lifecycle] reset rejected: vehicle class changed within play session\n";
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
        if (requested_class != active_vehicle_class_) {
            std::cerr << "[Lifecycle] reconnect rejected: vehicle class changed within play session\n";
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
            apply_vehicle_input(last_logged_input_);
            if (config_.physics_replay) {
                config_.physics_replay->event(simcore_host::PhysicsReplayEvent::Reconnect);
            }
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

    reset_player_vehicle(requested_class);
    if (config_.physics_replay) {
        config_.physics_replay->event(
            simcore_host::PhysicsReplayEvent::Reset, requested_class);
    }
    runtime_entities_ = initial_runtime_entities_;
    structure_damage_.reset();
    rebuild_lane_npc();
    rebuild_pedestrians();
    last_logged_input_ = make_safe_stop_input();
    apply_vehicle_input(last_logged_input_);
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
              << connection_generation << " vehicle="
              << simcore_host::runtime_vehicle_class_name(active_vehicle_class_)
              << "\n";
    publish_current_state();
    return ClientMessageResult::SimulationReset;
}

void SimulationHost::reset_player_vehicle(
    simcore_host::RuntimeVehicleClass vehicle_class)
{
    config_.vehicle_parameters = simcore_host::make_player_vehicle_parameters(
        base_vehicle_parameters_, vehicle_class);
    physics_.replace_parameters(config_.vehicle_parameters);
    active_vehicle_class_ = vehicle_class;
}

std::string SimulationHost::make_initial_hello()
{
    return simcore_host::serialize_hello_envelope(
        make_metadata(),
        "simcore-cpp-host-r1",
        {
            "world-state.v2",
            "control.v2",
            "simulation-reset.v1",
            "player-vehicle-selection.v1",
            "map-package-checksum.v1",
            "safe-stop.v1",
            "world-health.v1",
            "traffic-signals.v1",
            "pedestrian-signals.v1",
        });
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
