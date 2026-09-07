#include "replay/physics_replay.hpp"
#include "runtime_options.hpp"
#include "simulation_host.hpp"
#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

template <typename Function> void rejects(Function function)
{
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid replay/options unexpectedly accepted");
}

struct Scratch {
    std::filesystem::path path = std::filesystem::temp_directory_path()
        / ("simcore-replay-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() { require(std::filesystem::create_directory(path), "scratch directory collision"); }
    ~Scratch() { std::error_code error; std::filesystem::remove_all(path, error); }
};

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    require(static_cast<bool>(file), "test output failed");
}

std::string resign_recording(std::string bytes)
{
    const auto footer = bytes.rfind("END ");
    if (footer == std::string::npos) return bytes;
    const auto checksum = bytes.find("fnv1a64:", footer);
    if (checksum == std::string::npos) return bytes;
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = 0; index < footer; ++index) {
        hash ^= static_cast<unsigned char>(bytes[index]);
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    bytes.replace(checksum, 24, output.str());
    return bytes;
}

std::string frame(std::uint64_t sequence, bool reset, float throttle = 0.5f,
                  bool estop = false, std::string play = "play-1",
                  simcore::RuntimeVehicleClass vehicle_class =
                      simcore::RUNTIME_VEHICLE_CLASS_UNSPECIFIED)
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(sequence);
    envelope.set_source_id("replay-test-client");
    envelope.set_session_id("connection-1");
    envelope.set_map_package_checksum("test-map");
    if (reset) {
        envelope.mutable_simulation_reset()->set_play_session_id(play);
        envelope.mutable_simulation_reset()->set_client_time_ns(sequence);
        envelope.mutable_simulation_reset()->set_requested_vehicle_class(vehicle_class);
    } else {
        auto* command = envelope.mutable_control_command();
        command->set_mode(estop ? simcore::CONTROL_MODE_ESTOP : simcore::CONTROL_MODE_MANUAL);
        command->set_throttle(throttle);
        command->set_steering(0.1f);
        command->set_estop(estop);
        command->set_client_time_ns(sequence);
    }
    return envelope.SerializeAsString();
}

void one_tick(boost::asio::io_context& ioc, SimulationHost& host)
{
    host.start();
    require(ioc.run_one() == 1, "host tick did not execute");
    host.stop();
    ioc.poll();
    ioc.restart();
}

