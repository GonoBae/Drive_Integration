#include "terrain/live_obstacle_snapshot.hpp"
#include "terrain/live_ground_patch.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace simcore_host {
namespace {
constexpr double maximum_coordinate_m = 1'000'000.0;

bool finite_coordinate(double value) {
    return std::isfinite(value) && std::abs(value) <= maximum_coordinate_m;
}
bool finite_point(GroundPointEnu point) {
    return finite_coordinate(point.east_m) && finite_coordinate(point.north_m)
        && finite_coordinate(point.up_m);
}
bool valid_body(const ObbPrism& shape) {
    return finite_point({shape.center_enu.east_m, shape.center_enu.north_m, shape.center_up_m})
        && std::isfinite(shape.heading_rad)
        && finite_coordinate(shape.half_length_m) && shape.half_length_m > 0
        && finite_coordinate(shape.half_width_m) && shape.half_width_m > 0
        && finite_coordinate(shape.half_height_m) && shape.half_height_m > 0;
}
bool valid_id(const std::string& id) {
    return !id.empty() && id.size() <= 128
        && std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        });
}
GroundBoundsEnu bounds(const ObbPrism& shape) {
    const double sine = std::abs(std::sin(shape.heading_rad));
    const double cosine = std::abs(std::cos(shape.heading_rad));
    const double east = shape.half_length_m * sine + shape.half_width_m * cosine;
    const double north = shape.half_length_m * cosine + shape.half_width_m * sine;
    return {{shape.center_enu.east_m-east, shape.center_enu.north_m-north,
                shape.center_up_m-shape.half_height_m},
            {shape.center_enu.east_m+east, shape.center_enu.north_m+north,
                shape.center_up_m+shape.half_height_m}};
}
GroundBoundsEnu bounds(const LiveObstacleRequest& request) {
    const auto& c = request.centre_enu;
    const auto& h = request.half_extent_enu;
    return {{c.east_m-h.east_m,c.north_m-h.north_m,c.up_m-h.up_m},
            {c.east_m+h.east_m,c.north_m+h.north_m,c.up_m+h.up_m}};
}
bool contains(const GroundBoundsEnu& outer, const GroundBoundsEnu& inner) {
    return inner.minimum.east_m >= outer.minimum.east_m
        && inner.maximum.east_m <= outer.maximum.east_m
        && inner.minimum.north_m >= outer.minimum.north_m
        && inner.maximum.north_m <= outer.maximum.north_m
        && inner.minimum.up_m >= outer.minimum.up_m
        && inner.maximum.up_m <= outer.maximum.up_m;
}
bool overlaps(const GroundBoundsEnu& a, const GroundBoundsEnu& b) {
    return a.maximum.east_m >= b.minimum.east_m && a.minimum.east_m <= b.maximum.east_m
        && a.maximum.north_m >= b.minimum.north_m && a.minimum.north_m <= b.maximum.north_m
        && a.maximum.up_m >= b.minimum.up_m && a.minimum.up_m <= b.maximum.up_m;
}
GroundBoundsEnu swept_bounds(const ObbPrism& body, GroundPointEnu velocity,
    double dt, double yaw_rate) {
    auto result = bounds(body);
    // Rotation around the body's centre moves any corner by at most r*angle.
    // This deliberately overestimates a turn rather than missing an extremum
    // between the start and end orientations.
    const double padding = std::hypot(body.half_length_m, body.half_width_m)
        * std::min(std::abs(yaw_rate * dt), 2.0);
    result.minimum.east_m += std::min(0.0, velocity.east_m*dt)-padding;
    result.maximum.east_m += std::max(0.0, velocity.east_m*dt)+padding;
    result.minimum.north_m += std::min(0.0, velocity.north_m*dt)-padding;
    result.maximum.north_m += std::max(0.0, velocity.north_m*dt)+padding;
    result.minimum.up_m += std::min(0.0, velocity.up_m*dt);
    result.maximum.up_m += std::max(0.0, velocity.up_m*dt);
    return result;
}
}

void LiveObstacleSnapshot::reset_context(std::string context) {
    if (context == context_) return;
    context_ = std::move(context);
    ++epoch_;
    world_revision_ = 0;
    world_ready_ = false;
    pending_.reset(); installed_.reset(); previous_.reset(); next_send_ = {};
    last_missing_reason_ = "initial_measurement";
}

bool LiveObstacleSnapshot::observe_world_revision(std::uint64_t revision, bool ready) {
    if (!revision || revision < world_revision_) return false;
    if (revision == world_revision_) return ready == world_ready_;
    if (world_revision_) ++epoch_;
    world_revision_ = revision;
    world_ready_ = ready;
    ++invalidations_;
    pending_.reset(); installed_.reset(); previous_.reset(); next_send_ = {};
    last_missing_reason_ = ready ? "world_changed" : "world_measurement_unavailable";
    return true;
}

