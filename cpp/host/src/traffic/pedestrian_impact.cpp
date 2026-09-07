#include "traffic/pedestrian_impact.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace simcore_host {
namespace {
constexpr double half_pi = std::numbers::pi / 2.0;
constexpr double gravity = 9.81;
constexpr double maximum_pitch_rate_rad_s = 7.0;

double sedan_surface_up(const ObbPrism& vehicle, CollisionVector2 point)
{
    const double east = point.east_m - vehicle.center_enu.east_m;
    const double north = point.north_m - vehicle.center_enu.north_m;
    const double forward = east * std::sin(vehicle.heading_rad) + north * std::cos(vehicle.heading_rad);
    const double right = east * std::cos(vehicle.heading_rad) - north * std::sin(vehicle.heading_rad);
    if (std::abs(forward) > vehicle.half_length_m || std::abs(right) > vehicle.half_width_m)
        return -std::numeric_limits<double>::infinity();
    // Reduced sedan profile, not a roof-height rectangular wall at the bumper.
    // Front/rear decks are 58% of body height, with a sloped windscreen to the roof.
    const double longitudinal = forward / vehicle.half_length_m;
    const double roof = std::clamp((0.65 - std::abs(longitudinal)) / 0.35, 0.0, 1.0);
    const double bottom = vehicle.center_up_m - vehicle.half_height_m;
    return bottom + 2.0 * vehicle.half_height_m * (0.58 + 0.42 * roof);
}
}

ImpactTumbleSupport PedestrianImpactState::support() const noexcept
{
    return impact_tumble_support(body_dimensions, pitch_rad_, 0.0);
}

bool PedestrianImpactState::settled() const noexcept
{
    return !downed_ || (!airborne_ && vertical_velocity_mps_ == 0.0
        && pitch_rate_rad_s_ == 0.0 && std::abs(std::abs(pitch_rad_) - half_pi) < 1e-6);
}

void PedestrianImpactState::contact(std::uint32_t event, double impulse,
    CollisionVector2 direction, double current_up, double ground_up,
    const PedestrianImpactGeometry& geometry) noexcept
{
    const double length = std::hypot(direction.east_m, direction.north_m);
    if (event == 0 || !std::isfinite(impulse) || impulse < 12.0 || !std::isfinite(length)
        || length < 1e-9 || !std::isfinite(current_up) || !std::isfinite(ground_up)) return;
    if (event_ != 0 && event < event_) return;
    const bool new_event = event != event_;
    if (new_event) {
        event_ = event;
        event_impulse_n_s_ = 0.0;
        event_age_seconds_ = 0.0;
    }
    const double previous_impulse = event_impulse_n_s_;
    event_impulse_n_s_ = std::max(event_impulse_n_s_, impulse);
    if (impulse <= previous_impulse || event_age_seconds_ > contact_window_seconds + 1e-9) return;
    // Do not add repeated reports or separate gentle bumps together. Only the
    // growing peak of this brief physical contact episode can cause a fall.
    if (!downed_ && impulse < knockdown_impulse_n_s) return;
    const bool was_downed = downed_;
    const double contact_up = std::isfinite(geometry.contact_up_m) ? geometry.contact_up_m : ground_up + 0.45;
    const double lever = std::clamp(current_up - contact_up, -body_dimensions.half_height_m,
        body_dimensions.half_height_m);
    const double inertia = body_dimensions.mass_kg * (body_dimensions.half_height_m * body_dimensions.half_height_m
        + body_dimensions.half_length_m * body_dimensions.half_length_m) / 3.0;
    if (!downed_) {
        downed_ = true;
        heading_rad_ = std::atan2(direction.east_m, direction.north_m);
        // A force BELOW the centre of mass drives the legs ahead of the torso:
        // positive pitch leans the head BACK toward the striking vehicle.
        // A chest-height strike has the opposite torque; neither invents lift.
        fall_direction_ = lever >= 0.0 ? 1.0 : -1.0;
        pitch_rad_ = fall_direction_ * 0.02;
        pitch_rate_rad_s_ = 0.0;
        center_up_m_ = std::max(current_up, ground_up + support().support_height_m);
    }
    const double applied_impulse = was_downed && !new_event ? impulse - previous_impulse : impulse;
    const double along_body = (direction.east_m * std::sin(heading_rad_)
        + direction.north_m * std::cos(heading_rad_)) / length;
    const double angular_impulse = applied_impulse * lever * along_body / inertia;
    pitch_rate_rad_s_ = std::clamp(pitch_rate_rad_s_ + angular_impulse,
        -maximum_pitch_rate_rad_s, maximum_pitch_rate_rad_s);
    // Horizontal impulses contribute horizontal momentum in the finite-body
    // solver. Vertical motion can arise only from gravity or surface contact.
    airborne_ = vertical_velocity_mps_ > 0.0
        || center_up_m_ > ground_up + support().support_height_m + 0.002;
}

