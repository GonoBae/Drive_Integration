#pragma once

#include "terrain/ground_query_shadow.hpp"

namespace simcore_host {
inline constexpr std::string_view kLiveGroundCapability = "ground-patch.v3";
inline constexpr int kGroundPatchSide = 25;
inline constexpr int kGroundPatchSamples = kGroundPatchSide * kGroundPatchSide;
inline constexpr double kGroundPatchSpacingM = 0.5;

// Missing measurement is not a measured ray miss. The caller must roll back
// the entire tentative physics step, including suspension and powertrain history.
struct GroundMeasurementUnavailable {};

// Simulation-thread owned, bounded single-layer approximation. Network results
// replace a complete immutable patch; a tick cannot observe a partial scan.
class LiveGroundPatch final : public GroundQuery {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto Lifetime = std::chrono::milliseconds(350);
    void reset_context(std::string context);
    bool observe_world_revision(std::uint64_t revision, bool ready);
    std::uint64_t world_revision() const { return world_revision_; }
    std::uint64_t environment_epoch() const { return epoch_; }
    std::uint64_t invalidations() const { return invalidations_; }
    const GroundQueryBatch* prepare(GroundPointEnu centre, std::uint64_t time, Clock::time_point now,
        GroundPointEnu velocity = {});
    bool accept(const GroundQueryResult& result, Clock::time_point now);
    void begin_tick(Clock::time_point now) { tick_time_ = now; }
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override;
    // Static tile users must separately hold a fresh world-revision lease.
    // This changes retention, never the request/response deadline or geometry checks.
    std::optional<GroundHit> query_static_revision(const GroundQueryRequest& request, std::uint64_t revision) const;
    static GroundPointEnu sample_origin(const GroundQueryBatch& batch, std::size_t index);
    std::uint64_t accepted() const { return accepted_; }
    std::uint64_t rejected() const { return rejected_; }
    std::uint64_t expired() const { return expired_; }
    const char* last_missing_reason() const { return last_missing_reason_; }
    const std::string& last_rejection_reason() const { return last_rejection_reason_; }
    const GroundQueryRequest& last_missing_query() const { return last_missing_query_; }
private:
    std::optional<GroundHit> query_impl(const GroundQueryRequest& request, bool static_snapshot) const;
    [[noreturn]] void unavailable(const char* reason, const GroundQueryRequest& request) const {
        last_missing_reason_ = reason; last_missing_query_ = request;
        throw GroundMeasurementUnavailable{};
    }
    mutable const char* last_missing_reason_ = "initial_measurement";
    std::string last_rejection_reason_ = "none";
    mutable GroundQueryRequest last_missing_query_{};
    std::string context_;
    std::uint64_t next_request_ = 1, epoch_ = 0, accepted_ = 0, rejected_ = 0, expired_ = 0;
    std::uint64_t world_revision_ = 0, invalidations_ = 0;
    bool world_ready_ = false;
    struct MeasuredPatch {
        GroundQueryBatch batch;
        std::vector<GroundProbeResult> samples;
        std::vector<GroundCellBoundary> boundaries;
        Clock::time_point valid_until;
    };
    std::optional<GroundQueryBatch> pending_;
    std::optional<MeasuredPatch> installed_, previous_;
    Clock::time_point sent_at_{}, next_send_{}, tick_time_{};
};
}
