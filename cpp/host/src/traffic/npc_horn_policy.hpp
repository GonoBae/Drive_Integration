#pragma once

#include <cstdint>
#include <optional>

namespace simcore_host {

enum class NpcHornMotionContext : std::uint8_t {
    Normal,
    ObstructionStop,
    TrafficControlStop,
    Inactive,
};

enum class NpcHornReason : std::uint8_t {
    None,
    PersistentObstruction,
    ImminentCollision,
};

struct NpcHornPolicyConfig {
    double pulse_duration_s = 0.35;
    double cooldown_s = 5.0;
    double obstruction_delay_s = 1.5;
    double stopped_speed_mps = 0.35;
    double maximum_obstruction_clearance_m = 10.0;
    double minimum_imminent_closing_speed_mps = 2.0;
    double imminent_ttc_s = 0.8;
    double imminent_release_ttc_s = 1.2;
};

// Tick-local perception supplied by the host. obstacle_clearance_m is the
// longitudinal free space from the NPC's front bumper to the first detected
// obstacle surface, not a centre-to-centre distance. closing_speed_mps is
// non-negative; callers may conservatively use own speed when the obstacle's
// velocity is unavailable.
struct NpcHornObservation {
    bool enabled = true;
    NpcHornMotionContext motion_context = NpcHornMotionContext::Normal;
    bool persistent_obstruction = false;
    double speed_mps = 0.0;
    double closing_speed_mps = 0.0;
    std::optional<double> obstacle_clearance_m;
};

struct NpcHornState {
    bool active = false;
    NpcHornReason reason = NpcHornReason::None;
    std::uint32_t event_sequence = 0;
    double active_remaining_s = 0.0;
    double cooldown_remaining_s = 0.0;
    double obstruction_seconds = 0.0;
    bool invalid_input = false;
};

// Pure deterministic intent/pulse policy. It owns no audio, wall clock,
// collision query or random source. A persistent obstruction may produce a
// short reminder after each cooldown; one continuous imminent hazard produces
// at most one event until its TTC leaves the hysteresis band.
class NpcHornPolicy {
public:
    explicit NpcHornPolicy(NpcHornPolicyConfig config = {});

    const NpcHornState& step(double dt_seconds,
                             const NpcHornObservation& observation) noexcept;
    void reset() noexcept;

    [[nodiscard]] const NpcHornState& state() const noexcept { return state_; }
    [[nodiscard]] const NpcHornPolicyConfig& config() const noexcept { return config_; }

private:
    void trigger(NpcHornReason reason) noexcept;

    NpcHornPolicyConfig config_;
    NpcHornState state_;
    bool imminent_latched_ = false;
};

} // namespace simcore_host
