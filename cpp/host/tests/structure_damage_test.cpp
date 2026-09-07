#include "collision/structure_damage.hpp"
#include "collision/collision_world.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
using namespace simcore_host;

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

bool near(double a, double b, double tolerance = 1.0e-7)
{
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance;
}

TrafficNetwork traffic_fixture()
{
    TrafficNetwork network;
    network.signals = {
        {1, 1, {10.0, 20.0, 2.0}, 90.0, 1, TrafficSignalKind::Vehicle},
        {2, 2, {20.0, 20.0, 2.0}, 0.0, 1, TrafficSignalKind::Pedestrian},
        {3, 3, {30.0, 20.0, 2.0}, 0.0, 2, TrafficSignalKind::Vehicle},
    };
    return network;
}

StaticObbCollider building_fixture()
{
    return {"Building_01", StaticColliderSemantic::Wall,
        {{-10.0, 5.0}, 7.0, 0.0, 5.0, 8.0, 5.0}, {0.8, 0.0}};
}

StructureDamageRuntime runtime_fixture()
{
    StructureDamageRuntime result;
    const auto traffic = traffic_fixture();
    result.rebuild({building_fixture()}, &traffic);
    return result;
}

CollisionContact contact(std::string id, double impulse, CollisionVector2 normal = {-1.0, 0.0})
{
    return {std::move(id), normal, {9.75, 20.0}, 0.01, impulse};
}

void hit(StructureDamageRuntime& runtime, const std::string& id, double impulse,
    double height = 2.8, CollisionVector2 normal = {-1.0, 0.0})
{
    runtime.record_contacts({contact(id, impulse, normal)}, height);
    runtime.tick(1.0 / 60.0, true);
}

void advance(StructureDamageRuntime& runtime, double seconds, int hz = 60, bool enabled = true)
{
    for (int index = 0; index < static_cast<int>(std::round(seconds * hz)); ++index) {
        runtime.tick(1.0 / hz, enabled);
    }
}

const StructureDamageSnapshot& snapshot(const StructureDamageRuntime& runtime, const std::string& id)
{
    for (const auto& entry : runtime.snapshots()) {
        if (entry.collider_id == id) { return entry; }
    }
    throw std::runtime_error("expected damaged structure not found: " + id);
}

const ObbPrism& pole_shape(const StructureDamageRuntime& runtime, const std::string& id)
{
    for (const auto& proxy : runtime.collision_proxies()) {
        if (proxy.proxy_id == id) { return std::get<ObbPrism>(proxy.shape); }
    }
    throw std::runtime_error("expected pole proxy not found");
}

void intact_targets_are_bounded_sorted_and_do_not_replace_buildings()
{
    const auto runtime = runtime_fixture();
    require(runtime.snapshots().empty(), "intact structures need no damage wire entries");
    require(runtime.collision_proxies().size() == 3, "only signal poles own new collision proxies");
    require(runtime.collision_proxies()[0].proxy_id == "signal-pole-1", "proxy ordering must be deterministic");
    for (const auto& proxy : runtime.collision_proxies()) {
        require(proxy.mass_kg == 180.0 && proxy.yaw_inertia_kg_m2 > 0.0
            && near(proxy.breakaway_impulse_n_s, 2500.0) && !proxy.breakaway_released,
            "intact poles require finite mass behind a bounded anchor resistance");
    }
    const auto& shape = pole_shape(runtime, "signal-pole-1");
    require(near(shape.center_up_m, 3.925) && near(shape.half_height_m, 1.925)
        && near(shape.half_length_m, 0.25) && near(shape.half_width_m, 0.25),
        "upright collision prism must match the pole base and full visual height");
}

void low_and_unknown_contacts_do_not_damage()
{
    auto runtime = runtime_fixture();
    hit(runtime, "Building_01", 899.0);
    hit(runtime, "signal-pole-1", 249.0);
    hit(runtime, "not-a-target", 100000.0);
    require(runtime.snapshots().empty(), "low impulse and unrelated colliders must remain unchanged");
}

