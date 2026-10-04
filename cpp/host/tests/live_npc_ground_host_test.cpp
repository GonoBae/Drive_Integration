#include "simulation_host.hpp"
#include "vehicle.pb.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace simcore_host;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

class FileGroundCounter final : public GroundQuery {
public:
    mutable unsigned queries = 0;
    std::optional<GroundHit> query_down(const GroundQueryRequest& query) const override {
        ++queries;
        return FlatGroundQuery{}.query_down(query);
    }
};

SimulationHostConfig configuration(const std::shared_ptr<FileGroundCounter>& file, bool pedestrians) {
    SimulationHostConfig config;
    config.source_id = "npc-live-test";
    config.map_package_checksum = "map";
    config.require_client_hello = true;
    config.live_ground = true;
    config.live_npc_ground = true;
    config.ground_query = file;
    config.collision_world = std::make_shared<CollisionWorld>();
    config.physics_frequency_hz = 120;
    auto network = std::make_shared<TrafficNetwork>();
    network->source_map_checksum = "map";
    network->checksum = "fnv1a64:0000000000000001";
    TrafficLane first;
    first.id = 10; first.width_m = 4; first.speed_limit_mps = 6;
    first.points = {{20,0,0},{70,0,0}}; first.successors = {11};
    TrafficLane second;
    second.id = 11; second.width_m = 4; second.speed_limit_mps = 6;
    second.points = {{70,0,0},{70,50,0}}; second.successors = {12};
    TrafficLane third;
    third.id = 12; third.width_m = 4; third.speed_limit_mps = 6;
    third.points = {{70,50,0},{20,50,0}}; third.successors = {13};
    TrafficLane fourth;
    fourth.id = 13; fourth.width_m = 4; fourth.speed_limit_mps = 6;
    fourth.points = {{20,50,0},{20,0,0}}; fourth.successors = {10};
    network->lanes = {first,second,third,fourth};
    if (pedestrians) {
        network->signals = {
            {100,1,{35,12,0},0,1,TrafficSignalKind::Pedestrian},
            {101,1,{45,12,0},180,1,TrafficSignalKind::Pedestrian}};
    }
    config.traffic_network = network;
    config.npc_route = {10,11,12,13};
    config.npc_route_loop = true;
    config.npc_start_offset_m = 10;
    config.npc_count = 1;
    config.npc_autonomous = true;
    return config;
}

struct Harness {
    boost::asio::io_context ioc;
    std::shared_ptr<FileGroundCounter> file = std::make_shared<FileGroundCounter>();
    SimulationHost host;
    std::uint64_t sequence = 0;
    std::uint64_t revision = 1;
    bool measurements_ready = true;
    double npc_height = .12;
    bool npc_no_hit = false;
    unsigned npc_replies = 0;
    unsigned ego_replies = 0;
    bool fenced_reply_checked = false;

    explicit Harness(bool pedestrians = true) : host(ioc,configuration(file,pedestrians),{}) {}

