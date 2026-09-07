#pragma once

#include <cstdint>
#include <deque>

namespace simcore_host {

enum class ImpactRecoveryPhase {
    Driving,
    Settling,
    Holding,
    Recovering,
    Disabled,
};

[[nodiscard]] const char* impact_recovery_phase_name(ImpactRecoveryPhase phase) noexcept;

// Deterministic, pose-independent post-impact policy. The host owns collision
// response, settling detection and a collision-checked return to the route.
// Thresholds are demo tuning, not a prediction of real vehicle crash damage.
class ImpactRecoveryState {
public:
    // Noise floor for the entire rolling contact window, never per tick.
    static constexpr double minimum_contact_delta_v_mps = 0.02;
    // A small parking-speed touch still produces a presentation event, but it
    // must not stop the route motor for several seconds. Recovery begins only
    // after a clearly perceptible body-speed change.
    static constexpr double presentation_delta_v_mps = 0.15;
    static constexpr double reaction_delta_v_mps = 0.75;
    static constexpr double disabling_delta_v_mps = 6.0;
    static constexpr double contact_window_seconds = 0.25;
    static constexpr double minimum_hold_seconds = 2.0;
    static constexpr double maximum_hold_seconds = 5.0;
    static constexpr double recovery_speed_mps = 0.35;
    static constexpr double recovery_yaw_speed_radps = 0.13962634015954636;

    // Sum all solved normal impulses before tick(). Impulses divided among
    // contacts/solver passes in the same tick receive the same classification.
    // Invalid impulse/mass latches Disabled; reset() is the only way to clear it.
    void record_contact(double normal_impulse_n_s, double mass_kg) noexcept;

    // dt must be finite and in [0, 10] seconds. enabled=false pauses policy time
    // and discards pending contacts, without releasing an existing disable latch.
    // physically_settled describes residual impact velocity, not recovery motion.
    void tick(double dt_seconds, bool physically_settled, bool enabled = true);
    void finish_recovery() noexcept;
    // Physical immobilization (for example a car resting on its side/roof).
    // Does not invent additional impact damage; only reset releases the latch.
    void disable_driving() noexcept;
    void reset() noexcept;

    [[nodiscard]] ImpactRecoveryPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool active() const noexcept { return phase_ != ImpactRecoveryPhase::Driving; }
    [[nodiscard]] bool allows_driving() const noexcept { return phase_ == ImpactRecoveryPhase::Driving; }
    [[nodiscard]] bool disabled() const noexcept { return phase_ == ImpactRecoveryPhase::Disabled; }
    [[nodiscard]] bool invalid_input() const noexcept { return invalid_input_; }
    [[nodiscard]] double damage_percent() const noexcept { return damage_percent_; }
    [[nodiscard]] double severity_delta_v_mps() const noexcept { return severity_delta_v_mps_; }
    [[nodiscard]] double episode_delta_v_mps() const noexcept { return episode_peak_delta_v_mps_; }
    [[nodiscard]] double hold_remaining_seconds() const noexcept { return hold_remaining_seconds_; }
    [[nodiscard]] double hold_duration_seconds() const noexcept;
    // One presentation event per contact episode, including later hits on a
    // disabled body. The episode peak may grow without replaying the event.
    [[nodiscard]] std::uint32_t event_sequence() const noexcept { return event_sequence_; }
    [[nodiscard]] double last_impact_impulse_n_s() const noexcept { return last_impact_impulse_n_s_; }

private:
    struct ContactInterval {
        double duration_seconds = 0.0;
        double delta_v_mps = 0.0;
    };

    void fail_closed() noexcept;
    void update_window(double dt_seconds, double delta_v_mps);
    void clear_episode() noexcept;

    ImpactRecoveryPhase phase_ = ImpactRecoveryPhase::Driving;
    bool invalid_input_ = false;
    bool episode_active_ = false;
    bool episode_event_emitted_ = false;
    std::uint32_t event_sequence_ = 0;
    double last_contact_mass_kg_ = 0.0;
    double last_impact_impulse_n_s_ = 0.0;
    double pending_delta_v_mps_ = 0.0;
    double quiet_seconds_ = contact_window_seconds;
    double episode_quiet_seconds_ = contact_window_seconds;
    double window_duration_seconds_ = 0.0;
    double window_delta_v_mps_ = 0.0;
    double episode_peak_delta_v_mps_ = 0.0;
    double episode_damage_percent_ = 0.0;
    double damage_percent_ = 0.0;
    double severity_delta_v_mps_ = 0.0;
    double hold_remaining_seconds_ = 0.0;
    std::deque<ContactInterval> window_;
};

} // namespace simcore_host