void building_partial_damage_preserves_shell()
{
    auto runtime = runtime_fixture();
    hit(runtime, "Building_01", 6450.0, 999.0, {-3.0, 0.0});
    const auto& damage = snapshot(runtime, "Building_01");
    require(near(damage.damage_percent, 50.0) && damage.kind == StructureKind::Building,
        "building damage must use the bounded demo impulse profile");
    require(!damage.disabled && damage.fall_angle_rad == 0.0 && damage.signal_id == 0,
        "facade damage cannot collapse the building or affect signals");
    require(near(damage.impact_point_enu.up_m, 12.0) && near(damage.impact_normal_enu.east_m, -1.0),
        "impact height must be clamped to the facade and normal normalized");
    require(runtime.collision_proxies().size() == 3, "building damage cannot duplicate/remove authored shell collision");
    require(damage.event_sequence == 1, "one collision episode produces one visual event");
}

void penetrating_building_contact_is_projected_to_exterior()
{
    auto wall = building_fixture();
    wall.shape = {{40.0, -12.0}, 3.0, 0.0, 4.0, 7.0, 3.0};
    StructureDamageRuntime runtime;
    runtime.rebuild({wall}, nullptr);
    auto impact = contact(wall.collider_id, 6450.0, {1.0, 0.0});
    impact.contact_point_enu = {46.4, -10.7}; // 60cm inside the east facade.
    runtime.record_contacts({impact}, -1.0);
    runtime.tick(1.0 / 60.0, true);
    const auto& damage = snapshot(runtime, wall.collider_id);
    require(near(damage.impact_point_enu.east_m, 47.0)
        && near(damage.impact_point_enu.north_m, -10.7)
        && near(damage.impact_point_enu.up_m, 0.0),
        "penetrating support points must reach the authored exterior, not leave marks buried inside");
    require(near(damage.impact_normal_enu.east_m, 1.0)
        && near(damage.impact_normal_enu.north_m, 0.0),
        "projected marks require the facade's outward normal");
}

void rotated_building_projection_selects_facade_not_body_sat_axis()
{
    for (const double angle : {0.0, std::numbers::pi / 6.0,
        std::numbers::pi / 2.0, -0.9 * std::numbers::pi}) {
        auto wall = building_fixture();
        wall.shape = {{40.0, -12.0}, 3.0, angle, 4.0, 7.0, 3.0};
        const CollisionVector2 forward{std::sin(angle), std::cos(angle)};
        const CollisionVector2 right{std::cos(angle), -std::sin(angle)};
        auto world = [&](double f, double r) {
            return CollisionVector2{40.0 + f * forward.east_m + r * right.east_m,
                -12.0 + f * forward.north_m + r * right.north_m};
        };
        StructureDamageRuntime runtime;
        runtime.rebuild({wall}, nullptr);
        auto impact = contact(wall.collider_id, 6450.0,
            {0.8 * forward.east_m + 0.6 * right.east_m,
                0.8 * forward.north_m + 0.6 * right.north_m});
        impact.contact_point_enu = world(3.88, 6.7);
        runtime.record_contacts({impact}, 3.0);
        runtime.tick(1.0 / 60.0, true);
        const auto& first = snapshot(runtime, wall.collider_id);
        const auto expected_front = world(4.0, 6.79);
        require(near(first.impact_point_enu.east_m, expected_front.east_m)
            && near(first.impact_point_enu.north_m, expected_front.north_m)
            && near(first.impact_normal_enu.east_m, forward.east_m)
            && near(first.impact_normal_enu.north_m, forward.north_m),
            "a rotated corner contact must reach the first facade and replace the diagonal SAT normal");

        runtime.reset(); // Authored geometry must survive new Play reset.
        impact.normal_enu = {0.6 * forward.east_m - 0.8 * right.east_m,
            0.6 * forward.north_m - 0.8 * right.north_m};
        impact.contact_point_enu = world(3.7, -6.91);
        runtime.record_contacts({impact}, 3.0);
        runtime.tick(1.0 / 60.0, true);
        const auto& second = snapshot(runtime, wall.collider_id);
        const auto expected_left = world(3.7675, -7.0);
        require(near(second.impact_point_enu.east_m, expected_left.east_m)
            && near(second.impact_point_enu.north_m, expected_left.north_m)
            && near(second.impact_normal_enu.east_m, -right.east_m)
            && near(second.impact_normal_enu.north_m, -right.north_m),
            "opposite rotated facade projection must remain correct after reset");
    }
}

