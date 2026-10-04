#pragma once

#include "collision/collision_world.hpp"
#include "terrain/ground_query.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace simcore_host {

inline constexpr std::string_view kLiveObstacleCapability = "world-obstacles.v1";
inline constexpr std::size_t kMaximumLiveObstacleColliders = 128;

struct LiveObstacleRequest {
    std::uint64_t request_id = 0;
    std::uint64_t environment_epoch = 0;
    std::uint64_t reference_simulation_time_ns = 0;
    std::uint64_t world_revision = 0;
    GroundPointEnu centre_enu;
    GroundPointEnu half_extent_enu;
};

enum class LiveObstacleStatus : std::uint8_t {
    Complete = 1,
    Unsupported = 2,
    Overflow = 3,
};

struct LiveObstacleResult {
    std::uint64_t request_id = 0;
    std::uint64_t environment_epoch = 0;
    std::uint64_t reference_simulation_time_ns = 0;
    std::uint64_t world_revision = 0;
    LiveObstacleStatus status = LiveObstacleStatus::Unsupported;
    std::vector<StaticObbCollider> colliders;
};

// A Complete response describes every supported static obstacle intersecting
// the requested volume, not only objects already touching the current car.
// Dynamic traffic remains server-owned and is never included in this cache.
class LiveObstacleSnapshot {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto Lifetime = std::chrono::milliseconds(350);
    static constexpr auto RequestInterval = std::chrono::milliseconds(100);

    void reset_context(std::string context);
    bool observe_world_revision(std::uint64_t revision, bool ready);
    const LiveObstacleRequest* prepare(GroundPointEnu centre,
        std::uint64_t simulation_time_ns, Clock::time_point now,
        GroundPointEnu velocity = {});
    bool accept(const LiveObstacleResult& result, Clock::time_point now);

    // Covers translation and rotation through this step. The caller supplies
    // the swept vertical body envelope if its ground solve can change height.
    // A collision solve which projects the body beyond this volume must be
    // checked again before committing the tick.
    std::shared_ptr<const CollisionWorld> world_for_swept_body(
        const ObbPrism& body, GroundPointEnu velocity, double dt_seconds,
        Clock::time_point now, double heading_rate_rad_s = 0.0) const;

    std::uint64_t world_revision() const { return world_revision_; }
    std::uint64_t environment_epoch() const { return epoch_; }
    std::uint64_t accepted() const { return accepted_; }
    std::uint64_t rejected() const { return rejected_; }
    std::uint64_t expired() const { return expired_; }
    std::uint64_t invalidations() const { return invalidations_; }
    const char* last_missing_reason() const { return last_missing_reason_; }

private:
    struct Snapshot {
        LiveObstacleRequest request;
        LiveObstacleStatus status;
        std::shared_ptr<const CollisionWorld> world;
        Clock::time_point valid_from, valid_until;
    };
    [[noreturn]] void unavailable(const char* reason) const;

    std::string context_;
    std::uint64_t next_request_ = 1, epoch_ = 0, world_revision_ = 0;
    std::uint64_t accepted_ = 0, rejected_ = 0, expired_ = 0, invalidations_ = 0;
    bool world_ready_ = false;
    std::optional<LiveObstacleRequest> pending_;
    std::optional<Snapshot> installed_, previous_;
    Clock::time_point sent_at_{}, next_send_{};
    mutable const char* last_missing_reason_ = "initial_measurement";
};

} // namespace simcore_host
