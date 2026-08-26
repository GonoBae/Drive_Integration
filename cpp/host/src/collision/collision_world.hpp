#pragma once

#include "collision/collision_types.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace simcore_host {

enum class CollisionBroadPhaseMode {
    UniformGrid,
    // Deterministic reference path used for equivalence tests and diagnostics.
    BruteForceReference,
};

// Returns an OBB-prism manifold only when both the horizontal projections and
// vertical intervals overlap. The normal always points obstacle -> body.
[[nodiscard]] std::optional<CollisionManifold> intersect_obb_prisms(
    const ObbPrism& body,
    const ObbPrism& obstacle,
    CollisionVector2 preferred_body_velocity_enu = {});

// Horizontal OBB-circle narrow phase with a vertical interval test. The
// returned normal points capsule -> OBB body.
[[nodiscard]] std::optional<CollisionManifold> intersect_obb_vertical_capsule(
    const ObbPrism& body,
    const VerticalCapsule& capsule,
    CollisionVector2 preferred_relative_velocity_enu = {});

// Deterministic, ground-bound collision core. The world owns immutable static
// OBB prisms; tick-local dynamic proxies use the same narrow phase without
// coupling collision code to VehiclePhysics.
class CollisionWorld {
public:
    explicit CollisionWorld(std::vector<StaticObbCollider> static_colliders = {});
    CollisionWorld(
        std::vector<StaticObbCollider> static_colliders,
        CollisionBroadPhaseMode broad_phase_mode);

    [[nodiscard]] CollisionStepResult integrate(
        PlanarRigidBody body, double dt_seconds) const;

    // Dynamic proxies are copied, validated, sorted by proxy_id, and advanced
    // inside this tick's deterministic micro-substeps. The original two-arg
    // API remains the static-only compatibility path.
    [[nodiscard]] CollisionStepResult integrate(
        PlanarRigidBody body,
        double dt_seconds,
        std::vector<KinematicCollisionProxy> dynamic_proxies) const;

    [[nodiscard]] std::size_t static_collider_count() const {
        return static_colliders_.size();
    }

    [[nodiscard]] const std::vector<StaticObbCollider>& static_colliders() const {
        return static_colliders_;
    }

private:
    struct StaticGridIndex;

    std::vector<StaticObbCollider> static_colliders_;
    CollisionBroadPhaseMode broad_phase_mode_ =
        CollisionBroadPhaseMode::UniformGrid;
    std::shared_ptr<const StaticGridIndex> static_grid_index_;
};

} // namespace simcore_host
