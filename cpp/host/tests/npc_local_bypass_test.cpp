#include "traffic/npc_local_bypass.hpp"
#include "collision/collision_world.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {
using namespace simcore_host;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }

class Road final : public GroundQuery {
public:
    double half_width=4.5;
    bool classified=true;
    GroundSurfaceMaterialId road_material=GroundSurfaceMaterialId::Asphalt;
    bool support_hole=false;
    mutable std::size_t query_count=0;
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override
    {
        ++query_count;
        const auto& p=request.origin_enu;
        if (support_hole && p.north_m>20 && p.north_m<22) return std::nullopt;
        const bool road=std::abs(p.east_m)<=half_width;
        const double height=road ? 0.0 : 0.24;
        return GroundHit{{p.east_m,p.north_m,height},{0,0,1},p.up_m-height,
            classified ? (road ? road_material : GroundSurfaceMaterialId::Rough)
                : GroundSurfaceMaterialId::Default};
    }
};

TrafficNetwork network(double width=3.2)
{
    TrafficNetwork result;
    result.lanes={{10,width,6,0,true,{{0,0,0},{0,80,0}},{},{}}};
    return result;
}

void test_unmarked_road_support_and_hull()
{
    const auto traffic=network();
    Road ground;
    NpcLaneFollower follower;
    follower.rebuild(traffic,ground,{10},false,10.0);
    const ObbPrism obstacle{{0,23},0.85,0,2.2,1.0,0.75};
    auto clear=[&](const NpcLaneSample& sample,double) {
        return !intersect_obb_prisms({{sample.position_enu.east_m,sample.position_enu.north_m},
            .85,sample.heading_deg*std::numbers::pi/180,2.32,1.12,.75},obstacle);
    };
    const auto plan=plan_npc_local_bypass(traffic,ground,follower,17.5,{},clear);
    require(plan.has_value(),"an unmarked but measured asphalt shoulder must allow an off-centre pass");
    require(plan->lateral_offset_m()>1.6 && !plan->borrows_opposing_space(),
        "measured unmarked space must allow a right-side pass beyond the lane's painted width");
    double maximum_offset=0.0;
    bool signalled_out=false,signalled_return=false,parallel_off=false;
    auto previous=plan->sample(plan->begin_m());
    for (double s=plan->begin_m();s<=plan->end_m()+0.01;s+=.1) {
        const auto sample=plan->sample(s);
        require(sample && clear(*sample,0),"all local-bypass body poses must remain supported and collision-free");
        maximum_offset=std::max(maximum_offset,std::abs(sample->position_enu.east_m));
        const auto indicator=plan->turn_signal_intent(s);
        signalled_out|=indicator==(plan->lateral_offset_m()<0.0 ? 1U : 2U);
        signalled_return|=indicator==(plan->lateral_offset_m()<0.0 ? 2U : 1U);
        parallel_off|=indicator==0 && std::abs(sample->position_enu.east_m-plan->lateral_offset_m())<1.e-8;
        require(std::hypot(sample->position_enu.east_m-previous->position_enu.east_m,
            sample->position_enu.north_m-previous->position_enu.north_m)<.2,
            "lateral detours must be continuous, including their entry and exit");
        previous=sample;
    }
    const auto end=plan->sample(plan->end_m());
    require(end && std::abs(end->position_enu.east_m)<1.e-8 && std::abs(end->heading_deg)<1.e-8
        && end->lane_id==10 && maximum_offset>2.0,
        "a local bypass must return to its original route without a sideways adoption");
    require(signalled_out && signalled_return && parallel_off && plan->turn_signal_intent(plan->end_m())==0,
        "bypass indicators must describe departure and return separately, and stay off while passing in parallel");
    ground.support_hole=true;
    require(!plan->sample(plan->begin_m()+11.0),"live support loss must invalidate the trajectory sample");
    ground.support_hole=false;
    ground.half_width=1.8;
    require(!plan_npc_local_bypass(traffic,ground,follower,17.5,{},clear),
        "a sidewalk or insufficient real road width must not become an emergency passing lane");
    ground.half_width=4.5;
    ground.classified=false;
    require(!plan_npc_local_bypass(traffic,ground,follower,17.5,{},clear),
        "an unclassified infinite flat plane outside the known road is not proof of a shoulder");
}

