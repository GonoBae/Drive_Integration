#include "terrain/ground_query_shadow.hpp"
#include "simulation_host.hpp"
#include "runtime_options.hpp"
#include "vehicle.pb.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
using namespace simcore_host;
GroundQueryResult response(const GroundQueryBatch& b) {
    GroundQueryResult r{b.request_id,b.environment_epoch,b.reference_simulation_time_ns,{},0.25};
    for (const auto& p : b.origins) r.probes.push_back({GroundProbeStatus::Hit,{p.east_m,p.north_m,0},{0,0,1},0});
    return r;
}
void test_shadow_contract() {
    GroundQueryShadow shadow;
    FlatGroundQuery ground;
    const auto now = GroundQueryShadow::Clock::time_point{} + std::chrono::seconds(1);
    const std::vector<GroundPointEnu> origins{{0,0,2},{1,2,2}};
    shadow.reset_context("play-a/map-a/connection-a");
    const auto first = *shadow.prepare(17,origins,ground,now);
    require(shadow.prepare(18,origins,ground,now+std::chrono::milliseconds(20))->request_id == first.request_id,
        "one pending request must survive latest-state replacement");
    auto valid = response(first);
    auto bad = valid; bad.probes.pop_back();
    require(!shadow.accept(bad,now), "partial batch must fail atomically");
    bad = valid; bad.probes[1].point.east_m += 1;
    require(!shadow.accept(bad,now), "off-ray point must fail");
    bad = valid; bad.environment_epoch++;
    require(!shadow.accept(bad,now), "wrong world epoch must fail");
    bad = valid; bad.reference_simulation_time_ns++;
    require(!shadow.accept(bad,now), "wrong reference step must fail");
    bad = valid; bad.probes[0].normal.up_m = std::numeric_limits<double>::quiet_NaN();
    require(!shadow.accept(bad,now), "NaN must fail");
    bad = valid; bad.measurement_duration_ms = -1;
    require(!shadow.accept(bad,now), "negative duration must fail");
    require(shadow.statistics().hits_compared == 0, "invalid batch must not partially count contacts");
    require(shadow.accept(valid,now+std::chrono::milliseconds(25)), "valid result accepted");
    require(!shadow.accept(valid,now+std::chrono::milliseconds(26)), "duplicate rejected");
    require(shadow.statistics().last_round_trip_ms == 25 && shadow.statistics().max_height_error_m == 0,
        "local roundtrip and height comparison");
    require(!shadow.prepare(18,origins,ground,now+std::chrono::milliseconds(99)), "10Hz rate bound");
    const auto second = *shadow.prepare(18,origins,ground,now+std::chrono::milliseconds(100));
    require(!shadow.accept(response(second),now+std::chrono::milliseconds(600)), "500ms deadline excludes expired observations");
    const auto third = *shadow.prepare(20,origins,ground,now+std::chrono::milliseconds(600));
    require(third.request_id > second.request_id && shadow.statistics().timed_out == 1, "timeout replaces single pending request");
    auto mismatch = response(third);
    mismatch.probes[0] = {GroundProbeStatus::NoHit,{},{},0};
    mismatch.probes[1] = {GroundProbeStatus::Failed,{},{},0};
    require(shadow.accept(mismatch,now+std::chrono::milliseconds(620)), "no hit and failed are valid but distinct observations");
    require(shadow.statistics().presence_mismatches == 1 && shadow.statistics().failed_probes == 1, "separate missing and failed metrics");
    shadow.reset_context("play-b/map-a/connection-b");
    const auto next = *shadow.prepare(0,origins,ground,now+std::chrono::milliseconds(630));
    require(next.environment_epoch > first.environment_epoch && next.request_id > third.request_id, "new lifecycle cannot reuse request identity");
    require(!shadow.accept(valid,now+std::chrono::milliseconds(635)), "previous lifecycle response rejected");
}
simcore::Envelope envelope(std::uint64_t sequence) {
    simcore::Envelope e;
    e.set_schema_version(2); e.set_sequence(sequence); e.set_source_id("unreal-test");
    e.set_session_id("connection-a"); e.set_play_session_id("play-a"); e.set_map_package_checksum("test-map");
    return e;
}
void test_host(bool enabled, bool capture = false) {
    boost::asio::io_context ioc;
    SimulationHostConfig config;
    config.source_id="server"; config.map_package_checksum="test-map";
    config.ground_query=std::make_shared<FlatGroundQuery>(); config.ground_query_shadow=enabled;
    config.require_client_hello=true;
    SimulationHost host(ioc,config,{});
    auto hello=envelope(1); auto* h=hello.mutable_hello(); h->set_build("test"); h->set_schema("simcore-envelope-v2");
    for (const auto* c : {"world-state.v2","control.v2","simulation-reset.v1","map-package-checksum.v1","ground-query-shadow.v1"}) h->add_capabilities(c);
    if (capture) h->add_capabilities("ground-query-shadow.v2");
    require(host.handle_client_message(hello.SerializeAsString(),1)==ClientMessageResult::HelloAccepted,"hello");
    auto reset=envelope(2); reset.mutable_simulation_reset()->set_play_session_id("play-a"); reset.mutable_simulation_reset()->set_client_time_ns(1);
    require(host.handle_client_message(reset.SerializeAsString(),1)==ClientMessageResult::SimulationReset,"reset");
    if (capture) { host.start(); ioc.run_for(std::chrono::milliseconds(40)); host.stop(); }
    simcore::Envelope world; world.ParseFromString(host.make_initial_world_state());
    require(world.world_state().has_ground_query_shadow()==enabled,"shadow is opt-in");
    if (!enabled) return;
    auto result=envelope(3); auto* r=result.mutable_ground_query_result();
    const auto& batch=world.world_state().ground_query_shadow();
    require(capture ? batch.origins_enu_size() > 5 && batch.origins_enu_size() <= 64
                    : batch.origins_enu_size() == 5, "negotiated probe limit");
    require(capture ? batch.query_distances_m_size() == batch.origins_enu_size()
                    : batch.query_distances_m_size() == 0, "v2 alone carries exact query depths");
    r->set_request_id(batch.request_id()); r->set_environment_epoch(batch.environment_epoch());
    r->set_reference_simulation_time_ns(batch.reference_simulation_time_ns());
    for(const auto& p : batch.origins_enu()) {
        auto* out=r->add_probes(); out->set_status(simcore::GroundProbeResult::HIT);
        out->mutable_point_enu()->set_x(p.x()); out->mutable_point_enu()->set_y(p.y());
        out->mutable_normal_enu()->set_z(1);
    }
    auto bad=result; bad.set_session_id("old");
    require(host.handle_client_message(bad.SerializeAsString(),1)==ClientMessageResult::Rejected,"wrong connection");
    bad=result; bad.set_play_session_id("old");
    require(host.handle_client_message(bad.SerializeAsString(),1)==ClientMessageResult::Rejected,"wrong play");
    bad=result; bad.set_map_package_checksum("old");
    require(host.handle_client_message(bad.SerializeAsString(),1)==ClientMessageResult::Rejected,"wrong map");
    require(host.handle_client_message(result.SerializeAsString(),2)==ClientMessageResult::Rejected,"wrong generation");
    const auto before=host.state();
    require(host.handle_client_message(result.SerializeAsString(),1)==ClientMessageResult::GroundQueryResultAccepted,"measurement accepted");
    require(host.handle_client_message(result.SerializeAsString(),1)==ClientMessageResult::Rejected,"duplicate measurement rejected");
    const auto after=host.state();
    require(before.position_enu.x==after.position_enu.x && before.position_enu.z==after.position_enu.z && before.speed==after.speed,"measurement cannot move car");
    world.ParseFromString(host.make_initial_world_state());
    require(!world.world_state().health().has_control_command(),"measurements cannot refresh lease");
    auto control=envelope(3); control.mutable_control_command()->set_client_time_ns(2);
    require(host.handle_client_message(control.SerializeAsString(),1)==ClientMessageResult::Rejected,"shared envelope order fences control");
    control.set_sequence(4);
    require(host.handle_client_message(control.SerializeAsString(),1)==ClientMessageResult::ControlAccepted,"fresh control still works");
    require(host.ground_shadow_statistics().accepted==1,"diagnostic count");
}