void vehicle_contact_footprint_distinguishes_front_corner_side_and_wall_edge()
{
    auto wall = building_fixture();
    wall.shape = {{40.0, -12.0}, 3.0, 0.0, 4.0, 7.0, 3.0};
    auto damage_for = [&](ObbPrism body, double impulse = 6450.0) {
        StructureDamageRuntime runtime;
        runtime.rebuild({wall}, nullptr);
        auto impact = contact(wall.collider_id, impulse, {1.0, 0.0});
        impact.contact_point_enu = {46.95, body.center_enu.north_m};
        runtime.record_contacts({impact}, body.center_up_m, &body);
        runtime.tick(1.0 / 60.0, true);
        return snapshot(runtime, wall.collider_id);
    };
    const ObbPrism front{{49.2, -12.0}, 0.8, -std::numbers::pi / 2.0, 2.2, 1.0, 0.6};
    const auto frontal = damage_for(front);
    require(near(frontal.impact_half_width_m, 1.0) && near(frontal.impact_half_height_m, 0.6)
        && near(frontal.impact_point_enu.east_m, 47.0) && near(frontal.impact_point_enu.north_m, -12.0)
        && near(frontal.impact_severity, 0.5),
        "a square bumper impact must publish the actual two-metre contact band, not a fixed stamp");
    auto corner = front;
    corner.heading_rad = -std::numbers::pi / 4.0;
    corner.center_enu.east_m = 47.0 + 3.2 / std::sqrt(2.0);
    const auto glancing = damage_for(corner);
    require(glancing.impact_half_width_m < frontal.impact_half_width_m * 0.2
        && glancing.impact_point_enu.north_m > -11.5,
        "an angled corner strike must leave a narrow patch at the contacting corner");
    auto side = front;
    side.heading_rad = 0.0;
    side.center_enu.east_m = 48.0;
    const auto broadside = damage_for(side);
    require(near(broadside.impact_half_width_m, 2.2),
        "a side-on strike must use vehicle length as the affected facade width");
    auto edge = front;
    edge.center_enu.north_m = -8.2;
    const auto clipped = damage_for(edge);
    require(near(clipped.impact_half_width_m, 0.6) && near(clipped.impact_point_enu.north_m, -8.6),
        "contact patches must be clipped to the actual facade edge");
    const auto low = damage_for(front, 2010.0);
    require(near(low.impact_half_width_m, frontal.impact_half_width_m)
        && near(low.impact_severity, 0.1),
        "contact size and individual-impact strength must remain independent of accumulated building damage");
}

void saturated_building_still_reports_new_local_impacts()
{
    auto runtime = runtime_fixture();
    hit(runtime, "Building_01", 14000.0, 2.8);
    require(near(snapshot(runtime, "Building_01").damage_percent, 100.0), "first crash reaches damage cap");
    advance(runtime, 0.5);
    hit(runtime, "Building_01", 2010.0, 6.0, {0.0, 1.0});
    const auto& damage = snapshot(runtime, "Building_01");
    require(damage.event_sequence == 2 && near(damage.damage_percent, 100.0)
        && near(damage.impact_point_enu.up_m, 6.0) && near(damage.impact_severity, 0.1),
        "hitting another facade after the cumulative damage cap must still emit a local chip event");
}

void sustained_pressure_is_rate_independent_and_not_endless_damage()
{
    for (const int hz : {30, 60, 120, 1000}) {
        auto runtime = runtime_fixture();
        for (int index = 0; index < hz * 5; ++index) {
            runtime.record_contacts({contact("signal-pole-1", 4000.0 / hz)}, 2.8);
            runtime.tick(1.0 / hz, true);
        }
        const auto& damage = snapshot(runtime, "signal-pole-1");
        require(near(damage.damage_percent, 100.0 / 3.0, 1.0e-6) && !damage.disabled,
            "continuous pressure must charge only its 250ms peak at any physics rate");
        require(damage.event_sequence == 1, "solver impulses must not create a particle event every tick");
    }
}