void test_actual_host_and_strict_reader(const std::filesystem::path& directory)
{
    using namespace simcore_host;
    const PhysicsReplayIdentity identity{"test-build", "test-vehicle", "test-map", 0, 0, 0, 0, 1000};
    const auto path = directory / "host.replay";
    auto recorder = std::make_shared<PhysicsReplayRecorder>(path, identity, 80);
    SimulationHostConfig config;
    config.source_id = "replay-test-host";
    config.map_package_checksum = identity.map_checksum;
    config.physics_frequency_hz = identity.physics_hz;
    config.physics_replay = recorder;
    config.runtime_entities = {
        {1001, RuntimeEntityKind::NpcVehicle,
         {"test-npc", ObbPrism{{4, 5}, 0.75, 0, 2.2, 1, 0.75}, {0, 0.2},
          0.02, {0.9, 0.0}, 1500, 2850, 30}},
        {2001, RuntimeEntityKind::Pedestrian,
         {"test-ped", VerticalCapsule{{-3, 7}, 0.9, 0.35, 0.9}, {0.3, 0},
          0, {0.7, 0.0}, 75, 0, 15}}
    };
    boost::asio::io_context ioc;
    SimulationHost host(ioc, config, {});
    require(host.handle_client_message(frame(1, true)) == ClientMessageResult::SimulationReset,
            "initial reset rejected");
    std::uint64_t sequence = 2;
    for (int tick = 0; tick < 80; ++tick) {
        if (tick == 40) {
            require(host.handle_client_message(frame(sequence++, true, 0, false, "play-2",
                    simcore::RUNTIME_VEHICLE_CLASS_TRUCK))
                    == ClientMessageResult::SimulationReset, "second reset rejected");
        }
        if (tick < 75) {
            require(host.handle_client_message(frame(sequence++, false,
                    tick % 2 ? 0.5004f : 0.5f)) == ClientMessageResult::ControlAccepted,
                    "control rejected");
        } else if (tick == 75) {
            require(host.handle_client_message(frame(sequence++, false, 0, true))
                    == ClientMessageResult::EmergencyStopLatched, "E-stop rejected");
        }
        one_tick(ioc, host);
    }
    require(recorder->complete(), "bounded recording did not finalize");
    const auto result = verify_physics_replay(path, identity, config.vehicle_parameters, {}, {});
    require(result.frames == 80 && result.resets == 2 && result.events == 3
            && result.dynamic_proxy_frames == 80 && result.maximum_position_error_m == 0
            && result.maximum_yaw_error_deg == 0, "actual host replay differs");
    rejects([&] { PhysicsReplayRecorder duplicate(path, identity, 1); });
    for (int field = 0; field < 6; ++field) {
        auto changed = identity;
        if (field == 0) changed.executable_checksum += "-other";
        if (field == 1) changed.vehicle_checksum += "-other";
        if (field == 2) changed.map_checksum += "-other";
        if (field == 3) changed.physics_hz = 60;
        if (field == 4) changed.spawn_heading = 5;
        if (field == 5) changed.origin_alt = 1;
        rejects([&] { verify_physics_replay(path, changed, {}, {}, {}); });
    }
    const auto original = read_file(path);
    require(original.find("EVENT 0 RESET 1\n") != std::string::npos
            && original.find("EVENT 40 RESET 3\n") != std::string::npos,
            "RESET records must preserve each authoritative vehicle class");
    auto legacy = original;
    const auto legacy_reset = legacy.find("EVENT 0 RESET 1");
    require(legacy_reset != std::string::npos, "Sedan RESET record missing");
    legacy.replace(legacy_reset, std::string("EVENT 0 RESET 1").size(), "EVENT 0 RESET");
    for (auto begin = legacy.find(" CONTACT "); begin != std::string::npos; begin = legacy.find(" CONTACT ")) {
        legacy.erase(begin, legacy.find('\n', begin) - begin);
    }
    const auto legacy_path = directory / "legacy-v1-extensions.replay";
    write_file(legacy_path, resign_recording(legacy));
    require(verify_physics_replay(legacy_path, identity, config.vehicle_parameters, {}, {}).frames == 80,
        "legacy v1 RESET and proxy lines must retain Sedan/contact defaults");
    int corruption_index = 0;
    const auto corrupt = [&](std::string bytes, bool resign = true) {
        const auto corrupt_path = directory / ("corrupt-" + std::to_string(corruption_index++) + ".replay");
        // Signed malformed records must fail their own parser/domain guard,
        // not accidentally pass this negative test through checksum mismatch.
        write_file(corrupt_path, resign ? resign_recording(bytes) : bytes);
        rejects([&] { verify_physics_replay(corrupt_path, identity, {}, {}, {}); });
    };
    corrupt(original.substr(0, original.rfind("END ")));
    corrupt(original + "TRAILING\n");
    auto changed = original;
    changed.replace(changed.find("REPLAY 1"), 8, "REPLAY 2");
    corrupt(changed);
    changed = original;
    changed.replace(changed.find("RESET"), 5, "UNKNOWN");
    corrupt(changed);
    changed = original;
    const auto reset_class = changed.find("EVENT 0 RESET 1");
    require(reset_class != std::string::npos, "RESET class record missing");
    changed.replace(reset_class, std::string("EVENT 0 RESET 1").size(), "EVENT 0 RESET 5");
    corrupt(changed);
    changed = original;
    const auto truck_reset = changed.find("EVENT 40 RESET 3");
    require(truck_reset != std::string::npos, "Truck RESET class record missing");
    changed.replace(truck_reset, std::string("EVENT 40 RESET 3").size(), "EVENT 40 RESET 1");
    corrupt(changed);
    changed = original;
    changed.replace(changed.find("TICK 0"), 6, "TICK 2");
    corrupt(changed);
    changed = original;
    const auto state_begin = changed.find("STATE ") + 6;
    changed.replace(state_begin, changed.find(' ', state_begin) - state_begin, "nan");
    corrupt(changed);
    changed = original;
    changed.replace(state_begin, changed.find(' ', state_begin) - state_begin, "1000");
    corrupt(changed);
    changed = original;
    changed.replace(changed.find("PROXY test-npc B"), 16, "PROXY test-npc X");
    corrupt(changed);
    changed = original;
    const auto proxy_position = changed.find("PROXY test-npc B ") + 17;
    changed.replace(proxy_position, changed.find(' ', proxy_position) - proxy_position, "inf");
    corrupt(changed);
    changed = original;
    changed.replace(changed.find(" CONTACT 0 0 0"), 14, " CONTACT 2 0 0");
    corrupt(changed);
    changed = original;
    changed.replace(changed.find(" CONTACT 0 0 0"), 14, " CONTACT 0 -1 0");
    corrupt(changed);
    changed = original;
    changed.replace(changed.rfind("fnv1a64:") + 8, 16, "0000000000000000");
    corrupt(changed, false);
    corrupt(original.substr(0, original.find('\n') + 1) + std::string(5000, 'X') + "\n");
}