void test_capture_limits() {
    auto ground = std::make_shared<FlatGroundQuery>();
    CapturingGroundQuery capture(ground);
    capture.begin_frame(123);
    for (int i = 0; i < 600; ++i) capture.query_down({{double(i), 0, 4}, 24});
    for (int i = 0; i < 10; ++i) capture.query_down({{0, 0, 4}, 24});
    capture.query_down({{0, 0, 4}, 1}); // same origin but distinct depth/no hit
    capture.end_frame();
    require(capture.frame().calls == 611 && capture.frame().samples.size() == 512
        && capture.frame().discarded == 89, "bounded capture records omitted calls without affecting source");
    require(capture.query_down({{700, 0, 4}, 24}).has_value(), "full capture still forwards real query");
    require(capture.frame().calls == 611, "outside-step calls are not added");
    GroundQueryShadow shadow;
    shadow.reset_context("capture-a");
    auto now = GroundQueryShadow::Clock::now();
    const auto batch = *shadow.prepare_captured(capture.frame(), now);
    require(batch.origins.size() == 64 && batch.query_distances_m.size() == 64
        && batch.max_distance_m == 0 && batch.reference_simulation_time_ns == 123, "exact bounded solver slice");
    require(batch.query_distances_m.front() == 24 && batch.origins.front().up_m == 4, "do not substitute diagnostic 8m depth");
    require(shadow.accept(response(batch), now + std::chrono::milliseconds(20)), "captured comparison accepted");
    const auto next = *shadow.prepare_captured(capture.frame(), now + std::chrono::milliseconds(100));
    require(next.origins.front().east_m == 64, "rotate through the retained query set");
    auto result = response(next); result.probes.front().point.up_m = -21;
    require(!shadow.accept(result, now + std::chrono::milliseconds(110)), "hit beyond per-probe depth rejected");
    result = response(next);
    require(shadow.accept(result, now + std::chrono::milliseconds(140)), "valid second sample");
    require(shadow.statistics().round_trip_p95_ms == 40, "nearest-rank response p95");
    capture.begin_frame(124);
    capture.query_down({{0, 0, 4}, 1});
    capture.query_down({{0, 0, 4}, 24});
    capture.query_down({{0, 0, 4}, 24});
    capture.end_frame();
    require(capture.frame().samples.size() == 2 && !capture.frame().samples[0].hit
        && capture.frame().samples[1].hit.has_value(), "depth and explicit no-hit retained without duplicate storage");
    capture.clear();
    require(capture.frame().samples.empty() && capture.frame().calls == 0, "reset removes prior frame");
}

