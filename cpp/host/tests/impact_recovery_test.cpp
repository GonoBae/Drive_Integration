#include "traffic/impact_recovery.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

using namespace simcore_host;

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

bool near(double first, double second, double epsilon = 1.0e-7)
{
    return std::isfinite(first) && std::isfinite(second) && std::abs(first - second) <= epsilon;
}

void advance(ImpactRecoveryState& state, double seconds, bool settled = true, int hz = 60)
{
    const int ticks = static_cast<int>(std::lround(seconds * hz));
    for (int index = 0; index < ticks; ++index) { state.tick(1.0 / hz, settled); }
}

void defaults_and_names()
{
    ImpactRecoveryState state;
    require(state.phase() == ImpactRecoveryPhase::Driving && state.allows_driving()
                && !state.active() && !state.disabled(), "reset policy must allow driving");
    require(state.damage_percent() == 0.0 && state.severity_delta_v_mps() == 0.0,
            "reset damage and severity must be zero");
    require(std::string_view(impact_recovery_phase_name(ImpactRecoveryPhase::Holding)) == "Holding",
            "phase names must be readable for diagnostics");
    state.finish_recovery();
    require(state.allows_driving(), "finishing inactive recovery must be a no-op");
}

void impulse_accumulation_and_mass_scaling()
{
    ImpactRecoveryState car;
    ImpactRecoveryState pedestrian;
	for (int index = 0; index < 20; ++index) { car.record_contact(60.0, 1500.0); }
	pedestrian.record_contact(64.0, 80.0);
    car.tick(1.0 / 60.0, false);
    pedestrian.tick(1.0 / 60.0, false);
    require(car.phase() == ImpactRecoveryPhase::Settling, "same-tick solver impulses must aggregate");
	require(near(car.severity_delta_v_mps(), 0.8)
                && near(car.severity_delta_v_mps(), pedestrian.severity_delta_v_mps()),
            "reaction classification must scale with proxy mass");
}

void tiny_resting_contacts_do_not_trigger_recovery_or_damage()
{
    for (const int hz : {30, 60, 120, 1000}) {
        ImpactRecoveryState state;
        for (int index = 0; index < 60 * hz; ++index) {
            state.record_contact(1500.0 * 0.04 / hz, 1500.0); // 0.01m/s per 250ms window.
            state.tick(1.0 / hz, true);
        }
        require(state.allows_driving() && state.damage_percent() == 0.0,
                "tiny sustained contacts must not hold or progressively disable actors");
    }
}

void settling_hold_then_explicit_recovery_completion()
{
    ImpactRecoveryState state;
    state.record_contact(2250.0, 1500.0);
    state.tick(1.0 / 60.0, false);
    advance(state, 5.0, false);
    require(state.phase() == ImpactRecoveryPhase::Settling,
            "recovery must wait until the actual impact velocity settles");
    state.finish_recovery();
    require(state.phase() == ImpactRecoveryPhase::Settling, "host cannot skip settling");
    state.tick(1.0 / 60.0, true);
	require(state.phase() == ImpactRecoveryPhase::Holding && near(state.hold_remaining_seconds(), 2.75),
            "settled contact needs a severity-scaled hold at the displaced pose");
	advance(state, 2.5);
	require(state.phase() == ImpactRecoveryPhase::Holding && !state.allows_driving(),
			"route progress must stay frozen during the hold");
	advance(state, 0.5);
    require(state.phase() == ImpactRecoveryPhase::Recovering && !state.allows_driving(),
            "hold completion authorizes slow recovery, not route driving");
    advance(state, 20.0);
    require(state.phase() == ImpactRecoveryPhase::Recovering, "only host geometry can finish recovery");
    state.finish_recovery();
    require(state.allows_driving() && state.damage_percent() > 0.0,
            "safe recovery completion permits driving but retains damage");
}

void unsettled_body_restarts_hold()
{
    ImpactRecoveryState state;
	state.record_contact(1500.0, 1500.0);
    state.tick(1.0 / 60.0, true);
    advance(state, 1.0);
    state.tick(1.0 / 60.0, false);
    require(state.phase() == ImpactRecoveryPhase::Settling,
            "new physical displacement during hold must restart settling");
    state.tick(1.0 / 60.0, true);
    require(near(state.hold_remaining_seconds(), state.hold_duration_seconds()),
            "resettled actor must receive the complete hold duration");
}

