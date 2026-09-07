#include "traffic/npc_horn_policy.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using namespace simcore_host;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool near(double first, double second, double epsilon = 1.0e-8)
{
    return std::isfinite(first) && std::isfinite(second)
        && std::abs(first - second) <= epsilon;
}

NpcHornObservation stopped_obstruction()
{
    NpcHornObservation observation;
    observation.motion_context = NpcHornMotionContext::ObstructionStop;
    observation.persistent_obstruction = true;
    observation.speed_mps = 0.0;
    observation.closing_speed_mps = 0.0;
    observation.obstacle_clearance_m = 2.0;
    return observation;
}

void advance(NpcHornPolicy& policy, double seconds,
             const NpcHornObservation& observation, int hz = 60)
{
    const int ticks = static_cast<int>(std::lround(seconds * hz));
    for (int index = 0; index < ticks; ++index) {
        policy.step(1.0 / hz, observation);
    }
}

void persistent_obstruction_waits_then_pulses_with_cooldown()
{
    NpcHornPolicy policy;
    const auto obstruction = stopped_obstruction();
    advance(policy, 1.0, obstruction);
    require(policy.state().event_sequence == 0 && !policy.state().active,
        "a brief obstruction must not produce impatient horn spam");
    advance(policy, 0.5, obstruction);
    require(policy.state().event_sequence == 1 && policy.state().active
            && policy.state().reason == NpcHornReason::PersistentObstruction,
        "a continuously blocked NPC must emit one bounded horn pulse");
    advance(policy, 0.35, obstruction);
    require(!policy.state().active && policy.state().event_sequence == 1,
        "the host horn pulse must end without a release message");
    advance(policy, 4.64, obstruction);
    require(policy.state().event_sequence == 1,
        "the same obstruction must stay quiet throughout cooldown");
    advance(policy, 0.02, obstruction);
    require(policy.state().event_sequence == 2,
        "a persistent crash may receive a sparse post-cooldown reminder");
}

void ordinary_traffic_stops_never_horn()
{
    for (const auto context : {
            NpcHornMotionContext::TrafficControlStop,
            NpcHornMotionContext::Inactive}) {
        NpcHornPolicy policy;
        auto observation = stopped_obstruction();
        observation.motion_context = context;
        advance(policy, 30.0, observation);
        require(policy.state().event_sequence == 0 && !policy.state().active,
            "signals, route ends and disabled NPCs must remain silent");
    }
    NpcHornPolicy policy;
    auto queue = stopped_obstruction();
    queue.persistent_obstruction = false;
    advance(policy, 30.0, queue);
    require(policy.state().event_sequence == 0,
        "a healthy transient vehicle queue is not a meaningful obstruction");
}

void nonresponsive_obstruction_has_one_warning_despite_observation_gaps()
{
    NpcHornPolicy policy;
    auto obstruction = stopped_obstruction();
    obstruction.nonresponsive_obstacle_id = 4201;
    advance(policy, 30.0, obstruction);
    require(policy.state().event_sequence == 1 && !policy.state().active,
        "a fallen pedestrian must not receive repeated stationary reminders");

    NpcHornObservation missing;
    for (int index = 0; index < 10; ++index) {
        advance(policy, 0.1, missing);
        advance(policy, 3.0, obstruction);
    }
    require(policy.state().event_sequence == 1,
        "brief missing or zero-ID observations must not release the entity latch");

    auto approach = obstruction;
    approach.motion_context = NpcHornMotionContext::Normal;
    approach.speed_mps = approach.closing_speed_mps = 8.0;
    policy.step(1.0 / 60.0, approach);
    require(policy.state().event_sequence == 1,
        "a prior stationary warning must also suppress a same-entity TTC warning");

    auto unavailable = obstruction;
    unavailable.nonresponsive_obstacle_id = 4202;
    unavailable.enabled = false;
    advance(policy, 10.0, unavailable);
    unavailable.enabled = true;
    unavailable.motion_context = NpcHornMotionContext::Inactive;
    advance(policy, 10.0, unavailable);
    unavailable.motion_context = NpcHornMotionContext::TrafficControlStop;
    advance(policy, 10.0, unavailable);
    unavailable.motion_context = NpcHornMotionContext::ObstructionStop;
    unavailable.speed_mps = std::numeric_limits<double>::quiet_NaN();
    policy.step(1.0 / 60.0, unavailable);
    advance(policy, 10.0, obstruction);
    require(policy.state().event_sequence == 1 && !policy.state().invalid_input,
        "inactive, traffic-stop and malformed observations must preserve the latch");
}

