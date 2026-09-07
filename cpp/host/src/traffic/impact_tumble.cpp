#include "traffic/impact_tumble.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace simcore_host {
namespace {

constexpr double pi = std::numbers::pi_v<double>;
constexpr double quarter_turn = pi / 2.0;
constexpr double gravity_mps2 = 9.81;
constexpr double maximum_step_seconds = 1.0 / 480.0;
constexpr double angular_drag_per_second = 0.35;
constexpr double face_landing_restitution = 0.22;
constexpr double resting_rate_rad_s = 0.06;
constexpr double maximum_angular_rate_rad_s = 20.0;

bool valid_dimensions(const ImpactTumbleDimensions& dimensions) noexcept
{
    const auto valid_extent = [](double value) {
        return std::isfinite(value) && value >= 0.01 && value <= 100.0;
    };
    return valid_extent(dimensions.half_length_m) && valid_extent(dimensions.half_width_m)
        && valid_extent(dimensions.half_height_m) && std::isfinite(dimensions.mass_kg)
        && dimensions.mass_kg >= 1.0 && dimensions.mass_kg <= 1.0e7
        && std::isfinite(dimensions.contact_below_cg_m)
        && dimensions.contact_below_cg_m >= 0.0
        && dimensions.contact_below_cg_m <= dimensions.half_height_m;
}

double sign_away_from_face(double value) noexcept
{
    return std::abs(value) < 1.0e-12 ? 0.0 : (value > 0.0 ? 1.0 : -1.0);
}

bool on_face(double angle) noexcept
{
    return std::abs(angle - std::round(angle / quarter_turn) * quarter_turn) < 1.0e-9;
}

// Corner-to-corner motion loses energy when the next face contacts the ground.
// A face at rest is a non-smooth potential minimum: use zero static torque there
// instead of choosing an arbitrary sign at sin/cos(0), which causes chatter.
void advance_axis(double& angle, double& rate, double gravity_acceleration,
                  double dt_seconds) noexcept
{
    if (on_face(angle) && std::abs(rate) < resting_rate_rad_s) {
        angle = std::round(angle / quarter_turn) * quarter_turn;
        rate = 0.0;
        return;
    }
    const double before = angle;
    rate = std::clamp((rate + gravity_acceleration * dt_seconds)
                         * std::exp(-angular_drag_per_second * dt_seconds),
                     -maximum_angular_rate_rad_s, maximum_angular_rate_rad_s);
    angle += rate * dt_seconds;
    const double target_face = rate >= 0.0
        ? (std::floor(before / quarter_turn + 1.0e-12) + 1.0) * quarter_turn
        : (std::ceil(before / quarter_turn - 1.0e-12) - 1.0) * quarter_turn;
    if ((before < target_face && angle >= target_face)
        || (before > target_face && angle <= target_face)) {
        angle = target_face;
        rate *= face_landing_restitution;
        if (std::abs(rate) < resting_rate_rad_s) { rate = 0.0; }
    }
    angle = std::remainder(angle, 2.0 * pi);
}

} // namespace

ImpactTumbleSupport impact_tumble_support(const ImpactTumbleDimensions& dimensions,
                                         double pitch_rad, double roll_rad) noexcept
{
    ImpactTumbleSupport support;
    if (!valid_dimensions(dimensions) || !std::isfinite(pitch_rad) || !std::isfinite(roll_rad)) {
        return support;
    }
    const double sp = std::abs(std::sin(pitch_rad)), cp = std::abs(std::cos(pitch_rad));
    const double sr = std::abs(std::sin(roll_rad)), cr = std::abs(std::cos(roll_rad));
    support.support_height_m = dimensions.half_length_m * sp
        + dimensions.half_width_m * cp * sr + dimensions.half_height_m * cp * cr;
    support.projected_half_height_m = support.support_height_m;
    support.projected_half_length_m = dimensions.half_length_m * cp
        + dimensions.half_width_m * sp * sr + dimensions.half_height_m * sp * cr;
    support.projected_half_width_m = dimensions.half_width_m * cr + dimensions.half_height_m * sr;
    support.valid = true;
    return support;
}

