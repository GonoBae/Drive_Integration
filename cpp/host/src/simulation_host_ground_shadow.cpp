#include "simulation_host.hpp"
#include <cmath>
#include <iostream>
#include <numbers>

bool SimulationHost::live_ground_session_ready() const {
    const auto hello = hello_connection_states_.find(active_connection_generation_);
    return config_.live_ground && lifecycle_active_ && !estop_latched_ && !control_lease_.requires_reconnect()
        && hello != hello_connection_states_.end() && hello->second.supports_live_ground;
}
ClientMessageResult SimulationHost::handle_ground_world_revision(
    const simcore_host::ParsedGroundWorldRevision& revision, std::uint64_t generation, Clock::time_point now) {
    const auto hello = hello_connection_states_.find(generation);
    if (!live_ground_session_ready() || generation != active_connection_generation_
        || hello == hello_connection_states_.end()
        || revision.source_id != active_controller_source_id_ || revision.source_id != hello->second.source_id
        || revision.session_id != active_connection_session_id_ || revision.session_id != hello->second.session_id
        || revision.play_session_id != active_play_session_id_ || revision.map_package_checksum != config_.map_package_checksum
        || revision.sequence <= hello->second.highest_sequence) return ClientMessageResult::Rejected;
    live_ground_->reset_context(active_play_session_key_ + '\0' + active_connection_session_id_
        + '\0' + config_.map_package_checksum);
    live_obstacles_->reset_context(active_play_session_key_ + '\0' + active_connection_session_id_
        + '\0' + config_.map_package_checksum);
    live_npc_ground_->reset_context(active_play_session_key_ + '\0' + active_connection_session_id_
        + '\0' + config_.map_package_checksum);
    if (config_.live_npc_ground && !live_npc_ground_->can_observe_world_revision(
            revision.world_revision,revision.measurements_ready,now)) return ClientMessageResult::Rejected;
    const auto previous_revision = live_ground_->world_revision();
    if (!live_ground_->observe_world_revision(revision.world_revision,revision.measurements_ready))
        return ClientMessageResult::Rejected;
    if (!live_obstacles_->observe_world_revision(revision.world_revision,revision.measurements_ready))
        return ClientMessageResult::Rejected;
    if (config_.live_npc_ground && !live_npc_ground_->observe_world_revision(
            revision.world_revision,revision.measurements_ready,now)) return ClientMessageResult::Rejected;
    if (previous_revision != revision.world_revision) {
        live_npc_initialization_pending_ = true;
        live_ground_waiting_ = true;
        live_waiting_reason_ = revision.measurements_ready ? "world_changed" : "world_measurement_unavailable";
        physics_.clear_tick_contacts();
    }
    hello->second.highest_sequence = revision.sequence;
    hello->second.last_payload_kind = ClientPayloadKind::GroundWorldRevision;
    hello->second.last_payload_fingerprint.clear();
    // A world notice is not input and cannot renew the control lease.
    return ClientMessageResult::GroundWorldRevisionAccepted;
}
const simcore_host::GroundQueryBatch* SimulationHost::prepare_live_ground(Clock::time_point now) {
    if (!config_.live_ground) return nullptr;
    if (!live_ground_session_ready()) {
        live_ground_->reset_context({}); live_obstacles_->reset_context({});
        live_npc_ground_->reset_context({}); return nullptr;
    }
    live_ground_->reset_context(active_play_session_key_ + '\0' + active_connection_session_id_
        + '\0' + config_.map_package_checksum);
    const auto player = physics_.get_state();
    const double yaw = player.heading*std::numbers::pi/180;
    const auto& body_velocity = player.linear_velocity_body;
    const simcore_host::GroundPointEnu velocity{
        body_velocity.x*std::sin(yaw)-body_velocity.y*std::cos(yaw),
        body_velocity.x*std::cos(yaw)+body_velocity.y*std::sin(yaw),0};
    return live_ground_->prepare({player.position_enu.x, player.position_enu.y, player.position_enu.z},
        simulation_clock_.simulation_time_ns(), now,velocity);
}