void split_impulse_can_collapse_at_all_rates()
{
    for (const int hz : {30, 60, 120, 1000}) {
        auto runtime = runtime_fixture();
        const int ticks = hz / 5;
        for (int index = 0; index < ticks; ++index) {
            runtime.record_contacts({contact("signal-pole-1", 2600.0 / ticks)}, 2.8);
            runtime.tick(1.0 / hz, true);
        }
        const auto& damage = snapshot(runtime, "signal-pole-1");
        require(damage.disabled && near(damage.damage_percent, 100.0),
            "a severe impulse cannot disappear by subdivision into high-frequency contacts");
    }
}

void repeated_distinct_impacts_accumulate()
{
    auto runtime = runtime_fixture();
    hit(runtime, "signal-pole-1", 1500.0);
    require(!snapshot(runtime, "signal-pole-1").disabled, "first medium collision should leave the pole standing");
    advance(runtime, 0.5);
    hit(runtime, "signal-pole-1", 1500.0);
    const auto& damage = snapshot(runtime, "signal-pole-1");
    require(damage.disabled && near(damage.damage_percent, 100.0) && damage.event_sequence == 2,
        "distinct impacts must accumulate damage and each emit one event");
}

void collapse_direction_ground_support_and_no_ghost()
{
    auto runtime = runtime_fixture();
    hit(runtime, "signal-pole-1", 3000.0, 2.8, {0.0, 7.0});
    const auto& initial = snapshot(runtime, "signal-pole-1");
    require(initial.disabled && initial.fall_angle_rad > 0.0
        && near(initial.fall_direction_enu.east_m, 0.0)
        && near(initial.fall_direction_enu.north_m, -1.0),
        "the pole must fall away from the impacting vehicle, not toward the surface normal");
    require(near(initial.heading_rad, std::numbers::pi / 2.0),
        "falling must preserve the visual head's authored facing without a sudden yaw snap");
    double previous_angle = initial.fall_angle_rad;
    for (int index = 0; index < 180; ++index) {
        runtime.tick(1.0 / 60.0, true);
        const auto& state = snapshot(runtime, "signal-pole-1");
        const auto& shape = pole_shape(runtime, "signal-pole-1");
        require(state.fall_angle_rad >= previous_angle && state.fall_angle_rad <= std::numbers::pi / 2.0,
            "the gravity hinge must collapse monotonically within its ground stop");
        require(near(shape.center_up_m - shape.half_height_m, 2.0),
            "the conservative tilted prism cannot sink beneath the ground base");
        previous_angle = state.fall_angle_rad;
    }
    const auto& state = snapshot(runtime, "signal-pole-1");
    const auto& shape = pole_shape(runtime, "signal-pole-1");
    require(near(state.fall_angle_rad, std::numbers::pi / 2.0)
        && near(shape.half_height_m, 0.25) && near(shape.center_up_m, 2.25)
        && near(shape.half_length_m, 1.925) && near(shape.center_enu.north_m, 18.075)
        && near(std::abs(shape.heading_rad), std::numbers::pi),
        "fallen collision must be a ground-level horizontal pole, not its old upright prism");
    require(runtime.collision_proxies().size() == 3, "falling must replace the pose, never leave an upright ghost");
    advance(runtime, 10.0);
    require(near(snapshot(runtime, "signal-pole-1").fall_angle_rad, std::numbers::pi / 2.0),
        "destroyed poles cannot automatically repair themselves");
}

std::vector<TrafficSignalSnapshot> green_signals()
{
    std::vector<TrafficSignalSnapshot> states;
    for (const auto& signal : traffic_fixture().signals) {
        states.push_back({signal.id, signal.group_id, SignalAspect::Green,
            signal.position_enu, signal.heading_deg, 12.0, signal.controller_id, signal.kind});
    }
    return states;
}

