#include "terrain/live_ground_patch.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace simcore_host {
namespace {
using Corners = std::array<const GroundProbeResult*, 4>;
double dot(GroundPointEnu a, GroundPointEnu b) {
    return a.east_m*b.east_m + a.north_m*b.north_m + a.up_m*b.up_m;
}
double height_at(const GroundProbeResult& sample, GroundPointEnu p) {
    return sample.point.up_m - ((p.east_m-sample.point.east_m)*sample.normal.east_m
        + (p.north_m-sample.point.north_m)*sample.normal.north_m)/sample.normal.up_m;
}
bool same_plane(const GroundProbeResult& a, const GroundProbeResult& b) {
    return dot(a.normal,b.normal) >= .995 && std::abs(height_at(a,b.point)-b.point.up_m) <= .025;
}
Corners cell_corners(const std::vector<GroundProbeResult>& samples, std::size_t cell) {
    const auto i = (cell/24)*25+cell%24;
    return {&samples[i], &samples[i+1], &samples[i+25], &samples[i+26]};
}
double boundary_side(const GroundCellBoundary& b, GroundPointEnu p) {
    return (p.east_m-b.point.east_m)*b.normal.east_m + (p.north_m-b.point.north_m)*b.normal.north_m;
}
// A wall measurement identifies the discontinuity; corner heights alone cannot
// locate a curb. Require both measured levels to agree with that divider.
bool valid_boundary(const GroundCellBoundary& b, const Corners& corners) {
    double low = 1e6, high = -1e6;
    for (const auto* q : corners) {
        if (q->status != GroundProbeStatus::Hit || q->normal.up_m < .995
            || dot(corners[0]->normal,q->normal) < .995) return false;
        const double level = q->point.up_m-height_at(*corners[0],q->point);
        low = std::min(low,level); high = std::max(high,level);
    }
    if (high-low <= .025 || high-low > .5
        || b.point.east_m < corners[0]->point.east_m-.001
        || b.point.east_m > corners[3]->point.east_m+.001
        || b.point.north_m < corners[0]->point.north_m-.001
        || b.point.north_m > corners[3]->point.north_m+.001
        || b.point.up_m-height_at(*corners[0],b.point) < low-.001
        || b.point.up_m-height_at(*corners[0],b.point) > high+.001) return false;
    bool low_side = false, high_side = false;
    for (const auto* q : corners) {
        const double level = q->point.up_m-height_at(*corners[0],q->point);
        const bool is_low = std::abs(level-low) <= .01;
        if (!is_low && std::abs(level-high) > .01) return false;
        const double side = boundary_side(b,q->point);
        if ((is_low && side < -.001) || (!is_low && side > .001)) return false;
        low_side |= is_low && side > .001;
        high_side |= !is_low && side < -.001;
    }
    // A curb can coincide with a grid edge: one measured height group then
    // lies on the divider and only the other group occupies the cell interior.
    // Both levels and all side tests above still apply; coincident/ambiguous
    // groups with neither side in the interior remain unsupported.
    return low_side || high_side;
}
// A continuous crease is recoverable from two measured tangent planes only
// when their intersection separates the two groups of corner measurements.
const GroundProbeResult* crease_surface(const Corners& corners, GroundPointEnu p) {
    const auto* a = corners[0];
    const GroundProbeResult* b = nullptr;
    for (const auto* q : corners) if (!same_plane(*a,*q)) { b=q; break; }
    if (!b) return nullptr;
    const double dx = -a->normal.east_m/a->normal.up_m + b->normal.east_m/b->normal.up_m;
    const double dy = -a->normal.north_m/a->normal.up_m + b->normal.north_m/b->normal.up_m;
    if (std::hypot(dx,dy) < .001) return nullptr; // parallel levels need a wall trace
    int a_sign = 0;
    bool a_interior = false, b_interior = false;
    for (const auto* q : corners) {
        const bool in_a = same_plane(*a,*q), in_b = same_plane(*b,*q);
        if (!in_a && !in_b) return nullptr;
        const double diff = height_at(*a,q->point)-height_at(*b,q->point);
        if (std::abs(diff) <= .001) continue;
        if (in_a == in_b) return nullptr;
        const int sign = diff > 0 ? 1 : -1;
        const int candidate = in_a ? sign : -sign;
        if (a_sign && a_sign != candidate) return nullptr;
        a_sign = candidate;
        a_interior |= in_a; b_interior |= in_b;
    }
    if (!a_interior && !b_interior) return nullptr;
    // The crease can lie exactly on a cell edge. The other surface then has
    // only seam samples; the single interior branch covers this entire cell.
    if (!a_interior) return b;
    if (!b_interior) return a;
    return (height_at(*a,p)-height_at(*b,p))*a_sign >= 0 ? a : b;
}
}

