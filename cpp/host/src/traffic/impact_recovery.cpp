#include "traffic/impact_recovery.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace simcore_host {
namespace {

constexpr double epsilon = 1.0e-9;
// Only the peak severity of a contact episode is charged. Resting pressure or
// a stream of small solver impulses must not damage a vehicle forever.
constexpr double damage_onset_delta_v_mps = 1.25;

double episode_damage(double delta_v_mps)
{
    return 100.0 * std::clamp(
        (delta_v_mps - damage_onset_delta_v_mps)
            / (ImpactRecoveryState::disabling_delta_v_mps - damage_onset_delta_v_mps),
        0.0, 1.0);
}

} // namespace

const char* impact_recovery_phase_name(ImpactRecoveryPhase phase) noexcept
{
    switch (phase) {
    case ImpactRecoveryPhase::Driving: return "Driving";
    case ImpactRecoveryPhase::Settling: return "Settling";
    case ImpactRecoveryPhase::Holding: return "Holding";
    case ImpactRecoveryPhase::Recovering: return "Recovering";
    case ImpactRecoveryPhase::Disabled: return "Disabled";
    }
    return "Disabled";
}

void ImpactRecoveryState::fail_closed() noexcept
{
    invalid_input_ = true;
    phase_ = ImpactRecoveryPhase::Disabled;
    pending_delta_v_mps_ = 0.0;
    hold_remaining_seconds_ = 0.0;
}

void ImpactRecoveryState::record_contact(double normal_impulse_n_s, double mass_kg) noexcept
{
    if (invalid_input_) { return; }
    if (!std::isfinite(normal_impulse_n_s) || normal_impulse_n_s < 0.0
        || !std::isfinite(mass_kg) || mass_kg <= 0.0) {
        fail_closed();
        return;
    }
    const double delta_v_mps = normal_impulse_n_s / mass_kg;
    if (!std::isfinite(delta_v_mps)
        || !std::isfinite(pending_delta_v_mps_ + delta_v_mps)) {
        fail_closed();
        return;
    }
    pending_delta_v_mps_ += delta_v_mps;
    if (normal_impulse_n_s > 0.0) last_contact_mass_kg_ = mass_kg;
}

void ImpactRecoveryState::clear_episode() noexcept
{
    episode_active_ = false;
    episode_event_emitted_ = false;
    episode_peak_delta_v_mps_ = 0.0;
    episode_damage_percent_ = 0.0;
}

void ImpactRecoveryState::update_window(double dt_seconds, double delta_v_mps)
{
    // Treat the current tick's solved impulse as uniform over its interval.
    // Trimming a partial interval keeps a 250ms sustained impulse invariant
    // under 30/60/120Hz subdivisions, instead of using an integer tick count.
    if (dt_seconds == 0.0 && delta_v_mps == 0.0) { return; }
    if (dt_seconds == 0.0 && !window_.empty() && window_.back().duration_seconds == 0.0) {
        window_.back().delta_v_mps += delta_v_mps;
    } else {
        window_.push_back({dt_seconds, delta_v_mps});
    }
    window_duration_seconds_ += dt_seconds;
    window_delta_v_mps_ += delta_v_mps;
    double excess = std::max(0.0, window_duration_seconds_ - contact_window_seconds);
    while (!window_.empty() && excess > epsilon) {
        auto& oldest = window_.front();
        if (oldest.duration_seconds <= excess + epsilon) {
            excess = std::max(0.0, excess - oldest.duration_seconds);
            window_duration_seconds_ -= oldest.duration_seconds;
            window_delta_v_mps_ -= oldest.delta_v_mps;
            window_.pop_front();
        } else {
            const double removed_delta_v = oldest.delta_v_mps
                * (excess / oldest.duration_seconds);
            oldest.duration_seconds -= excess;
            oldest.delta_v_mps -= removed_delta_v;
            window_duration_seconds_ -= excess;
            window_delta_v_mps_ -= removed_delta_v;
            excess = 0.0;
        }
    }
    window_delta_v_mps_ = std::max(0.0, window_delta_v_mps_);
}

double ImpactRecoveryState::hold_duration_seconds() const noexcept
{
    return minimum_hold_seconds
        + (maximum_hold_seconds - minimum_hold_seconds)
            * std::clamp(severity_delta_v_mps_ / disabling_delta_v_mps, 0.0, 1.0);
}