void test_capture_keeps_driving_identical() {
    const auto ground = std::make_shared<FlatGroundQuery>();
    const auto capture = std::make_shared<CapturingGroundQuery>(ground);
    VehiclePhysics baseline(0,0,0,0,{},ground);
    VehiclePhysics observed(0,0,0,0,{},capture);
    std::size_t maximum_queries = 0, maximum_unique = 0;
    const EnvelopeMetadata metadata{1,0,"test","test-map"};
    for (int tick = 0; tick < 600; ++tick) {
        VehicleInput input;
        input.throttle = tick < 480 ? 0.4f : 0;
        input.brake = tick >= 480 ? 0.4f : 0;
        input.steering = tick >= 180 && tick < 420 ? 0.15f : 0;
        baseline.set_input(input); observed.set_input(input);
        capture->begin_frame(tick);
        const auto expected = baseline.update(1.0/60.0);
        auto actual = observed.update(1.0/60.0);
        capture->end_frame();
        // Wall-clock publication time differs between sequential calls. It is
        // metadata, not a simulated state value; compare all other fields.
        actual.timestamp = expected.timestamp;
        require(serialize_world_state_envelope(expected, metadata)
            == serialize_world_state_envelope(actual, metadata), "capture must preserve every published physical field");
        require(capture->frame().discarded == 0, "flat driving trace fits capture budget");
        maximum_queries = std::max(maximum_queries, capture->frame().calls);
        maximum_unique = std::max(maximum_unique, capture->frame().samples.size());
    }
    require(maximum_unique > 5, "actual solver requires more than five footprint rays");
    std::cout << "600 identical driving ticks; max queries=" << maximum_queries
        << " max unique=" << maximum_unique << '\n';
}
}
int main() {
    try {
        test_shadow_contract(); test_host(false); test_host(true); test_host(true, true);
        test_capture_limits(); test_capture_keeps_driving_identical();
        const auto defaults=simcore_host::make_runtime_option_defaults("vehicle.cfg","map");
        require(!defaults.ground_query_shadow,"default disabled");
        require(simcore_host::parse_runtime_options({"--ground-query-shadow"},defaults).ground_query_shadow,"CLI opt in");
        std::cout << "Ground shadow contract, lifecycle, observation-only and CLI tests passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
