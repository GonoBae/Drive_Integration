#include "collision/collision_world.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(double lhs, double rhs, double tolerance = 1e-9)
{
    return std::abs(lhs - rhs) <= tolerance;
}

void require_same_physics_result(
    const simcore_host::CollisionStepResult& lhs,
    const simcore_host::CollisionStepResult& rhs,
    const char* message)
{
    const bool same_body =
        lhs.body.shape.center_enu.east_m == rhs.body.shape.center_enu.east_m
        && lhs.body.shape.center_enu.north_m == rhs.body.shape.center_enu.north_m
        && lhs.body.shape.heading_rad == rhs.body.shape.heading_rad
        && lhs.body.linear_velocity_enu_mps.east_m
            == rhs.body.linear_velocity_enu_mps.east_m
        && lhs.body.linear_velocity_enu_mps.north_m
            == rhs.body.linear_velocity_enu_mps.north_m
        && lhs.body.heading_rate_rad_s == rhs.body.heading_rate_rad_s
        && lhs.substep_count == rhs.substep_count
        && lhs.motion_clamped == rhs.motion_clamped;
    bool same_contacts = lhs.contacts.size() == rhs.contacts.size();
    for (std::size_t index = 0;
         same_contacts && index < lhs.contacts.size(); ++index) {
        const auto& left = lhs.contacts[index];
        const auto& right = rhs.contacts[index];
        same_contacts = left.collider_id == right.collider_id
            && left.normal_enu.east_m == right.normal_enu.east_m
            && left.normal_enu.north_m == right.normal_enu.north_m
            && left.contact_point_enu.east_m
                == right.contact_point_enu.east_m
            && left.contact_point_enu.north_m
                == right.contact_point_enu.north_m
            && left.maximum_penetration_m == right.maximum_penetration_m
            && left.accumulated_normal_impulse_n_s
                == right.accumulated_normal_impulse_n_s;
    }
    require(same_body && same_contacts, message);
}

simcore_host::StaticObbCollider make_collider(
    std::string id,
    simcore_host::CollisionVector2 center,
    double heading_rad,
    double half_length_m,
    double half_width_m,
    double center_up_m = 1.0,
    double half_height_m = 1.0,
    double friction = 0.0)
{
    return {
        std::move(id),
        simcore_host::StaticColliderSemantic::Wall,
        {center, center_up_m, heading_rad,
         half_length_m, half_width_m, half_height_m},
        {friction, 0.0}};
}

simcore_host::PlanarRigidBody make_body(
    simcore_host::CollisionVector2 center,
    simcore_host::CollisionVector2 velocity)
{
    return {
        "ego",
        {center, 0.5, 0.0, 1.0, 0.5, 0.5},
        velocity,
        0.0,
        1500.0,
        2500.0};
}

simcore_host::KinematicCollisionProxy make_npc_proxy(
    std::string id,
    simcore_host::CollisionVector2 center,
    simcore_host::CollisionVector2 velocity = {},
    double heading_rad = 0.0,
    double heading_rate_rad_s = 0.0)
{
    simcore_host::KinematicCollisionProxy proxy;
    proxy.proxy_id = std::move(id);
    proxy.shape = simcore_host::ObbPrism{
        center, 0.5, heading_rad, 1.0, 0.5, 0.5};
    proxy.linear_velocity_enu_mps = velocity;
    proxy.heading_rate_rad_s = heading_rate_rad_s;
    proxy.material = {0.0, 0.0};
    return proxy;
}

simcore_host::KinematicCollisionProxy make_pedestrian_proxy(
    std::string id,
    simcore_host::CollisionVector2 center,
    simcore_host::CollisionVector2 velocity = {},
    double center_up_m = 0.9)
{
    simcore_host::KinematicCollisionProxy proxy;
    proxy.proxy_id = std::move(id);
    proxy.shape = simcore_host::VerticalCapsule{
        center, center_up_m, 0.35, 0.9};
    proxy.linear_velocity_enu_mps = velocity;
    proxy.material = {0.0, 0.0};
    return proxy;
}

void require_not_overlapping(
    const simcore_host::ObbPrism& body,
    const simcore_host::ObbPrism& obstacle,
    const char* message)
{
    require(!simcore_host::intersect_obb_prisms(body, obstacle).has_value(),
            message);
}