void outage_is_head_scoped_and_reset_clears_it()
{
    auto runtime = runtime_fixture();
    hit(runtime, "signal-pole-1", 3000.0);
    auto signals = green_signals();
    runtime.apply_signal_faults(signals);
    require(signals[0].aspect == SignalAspect::Red && signals[0].remaining_seconds == 0.0
        && signals[1].aspect == SignalAspect::Green && near(signals[1].remaining_seconds, 12.0),
        "one broken pole must not freeze functioning heads on the same controller");
    require(signals[0].out_of_service && !signals[1].out_of_service && !signals[2].out_of_service,
        "only the physically broken head is out of service");
    require(signals[2].aspect == SignalAspect::Green && near(signals[2].remaining_seconds, 12.0),
        "independent intersections must retain their normal signal plan");
    auto traffic = traffic_fixture();
    traffic.format_version = 2;
    TrafficSignalPlan plan;
    plan.id = 1;
    plan.groups = {1, 2};
    plan.phases = {{1000, {}, {}}, {3000, {1}, {}}, {1000, {}, {1}},
                   {1000, {}, {}}, {3000, {2}, {}}, {1000, {}, {2}}};
    plan.cycle_ms = 10000;
    traffic.signal_plans = {plan};
    for (std::uint64_t seconds = 0; seconds < 300; ++seconds) {
        signals = traffic.signals_at(seconds * 1000000000ULL);
        const auto expected = signals[1];
        runtime.apply_signal_faults(signals);
        require(signals[0].out_of_service && signals[0].remaining_seconds == 0.0
            && signals[1].aspect == expected.aspect
            && signals[1].remaining_seconds == expected.remaining_seconds,
            "five minutes after pole destruction must preserve every healthy phase/countdown");
    }
    runtime.reset();
    signals = green_signals();
    runtime.apply_signal_faults(signals);
    require(runtime.snapshots().empty() && signals[0].aspect == SignalAspect::Green
        && !signals[0].out_of_service && near(pole_shape(runtime, "signal-pole-1").half_height_m, 1.925),
        "new Play reset must clear damage, faults and fallen poses");
}

void pause_discards_pending_and_freezes_existing_pose()
{
    auto runtime = runtime_fixture();
    runtime.record_contacts({contact("signal-pole-1", 3000.0)}, 2.8);
    runtime.tick(1.0, false);
    runtime.tick(1.0 / 60.0, true);
    require(runtime.snapshots().empty(), "paused input cannot cause a deferred crash after resume");
    hit(runtime, "signal-pole-1", 3000.0);
    const double angle = snapshot(runtime, "signal-pole-1").fall_angle_rad;
    advance(runtime, 10.0, 60, false);
    require(near(snapshot(runtime, "signal-pole-1").fall_angle_rad, angle),
        "paused simulation must freeze an in-progress hinge pose");
    advance(runtime, 3.0);
    require(near(snapshot(runtime, "signal-pole-1").fall_angle_rad, std::numbers::pi / 2.0),
        "resume must continue the same fall without healing");
}

void invalid_inputs_do_not_poison_authoritative_snapshots()
{
    auto runtime = runtime_fixture();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double value : {nan, inf, -1.0}) { hit(runtime, "signal-pole-1", value); }
    hit(runtime, "signal-pole-1", 3000.0, nan);
    hit(runtime, "signal-pole-1", 3000.0, 2.8, {0.0, 0.0});
    hit(runtime, "signal-pole-1", 3000.0, 2.8, {nan, 0.0});
    require(runtime.snapshots().empty(), "non-finite or invalid contact data must not become damage");
    for (double value : {nan, inf, -1.0, 10.1}) {
        runtime.record_contacts({contact("signal-pole-1", 3000.0)}, 2.8);
        runtime.tick(value, true);
    }
    runtime.tick(1.0 / 60.0, true);
    require(runtime.snapshots().empty(), "invalid tick duration must discard its pending impulse");
    hit(runtime, "signal-pole-1", std::numeric_limits<double>::max());
    require(snapshot(runtime, "signal-pole-1").disabled,
        "a finite extreme impulse should saturate safely, not overflow the state");
}