void LiveGroundPatch::reset_context(std::string context) {
    if (context == context_) return;
    context_ = std::move(context);
    ++epoch_;
    world_revision_ = 0; world_ready_ = false;
    pending_.reset(); installed_.reset(); previous_.reset(); next_send_ = {};
}
bool LiveGroundPatch::observe_world_revision(std::uint64_t revision, bool ready) {
    if (!revision || revision < world_revision_) return false;
    if (revision == world_revision_) return ready == world_ready_;
    if (world_revision_) ++epoch_;
    world_revision_ = revision; world_ready_ = ready;
    ++invalidations_;
    pending_.reset(); installed_.reset(); previous_.reset(); next_send_ = {};
    last_missing_reason_ = ready ? "world_changed" : "world_measurement_unavailable";
    return true;
}
GroundPointEnu LiveGroundPatch::sample_origin(const GroundQueryBatch& batch, std::size_t index) {
    auto p = batch.origins.at(0);
    p.east_m += (static_cast<int>(index % kGroundPatchSide) - kGroundPatchSide/2) * batch.patch_step_enu.east_m;
    p.north_m += (static_cast<int>(index / kGroundPatchSide) - kGroundPatchSide/2) * batch.patch_step_enu.north_m;
    return p;
}
const GroundQueryBatch* LiveGroundPatch::prepare(GroundPointEnu centre, std::uint64_t time, Clock::time_point now,
    GroundPointEnu velocity) {
    if (pending_ && now - sent_at_ >= Lifetime) { pending_.reset(); ++expired_; }
    if (!pending_ && !context_.empty() && now >= next_send_) {
        const auto bounded_speed = [](double v) { return std::isfinite(v) ? std::clamp(v,-80.0,80.0) : 0.0; };
        const double vx = bounded_speed(velocity.east_m), vy = bounded_speed(velocity.north_m);
        // Retain six metres around the current body and look ahead along actual
        // movement, including reverse and side slip. Sample count stays fixed.
        GroundPointEnu step{std::clamp((6+std::abs(vx)*.25)/12,.5,2.0),
            std::clamp((6+std::abs(vy)*.25)/12,.5,2.0),0};
        centre.east_m += std::clamp(vx*.15,-8.0,8.0);
        centre.north_m += std::clamp(vy*.15,-8.0,8.0);
        // Keep the vertical budget anchored to the current body: predicting
        // height upward would lose the bottom of the solver's 24m query rays.
        centre.up_m += 8;
        pending_ = GroundQueryBatch{next_request_++,epoch_,time,1,{centre},0,{32},step,world_revision_};
        sent_at_ = now;
        next_send_ = now + std::chrono::milliseconds(100);
    }
    return pending_ ? &*pending_ : nullptr;
}
bool LiveGroundPatch::accept(const GroundQueryResult& result, Clock::time_point now) {
    const auto reject = [&](std::string reason) { last_rejection_reason_ = std::move(reason); ++rejected_; return false; };
    if (!world_ready_ || !pending_ || now < sent_at_ || now-sent_at_ >= Lifetime
        || result.request_id != pending_->request_id
        || result.environment_epoch != pending_->environment_epoch
        || result.world_revision != world_revision_ || result.world_revision != pending_->world_revision
        || result.reference_simulation_time_ns != pending_->reference_simulation_time_ns) return reject("request_context_or_deadline");
    if (!GroundQueryShadow::valid_result(result,kGroundPatchSamples,true)
        || result.probes.size() != kGroundPatchSamples) return reject("probe_schema");
    for (std::size_t i=0; i<result.probes.size(); ++i) {
        const auto& p = result.probes[i];
        const auto origin = sample_origin(*pending_,i);
        if (p.surface_material_id > 3) return reject("unsupported_material sample=" + std::to_string(i));
        if (p.status == GroundProbeStatus::Hit
            && (std::abs(p.point.east_m-origin.east_m) > .001
                || std::abs(p.point.north_m-origin.north_m) > .001
                || p.point.up_m > origin.up_m || p.point.up_m < origin.up_m-32))
            return reject("probe_outside_ray sample=" + std::to_string(i));
    }
    for (const auto& b : result.boundaries)
        if (!valid_boundary(b,cell_corners(result.probes,b.cell_index)))
            return reject("boundary_geometry cell=" + std::to_string(b.cell_index));
    previous_ = std::move(installed_);
    installed_ = MeasuredPatch{*pending_,result.probes,result.boundaries,sent_at_+Lifetime};
    pending_.reset(); ++accepted_;
    return true;
}
std::optional<GroundHit> LiveGroundPatch::query_down(const GroundQueryRequest& request) const {
    return query_impl(request,false);
}
std::optional<GroundHit> LiveGroundPatch::query_static_revision(const GroundQueryRequest& request,std::uint64_t revision) const {
    if (!world_ready_ || !revision || revision!=world_revision_) unavailable("world_revision_mismatch",request);
    return query_impl(request,true);
}
std::optional<GroundHit> LiveGroundPatch::query_impl(const GroundQueryRequest& request,bool static_snapshot) const {
    if (!std::isfinite(request.max_distance_m) || request.max_distance_m <= 0)
        unavailable("missing_or_expired",request);
    const auto& p = request.origin_enu;
    const auto covers = [&](const MeasuredPatch& patch) {
        const auto origin = sample_origin(patch.batch,0);
        const double x = (p.east_m-origin.east_m)/patch.batch.patch_step_enu.east_m;
        const double y = (p.north_m-origin.north_m)/patch.batch.patch_step_enu.north_m;
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(p.up_m)
            && x >= 0 && y >= 0 && x < 24 && y < 24
            && p.up_m <= origin.up_m && p.up_m-request.max_distance_m >= origin.up_m-32;
    };
    const MeasuredPatch* patch = nullptr;
    bool any_fresh = false;
    for (const auto* slot : {&installed_,&previous_}) {
        if (!*slot || (!static_snapshot && tick_time_ >= (*slot)->valid_until)) continue;
        any_fresh = true;
        if (covers(**slot)) { patch=&**slot; break; }
    }
    if (!patch) unavailable(any_fresh ? "outside_measured_volume" : "missing_or_expired",request);
    const auto origin = sample_origin(patch->batch,0);
    const double x = (p.east_m-origin.east_m)/patch->batch.patch_step_enu.east_m;
    const double y = (p.north_m-origin.north_m)/patch->batch.patch_step_enu.north_m;
    const auto col = static_cast<int>(x), row = static_cast<int>(y);
    const std::size_t cell = row*24+col;
    const auto corners = cell_corners(patch->samples,cell);
    // Never fall back to older terrain when the newest covering patch reports
    // a hole, failed probe or ambiguous surface. Only coverage may overlap.
    if (std::all_of(corners.begin(),corners.end(),[](auto q){return q->status == GroundProbeStatus::NoHit;}))
        return std::nullopt;
    for (const auto* q : corners)
        if (q->status != GroundProbeStatus::Hit || q->normal.up_m < .5-1e-9)
            unavailable("incomplete_or_mixed_cell",request);
    const GroundProbeResult* surface = corners[0];
    double height = 0;
    const auto boundary = std::find_if(patch->boundaries.begin(),patch->boundaries.end(),
        [cell](const auto& b){return b.cell_index == cell;});
    if (boundary != patch->boundaries.end()) {
        const bool low = boundary_side(*boundary,p) > 0;
        surface = *std::min_element(corners.begin(),corners.end(),[&](auto a,auto b){
            const double ah=a->point.up_m-height_at(*corners[0],a->point);
            const double bh=b->point.up_m-height_at(*corners[0],b->point);
            return low ? ah < bh : ah > bh;
        });
        height = height_at(*surface,p);
    } else if (std::all_of(corners.begin(),corners.end(),[&](auto q){return same_plane(*surface,*q);})) {
        const double u=x-col, v=y-row;
        height = (1-v)*((1-u)*corners[0]->point.up_m+u*corners[1]->point.up_m)
            + v*((1-u)*corners[2]->point.up_m+u*corners[3]->point.up_m);
        // Geometry can be continuous even where physical materials change.
        surface = corners[(u >= .5 ? 1 : 0)+(v >= .5 ? 2 : 0)];
    } else {
        surface = crease_surface(corners,p);
        if (!surface) unavailable("discontinuous_cell",request);
        height = height_at(*surface,p);
    }
    if (p.up_m < height) unavailable("below_measured_layer",request);
    const double depth = p.up_m-height;
    if (depth > request.max_distance_m) return std::nullopt;
    const double friction = surface->surface_material_id==2 ? .35 : surface->surface_material_id==3 ? 1.10 : 1.0;
    return GroundHit{{p.east_m,p.north_m,height},surface->normal,depth,
        static_cast<GroundSurfaceMaterialId>(surface->surface_material_id),friction};
}
}