void test_rotated_obb_sat_and_vertical_interval()
{
    const simcore_host::ObbPrism body{
        {0.0, 0.0}, 0.5, 0.4, 1.0, 0.5, 0.5};
    const simcore_host::ObbPrism overlapping{
        {0.9, 0.2}, 0.5, -0.3, 1.0, 0.4, 0.5};
    const simcore_host::ObbPrism horizontally_separated{
        {5.0, 0.0}, 0.5, -0.3, 1.0, 0.4, 0.5};
    const simcore_host::ObbPrism vertically_separated{
        {0.0, 0.0}, 3.0, 0.0, 1.0, 0.5, 0.5};

    const auto manifold = simcore_host::intersect_obb_prisms(
        body, overlapping);
    require(manifold.has_value() && manifold->penetration_m > 0.0,
            "rotated overlapping OBB prisms must produce a manifold");
    require(near(std::hypot(
                     manifold->normal_enu.east_m,
                     manifold->normal_enu.north_m),
                 1.0),
            "SAT contact normal must be unit length");
    require(!simcore_host::intersect_obb_prisms(
                 body, horizontally_separated).has_value(),
            "horizontally separated OBB prisms must not collide");
    require(!simcore_host::intersect_obb_prisms(
                 body, vertically_separated).has_value(),
            "disjoint vertical intervals must not collide");
}

void test_high_speed_head_on_motion_does_not_tunnel()
{
    const auto wall = make_collider(
        "wall", {0.0, 0.0}, std::numbers::pi_v<double> * 0.5,
        5.0, 0.05);
    const simcore_host::CollisionWorld world({wall});
    const auto result = world.integrate(
        make_body({0.0, -3.0}, {0.0, 50.0}), 0.1);

    require(!result.motion_clamped && result.substep_count >= 50,
            "50 m/s motion must use fixed micro-substeps without clamping");
    require(result.contacts.size() == 1
            && result.contacts[0].collider_id == "wall",
            "swept integration must report the wall contact");
    require(result.body.shape.center_enu.north_m <= -1.05 + 1e-6,
            "the body front must remain on the near side of the wall");
    require(result.body.linear_velocity_enu_mps.north_m <= 1e-9,
            "zero-restitution normal impulse must remove incoming speed");
    require_not_overlapping(
        result.body.shape, wall.shape,
        "head-on collision must not leave persistent penetration");
}

void test_glancing_contact_slides_without_normal_penetration()
{
    const auto wall = make_collider(
        "wall", {0.0, 0.0}, 0.0, 10.0, 0.05);
    const simcore_host::CollisionWorld world({wall});
    const auto result = world.integrate(
        make_body({-2.0, -1.0}, {20.0, 4.0}), 0.1);

    require(!result.contacts.empty(),
            "glancing motion must contact the wall");
    require(result.body.shape.center_enu.east_m <= -0.55 + 1e-6,
            "glancing motion must remain outside the wall");
    require(std::abs(result.body.linear_velocity_enu_mps.east_m) <= 1e-9,
            "wall impulse must remove only the incoming normal velocity");
    require(near(result.body.linear_velocity_enu_mps.north_m, 4.0, 1e-9),
            "zero-friction wall contact must preserve tangential velocity");
    require_not_overlapping(
        result.body.shape, wall.shape,
        "glancing collision must finish outside the wall");
}

void test_initial_deep_penetration_is_projected_out()
{
    const auto wall = make_collider(
        "wall", {0.0, 0.0}, 0.0, 2.0, 0.25);
    const simcore_host::CollisionWorld world({wall});
    auto body = make_body({0.0, 0.0}, {0.0, 0.0});
    body.shape.half_length_m = 0.75;
    const auto result = world.integrate(std::move(body), 1.0 / 60.0);

    require(!result.contacts.empty()
            && result.contacts[0].maximum_penetration_m > 0.0,
            "initial overlap must generate a contact");
    require_not_overlapping(
        result.body.shape, wall.shape,
        "position projection must eliminate initial deep penetration");
}

void test_collider_input_order_is_deterministic()
{
    const auto east_wall = make_collider(
        "a-east-wall", {0.0, 0.0}, 0.0, 10.0, 0.05);
    const auto north_wall = make_collider(
        "b-north-wall", {0.0, 0.0}, std::numbers::pi_v<double> * 0.5,
        10.0, 0.05);
    const auto body = make_body({-2.0, -2.0}, {20.0, 20.0});

    const auto first = simcore_host::CollisionWorld(
        {east_wall, north_wall}).integrate(body, 0.1);
    const auto second = simcore_host::CollisionWorld(
        {north_wall, east_wall}).integrate(body, 0.1);

    require(first.body.shape.center_enu.east_m
                == second.body.shape.center_enu.east_m
            && first.body.shape.center_enu.north_m
                == second.body.shape.center_enu.north_m
            && first.body.linear_velocity_enu_mps.east_m
                == second.body.linear_velocity_enu_mps.east_m
            && first.body.linear_velocity_enu_mps.north_m
                == second.body.linear_velocity_enu_mps.north_m
            && first.contacts.size() == second.contacts.size(),
            "collider construction order must not affect the solved state");
    for (std::size_t index = 0; index < first.contacts.size(); ++index) {
        require(first.contacts[index].collider_id
                    == second.contacts[index].collider_id,
                "contact ordering must be stable by collider ID");
    }
}