void ImpactRecoveryState::tick(double dt_seconds, bool physically_settled, bool enabled)
{
    if (invalid_input_) { pending_delta_v_mps_ = 0.0; return; }
    if (!std::isfinite(dt_seconds) || dt_seconds < 0.0 || dt_seconds > 10.0) {
        fail_closed();
        return;
    }
    if (!enabled) { pending_delta_v_mps_ = 0.0; return; }

    const double contact_delta_v = pending_delta_v_mps_;
    pending_delta_v_mps_ = 0.0;
    const bool driving_disabled = disabled();
    // Collect every positive solved impulse before applying a noise floor.
    // A high-frequency physics tick must not hide the same impact waveform.
    update_window(dt_seconds, contact_delta_v);
    const bool contact = contact_delta_v > 0.0
        && window_delta_v_mps_ + epsilon >= minimum_contact_delta_v_mps;
    const bool reactive_contact = contact
        && (window_delta_v_mps_ + epsilon >= reaction_delta_v_mps
            || contact_delta_v + epsilon >= reaction_delta_v_mps);
    // A disabled body may retain low solver pressure while resting against
    // another collider. Only a genuinely reactive hit may open a new visual
    // impact episode in that state; otherwise the pressure eventually sums to
    // the presentation threshold and replays dents/ragdoll effects forever.
    if (driving_disabled ? reactive_contact : contact) {
        if (!episode_active_) {
            clear_episode();
            episode_active_ = true;
        }
    }
    if (reactive_contact) {
        quiet_seconds_ = 0.0;
    } else {
        quiet_seconds_ = std::min(contact_window_seconds, quiet_seconds_ + dt_seconds);
    }
    if (contact) {
        episode_quiet_seconds_ = 0.0;
    } else {
        episode_quiet_seconds_ = std::min(
            contact_window_seconds, episode_quiet_seconds_ + dt_seconds);
    }

    const double previous_hold_duration = hold_duration_seconds();
    if (episode_active_) {
        episode_peak_delta_v_mps_ = std::max({
            episode_peak_delta_v_mps_, window_delta_v_mps_, contact_delta_v});
        if (episode_peak_delta_v_mps_ + epsilon >= presentation_delta_v_mps) {
            if (!episode_event_emitted_) {
                if (event_sequence_ != std::numeric_limits<std::uint32_t>::max())
                    ++event_sequence_;
                episode_event_emitted_ = true;
            }
            last_impact_impulse_n_s_ = std::min(1.0e7,
                episode_peak_delta_v_mps_ * last_contact_mass_kg_);
        }
    }
    // Disabled only stops the route motor; it must not suppress a subsequent
    // impact event used by dent/ragdoll presentation. Damage remains latched.
    if (driving_disabled) {
        if (!contact && episode_quiet_seconds_ + epsilon >= contact_window_seconds) clear_episode();
        return;
    }
    if (episode_active_) {
        const double new_episode_damage = episode_damage(episode_peak_delta_v_mps_);
        damage_percent_ = std::clamp(
            damage_percent_ + std::max(0.0, new_episode_damage - episode_damage_percent_),
            0.0, 100.0);
        episode_damage_percent_ = new_episode_damage;
        if (episode_peak_delta_v_mps_ + epsilon >= disabling_delta_v_mps) {
            phase_ = ImpactRecoveryPhase::Disabled;
            hold_remaining_seconds_ = 0.0;
            severity_delta_v_mps_ = std::max(severity_delta_v_mps_, episode_peak_delta_v_mps_);
            return;
        }
    }

    if (reactive_contact && episode_peak_delta_v_mps_ + epsilon >= reaction_delta_v_mps) {
        if (phase_ == ImpactRecoveryPhase::Driving) {
            severity_delta_v_mps_ = episode_peak_delta_v_mps_;
        } else {
            severity_delta_v_mps_ = std::max(severity_delta_v_mps_, episode_peak_delta_v_mps_);
        }
        if (phase_ == ImpactRecoveryPhase::Driving || phase_ == ImpactRecoveryPhase::Recovering) {
            phase_ = ImpactRecoveryPhase::Settling;
            hold_remaining_seconds_ = hold_duration_seconds();
        } else if (phase_ == ImpactRecoveryPhase::Holding) {
            hold_remaining_seconds_ += std::max(0.0, hold_duration_seconds() - previous_hold_duration);
        }
    }

    if (phase_ == ImpactRecoveryPhase::Settling) {
        hold_remaining_seconds_ = hold_duration_seconds();
        if (physically_settled) { phase_ = ImpactRecoveryPhase::Holding; }
    } else if (phase_ == ImpactRecoveryPhase::Holding) {
        if (!physically_settled) {
            phase_ = ImpactRecoveryPhase::Settling;
            hold_remaining_seconds_ = hold_duration_seconds();
        } else {
            hold_remaining_seconds_ = std::max(0.0, hold_remaining_seconds_ - dt_seconds);
            if (hold_remaining_seconds_ <= epsilon && quiet_seconds_ + epsilon >= contact_window_seconds) {
                hold_remaining_seconds_ = 0.0;
                phase_ = ImpactRecoveryPhase::Recovering;
            }
        }
    } else if (phase_ == ImpactRecoveryPhase::Recovering && !physically_settled) {
        phase_ = ImpactRecoveryPhase::Settling;
        hold_remaining_seconds_ = hold_duration_seconds();
    }

    if (!contact && episode_quiet_seconds_ + epsilon >= contact_window_seconds) { clear_episode(); }
}

void ImpactRecoveryState::finish_recovery() noexcept
{
    if (phase_ != ImpactRecoveryPhase::Recovering) { return; }
    phase_ = ImpactRecoveryPhase::Driving;
    hold_remaining_seconds_ = 0.0;
}

void ImpactRecoveryState::disable_driving() noexcept
{
    phase_ = ImpactRecoveryPhase::Disabled;
    pending_delta_v_mps_ = 0.0;
    hold_remaining_seconds_ = 0.0;
}

void ImpactRecoveryState::reset() noexcept
{
    phase_ = ImpactRecoveryPhase::Driving;
    invalid_input_ = false;
    pending_delta_v_mps_ = 0.0;
    quiet_seconds_ = contact_window_seconds;
    episode_quiet_seconds_ = contact_window_seconds;
    damage_percent_ = 0.0;
    severity_delta_v_mps_ = 0.0;
    event_sequence_ = 0;
    last_contact_mass_kg_ = 0.0;
    last_impact_impulse_n_s_ = 0.0;
    hold_remaining_seconds_ = 0.0;
    clear_episode();
    window_duration_seconds_ = 0.0;
    window_delta_v_mps_ = 0.0;
    window_.clear();
}

} // namespace simcore_host