const LiveObstacleRequest* LiveObstacleSnapshot::prepare(GroundPointEnu centre,
    std::uint64_t simulation_time_ns, Clock::time_point now, GroundPointEnu velocity) {
    if (pending_ && now >= sent_at_ && now-sent_at_ >= Lifetime) {
        pending_.reset(); ++expired_;
    }
    if (!world_ready_ || context_.empty() || !finite_point(centre)) return nullptr;
    if (!pending_ && now >= next_send_) {
        const auto speed = [](double v) { return std::isfinite(v) ? std::clamp(v,-80.0,80.0) : 0.0; };
        const double east = speed(velocity.east_m), north = speed(velocity.north_m);
        const GroundPointEnu half{std::clamp(6+std::abs(east)*.25,6.0,24.0),
            std::clamp(6+std::abs(north)*.25,6.0,24.0),8.0};
        centre.east_m += std::clamp(east*.15,-8.0,8.0);
        centre.north_m += std::clamp(north*.15,-8.0,8.0);
        LiveObstacleRequest request{next_request_,epoch_,simulation_time_ns,world_revision_,centre,half};
        const auto region = bounds(request);
        if (!finite_point(region.minimum) || !finite_point(region.maximum)) return nullptr;
        pending_ = request;
        ++next_request_;
        sent_at_ = now;
        next_send_ = now + RequestInterval;
    }
    return pending_ ? &*pending_ : nullptr;
}

bool LiveObstacleSnapshot::accept(const LiveObstacleResult& result, Clock::time_point now) {
    const auto reject = [&] { ++rejected_; return false; };
    if (!world_ready_ || !pending_ || now < sent_at_ || now-sent_at_ >= Lifetime
        || result.request_id != pending_->request_id
        || result.environment_epoch != pending_->environment_epoch
        || result.reference_simulation_time_ns != pending_->reference_simulation_time_ns
        || result.world_revision != world_revision_ || result.world_revision != pending_->world_revision)
        return reject();
    if (result.status != LiveObstacleStatus::Complete
        && result.status != LiveObstacleStatus::Unsupported && result.status != LiveObstacleStatus::Overflow)
        return reject();
    if (result.colliders.size() > kMaximumLiveObstacleColliders
        || (result.status != LiveObstacleStatus::Complete && !result.colliders.empty())) return reject();
    std::shared_ptr<const CollisionWorld> world;
    if (result.status == LiveObstacleStatus::Complete) {
        const auto volume = bounds(*pending_);
        for (const auto& collider : result.colliders) {
            if (!valid_id(collider.collider_id) || !valid_body(collider.shape)
                || !finite_point(bounds(collider.shape).minimum)
                || !finite_point(bounds(collider.shape).maximum)
                || !overlaps(volume,bounds(collider.shape))) return reject();
        }
        try { world = std::make_shared<const CollisionWorld>(result.colliders); }
        catch (const std::invalid_argument&) { return reject(); }
    }
    previous_ = std::move(installed_);
    installed_ = Snapshot{*pending_,result.status,std::move(world),sent_at_,sent_at_+Lifetime};
    pending_.reset(); ++accepted_;
    return true;
}

[[noreturn]] void LiveObstacleSnapshot::unavailable(const char* reason) const {
    last_missing_reason_ = reason;
    throw GroundMeasurementUnavailable{};
}

std::shared_ptr<const CollisionWorld> LiveObstacleSnapshot::world_for_swept_body(
    const ObbPrism& body, GroundPointEnu velocity, double dt_seconds,
    Clock::time_point now, double heading_rate_rad_s) const {
    if (!world_ready_ || !valid_body(body) || !finite_point(velocity)
        || !std::isfinite(dt_seconds) || dt_seconds < 0 || dt_seconds > .1
        || !std::isfinite(heading_rate_rad_s)) unavailable("invalid_or_unready_query");
    const auto swept = swept_bounds(body,velocity,dt_seconds,heading_rate_rad_s);
    if (!finite_point(swept.minimum) || !finite_point(swept.maximum)) unavailable("invalid_query_bounds");
    for (const auto* snapshot : {&installed_,&previous_}) {
        if (!*snapshot || (*snapshot)->request.world_revision != world_revision_
            || now < (*snapshot)->valid_from || now >= (*snapshot)->valid_until) continue;
        const auto volume = bounds((*snapshot)->request);
        // An incomplete newer observation also fences an overlapping older
        // snapshot. It must never masquerade as an empty obstacle region.
        if ((*snapshot)->status != LiveObstacleStatus::Complete && overlaps(volume,swept))
            unavailable((*snapshot)->status == LiveObstacleStatus::Overflow
                ? "obstacle_overflow" : "unsupported_obstacle");
        if (!contains(volume,swept)) continue;
        if (!(*snapshot)->world) unavailable("missing_obstacle_world");
        return (*snapshot)->world;
    }
    unavailable("outside_measured_volume_or_expired");
}

} // namespace simcore_host