void test_truck_dimensions_and_signal_retention()
{
    auto traffic=network(8.0);
    traffic.lanes[0].terminal=false;
    traffic.lanes[0].signal_group_id=1;
    traffic.lanes[0].successors={20};
    traffic.lanes.push_back({20,8,6,0,true,{{0,80,0},{0,100,0}},{},{}});
    traffic.signals={{1,1,{6,80,0},0}};
    Road ground; ground.half_width=5;
    NpcLaneFollower follower({6,.85,2.2,3.65,1.22,.5});
    follower.rebuild(traffic,ground,{10,20},false,47.0);
    const ObbPrism obstacle{{0,66},.85,0,2.2,1.0,.75};
    const auto plan=plan_npc_local_bypass(traffic,ground,follower,25.0,{3.65,1.22,8.5},
        [&](const NpcLaneSample& sample,double) {
            return !intersect_obb_prisms({{sample.position_enu.east_m,sample.position_enu.north_m},
                1.35,sample.heading_deg*std::numbers::pi/180,3.77,1.34,1.25},obstacle);
        });
    require(plan.has_value(),"the truck must plan with its own length, width and minimum turning radius");
    const TrafficSignalSnapshot red{1,1,SignalAspect::Red,{6,80,0},0,100};
    for (int i=0;i<1200;++i) follower.step(1.0/60,std::span(&red,1),true,std::nullopt,
        plan->route_speed_limit_mps(),plan->extra_stop_margin_m());
    require(follower.state().stop_reason==NpcLaneStopReason::Signal
        && follower.state().position_enu.north_m+3.65+plan->extra_stop_margin_m()<=79.51,
        "an off-centre path must retain the original movement's red light and enlarged front envelope");
}

using ClearanceTrace = std::vector<std::array<double, 4>>;

void require_same_plan(const NpcLocalBypassPlan& expected, const NpcLocalBypassPlan& actual)
{
    require(expected.begin_m() == actual.begin_m() && expected.end_m() == actual.end_m()
            && expected.lateral_offset_m() == actual.lateral_offset_m()
            && expected.route_speed_limit_mps() == actual.route_speed_limit_mps()
            && expected.expected_duration_seconds() == actual.expected_duration_seconds()
            && expected.borrows_opposing_space() == actual.borrows_opposing_space(),
        "yielding must preserve the selected candidate and its exact speed/time contract");
    for (int index = 0; index <= 40; ++index) {
        const double distance = expected.begin_m()
            + (expected.end_m() - expected.begin_m()) * index / 40;
        const auto a = expected.sample(distance);
        const auto b = actual.sample(distance);
        require(a && b && a->position_enu.east_m == b->position_enu.east_m
                && a->position_enu.north_m == b->position_enu.north_m
                && a->position_enu.up_m == b->position_enu.up_m
                && a->heading_deg == b->heading_deg && a->lane_id == b->lane_id
                && expected.turn_signal_intent(distance) == actual.turn_signal_intent(distance),
            "yielding must preserve every compared supported pose and turn-signal intent");
    }
}

void require_body_right_of_centerline(const NpcLocalBypassPlan& plan,
    double half_length=2.2, double half_width=1.0)
{
    require(!plan.borrows_opposing_space(), "accepted bypasses must never borrow opposing traffic space");
    const int count=static_cast<int>(std::ceil((plan.end_m()-plan.begin_m())/0.05));
    for (int index=0; index<=count; ++index) {
        const auto sample=plan.sample(plan.begin_m()
            +(plan.end_m()-plan.begin_m())*index/count);
        require(sample.has_value(), "the complete accepted bypass must remain sampleable");
        const double heading=sample->heading_deg*std::numbers::pi/180.0;
        for (const double front : {-half_length,half_length})
            for (const double side : {-half_width,half_width})
                require(sample->position_enu.east_m+std::sin(heading)*front
                        +std::cos(heading)*side>=-1.e-8,
                    "every rotated body corner must stay on its side of the physical centerline");
    }
}

