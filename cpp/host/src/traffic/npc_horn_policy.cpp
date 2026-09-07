#include "traffic/npc_horn_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcore_host {
namespace {

constexpr double kTimerEpsilonS = 1.0e-9;

bool finite_non_negative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

bool valid_config(const NpcHornPolicyConfig& config)
{
    return std::isfinite(config.pulse_duration_s)
        && config.pulse_duration_s > 0.0
        && std::isfinite(config.cooldown_s)
        && config.cooldown_s >= config.pulse_duration_s
        && finite_non_negative(config.obstruction_delay_s)
        && finite_non_negative(config.stopped_speed_mps)
        && std::isfinite(config.maximum_obstruction_clearance_m)
        && config.maximum_obstruction_clearance_m > 0.0
        && std::isfinite(config.minimum_imminent_closing_speed_mps)
        && config.minimum_imminent_closing_speed_mps > 0.0
        && std::isfinite(config.imminent_ttc_s)
        && config.imminent_ttc_s > 0.0
        && std::isfinite(config.imminent_release_ttc_s)
        && config.imminent_release_ttc_s > config.imminent_ttc_s;
}

bool valid_observation(const NpcHornObservation& observation)
{
    return static_cast<unsigned>(observation.motion_context)
            <= static_cast<unsigned>(NpcHornMotionContext::Inactive)
        && finite_non_negative(observation.speed_mps)
        && finite_non_negative(observation.closing_speed_mps)
        && (!observation.obstacle_clearance_m
            || finite_non_negative(*observation.obstacle_clearance_m));
}

} // namespace

NpcHornPolicy::NpcHornPolicy(NpcHornPolicyConfig config)
    : config_(config)
{
    if (!valid_config(config_)) {
        throw std::invalid_argument("NPC horn policy config is invalid");
    }
}

const NpcHornState& NpcHornPolicy::step(
    double dt_seconds, const NpcHornObservation& observation) noexcept
{
    if (!std::isfinite(dt_seconds) || dt_seconds < 0.0 || dt_seconds > 10.0
        || !valid_observation(observation)) {
        // A presentation signal must fail silent. Preserve sequence/cooldown so
        // malformed input cannot manufacture an event or bypass anti-spam.
        state_.active = false;
        state_.reason = NpcHornReason::None;
        state_.active_remaining_s = 0.0;
        state_.obstruction_seconds = 0.0;
        state_.invalid_input = true;
        imminent_latched_ = false;
        return state_;
    }
    state_.invalid_input = false;

    if (!observation.enabled
        || observation.motion_context == NpcHornMotionContext::Inactive) {
        // Lifecycle pause and disabled/crashed NPCs are silent immediately.
        // Simulation-time cooldown is intentionally paused while disabled.
        state_.active = false;
        state_.reason = NpcHornReason::None;
        state_.active_remaining_s = 0.0;
        state_.obstruction_seconds = 0.0;
        imminent_latched_ = false;
        return state_;
    }

    state_.active_remaining_s = std::max(0.0,
        state_.active_remaining_s - dt_seconds);
    state_.cooldown_remaining_s = std::max(0.0,
        state_.cooldown_remaining_s - dt_seconds);
    if (state_.active_remaining_s <= kTimerEpsilonS) {
        state_.active_remaining_s = 0.0;
    }
    if (state_.cooldown_remaining_s <= kTimerEpsilonS) {
        state_.cooldown_remaining_s = 0.0;
    }
    state_.active = state_.active_remaining_s > 0.0;
    if (!state_.active) state_.reason = NpcHornReason::None;

    const bool horn_permitted = observation.motion_context
        != NpcHornMotionContext::TrafficControlStop;
    if (horn_permitted && observation.obstacle_clearance_m
        && observation.nonresponsive_obstacle_id != 0
        && observation.nonresponsive_obstacle_id != nonresponsive_obstacle_id_) {
        nonresponsive_obstacle_id_ = observation.nonresponsive_obstacle_id;
        nonresponsive_obstacle_warned_ = false;
        imminent_latched_ = false;
    }
    const bool nonresponsive_already_warned =
        observation.nonresponsive_obstacle_id != 0
        && observation.nonresponsive_obstacle_id == nonresponsive_obstacle_id_
        && nonresponsive_obstacle_warned_;
    const bool obstruction = horn_permitted
        && observation.motion_context == NpcHornMotionContext::ObstructionStop
        && observation.persistent_obstruction
        && observation.obstacle_clearance_m
        && *observation.obstacle_clearance_m
            <= config_.maximum_obstruction_clearance_m
        && observation.speed_mps <= config_.stopped_speed_mps;
    state_.obstruction_seconds = obstruction
        ? state_.obstruction_seconds + dt_seconds : 0.0;

    double ttc_seconds = std::numeric_limits<double>::infinity();
    if (observation.obstacle_clearance_m
        && observation.closing_speed_mps > 0.0) {
        ttc_seconds = *observation.obstacle_clearance_m
            / observation.closing_speed_mps;
    }
    const bool imminent = horn_permitted
        && observation.obstacle_clearance_m
        && observation.closing_speed_mps
            >= config_.minimum_imminent_closing_speed_mps
        && ttc_seconds <= config_.imminent_ttc_s;
    if (!horn_permitted || !observation.obstacle_clearance_m
        || observation.closing_speed_mps <= 0.0
        || ttc_seconds >= config_.imminent_release_ttc_s) {
        imminent_latched_ = false;
    }

    if (imminent && !imminent_latched_) {
        imminent_latched_ = true;
        if (!nonresponsive_already_warned
            && state_.cooldown_remaining_s <= kTimerEpsilonS) {
            trigger(NpcHornReason::ImminentCollision);
            if (observation.nonresponsive_obstacle_id != 0) {
                nonresponsive_obstacle_warned_ = true;
            }
        }
    } else if (obstruction
        && !nonresponsive_already_warned
        && state_.obstruction_seconds + kTimerEpsilonS
            >= config_.obstruction_delay_s
        && state_.cooldown_remaining_s <= kTimerEpsilonS) {
        trigger(NpcHornReason::PersistentObstruction);
        if (observation.nonresponsive_obstacle_id != 0) {
            nonresponsive_obstacle_warned_ = true;
        }
        // Continue measuring the same obstruction, but require the full delay
        // again if a caller configures a cooldown shorter than that delay.
        state_.obstruction_seconds = 0.0;
    }
    return state_;
}

void NpcHornPolicy::reset() noexcept
{
    state_ = {};
    imminent_latched_ = false;
    nonresponsive_obstacle_id_ = 0;
    nonresponsive_obstacle_warned_ = false;
}

void NpcHornPolicy::trigger(NpcHornReason reason) noexcept
{
    state_.active = true;
    state_.reason = reason;
    state_.active_remaining_s = config_.pulse_duration_s;
    state_.cooldown_remaining_s = config_.cooldown_s;
    if (state_.event_sequence != std::numeric_limits<std::uint32_t>::max()) {
        ++state_.event_sequence;
    }
}

} // namespace simcore_host
