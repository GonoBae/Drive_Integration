#pragma once

#include "terrain/ground_query.hpp"
#include "terrain/ground_query_capture.hpp"
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace simcore_host {
inline constexpr std::string_view kGroundQueryShadowCapability = "ground-query-shadow.v1";
inline constexpr std::string_view kGroundQueryCaptureCapability = "ground-query-shadow.v2";
inline constexpr std::size_t kMaxGroundShadowProbes = 5;
inline constexpr std::size_t kMaxCapturedGroundProbes = 64;

struct GroundQueryBatch {
    std::uint64_t request_id = 0;
    std::uint64_t environment_epoch = 0;
    std::uint64_t reference_simulation_time_ns = 0;
    std::uint32_t entity_id = 1;
    std::vector<GroundPointEnu> origins;
    double max_distance_m = 8.0;
    // v2 only: one exact query depth for each origin. Empty preserves v1.
    std::vector<double> query_distances_m;
    // ground-patch.v2 only: grid spacing in XY metres, zero Z.
    GroundPointEnu patch_step_enu{};
    std::uint64_t world_revision = 0;
};
enum class GroundProbeStatus : std::uint8_t { NoHit = 1, Hit = 2, Failed = 3 };
struct GroundProbeResult {
    GroundProbeStatus status = GroundProbeStatus::Failed;
    GroundPointEnu point;
    GroundPointEnu normal;
    std::uint32_t surface_material_id = 0;
};
struct GroundCellBoundary {
    std::uint32_t cell_index = 0; // row-major 24x24 cells
    GroundPointEnu point;
    GroundPointEnu normal; // horizontal, from high ground toward low ground
};
inline constexpr std::size_t kMaxGroundCellBoundaries = 128;
struct GroundQueryResult {
    std::uint64_t request_id = 0;
    std::uint64_t environment_epoch = 0;
    std::uint64_t reference_simulation_time_ns = 0;
    std::vector<GroundProbeResult> probes;
    double measurement_duration_ms = 0;
    std::vector<GroundCellBoundary> boundaries;
    std::uint64_t world_revision = 0;
};
struct GroundShadowStatistics {
    std::uint64_t requested = 0, accepted = 0, rejected = 0, timed_out = 0;
    std::uint64_t hits_compared = 0, presence_mismatches = 0, failed_probes = 0, material_mismatches = 0;
    double max_height_error_m = 0, max_normal_error_deg = 0;
    double last_round_trip_ms = 0, max_round_trip_ms = 0, last_measurement_ms = 0;
    std::size_t source_calls = 0, source_unique = 0, source_discarded = 0, selected_probes = 0;
    double round_trip_p95_ms = 0, measurement_p95_ms = 0;
};

// A single diagnostic request, never a physics GroundQuery implementation.
class GroundQueryShadow {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto Interval = std::chrono::milliseconds(100);
    static constexpr auto Timeout = std::chrono::milliseconds(500);
    void reset_context(std::string context);
    const GroundQueryBatch* prepare(std::uint64_t simulation_time_ns,
        std::vector<GroundPointEnu> origins, const GroundQuery& ground, Clock::time_point now);
    const GroundQueryBatch* prepare_captured(const GroundQueryFrame& frame, Clock::time_point now);
    bool accept(const GroundQueryResult& result, Clock::time_point now);
    const GroundShadowStatistics& statistics() const { return statistics_; }
    static bool valid_result(const GroundQueryResult& result, std::size_t maximum_probes = kMaxCapturedGroundProbes,
        bool allow_boundaries = false);
private:
    bool can_prepare(Clock::time_point now);
    std::string context_;
    std::uint64_t next_request_ = 1, epoch_ = 0;
    std::optional<GroundQueryBatch> pending_;
    std::vector<std::optional<GroundHit>> reference_;
    Clock::time_point sent_at_{}, next_send_{};
    GroundShadowStatistics statistics_;
    std::size_t sample_cursor_ = 0, timing_cursor_ = 0;
    std::vector<double> round_trip_samples_, measurement_samples_;
};
}