void test_two_way_centerline_forbids_empty_opposing_space()
{
    auto traffic=network(3.0);
    traffic.lanes[0].points={{2,0,0},{2,80,0}};
    traffic.lanes.push_back({20,3.0,6,0,true,{{-2,80,0},{-2,0,0}},{},{}});
    const ObbPrism obstacle{{2,23},.85,0,2.2,1,.75};
    for (const auto material : {GroundSurfaceMaterialId::Asphalt,
            GroundSurfaceMaterialId::LowFriction,GroundSurfaceMaterialId::Default}) {
        Road ground;
        ground.half_width=6.0;
        ground.road_material=material;
        NpcLaneFollower follower;
        follower.rebuild(traffic,ground,{10},false,10.0);
        const auto clear=[&](const NpcLaneSample& sample,double) {
            return !intersect_obb_prisms({{sample.position_enu.east_m,sample.position_enu.north_m},
                .85,sample.heading_deg*std::numbers::pi/180.0,2.32,1.12,.75},obstacle);
        };
        if (material!=GroundSurfaceMaterialId::Default) {
            const auto plan=plan_npc_local_bypass(traffic,ground,follower,17.5,{},clear);
            require(plan && plan->lateral_offset_m()>0.0,
                "a two-way collector must pass only through its clear right-side asphalt space");
            require_body_right_of_centerline(*plan);
            NpcLocalBypassSearch search;
            search.begin(traffic,ground,follower,17.5,{});
            for (int calls=0; search.status()==NpcLocalBypassSearchStatus::Pending && calls<50000; ++calls)
                require(search.advance(clear,3).samples_checked<=3,
                    "centerline-safe search must retain its per-advance work budget");
            require(search.status()==NpcLocalBypassSearchStatus::Found && search.plan(),
                "incremental search must find the same legal right-side pass");
            require_same_plan(*plan,*search.plan());
            require_body_right_of_centerline(*search.plan());
        }
        const auto only_left_clear=[&](const NpcLaneSample& sample,double horizon) {
            return sample.position_enu.east_m<=2.0+1.e-8 && clear(sample,horizon);
        };
        require(!plan_npc_local_bypass(traffic,ground,follower,17.5,{},only_left_clear),
            "an empty opposing lane must not authorize passing when the right-side space is blocked");
        NpcLocalBypassSearch search;
        search.begin(traffic,ground,follower,17.5,{});
        for (int calls=0; search.status()==NpcLocalBypassSearchStatus::Pending && calls<50000; ++calls)
            require(search.advance(only_left_clear,3).samples_checked<=3,
                "rejected opposing-space candidates must retain the incremental work budget");
        require(search.status()==NpcLocalBypassSearchStatus::Exhausted && !search.plan(),
            "synchronous and incremental searches must both reject the opposing-only escape");
    }
}

void test_centerline_gap_does_not_allow_ten_centimetre_body_intrusion()
{
    auto traffic=network(3.0);
    traffic.lanes[0].points={{2,0,0},{2,80,0}};
    traffic.lanes.push_back({20,3.0,6,0,true,{{-2,80,0},{-2,0,0}},{},{}});
    Road ground;
    ground.half_width=6.0;
    NpcLaneFollower follower;
    follower.rebuild(traffic,ground,{10},false,10.0);
    // The only clear parallel position puts the body center at x=1.25m.
    // Its 1.35m half-width crosses the actual x=0 centerline by just 10cm,
    // without reaching the reverse lane's nearest footprint at x=-0.47m.
    const auto narrow_opening=[](const NpcLaneSample& sample,double) {
        return sample.position_enu.north_m<20.0 || sample.position_enu.north_m>22.0
            || std::abs(sample.position_enu.east_m-1.25)<1.e-6;
    };
    const NpcLocalBypassDimensions dimensions{2.2,1.35,4.8};
    require(!plan_npc_local_bypass(traffic,ground,follower,17.5,dimensions,narrow_opening),
        "a centerline crossing by the body edge must fail even when its center stays in the correct direction");
    NpcLocalBypassSearch search;
    search.begin(traffic,ground,follower,17.5,dimensions);
    for (int calls=0; search.status()==NpcLocalBypassSearchStatus::Pending && calls<50000; ++calls)
        require(search.advance(narrow_opening,3).samples_checked<=3,
            "body-edge rejection must remain bounded across incremental advances");
    require(search.status()==NpcLocalBypassSearchStatus::Exhausted && !search.plan(),
        "incremental search must also reject body intrusion in the gap between authored lane footprints");
}

