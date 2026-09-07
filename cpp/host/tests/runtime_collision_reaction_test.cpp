#include "runtime_collision_reaction.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace simcore_host;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

RuntimeCollisionReaction recovering_reaction()
{
    RuntimeCollisionReaction reaction;
    KinematicCollisionProxy actual;
    actual.proxy_id = "npc-1001";
    reaction.recovery.record_contact(2250.0, 1500.0);
    reaction.update(actual, 1.0 / 60.0, true);
    for (int tick = 0; tick < 600
            && reaction.recovery.phase() != ImpactRecoveryPhase::Recovering; ++tick) {
        reaction.update(actual, 1.0 / 60.0, true);
    }
    require(reaction.recovery.phase() == ImpactRecoveryPhase::Recovering,
        "a settled recoverable impact must eventually authorize return motion");
    return reaction;
}

void impact_preserves_momentum_and_resets_the_route_return()
{
    RuntimeCollisionReaction reaction;
    reaction.return_speed_mps = 0.4;
    reaction.return_yaw_speed_rad_s = 0.1;
    reaction.return_departure_grace_remaining_s = 1.0;
    reaction.vehicle_return_path_initialized = true;
    reaction.vehicle_return_direction = -1;
    reaction.vehicle_return_progress = 0.5;
    reaction.vehicle_return_p0 = {3.0, 4.0};
    KinematicCollisionProxy actual;
    actual.proxy_id = "npc-1001";
    actual.linear_velocity_enu_mps = {2.0, 3.0};
    actual.heading_rate_rad_s = 0.4;
    reaction.recovery.record_contact(2250.0, 1500.0);

    reaction.update(actual, 1.0 / 60.0, true);
    require(reaction.recovery.phase() == ImpactRecoveryPhase::Settling,
        "a moving impacted body must settle before holding");
    require(reaction.velocity_enu_mps.east_m == 2.0
            && reaction.velocity_enu_mps.north_m == 3.0
            && reaction.heading_rate_rad_s == 0.4,
        "stopping the route motor must retain the solved physical momentum");
    require(reaction.return_speed_mps == 0.0 && reaction.return_yaw_speed_rad_s == 0.0
            && reaction.return_departure_grace_remaining_s == 0.0
            && !reaction.vehicle_return_path_initialized
            && reaction.vehicle_return_direction == 0
            && reaction.vehicle_return_progress == 0.0
            && reaction.vehicle_return_p0.east_m == 0.0,
        "a new impact must discard the stale return plan and boarding delay");

    reaction.integrate_offset(1.0 / 60.0);
    require(reaction.offset_enu_m.east_m > 0.0 && reaction.offset_enu_m.north_m > 0.0
            && reaction.velocity_enu_mps.east_m > 0.0
            && reaction.velocity_enu_mps.east_m < 2.0,
        "settling must advance and damp physical momentum");
}

void boarding_pause_and_exact_return_completion()
{
    auto reaction = recovering_reaction();
    require(reaction.return_departure_grace_remaining_s == 2.0,
        "entering recovery must preserve the driver's two-second boarding interval");
    reaction.offset_enu_m = {0.0, 2.0};
    reaction.heading_offset_rad = 0.1;
    reaction.velocity_enu_mps = {0.3, 0.2};
    reaction.heading_rate_rad_s = 0.1;
    reaction.integrate_offset(0.1,
        RuntimeCollisionReaction::kDefaultVelocityDampingPerSecond, true, 0.0);
    require(reaction.offset_enu_m.north_m == 2.0 && reaction.heading_offset_rad == 0.1
            && reaction.velocity_enu_mps.east_m == 0.0
            && reaction.velocity_enu_mps.north_m == 0.0
            && reaction.heading_rate_rad_s == 0.0
            && std::abs(reaction.return_departure_grace_remaining_s - 1.9) < 1e-12,
        "boarding must keep the displaced pose fixed and clear residual velocity");

    reaction.finish_recovery();
    require(reaction.recovery.phase() == ImpactRecoveryPhase::Recovering,
        "elapsed policy time alone cannot finish a displaced return");
    reaction.offset_enu_m = {};
    reaction.heading_offset_rad = 0.0;
    reaction.velocity_enu_mps.east_m = 1e-12;
    reaction.finish_recovery();
    require(reaction.recovery.phase() == ImpactRecoveryPhase::Recovering,
        "return completion requires exactly settled physical velocity");
    reaction.velocity_enu_mps = {};
    reaction.finish_recovery();
    require(reaction.recovery.allows_driving()
            && reaction.return_departure_grace_remaining_s == 0.0,
        "a completed return releases route driving and clears its boarding delay");
}

void damage_publication_uses_the_strongest_owned_contact_once()
{
    RuntimeEntityState entity;
    entity.kind = RuntimeEntityKind::NpcVehicle;
    entity.collision_proxy.proxy_id = "npc-1001";
    entity.collision_proxy.shape = ObbPrism{{0.0, 0.0}, 0.75, 0.0, 2.2, 1.0, 0.75};
    const std::vector<CollisionContact> contacts{
        {"other", {0.0, -1.0}, {0.0, 2.2}, 0.0, 10000.0},
        {"npc-1001", {0.0, -1.0}, {0.0, 2.2}, 0.0, 2000.0},
        {"npc-1001", {-1.0, 0.0}, {1.0, 0.0}, 0.0, 4000.0},
        {"npc-1001", {0.0, 1.0}, {0.0, -2.2}, 0.0, 4000.0},
    };
    RuntimeCollisionReaction reaction;
    reaction.recovery.record_contact(4500.0, 1500.0);
    reaction.update(entity.collision_proxy, 1.0 / 60.0, true);
    reaction.publish(entity, contacts);
    require(entity.damage_zone == VehicleDamageZone::Right
            && entity.impact_direction_enu.east_m == 1.0
            && entity.impact_direction_enu.north_m == 0.0
            && entity.last_impact_impulse_n_s == 4500.0f
            && entity.collision_event_sequence != 0
            && entity.dent_patches.size() == 1,
        "publication must use the first strongest owned contact and episode impulse");

    const auto depth = entity.dent_patches.front().depth_m;
    reaction.publish(entity,
        {{"npc-1001", {1.0, 0.0}, {-1.0, 0.0}, 0.0, 9000.0}});
    require(entity.damage_zone == VehicleDamageZone::Right
            && entity.dent_patches.size() == 1 && entity.dent_patches.front().depth_m == depth,
        "repeated publication of one unchanged event must not move or deepen a dent");

    RuntimeCollisionReaction pedestrian;
    entity.kind = RuntimeEntityKind::Pedestrian;
    pedestrian.recovery.record_contact(4500.0, 80.0);
    pedestrian.update(entity.collision_proxy, 1.0 / 60.0, true);
    pedestrian.publish(entity, contacts);
    require(entity.dent_patches.empty(),
        "a downed pedestrian's box shape must never receive vehicle dent patches");
}

} // namespace

int main()
{
    try {
        impact_preserves_momentum_and_resets_the_route_return();
        boarding_pause_and_exact_return_completion();
        damage_publication_uses_the_strongest_owned_contact_once();
        std::cout << "runtime_collision_reaction_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "runtime_collision_reaction_tests: " << error.what() << '\n';
        return 1;
    }
}
