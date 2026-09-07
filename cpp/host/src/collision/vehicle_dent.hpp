#pragma once

#include "collision/collision_types.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace simcore_host {
// Bounded cosmetic contact history. XY position is normalized to the undeformed
// body shell in FLU (forward/left), not a whole-panel damage category.
struct VehicleDentPatch {
    float forward = 0.0f;
    float left = 0.0f;
    float inward_forward = 0.0f;
    float inward_left = 0.0f;
    float radius_m = 0.0f;
    float depth_m = 0.0f;
};
inline constexpr std::size_t kMaximumVehicleDentPatches = 16;

inline bool valid_vehicle_dent(const VehicleDentPatch& p)
{
    const double length = std::hypot(p.inward_forward, p.inward_left);
    return std::isfinite(p.forward) && std::isfinite(p.left)
        && std::abs(p.forward) <= 1.001 && std::abs(p.left) <= 1.001
        && std::max(std::abs(p.forward), std::abs(p.left)) >= .99
        && std::isfinite(length) && std::abs(length - 1.0) < .01
        && std::isfinite(p.radius_m) && p.radius_m >= .15f && p.radius_m <= .95f
        && std::isfinite(p.depth_m) && p.depth_m > 0.0f && p.depth_m <= .28f
        && p.forward * p.inward_forward + p.left * p.inward_left < -.05;
}

inline void record_vehicle_dent(std::vector<VehicleDentPatch>& patches,
    const ObbPrism& body, const CollisionContact& contact, bool reverse_normal = false)
{
    const double impulse = contact.accumulated_normal_impulse_n_s;
    if (!std::isfinite(impulse) || impulse <= 2500.0) return;
    const double s = std::sin(body.heading_rad), c = std::cos(body.heading_rad);
    const double east = contact.contact_point_enu.east_m - body.center_enu.east_m;
    const double north = contact.contact_point_enu.north_m - body.center_enu.north_m;
    double f = std::clamp((east*s + north*c) / body.half_length_m, -1.0, 1.0);
    double l = std::clamp((-east*c + north*s) / body.half_width_m, -1.0, 1.0);
    // Project to the struck shell face while retaining the longitudinal/lateral
    // contact coordinate. A door-corner contact cannot crush the opposite door.
    const bool end_face = std::abs(f) >= std::abs(l);
    if (end_face) f = std::copysign(1.0, f); else l = std::copysign(1.0, l);
    const double sign = reverse_normal ? -1.0 : 1.0;
    double nf = sign*(contact.normal_enu.east_m*s + contact.normal_enu.north_m*c);
    double nl = sign*(-contact.normal_enu.east_m*c + contact.normal_enu.north_m*s);
    const double length = std::hypot(nf,nl);
    if (!std::isfinite(length) || length < .5) return;
    nf /= length; nl /= length;
    if (f*nf + l*nl >= -.05) { nf = end_face ? -f : 0.0; nl = end_face ? 0.0 : -l; }
    const double incidence = std::clamp(std::abs(end_face ? nf : nl), .2, 1.0);
    const double strength = std::clamp((impulse-2500.0)/20000.0, 0.0, 1.0);
    VehicleDentPatch patch{static_cast<float>(f),static_cast<float>(l),
        static_cast<float>(nf),static_cast<float>(nl),
        static_cast<float>(.25 + .55*std::sqrt(strength)),
        static_cast<float>(std::clamp((.015 + .265*std::sqrt(strength))*incidence,.005,.28))};
    if (!valid_vehicle_dent(patch)) return;
    for (auto& existing : patches) {
        const double distance = std::hypot((existing.forward-f)*body.half_length_m,
                                           (existing.left-l)*body.half_width_m);
        if (distance < .30 && existing.inward_forward*nf + existing.inward_left*nl > .8) {
            // Persistent solver pressure is not a fresh stack of dents. Keep
            // earlier geometry and only deepen this same small contact patch.
            existing.depth_m = std::max(existing.depth_m,patch.depth_m);
            existing.radius_m = std::max(existing.radius_m,patch.radius_m);
            return;
        }
    }
    // Never move an old dent to a new location when the fixed budget is full.
    if (patches.size() < kMaximumVehicleDentPatches) patches.push_back(patch);
}
}