    simcore::Envelope envelope() {
        simcore::Envelope value;
        value.set_schema_version(2); value.set_sequence(++sequence);
        value.set_source_id("ue"); value.set_session_id("connection");
        value.set_play_session_id("play"); value.set_map_package_checksum("map");
        return value;
    }
    ClientMessageResult send(const simcore::Envelope& value) {
        return host.handle_client_message(value.SerializeAsString(),1);
    }
    void connect() {
        require(host.npc_navigation().size() == 1,
            "unmeasured autonomous NPC must retain its route descriptor at construction");
        require(host.runtime_entities().empty(),"no NPC or pedestrian may spawn on file ground before measurement");
        auto hello = envelope();
        auto* value = hello.mutable_hello();
        value->set_build("test"); value->set_schema("simcore-envelope-v2");
        for (const auto* capability : {"world-state.v2","control.v2","simulation-reset.v1",
                "map-package-checksum.v1","ground-patch.v3"}) value->add_capabilities(capability);
        require(send(hello) == ClientMessageResult::Rejected,"NPC live mode requires explicit client capability");
        value->add_capabilities("npc-ground.v1");
        require(send(hello) == ClientMessageResult::HelloAccepted,"NPC ground capability negotiated");
        auto reset = envelope();
        reset.mutable_simulation_reset()->set_play_session_id("play");
        reset.mutable_simulation_reset()->set_client_time_ns(1);
        require(send(reset) == ClientMessageResult::SimulationReset,"live session reset accepted");
        require(host.npc_navigation().size() == 1,"reset must not erase pending NPC route");
        notice();
    }
    void notice() {
        auto message = envelope();
        message.mutable_ground_world_revision()->set_world_revision(revision);
        message.mutable_ground_world_revision()->set_measurements_ready(measurements_ready);
        require(send(message) == ClientMessageResult::GroundWorldRevisionAccepted,"world notice accepted");
    }
    void control() {
        auto message = envelope();
        auto* command = message.mutable_control_command();
        command->set_mode(simcore::CONTROL_MODE_MANUAL);
        command->set_brake(1);
        command->set_client_time_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
            SimulationHost::Clock::now().time_since_epoch()).count());
        require(send(message) == ClientMessageResult::ControlAccepted,"fresh command retains active control");
    }
    simcore::Envelope world() {
        simcore::Envelope message;
        require(message.ParseFromString(host.make_initial_world_state()) && message.has_world_state(),
            "host emits parseable world state");
        return message;
    }
    void reply(const simcore::GroundQueryBatch& query, bool npc) {
        auto message = envelope();
        auto* result = npc ? message.mutable_npc_ground_tile_result() : message.mutable_live_ground_patch_result();
        result->set_request_id(query.request_id());
        result->set_environment_epoch(query.environment_epoch());
        result->set_reference_simulation_time_ns(query.reference_simulation_time_ns());
        result->set_world_revision(query.world_revision());
        require(query.origins_enu_size() == 1 && query.has_patch_step_enu(),"bounded tiled request shape");
        for (int index = 0; index < kGroundPatchSamples; ++index) {
            auto* sample = result->add_probes();
            if (npc && npc_no_hit) { sample->set_status(simcore::GroundProbeResult::NO_HIT); continue; }
            sample->set_status(simcore::GroundProbeResult::HIT);
            sample->mutable_point_enu()->set_x(query.origins_enu(0).x() + (index%25-12)*query.patch_step_enu().x());
            sample->mutable_point_enu()->set_y(query.origins_enu(0).y() + (index/25-12)*query.patch_step_enu().y());
            sample->mutable_point_enu()->set_z(npc ? npc_height : .75);
            sample->mutable_normal_enu()->set_z(1);
            sample->set_surface_material_id(1);
        }
        if (npc && !fenced_reply_checked) {
            auto wrong = message; wrong.set_session_id("old-connection");
            require(send(wrong) == ClientMessageResult::Rejected,"old connection cannot populate NPC cache");
            wrong = message; wrong.mutable_npc_ground_tile_result()->set_world_revision(query.world_revision()+1);
            require(send(wrong) == ClientMessageResult::Rejected,"NPC cache rejects a different world revision");
            fenced_reply_checked = true;
        }
        require(message.ByteSizeLong() < 64*1024,"NPC tile obeys transport frame budget");
        require(send(message) == ClientMessageResult::GroundQueryResultAccepted,"measured tile accepted over actual envelope path");
        if (npc) ++npc_replies; else ++ego_replies;
    }
    void tick(bool heartbeat = true, bool answer_npc = true) {
        if (heartbeat) notice();
        control();
        const auto pending = world();
        if (measurements_ready && pending.world_state().has_live_ground_patch())
            reply(pending.world_state().live_ground_patch(),false);
        if (measurements_ready && answer_npc && pending.world_state().has_npc_ground_tile())
            reply(pending.world_state().npc_ground_tile(),true);
        host.start();
        require(ioc.run_one() == 1,"fixed host timer runs one tick");
        host.stop();
        (void)ioc.poll(); ioc.restart();
    }
    void ticks(unsigned count, bool heartbeat = true, bool answer_npc = true) {
        for (unsigned index=0; index<count; ++index) tick(heartbeat,answer_npc);
    }
    NpcNavigationSnapshot navigation() const {
        const auto states = host.npc_navigation();
        require(states.size() == 1,"configured NPC route identity remains present");
        return states.front();
    }
    const RuntimeEntityState& npc() const {
        const auto& entities = host.runtime_entities();
        const auto found = std::find_if(entities.begin(),entities.end(),[](const auto& value) { return value.entity_id == 1001; });
        require(found != entities.end(),"NPC must appear after measured spawn completes");
        return *found;
    }
    std::vector<double> positions() const {
        std::vector<double> values;
        for (const auto& entity : host.runtime_entities()) {
            values.push_back(entity.entity_id);
            std::visit([&](const auto& shape) {
                values.insert(values.end(),{shape.center_enu.east_m,shape.center_enu.north_m,shape.center_up_m});
            },entity.collision_proxy.shape);
        }
        return values;
    }
};