void test_same_direction_lane_gap_remains_passable()
{
    auto traffic=network(3.2);
    traffic.lanes[0].points={{5,0,0},{5,80,0}};
    traffic.lanes.push_back({20,3.2,6,0,true,{{1.7,0,0},{1.7,80,0}},{},{}});
    traffic.lanes.push_back({30,3.0,6,0,true,{{-2,80,0},{-2,0,0}},{},{}});
    Road ground;
    ground.half_width=8.5;
    ground.classified=false;
    NpcLaneFollower follower;
    follower.rebuild(traffic,ground,{10},false,10.0);
    const ObbPrism obstacle{{5,23},.85,0,2.2,1,.75};
    double requested_horizon=0.0;
    const auto clear=[&](const NpcLaneSample& sample,double horizon) {
        requested_horizon=std::max(requested_horizon,horizon);
        return sample.position_enu.east_m<=5.0+1.e-8
            && !intersect_obb_prisms({{sample.position_enu.east_m,sample.position_enu.north_m},
                .85,sample.heading_deg*std::numbers::pi/180.0,2.32,1.12,.75},obstacle);
    };
    const auto plan=plan_npc_local_bypass(traffic,ground,follower,17.5,{},clear);
    require(plan && plan->lateral_offset_m()< -1.6,
        "same-direction lane centers 3.3m apart must permit a left pass across their narrow width gap");
    require_body_right_of_centerline(*plan);
    require(requested_horizon>=plan->expected_duration_seconds() && requested_horizon>3.0,
        "legal same-direction passes must retain prediction over the complete departure and return");
    NpcLocalBypassSearch search;
    search.begin(traffic,ground,follower,17.5,{});
    for (int calls=0; search.status()==NpcLocalBypassSearchStatus::Pending && calls<50000; ++calls)
        require(search.advance(clear,3).samples_checked<=3,
            "same-direction lane-gap search must retain its incremental work budget");
    require(search.status()==NpcLocalBypassSearchStatus::Found && search.plan(),
        "incremental search must also find the legal same-direction pass");
    require_same_plan(*plan,*search.plan());
    require_body_right_of_centerline(*search.plan());
}

