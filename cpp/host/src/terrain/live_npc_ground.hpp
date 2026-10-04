#pragma once

#include "terrain/live_ground_patch.hpp"

#include <deque>
#include <map>
#include <tuple>
#include <utility>

namespace simcore_host {
inline constexpr std::string_view kLiveNpcGroundCapability = "npc-ground.v1";

// Shared static-world measurements for traffic. Transport replies still have
// a 350 ms deadline; installed geometry is leased by fresh world notices and
// never survives a world revision, session change, or notice timeout.
class LiveNpcGround final : public GroundQuery {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto Lifetime = std::chrono::milliseconds(350);
    static constexpr std::size_t MaximumTiles = 128;
    static constexpr std::size_t MaximumDemands = 4;

    class PlanningScope {
    public:
        PlanningScope() = default;
        explicit PlanningScope(const LiveNpcGround* owner) : owner_(owner) {
            if (owner_) ++owner_->planning_depth_;
        }
        PlanningScope(const PlanningScope&) = delete;
        PlanningScope& operator=(const PlanningScope&) = delete;
        PlanningScope(PlanningScope&& other) noexcept : owner_(std::exchange(other.owner_,nullptr)) {}
        ~PlanningScope() { if (owner_) --owner_->planning_depth_; }
    private:
        const LiveNpcGround* owner_ = nullptr;
    };

    void reset_context(std::string context);
    bool can_observe_world_revision(std::uint64_t revision, bool ready, Clock::time_point received_at) const;
    bool observe_world_revision(std::uint64_t revision, bool ready, Clock::time_point received_at);
    void begin_tick(Clock::time_point now);
    const GroundQueryBatch* prepare(std::uint64_t simulation_time_ns, Clock::time_point now);
    bool accept(const GroundQueryResult& result, Clock::time_point now);
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override;
    PlanningScope planning_scope() const { return PlanningScope(this); }
    bool ready() const;

    std::uint64_t accepted() const { return accepted_; }
    std::uint64_t rejected() const { return rejected_; }
    std::uint64_t expired() const { return expired_; }
    std::uint64_t world_revision() const { return revision_; }
    std::uint64_t environment_epoch() const { return epoch_; }
    std::size_t tile_count() const { return tiles_.size(); }
    std::size_t pending_demand_count() const { return demands_.size(); }
    const char* last_missing_reason() const { return last_missing_reason_; }
    const std::string& last_rejection_reason() const { return last_rejection_reason_; }

private:
    using Key = std::tuple<std::int64_t,std::int64_t,std::int64_t>;
    struct Tile {
        std::shared_ptr<LiveGroundPatch> patch;
        std::uint64_t last_use = 0;
        Clock::time_point pinned_until{};
        Clock::time_point next_retry{};
    };
    struct Demand { Key key; bool support; };
    struct Pending {
        Key key;
        std::shared_ptr<LiveGroundPatch> patch;
        GroundQueryBatch local, wire;
        bool support;
        Clock::time_point sent_at;
    };
    void clear_measurements();
    void enqueue(Key key, bool support) const;
    [[noreturn]] void unavailable(const char* reason) const;
    bool make_room(Key key);

    std::string context_;
    std::uint64_t epoch_ = 0, revision_ = 0, next_request_ = 1;
    std::uint64_t accepted_ = 0, rejected_ = 0, expired_ = 0;
    bool world_ready_ = false, notice_expired_ = true;
    Clock::time_point tick_time_{}, notice_at_{};
    mutable std::map<Key,Tile> tiles_;
    mutable std::deque<Demand> demands_;
    std::optional<Pending> pending_;
    mutable std::uint64_t use_counter_ = 0;
    mutable std::size_t planning_depth_ = 0;
    mutable const char* last_missing_reason_ = "initial_measurement";
    std::string last_rejection_reason_ = "none";
};
} // namespace simcore_host
