#include "terrain/ground_query_shadow.hpp"
#include <algorithm>
#include <numbers>

namespace simcore_host {
namespace {
bool finite_point(const GroundPointEnu& p) {
    return std::isfinite(p.east_m) && std::isfinite(p.north_m) && std::isfinite(p.up_m)
        && std::abs(p.east_m) <= 1e6 && std::abs(p.north_m) <= 1e6 && std::abs(p.up_m) <= 1e6;
}
}
void GroundQueryShadow::reset_context(std::string context) {
    if (context == context_) return;
    context_ = std::move(context);
    ++epoch_;
    pending_.reset();
    reference_.clear();
    next_send_ = {};
    sample_cursor_ = 0;
}
bool GroundQueryShadow::can_prepare(Clock::time_point now) {
    if (pending_ && now - sent_at_ >= Timeout) {
        ++statistics_.timed_out;
        pending_.reset();
    }
    return !pending_ && !context_.empty() && now >= next_send_;
}
const GroundQueryBatch* GroundQueryShadow::prepare(std::uint64_t time,
    std::vector<GroundPointEnu> origins, const GroundQuery& ground, Clock::time_point now) {
    if (!can_prepare(now)) return pending_ ? &*pending_ : nullptr;
    if (origins.empty() || origins.size() > kMaxGroundShadowProbes
        || !std::all_of(origins.begin(), origins.end(), finite_point))
        throw std::invalid_argument("Invalid shadow ground probe origins");
    pending_ = GroundQueryBatch{next_request_++, epoch_, time, 1, std::move(origins), 8.0};
    reference_.clear();
    for (const auto& origin : pending_->origins)
        reference_.push_back(ground.query_down({origin, pending_->max_distance_m}));
    sent_at_ = now;
    next_send_ = now + Interval;
    ++statistics_.requested;
    return &*pending_;
}
const GroundQueryBatch* GroundQueryShadow::prepare_captured(const GroundQueryFrame& frame, Clock::time_point now) {
    if (!can_prepare(now)) return pending_ ? &*pending_ : nullptr;
    if (frame.samples.empty()) return nullptr;
    GroundQueryBatch batch{next_request_++, epoch_, frame.simulation_time_ns, 1, {}, 0, {}};
    reference_.clear();
    const auto count = std::min(frame.samples.size(), kMaxCapturedGroundProbes);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& sample = frame.samples[(sample_cursor_ + i) % frame.samples.size()];
        const auto& request = sample.request;
        if (!finite_point(request.origin_enu) || !std::isfinite(request.max_distance_m)
            || request.max_distance_m <= 0 || request.max_distance_m > 32) return nullptr;
        batch.origins.push_back(request.origin_enu);
        batch.query_distances_m.push_back(request.max_distance_m);
        reference_.push_back(sample.hit);
    }
    sample_cursor_ = (sample_cursor_ + count) % frame.samples.size();
    statistics_.source_calls = frame.calls;
    statistics_.source_unique = frame.samples.size();
    statistics_.source_discarded = frame.discarded;
    statistics_.selected_probes = count;
    pending_ = std::move(batch);
    sent_at_ = now;
    next_send_ = now + Interval;
    ++statistics_.requested;
    return &*pending_;
}
bool GroundQueryShadow::valid_result(const GroundQueryResult& result, std::size_t maximum_probes, bool allow_boundaries) {
    if (!result.request_id || !result.environment_epoch || result.probes.empty()
        || result.probes.size() > maximum_probes
        || (!allow_boundaries && !result.boundaries.empty())
        || (allow_boundaries ? result.world_revision == 0 : result.world_revision != 0)
        || result.boundaries.size() > kMaxGroundCellBoundaries
        || !std::isfinite(result.measurement_duration_ms)
        || result.measurement_duration_ms < 0 || result.measurement_duration_ms > 500) return false;
    for (const auto& probe : result.probes) {
        if (probe.status != GroundProbeStatus::Hit && probe.status != GroundProbeStatus::NoHit
            && probe.status != GroundProbeStatus::Failed) return false;
        if (!finite_point(probe.point) || !finite_point(probe.normal)) return false;
        if (probe.status == GroundProbeStatus::Hit) {
            const auto& n = probe.normal;
            const double norm = n.east_m*n.east_m + n.north_m*n.north_m + n.up_m*n.up_m;
            if (std::abs(norm - 1.0) > 0.01 || n.up_m <= 0) return false;
        } else if (probe.point.east_m != 0 || probe.point.north_m != 0 || probe.point.up_m != 0
            || probe.normal.east_m != 0 || probe.normal.north_m != 0 || probe.normal.up_m != 0
            || probe.surface_material_id != 0) return false;
    }
    for (std::size_t i = 0; i < result.boundaries.size(); ++i) {
        const auto& b = result.boundaries[i];
        const double norm = b.normal.east_m*b.normal.east_m + b.normal.north_m*b.normal.north_m;
        if (b.cell_index >= 24*24 || !finite_point(b.point) || !finite_point(b.normal)
            || std::abs(norm-1) > .001 || std::abs(b.normal.up_m) > .000001) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (result.boundaries[j].cell_index == b.cell_index) return false;
    }
    return true;
}
bool GroundQueryShadow::accept(const GroundQueryResult& result, Clock::time_point now) {
    const auto reject = [&] { ++statistics_.rejected; return false; };
    if (!pending_ || !valid_result(result) || now < sent_at_ || now - sent_at_ >= Timeout
        || result.request_id != pending_->request_id || result.environment_epoch != pending_->environment_epoch
        || result.reference_simulation_time_ns != pending_->reference_simulation_time_ns
        || result.probes.size() != pending_->origins.size()) return reject();
    for (std::size_t i = 0; i < result.probes.size(); ++i) {
        const auto& p = result.probes[i];
        const auto& origin = pending_->origins[i];
        const double depth = pending_->query_distances_m.empty()
            ? pending_->max_distance_m : pending_->query_distances_m[i];
        if (p.status == GroundProbeStatus::Hit
            && (std::abs(p.point.east_m-origin.east_m) > 0.01 || std::abs(p.point.north_m-origin.north_m) > 0.01
                || p.point.up_m > origin.up_m+0.01 || origin.up_m-p.point.up_m > depth+0.01)) return reject();
    }
    for (std::size_t i = 0; i < result.probes.size(); ++i) {
        const auto& p = result.probes[i];
        const auto& ref = reference_[i];
        if (p.status == GroundProbeStatus::Failed) { ++statistics_.failed_probes; continue; }
        if ((p.status == GroundProbeStatus::Hit) != ref.has_value()) { ++statistics_.presence_mismatches; continue; }
        if (!ref) continue;
        ++statistics_.hits_compared;
        statistics_.max_height_error_m = std::max(statistics_.max_height_error_m, std::abs(p.point.up_m-ref->point_enu.up_m));
        const auto& n = ref->normal_enu;
        const double dot = p.normal.east_m*n.east_m + p.normal.north_m*n.north_m + p.normal.up_m*n.up_m;
        statistics_.max_normal_error_deg = std::max(statistics_.max_normal_error_deg,
            std::acos(std::clamp(dot, -1.0, 1.0))*180/std::numbers::pi);
        statistics_.material_mismatches += p.surface_material_id != static_cast<std::uint32_t>(ref->surface_material_id);
    }
    ++statistics_.accepted;
    statistics_.last_round_trip_ms = std::chrono::duration<double,std::milli>(now-sent_at_).count();
    statistics_.max_round_trip_ms = std::max(statistics_.max_round_trip_ms, statistics_.last_round_trip_ms);
    statistics_.last_measurement_ms = result.measurement_duration_ms;
    if (round_trip_samples_.size() < 256) {
        round_trip_samples_.push_back(statistics_.last_round_trip_ms);
        measurement_samples_.push_back(result.measurement_duration_ms);
    } else {
        round_trip_samples_[timing_cursor_] = statistics_.last_round_trip_ms;
        measurement_samples_[timing_cursor_] = result.measurement_duration_ms;
    }
    timing_cursor_ = (timing_cursor_ + 1) % 256;
    const auto percentile = [](std::vector<double> values) {
        const auto index = static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1;
        std::nth_element(values.begin(), values.begin() + index, values.end());
        return values[index];
    };
    statistics_.round_trip_p95_ms = percentile(round_trip_samples_);
    statistics_.measurement_p95_ms = percentile(measurement_samples_);
    pending_.reset();
    return true;
}
}