void test_incremental_search_budget_and_synchronous_equivalence()
{
    const auto traffic = network();
    Road ground;
    NpcLaneFollower follower;
    follower.rebuild(traffic, ground, {10}, false, 10.0);
    const ObbPrism obstacle{{0,23}, .85, 0, 2.2, 1.0, .75};
    const auto make_clear = [&](ClearanceTrace& trace) {
        return [&](const NpcLaneSample& sample, double horizon) {
            trace.push_back({sample.position_enu.east_m, sample.position_enu.north_m,
                sample.heading_deg, horizon});
            return !intersect_obb_prisms(
                {{sample.position_enu.east_m, sample.position_enu.north_m}, .85,
                    sample.heading_deg * std::numbers::pi / 180, 2.32, 1.12, .75}, obstacle);
        };
    };
    ClearanceTrace synchronous_trace;
    const auto synchronous = plan_npc_local_bypass(
        traffic, ground, follower, 17.5, {}, make_clear(synchronous_trace));
    require(synchronous.has_value(), "the budget fixture must have a complete supported bypass");
    const auto road_index = make_npc_local_road_index(traffic);
    for (const std::size_t budget : {1U, 3U, 8U}) {
        NpcLocalBypassSearch search;
        ClearanceTrace trace;
        const auto clear = make_clear(trace);
        search.begin(traffic, ground, follower, 17.5, {}, road_index);
        const auto query_start = ground.query_count;
        const auto zero = search.advance(clear, 0);
        require(zero.status == NpcLocalBypassSearchStatus::Pending && zero.samples_checked == 0
                && search.total_samples_checked() == 0 && trace.empty()
                && ground.query_count == query_start,
            "zero budget must not sample ground or call collision policy");
        std::size_t work = 0;
        std::size_t calls = 0;
        bool yielded_geometry = false, yielded_prediction = false;
        while (search.status() == NpcLocalBypassSearchStatus::Pending && calls++ < 50000) {
            const auto before_queries = ground.query_count;
            const auto before_callbacks = trace.size();
            const auto result = search.advance(clear, budget);
            require(result.samples_checked <= budget
                    && trace.size() - before_callbacks <= result.samples_checked,
                "each advance must bound both geometry and predicted collision callbacks");
            // The heading stencil uses up to ten follower samples, each with
            // a centre and four corner queries. This is followed by the plan
            // centre and the default 6 x 3 underbody grid: 10*5 + 1 + 18 = 69.
            // Prediction units perform no additional ground query.
            constexpr std::size_t ground_queries_per_sample = 10 * 5 + 1 + 6 * 3;
            require(ground.query_count - before_queries <= ground_queries_per_sample * result.samples_checked,
                "a yielded advance must not scan the rest of a candidate's underbody samples");
            work += result.samples_checked;
            require(search.total_samples_checked() == work,
                "reported work must include initial, geometry and prediction samples");
            if (result.status == NpcLocalBypassSearchStatus::Pending && !trace.empty()) {
                yielded_geometry |= trace.back()[3] == 0.0;
                yielded_prediction |= trace.back()[3] > 0.0;
            }
        }
        require(search.status() == NpcLocalBypassSearchStatus::Found && search.plan(),
            "a finite series of bounded advances must finish the valid search");
        require(yielded_geometry && yielded_prediction,
            "the fixture must yield within both geometry and predicted collision passes");
        require(trace == synchronous_trace,
            "budget changes must preserve the exact synchronous callback order and horizons");
        require_same_plan(*synchronous, *search.plan());
        const auto terminal = search.advance(clear, budget);
        require(terminal.status == NpcLocalBypassSearchStatus::Found
                && terminal.samples_checked == 0 && search.total_samples_checked() == work,
            "an accepted search must remain terminal without repeating work");
        search.begin(traffic, ground, follower, -1.0, {}, road_index);
        require(search.status() == NpcLocalBypassSearchStatus::Exhausted
                && !search.plan() && search.total_samples_checked() == 0,
            "begin must discard a previous result and counters even for invalid input");
    }
}

