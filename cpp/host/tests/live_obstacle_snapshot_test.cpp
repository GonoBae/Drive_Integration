#include "terrain/live_obstacle_snapshot.hpp"
#include "terrain/live_ground_patch.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace simcore_host;
using Clock = LiveObstacleSnapshot::Clock;
using namespace std::chrono_literals;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void unavailable(F&& action) {
    bool caught = false;
    try { action(); } catch (const GroundMeasurementUnavailable&) { caught = true; }
    require(caught,"unknown obstacle measurement must not become a clear path");
}
void ready(LiveObstacleSnapshot& cache) {
    cache.reset_context("play-session-1");
    require(cache.observe_world_revision(1,true),"initial world revision");
}
LiveObstacleResult response(const LiveObstacleRequest& request,
    std::vector<StaticObbCollider> colliders = {},
    LiveObstacleStatus status = LiveObstacleStatus::Complete) {
    return {request.request_id,request.environment_epoch,request.reference_simulation_time_ns,
        request.world_revision,status,std::move(colliders)};
}
StaticObbCollider wall(std::string id = "building-1", double north = 3) {
    return {std::move(id),StaticColliderSemantic::Wall,
        {{0,north},1,0,.25,5,1},{.8,0}};
}
ObbPrism body(double east = 0, double north = 0) {
    return {{east,north},.8,0,1,.5,.5};
}
void freshness_and_identity() {
    LiveObstacleSnapshot cache;
    const auto now = Clock::now();
    require(!cache.prepare({},0,now),"unidentified world cannot request obstacles");
    cache.reset_context("play-session-1");
    require(!cache.prepare({},0,now),"world readiness must precede requests");
    require(!cache.observe_world_revision(0,true),"zero revision rejected");
    require(cache.observe_world_revision(1,true),"revision becomes ready");
    const auto request = *cache.prepare({},11,now);
    require(request.request_id && request.environment_epoch,"nonzero request identity");
    require(cache.prepare({100,100,0},22,now+50ms)->request_id == request.request_id,
        "one pending request is stable despite player movement");
    auto result = response(request,{wall()});
    auto bad = result; ++bad.request_id;
    require(!cache.accept(bad,now+1ms),"wrong request rejected");
    bad = result; ++bad.environment_epoch;
    require(!cache.accept(bad,now+1ms),"wrong environment rejected");
    bad = result; ++bad.reference_simulation_time_ns;
    require(!cache.accept(bad,now+1ms),"wrong reference tick rejected");
    bad = result; ++bad.world_revision;
    require(!cache.accept(bad,now+1ms),"wrong world revision rejected");
    require(!cache.accept(result,now-1ms),"negative response age rejected");
    require(cache.accept(result,now+349ms),"bounded delayed response accepted");
    require(!cache.accept(result,now+349ms),"duplicate completion rejected");
    require(cache.world_for_swept_body(body(),{},0,now+349ms)->static_collider_count() == 1,
        "installed world is available before request-based expiry");
    unavailable([&] { cache.world_for_swept_body(body(),{},0,now+350ms); });
    unavailable([&] { cache.world_for_swept_body(body(),{},0,now-1ms); });
    const auto retry = *cache.prepare({},12,now+350ms);
    require(retry.request_id > request.request_id,"new request identity increases");
    require(!cache.accept(response(retry),now+700ms),"response at expiry rejected");
    const auto after_timeout = *cache.prepare({},13,now+700ms);
    require(after_timeout.request_id > retry.request_id && cache.expired() == 1,
        "timed out request is replaced and counted once");
    require(cache.accept(response(after_timeout),now+701ms),"empty measured region accepted");
    require(cache.world_for_swept_body(body(),{},0,now+701ms)->static_collider_count() == 0,
        "complete empty result is different from unknown");
    require(!cache.prepare({},14,now+799ms),"minimum request spacing maintained");
    require(cache.prepare({},14,now+800ms),"request interval opens at boundary");
}
void validation() {
    const auto now = Clock::now();
    LiveObstacleSnapshot cache; ready(cache);
    const auto request = *cache.prepare({},1,now);
    const auto rejected = [&](LiveObstacleResult result) {
        require(!cache.accept(result,now+1ms),"invalid payload rejected");
    };
    auto invalid = wall(); invalid.collider_id.clear(); rejected(response(request,{invalid}));
    invalid = wall("bad/id"); rejected(response(request,{invalid}));
    invalid = wall(std::string(129,'x')); rejected(response(request,{invalid}));
    rejected(response(request,{wall(),wall()}));
    invalid = wall(); invalid.shape.center_up_m = std::numeric_limits<double>::quiet_NaN();
    rejected(response(request,{invalid}));
    invalid = wall(); invalid.shape.half_length_m = 0; rejected(response(request,{invalid}));
    invalid = wall(); invalid.shape.center_enu.east_m = 1'000'001; rejected(response(request,{invalid}));
    invalid = wall(); invalid.material.restitution = 1.01; rejected(response(request,{invalid}));
    invalid = wall(); invalid.material.friction = -1; rejected(response(request,{invalid}));
    invalid = wall(); invalid.semantic = static_cast<StaticColliderSemantic>(255);
    rejected(response(request,{invalid}));
    rejected(response(request,{wall("far",50)}));
    rejected(response(request,{wall()},LiveObstacleStatus::Unsupported));
    rejected(response(request,{},static_cast<LiveObstacleStatus>(255)));
    std::vector<StaticObbCollider> colliders;
    for (std::size_t i = 0; i <= kMaximumLiveObstacleColliders; ++i)
        colliders.push_back(wall("wall-"+std::to_string(i)));
    rejected(response(request,colliders));
    colliders.pop_back();
    // A wall can extend outside the measurement volume. Rejecting that wall
    // would omit the part the car can actually reach inside the volume.
    colliders.front().shape.half_width_m = 100;
    require(cache.accept(response(request,colliders),now+2ms),"bounded count and overlapping large wall accepted");
    require(cache.world_for_swept_body(body(),{},0,now+2ms)->static_collider_count() == 128,
        "all colliders are retained, not truncated");
}
void revision_fences() {
    const auto now = Clock::now();
    LiveObstacleSnapshot cache; ready(cache);
    const auto first = *cache.prepare({},1,now);
    require(cache.accept(response(first,{wall()}),now+1ms),"first world installed");
    const auto outstanding = *cache.prepare({},2,now+100ms);
    require(cache.observe_world_revision(2,false),"unready newer world invalidates");
    require(!cache.accept(response(outstanding),now+101ms),"old in-flight world rejected");
    unavailable([&] { cache.world_for_swept_body(body(),{},0,now+101ms); });
    require(!cache.prepare({},3,now+102ms),"unready revision does not start new work");
    require(!cache.observe_world_revision(1,true),"older revision cannot return");
    require(!cache.observe_world_revision(2,true),"same revision cannot change readiness");
    require(cache.observe_world_revision(3,true),"new ready revision accepted");
    const auto fresh = *cache.prepare({},3,now+103ms);
    require(fresh.environment_epoch > outstanding.environment_epoch
        && fresh.world_revision == 3,"new request carries revision fence");
    require(cache.accept(response(fresh),now+104ms),"new world accepted");
    cache.reset_context("play-session-1");
    require(cache.world_for_swept_body(body(),{},0,now+105ms) != nullptr,"same context preserves cache");
    cache.reset_context("play-session-2");
    unavailable([&] { cache.world_for_swept_body(body(),{},0,now+106ms); });
    require(cache.observe_world_revision(1,true),"fresh play permits reset revision");
    const auto restarted = *cache.prepare({},4,now+107ms);
    require(restarted.request_id > fresh.request_id && restarted.environment_epoch > fresh.environment_epoch,
        "play restart does not recycle request identities");
    require(!cache.accept(response(fresh),now+108ms),"prior-play result rejected");
}
void coverage_and_failed_observations() {
    const auto now = Clock::now();
    LiveObstacleSnapshot cache; ready(cache);
    auto request = *cache.prepare({},1,now);
    require(cache.accept(response(request,{wall()}),now+1ms),"initial snapshot");
    unavailable([&] { cache.world_for_swept_body(body(6,0),{},0,now+1ms); });
    unavailable([&] { cache.world_for_swept_body(body(4,0),{20,0,0},.1,now+1ms); });
    unavailable([&] { cache.world_for_swept_body(body(5,0),{},.1,now+1ms,20); });
    auto high = body(); high.center_up_m = 7.6;
    unavailable([&] { cache.world_for_swept_body(high,{},0,now+1ms); });
    unavailable([&] { cache.world_for_swept_body(body(),{0,0,100},.1,now+1ms); });
    unavailable([&] { cache.world_for_swept_body(body(),{},.11,now+1ms); });
    unavailable([&] { cache.world_for_swept_body(body(),{},-.01,now+1ms); });
    unavailable([&] { cache.world_for_swept_body(body(),{NAN,0,0},0,now+1ms); });
    request = *cache.prepare({8,0,0},2,now+100ms);
    require(cache.accept(response(request),now+101ms),"adjacent region installed");
    require(cache.world_for_swept_body(body(),{},0,now+102ms)->static_collider_count() == 1,
        "prior unexpired region serves body outside new coverage");
    require(cache.world_for_swept_body(body(8),{},0,now+102ms)->static_collider_count() == 0,
        "new complete empty region supersedes old world");
    request = *cache.prepare({8,0,0},3,now+200ms);
    require(cache.accept(response(request,{},LiveObstacleStatus::Unsupported),now+201ms),
        "unsupported result is a valid observation, not a clear region");
    unavailable([&] { cache.world_for_swept_body(body(8),{},0,now+202ms); });
    require(std::string(cache.last_missing_reason()) == "unsupported_obstacle","unsupported diagnostic");
    unavailable([&] { cache.world_for_swept_body(body(),{},0,now+202ms); });

    LiveObstacleSnapshot overlap_cache; ready(overlap_cache);
    request = *overlap_cache.prepare({},1,now);
    require(overlap_cache.accept(response(request),now+1ms),"old region measured");
    request = *overlap_cache.prepare({8,0,0},2,now+100ms);
    require(overlap_cache.accept(response(request,{},LiveObstacleStatus::Overflow),now+101ms),"overflow observation accepted");
    unavailable([&] { overlap_cache.world_for_swept_body(body(1.9),{},0,now+102ms); });
    require(std::string(overlap_cache.last_missing_reason()) == "obstacle_overflow", "partial overlap with unknown region also fenced");
    require(overlap_cache.world_for_swept_body(body(),{},0,now+102ms) != nullptr, "disjoint old measured region can still serve");
}
void predictive_coverage() {
    const auto now = Clock::now();
    for (const GroundPointEnu velocity : std::vector<GroundPointEnu>{{0,0,0},{50,0,0},
            {-50,0,0},{0,50,0},{25,-50,0},{1000,-1000,0}}) {
        LiveObstacleSnapshot cache; ready(cache);
        const auto request = *cache.prepare({},1,now,velocity);
        require(request.half_extent_enu.east_m >= 6 && request.half_extent_enu.east_m <= 24
            && request.half_extent_enu.north_m >= 6 && request.half_extent_enu.north_m <= 24
            && request.half_extent_enu.up_m == 8,"adaptive measurement volume remains bounded");
        require(std::abs(request.centre_enu.east_m) <= 8 && std::abs(request.centre_enu.north_m) <= 8,
            "signed directional lead remains bounded");
        require(cache.accept(response(request),now+1ms),"predictive region accepted");
        if (std::abs(velocity.east_m) <= 50 && std::abs(velocity.north_m) <= 50)
            require(cache.world_for_swept_body(body(),velocity,1.0/60,now+1ms) != nullptr,
                "forward reverse and lateral movement fit freshly requested coverage");
    }
}
void real_collision_and_moved_wall() {
    const auto now = Clock::now();
    LiveObstacleSnapshot cache; ready(cache);
    auto request = *cache.prepare({},1,now);
    require(cache.accept(response(request,{wall()}),now+1ms),"near wall measured");
    PlanarRigidBody car{"ego",body(0,1.5),{0,10},0,1500,2000};
    const auto near_world = cache.world_for_swept_body(car.shape,{0,10,0},.1,now+2ms);
    const auto collision = near_world->integrate(car,.1);
    require(!collision.contacts.empty() && collision.body.linear_velocity_enu_mps.north_m < 1,
        "measured box produces actual server collision response");
    require(cache.observe_world_revision(2,true),"wall movement invalidates old world");
    unavailable([&] { cache.world_for_swept_body(car.shape,{0,10,0},.1,now+3ms); });
    request = *cache.prepare({},2,now+4ms);
    require(cache.accept(response(request,{wall("building-1",5)}),now+5ms),"moved wall measured with stable ID");
    const auto moved_world = cache.world_for_swept_body(car.shape,{0,10,0},.1,now+6ms);
    const auto moved = moved_world->integrate(car,.1);
    require(moved.contacts.empty() && std::abs(moved.body.shape.center_enu.north_m-2.5) < 1e-8,
        "car passes old location after actual measured wall moves");
    require(near_world->static_colliders().front().shape.center_enu.north_m == 3,
        "installed worlds remain immutable after replacement");
}
}

int main() {
    try {
        freshness_and_identity(); validation(); revision_fences();
        coverage_and_failed_observations(); predictive_coverage(); real_collision_and_moved_wall();
        std::cout << "live obstacle snapshot tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
