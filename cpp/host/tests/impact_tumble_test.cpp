#include "traffic/impact_tumble.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {

using namespace simcore_host;
constexpr double pi = std::numbers::pi_v<double>;
const ImpactTumbleDimensions car;

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

bool near(double first, double second, double epsilon = 1.0e-8)
{
    return std::isfinite(first) && std::isfinite(second) && std::abs(first - second) <= epsilon;
}

void advance(ImpactTumbleState& state, double seconds, int hz = 60)
{
    for (int index = 0; index < static_cast<int>(std::lround(seconds * hz)); ++index) {
        state.tick(1.0 / hz, car);
    }
}

void default_and_reset()
{
    ImpactTumbleState state;
    require(!state.active() && state.settled() && !state.overturned(), "default state must be upright and settled");
    state.apply_contact(30000.0, {-1.0, 0.0}, 0.0, car);
    advance(state, 10.0);
    require(state.overturned(), "fixture must topple before reset");
    state.reset();
    require(!state.active() && state.settled() && !state.overturned() && !state.invalid_input(),
            "new Play reset must clear pose, rates and overturn latch");
}

void weak_contact_rocks_then_returns_upright()
{
    ImpactTumbleState state;
    state.apply_contact(4500.0, {-1.0, 0.0}, 0.0, car); // 3m/s lateral delta-V.
    double largest_roll = 0.0;
    for (int index = 0; index < 600; ++index) {
        state.tick(1.0 / 60.0, car);
        largest_roll = std::max(largest_roll, std::abs(state.roll_rad()));
    }
    require(largest_roll > 0.005 && largest_roll < 0.2, "minor collision must visibly rock without a scripted flip");
    require(!state.overturned() && state.settled() && near(state.roll_rad(), 0.0),
            "gravity and landing loss must settle weak collision upright without chatter");
}

void strong_lateral_contact_lands_on_side()
{
    ImpactTumbleState state;
    state.apply_contact(30000.0, {-1.0, 0.0}, 0.0, car); // 20m/s severe delta-V.
    advance(state, 15.0);
    require(state.overturned() && state.settled(), "strong lateral impact must overturn and settle");
    require(near(std::abs(state.roll_rad()), pi / 2.0), "overturned car must remain on its side, not spring upright");
    advance(state, 20.0);
    require(near(std::abs(state.roll_rad()), pi / 2.0), "resting overturned car cannot auto-right");
}

void larger_contact_can_reach_roof()
{
    ImpactTumbleState state;
    state.apply_contact(180000.0, {-1.0, 0.0}, 0.0, car);
    advance(state, 15.0);
    require(state.overturned() && state.settled(), "large finite impact must remain bounded and settle");
    require(near(std::abs(state.roll_rad()), pi), "enough angular energy must allow rotation beyond the side onto roof");
}

void mirrored_contacts_and_heading_transform()
{
    ImpactTumbleState left;
    ImpactTumbleState right;
    ImpactTumbleState eastbound;
    left.apply_contact(30000.0, {-1.0, 0.0}, 0.0, car);
    right.apply_contact(30000.0, {1.0, 0.0}, 0.0, car);
    eastbound.apply_contact(30000.0, {0.0, 1.0}, pi / 2.0, car);
    for (int index = 0; index < 600; ++index) {
        left.tick(1.0 / 60.0, car);
        right.tick(1.0 / 60.0, car);
        eastbound.tick(1.0 / 60.0, car);
        require(near(left.roll_rad(), -right.roll_rad()), "mirrored impulse must give mirrored roll");
        require(near(left.roll_rad(), eastbound.roll_rad()), "world heading transform must preserve actor-relative contact");
        require(near(left.pitch_rad(), 0.0) && near(eastbound.pitch_rad(), 0.0),
                "pure side impact must not produce pitch");
    }
}

void frontal_contact_respects_larger_pitch_barrier()
{
    ImpactTumbleState lateral;
    ImpactTumbleState frontal;
    lateral.apply_contact(30000.0, {-1.0, 0.0}, 0.0, car);
    frontal.apply_contact(30000.0, {0.0, 1.0}, 0.0, car);
    advance(lateral, 15.0);
    advance(frontal, 15.0);
    require(lateral.overturned() && !frontal.overturned() && frontal.settled(),
            "longitudinal support requires more energy than lateral rollover");
    frontal.apply_contact(150000.0, {0.0, 1.0}, 0.0, car);
    advance(frontal, 15.0);
    require(frontal.overturned() && frontal.settled() && std::abs(frontal.pitch_rad()) > 1.0,
            "a sufficiently severe frontal impulse may pitch the car onto another face");
}