void test_empty_or_vertically_clear_world_preserves_motion()
{
    auto body = make_body({1.0, 2.0}, {3.0, 4.0});
    body.heading_rate_rad_s = 0.5;
    const auto empty_result = simcore_host::CollisionWorld{}.integrate(body, 0.1);
    require(near(empty_result.body.shape.center_enu.east_m, 1.3)
            && near(empty_result.body.shape.center_enu.north_m, 2.4)
            && near(empty_result.body.shape.heading_rad, 0.05),
            "an empty collision world must preserve unconstrained integration");

    const auto overhead = make_collider(
        "overhead", {1.0, 2.0}, 0.0, 10.0, 10.0, 5.0, 1.0);
    const auto clear_result = simcore_host::CollisionWorld({overhead}).integrate(
        body, 0.1);
    require(clear_result.contacts.empty()
            && near(clear_result.body.shape.center_enu.east_m, 1.3)
            && near(clear_result.body.shape.center_enu.north_m, 2.4),
            "vertical clearance must leave planar motion unconstrained");
}

void test_excess_motion_fails_closed_at_substep_limit()
{
    const auto result = simcore_host::CollisionWorld{}.integrate(
        make_body({0.0, 0.0}, {100.0, 0.0}), 0.1);
    require(result.motion_clamped && result.substep_count == 64,
            "motion beyond the deterministic substep budget must be flagged");
    require(result.body.shape.center_enu.east_m <= 6.4 + 1e-9,
            "motion beyond the substep budget must not be integrated unchecked");
}

void test_obb_capsule_narrow_phase_and_vertical_separation()
{
    const simcore_host::ObbPrism body{
        {0.0, 0.0}, 0.5, 0.0, 1.0, 0.5, 0.5};
    const simcore_host::VerticalCapsule overlapping{
        {0.7, 0.0}, 0.9, 0.35, 0.9};
    const simcore_host::VerticalCapsule vertically_clear{
        {0.7, 0.0}, 3.0, 0.35, 0.9};

    const auto manifold = simcore_host::intersect_obb_vertical_capsule(
        body, overlapping);
    require(manifold.has_value()
            && near(manifold->penetration_m, 0.15, 1e-9)
            && manifold->normal_enu.east_m < -0.999,
            "OBB-capsule narrow phase must return capsule-to-body normal");
    require(!simcore_host::intersect_obb_vertical_capsule(
                 body, vertically_clear).has_value(),
            "vertically separated pedestrian capsule must not collide");
}

void test_stationary_npc_obb_prevents_ego_penetration()
{
    const auto npc = make_npc_proxy("npc-7", {0.0, 0.0});
    const auto result = simcore_host::CollisionWorld{}.integrate(
        make_body({0.0, -4.0}, {0.0, 30.0}),
        0.1,
        {npc});

    require(result.contacts.size() == 1
            && result.contacts[0].collider_id == "npc-7",
            "Ego must report its kinematic NPC contact");
    require(result.body.linear_velocity_enu_mps.north_m <= 1e-9,
            "stationary infinite-mass NPC must remove incoming Ego speed");
    require_not_overlapping(
        result.body.shape,
        std::get<simcore_host::ObbPrism>(npc.shape),
        "Ego must not remain inside the NPC OBB");
}

void test_stationary_pedestrian_capsule_prevents_ego_penetration()
{
    const auto pedestrian = make_pedestrian_proxy(
        "pedestrian-3", {0.0, 0.0});
    const auto result = simcore_host::CollisionWorld{}.integrate(
        make_body({0.0, -3.0}, {0.0, 30.0}),
        0.1,
        {pedestrian});

    require(result.contacts.size() == 1
            && result.contacts[0].collider_id == "pedestrian-3",
            "Ego must report its pedestrian capsule contact");
    require(!simcore_host::intersect_obb_vertical_capsule(
                 result.body.shape,
                 std::get<simcore_host::VerticalCapsule>(pedestrian.shape))
                 .has_value(),
            "Ego must not remain inside the pedestrian capsule");
}

void test_approaching_kinematic_proxy_pushes_ego_with_relative_velocity()
{
    const auto npc = make_npc_proxy(
        "npc-approaching", {0.0, -4.0}, {0.0, 30.0});
    const auto result = simcore_host::CollisionWorld{}.integrate(
        make_body({0.0, 0.0}, {0.0, 0.0}),
        0.1,
        {npc});

    require(!result.contacts.empty(),
            "an approaching proxy must collide during the shared microsteps");
    require(result.body.shape.center_enu.north_m > 0.0
            && result.body.linear_velocity_enu_mps.north_m > 0.0,
            "relative normal impulse from a moving proxy must push Ego");
    auto final_npc = std::get<simcore_host::ObbPrism>(npc.shape);
    final_npc.center_enu.north_m += 3.0;
    require_not_overlapping(
        result.body.shape,
        final_npc,
        "moving infinite-mass NPC must finish without Ego penetration");
}

