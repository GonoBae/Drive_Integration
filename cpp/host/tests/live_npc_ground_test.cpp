#include "terrain/live_npc_ground.hpp"
#include "traffic/npc_lane_follower.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace simcore_host;
using Clock = LiveNpcGround::Clock;
using namespace std::chrono_literals;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void unavailable(F&& action) {
    bool caught = false;
    try { action(); } catch (const GroundMeasurementUnavailable&) { caught = true; }
    require(caught,"missing NPC measurement must remain distinct from NoHit");
}
GroundQueryRequest query(double east = 1, double north = 1) { return {{east,north,2},4}; }
GroundQueryResult response(const GroundQueryBatch& batch, GroundProbeStatus status = GroundProbeStatus::Hit) {
    GroundQueryResult result;
    result.request_id = batch.request_id; result.environment_epoch = batch.environment_epoch;
    result.reference_simulation_time_ns = batch.reference_simulation_time_ns;
    result.world_revision = batch.world_revision;
    for (int i = 0; i < kGroundPatchSamples; ++i) {
        auto point = LiveGroundPatch::sample_origin(batch,i); point.up_m = 0;
        result.probes.push_back(status == GroundProbeStatus::Hit
            ? GroundProbeResult{status,point,{0,0,1},1} : GroundProbeResult{status,{},{},0});
    }
    return result;
}
void ready(LiveNpcGround& ground, Clock::time_point now) {
    ground.reset_context("play-1");
    require(ground.observe_world_revision(1,true,now),"first world notice");
    ground.begin_tick(now);
}
void lifetime_and_revision() {
    LiveNpcGround ground; const auto now = Clock::now();
    unavailable([&] { ground.query_down(query()); });
    ready(ground,now);
    unavailable([&] { ground.query_down(query()); });
    require(ground.pending_demand_count() == 1,"query queues one demand");
    const auto batch = *ground.prepare(11,now);
    require(batch.origins.size() == 1 && batch.patch_step_enu.east_m == .5
        && batch.patch_step_enu.north_m == .5 && batch.query_distances_m == std::vector<double>{32},
        "NPC request keeps bounded measured patch contract");
    require(ground.prepare(12,now+1ms)->request_id == batch.request_id,"single pending request remains stable");
    auto wrong = response(batch); ++wrong.world_revision;
    require(!ground.accept(wrong,now+1ms),"wrong revision rejected");
    wrong = response(batch); ++wrong.environment_epoch;
    require(!ground.accept(wrong,now+1ms),"wrong epoch rejected");
    wrong = response(batch); ++wrong.reference_simulation_time_ns;
    require(!ground.accept(wrong,now+1ms),"wrong reference tick rejected");
    require(ground.accept(response(batch),now+2ms),"matching measurement accepted");
    require(!ground.accept(response(batch),now+3ms),"duplicate result rejected");
    require(ground.can_observe_world_revision(2,false,now+4ms)
        && !ground.can_observe_world_revision(2,true,now-1ms),"world notice preflight validates all fields");
    require(ground.world_revision() == 1 && ground.tile_count() == 1,
        "successful and rejected preflight both preserve installed measurements");
    require(!ground.observe_world_revision(2,true,now-1ms),"out-of-order world notice rejected");
    require(ground.world_revision() == 1 && ground.tile_count() == 1,
        "rejected world notice cannot partially invalidate the installed cache");
    for (int step = 1; step <= 10; ++step) {
        require(ground.observe_world_revision(1,true,now+step*100ms),"same revision heartbeat renews world lease");
        ground.begin_tick(now+step*100ms);
        require(ground.query_down(query()).has_value(),"static geometry survives scan TTL only with fresh world lease");
    }
    ground.begin_tick(now+1350ms);
    require(ground.tile_count() == 0 && !ground.ready(),"world lease expiry clears cached measurements");
    unavailable([&] { ground.query_down(query()); });
    require(ground.observe_world_revision(1,true,now+1351ms),"same revision can resume after renewed notice");
    ground.begin_tick(now+1351ms);
    unavailable([&] { ground.query_down(query()); });
    const auto renewed = *ground.prepare(20,now+1351ms);
    require(renewed.environment_epoch != batch.environment_epoch,"notice gap fences old responses");
    require(!ground.accept(response(batch),now+1352ms),"old response cannot refill renewed cache");
    require(ground.accept(response(renewed),now+1353ms),"fresh rescan accepted");
    require(ground.observe_world_revision(2,false,now+1354ms),"world edit invalidates all tiles");
    require(!ground.observe_world_revision(2,true,now+1355ms),"same revision cannot change readiness");
    require(!ground.observe_world_revision(1,true,now+1355ms),"old world notice rejected");
    require(!ground.prepare(21,now+1355ms),"unready world does not dispatch work");
    require(ground.observe_world_revision(3,true,now+1356ms),"new ready world");
    ground.begin_tick(now+1356ms);
    unavailable([&] { ground.query_down(query()); });
    const auto changed = *ground.prepare(22,now+1356ms);
    require(changed.world_revision == 3 && changed.request_id > renewed.request_id,"world and request identities progress");
    ground.reset_context("play-2");
    require(!ground.accept(response(changed),now+1357ms),"prior-play scan rejected");
}
void queue_and_late_reply() {
    LiveNpcGround ground; const auto now = Clock::now(); ready(ground,now);
    {
        auto planning = ground.planning_scope();
        for (int i = 0; i < 8; ++i) unavailable([&] { ground.query_down(query(1+i*8)); });
    }
    require(ground.pending_demand_count() == 4,"optional query backlog is bounded");
    unavailable([&] { ground.query_down(query(81)); });
    const auto request = *ground.prepare(1,now);
    require(request.origins[0].east_m == 84,"current support preempts queued optional work");
    require(ground.observe_world_revision(1,true,now+300ms),"lease kept fresh during delayed scan");
    require(!ground.accept(response(request),now+350ms),"late scan is rejected despite fresh world notice");
    const auto retry = *ground.prepare(2,now+350ms);
    require(retry.request_id > request.request_id && ground.expired() == 1,"expired scan retried with fresh ID");
    require(ground.accept(response(retry),now+351ms),"retry accepted");
    require(ground.query_down(query(81)).has_value(),"measured support ready");
    const auto optional = *ground.prepare(3,now+352ms);
    require(optional.origins[0].east_m == 4,"oldest optional demand resumes deterministically");
}
void bounded_cache_and_pinning() {
    LiveNpcGround ground; const auto now = Clock::now(); ready(ground,now);
    for (int i = 0; i < 128; ++i) {
        const auto time = now+std::chrono::milliseconds(i);
        ground.observe_world_revision(1,true,time); ground.begin_tick(time);
        unavailable([&] { ground.query_down(query(1+i*8)); });
        const auto batch = *ground.prepare(i,time);
        require(ground.accept(response(batch),time),"populate one static tile");
    }
    require(ground.tile_count() == LiveNpcGround::MaximumTiles,"installed cache bound");
    ground.begin_tick(now+128ms);
    unavailable([&] { ground.query_down(query(1+128*8)); });
    require(!ground.prepare(129,now+128ms),"all pinned support is not evicted for new work");
    ground.observe_world_revision(1,true,now+300ms); ground.begin_tick(now+300ms);
    require(ground.query_down(query()).has_value(),"current support refreshes its pin");
    const auto new_tile = *ground.prepare(130,now+300ms);
    require(ground.accept(response(new_tile),now+301ms),"unpinned LRU makes room");
    require(ground.tile_count() == 128,"cache never exceeds hard cap");
    require(ground.query_down(query()).has_value(),"pinned current tile survived eviction");
    unavailable([&] { ground.query_down(query(9)); });
    require(ground.pending_demand_count() == 1,"oldest unpinned tile was evicted");
}
void measured_misses() {
    const auto now = Clock::now();
    for (const auto status : {GroundProbeStatus::NoHit,GroundProbeStatus::Failed}) {
        LiveNpcGround ground; ready(ground,now);
        unavailable([&] { ground.query_down(query()); });
        const auto batch = *ground.prepare(1,now);
        require(ground.accept(response(batch,status),now+1ms),"complete probe batch accepted");
        if (status == GroundProbeStatus::NoHit)
            require(!ground.query_down(query()),"actual ray miss remains NoHit");
        else unavailable([&] { ground.query_down(query()); });
    }
}
void curb_on_grid_edge() {
    const auto now = Clock::now();
    for (const bool mirror : {false,true}) {
        LiveGroundPatch patch;
        patch.reset_context("grid-edge"); patch.observe_world_revision(1,true);
        // Actual city fixture: tile centre (-12,28), curb north=30 and
        // adjoining pavement 24 cm higher. Upper corners fall on the wall.
        const auto batch = *patch.prepare({-12,28,4},0,now);
        auto data = response(batch);
        for (auto& sample : data.probes)
            sample.point.up_m = (mirror ? sample.point.north_m >= 30.5 : sample.point.north_m <= 30) ? .24 : 0;
        data.boundaries.push_back({384,{-18,mirror ? 30.5-0.0000012 : 30.0+0.0000012,.12},
            {0,mirror ? -1.0 : 1.0,0}});
        auto bad = data; bad.boundaries[0].normal.north_m *= -1;
        require(!patch.accept(bad,now),"edge-aligned wall still rejects incorrect low-side direction");
        bad = data; bad.boundaries[0].point.up_m = .4;
        require(!patch.accept(bad,now),"edge-aligned wall still rejects heights outside measured levels");
        bad = data;
        for (const auto index : {400,401,425,426})
            bad.probes[index].point.north_m = bad.boundaries[0].point.north_m;
        require(!patch.accept(bad,now),"both height groups collapsed onto a divider are not a measured cell");
        require(patch.accept(data,now),"measured grid-edge curb must not invalidate its entire NPC tile");
        patch.begin_tick(now);
        const auto hit = patch.query_down({{-17.75,30.25,2},4});
        require(hit && std::abs(hit->point_enu.up_m) < 1e-8,
            "grid-edge curb selects the measured low plane without inventing a ramp");
    }
}
class SwitchableGround final : public GroundQuery {
public:
    bool measured = false;
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override {
        if (!measured) throw GroundMeasurementUnavailable{};
        return FlatGroundQuery{}.query_down(request);
    }
};
void follower_waits_without_losing_route() {
    TrafficNetwork network;
    TrafficLane lane; lane.id = 10; lane.width_m = 4; lane.speed_limit_mps = 6;
    lane.terminal = true; lane.points = {{0,0,0},{50,0,0}};
    network.lanes.push_back(lane);
    SwitchableGround ground; NpcLaneFollower follower;
    follower.rebuild(network,ground,{10},false,10);
    require(follower.route_length_m() == 50 && !follower.state().valid
        && follower.state().stop_reason == NpcLaneStopReason::AwaitingGround,
        "unmeasured initial anchor preserves route and requested station");
    unavailable([&] { (void)follower.sample_ahead(0); });
    ground.measured = true;
    require(follower.sample_ahead(0)->position_enu.east_m == 10,"initial station not reset to route start");
    follower.step(1.0/60,{},true);
    require(follower.state().valid && follower.state().position_enu.east_m >= 10,"measured route resumes");
    const auto before = follower;
    ground.measured = false;
    unavailable([&] { follower.step(1.0/60,{},true); });
    follower = before; // The host's complete NPC transaction restores controller + modules together.
    ground.measured = true;
    require(follower.sample_ahead(0)->position_enu.east_m == before.state().position_enu.east_m,
        "controller checkpoint preserves accepted progress through measurement outage");
}
}
int main() {
    try {
        lifetime_and_revision(); queue_and_late_reply(); bounded_cache_and_pinning();
        measured_misses(); curb_on_grid_edge(); follower_waits_without_losing_route();
        std::cout << "live NPC ground tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