void a_different_nonresponsive_entity_and_reset_allow_one_new_warning()
{
    NpcHornPolicy policy;
    auto obstruction = stopped_obstruction();
    obstruction.nonresponsive_obstacle_id = 4201;
    advance(policy, 10.0, obstruction);
    obstruction.nonresponsive_obstacle_id = 4202;
    advance(policy, 30.0, obstruction);
    require(policy.state().event_sequence == 2,
        "a different nonresponsive entity may receive exactly one new warning");

    policy.reset();
    advance(policy, 30.0, obstruction);
    require(policy.state().event_sequence == 1,
        "explicit lifecycle reset must release the nonresponsive entity latch");
}

void nonresponsive_ttc_chatter_does_not_repeat_a_warning()
{
    NpcHornPolicy policy;
    auto hazard = stopped_obstruction();
    hazard.nonresponsive_obstacle_id = 4201;
    hazard.motion_context = NpcHornMotionContext::Normal;
    hazard.speed_mps = hazard.closing_speed_mps = 8.0;
    hazard.obstacle_clearance_m = 6.0;
    policy.step(1.0 / 60.0, hazard);
    require(policy.state().event_sequence == 1
            && policy.state().reason == NpcHornReason::ImminentCollision,
        "the first approach to a nonresponsive entity retains its TTC warning");

    auto stopped = stopped_obstruction();
    stopped.nonresponsive_obstacle_id = hazard.nonresponsive_obstacle_id;
    advance(policy, 30.0, stopped);
    for (int index = 0; index < 5; ++index) {
        auto clear = hazard;
        clear.nonresponsive_obstacle_id = 0;
        clear.obstacle_clearance_m = 12.0;
        advance(policy, 6.0, clear);
        advance(policy, 1.0, hazard);
    }
    require(policy.state().event_sequence == 1,
        "TTC release, missing identity and reapproach must not rewarn the same entity");

    hazard.nonresponsive_obstacle_id = 4202;
    policy.step(1.0 / 60.0, hazard);
    require(policy.state().event_sequence == 2
            && policy.state().reason == NpcHornReason::ImminentCollision,
        "a different imminent entity is a new hazard even without a TTC gap");

    NpcHornObservation clear;
    advance(policy, 6.0, clear);
    hazard.nonresponsive_obstacle_id = 0;
    policy.step(1.0 / 60.0, hazard);
    require(policy.state().event_sequence == 3,
        "ordinary new TTC hazards must retain their existing warning behavior");
    advance(policy, 6.0, clear);
    advance(policy, 6.0, stopped_obstruction());
    require(policy.state().event_sequence == 4,
        "ordinary obstructions must retain post-cooldown reminders");
}

void imminent_collision_is_one_shot_until_hysteresis_release()
{
    NpcHornPolicyConfig config;
    config.cooldown_s = 1.0;
    NpcHornPolicy policy(config);
    NpcHornObservation hazard;
    hazard.speed_mps = 8.0;
    hazard.closing_speed_mps = 8.0;
    hazard.obstacle_clearance_m = 6.4; // Exactly 0.8s TTC.
    policy.step(1.0 / 60.0, hazard);
    require(policy.state().event_sequence == 1
            && policy.state().reason == NpcHornReason::ImminentCollision,
        "the inclusive TTC boundary must trigger immediately");
    advance(policy, 3.0, hazard);
    require(policy.state().event_sequence == 1,
        "one continuous imminent hazard must stay latched after cooldown");
    hazard.obstacle_clearance_m = 9.59; // Below 1.2s release boundary.
    policy.step(1.0 / 60.0, hazard);
    hazard.obstacle_clearance_m = 6.0;
    policy.step(1.0 / 60.0, hazard);
    require(policy.state().event_sequence == 1,
        "TTC hysteresis must reject boundary chatter");
    hazard.obstacle_clearance_m = 9.6; // Exactly 1.2s: release.
    policy.step(1.0 / 60.0, hazard);
    hazard.obstacle_clearance_m = 6.0;
    policy.step(1.0 / 60.0, hazard);
    require(policy.state().event_sequence == 2,
        "a hazard that genuinely cleared may trigger a new event");
}