void test_finite_npc_receives_equal_opposite_normal_and_tangent_impulse()
{
    auto npc = make_npc_proxy("finite-npc", {0.0, 0.0});
    npc.material = {0.8, 0.05};
    npc.mass_kg = 1500.0;
    npc.yaw_inertia_kg_m2 = 2500.0;
    npc.maximum_linear_speed_mps = 100.0;
    const auto body = make_body({-3.0, 0.0}, {30.0, 5.0});
    const double initial_east_momentum =
        body.mass_kg * body.linear_velocity_enu_mps.east_m;
    const double initial_north_momentum =
        body.mass_kg * body.linear_velocity_enu_mps.north_m;

    const auto result = simcore_host::CollisionWorld{}.integrate(
        body, 0.1, {npc});
    require(result.contacts.size() == 1
                && result.contacts[0].collider_id == "finite-npc"
                && result.resolved_dynamic_proxies.size() == 1,
            "finite NPC contact must return one sorted resolved proxy");
    const auto& resolved = result.resolved_dynamic_proxies[0];
    require(resolved.linear_velocity_enu_mps.east_m > 0.0
                && resolved.linear_velocity_enu_mps.north_m > 0.0
                && result.body.linear_velocity_enu_mps.east_m < 30.0
                && result.body.linear_velocity_enu_mps.north_m < 5.0,
            "finite NPC must receive both normal and friction impulse while Ego slows");
    require(near(
                result.body.mass_kg * result.body.linear_velocity_enu_mps.east_m
                    + resolved.mass_kg * resolved.linear_velocity_enu_mps.east_m,
                initial_east_momentum, 1e-6)
                && near(
                    result.body.mass_kg * result.body.linear_velocity_enu_mps.north_m
                        + resolved.mass_kg * resolved.linear_velocity_enu_mps.north_m,
                    initial_north_momentum, 1e-6),
            "uncapped finite pair impulses must conserve planar linear momentum");
    require_not_overlapping(
        result.body.shape,
        std::get<simcore_host::ObbPrism>(resolved.shape),
        "finite NPC and Ego must finish without overlap");
}

void test_finite_pedestrian_is_pushed_with_bounded_speed()
{
    auto pedestrian = make_pedestrian_proxy(
        "finite-pedestrian", {0.0, 0.0});
    pedestrian.material = {0.4, 0.0};
    pedestrian.mass_kg = 80.0;
    pedestrian.maximum_linear_speed_mps = 15.0;
    const auto result = simcore_host::CollisionWorld{}.integrate(
        make_body({-3.0, 0.0}, {40.0, 0.0}), 0.1, {pedestrian});

    require(result.contacts.size() == 1
                && result.resolved_dynamic_proxies.size() == 1,
            "finite pedestrian collision must return its authoritative response");
    const auto& resolved = result.resolved_dynamic_proxies[0];
    const double pedestrian_speed = std::hypot(
        resolved.linear_velocity_enu_mps.east_m,
        resolved.linear_velocity_enu_mps.north_m);
    require(pedestrian_speed > 0.0 && pedestrian_speed <= 15.0 + 1e-9
                && result.body.linear_velocity_enu_mps.east_m < 40.0,
            "light pedestrian must be pushed while its configured speed stays bounded");
    require(!simcore_host::intersect_obb_vertical_capsule(
                 result.body.shape,
                 std::get<simcore_host::VerticalCapsule>(resolved.shape))
                 .has_value(),
            "finite pedestrian and Ego must finish without overlap");
}

void test_rotating_npc_proxy_is_advanced_during_microsteps()
{
    auto rotating = make_npc_proxy(
        "npc-rotating", {1.5, 0.0}, {}, 0.0,
        std::numbers::pi_v<double> * 2.5);
    auto& shape = std::get<simcore_host::ObbPrism>(rotating.shape);
    shape.half_length_m = 2.0;
    shape.half_width_m = 0.1;

    const auto result = simcore_host::CollisionWorld{}.integrate(
        make_body({0.0, 0.0}, {0.0, 0.0}),
        0.1,
        {rotating});
    require(!result.contacts.empty()
            && result.contacts[0].collider_id == "npc-rotating",
            "angular proxy motion must participate in microstep collision detection");
}