void ImpactTumbleState::apply_contact(double impulse, CollisionVector2 direction,
                                     double heading, const ImpactTumbleDimensions& dimensions) noexcept
{
    if (invalid_input_) { return; }
    if (!valid_dimensions(dimensions) || !std::isfinite(impulse) || impulse < 0.0
        || !std::isfinite(heading) || !std::isfinite(direction.east_m)
        || !std::isfinite(direction.north_m)) {
        fail_closed();
        return;
    }
    if (impulse == 0.0) { return; }
    const double length = std::hypot(direction.east_m, direction.north_m);
    const double delta_v = impulse / dimensions.mass_kg;
    if (!std::isfinite(length) || length < 1.0e-12 || !std::isfinite(delta_v)) {
        fail_closed();
        return;
    }
    const double forward = (direction.east_m / length) * std::sin(heading)
        + (direction.north_m / length) * std::cos(heading);
    const double left = -(direction.east_m / length) * std::cos(heading)
        + (direction.north_m / length) * std::sin(heading);
    // I_pivot/m = I_CG/m + distance_to_support_edge^2. This reduced model
    // includes the work needed to lift the centre instead of using CG-only
    // inertia and gifting a grounded body four times too much rocking energy.
    const double pitch_inertia_per_mass = 4.0 / 3.0
        * (dimensions.half_length_m * dimensions.half_length_m
           + dimensions.half_height_m * dimensions.half_height_m);
    const double roll_inertia_per_mass = 4.0 / 3.0
        * (dimensions.half_width_m * dimensions.half_width_m
           + dimensions.half_height_m * dimensions.half_height_m);
    const double pitch_impulse = delta_v * dimensions.contact_below_cg_m * forward
        / pitch_inertia_per_mass;
    const double roll_impulse = delta_v * dimensions.contact_below_cg_m * left
        / roll_inertia_per_mass;
    if (!std::isfinite(pitch_impulse) || !std::isfinite(roll_impulse)) { fail_closed(); return; }
    pitch_rate_rad_s_ = std::clamp(pitch_rate_rad_s_ + pitch_impulse,
                                 -maximum_angular_rate_rad_s, maximum_angular_rate_rad_s);
    roll_rate_rad_s_ = std::clamp(roll_rate_rad_s_ + roll_impulse,
                                -maximum_angular_rate_rad_s, maximum_angular_rate_rad_s);
}

void ImpactTumbleState::tick(double dt, const ImpactTumbleDimensions& dimensions, bool enabled) noexcept
{
    if (invalid_input_) { return; }
    if (!valid_dimensions(dimensions) || !std::isfinite(dt) || dt < 0.0 || dt > 10.0) {
        fail_closed();
        return;
    }
    if (!enabled || dt == 0.0) { return; }
    const int steps = static_cast<int>(std::ceil(dt / maximum_step_seconds));
    const double step = dt / steps;
    const double pitch_inertia_per_mass = 4.0 / 3.0
        * (dimensions.half_length_m * dimensions.half_length_m
           + dimensions.half_height_m * dimensions.half_height_m);
    const double roll_inertia_per_mass = 4.0 / 3.0
        * (dimensions.half_width_m * dimensions.half_width_m
           + dimensions.half_height_m * dimensions.half_height_m);
    for (int index = 0; index < steps; ++index) {
        const double sp = std::sin(pitch_rad_), cp = std::cos(pitch_rad_);
        const double sr = std::sin(roll_rad_), cr = std::cos(roll_rad_);
        const double dh_dp = dimensions.half_length_m * sign_away_from_face(sp) * cp
            - (dimensions.half_width_m * std::abs(sr) + dimensions.half_height_m * std::abs(cr))
                * sign_away_from_face(cp) * sp;
        const double dh_dr = std::abs(cp)
            * (dimensions.half_width_m * sign_away_from_face(sr) * cr
               - dimensions.half_height_m * sign_away_from_face(cr) * sr);
        advance_axis(pitch_rad_, pitch_rate_rad_s_, -gravity_mps2 * dh_dp / pitch_inertia_per_mass, step);
        advance_axis(roll_rad_, roll_rate_rad_s_, -gravity_mps2 * dh_dr / roll_inertia_per_mass, step);
        if (std::cos(pitch_rad_) * std::cos(roll_rad_) < 0.5) { overturned_ = true; }
    }
}

bool ImpactTumbleState::active() const noexcept
{
    return invalid_input_ || overturned_ || std::abs(pitch_rad_) > 1.0e-9
        || std::abs(roll_rad_) > 1.0e-9 || pitch_rate_rad_s_ != 0.0 || roll_rate_rad_s_ != 0.0;
}

bool ImpactTumbleState::settled() const noexcept
{
    return !invalid_input_ && pitch_rate_rad_s_ == 0.0 && roll_rate_rad_s_ == 0.0
        && on_face(pitch_rad_) && on_face(roll_rad_);
}

void ImpactTumbleState::fail_closed() noexcept
{
    invalid_input_ = true;
    pitch_rate_rad_s_ = 0.0;
    roll_rate_rad_s_ = 0.0;
}

void ImpactTumbleState::reset() noexcept
{
    *this = ImpactTumbleState{};
}

void ImpactTumbleState::stop_motion() noexcept
{
    pitch_rate_rad_s_ = 0.0;
    roll_rate_rad_s_ = 0.0;
}

} // namespace simcore_host