void test_recording_guards_and_cli(const std::filesystem::path& directory)
{
    using namespace simcore_host;
    PhysicsReplayIdentity identity{"build", "vehicle", "map"};
    const auto path = directory / "invalidated.replay";
    PhysicsReplayRecorder recorder(path, identity, 2);
    VehiclePhysics physics(0, 0, 0, 0);
    VehicleInput input;
    input.throttle = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { recorder.tick(input, {}, physics.get_state()); });
    recorder.invalidate("map_package_reloaded");
    require(!recorder.valid() && !recorder.complete(), "invalidated recording accepted");
    rejects([&] { verify_physics_replay(path, identity, {}, {}, {}); });
    const auto limited_path = directory / "limited.replay";
    PhysicsReplayRecorder limited(limited_path, identity, 100, 2048);
    rejects([&] {
        for (int index = 0; index < 100; ++index) limited.tick({}, {}, physics.update(1.0 / 60));
    });
    require(!limited.valid() && !limited.complete()
            && std::filesystem::file_size(limited_path) <= 2048,
            "writer exceeded reader cap or finalized a partial frame");
    rejects([&] { verify_physics_replay(limited_path, identity, {}, {}, {}); });
    const auto defaults = make_runtime_option_defaults("vehicle.cfg", "map");
    const auto parsed = parse_runtime_options({"--record-physics", "new.replay", "--record-ticks", "120"}, defaults);
    require(parsed.record_ticks == 120 && parsed.record_physics_path == "new.replay",
            "capture CLI parsing failed");
    for (const std::vector<std::string>& options : {
            std::vector<std::string>{"--record-ticks", "120"},
            {"--record-physics", "new", "--record-ticks", "0"},
            {"--record-physics", "new", "--record-ticks", "216001"},
            {"--record-physics", "new", "--record-ticks", "-1"},
            {"--record-physics", "new", "--verify-physics-replay", "old"}}) {
        rejects([&] { parse_runtime_options(options, defaults); });
    }
}

void test_contact_policy_round_trip(const std::filesystem::path& directory)
{
    using namespace simcore_host;
    PhysicsReplayIdentity identity{"build", "vehicle", "map"};
    const auto path = directory / "contact-policy.replay";
    PhysicsReplayRecorder recorder(path, identity, 3);
    VehiclePhysics physics(0, 0, 0, 0);
    KinematicCollisionProxy proxy{"fallen-pole", ObbPrism{{40, 40}, .25, 0, 1.9, .25, .25}, {}, 0,
        {.65, .05}, 180, 230, 60};
    proxy.tire_support_candidate = true;
    proxy.breakaway_released = true;
    proxy.supported_vehicle_id = "ego";
    for (int tick = 0; tick < 3; ++tick) {
        if (tick == 1) {
            proxy.tire_support_candidate = false;
            proxy.breakaway_released = false;
            proxy.breakaway_impulse_n_s = 1250;
            proxy.supported_vehicle_id.clear();
        }
        recorder.tick({}, {proxy}, physics.update(1.0 / 60, {proxy}));
    }
    require(recorder.complete() && verify_physics_replay(path, identity, {}, {}, {}).frames == 3,
        "finite support and remaining breakaway resistance must round-trip");
    const auto content = read_file(path);
    require(content.find(" CONTACT 1 0 1 \"ego\"") != std::string::npos
        && content.find(" CONTACT 0 1250 0 \"\"") != std::string::npos,
        "recording must capture all contact policy fields instead of silently dropping them");
}
} // namespace

int main()
{
    try {
        Scratch scratch;
        test_actual_host_and_strict_reader(scratch.path);
        test_recording_guards_and_cli(scratch.path);
        test_contact_policy_round_trip(scratch.path);
        std::cout << "physics replay tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