void startup_and_live_lifecycle() {
    Harness test;
    test.connect();
    auto stale_notice = test.envelope();
    stale_notice.mutable_ground_world_revision()->set_world_revision(2);
    stale_notice.mutable_ground_world_revision()->set_measurements_ready(true);
    require(test.host.handle_client_message(stale_notice.SerializeAsString(),1,
            SimulationHost::Clock::now()-std::chrono::seconds(1)) == ClientMessageResult::Rejected,
        "out-of-order receipt timestamp must reject revision before any provider mutation");
    require(test.world().world_state().live_ground_patch().world_revision() == 1,
        "rejected NPC notice cannot leave Ego provider on a different world revision");
    test.notice();
    test.ticks(8,true,false);
    require(test.host.runtime_entities().empty(),"missing tile replies cannot fabricate NPC spawn height");
    require(test.navigation().route_distance_travelled_m == 0,"pending spawn cannot consume route progress");
    test.ticks(100);
    require(test.host.runtime_entities().size() == 3,"one NPC and both pedestrian scaffolds become measured entities");
    const auto body = std::get<ObbPrism>(test.npc().collision_proxy.shape);
    require(body.center_up_m-body.half_height_m > .15,
        "NPC body height must use measured .12m surface, not authored/file zero");
    require(test.navigation().route_distance_travelled_m > .1,"measured NPC must actually drive after startup");
    require(test.navigation().destinations_selected > 0,"autonomous destination selection resumes after measured spawn");
    require(test.file->queries == 0,"opt-in NPC and pedestrian ground must never fall back to map-package queries");

    // Ego replies and controls continue: only the world heartbeat is lost.
    test.ticks(52,false,false);
    const auto frozen = test.positions();
    const auto progress = test.navigation().route_distance_travelled_m;
    const auto player = test.host.state();
    test.ticks(12,false,false);
    require(test.positions() == frozen && test.navigation().route_distance_travelled_m == progress,
        "expired world lease preserves existing NPC/pedestrian poses and controller progress");
    require(test.host.state().position_enu.x == player.position_enu.x
        && test.host.state().position_enu.y == player.position_enu.y
        && test.host.state().position_enu.z == player.position_enu.z,
        "post-contact NPC unknown rolls the complete player tick back");
    const auto replies = test.npc_replies;
    test.ticks(90);
    require(test.npc_replies > replies && test.navigation().route_distance_travelled_m > progress,
        "new heartbeat requires fresh tiles, then resumes existing traffic without respawn");
    require(test.host.runtime_entities().size() == 3,"measurement outage cannot delete traffic entities");

    ++test.revision; test.measurements_ready = false; test.notice();
    const auto before_edit = test.positions();
    test.ticks(8);
    require(test.positions() == before_edit,"unready revision holds existing traffic exactly");
    require(!test.world().world_state().has_npc_ground_tile(),"unready world dispatches no NPC tile requests");
    ++test.revision; test.measurements_ready = true; test.npc_height = .18; test.notice();
    test.ticks(90);
    const auto raised = std::get<ObbPrism>(test.npc().collision_proxy.shape);
    require(raised.center_up_m-body.center_up_m > .04,
        "new revision adopts edited Unreal height rather than retaining stale tile geometry");
    require(test.file->queries == 0,"revisions cannot re-enable a hidden file fallback");
}