void split_severe_impulse_disables_at_all_supported_rates()
{
    for (const int hz : {30, 60, 120, 1000}) {
        ImpactRecoveryState state;
		const int ticks = hz / 5; // The same 6.2m/s impulse over 200ms.
		for (int index = 0; index < ticks; ++index) {
			state.record_contact(1500.0 * 6.2 / ticks, 1500.0);
            state.tick(1.0 / hz, false);
        }
        require(state.disabled(), "severe contact cannot hide by splitting impulse across ticks");
        state.finish_recovery();
        advance(state, 30.0);
        require(state.disabled() && !state.allows_driving(), "severe damage must stay latched until reset");
        state.reset();
        require(state.allows_driving() && state.damage_percent() == 0.0, "reset must clear the crash latch");
    }
}

void split_minor_impulse_reacts_at_all_supported_rates()
{
    for (const int hz : {30, 60, 120, 1000}) {
        ImpactRecoveryState state;
		const int ticks = hz / 10; // Exactly the 0.75m/s recovery boundary over 100ms.
		for (int index = 0; index < ticks; ++index) {
			state.record_contact(1500.0 * 0.75 / ticks, 1500.0);
            state.tick(1.0 / hz, false);
        }
        require(state.phase() == ImpactRecoveryPhase::Settling
				&& near(state.severity_delta_v_mps(), 0.75),
                "the noise floor must not hide the same minor impact at higher tick rates");
    }
}

void sustained_contact_uses_bounded_window_peak()
{
    double reference_damage = -1.0;
    for (const int hz : {30, 60, 120, 1000}) {
        ImpactRecoveryState state;
        for (int index = 0; index < hz * 10; ++index) {
            state.record_contact(1500.0 * 4.8 / hz, 1500.0); // 1.2m/s per 250ms window.
            state.tick(1.0 / hz, true);
        }
        require(!state.disabled() && near(state.episode_delta_v_mps(), 1.2),
                "continuous low-strength pressure must not accumulate unbounded severity");
        require(state.phase() == ImpactRecoveryPhase::Holding,
                "recovery must not pull an actor through an ongoing contact");
        if (reference_damage < 0.0) { reference_damage = state.damage_percent(); }
        require(near(state.damage_percent(), reference_damage),
                "fractional 250ms window must have identical damage at 30/60/120/1000Hz");
        require(state.damage_percent() < 15.0, "one episode may only be charged for its peak severity");
    }
}

void low_pressure_has_no_damage()
{
    ImpactRecoveryState state;
    for (int index = 0; index < 1200; ++index) {
        state.record_contact(45.0, 1500.0); // 0.45m/s per 250ms.
        state.tick(1.0 / 60.0, true);
    }
    require(!state.disabled() && state.damage_percent() == 0.0,
            "a continuously nudged actor must not acquire crash damage");
}

void resting_contact_does_not_permanently_hold_a_crashed_car()
{
	ImpactRecoveryState state;
	state.record_contact(1500.0, 1500.0);
	state.tick(1.0 / 60.0, true);
	require(state.phase() == ImpactRecoveryPhase::Holding,
		"fixture must begin with a recoverable one metre-per-second impact");
	for (int index = 0; index < 240; ++index) {
		state.record_contact(45.0, 1500.0); // Resting solver pressure, 0.45m/s per window.
		state.tick(1.0 / 60.0, true);
	}
	require(state.phase() == ImpactRecoveryPhase::Recovering,
		"sub-threshold resting contact must not oscillate holding forever");
}

void separate_crashes_accumulate_damage()
{
    ImpactRecoveryState state;
	state.record_contact(4500.0, 1500.0);
    state.tick(1.0 / 60.0, false);
    const double first_damage = state.damage_percent();
	require(!state.disabled() && first_damage > 30.0, "first medium crash should be recoverable");
    advance(state, 0.5, false);
    state.record_contact(4500.0, 1500.0);
    state.tick(1.0 / 60.0, false);
	require(!state.disabled() && state.damage_percent() > first_damage,
			"distinct medium crashes accumulate damage without permanently welding an upright car");
}

void new_contact_interrupts_recovery()
{
    ImpactRecoveryState state;
	state.record_contact(1500.0, 1500.0);
    state.tick(1.0 / 60.0, true);
    advance(state, 3.0);
    require(state.phase() == ImpactRecoveryPhase::Recovering, "fixture must reach recovery");
	state.record_contact(1500.0, 1500.0);
    state.tick(1.0 / 60.0, false);
    require(state.phase() == ImpactRecoveryPhase::Settling, "another impact must interrupt return to route");
}