const simcore_host::GroundQueryBatch* SimulationHost::prepare_live_npc_ground(Clock::time_point now) {
    if (!config_.live_npc_ground || !live_ground_session_ready()) return nullptr;
    return live_npc_ground_->prepare(simulation_clock_.simulation_time_ns(),now);
}

const simcore_host::LiveObstacleRequest* SimulationHost::prepare_live_obstacles(Clock::time_point now) {
    if (!config_.live_obstacles || !live_ground_session_ready()) return nullptr;
    const auto state=physics_.get_state();
    const double yaw=state.heading*std::numbers::pi/180;
    return live_obstacles_->prepare({state.position_enu.x,state.position_enu.y,state.position_enu.z},
        simulation_clock_.simulation_time_ns(),now,
        {state.linear_velocity_body.x*std::sin(yaw)-state.linear_velocity_body.y*std::cos(yaw),
         state.linear_velocity_body.x*std::cos(yaw)+state.linear_velocity_body.y*std::sin(yaw),0});
}
ClientMessageResult SimulationHost::handle_world_obstacles(const simcore_host::ParsedWorldObstacleResult& parsed,
    std::uint64_t generation, Clock::time_point now) {
    const auto hello=hello_connection_states_.find(generation);
    if (!config_.live_obstacles || !live_ground_session_ready() || generation!=active_connection_generation_
        || hello==hello_connection_states_.end()
        || parsed.source_id!=active_controller_source_id_ || parsed.source_id!=hello->second.source_id
        || parsed.session_id!=active_connection_session_id_ || parsed.session_id!=hello->second.session_id
        || parsed.play_session_id!=active_play_session_id_ || parsed.map_package_checksum!=config_.map_package_checksum
        || parsed.sequence<=hello->second.highest_sequence) return ClientMessageResult::Rejected;
    if (!live_obstacles_->accept(parsed.result,now)) return ClientMessageResult::Rejected;
    hello->second.highest_sequence=parsed.sequence;
    hello->second.last_payload_kind=ClientPayloadKind::WorldObstacleResult;
    hello->second.last_payload_fingerprint.clear();
    return ClientMessageResult::WorldObstacleResultAccepted;
}