void test_excess_finite_npc_rotation_does_not_truncate_unrelated_proxy_motion()
{
    constexpr double dt_seconds = 1.0 / 60.0;
    constexpr double pedestrian_speed_mps = 1.35;
    constexpr double maximum_finite_heading_rate_rad_s =
        4.0 * std::numbers::pi_v<double>;

    auto discontinuous_npc = make_npc_proxy(
        "a-discontinuous-npc", {100.0, 100.0}, {}, 0.0, 164.0);
    discontinuous_npc.mass_kg = 1500.0;
    discontinuous_npc.yaw_inertia_kg_m2 = 2600.0;
    discontinuous_npc.maximum_linear_speed_mps = 30.0;

    auto walking_pedestrian = make_pedestrian_proxy(
        "b-walking-pedestrian", {50.0, 50.0},
        {pedestrian_speed_mps, 0.0});
    walking_pedestrian.mass_kg = 80.0;
    walking_pedestrian.maximum_linear_speed_mps = 15.0;

    const auto result = simcore_host::CollisionWorld{}.integrate(
        make_body({0.0, 0.0}, {}), dt_seconds,
        {discontinuous_npc, walking_pedestrian});

    require(!result.motion_clamped,
            "one finite NPC heading discontinuity must not truncate the shared tick");
    require(result.resolved_dynamic_proxies.size() == 2,
            "both unrelated finite proxies must retain resolved states");
    const auto& resolved_npc = result.resolved_dynamic_proxies[0];
    const auto& resolved_pedestrian = result.resolved_dynamic_proxies[1];
    require(resolved_npc.proxy_id == "a-discontinuous-npc"
                && near(resolved_npc.heading_rate_rad_s,
                        maximum_finite_heading_rate_rad_s),
            "finite NPC input heading rate must use the existing solver bound");
    const auto pedestrian_center = std::get<simcore_host::VerticalCapsule>(
        resolved_pedestrian.shape).center_enu;
    require(resolved_pedestrian.proxy_id == "b-walking-pedestrian"
                && near(pedestrian_center.east_m,
                        50.0 + pedestrian_speed_mps * dt_seconds)
                && near(pedestrian_center.north_m, 50.0)
                && near(
                    resolved_pedestrian.linear_velocity_enu_mps.east_m,
                    pedestrian_speed_mps),
            "unrelated pedestrian must receive its complete nominal tick without speed pollution");
}

void test_dynamic_proxy_input_order_is_deterministic()
{
    const auto npc = make_npc_proxy("a-npc", {0.0, 0.0});
    const auto pedestrian = make_pedestrian_proxy("b-ped", {-0.8, -0.8});
    const auto body = make_body({-2.0, -2.0}, {20.0, 20.0});

    const auto first = simcore_host::CollisionWorld{}.integrate(
        body, 0.1, {npc, pedestrian});
    const auto second = simcore_host::CollisionWorld{}.integrate(
        body, 0.1, {pedestrian, npc});

    require(first.body.shape.center_enu.east_m
                == second.body.shape.center_enu.east_m
            && first.body.shape.center_enu.north_m
                == second.body.shape.center_enu.north_m
            && first.body.linear_velocity_enu_mps.east_m
                == second.body.linear_velocity_enu_mps.east_m
            && first.body.linear_velocity_enu_mps.north_m
                == second.body.linear_velocity_enu_mps.north_m
            && first.contacts.size() == second.contacts.size(),
            "dynamic proxy input order must not affect the solved state");
    require(first.resolved_dynamic_proxies.size() == 2
                && second.resolved_dynamic_proxies.size() == 2
                && first.resolved_dynamic_proxies[0].proxy_id == "a-npc"
                && first.resolved_dynamic_proxies[1].proxy_id == "b-ped"
                && second.resolved_dynamic_proxies[0].proxy_id == "a-npc"
                && second.resolved_dynamic_proxies[1].proxy_id == "b-ped",
            "resolved proxy states must use stable proxy-ID order");
    for (std::size_t index = 0;
         index < first.resolved_dynamic_proxies.size(); ++index) {
        const auto first_center = std::visit(
            [](const auto& shape) { return shape.center_enu; },
            first.resolved_dynamic_proxies[index].shape);
        const auto second_center = std::visit(
            [](const auto& shape) { return shape.center_enu; },
            second.resolved_dynamic_proxies[index].shape);
        require(first_center.east_m == second_center.east_m
                    && first_center.north_m == second_center.north_m,
                "resolved proxy motion must be independent of caller input order");
    }
    for (std::size_t index = 0; index < first.contacts.size(); ++index) {
        require(first.contacts[index].collider_id
                    == second.contacts[index].collider_id,
                "dynamic contacts must remain sorted by proxy ID");
    }
}