void rebuild_validation_is_transactional()
{
    auto runtime = runtime_fixture();
    auto reject = [&](std::vector<StaticObbCollider> walls, TrafficNetwork traffic) {
        bool threw = false;
        try { runtime.rebuild(walls, &traffic); }
        catch (const std::runtime_error&) { threw = true; }
        require(threw && runtime.collision_proxies().size() == 3,
            "invalid rebuild must preserve the complete previous target set");
    };
    auto duplicate = traffic_fixture();
    duplicate.signals.push_back(duplicate.signals.front());
    reject({building_fixture()}, duplicate);
    auto reserved = building_fixture();
    reserved.collider_id = "signal-pole-99";
    reject({reserved}, traffic_fixture());
    reject({building_fixture(), building_fixture()}, traffic_fixture());
    auto invalid = building_fixture();
    invalid.shape.half_height_m = std::numeric_limits<double>::quiet_NaN();
    reject({invalid}, traffic_fixture());
    auto invalid_traffic = traffic_fixture();
    invalid_traffic.signals[0].position_enu.east_m = std::numeric_limits<double>::infinity();
    reject({building_fixture()}, invalid_traffic);
    std::vector<StaticObbCollider> too_many;
    for (std::size_t index = 0; index <= StructureDamageRuntime::maximum_targets; ++index) {
        auto wall = building_fixture();
        wall.collider_id = "Building_" + std::to_string(index);
        too_many.push_back(std::move(wall));
    }
    reject(too_many, {});
    runtime.rebuild({}, nullptr);
    require(runtime.snapshots().empty() && runtime.collision_proxies().empty(),
        "maps without traffic or walls must remain supported");
}

void reset_clears_pending_impulse_and_episode_history()
{
    auto runtime = runtime_fixture();
    hit(runtime, "Building_01", 6450.0);
    runtime.record_contacts({contact("signal-pole-1", 3000.0)}, 2.8);
    runtime.reset();
    runtime.tick(1.0 / 60.0, true);
    require(runtime.snapshots().empty(), "reset must discard pending contact and previous facade damage");
    hit(runtime, "Building_01", 6450.0);
    const auto& damage = snapshot(runtime, "Building_01");
    require(near(damage.damage_percent, 50.0) && damage.event_sequence == 1,
        "new Play must start a fresh damage episode and event sequence");
}

void real_pole_impact_spends_anchor_before_finite_mass_and_preserves_motion()
{
    for (int hz : {30, 60, 120}) {
        TrafficNetwork traffic;
        traffic.signals = {{1, 1, {0, 0, 0}, 0, 1, TrafficSignalKind::Vehicle}};
        StructureDamageRuntime runtime;
        runtime.rebuild({}, &traffic);
        CollisionWorld world;
        PlanarRigidBody body{"ego", {{0, -2.46}, .8, 0, 2.2, 1, .55}, {0, 15}, 0, 1500, 2850};
        const double original_energy = .5 * body.mass_kg * 15 * 15;
        auto impact = world.integrate(body, 1.0 / hz, runtime.collision_proxies());
        require(impact.resolved_dynamic_proxies.front().breakaway_released
            && impact.body.linear_velocity_enu_mps.north_m > 10.0
            && impact.body.linear_velocity_enu_mps.north_m < 15.0,
            "a severe hit must spend the anchor and retain finite forward momentum in the first tick");
        const auto& moved = impact.resolved_dynamic_proxies.front();
        const double energy = .5 * impact.body.mass_kg * std::pow(impact.body.linear_velocity_enu_mps.north_m, 2)
            + .5 * moved.mass_kg * std::pow(moved.linear_velocity_enu_mps.north_m, 2);
        require(energy < original_energy && moved.linear_velocity_enu_mps.north_m > 0,
            "base breakage must dissipate energy and transfer residual momentum, never refund a stopped car");
        runtime.record_contacts(impact.contacts, .8, &impact.body.shape);
        runtime.accept_resolved_proxies(impact.resolved_dynamic_proxies);
        body = impact.body;
        const double released_north = std::get<ObbPrism>(moved.shape).center_enu.north_m;
        for (int tick = 0; tick < hz; ++tick) {
            runtime.tick(1.0 / hz, true);
            impact = world.integrate(body, 1.0 / hz, runtime.collision_proxies());
            runtime.record_contacts(impact.contacts, .8, &impact.body.shape);
            runtime.accept_resolved_proxies(impact.resolved_dynamic_proxies);
            body = impact.body;
        }
        require(snapshot(runtime, "signal-pole-1").disabled
            && body.shape.center_enu.north_m > 4.0 && body.linear_velocity_enu_mps.north_m > 5.0
            && pole_shape(runtime, "signal-pole-1").center_enu.north_m > released_north + 1.0,
            "released poles must retain solved motion across ticks instead of resetting into an invisible wall");
        runtime.tick(.1, false);
        require(runtime.collision_proxies().front().linear_velocity_enu_mps.north_m == 0,
            "SafeStop must publish zero pole motion to the collision solve without healing it");
        runtime.tick(.01, true);
        require(runtime.collision_proxies().front().linear_velocity_enu_mps.north_m > 1.0,
            "resuming must preserve the released pole's stored momentum");
        runtime.reset();
        require(runtime.snapshots().empty() && near(pole_shape(runtime, "signal-pole-1").center_enu.north_m, 0)
            && !runtime.collision_proxies().front().breakaway_released,
            "new Play must restore the authored base, not the translated crash position");

        auto slow = PlanarRigidBody{"ego", {{0, -2.44}, .8, 0, 2.2, 1, .55}, {0, .3}, 0, 1500, 2850};
        const auto gentle = world.integrate(slow, 1.0 / hz, runtime.collision_proxies());
        require(!gentle.resolved_dynamic_proxies.front().breakaway_released
            && gentle.body.linear_velocity_enu_mps.north_m < .03
            && near(std::get<ObbPrism>(gentle.resolved_dynamic_proxies.front().shape).center_enu.north_m, 0),
            "a gentle bumper touch cannot release or translate an intact pole");

        CollisionWorld wall({{"rigid-wall", StaticColliderSemantic::Wall,
            {{0, 0}, 1.925, 0, .25, 8, 1.925}, {.65, 0}}});
        body = {"ego", {{0, -2.46}, .8, 0, 2.2, 1, .55}, {0, 15}, 0, 1500, 2850};
        require(std::abs(wall.integrate(body, 1.0 / hz).body.linear_velocity_enu_mps.north_m) < .01,
            "breakaway behavior must not weaken rigid walls");
    }
}

