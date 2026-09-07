#include "traffic/pedestrian_impact.hpp"
#include "traffic/impact_recovery.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void contact_launch_ground_and_recovery()
{
    simcore_host::PedestrianImpactState body;
    body.contact(1, 320.0, {1,0}, .9, 0);
    require(body.downed() && body.vertical_velocity_mps() == 0.0
        && body.pitch_rad() > 0.0 && body.pitch_rate_rad_s() > 0.0,
        "a low bumper hit must immediately rotate the torso toward the hood without inventing upward launch");
    for (int i = 0; i < 240; ++i) {
        body.tick(1.0 / 120.0, 0, false);
        require(body.center_up_m() - body.support().support_height_m >= -1e-8,
            "airborne/rotating body envelope must never pass below its ground support");
    }
    const auto support = body.support();
    require(body.downed() && body.settled() && !body.airborne()
        && std::abs(support.projected_half_height_m - .18) < 1e-8
        && std::abs(support.projected_half_length_m - .90) < 1e-8,
        "settled pedestrian must have a low horizontal body instead of an invisible standing capsule");
    body.contact(1, 320.0, {1,0}, body.center_up_m(), 0);
    require(!body.airborne(), "one continuing contact episode cannot repeatedly launch a person");
    body.contact(2, 480.0, {1,0}, body.center_up_m(), 0);
    require(body.pitch_rate_rad_s() < 0.0 && body.vertical_velocity_mps() == 0.0,
        "a later independent impact rocks the downed body by its contact torque without artificial lift");
    for (int i = 0; i < 360; ++i) body.tick(1.0 / 120.0, 0, false);
    for (int i = 0; i < 240; ++i) body.tick(1.0 / 120.0, 0, true);
    require(!body.downed() && !body.airborne() && std::abs(body.center_up_m() - .9) < 1e-8,
        "only an authorized recovery may restore the standing body height");
    body.reset();
    require(!body.downed() && body.vertical_velocity_mps() == 0.0, "new Play resets body trajectory");
}
void elevated_body_falls_under_gravity_and_anchors_to_ground()
{
    simcore_host::PedestrianImpactState body;
    body.contact(1, 320.0, {1,0}, 4.0, 0.0);
    require(body.downed() && body.airborne()
            && body.center_up_m() == 4.0 && body.vertical_velocity_mps() == 0.0,
        "a body already above the surface starts without fabricated upward velocity");
    body.tick(0.25, 0.0, false);
    require(body.airborne() && body.vertical_velocity_mps() < -2.45
            && body.vertical_velocity_mps() > -2.46
            && body.center_up_m() < 3.70,
        "an unsupported pedestrian must visibly accelerate down at earth gravity");
    for (int i = 0; i < 600; ++i) body.tick(1.0 / 120.0, 0.0, false);
    require(body.settled() && !body.airborne()
            && std::abs(body.center_up_m() - body.support().support_height_m) < 1e-9
            && body.vertical_velocity_mps() == 0.0,
        "after impact flight the finite body must finish exactly on its ground support plane");
}
void tick_rate_and_pause()
{
    double reference = 0;
    for (const int hz : {30,60,120}) {
        simcore_host::PedestrianImpactState body;
        body.contact(1, 640, {0,1}, 2.9, 2.0);
        const double initial_height = body.center_up_m();
        body.tick(.5, 2.0, false, false);
        require(body.center_up_m() == initial_height, "pause must not advance airborne time");
        for (int i=0; i<hz/2; ++i) body.tick(1.0/hz, 2.0, false);
        if (hz==30) reference=body.center_up_m();
        require(std::abs(body.center_up_m()-reference)<1e-5,
            "the same half-second flight must agree across 30/60/120Hz");
    }
}
void gentle_shoves_do_not_accumulate_into_falls()
{
    simcore_host::PedestrianImpactState body;
    for (std::uint32_t event = 1; event <= 20; ++event) {
        body.contact(event, 60.0, {1,0}, .9, 0);
        for (int frame = 0; frame < 30; ++frame) {
            body.tick(1.0 / 60.0, 0, false);
            body.contact(event, 60.0, {1,0}, .9, 0);
        }
        require(!body.downed() && !body.airborne() && body.pitch_rad() == 0
                && body.vertical_velocity_mps() == 0,
            "separate gentle bumps and duplicate reports must retain the standing capsule");
    }
    body.contact(20, 400.0, {1,0}, .9, 0);
    require(!body.downed(), "late growth in a held contact is pressure, not a delayed launch");
    body.contact(19, 500.0, {1,0}, .9, 0);
    require(!body.downed(), "an older collision event cannot overwrite the current standing episode");
    body.contact(21, 160.0, {1,0}, .9, 0);
    require(body.downed() && body.pitch_rad() > 0 && body.vertical_velocity_mps() == 0,
        "a moderate independent impact causes loss of balance without an artificial upward throw");
    body.reset();
    body.contact(1, 60.0, {0,1}, .9, 0);
    require(!body.downed(), "new Play resets event history without promoting an old gentle contact");
}
void growing_contact_crosses_threshold_once()
{
    simcore_host::PedestrianImpactState body;
    body.contact(1, 40.0, {1,0}, .9, 0);
    body.tick(.04, 0, false);
    body.contact(1, 100.0, {1,0}, .9, 0);
    require(!body.downed(), "a sub-threshold growing episode remains a standing shove");
    body.tick(.04, 0, false);
    body.contact(1, 160.0, {1,0}, .9, 0);
    require(body.downed() && body.vertical_velocity_mps() == 0,
        "crossing the balance threshold within the same event immediately starts the fall");
    body.contact(1, 320.0, {1,0}, body.center_up_m(), 0);
    const double rotation = body.pitch_rate_rad_s();
    require(rotation > 0.0 && body.vertical_velocity_mps() == 0.0,
        "a stronger horizontal impulse adds signed torque, never unrelated upward velocity");
    body.contact(1, 320.0, {1,0}, body.center_up_m(), 0);
    body.contact(1, 120.0, {1,0}, body.center_up_m(), 0);
    require(body.pitch_rate_rad_s() == rotation && body.vertical_velocity_mps() == 0.0,
        "duplicate or lower event peaks cannot add another angular impulse");
    body.contact(1, 400.0, {1,0}, body.center_up_m(), 0);
    require(body.pitch_rate_rad_s() > rotation && body.vertical_velocity_mps() == 0.0,
        "stronger same-event reports add only their new torque contribution");
}
void contact_height_and_vehicle_support()
{
    using namespace simcore_host;
    PedestrianImpactState knee, chest, side;
    knee.contact(1, 320, {0,1}, .9, 0, {.45});
    chest.contact(1, 320, {0,1}, .9, 0, {1.35});
    side.contact(1, 320, {1,0}, .9, 0, {.45});
    require(knee.pitch_rate_rad_s() > 0 && chest.pitch_rate_rad_s() < 0
        && std::abs(side.heading_rad() - std::numbers::pi / 2) < 1e-9,
        "contact height reverses torque while side strikes rotate in the actual impact plane");
    require(knee.vertical_velocity_mps() == 0 && chest.vertical_velocity_mps() == 0,
        "neither a low nor high purely horizontal impact fabricates upward impulse");
    const std::vector<PedestrianVehicleSurface> vehicles{{"ego", {{0,0}, .85, 0, 2.2, 1, .75}}};
    bool hood_support = false;
    for (int i=0; i<180; ++i) {
        knee.tick(1.0/120, 0, false, true, {0,2.4}, vehicles);
        hood_support |= knee.supported_vehicle_id() == "ego";
    }
    require(hood_support && knee.center_up_m() > 1.0,
        "a lower-body frontal strike can rotate onto the overlapping hood instead of always ejecting forward");
    PedestrianImpactState under_car;
    under_car.contact(1, 320, {0,1}, .9, 0);
    for (int i=0; i<240; ++i) under_car.tick(1.0/120, 0, false);
    for (int i=0; i<60; ++i) under_car.tick(1.0/120, 0, false, true, {0,0}, vehicles);
    require(under_car.supported_vehicle_id().empty() && under_car.center_up_m() < .3,
        "a person below the car cannot be teleported onto its roof or globally ignore car contacts");
    for (int i=0; i<240; ++i) knee.tick(1.0/120, 0, false, true, {0,8}, vehicles);
    require(knee.supported_vehicle_id().empty() && knee.center_up_m() < .3,
        "leaving the hood clears the exact vehicle contact exclusion and lands naturally");
}
void shared_contact_policy_agrees_across_tick_rates()
{
    for (const int hz : {30,60,120}) {
        for (const double total_impulse : {60.0, 320.0}) {
            simcore_host::ImpactRecoveryState contact;
            simcore_host::PedestrianImpactState body;
            const int frames = hz / 5; // Same physical impulse spread over 200ms.
            std::uint32_t event = 0;
            bool saw_standing_event = false;
            for (int frame = 0; frame < frames; ++frame) {
                body.tick(1.0 / hz, 0, false);
                contact.record_contact(total_impulse / frames,
                    simcore_host::PedestrianImpactState::body_dimensions.mass_kg);
                contact.tick(1.0 / hz, false);
                body.contact(contact.event_sequence(), contact.last_impact_impulse_n_s(),
                    {1,0}, body.downed() ? body.center_up_m() : .9, 0);
                if (contact.event_sequence() != 0) {
                    if (event == 0) event = contact.event_sequence();
                    require(contact.event_sequence() == event,
                        "an increasing collision impulse retains one shared presentation event");
                    if (contact.last_impact_impulse_n_s()
                        < simcore_host::PedestrianImpactState::knockdown_impulse_n_s) {
                        saw_standing_event = true;
                        require(!body.downed(), "a valid contact event alone must not cause a fall");
                    } else {
                        require(body.downed(), "sufficient shared episode impulse falls in the same tick");
                    }
                }
            }
            require(event != 0 && saw_standing_event, "fixture must exercise mild contact metadata first");
            require(body.downed() == (total_impulse >= simcore_host::PedestrianImpactState::knockdown_impulse_n_s),
                "60Ns shoves and 320Ns falls must classify identically at 30/60/120Hz");
        }
    }
}
}
int main()
{
    try { contact_launch_ground_and_recovery(); tick_rate_and_pause();
        gentle_shoves_do_not_accumulate_into_falls(); growing_contact_crosses_threshold_once();
        elevated_body_falls_under_gravity_and_anchors_to_ground();
        shared_contact_policy_agrees_across_tick_rates();
        contact_height_and_vehicle_support();
        std::cout << "pedestrian_impact_tests: passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