void PedestrianImpactState::tick(double dt, double ground_up, bool recovering, bool enabled,
    CollisionVector2 center, std::span<const PedestrianVehicleSurface> vehicles) noexcept
{
    if (!enabled || !std::isfinite(dt) || dt <= 0.0 || dt > 1.0
        || !std::isfinite(ground_up)) return;
    // Age even while standing: a held bumper must not become a delayed launch
    // merely because its episode's reported pressure grows several seconds later.
    if (event_ != 0) event_age_seconds_ += dt;
    if (!downed_) return;
    const int steps = std::max(1, static_cast<int>(std::ceil(dt * 240.0)));
    const double step = dt / steps;
    for (int index = 0; index < steps; ++index) {
        const bool was_vehicle_supported = !supported_vehicle_id_.empty();
        supported_vehicle_id_.clear();
        if (recovering && !airborne_ && !was_vehicle_supported) {
            const double previous_pitch = pitch_rad_;
            pitch_rad_ -= std::copysign(std::min(std::abs(pitch_rad_), 1.5 * step), pitch_rad_);
            pitch_rate_rad_s_ = (pitch_rad_ - previous_pitch) / step;
            center_up_m_ = ground_up + support().support_height_m;
            vertical_velocity_mps_ = 0.0;
            if (pitch_rad_ == 0.0) {
                downed_ = false;
                pitch_rate_rad_s_ = 0.0;
                center_up_m_ = ground_up + 0.9;
                break;
            }
            continue;
        }
        const double previous_pitch = pitch_rad_;
        const double previous_up = center_up_m_;
        if (std::abs(pitch_rad_) < half_pi) {
            pitch_rate_rad_s_ = std::clamp(
                pitch_rate_rad_s_ + fall_direction_ * 6.0 * std::cos(pitch_rad_) * step,
                -maximum_pitch_rate_rad_s, maximum_pitch_rate_rad_s);
            pitch_rad_ = std::clamp(pitch_rad_ + pitch_rate_rad_s_ * step, -half_pi, half_pi);
            if (std::abs(pitch_rad_) == half_pi) pitch_rate_rad_s_ = 0.0;
        } else if (pitch_rate_rad_s_ * pitch_rad_ < 0.0) {
            // A separately solved re-impact may rock an already fallen body.
            pitch_rad_ += pitch_rate_rad_s_ * step;
            fall_direction_ = std::copysign(1.0, pitch_rad_);
        } else {
            pitch_rate_rad_s_ = 0.0;
        }
        center_up_m_ += vertical_velocity_mps_ * step - 0.5 * gravity * step * step;
        vertical_velocity_mps_ -= gravity * step;
        const double floor = ground_up + support().support_height_m;
        if (center_up_m_ <= floor) {
            center_up_m_ = floor;
            vertical_velocity_mps_ = 0.0;
        }
        // Sample the rotated body shell against actual overlapping vehicle
        // decks. Only points arriving from ABOVE may be supported: a person
        // already lying under a car must never teleport onto its roof.
        double vehicle_floor = -std::numeric_limits<double>::infinity();
        std::string vehicle_id;
        for (const auto& vehicle : vehicles) {
            if (vehicle.vehicle_id.empty()) continue;
            for (const double x : {-body_dimensions.half_length_m, body_dimensions.half_length_m}) {
                for (const double z : {-body_dimensions.half_height_m, 0.0, body_dimensions.half_height_m}) {
                    const double horizontal = x * std::cos(pitch_rad_) - z * std::sin(pitch_rad_);
                    const double up = x * std::sin(pitch_rad_) + z * std::cos(pitch_rad_);
                    const CollisionVector2 sample{center.east_m + horizontal * std::sin(heading_rad_),
                        center.north_m + horizontal * std::cos(heading_rad_)};
                    const double surface = sedan_surface_up(vehicle.shape, sample);
                    const double previous_sample_up = previous_up + x * std::sin(previous_pitch) + z * std::cos(previous_pitch);
                    if (std::isfinite(surface) && previous_sample_up >= surface - 0.10
                        && center_up_m_ + up <= surface + 0.05 && surface - up > vehicle_floor) {
                        vehicle_floor = surface - up;
                        vehicle_id = vehicle.vehicle_id;
                    }
                }
            }
        }
        if (vehicle_floor >= floor && center_up_m_ <= vehicle_floor + 0.05) {
            center_up_m_ = std::max(center_up_m_, vehicle_floor);
            vertical_velocity_mps_ = 0.0;
            supported_vehicle_id_ = std::move(vehicle_id);
        }
        airborne_ = supported_vehicle_id_.empty()
            && (center_up_m_ > floor + 0.002 || vertical_velocity_mps_ > 0.0);
    }
}

} // namespace simcore_host