void tire_support_and_roof_support_are_exact_pair_scoped()
{
    CollisionWorld world;
    PlanarRigidBody body{"ego", {{0, 0}, .8, 0, 2.2, 1, .55}, {0, 5}, 0, 1500, 2850};
    KinematicCollisionProxy low{"fallen", ObbPrism{{0, 2.3}, .2, 0, .5, .5, .2}, {}, 0,
        {.5, 0}, 80, 10, 30};
    low.tire_support_candidate = true;
    const auto supported = world.integrate_with_tire_supported_curbs(body, .02, {low}, {}, {"fallen"});
    require(supported.contacts.empty() && supported.resolved_dynamic_proxies.size() == 1,
        "tire support suppresses only the supported Ego planar pair and retains the finite object");
    low.tire_support_candidate = false;
    bool rejected = false;
    try { (void)world.integrate_with_tire_supported_curbs(body, .02, {low}, {}, {"fallen"}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "ordinary proxies cannot be passed as tire support");
    low.shape = ObbPrism{{0, 2.3}, .8, 0, .5, .5, .4};
    low.supported_vehicle_id = "ego";
    require(world.integrate(body, .02, {low}).contacts.empty(), "verified roof support excludes its supporting Ego");
    low.supported_vehicle_id = "another-car";
    require(!world.integrate(body, .02, {low}).contacts.empty(), "roof support cannot exclude another vehicle");
}

} // namespace

int main()
{
    try {
        intact_targets_are_bounded_sorted_and_do_not_replace_buildings();
        low_and_unknown_contacts_do_not_damage();
        building_partial_damage_preserves_shell();
        penetrating_building_contact_is_projected_to_exterior();
        rotated_building_projection_selects_facade_not_body_sat_axis();
        vehicle_contact_footprint_distinguishes_front_corner_side_and_wall_edge();
        saturated_building_still_reports_new_local_impacts();
        sustained_pressure_is_rate_independent_and_not_endless_damage();
        split_impulse_can_collapse_at_all_rates();
        repeated_distinct_impacts_accumulate();
        collapse_direction_ground_support_and_no_ghost();
        outage_is_head_scoped_and_reset_clears_it();
        pause_discards_pending_and_freezes_existing_pose();
        invalid_inputs_do_not_poison_authoritative_snapshots();
        rebuild_validation_is_transactional();
        reset_clears_pending_impulse_and_episode_history();
        real_pole_impact_spends_anchor_before_finite_mass_and_preserves_motion();
        tire_support_and_roof_support_are_exact_pair_scoped();
        std::cout << "[PASS] Structure damage policy: 18 cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