const simcore_host::GroundQueryBatch* SimulationHost::prepare_ground_shadow(Clock::time_point now) {
    const auto hello = hello_connection_states_.find(active_connection_generation_);
    if (!config_.ground_query_shadow || !lifecycle_active_ || !config_.ground_query
        || control_lease_.requires_reconnect() || estop_latched_
        || hello == hello_connection_states_.end() || !hello->second.supports_ground_shadow) return nullptr;
    ground_shadow_.reset_context(active_play_session_key_ + '\0' + active_connection_session_id_
        + '\0' + config_.map_package_checksum);
    const simcore_host::GroundQueryBatch* batch = nullptr;
    if (hello->second.supports_ground_capture && ground_capture_) {
        batch = ground_shadow_.prepare_captured(ground_capture_->frame(), now);
    } else {
        const auto state = physics_.get_state();
        // Compatibility with v1 clients: this footprint is not solver coverage.
        const auto& p = config_.vehicle_parameters;
        const double yaw = state.heading*std::numbers::pi/180;
        std::vector<simcore_host::GroundPointEnu> origins{{state.position_enu.x,state.position_enu.y,state.position_enu.z+2.0}};
        for (int axle = 0; axle < 2; ++axle) {
            const double forward = (axle == 0 ? 0.5 : -0.5)*p.wheelbase_m;
            const double track = axle == 0 ? p.front_track_m : p.rear_track_m;
            for (double side : {-0.5, 0.5}) origins.push_back({
                state.position_enu.x + forward*std::sin(yaw) + side*track*std::cos(yaw),
                state.position_enu.y + forward*std::cos(yaw) - side*track*std::sin(yaw), state.position_enu.z+2.0});
        }
        batch = ground_shadow_.prepare(simulation_clock_.simulation_time_ns(), std::move(origins), *config_.ground_query, now);
    }
    if (now-last_shadow_log_ >= std::chrono::seconds(1)) {
        const auto& s = ground_shadow_.statistics();
        std::cout << "[GroundShadow] requested=" << s.requested << " accepted=" << s.accepted
            << " rejected=" << s.rejected << " timed_out=" << s.timed_out
            << " hits=" << s.hits_compared << " presence_mismatch=" << s.presence_mismatches
            << " failed=" << s.failed_probes << " material_mismatch=" << s.material_mismatches
            << " max_height_error_m=" << s.max_height_error_m << " max_normal_error_deg=" << s.max_normal_error_deg
            << " round_trip_ms=" << s.last_round_trip_ms << " max_round_trip_ms=" << s.max_round_trip_ms
            << " measurement_ms=" << s.last_measurement_ms
            << " round_trip_p95_ms=" << s.round_trip_p95_ms << " measurement_p95_ms=" << s.measurement_p95_ms
            << " source_calls=" << s.source_calls << " source_unique=" << s.source_unique
            << " source_discarded=" << s.source_discarded << " selected=" << s.selected_probes
            << " source=" << (hello->second.supports_ground_capture ? "solver" : "footprint") << " physics=file\n";
        last_shadow_log_ = now;
    }
    return batch;
}

ClientMessageResult SimulationHost::handle_ground_shadow(const simcore_host::ParsedGroundQueryResult& parsed,
    std::uint64_t generation, Clock::time_point received_at) {
    const auto hello = hello_connection_states_.find(generation);
    const bool enabled = parsed.npc_tile ? config_.live_npc_ground
        : parsed.live_patch ? config_.live_ground : config_.ground_query_shadow;
    if (!enabled || !lifecycle_active_ || estop_latched_
        || control_lease_.requires_reconnect() || generation != active_connection_generation_
        || hello == hello_connection_states_.end()
        || (parsed.live_patch ? !hello->second.supports_live_ground : !hello->second.supports_ground_shadow)
        || parsed.source_id != active_controller_source_id_ || parsed.source_id != hello->second.source_id
        || parsed.session_id != active_connection_session_id_ || parsed.session_id != hello->second.session_id
        || parsed.play_session_id != active_play_session_id_ || parsed.map_package_checksum != config_.map_package_checksum
        || parsed.sequence <= hello->second.highest_sequence) return ClientMessageResult::Rejected;
    // Do not accept an old request between a lifecycle change and the next publication.
    ground_shadow_.reset_context(active_play_session_key_ + '\0' + active_connection_session_id_
        + '\0' + config_.map_package_checksum);
    if (parsed.npc_tile) {
        live_npc_ground_->reset_context(active_play_session_key_ + '\0' + active_connection_session_id_
            + '\0' + config_.map_package_checksum);
        if (!live_npc_ground_->accept(parsed.result,received_at)) return ClientMessageResult::Rejected;
    } else if (parsed.live_patch) {
        live_ground_->reset_context(active_play_session_key_ + '\0' + active_connection_session_id_
            + '\0' + config_.map_package_checksum);
        if (!live_ground_->accept(parsed.result, received_at)) return ClientMessageResult::Rejected;
    } else if (!ground_shadow_.accept(parsed.result, received_at)) return ClientMessageResult::Rejected;
    hello->second.highest_sequence = parsed.sequence;
    hello->second.last_payload_kind = ClientPayloadKind::GroundQueryResult;
    hello->second.last_payload_fingerprint.clear();
    // Neither observations nor live measurements refresh the control lease.
    return ClientMessageResult::GroundQueryResultAccepted;
}