void test_uniform_grid_matches_brute_force_and_reduces_candidates()
{
    std::vector<simcore_host::StaticObbCollider> static_colliders;
    std::vector<simcore_host::KinematicCollisionProxy> proxies;
    for (int index = 0; index < 120; ++index) {
        static_colliders.push_back(make_collider(
            "far-static-" + std::to_string(index),
            {1000.0 + index * 20.0, 1000.0},
            0.0, 2.0, 0.2));
        proxies.push_back(make_pedestrian_proxy(
            "far-proxy-" + std::to_string(index),
            {-1000.0 - index * 20.0, -1000.0}));
    }
    static_colliders.push_back(make_collider(
        "near-static-wall", {200.0, 0.0}, 0.0, 10.0, 0.05));
    proxies.push_back(make_npc_proxy("near-npc", {0.0, 0.0}));

    const simcore_host::CollisionWorld grid_world(
        static_colliders,
        simcore_host::CollisionBroadPhaseMode::UniformGrid);
    const simcore_host::CollisionWorld reference_world(
        static_colliders,
        simcore_host::CollisionBroadPhaseMode::BruteForceReference);
    const auto dynamic_body = make_body({0.0, -4.0}, {0.0, 30.0});
    const auto grid = grid_world.integrate(dynamic_body, 0.1, proxies);
    const auto brute = reference_world.integrate(dynamic_body, 0.1, proxies);

    require_same_physics_result(
        grid, brute,
        "uniform-grid and brute-force broad phases must solve identically");
    require(!grid.contacts.empty()
            && grid.contacts[0].collider_id == "near-npc",
            "equivalence scenario must exercise a real dynamic contact");
    require(grid.narrow_phase_candidate_count
                < brute.narrow_phase_candidate_count / 10,
            "uniform grid must materially reduce narrow-phase candidates");
    require(grid.broad_phase_query_count == brute.broad_phase_query_count,
            "both modes must execute the same deterministic solver queries");

    const auto static_body = make_body({196.5, 0.0}, {30.0, 0.0});
    const auto grid_static = grid_world.integrate(static_body, 0.1, proxies);
    const auto brute_static = reference_world.integrate(
        static_body, 0.1, proxies);
    require_same_physics_result(
        grid_static, brute_static,
        "static grid candidates must solve identically to brute force");
    require(!grid_static.contacts.empty()
            && grid_static.contacts[0].collider_id == "near-static-wall",
            "equivalence scenario must exercise a real static contact");
    require(grid_static.narrow_phase_candidate_count
                < brute_static.narrow_phase_candidate_count / 10,
            "uniform grid must reduce candidates around a static contact");
}

void test_large_static_and_dynamic_colliders_use_safe_fallback()
{
    auto huge_static = make_collider(
        "huge-static", {0.0, 0.0}, std::numbers::pi_v<double> * 0.5,
        50000.0, 0.05);
    const auto static_result = simcore_host::CollisionWorld(
        {huge_static}).integrate(
            make_body({0.0, -3.0}, {0.0, 50.0}), 0.1);
    require(!static_result.contacts.empty()
            && static_result.contacts[0].collider_id == "huge-static",
            "a static collider spanning more than the cell budget must not be lost");

    auto huge_dynamic = make_npc_proxy("huge-dynamic", {0.0, 0.0});
    auto& dynamic_shape = std::get<simcore_host::ObbPrism>(
        huge_dynamic.shape);
    dynamic_shape.heading_rad = std::numbers::pi_v<double> * 0.5;
    dynamic_shape.half_length_m = 50000.0;
    dynamic_shape.half_width_m = 0.05;
    const auto dynamic_result = simcore_host::CollisionWorld{}.integrate(
        make_body({0.0, -3.0}, {0.0, 50.0}),
        0.1,
        {huge_dynamic});
    require(!dynamic_result.contacts.empty()
            && dynamic_result.contacts[0].collider_id == "huge-dynamic",
            "a dynamic collider spanning more than the cell budget must not be lost");
}

void test_grid_boundary_contact_and_duplicate_candidate_removal()
{
    const auto boundary_wall = make_collider(
        "boundary-wall", {8.0, 0.0}, 0.0, 10.0, 0.05);
    const auto boundary_result = simcore_host::CollisionWorld(
        {boundary_wall}).integrate(
            make_body({6.0, 0.0}, {20.0, 0.0}), 0.1);
    require(!boundary_result.contacts.empty()
            && boundary_result.body.shape.center_enu.east_m
                <= 7.45 + 1e-6,
            "grid-cell boundary must not hide an actual wall contact");

    const auto multi_cell_overhead = make_collider(
        "multi-cell-overhead", {8.0, 0.0}, 0.0,
        20.0, 2.0, 5.0, 1.0);
    const auto duplicate_result = simcore_host::CollisionWorld(
        {multi_cell_overhead}).integrate(
            make_body({8.0, 0.0}, {0.0, 0.0}), 1.0 / 60.0);
    require(duplicate_result.contacts.empty()
            && duplicate_result.broad_phase_query_count == 1
            && duplicate_result.narrow_phase_candidate_count == 1,
            "a collider shared by many queried cells must be tested only once");
}

