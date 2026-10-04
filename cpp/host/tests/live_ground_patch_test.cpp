#include "terrain/live_ground_patch.hpp"
#include "simulation_host.hpp"
#include "runtime_options.hpp"
#include "vehicle.pb.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <limits>

namespace {
using namespace simcore_host;
using Clock = LiveGroundPatch::Clock;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void unavailable(F f) {
    bool caught=false; try { f(); } catch(const GroundMeasurementUnavailable&) { caught=true; }
    require(caught, "missing data must not become NoHit");
}
GroundQueryResult response(const GroundQueryBatch& b, double height=0, double slope=0) {
    GroundQueryResult r{b.request_id,b.environment_epoch,b.reference_simulation_time_ns,{},.5};
    r.world_revision=b.world_revision;
    for(int i=0; i<kGroundPatchSamples; ++i) {
        auto p=LiveGroundPatch::sample_origin(b,i); p.up_m=height+slope*p.north_m;
        const double n=std::sqrt(1+slope*slope);
        r.probes.push_back({GroundProbeStatus::Hit,p,{0,-slope/n,1/n},1});
    }
    return r;
}
constexpr std::uint32_t centre_cell = 12*24+12;
GroundPointEnu cell_origin(const GroundQueryBatch& batch, std::uint32_t cell=centre_cell) {
    return LiveGroundPatch::sample_origin(batch,(cell/24)*25+cell%24);
}
GroundHit hit_at(const LiveGroundPatch& patch, double east, double north, double height) {
    const auto hit=patch.query_down({{east,north,4},24});
    require(hit.has_value(),"measured surface must return a hit");
    require(std::abs(hit->point_enu.up_m-height)<1e-8,"query must preserve the measured plane height");
    return *hit;
}
GroundQueryResult curb_response(const GroundQueryBatch& batch, GroundPointEnu edge,
    GroundPointEnu high_to_low, double height=.2) {
    auto result=response(batch);
    for(auto& probe:result.probes) {
        const double side=(probe.point.east_m-edge.east_m)*high_to_low.east_m
            +(probe.point.north_m-edge.north_m)*high_to_low.north_m;
        probe.point.up_m=side<=0 ? height : 0;
    }
    edge.up_m=height/2;
    result.boundaries.push_back({centre_cell,edge,high_to_low});
    return result;
}
void adaptive_coverage() {
    const auto now=Clock::now();
    for(const GroundPointEnu velocity:std::vector<GroundPointEnu>{{0,0,0},{50,0,0},{-50,0,0},
            {0,50,0},{0,-50,0},{25,-50,0},{1000,-1000,0}}) {
        LiveGroundPatch patch; patch.reset_context("predictive"); patch.observe_world_revision(1,true);
        const GroundPointEnu centre{3,-4,.6};
        const auto batch=*patch.prepare(centre,1,now,velocity);
        const double step_x=std::clamp((6+std::abs(velocity.east_m)*.25)/12,.5,2.0);
        const double step_y=std::clamp((6+std::abs(velocity.north_m)*.25)/12,.5,2.0);
        const double lead_x=std::clamp(velocity.east_m*.15,-8.0,8.0);
        const double lead_y=std::clamp(velocity.north_m*.15,-8.0,8.0);
        require(std::abs(batch.patch_step_enu.east_m-step_x)<1e-12
            && std::abs(batch.patch_step_enu.north_m-step_y)<1e-12
            && batch.patch_step_enu.up_m==0,"spacing is bounded independently on each axis");
        require(std::abs(batch.origins[0].east_m-centre.east_m-lead_x)<1e-12
            && std::abs(batch.origins[0].north_m-centre.north_m-lead_y)<1e-12,
            "patch centre leads forward, reverse, and lateral motion");
        require(std::abs(batch.origins[0].up_m-8.6)<1e-12 && batch.query_distances_m[0]==32,
            "predictive coverage preserves vertical measurement volume");
        const auto first=LiveGroundPatch::sample_origin(batch,0);
        const auto last=LiveGroundPatch::sample_origin(batch,kGroundPatchSamples-1);
        require(std::abs(last.east_m-first.east_m-24*step_x)<1e-12
            && std::abs(last.north_m-first.north_m-24*step_y)<1e-12,
            "sample origins use transmitted axis spacing");
        require(patch.accept(response(batch),now+std::chrono::milliseconds(80)),"adaptive patch accepted");
        patch.begin_tick(now+std::chrono::milliseconds(340));
        if(std::abs(velocity.east_m)<=50 && std::abs(velocity.north_m)<=50) {
            for(const double elapsed:{0.0,.34}) for(const double east_offset:{-3.0,3.0})
                for(const double north_offset:{-2.0,2.0})
                    hit_at(patch,centre.east_m+velocity.east_m*elapsed+east_offset,
                        centre.north_m+velocity.north_m*elapsed+north_offset,0);
        }
    }
}
void curb_reconstruction() {
    const auto now=Clock::now();
    const double diagonal=std::sqrt(.5);
    for(const GroundPointEnu centre:std::vector<GroundPointEnu>{{0,0,.6},{.17,-.23,.6}})
        for(const GroundPointEnu direction:std::vector<GroundPointEnu>{{1,0,0},{-1,0,0},
                {0,1,0},{0,-1,0},{diagonal,diagonal,0},{-diagonal,-diagonal,0}}) {
            LiveGroundPatch patch; patch.reset_context("curb"); patch.observe_world_revision(1,true);
            const auto batch=*patch.prepare(centre,1,now);
            auto edge=cell_origin(batch); edge.east_m+=.21; edge.north_m+=.18;
            auto result=curb_response(batch,edge,direction);
            auto without_edge=result; without_edge.boundaries.clear();
            require(patch.accept(without_edge,now),"unresolved curb samples accepted");
            patch.begin_tick(now);
            unavailable([&]{patch.query_down({{edge.east_m,edge.north_m,4},24});});
            patch.reset_context("curb-with-edge"); patch.observe_world_revision(1,true);
            const auto resolved_batch=*patch.prepare(centre,2,now);
            result=curb_response(resolved_batch,edge,direction);
            require(patch.accept(result,now),"verified curb descriptor accepted");
            patch.begin_tick(now);
            for(const double distance:{.02,.1}) {
                const auto high=hit_at(patch,edge.east_m-direction.east_m*distance,
                    edge.north_m-direction.north_m*distance,.2);
                const auto low=hit_at(patch,edge.east_m+direction.east_m*distance,
                    edge.north_m+direction.north_m*distance,0);
                require(high.normal_enu.up_m==1 && low.normal_enu.up_m==1,"curb surfaces cannot become an invented ramp");
            }
            hit_at(patch,edge.east_m,edge.north_m,.2);
        }
    LiveGroundPatch graded; graded.reset_context("graded-curb"); graded.observe_world_revision(1,true);
    const auto batch=*graded.prepare({-8,-8,.6},1,now,{1000,1000,0});
    require(batch.patch_step_enu.east_m==2 && batch.patch_step_enu.north_m==2,
        "graded curb regression uses the widest supported grid");
    auto result=response(batch,0,.02);
    for(auto& probe:result.probes) if(probe.point.east_m>=.8) probe.point.up_m+=.2;
    result.boundaries.push_back({centre_cell,{.8,.7,.02*.7+.1},{-1,0,0}});
    require(graded.accept(result,now),"two percent graded curb groups follow their common tangent plane");
    graded.begin_tick(now);
    hit_at(graded,.79,.7,.02*.7); hit_at(graded,.81,.7,.02*.7+.2);
    hit_at(graded,.8,.7,.02*.7+.2);
}
void boundary_validation() {
    LiveGroundPatch patch; patch.reset_context("boundary-validation"); patch.observe_world_revision(1,true); const auto now=Clock::now();
    const auto batch=*patch.prepare({0,0,.6},1,now);
    auto edge=cell_origin(batch); edge.east_m+=.25; edge.north_m+=.25;
    const auto valid=curb_response(batch,edge,{1,0,0});
    const auto rejected=[&](const GroundQueryResult& result) {
        require(!patch.accept(result,now),"forged or unbounded curb descriptors must be rejected");
    };
    auto bad=valid; bad.boundaries[0].normal.east_m=-1; rejected(bad);
    bad=valid; bad.boundaries[0].normal={0,1,0}; rejected(bad);
    bad=valid; bad.boundaries[0].normal={2,0,0}; rejected(bad);
    bad=valid; bad.boundaries[0].normal={std::sqrt(.75),0,.5}; rejected(bad);
    bad=valid; bad.boundaries[0].point.east_m=.75; rejected(bad);
    bad=valid; bad.boundaries[0].point.up_m=.3; rejected(bad);
    bad=valid; bad.boundaries[0].point.up_m=-.1; rejected(bad);
    bad=valid; bad.boundaries[0].point.north_m=std::numeric_limits<double>::quiet_NaN(); rejected(bad);
    bad=valid; bad.boundaries[0].cell_index=24*24; rejected(bad);
    bad=valid; bad.boundaries.push_back(bad.boundaries[0]); rejected(bad);
    bad=valid; for(auto& probe:bad.probes) probe.point.up_m=0; rejected(bad);
    bad=curb_response(batch,edge,{1,0,0},.6); rejected(bad);
    bad=valid; bad.probes[12*25+12]={GroundProbeStatus::NoHit,{},{},0}; rejected(bad);
    bad=valid; bad.probes[12*25+12].point.up_m=.1; rejected(bad);
    require(patch.accept(valid,now),"rejected descriptors do not consume the pending measurement");

    patch.reset_context("boundary-limit"); patch.observe_world_revision(1,true); const auto many_batch=*patch.prepare({0,0,.6},2,now);
    auto many=response(many_batch);
    for(int i=0;i<kGroundPatchSamples;++i) many.probes[i].point.up_m=(i%25)%2==0 ? .2 : 0;
    for(std::uint32_t cell=0;cell<kMaxGroundCellBoundaries;++cell) {
        auto point=cell_origin(many_batch,cell); point.east_m+=.25; point.north_m+=.25; point.up_m=.1;
        many.boundaries.push_back({cell,point,{cell%24%2==0 ? 1.0 : -1.0,0,0}});
    }
    bad=many;
    auto extra=cell_origin(many_batch,kMaxGroundCellBoundaries);
    extra.east_m+=.25; extra.north_m+=.25; extra.up_m=.1;
    bad.boundaries.push_back({static_cast<std::uint32_t>(kMaxGroundCellBoundaries),extra,{1,0,0}});
    rejected(bad);
    require(patch.accept(many,now),"maximum unique validated curb descriptors accepted");
}
void crease_reconstruction() {
    const auto now=Clock::now();
    for(const double degrees:{30.0,40.0,50.0}) for(const double orientation:{-1.0,1.0}) {
        LiveGroundPatch patch; patch.reset_context("crease"); patch.observe_world_revision(1,true);
        const auto batch=*patch.prepare({.13,-.17,.6},1,now);
        const double divider=cell_origin(batch).east_m+.25;
        const double slope=orientation*std::tan(degrees*std::acos(-1.0)/180);
        auto result=response(batch,.1);
        for(auto& probe:result.probes) if(orientation*(probe.point.east_m-divider)>0) {
            probe.point.up_m=.1+slope*(probe.point.east_m-divider);
            const double length=std::sqrt(1+slope*slope); probe.normal={-slope/length,0,1/length};
        }
        require(patch.accept(result,now),"continuous crease samples accepted"); patch.begin_tick(now);
        const auto flat=hit_at(patch,divider-orientation*.1,-.02,.1);
        const auto ramp=hit_at(patch,divider+orientation*.1,-.02,.1+std::abs(slope)*.1);
        require(flat.normal_enu.up_m==1,"flat half keeps its plane normal");
        require(std::abs(ramp.normal_enu.east_m+slope/std::sqrt(1+slope*slope))<1e-8,
            "30 to 50 degree ramp half keeps its own plane normal");
        hit_at(patch,divider,-.02,.1);
    }
    LiveGroundPatch aligned; aligned.reset_context("grid-aligned-crease"); aligned.observe_world_revision(1,true);
    const auto aligned_batch=*aligned.prepare({0,0,.6},1,now);
    auto aligned_result=response(aligned_batch); const double slope=std::tan(std::acos(-1.0)/6);
    for(auto& probe:aligned_result.probes) if(probe.point.east_m>0) {
        probe.point.up_m=slope*probe.point.east_m;
        probe.normal={-slope/std::sqrt(1+slope*slope),0,1/std::sqrt(1+slope*slope)};
    }
    require(aligned.accept(aligned_result,now),"crease on a sample-grid line accepted"); aligned.begin_tick(now);
    hit_at(aligned,-.1,.25,0); hit_at(aligned,0,.25,0);
    require(std::abs(hit_at(aligned,.25,.25,slope*.25).normal_enu.up_m-std::cos(std::acos(-1.0)/6))<1e-8,
        "flat normals at the seam do not hide the thirty-degree cell interior");
    LiveGroundPatch patch; patch.reset_context("bad-crease"); patch.observe_world_revision(1,true);
    const auto batch=*patch.prepare({0,0,.6},1,now); auto result=response(batch);
    // The right-hand plane meets the left plane outside this cell, so it cannot explain this jump.
    for(auto& probe:result.probes) if(probe.point.east_m>.25) {
        probe.point.up_m=.2+.1*probe.point.east_m;
        probe.normal={-.1/std::sqrt(1.01),0,1/std::sqrt(1.01)};
    }
    require(patch.accept(result,now),"unresolved nonparallel planes accepted as measurements"); patch.begin_tick(now);
    unavailable([&]{patch.query_down({{.25,.25,4},24});});
    patch.reset_context("three-planes"); patch.observe_world_revision(1,true); const auto three_batch=*patch.prepare({0,0,.6},2,now);
    result=response(three_batch);
    result.probes[12*25+13].normal={-.6,0,.8};
    result.probes[13*25+12].normal={0,-.6,.8};
    require(patch.accept(result,now),"three distinct planes retained as unknown geometry"); patch.begin_tick(now);
    unavailable([&]{patch.query_down({{.25,.25,4},24});});
}
void incomplete_cells_and_materials() {
    const auto now=Clock::now();
    for(const auto status:{GroundProbeStatus::NoHit,GroundProbeStatus::Failed}) {
        LiveGroundPatch patch; patch.reset_context("missing-corner"); patch.observe_world_revision(1,true);
        const auto batch=*patch.prepare({0,0,.6},1,now); auto result=response(batch);
        result.probes[12*25+13]={status,{},{},0};
        require(patch.accept(result,now),"partial coverage is a valid measurement result"); patch.begin_tick(now);
        unavailable([&]{patch.query_down({{.25,.25,4},24});});
        hit_at(patch,-2,-2,0);
    }
    LiveGroundPatch patch; patch.reset_context("steep-normal"); patch.observe_world_revision(1,true);
    auto batch=*patch.prepare({0,0,.6},1,now); auto result=response(batch);
    for(auto& probe:result.probes) probe.normal={std::sqrt(.84),0,.4};
    require(patch.accept(result,now),"steep measurements accepted without fabricating support"); patch.begin_tick(now);
    unavailable([&]{patch.query_down({{.25,.25,4},24});});
    patch.reset_context("normal-limit"); patch.observe_world_revision(1,true); batch=*patch.prepare({0,0,.6},2,now); result=response(batch);
    for(const int index:{12*25+12,12*25+13,13*25+12,13*25+13}) {
        auto& probe=result.probes[index]; probe.point.up_m=std::sqrt(3.0)*probe.point.east_m;
        probe.normal={-std::sqrt(3.0)/2,0,.5};
    }
    require(patch.accept(result,now),"normal at the supported lower limit accepted"); patch.begin_tick(now);
    require(hit_at(patch,.25,.25,std::sqrt(3.0)*.25).normal_enu.up_m==.5,
        "normal up equal to one half remains supported");
    patch.reset_context("material-boundary"); patch.observe_world_revision(1,true); batch=*patch.prepare({0,0,.6},3,now); result=response(batch,0,.1);
    for(auto& probe:result.probes) probe.surface_material_id=probe.point.east_m<=0 ? 2 : 3;
    require(patch.accept(result,now),"coplanar material boundary accepted"); patch.begin_tick(now);
    const auto left=hit_at(patch,.1,.2,.02), right=hit_at(patch,.4,.2,.02);
    require(left.surface_material_id==GroundSurfaceMaterialId::LowFriction && left.friction_multiplier==.35,
        "left query selects nearest-corner material");
    require(right.surface_material_id==GroundSurfaceMaterialId::Rough && right.friction_multiplier==1.1,
        "right query selects nearest-corner material");
}
void patch_handoff() {
    const auto now=Clock::now(); LiveGroundPatch patch; patch.reset_context("handoff"); patch.observe_world_revision(1,true);
    auto batch=*patch.prepare({0,0,.6},1,now);
    require(patch.accept(response(batch),now+std::chrono::milliseconds(20)),"older patch accepted");
    batch=*patch.prepare({8,0,2},2,now+std::chrono::milliseconds(100));
    require(patch.accept(response(batch,1),now+std::chrono::milliseconds(120)),"newer patch accepted");
    patch.begin_tick(now+std::chrono::milliseconds(180));
    hit_at(patch,3,0,1); hit_at(patch,0,0,0);
    const auto deeper=patch.query_down({{3,0,4},27});
    require(deeper && deeper->point_enu.up_m==0,"previous patch can cover a volume outside the newest vertical scan");
    patch.begin_tick(now+LiveGroundPatch::Lifetime);
    unavailable([&]{patch.query_down({{0,0,4},24});});
    hit_at(patch,3,0,1);
    patch.begin_tick(now+LiveGroundPatch::Lifetime+std::chrono::milliseconds(100));
    unavailable([&]{patch.query_down({{3,0,4},24});});

    for(int condition=0;condition<4;++condition) {
        LiveGroundPatch replaced; replaced.reset_context("no-fallback"); replaced.observe_world_revision(1,true);
        batch=*replaced.prepare({0,0,.6},1,now);
        require(replaced.accept(response(batch,1),now),"fallback source accepted");
        batch=*replaced.prepare({0,0,.6},2,now+std::chrono::milliseconds(100));
        auto result=response(batch);
        if(condition==0) for(auto& probe:result.probes) probe={GroundProbeStatus::Failed,{},{},0};
        if(condition==1) for(auto& probe:result.probes) probe={GroundProbeStatus::NoHit,{},{},0};
        if(condition==2) result.probes[12*25+13]={GroundProbeStatus::NoHit,{},{},0};
        if(condition==3) for(auto& probe:result.probes) probe.point.up_m=probe.point.east_m<=.25 ? .2 : 0;
        require(replaced.accept(result,now+std::chrono::milliseconds(120)),"new result replaces covered volume");
        replaced.begin_tick(now+std::chrono::milliseconds(130));
        if(condition==1) require(!replaced.query_down({{.25,.25,4},24}),"new measured NoHit cannot resurrect old ground");
        else unavailable([&]{replaced.query_down({{.25,.25,4},24});});
    }
}
void delayed_driving() {
    for(const bool reverse:{false,true}) {
        VehiclePhysics vehicle(0,0,0,reverse ? 0.f : 90.f);
        auto patch=std::make_shared<LiveGroundPatch>(); patch->reset_context("delayed-driving"); patch->observe_world_revision(1,true);
        const auto now=Clock::now(); const auto initial=vehicle.get_state();
        const auto initial_batch=*patch->prepare(
            {initial.position_enu.x,initial.position_enu.y,initial.position_enu.z},0,now);
        require(patch->accept(response(initial_batch),now),"initial live driving measurement accepted");
        std::optional<GroundQueryBatch> in_flight;
        Clock::time_point delivered_at{};
        int refreshes=0; double peak_speed=0;
        VehicleInput drive; drive.throttle=1; drive.gear=reverse ? VehicleGear::Reverse : VehicleGear::Drive;
        vehicle.set_input(drive);
        for(int tick=0;tick<600;++tick) {
            const auto tick_time=now+std::chrono::nanoseconds(static_cast<std::int64_t>(tick)*1000000000/60);
            if(in_flight && tick_time>=delivered_at) {
                require(patch->accept(response(*in_flight),tick_time),"delayed moving patch accepted");
                in_flight.reset(); ++refreshes;
            }
            if(!in_flight) {
                const auto state=vehicle.get_state();
                const double heading=state.heading*std::acos(-1.0)/180;
                const GroundPointEnu velocity{
                    state.linear_velocity_body.x*std::sin(heading)-state.linear_velocity_body.y*std::cos(heading),
                    state.linear_velocity_body.x*std::cos(heading)+state.linear_velocity_body.y*std::sin(heading),0};
                const auto* batch=patch->prepare({state.position_enu.x,state.position_enu.y,state.position_enu.z},
                    static_cast<std::uint64_t>(tick),tick_time,velocity);
                if(batch) {
                    in_flight=*batch;
                    delivered_at=tick_time+std::chrono::milliseconds(100+25*(refreshes%3));
                }
            }
            patch->begin_tick(tick_time);
            const bool advanced=vehicle.try_update_with_ground(1.0/60,{},patch,tick==0);
            if(!advanced) {
                const auto& query=patch->last_missing_query();
                std::cerr<<"delayed drive blocked reverse="<<reverse<<" tick="<<tick
                    <<" reason="<<patch->last_missing_reason()<<" ray="<<query.origin_enu.east_m<<","
                    <<query.origin_enu.north_m<<","<<query.origin_enu.up_m<<" depth="<<query.max_distance_m<<"\n";
            }
            require(advanced,"moving vehicle must complete every step with 100 to 150 ms measurement delay");
            peak_speed=std::max(peak_speed,std::abs(static_cast<double>(vehicle.get_state().speed)));
        }
        const auto state=vehicle.get_state();
        const double distance=std::hypot(state.position_enu.x,state.position_enu.y);
        require(refreshes>20,"delayed drive exercises repeated moving patch handoffs");
        require(peak_speed>(reverse ? 3 : 10) && distance>(reverse ? 15 : 30),
            "delayed coverage supports substantial motion at changing vehicle speed");
        require(reverse ? state.position_enu.y<0 : state.position_enu.x>0,
            "delayed patch prediction follows actual forward and reverse motion");
        std::cout<<"delayed live drive reverse="<<reverse<<" accepted_steps=600 refreshes="<<refreshes
            <<" distance="<<distance<<" peak_speed="<<peak_speed<<"\n";
    }
}
void install(LiveGroundPatch& p, Clock::time_point now, double height=0, double slope=0) {
    const auto b=*p.prepare({0,0,.6},1,now);
    require(p.accept(response(b,height,slope),now+std::chrono::milliseconds(80)),"install patch");
    p.begin_tick(now+std::chrono::milliseconds(90));
}

void revision_fencing() {
    LiveGroundPatch patch; const auto now=Clock::now(); patch.reset_context("revision");
    const auto bootstrap=*patch.prepare({0,0,.6},1,now);
    require(bootstrap.world_revision==0,"bootstrap asks client for world revision");
    require(!patch.accept(response(bootstrap),now),"cannot install before world is known");
    require(patch.observe_world_revision(10,true),"first available world");
    const auto a=*patch.prepare({0,0,.6},2,now);
    const auto old=response(a);
    require(patch.accept(old,now),"initial world installed"); patch.begin_tick(now);
    hit_at(patch,0,0,0);
    require(patch.observe_world_revision(11,true),"changed world accepted");
    unavailable([&]{patch.query_down({{0,0,4},24});});
    require(!patch.observe_world_revision(10,true),"old world notice rejected");
    require(!patch.observe_world_revision(11,false),"readiness change requires new revision");
    const auto b=*patch.prepare({0,0,.6},3,now);
    require(b.environment_epoch>a.environment_epoch && b.request_id>a.request_id,"world change rotates request fence");
    require(!patch.accept(old,now),"previous in-flight result rejected");
    auto changed=response(b,.2); changed.world_revision=10;
    require(!patch.accept(changed,now),"correct request with stale revision rejected");
    changed.world_revision=11;
    require(patch.accept(changed,now),"new world installed"); hit_at(patch,0,0,.2);
    require(patch.observe_world_revision(11,true),"same notice idempotent"); hit_at(patch,0,0,.2);
    require(patch.observe_world_revision(12,false),"unsupported world invalidates cache immediately");
    unavailable([&]{patch.query_down({{0,0,4},24});});
    const auto invalid=*patch.prepare({0,0,.6},4,now);
    require(!patch.accept(response(invalid),now),"not-ready world cannot install a fabricated result");
    require(patch.observe_world_revision(13,true),"world repaired");
    const auto repaired=*patch.prepare({0,0,.6},5,now);
    require(patch.accept(response(repaired,.3),now),"repaired world resumes"); hit_at(patch,0,0,.3);
    patch.reset_context("new-session");
    require(patch.world_revision()==0,"world revision belongs to connection lifecycle");
    unavailable([&]{patch.query_down({{0,0,4},24});});
}

void contract() {
    LiveGroundPatch patch; auto t=Clock::now(); patch.reset_context("a"); patch.observe_world_revision(1,true);
    unavailable([&]{patch.query_down({{0,0,4},24});});
    const auto b=*patch.prepare({0,0,.6},9,t);
    auto r=response(b,1,.1), bad=r;
    bad.probes.pop_back(); require(!patch.accept(bad,t),"partial rejected");
    bad=r; bad.probes[0].point.east_m+=1; require(!patch.accept(bad,t),"off-ray rejected");
    bad=r; bad.environment_epoch++; require(!patch.accept(bad,t),"epoch rejected");
    bad=r; bad.probes[0].surface_material_id=99; require(!patch.accept(bad,t),"material rejected");
    require(patch.accept(r,t+std::chrono::milliseconds(100)),"slope accepted");
    patch.begin_tick(t+std::chrono::milliseconds(101));
    const auto hit=patch.query_down({{.25,.25,4},24});
    require(hit && std::abs(hit->point_enu.up_m-1.025)<1e-8,"live plane interpolation");
    unavailable([&]{patch.query_down({{6.01,0,4},24});});
    unavailable([&]{patch.query_down({{0,0,10},24});});
    unavailable([&]{patch.query_down({{0,0,0},24});});
    require(!patch.query_down({{0,0,4},1}),"measured ray too short is NoHit");
    patch.begin_tick(t+LiveGroundPatch::Lifetime);
    unavailable([&]{patch.query_down({{0,0,4},24});});
    patch.reset_context("b"); patch.observe_world_revision(1,true); require(!patch.accept(r,t),"old lifecycle rejected");
    auto next=*patch.prepare({0,0,.6},10,t);
    r=response(next); for(auto& q:r.probes) q={GroundProbeStatus::NoHit,{},{},0};
    require(patch.accept(r,t),"complete empty patch accepted"); patch.begin_tick(t);
    require(!patch.query_down({{0,0,4},24}),"confirmed empty is not unavailable");
    patch.reset_context("c"); patch.observe_world_revision(1,true); next=*patch.prepare({0,0,.6},11,t);
    r=response(next); r.probes[12*25+13].point.up_m=.2;
    require(patch.accept(r,t),"step samples accepted"); patch.begin_tick(t);
    unavailable([&]{patch.query_down({{.25,.25,4},24});});
    require(patch.query_down({{-2,-2,4},24}).has_value(),"discontinuity local, not whole patch invalid");
}
class InterruptingGround : public GroundQuery {
public:
    mutable int calls=0; int fail_at=0;
    FlatGroundQuery flat;
    std::optional<GroundHit> query_down(const GroundQueryRequest& r) const override {
        if (++calls==fail_at) throw GroundMeasurementUnavailable{};
        return flat.query_down(r);
    }
};
std::string physical_state(VehicleState s) { s.timestamp=0; return serialize_entity_state_packet(s); }
void rollback() {
    auto interrupted=std::make_shared<InterruptingGround>();
    VehiclePhysics actual(0,0,0,0), baseline(0,0,0,0);
    VehicleInput input; input.throttle=.5; input.steering=.2;
    actual.set_input(input); baseline.set_input(input);
    // Interrupt progressively later, including after integration and contact corrections.
    for(int step=0;step<180;++step) {
        interrupted->calls=0; interrupted->fail_at=0;
        require(actual.try_update_with_ground(1.0/60,{},interrupted,false),"valid step");
        baseline.update(1.0/60);
        require(physical_state(actual.get_state())==physical_state(baseline.get_state()),"provider substitution preserves physics");
        const int total=interrupted->calls;
        const auto before=physical_state(actual.get_state());
        interrupted->calls=0; interrupted->fail_at=1+(step%total);
        require(!actual.try_update_with_ground(1.0/60,{},interrupted,false),"tentative step rejected");
        require(physical_state(actual.get_state())==before,"full published state restored");
    }
    auto live=std::make_shared<LiveGroundPatch>(); live->reset_context("raise"); live->observe_world_revision(1,true);
    auto now=Clock::now(); install(*live,now,1);
    require(actual.try_update_with_ground(1.0/60,{},live,true),"initialization uses live ground");
    require(actual.get_state().position_enu.z>1.4,"file height cannot supply live initialization");
    live->begin_tick(now+LiveGroundPatch::Lifetime);
    const auto before=physical_state(actual.get_state());
    require(!actual.try_update_with_ground(1.0/60,{},live,false),"expiry freezes only tentative player step");
    require(physical_state(actual.get_state())==before,"expiry cannot consume fuel or advance pose");
}
void host_contract() {
    boost::asio::io_context ioc;
    SimulationHostConfig config; config.live_ground=true; config.map_package_checksum="map"; config.source_id="server";
    config.ground_query=std::make_shared<FlatGroundQuery>(); config.require_client_hello=true;
    SimulationHost host(ioc,config,{});
    const auto envelope=[](int seq) {
        simcore::Envelope e; e.set_schema_version(2); e.set_sequence(seq); e.set_source_id("ue");
        e.set_session_id("conn"); e.set_play_session_id("play"); e.set_map_package_checksum("map"); return e;
    };
    auto hello=envelope(1); auto* h=hello.mutable_hello(); h->set_build("test"); h->set_schema("simcore-envelope-v2");
    for(const auto* c:{"world-state.v2","control.v2","simulation-reset.v1","map-package-checksum.v1"}) h->add_capabilities(c);
    require(host.handle_client_message(hello.SerializeAsString(),1)==ClientMessageResult::Rejected,"old client cannot silently use files");
    h->add_capabilities("ground-patch.v1");
    require(host.handle_client_message(hello.SerializeAsString(),1)==ClientMessageResult::Rejected,"v1 client cannot supply v2 live patches");
    h->add_capabilities("ground-patch.v3");
    require(host.handle_client_message(hello.SerializeAsString(),1)==ClientMessageResult::HelloAccepted,"live hello");
    auto reset=envelope(2); reset.mutable_simulation_reset()->set_play_session_id("play");
    reset.mutable_simulation_reset()->set_client_time_ns(1);
    require(host.handle_client_message(reset.SerializeAsString(),1)==ClientMessageResult::SimulationReset,"live reset");
    auto notice=envelope(3); notice.mutable_ground_world_revision()->set_world_revision(1);
    notice.mutable_ground_world_revision()->set_measurements_ready(true);
    require(host.handle_client_message(notice.SerializeAsString(),1)==ClientMessageResult::GroundWorldRevisionAccepted,"initial world revision");
    simcore::Envelope world; world.ParseFromString(host.make_initial_world_state());
    require(world.world_state().has_live_ground_patch() && !world.world_state().has_ground_query_shadow(),"separate live wire field");
    const auto& wire=world.world_state().live_ground_patch();
    GroundQueryBatch batch{wire.request_id(),wire.environment_epoch(),wire.reference_simulation_time_ns(),1,
        {{wire.origins_enu(0).x(),wire.origins_enu(0).y(),wire.origins_enu(0).z()}},0,{32}};
    require(wire.has_patch_step_enu(),"v2 patch transmits sample spacing");
    batch.patch_step_enu={wire.patch_step_enu().x(),wire.patch_step_enu().y(),wire.patch_step_enu().z()};
    batch.world_revision=wire.world_revision();
    const auto data=response(batch,.75);
    auto reply=envelope(4); auto* out=reply.mutable_live_ground_patch_result();
    out->set_world_revision(data.world_revision);
    out->set_request_id(data.request_id); out->set_environment_epoch(data.environment_epoch);
    out->set_reference_simulation_time_ns(data.reference_simulation_time_ns);
    for(const auto& q:data.probes) {
        auto* p=out->add_probes(); p->set_status(simcore::GroundProbeResult::HIT);
        p->mutable_point_enu()->set_x(q.point.east_m); p->mutable_point_enu()->set_y(q.point.north_m); p->mutable_point_enu()->set_z(q.point.up_m);
        p->mutable_normal_enu()->set_z(1); p->set_surface_material_id(1);
    }
    require(reply.ByteSizeLong()<64*1024,"existing incoming frame limit preserved");
    auto bad=reply; bad.set_session_id("old");
    require(host.handle_client_message(bad.SerializeAsString(),1)==ClientMessageResult::Rejected,"old connection fenced");
    require(host.handle_client_message(reply.SerializeAsString(),1)==ClientMessageResult::GroundQueryResultAccepted,"live reply accepted");
    require(host.handle_client_message(reply.SerializeAsString(),1)==ClientMessageResult::Rejected,"duplicate reply fenced");
    world.ParseFromString(host.make_initial_world_state());
    require(!world.world_state().health().has_control_command(),"live ground must not refresh lease");
    host.start(); ioc.run_for(std::chrono::milliseconds(45)); host.stop();
    require(host.state().position_enu.z>1.2,"host physics consumes measured height, not file zero");
}
}
void unreal_fixtures(const std::filesystem::path& dir) {
    using namespace simcore_host;
    VehiclePhysics vehicle(0,0,0,0); // keep one running vehicle across both terrain edits
    for(int phase=0;phase<3;++phase) {
        std::ifstream input(dir/("patch-"+std::to_string(phase)+".bin"),std::ios::binary);
        require(bool(input),"Unreal-generated fixture missing");
        const std::string bytes{std::istreambuf_iterator<char>(input),{}};
        const auto parsed=parse_client_message_envelope(bytes);
        require(parsed && std::holds_alternative<ParsedGroundQueryResult>(*parsed),"Unreal wire decoded by server");
        const auto& result=std::get<ParsedGroundQueryResult>(*parsed);
        require(result.live_patch,"separate live payload");
        auto patch=std::make_shared<LiveGroundPatch>(); patch->reset_context("ue-fixture"); patch->observe_world_revision(1,true);
        const auto now=Clock::now();
        for(int id=1;id<=phase+1;++id) patch->prepare({0,0,.6},0,now+LiveGroundPatch::Lifetime*(id-1));
        const auto current=now+LiveGroundPatch::Lifetime*phase;
        require(patch->accept(result.result,current),"actual UE measurement accepted without ground file");
        patch->begin_tick(current);
        bool advanced = true;
        for(int tick=0;tick<20 && advanced;++tick) {
            patch->begin_tick(current+std::chrono::milliseconds(tick*16));
            advanced = vehicle.try_update_with_ground(1.0/60,{},patch,phase==0 && tick==0);
        }
        if (!advanced) {
            const auto& q=patch->last_missing_query();
            std::cerr<<"UE blocked phase="<<phase<<" reason="<<patch->last_missing_reason()
                <<" ray="<<q.origin_enu.east_m<<","<<q.origin_enu.north_m<<","<<q.origin_enu.up_m
                <<" depth="<<q.max_distance_m<<"\n";
        }
        require(advanced,"UE fixture drives solver");
        const double measured=result.result.probes[312].point.up_m;
        require(std::abs((vehicle.get_state().position_enu.z-measured)-.55)<.2,"body follows UE surface height");
        if(phase==2) require(std::abs(vehicle.get_state().pitch)>10 || std::abs(vehicle.get_state().roll)>10,"body follows UE slope");
        std::cout<<"UE fixture phase="<<phase<<" measured_height="<<measured
            <<" body_height="<<vehicle.get_state().position_enu.z<<" pitch="<<vehicle.get_state().pitch
            <<" roll="<<vehicle.get_state().roll<<"\n";
    }
    std::ifstream curb_input(dir/"patch-3.bin",std::ios::binary);
    require(bool(curb_input),"Unreal-generated curb fixture missing");
    const std::string curb_bytes{std::istreambuf_iterator<char>(curb_input),{}};
    const auto curb_parsed=parse_client_message_envelope(curb_bytes);
    require(curb_parsed && std::holds_alternative<ParsedGroundQueryResult>(*curb_parsed),
        "Unreal curb wire decoded by server");
    const auto& curb_result=std::get<ParsedGroundQueryResult>(*curb_parsed);
    require(curb_result.live_patch && curb_result.result.request_id==4
        && curb_result.result.environment_epoch==1 && curb_result.result.reference_simulation_time_ns==0,
        "Unreal curb fixture uses the expected live request context");
    require(curb_result.result.probes.size()==kGroundPatchSamples && curb_result.result.boundaries.size()==24,
        "Unreal measured all curb samples and each crossed cell wall");
    auto curb_patch=std::make_shared<LiveGroundPatch>(); curb_patch->reset_context("ue-curb-fixture"); curb_patch->observe_world_revision(1,true);
    const auto curb_start=Clock::now();
    for(int id=1;id<=4;++id) curb_patch->prepare({0,0,.6},0,curb_start+LiveGroundPatch::Lifetime*(id-1));
    const auto current=curb_start+LiveGroundPatch::Lifetime*3;
    require(curb_patch->accept(curb_result.result,current),"actual Unreal wall measurements accepted");
    curb_patch->begin_tick(current);
    hit_at(*curb_patch,.24,0,0); hit_at(*curb_patch,.26,0,.2);

    VehiclePhysics curb_vehicle(0,0,0,90); // heading 90 advances east across the measured wall
    bool saw_low_contact=false, saw_high_contact=false, reached_high_side=false;
    int refreshes=0, accepted_steps=0;
    for(int tick=0;tick<900;++tick) {
        const auto tick_time=current+std::chrono::nanoseconds(static_cast<std::int64_t>(tick)*1000000000/60);
        if(tick>0 && tick%6==0) {
            // Replay this static UE measurement on the identical grid; only the request fence changes.
            const auto* batch=curb_patch->prepare({0,0,.6},0,tick_time);
            require(batch && batch->patch_step_enu.east_m==.5 && batch->patch_step_enu.north_m==.5,
                "static curb replay keeps the measured fixture grid");
            auto refreshed=curb_result.result;
            refreshed.request_id=batch->request_id; refreshed.environment_epoch=batch->environment_epoch;
            refreshed.reference_simulation_time_ns=batch->reference_simulation_time_ns;
            require(curb_patch->accept(refreshed,tick_time),"static UE curb measurement refreshed before expiry");
            ++refreshes;
        }
        curb_patch->begin_tick(tick_time);
        VehicleInput drive; drive.throttle=tick<30 ? 0.f : .35f; curb_vehicle.set_input(drive);
        const bool advanced=curb_vehicle.try_update_with_ground(1.0/60,{},curb_patch,tick==0);
        if(!advanced) {
            const auto& query=curb_patch->last_missing_query();
            std::cerr<<"UE curb blocked tick="<<tick<<" reason="<<curb_patch->last_missing_reason()
                <<" ray="<<query.origin_enu.east_m<<","<<query.origin_enu.north_m<<","<<query.origin_enu.up_m
                <<" depth="<<query.max_distance_m<<"\n";
        }
        require(advanced,"Unreal curb fixture supports every actual solver step while crossing");
        ++accepted_steps;
        const auto state=curb_vehicle.get_state();
        for(const auto& wheel:state.wheels) if(wheel.in_contact) {
            const double height=wheel.contact_point_enu.z;
            require(std::abs(height)<1e-6 || std::abs(height-.2)<1e-6,
                "solver curb contact remains on a measured level");
            saw_low_contact|=std::abs(height)<1e-6; saw_high_contact|=std::abs(height-.2)<1e-6;
        }
        if(state.position_enu.x>=3) {
            require(state.position_enu.z>.6,"vehicle body rises onto the measured high curb surface");
            reached_high_side=true;
            std::cout<<"UE curb fixture accepted_steps="<<accepted_steps<<" refreshes="<<refreshes
                <<" east="<<state.position_enu.x<<" body_height="<<state.position_enu.z<<"\n";
            break;
        }
    }
    require(saw_low_contact && saw_high_contact,"solver consumes both measured curb levels");
    require(reached_high_side && refreshes>1,"vehicle traverses the live Unreal curb across patch refreshes");
}
int main(int argc,char** argv) { try {
    const auto defaults=simcore_host::make_runtime_option_defaults("vehicle.cfg","map");
    require(!defaults.live_ground,"default remains file ground");
    require(simcore_host::parse_runtime_options({"--live-ground"},defaults).live_ground,"explicit opt in");
    for(const auto& other:{"--ground-query-shadow"}) {
        std::vector<std::string> args{"--live-ground",other};
        if(std::string(other)!="--ground-query-shadow") args.push_back("test.bin");
        bool rejected=false; try { simcore_host::parse_runtime_options(args,defaults); } catch(const std::exception&) { rejected=true; }
        require(rejected,"unsupported live combination rejected");
    }
    require(simcore_host::parse_runtime_options({"--live-ground","--record-physics","test.bin"},defaults).live_ground,
        "measured live physics recording is supported");
    require(simcore_host::parse_runtime_options({"--live-ground","--verify-physics-replay","test.bin"},defaults).live_ground,
        "measured live physics replay is supported");
    revision_fencing(); contract(); adaptive_coverage(); curb_reconstruction(); boundary_validation(); crease_reconstruction();
    incomplete_cells_and_materials(); patch_handoff(); rollback(); host_contract();
    if(argc==2) unreal_fixtures(argv[1]); delayed_driving(); std::cout<<"live ground patch tests passed\n"; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