void bumper_clearance_and_closing_speed_define_ttc()
{
    NpcHornPolicy policy;
    NpcHornObservation observation;
    observation.speed_mps = 20.0;
    observation.closing_speed_mps = 2.0;
    observation.obstacle_clearance_m = 1.61;
    policy.step(1.0 / 60.0, observation);
    require(policy.state().event_sequence == 0,
        "own speed must not replace supplied relative closing speed");
    observation.obstacle_clearance_m = 1.6;
    policy.step(1.0 / 60.0, observation);
    require(policy.state().event_sequence == 1,
        "TTC must use physical bumper clearance and closing speed");
}

void cooldown_and_delay_are_rate_stable()
{
    for (const int hz : {30, 60, 120, 1000}) {
        NpcHornPolicy policy;
        const auto obstruction = stopped_obstruction();
        advance(policy, 11.45, obstruction, hz);
        require(policy.state().event_sequence == 2,
            "fixed simulation time must yield the same sparse event count");
        advance(policy, 0.10, obstruction, hz);
        require(policy.state().event_sequence == 3,
            "cooldown boundary must be stable across supported tick rates");
    }
}

void invalid_input_fails_silent_without_bypassing_cooldown()
{
    NpcHornPolicy policy;
    auto hazard = stopped_obstruction();
    hazard.motion_context = NpcHornMotionContext::Normal;
    hazard.speed_mps = hazard.closing_speed_mps = 4.0;
    hazard.obstacle_clearance_m = 1.0;
    policy.step(1.0 / 60.0, hazard);
    const double cooldown = policy.state().cooldown_remaining_s;
    hazard.speed_mps = std::numeric_limits<double>::quiet_NaN();
    policy.step(1.0 / 60.0, hazard);
    require(policy.state().invalid_input && !policy.state().active
            && policy.state().event_sequence == 1
            && near(policy.state().cooldown_remaining_s, cooldown),
        "malformed perception must silence output and preserve anti-spam state");
    policy.reset();
    require(policy.state().event_sequence == 0
            && near(policy.state().cooldown_remaining_s, 0.0),
        "explicit lifecycle reset must clear the complete policy state");
}

void invalid_config_is_rejected()
{
    for (const int field : {0, 1, 2}) {
        NpcHornPolicyConfig config;
        if (field == 0) config.pulse_duration_s = 0.0;
        if (field == 1) config.cooldown_s = 0.1;
        if (field == 2) config.imminent_release_ttc_s = config.imminent_ttc_s;
        bool rejected = false;
        try { NpcHornPolicy policy(config); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid horn policy config must fail at construction");
    }
}

} // namespace

int main()
{
    try {
        persistent_obstruction_waits_then_pulses_with_cooldown();
        ordinary_traffic_stops_never_horn();
        nonresponsive_obstruction_has_one_warning_despite_observation_gaps();
        a_different_nonresponsive_entity_and_reset_allow_one_new_warning();
        nonresponsive_ttc_chatter_does_not_repeat_a_warning();
        imminent_collision_is_one_shot_until_hysteresis_release();
        bumper_clearance_and_closing_speed_define_ttc();
        cooldown_and_delay_are_rate_stable();
        invalid_input_fails_silent_without_bypassing_cooldown();
        invalid_config_is_rejected();
        std::cout << "npc_horn_policy_tests: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_horn_policy_tests: " << error.what() << '\n';
        return 1;
    }
}