void test_dynamic_proxy_validation_fails_closed()
{
    const auto body = make_body({0.0, 0.0}, {0.0, 0.0});
    const auto require_rejected = [&](std::vector<simcore_host::KinematicCollisionProxy> proxies,
                                      const char* message) {
        bool rejected = false;
        try {
            (void)simcore_host::CollisionWorld{}.integrate(
                body, 1.0 / 60.0, std::move(proxies));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, message);
    };

    auto empty_id = make_npc_proxy("", {0.0, 0.0});
    require_rejected({empty_id}, "empty proxy ID must be rejected");

    const auto duplicate = make_npc_proxy("duplicate", {0.0, 0.0});
    require_rejected(
        {duplicate, duplicate},
        "duplicate proxy IDs must be rejected");

    auto non_finite = make_npc_proxy("bad-velocity", {0.0, 0.0});
    non_finite.linear_velocity_enu_mps.east_m =
        std::numeric_limits<double>::quiet_NaN();
    require_rejected(
        {non_finite},
        "non-finite proxy velocity must be rejected");

    auto invalid_capsule = make_pedestrian_proxy("bad-capsule", {0.0, 0.0});
    std::get<simcore_host::VerticalCapsule>(invalid_capsule.shape).radius_m = 0.0;
    require_rejected(
        {invalid_capsule},
        "non-positive capsule radius must be rejected");

    auto finite_without_bound = make_npc_proxy("finite-no-bound", {0.0, 0.0});
    finite_without_bound.mass_kg = 1500.0;
    finite_without_bound.yaw_inertia_kg_m2 = 2500.0;
    require_rejected(
        {finite_without_bound},
        "finite proxy without a positive speed bound must be rejected");

    auto finite_obb_without_inertia = make_npc_proxy(
        "finite-no-inertia", {0.0, 0.0});
    finite_obb_without_inertia.mass_kg = 1500.0;
    finite_obb_without_inertia.maximum_linear_speed_mps = 30.0;
    require_rejected(
        {finite_obb_without_inertia},
        "finite OBB proxy without yaw inertia must be rejected");
}

void test_tire_supported_bypass_is_restricted_to_curb_semantics()
{
    auto curb = make_collider("curb", {0.0, 2.0}, 0.0, 0.1, 2.0);
    curb.semantic = simcore_host::StaticColliderSemantic::Curb;
    auto wall = make_collider("wall", {0.0, 3.0}, 0.0, 0.1, 2.0);
    auto barrier = make_collider("barrier", {0.0, 3.0}, 0.0, 0.1, 2.0);
    barrier.semantic = simcore_host::StaticColliderSemantic::Barrier;
    const simcore_host::CollisionWorld curb_only_world({curb});
    const auto body = make_body({0.0, 0.0}, {0.0, 20.0});
    const auto ordinary = curb_only_world.integrate(body, 0.1);
    require(ordinary.contacts.size() == 1
                && ordinary.contacts[0].collider_id == "curb",
            "ordinary integration must physically block a Curb");
    const auto empty_bypass = curb_only_world.integrate_with_tire_supported_curbs(
        body, 0.1, {}, {});
    require(empty_bypass.contacts.size() == 1
                && empty_bypass.contacts[0].collider_id == "curb",
            "an empty tire-supported set must still block a Curb");
    const auto bypassed = curb_only_world.integrate_with_tire_supported_curbs(
        body, 0.1, {}, {"curb"});
    require(bypassed.contacts.empty(),
            "an explicitly tire-supported curb may be omitted for one step");

    const simcore_host::CollisionWorld wall_world({curb, wall});
    const auto wall_preserved = wall_world.integrate_with_tire_supported_curbs(
        body, 0.1, {}, {"curb"});
    require(std::any_of(
                wall_preserved.contacts.begin(), wall_preserved.contacts.end(),
                [](const auto& contact) { return contact.collider_id == "wall"; }),
            "bypassing a Curb must preserve a same-tick Wall contact");
    const simcore_host::CollisionWorld barrier_world({curb, barrier});
    const auto barrier_preserved =
        barrier_world.integrate_with_tire_supported_curbs(
            body, 0.1, {}, {"curb"});
    require(std::any_of(
                barrier_preserved.contacts.begin(),
                barrier_preserved.contacts.end(),
                [](const auto& contact) {
                    return contact.collider_id == "barrier";
                }),
            "bypassing a Curb must preserve a same-tick Barrier contact");

    const simcore_host::CollisionWorld validation_world({curb, wall, barrier});
    const auto require_id_rejected = [&](const std::string& id,
                                         const char* message) {
        bool rejected = false;
        try {
            (void)validation_world.integrate_with_tire_supported_curbs(
                body, 0.1, {}, {id});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, message);
    };
    require_id_rejected(
        "wall", "the tire-supported API must reject Wall IDs");
    require_id_rejected(
        "barrier", "the tire-supported API must reject Barrier IDs");
    require_id_rejected(
        "unknown", "the tire-supported API must reject unknown IDs");
}

void test_fast_npc_crossing_pedestrian_uses_shared_microsteps()
{
    using namespace simcore_host;
    auto ego = make_body({100,100}, {});
    const std::vector<KinematicCollisionProxy> proxies{
        {"npc-fast", ObbPrism{{0,0},.85,std::numbers::pi/2,2.2,1,.75}, {60,0},0,{.8,0},1500,2600,60},
        {"ped-fast-crossing", VerticalCapsule{{3.2,0},.9,.35,.9}, {},0,{.7,0},80,0,15}};
    // Without intermediate pair solves, the car moves from E=0 to E=6 and
    // completely crosses the person: neither endpoint's shapes overlap.
    const auto result = CollisionWorld{}.integrate(ego, .1, proxies);
    require(result.substep_count >= 60 && !result.motion_clamped && result.contacts.empty()
        && !result.runtime_proxy_contacts.empty(),
        "fast NPC/ped crossing must be caught inside microsteps and not reported as Ego damage");
    require(result.resolved_dynamic_proxies[1].linear_velocity_enu_mps.east_m > 1.0
        && std::get<VerticalCapsule>(result.resolved_dynamic_proxies[1].shape).center_enu.east_m > 3.2,
        "a high-speed NPC must transfer finite momentum instead of passing through a pedestrian");
    auto clear = proxies;
    std::get<VerticalCapsule>(clear[1].shape).center_enu.north_m = 20;
    const auto free = CollisionWorld{}.integrate(ego, .1, clear);
    require(free.runtime_proxy_contacts.empty()
        && near(std::get<ObbPrism>(free.resolved_dynamic_proxies[0].shape).center_enu.east_m, 6, 1e-6),
        "pair checking must not advance unaffected NPC motion a second time");
}