void all_tilted_corners_remain_above_floor()
{
    for (int pitch_degrees = -180; pitch_degrees <= 180; pitch_degrees += 10) {
        for (int roll_degrees = -180; roll_degrees <= 180; roll_degrees += 10) {
            const double pitch = pitch_degrees * pi / 180.0, roll = roll_degrees * pi / 180.0;
            const double sp = std::sin(pitch), cp = std::cos(pitch);
            const double sr = std::sin(roll), cr = std::cos(roll);
            const auto support = impact_tumble_support(car, pitch, roll);
            require(support.valid, "finite supported geometry must be valid");
            double lowest = 100.0;
            for (const double forward : {-car.half_length_m, car.half_length_m}) {
                for (const double left : {-car.half_width_m, car.half_width_m}) {
                    for (const double up : {-car.half_height_m, car.half_height_m}) {
                        const double transformed_up = forward * sp + left * cp * sr + up * cp * cr;
                        const double transformed_forward = forward * cp - left * sp * sr - up * sp * cr;
                        const double transformed_left = left * cr - up * sr;
                        lowest = std::min(lowest, support.support_height_m + transformed_up);
                        require(support.support_height_m + transformed_up >= -1.0e-9,
                                "a rotated corner must not penetrate the ground plane");
                        require(std::abs(transformed_forward) <= support.projected_half_length_m + 1.0e-9
                                    && std::abs(transformed_left) <= support.projected_half_width_m + 1.0e-9,
                                "yaw-aligned collision envelope must contain every tilted corner");
                    }
                }
            }
            require(near(lowest, 0.0), "at least one support corner must touch floor at every angle");
        }
    }
    require(near(impact_tumble_support(car, 0.0, 0.0).support_height_m, 0.75), "upright baseline must stay unchanged");
    require(near(impact_tumble_support(car, 0.0, pi / 2.0).support_height_m, 1.0), "side landing support uses half width");
    require(near(impact_tumble_support(car, 0.0, pi).support_height_m, 0.75), "roof landing support uses half height");
}

void rate_and_pause_safety()
{
    for (const int hz : {30, 60, 120, 1000}) {
        ImpactTumbleState state;
        state.apply_contact(30000.0, {-1.0, 0.0}, 0.0, car);
        advance(state, 15.0, hz);
        require(state.overturned() && state.settled() && near(std::abs(state.roll_rad()), pi / 2.0),
                "supported tick rates must converge to the same landed face");
    }
    ImpactTumbleState state;
    state.apply_contact(30000.0, {-1.0, 0.0}, 0.0, car);
    state.tick(0.1, car);
    const double angle = state.roll_rad(), rate = state.roll_rate_rad_s();
    state.tick(10.0, car, false);
    require(near(state.roll_rad(), angle) && near(state.roll_rate_rad_s(), rate), "pause must freeze pose and velocity");
    state.stop_motion();
    require(near(state.roll_rad(), angle) && state.roll_rate_rad_s() == 0.0,
            "collision stop must not snap rotation upright");
    state.tick(10.0, car);
    require(state.settled() && std::isfinite(state.roll_rad()), "large valid dt must substep to bounded settled pose");
}

void invalid_values_fail_closed()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double impulse : {-1.0, nan, inf}) {
        ImpactTumbleState state;
        state.apply_contact(impulse, {1.0, 0.0}, 0.0, car);
        require(state.invalid_input() && std::isfinite(state.roll_rad()), "invalid contact must latch without poisoning pose");
    }
    for (double dt : {-1.0, nan, inf, 10.01}) {
        ImpactTumbleState state;
        state.tick(dt, car);
        require(state.invalid_input(), "invalid duration must fail closed");
    }
    ImpactTumbleState invalid_direction;
    invalid_direction.apply_contact(1.0, {0.0, 0.0}, 0.0, car);
    require(invalid_direction.invalid_input(), "nonzero impulse needs a valid direction");
    auto invalid_car = car;
    invalid_car.half_width_m = nan;
    require(!impact_tumble_support(invalid_car, 0.0, 0.0).valid
                && !impact_tumble_support(car, inf, 0.0).valid,
            "invalid geometry must return an invalid finite envelope");
}

} // namespace

int main()
{
    try {
        default_and_reset();
        weak_contact_rocks_then_returns_upright();
        strong_lateral_contact_lands_on_side();
        larger_contact_can_reach_roof();
        mirrored_contacts_and_heading_transform();
        frontal_contact_respects_larger_pitch_barrier();
        all_tilted_corners_remain_above_floor();
        rate_and_pause_safety();
        invalid_values_fail_closed();
        std::cout << "[PASS] Impact tumble physics: 9 cases\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }
}