void test_incremental_prediction_rejection_and_fresh_callback()
{
    const auto traffic = network();
    Road ground;
    NpcLaneFollower follower;
    follower.rebuild(traffic, ground, {10}, false, 10.0);
    NpcLocalBypassSearch search;
    search.begin(traffic, ground, follower, 17.5, {});
    std::size_t initial_callbacks = 0;
    const auto initial = search.advance([&](const NpcLaneSample&, double) {
        ++initial_callbacks;
        return true;
    }, 1);
    require(initial.status == NpcLocalBypassSearchStatus::Pending
            && initial.samples_checked == 1 && initial_callbacks == 0,
        "the initial supported-pose sample must itself consume budget before geometry callbacks");
    std::size_t replacement_callbacks = 0;
    const auto blocked = [&](const NpcLaneSample&, double) {
        ++replacement_callbacks;
        return false;
    };
    for (int calls = 0; search.status() == NpcLocalBypassSearchStatus::Pending && calls < 1000; ++calls)
        require(search.advance(blocked, 1).samples_checked <= 1,
            "rejecting a candidate must not consume the next candidate's samples outside budget");
    require(search.status() == NpcLocalBypassSearchStatus::Exhausted && !search.plan()
            && initial_callbacks == 0 && replacement_callbacks != 0,
        "advance must use its current collision callback rather than retaining the first one");
    const auto terminal = search.advance(blocked, 8);
    require(terminal.status == NpcLocalBypassSearchStatus::Exhausted && terminal.samples_checked == 0,
        "exhausted searches must not retry without an explicit begin");

    search.begin(traffic, ground, follower, 17.5, {});
    std::size_t prediction_callbacks = 0;
    const auto predicted_block = [&](const NpcLaneSample&, double horizon) {
        if (horizon == 0.0) return true;
        ++prediction_callbacks;
        return false;
    };
    for (int calls = 0; search.status() == NpcLocalBypassSearchStatus::Pending && calls < 50000; ++calls) {
        const auto before = prediction_callbacks;
        const auto result = search.advance(predicted_block, 1);
        require(result.samples_checked <= 1 && prediction_callbacks - before <= 1,
            "predicted collision checks must yield individually, not run an unbounded all_of pass");
    }
    require(search.status() == NpcLocalBypassSearchStatus::Exhausted
            && !search.plan() && prediction_callbacks != 0,
        "a clear present-time path must still fail when its predicted pass is occupied");
    require(!plan_npc_local_bypass(traffic, ground, follower, 17.5, {}, predicted_block),
        "the synchronous wrapper must retain predicted rejection semantics");
}

void test_incremental_search_invalid_input()
{
    const auto traffic = network();
    Road ground;
    NpcLaneFollower follower;
    follower.rebuild(traffic, ground, {10}, false, 10.0);
    std::size_t callbacks = 0;
    const auto clear = [&](const NpcLaneSample&, double) { ++callbacks; return true; };
    const auto require_invalid = [&](const NpcLaneFollower& reference, double blocked,
                                     NpcLocalBypassDimensions dimensions) {
        NpcLocalBypassSearch search;
        const auto queries = ground.query_count;
        search.begin(traffic, ground, reference, blocked, dimensions);
        const auto result = search.advance(clear, 8);
        require(result.status == NpcLocalBypassSearchStatus::Exhausted
                && result.samples_checked == 0 && !search.plan()
                && ground.query_count == queries && callbacks == 0,
            "invalid search inputs must exhaust without ground or collision work");
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    require_invalid(NpcLaneFollower{}, 17.5, {});
    for (const double blocked : {-1.0, 40.01, nan, std::numeric_limits<double>::infinity()})
        require_invalid(follower, blocked, {});
    for (const auto dimensions : std::array<NpcLocalBypassDimensions, 9>{{
            {0,1,4.8}, {5.01,1,4.8}, {2.2,0,4.8}, {2.2,2.01,4.8},
            {2.2,1,1.99}, {2.2,1,15.01}, {nan,1,4.8}, {2.2,nan,4.8}, {2.2,1,nan}}})
        require_invalid(follower, 17.5, dimensions);
    NpcLocalBypassSearch search;
    search.begin(traffic, ground, follower, 17.5, {});
    const auto empty_callback = search.advance({}, 8);
    require(empty_callback.status == NpcLocalBypassSearchStatus::Exhausted
            && empty_callback.samples_checked == 0 && !search.plan(),
        "an absent callback must fail closed rather than leave a runnable search");
    require(!plan_npc_local_bypass(traffic, ground, follower, 17.5, {}, {}),
        "the synchronous API must continue rejecting an absent collision callback");
}
}

int main()
{
    try {
        test_unmarked_road_support_and_hull();
        test_truck_dimensions_and_signal_retention();
        test_two_way_centerline_forbids_empty_opposing_space();
        test_centerline_gap_does_not_allow_ten_centimetre_body_intrusion();
        test_same_direction_lane_gap_remains_passable();
        test_incremental_search_budget_and_synchronous_equivalence();
        test_incremental_prediction_rejection_and_fresh_callback();
        test_incremental_search_invalid_input();
        std::cout << "npc_local_bypass_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "npc_local_bypass_test: " << error.what() << '\n';
        return 1;
    }
}