void test_runtime_vehicle_pedestrian_pairs_are_finite_and_height_aware()
{
    using namespace simcore_host;
    const std::vector<KinematicCollisionProxy> initial{
        {"npc", ObbPrism{{0,0},.85,std::numbers::pi/2,2.2,1,.75}, {6,0},0,{.8,0},1500,2600,30},
        {"ped", VerticalCapsule{{2.45,0},.9,.35,.9}, {},0,{.7,0},80,0,15}};
    auto proxies = initial;
    const auto contacts = resolve_runtime_proxy_pairs(proxies);
    require(contacts.size()==2 && contacts[0].proxy_id == "npc" && contacts[1].proxy_id == "ped"
        && contacts[1].contact.accumulated_normal_impulse_n_s > 0
        && contacts[1].contact.normal_enu.east_m > .9,
        "NPC/ped collision must emit separate equal/opposite receiver contacts, not Ego contacts");
    require(proxies[0].linear_velocity_enu_mps.east_m < 6
        && proxies[1].linear_velocity_enu_mps.east_m > 1
        && near(1500 * proxies[0].linear_velocity_enu_mps.east_m
              + 80 * proxies[1].linear_velocity_enu_mps.east_m, 9000, 1e-5),
        "runtime pair collision must transfer momentum without treating the pedestrian as kinematic");
    auto reverse = initial;
    std::reverse(reverse.begin(), reverse.end());
    (void)resolve_runtime_proxy_pairs(reverse);
    require(near(reverse[1].linear_velocity_enu_mps.east_m, proxies[1].linear_velocity_enu_mps.east_m),
        "runtime pair solve must remain deterministic under input ordering");
    proxies = initial;
    proxies[1].shape = ObbPrism{{2.45,0}, .25, std::numbers::pi/2, .95,.35,.25};
    proxies[1].yaw_inertia_kg_m2 = 30;
    std::get<ObbPrism>(proxies[0].shape).center_up_m = 1.6; // min Z=.85, clear of downed max Z=.5
    require(resolve_runtime_proxy_pairs(proxies).empty(),
        "a low downed collider cannot leave a blocking upright-capsule ghost above it");
    std::get<ObbPrism>(proxies[0].shape).center_up_m = .85;
    require(!resolve_runtime_proxy_pairs(proxies).empty(),
        "a ground-level NPC must still physically contact a downed body");
}

} // namespace

int main()
{
    try {
        test_rotated_obb_sat_and_vertical_interval();
        test_high_speed_head_on_motion_does_not_tunnel();
        test_glancing_contact_slides_without_normal_penetration();
        test_initial_deep_penetration_is_projected_out();
        test_collider_input_order_is_deterministic();
        test_empty_or_vertically_clear_world_preserves_motion();
        test_excess_motion_fails_closed_at_substep_limit();
        test_obb_capsule_narrow_phase_and_vertical_separation();
        test_stationary_npc_obb_prevents_ego_penetration();
        test_stationary_pedestrian_capsule_prevents_ego_penetration();
        test_approaching_kinematic_proxy_pushes_ego_with_relative_velocity();
        test_finite_npc_receives_equal_opposite_normal_and_tangent_impulse();
        test_finite_pedestrian_is_pushed_with_bounded_speed();
        test_rotating_npc_proxy_is_advanced_during_microsteps();
        test_excess_finite_npc_rotation_does_not_truncate_unrelated_proxy_motion();
        test_dynamic_proxy_input_order_is_deterministic();
        test_uniform_grid_matches_brute_force_and_reduces_candidates();
        test_large_static_and_dynamic_colliders_use_safe_fallback();
        test_grid_boundary_contact_and_duplicate_candidate_removal();
        test_dynamic_proxy_validation_fails_closed();
        test_tire_supported_bypass_is_restricted_to_curb_semantics();
        test_runtime_vehicle_pedestrian_pairs_are_finite_and_height_aware();
        test_fast_npc_crossing_pedestrian_uses_shared_microsteps();
        std::cout << "collision_world_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "collision_world_tests: " << error.what() << '\n';
        return 1;
    }
}