void pause_preserves_hold()
{
    ImpactRecoveryState state;
    state.record_contact(1500.0, 1500.0);
    state.tick(1.0 / 60.0, true);
    const double remaining = state.hold_remaining_seconds();
    for (int index = 0; index < 1200; ++index) { state.tick(1.0 / 60.0, true, false); }
    require(state.phase() == ImpactRecoveryPhase::Holding && near(state.hold_remaining_seconds(), remaining),
            "lifecycle pause cannot silently complete recovery");
}

void invalid_values_fail_closed()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double impulse : {nan, inf, -1.0}) {
        ImpactRecoveryState state;
        state.record_contact(impulse, 1500.0);
        require(state.disabled() && state.invalid_input(), "invalid impulse must fail closed");
    }
    for (const double mass : {nan, inf, -1.0, 0.0}) {
        ImpactRecoveryState state;
        state.record_contact(1.0, mass);
        require(state.disabled() && state.invalid_input(), "invalid mass must fail closed");
    }
    for (const double dt : {nan, inf, -1.0, 10.01}) {
        ImpactRecoveryState state;
        state.tick(dt, true);
        require(state.disabled() && state.invalid_input(), "invalid time step must fail closed");
        state.finish_recovery();
        require(state.disabled(), "finish recovery must not clear invalid-input latch");
    }
    ImpactRecoveryState overflow;
    overflow.record_contact(std::numeric_limits<double>::max(), std::numeric_limits<double>::min());
    require(overflow.disabled() && overflow.invalid_input(), "non-finite impulse/mass quotient must fail closed");
}

void zero_duration_and_large_single_impulse()
{
    ImpactRecoveryState state;
	state.record_contact(9000.0, 1500.0);
    state.tick(0.0, false);
    require(state.disabled(), "zero elapsed time must not discard a solved severe impulse");
    state.reset();
	state.record_contact(9000.0, 1500.0);
    state.tick(10.0, false);
    require(state.disabled(), "large valid tick must not dilute a single severe impulse");
}

void impact_events_survive_disable_without_replaying_resting_contact()
{
    ImpactRecoveryState state;
    state.record_contact(9000.0, 1500.0);
    state.tick(1.0 / 60.0, false);
    require(state.disabled() && state.event_sequence() == 1
                && near(state.last_impact_impulse_n_s(), 9000.0),
        "the first disabling crash must publish one impulse event");
    for (int index = 0; index < 120; ++index) {
        state.record_contact(45.0, 1500.0);
        state.tick(1.0 / 60.0, false);
    }
    require(state.disabled() && state.event_sequence() == 1,
        "continued contact on the same disabled car must not replay the crash effect every tick");
    advance(state, 0.5);
    state.record_contact(1500.0, 1500.0);
    state.tick(1.0 / 60.0, false);
    require(state.disabled() && state.event_sequence() == 2
                && near(state.last_impact_impulse_n_s(), 1500.0)
                && near(state.damage_percent(), 100.0),
        "a separate hit must publish a new event even when the car can no longer drive");
    state.reset();
    require(state.event_sequence() == 0 && state.last_impact_impulse_n_s() == 0.0,
        "new Play must clear persistent impact presentation state");
}

void split_impact_event_is_one_episode_at_all_rates()
{
    for (const int hz : {30, 60, 120, 1000}) {
        ImpactRecoveryState state;
        for (int index = 0; index < hz / 5; ++index) {
            state.record_contact(1500.0 * 0.2 / (hz / 5), 1500.0);
            state.tick(1.0 / hz, false);
        }
        require(state.event_sequence() == 1 && near(state.last_impact_impulse_n_s(), 300.0),
            "a contact split over 200ms must produce one event with the same impulse at every rate");
        advance(state, 1.0, true, hz);
        require(state.event_sequence() == 1,
            "settling and hold snapshots must not repeat the last impact event");
    }
}

} // namespace

int main()
{
    try {
        defaults_and_names();
        impulse_accumulation_and_mass_scaling();
        tiny_resting_contacts_do_not_trigger_recovery_or_damage();
        settling_hold_then_explicit_recovery_completion();
        unsettled_body_restarts_hold();
        split_severe_impulse_disables_at_all_supported_rates();
        split_minor_impulse_reacts_at_all_supported_rates();
        sustained_contact_uses_bounded_window_peak();
		low_pressure_has_no_damage();
		resting_contact_does_not_permanently_hold_a_crashed_car();
        separate_crashes_accumulate_damage();
        new_contact_interrupts_recovery();
        pause_preserves_hold();
        invalid_values_fail_closed();
        zero_duration_and_large_single_impulse();
        impact_events_survive_disable_without_replaying_resting_contact();
        split_impact_event_is_one_episode_at_all_rates();
        std::cout << "[PASS] Impact recovery policy: 17 cases\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }
}
