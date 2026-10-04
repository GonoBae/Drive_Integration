#include "terrain/live_npc_ground.hpp"

#include <algorithm>
#include <cmath>

namespace simcore_host {
void LiveNpcGround::clear_measurements() {
    tiles_.clear(); demands_.clear(); pending_.reset();
}
void LiveNpcGround::reset_context(std::string context) {
    if (context == context_) return;
    context_ = std::move(context);
    ++epoch_; revision_ = 0; world_ready_ = false; notice_expired_ = true;
    clear_measurements();
}
bool LiveNpcGround::can_observe_world_revision(std::uint64_t revision, bool ready,
    Clock::time_point received_at) const {
    if (!revision || revision < revision_ || context_.empty()) return false;
    if (revision == revision_ && ready != world_ready_) return false;
    if (received_at < notice_at_ && !notice_expired_) return false;
    return true;
}
bool LiveNpcGround::observe_world_revision(std::uint64_t revision, bool ready,
    Clock::time_point received_at) {
    if (!can_observe_world_revision(revision,ready,received_at)) return false;
    if (revision != revision_) {
        ++epoch_; clear_measurements(); revision_ = revision; world_ready_ = ready;
    }
    notice_at_ = received_at; notice_expired_ = false;
    return true;
}
void LiveNpcGround::begin_tick(Clock::time_point now) {
    tick_time_ = now;
    if (!notice_expired_ && (now < notice_at_ || now-notice_at_ >= Lifetime)) {
        notice_expired_ = true; ++epoch_; clear_measurements();
    }
}
bool LiveNpcGround::ready() const {
    return !context_.empty() && world_ready_ && !notice_expired_
        && tick_time_ >= notice_at_ && tick_time_-notice_at_ < Lifetime;
}
void LiveNpcGround::enqueue(Key key, bool support) const {
    if (pending_ && pending_->key == key) return;
    const auto existing = std::find_if(demands_.begin(),demands_.end(),
        [&](const auto& demand) { return demand.key == key; });
    if (existing != demands_.end()) {
        existing->support |= support;
        return;
    }
    if (demands_.size() == MaximumDemands) {
        if (!support) return;
        const auto optional = std::find_if(demands_.rbegin(),demands_.rend(),
            [](const auto& demand) { return !demand.support; });
        if (optional == demands_.rend()) return;
        demands_.erase(std::next(optional).base());
    }
    demands_.push_back({key,support});
}
bool LiveNpcGround::make_room(Key key) {
    if (tiles_.contains(key) || tiles_.size() < MaximumTiles) return true;
    auto victim = tiles_.end();
    for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
        if (it->second.pinned_until > tick_time_) continue;
        if (victim == tiles_.end() || it->second.last_use < victim->second.last_use) victim = it;
    }
    if (victim == tiles_.end()) return false;
    tiles_.erase(victim);
    return true;
}
const GroundQueryBatch* LiveNpcGround::prepare(std::uint64_t simulation_time_ns, Clock::time_point now) {
    begin_tick(now);
    if (!ready()) return nullptr;
    if (pending_ && now-pending_->sent_at >= Lifetime) {
        const auto demand = Demand{pending_->key,pending_->support};
        pending_.reset(); ++expired_; enqueue(demand.key,demand.support);
    }
    if (!pending_ && !demands_.empty()) {
        auto demand = std::find_if(demands_.begin(),demands_.end(),
            [](const auto& value) { return value.support; });
        if (demand == demands_.end()) demand = demands_.begin();
        if (!make_room(demand->key)) return nullptr;
        const auto selected = *demand; demands_.erase(demand);
        auto patch = std::make_shared<LiveGroundPatch>();
        patch->reset_context(context_+"/npc/"+std::to_string(next_request_));
        patch->observe_world_revision(revision_,true);
        const auto [x,y,z] = selected.key;
        const GroundPointEnu centre{x*8.0+4.0,y*8.0+4.0,z*8.0+4.0};
        const auto local = *patch->prepare(centre,simulation_time_ns,now);
        auto wire = local;
        wire.request_id = next_request_++; wire.environment_epoch = epoch_;
        pending_ = Pending{selected.key,std::move(patch),local,std::move(wire),selected.support,now};
    }
    return pending_ ? &pending_->wire : nullptr;
}
bool LiveNpcGround::accept(const GroundQueryResult& result, Clock::time_point now) {
    begin_tick(now);
    const auto reject = [&](std::string reason) { last_rejection_reason_ = std::move(reason); ++rejected_; return false; };
    if (!ready()) return reject("world_notice_unavailable");
    if (!pending_) return reject("no_pending_request");
    if (now < pending_->sent_at || now-pending_->sent_at >= Lifetime) return reject("reply_deadline");
    if (result.request_id != pending_->wire.request_id
        || result.environment_epoch != pending_->wire.environment_epoch
        || result.reference_simulation_time_ns != pending_->wire.reference_simulation_time_ns
        || result.world_revision != revision_) return reject("request_identity_mismatch");
    auto local_result = result;
    local_result.request_id = pending_->local.request_id;
    local_result.environment_epoch = pending_->local.environment_epoch;
    if (!pending_->patch->accept(local_result,now)) {
        const auto centre = pending_->wire.origins.at(0);
        return reject(pending_->patch->last_rejection_reason() + " centre=" + std::to_string(centre.east_m) + ","
            + std::to_string(centre.north_m) + "," + std::to_string(centre.up_m)
            + " probes=" + std::to_string(result.probes.size()) + " boundaries=" + std::to_string(result.boundaries.size()));
    }
    if (!make_room(pending_->key)) return reject("cache_pinned");
    tiles_[pending_->key] = Tile{pending_->patch,++use_counter_,
        pending_->support ? now+std::chrono::milliseconds(250) : now,
        now+std::chrono::milliseconds(100)};
    pending_.reset(); ++accepted_;
    return true;
}
[[noreturn]] void LiveNpcGround::unavailable(const char* reason) const {
    last_missing_reason_ = reason; throw GroundMeasurementUnavailable{};
}
std::optional<GroundHit> LiveNpcGround::query_down(const GroundQueryRequest& request) const {
    if (!ready()) unavailable("world_notice_unavailable");
    const auto& p = request.origin_enu;
    if (!std::isfinite(p.east_m) || !std::isfinite(p.north_m) || !std::isfinite(p.up_m)
        || std::abs(p.east_m) > 999'980 || std::abs(p.north_m) > 999'980 || std::abs(p.up_m) > 999'980
        || !std::isfinite(request.max_distance_m) || request.max_distance_m <= 0 || request.max_distance_m > 8)
        unavailable("unsupported_npc_ground_query");
    const Key key{static_cast<std::int64_t>(std::floor(p.east_m/8)),
        static_cast<std::int64_t>(std::floor(p.north_m/8)),static_cast<std::int64_t>(std::floor(p.up_m/8))};
    const bool support = planning_depth_ == 0;
    const auto found = tiles_.find(key);
    if (found != tiles_.end()) {
        found->second.last_use = ++use_counter_;
        if (support) found->second.pinned_until = tick_time_+std::chrono::milliseconds(250);
        try { return found->second.patch->query_static_revision(request,revision_); }
        catch (const GroundMeasurementUnavailable&) {
            if (tick_time_ >= found->second.next_retry) enqueue(key,support);
            unavailable("unresolved_npc_ground_cell");
        }
    }
    enqueue(key,support);
    unavailable("npc_ground_tile_pending");
}
} // namespace simcore_host
