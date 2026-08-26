#pragma once

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace simcore_host {

// Collision geometry uses map_enu coordinates: X=east, Y=north, Z=up.
// heading_rad follows the vehicle/navigation convention: North=0 and positive
// rotation is clockwise. heading_rate_rad_s uses the same sign convention.
struct CollisionVector2 {
    double east_m = 0.0;
    double north_m = 0.0;
};

struct CollisionMaterial {
    double friction = 0.8;
    double restitution = 0.0;
};

struct ObbPrism {
    CollisionVector2 center_enu;
    double center_up_m = 0.0;
    double heading_rad = 0.0;
    double half_length_m = 0.0;
    double half_width_m = 0.0;
    double half_height_m = 0.0;
};

enum class StaticColliderSemantic {
    Wall,
    Curb,
    Barrier,
};

struct StaticObbCollider {
    std::string collider_id;
    StaticColliderSemantic semantic = StaticColliderSemantic::Barrier;
    ObbPrism shape;
    CollisionMaterial material;
};

// A vertical capsule is represented by its circular horizontal projection and
// full vertical interval. half_height_m includes the hemispherical caps and is
// therefore required to be at least radius_m.
struct VerticalCapsule {
    CollisionVector2 center_enu;
    double center_up_m = 0.0;
    double radius_m = 0.0;
    double half_height_m = 0.0;
};

using KinematicProxyShape = std::variant<ObbPrism, VerticalCapsule>;

// Tick-local collision snapshot for an NPC vehicle or pedestrian. Proxies are
// infinite-mass kinematic obstacles: their motion affects Ego but the solve
// never modifies the caller's authoritative proxy state.
struct KinematicCollisionProxy {
    std::string proxy_id;
    KinematicProxyShape shape;
    CollisionVector2 linear_velocity_enu_mps;
    // Navigation heading rate. It rotates OBB proxies and supplies surface
    // velocity for contact response; capsule orientation itself is invariant.
    double heading_rate_rad_s = 0.0;
    CollisionMaterial material;
};

struct PlanarRigidBody {
    std::string body_id;
    ObbPrism shape;
    CollisionVector2 linear_velocity_enu_mps;
    // Navigation heading rate: positive rotates North toward East.
    double heading_rate_rad_s = 0.0;
    double mass_kg = 0.0;
    double yaw_inertia_kg_m2 = 0.0;
};

struct CollisionManifold {
    // Unit vector pointing from the obstacle toward the body.
    CollisionVector2 normal_enu;
    CollisionVector2 contact_point_enu;
    double penetration_m = 0.0;
};

struct CollisionContact {
    std::string collider_id;
    CollisionVector2 normal_enu;
    CollisionVector2 contact_point_enu;
    double maximum_penetration_m = 0.0;
    double accumulated_normal_impulse_n_s = 0.0;
};

struct CollisionStepResult {
    PlanarRigidBody body;
    std::vector<CollisionContact> contacts;
    std::size_t substep_count = 0;
    bool motion_clamped = false;
    std::size_t broad_phase_query_count = 0;
    std::size_t narrow_phase_candidate_count = 0;
};

} // namespace simcore_host
