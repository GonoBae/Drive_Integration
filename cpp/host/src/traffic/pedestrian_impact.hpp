#pragma once

#include "collision/collision_types.hpp"
#include "traffic/impact_tumble.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace simcore_host {

struct PedestrianVehicleSurface {
    std::string vehicle_id;
    ObbPrism shape;
};

struct PedestrianImpactGeometry {
    // Absolute contact height. NaN selects a typical knee-height bumper;
    // callers with actual source geometry override this approximation.
    double contact_up_m = std::numeric_limits<double>::quiet_NaN();
};

// Reduced authoritative pelvis/body trajectory. Articulated limb motion is
// presentation only; this OBB replaces (never supplements) the standing capsule.
class PedestrianImpactState {
public:
    // Match the 1.8 m standing capsule, then use a plausible 36 cm torso depth
    // once it lies down.  The former 0.95/0.25 m pair made the finite body taller
    // than the standing capsule at the first tilted frame and visibly lifted its
    // centre before gravity had acted.
    static constexpr ImpactTumbleDimensions body_dimensions{0.18, 0.35, 0.90, 80.0, 0.25};
    // Contact events include small shoves. Losing balance has a demo-tuning
    // threshold; flight is a geometric contact/gravity result, not a threshold.
    static constexpr double contact_window_seconds = 0.25;
    static constexpr double knockdown_delta_v_mps = 1.5;
    static constexpr double knockdown_impulse_n_s = body_dimensions.mass_kg * knockdown_delta_v_mps;
    void contact(std::uint32_t event, double impulse_n_s, CollisionVector2 direction,
                 double current_center_up_m, double ground_up_m,
                 const PedestrianImpactGeometry& geometry = {}) noexcept;
    void tick(double dt, double ground_up_m, bool recovering, bool enabled = true,
              CollisionVector2 center = {},
              std::span<const PedestrianVehicleSurface> vehicles = {}) noexcept;
    [[nodiscard]] bool downed() const noexcept { return downed_; }
    [[nodiscard]] bool airborne() const noexcept { return airborne_; }
    [[nodiscard]] bool settled() const noexcept;
    [[nodiscard]] double center_up_m() const noexcept { return center_up_m_; }
    [[nodiscard]] double vertical_velocity_mps() const noexcept { return vertical_velocity_mps_; }
    [[nodiscard]] double heading_rad() const noexcept { return heading_rad_; }
    [[nodiscard]] double pitch_rad() const noexcept { return pitch_rad_; }
    [[nodiscard]] double pitch_rate_rad_s() const noexcept { return pitch_rate_rad_s_; }
    [[nodiscard]] ImpactTumbleSupport support() const noexcept;
    [[nodiscard]] const std::string& supported_vehicle_id() const noexcept { return supported_vehicle_id_; }
    void reset() noexcept { *this = {}; }
private:
    bool downed_ = false;
    bool airborne_ = false;
    std::uint32_t event_ = 0;
    double event_impulse_n_s_ = 0.0;
    double event_age_seconds_ = 0.0;
    double center_up_m_ = 0.0;
    double vertical_velocity_mps_ = 0.0;
    double heading_rad_ = 0.0;
    double pitch_rad_ = 0.0;
    double pitch_rate_rad_s_ = 0.0;
    double fall_direction_ = 1.0;
    std::string supported_vehicle_id_;
};

} // namespace simcore_host
