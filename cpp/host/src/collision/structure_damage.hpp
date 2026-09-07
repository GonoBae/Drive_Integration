#pragma once

#include "collision/collision_types.hpp"
#include "traffic/traffic_network.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace simcore_host {

enum class StructureKind : std::uint32_t { Building = 1, SignalPole = 2 };

struct StructureDamageSnapshot {
    std::string collider_id;
    StructureKind kind = StructureKind::Building;
    std::uint32_t signal_id = 0;
    double damage_percent = 0.0;
    std::uint32_t event_sequence = 0;
    GroundPointEnu impact_point_enu;
    GroundPointEnu impact_normal_enu{0.0, 1.0, 0.0};
    GroundPointEnu base_position_enu;
    double heading_rad = 0.0;
    double fall_angle_rad = 0.0;
    CollisionVector2 fall_direction_enu{0.0, 1.0};
    bool disabled = false;
    // Current contact patch, not the building-wide cumulative damage radius.
    double impact_half_width_m = 0.0;
    double impact_half_height_m = 0.0;
    double impact_severity = 0.0;
};

// Bounded demonstration damage policy, not a structural-engineering model.
// Building shells stay in the static world; only signal poles own runtime
// collision proxies. An intact base resists contact; a broken base releases a
// finite pole, preserving its solved translation while its fall is integrated.
class StructureDamageRuntime {
public:
    static constexpr std::size_t maximum_targets = 256;
    static constexpr double contact_window_seconds = 0.25;
    static constexpr double building_damage_onset_impulse_n_s = 900.0;
    static constexpr double building_full_damage_impulse_n_s = 12000.0;
    static constexpr double pole_damage_onset_impulse_n_s = 250.0;
    static constexpr double pole_collapse_impulse_n_s = 2500.0;
    static constexpr double pole_height_m = 3.85;
    static constexpr double pole_half_width_m = 0.25;
    static constexpr double pole_mass_kg = 180.0;

    // Rejects duplicate/reserved IDs and non-finite geometry transactionally.
    // traffic may be null. Wall targets retain the exact authored collider IDs.
    void rebuild(const std::vector<StaticObbCollider>& colliders, const TrafficNetwork* traffic);
    void reset();
    // Consumes prior record_contacts calls, then advances the hinge. Paused or
    // invalid dt ticks discard pending impulses but cannot heal existing damage.
    void tick(double dt_seconds, bool enabled);
    void record_contacts(const std::vector<CollisionContact>& contacts, double impact_height_m,
        const ObbPrism* impacting_body = nullptr);
    // Preserve finite motion from the same solve that affected the vehicle;
    // otherwise rebuilding a proxy at its old base creates an immovable ghost.
    void accept_resolved_proxies(const std::vector<KinematicCollisionProxy>& proxies);
    [[nodiscard]] const std::vector<StructureDamageSnapshot>& snapshots() const noexcept {
        return snapshots_;
    }
    [[nodiscard]] const std::vector<KinematicCollisionProxy>& collision_proxies() const noexcept {
        return proxies_;
    }
    // Apply to freshly evaluated snapshots. Disabled heads are dark/Red0;
    // functioning heads retain the independent controller's phase and timer.
    void apply_signal_faults(std::vector<TrafficSignalSnapshot>& signals) const;

private:
    struct ContactInterval {
        double duration_seconds = 0.0;
        double impulse_n_s = 0.0;
    };
    struct Target {
        StructureDamageSnapshot state;
        std::uint32_t controller_id = 0;
        double top_up_m = 0.0;
        double authored_heading_rad = 0.0;
        GroundPointEnu authored_base_position;
        bool breakaway_released = false;
        CollisionVector2 linear_velocity_enu_mps;
        double heading_rate_rad_s = 0.0;
        ObbPrism building_shape;
        double pending_impulse_n_s = 0.0;
        double pending_peak_impulse_n_s = 0.0;
        GroundPointEnu pending_point;
        GroundPointEnu pending_normal{0.0, 1.0, 0.0};
        double pending_half_width_m = 0.35;
        double pending_half_height_m = 0.30;
        double quiet_seconds = contact_window_seconds;
        double window_duration_seconds = 0.0;
        double window_impulse_n_s = 0.0;
        double episode_peak_impulse_n_s = 0.0;
        double episode_damage_percent = 0.0;
        bool episode_has_event = false;
        double fall_rate_rad_s = 0.0;
        std::deque<ContactInterval> window;
    };

    static void update_window(Target& target, double dt_seconds, double impulse_n_s);
    void refresh_caches();
    std::vector<Target> targets_;
    std::vector<StructureDamageSnapshot> snapshots_;
    std::vector<KinematicCollisionProxy> proxies_;
    bool motion_enabled_ = true;
};

} // namespace simcore_host