void measured_no_hit_is_not_unknown() {
    Harness test(false);
    test.npc_no_hit = true;
    test.connect();
    test.ticks(45);
    require(test.host.runtime_entities().empty(),"measured unsupported NPC spawn remains unspawned");
    require(test.navigation().lane_change_wait_reason == "spawn_no_ground",
        "known missing ground has an explicit diagnostic instead of indefinite pending measurement");
    require(test.host.state().position_enu.z > 1.2,
        "known NPC NoHit must not hold all player physics in an initialization deadlock");
    require(test.file->queries == 0,"known NoHit cannot substitute file ground");
}

void deferred_loader_preserves_topology_validation() {
    struct Fixture {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("simcore-live-npc-topology-" + std::to_string(SimulationHost::Clock::now().time_since_epoch().count()) + ".json");
        ~Fixture() { std::error_code ignored; std::filesystem::remove(path,ignored); }
        void write(bool broken) const {
            std::ofstream stream(path,std::ios::binary);
            stream << R"({"format_version":1,"source_map_checksum":"fnv1a64:0000000000000001","signals":[],"lanes":[)"
                << R"({"id":10,"width_m":4,"speed_limit_mps":6,"signal_group_id":0,"points":[[0,0,0],[50,0,0]],)"
                << (broken ? R"("terminal":false,"successors":[999])" : R"("terminal":true,"successors":[])" )
                << "}]}";
            require(bool(stream),"temporary topology fixture written");
        }
    } fixture;
    FileGroundCounter file;
    fixture.write(false);
    constexpr const char* checksum = "fnv1a64:0000000000000001";
    const auto deferred = load_traffic_network(fixture.path,checksum,file,TrafficGroundValidation::DeferredLive);
    require(deferred.lanes.size() == 1 && file.queries == 0,
        "deferred live topology loader must not validate route height against file ground");
    (void)load_traffic_network(fixture.path,checksum,file);
    require(file.queries > 0,"existing default loader retains immediate support validation");
    file.queries = 0;
    fixture.write(true);
    bool rejected = false;
    try { (void)load_traffic_network(fixture.path,checksum,file,TrafficGroundValidation::DeferredLive); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected && file.queries == 0,
        "deferred support must not disable malformed successor/topology checks");
}
void measured_unreal_fixture(const std::filesystem::path& directory) {
    const auto read = [&](const char* name) {
        std::ifstream stream(directory/name,std::ios::binary);
        require(bool(stream),"captured Unreal NPC wire fixture exists");
        return std::string(std::istreambuf_iterator<char>(stream),{});
    };
    simcore::Envelope world;
    require(world.ParseFromString(read("last-npc-request.bin")) && world.world_state().has_npc_ground_tile(),
        "captured NPC request decodes");
    const auto& request = world.world_state().npc_ground_tile();
    const auto parsed = parse_client_message_envelope(read("last-npc-result.bin"));
    require(parsed && std::holds_alternative<ParsedGroundQueryResult>(*parsed),"captured Unreal NPC result decodes");
    auto result = std::get<ParsedGroundQueryResult>(*parsed).result;
    require(result.request_id == request.request_id() && result.world_revision == request.world_revision(),
        "captured request and result form a matching pair");
    LiveGroundPatch patch;
    patch.reset_context("unreal-fixture"); patch.observe_world_revision(request.world_revision(),true);
    const auto now = SimulationHost::Clock::now();
    const auto local = *patch.prepare({request.origins_enu(0).x(),request.origins_enu(0).y(),
        request.origins_enu(0).z()-8},request.reference_simulation_time_ns(),now);
    result.request_id = local.request_id; result.environment_epoch = local.environment_epoch;
    const bool accepted = patch.accept(result,now);
    require(accepted,patch.last_rejection_reason().c_str());
    std::cout << "actual Unreal NPC tile accepted: samples=" << result.probes.size()
        << " boundaries=" << result.boundaries.size() << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--npc-fixture") {
            measured_unreal_fixture(argv[2]);
            return 0;
        }
        startup_and_live_lifecycle();
        measured_no_hit_is_not_unknown();
        deferred_loader_preserves_topology_validation();
        std::cout << "live NPC ground host tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "live NPC ground host test failed: " << error.what() << '\n';
        return 1;
    }
}
