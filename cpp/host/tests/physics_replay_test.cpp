#include "replay/physics_replay.hpp"
#include "runtime_options.hpp"
#include "simulation_host.hpp"
#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#ifndef SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH
#error SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH must identify the shared runtime catalog
#endif

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
    // Commands are generated on the real host clock. A sequence number is not
    // a nanosecond timestamp, even when replay physics runs at a faster rate.
    const auto client_time_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            SimulationHost::Clock::now().time_since_epoch()).count());
    if (reset) {
        envelope.mutable_simulation_reset()->set_play_session_id(play);
        envelope.mutable_simulation_reset()->set_client_time_ns(client_time_ns);
        envelope.mutable_simulation_reset()->set_requested_vehicle_class(vehicle_class);
    } else {
        auto* command = envelope.mutable_control_command();
        command->set_mode(estop ? simcore::CONTROL_MODE_ESTOP : simcore::CONTROL_MODE_MANUAL);
        command->set_throttle(throttle);
        command->set_steering(0.1f);
        command->set_estop(estop);
        command->set_client_time_ns(client_time_ns);
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

void test_explicit_runtime_catalog_replay(const std::filesystem::path& directory)
{
    using namespace simcore_host;
    const auto source = std::filesystem::canonical(SIMCORE_TEST_RUNTIME_VEHICLE_CATALOG_PATH).parent_path();
    const auto fixture = directory / "runtime_catalog";
    std::filesystem::create_directory(fixture);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        if (!entry.is_regular_file()) continue;
        const auto target = fixture / entry.path().lexically_relative(source);
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(entry.path(), target);
    }
    const auto replace = [&](const char* relative, const std::string& before, const std::string& after) {
        const auto path = fixture / relative;
        auto bytes = read_file(path);
        const auto position = bytes.find(before);
        require(position != std::string::npos, "runtime catalog fixture mutation anchor missing");
        bytes.replace(position, before.size(), after);
        write_file(path, bytes);
    };
    // All four reset branches differ from the compiled default catalog. Replay
    // must use the same explicitly supplied immutable snapshot as the host.
    replace("profiles/sedan.json", "\"player_overrides\": {}", "\"player_overrides\": {\"mass_kg\": 1600}");
    replace("profiles/compact.json", "\"mass_kg\": 1050", "\"mass_kg\": 1125");
    replace("profiles/truck.json", "\"mass_kg\": 6200", "\"mass_kg\": 6300");
    replace("profiles/motorcycle.json", "\"mass_kg\": 240", "\"mass_kg\": 260");
    // Selected axle parts must survive reset and use the recorded catalog in
    // replay, not silently revert to the scalar compatibility parameters.
    replace("profiles/sedan.json", "\"tire\": null", "\"tire\": \"tire_sedan_comfort\"");
    replace("profiles/sedan.json", "\"suspension\": null", "\"suspension\": \"suspension_sedan_comfort\"");
    replace("profiles/sedan.json", "\"powertrain_modules\": null", R"("powertrain_modules": {
      "engine": "engine_sedan_gasoline_2_0",
      "transmission": "transmission_sedan_automatic_6speed",
      "drivetrain": "drivetrain_sedan_rwd",
      "fuel_tank": "fuel_tank_sedan_50l",
      "initial_fuel_l": 40,
      "upshift_rpm": 5200,
      "downshift_rpm": 1800,
      "shift_duration_s": 0.25
    })");
    const auto catalog = std::make_shared<const RuntimeVehicleCatalog>(
        load_runtime_vehicle_catalog(fixture / "catalog.json"));
    require(catalog->checksum() != default_runtime_vehicle_catalog().checksum(),
        "custom replay fixture must have a different catalog identity");
    const auto selected = catalog->player_parameters({}, RuntimeVehicleClass::Sedan);
    require(selected.front_axle_contact.tire && selected.front_axle_contact.suspension
            && !selected.rear_axle_contact.tire && !selected.rear_axle_contact.suspension,
        "replay fixture must select front modules without overwriting the rear axle");
    require(selected.powertrain.has_value(), "replay fixture must select the powertrain modules");
    const PhysicsReplayIdentity identity{"test-build", "test-vehicle+" + catalog->checksum(),
        "test-map", 0, 0, 0, 0, 1000};
    const auto path = directory / "runtime-catalog.replay";
    auto recorder = std::make_shared<PhysicsReplayRecorder>(path, identity, 40);
    SimulationHostConfig config;
    config.source_id = "catalog-replay-test-host";
    config.map_package_checksum = identity.map_checksum;
    config.physics_frequency_hz = identity.physics_hz;
    config.physics_replay = recorder;
    config.vehicle_catalog = catalog;
    boost::asio::io_context ioc;
    SimulationHost host(ioc, config, {});
    std::uint64_t sequence = 1;
    for (unsigned encoded_class = 1; encoded_class <= 4; ++encoded_class) {
        require(host.handle_client_message(frame(sequence++, true, 0, false,
                "catalog-play-" + std::to_string(encoded_class),
                static_cast<simcore::RuntimeVehicleClass>(encoded_class))) == ClientMessageResult::SimulationReset,
            "custom catalog reset rejected");
        for (int tick = 0; tick < 10; ++tick) {
            require(host.handle_client_message(frame(sequence++, false, 0.6f)) == ClientMessageResult::ControlAccepted,
                "custom catalog control rejected");
            one_tick(ioc, host);
        }
    }
    require(recorder->complete(), "custom catalog recording did not finalize");
    const auto result = verify_physics_replay(path, identity, config.vehicle_parameters, {}, {}, catalog.get());
    require(result.frames == 40 && result.resets == 4 && result.maximum_position_error_m == 0
        && result.maximum_yaw_error_deg == 0, "explicit catalog replay must match all four host reset profiles exactly");

    std::string mismatch;
    try {
        (void)verify_physics_replay(path, identity, config.vehicle_parameters, {}, {}, &default_runtime_vehicle_catalog());
    } catch (const std::exception& error) {
        mismatch = error.what();
    }
    require(mismatch.find("resolved runtime catalog does not match replay identity") != std::string::npos,
        "a different catalog must be rejected by identity before physics verification");

    // Run enough physical time for shifts and fuel use, independent of the
    // wall-clock scheduler. The second reset must discard internal RPM/gear/fuel history.
    auto driving_identity = identity;
    driving_identity.physics_hz = 60;
    const auto driving_path = directory / "powertrain-catalog.replay";
    PhysicsReplayRecorder driving_recorder(driving_path, driving_identity, 1200);
    VehiclePhysics physics(0, 0, 0, 0, selected);
    for (int tick = 0; tick < 1200; ++tick) {
        if (tick == 0 || tick == 600) {
            physics.replace_parameters(selected);
            driving_recorder.event(PhysicsReplayEvent::Reset, RuntimeVehicleClass::Sedan);
            require(std::abs(physics.get_state().fuel - 80.f) < 0.001f,
                "powertrain reset must restore initial litres as fuel percentage");
        }
        VehicleInput input;
        input.throttle = 0.9f;
        if (tick >= 600 && tick < 660) input.gear = VehicleGear::Neutral;
        else if (tick >= 660) input.gear = VehicleGear::Reverse;
        physics.set_input(input);
        driving_recorder.tick(input, {}, physics.update(1.0 / 60));
    }
    require(physics.get_state().fuel < 80.f, "selected engine must consume recorded fuel");
    const auto driving_result = verify_physics_replay(driving_path, driving_identity,
        config.vehicle_parameters, {}, {}, catalog.get());
    require(driving_result.frames == 1200 && driving_result.resets == 2
        && driving_result.maximum_position_error_m == 0 && driving_result.maximum_yaw_error_deg == 0,
        "powertrain shifts, reverse, fuel and reset must replay deterministically");
}
void test_named_loadout_replay(const std::filesystem::path& directory)
{
    using namespace simcore_host;
    const auto& catalog = default_runtime_vehicle_catalog();
    const PhysicsReplayIdentity identity{"test-build", "test-vehicle+" + catalog.checksum(),
        "test-map", 0, 0, 0, 0, 60};
    const auto path = directory / "named-loadouts.replay";
    PhysicsReplayRecorder recorder(path, identity, 600);
    VehiclePhysics physics(0, 0, 0, 0);
    for (int tick = 0; tick < 600; ++tick) {
        if (tick == 0 || tick == 300) {
            const auto* id = tick == 0 ? "sedan_modular_standard" : "sedan_modular_comfort";
            physics.replace_parameters(catalog.player_parameters({}, RuntimeVehicleClass::Sedan, id));
            recorder.event(PhysicsReplayEvent::Reset, RuntimeVehicleClass::Sedan, id);
        }
        VehicleInput input;
        input.throttle = .65f;
        input.steering = tick % 300 > 100 ? .05f : 0.f;
        physics.set_input(input);
        recorder.tick(input, {}, physics.update(1.0 / 60));
    }
    const auto result = verify_physics_replay(path, identity, {}, {}, {}, &catalog);
    require(result.frames == 600 && result.resets == 2 && result.maximum_position_error_m == 0
        && result.maximum_yaw_error_deg == 0, "named loadout reset history did not replay exactly");
    const auto bytes = read_file(path);
    require(bytes.find("RESET 1 LOADOUT sedan_modular_standard") != std::string::npos
        && bytes.find("RESET 1 LOADOUT sedan_modular_comfort") != std::string::npos,
        "replay must preserve actual loadout rather than only vehicle class");
    for (const auto* invalid : {"missing_loadout", "../escape", "sedan_modular_comfort trailing"}) {
        auto changed = bytes;
        const auto position = changed.find("sedan_modular_standard");
        changed.replace(position, std::string("sedan_modular_standard").size(), invalid);
        const auto invalid_path = directory / "invalid-loadout.replay";
        write_file(invalid_path, resign_recording(changed));
        rejects([&] { verify_physics_replay(invalid_path, identity, {}, {}, {}, &catalog); });
    }
}

void test_custom_parts_replay(const std::filesystem::path& directory)
{
    using namespace simcore_host;
    const auto& catalog = default_runtime_vehicle_catalog();
    constexpr auto custom = "parts_v1_01_0504070401080002";
    const PhysicsReplayIdentity identity{"test-build", "test-vehicle+" + catalog.checksum(),
        "test-map", 0, 0, 0, 0, 60};
    const auto path = directory / "custom-parts.replay";
    PhysicsReplayRecorder recorder(path, identity, 360);
    VehiclePhysics physics(0, 0, 0, 0);
    for (int tick = 0; tick < 360; ++tick) {
        if (tick == 0 || tick == 180) {
            const auto* id = tick == 0 ? custom : "sedan_modular_standard";
            physics.replace_parameters(catalog.player_parameters({}, RuntimeVehicleClass::Sedan, id));
            recorder.event(PhysicsReplayEvent::Reset, RuntimeVehicleClass::Sedan, id);
        }
        VehicleInput input;
        input.throttle = .6f;
        input.steering = tick % 180 > 70 ? .06f : 0.f;
        physics.set_input(input);
        recorder.tick(input, {}, physics.update(1.0 / 60));
    }
    for (const auto* selected_catalog : {&catalog, static_cast<const RuntimeVehicleCatalog*>(nullptr)}) {
        const auto result = verify_physics_replay(path, identity, {}, {}, {}, selected_catalog);
        require(result.frames == 360 && result.resets == 2 && result.maximum_position_error_m == 0
            && result.maximum_yaw_error_deg == 0, "individual part selection did not replay exactly");
    }
    auto legacy_identity = identity;
    legacy_identity.vehicle_checksum = "test-vehicle";
    auto bytes = read_file(path);
    const auto suffix = "+" + catalog.checksum();
    const auto suffix_at = bytes.find(suffix);
    require(suffix_at != std::string::npos, "custom replay catalog identity fixture missing");
    bytes.erase(suffix_at, suffix.size());
    const auto legacy_path = directory / "custom-parts-without-catalog.replay";
    write_file(legacy_path, resign_recording(bytes));
    rejects([&] { verify_physics_replay(legacy_path, legacy_identity, {}, {}, {}, nullptr); });
    auto wrong_identity = identity;
    wrong_identity.vehicle_checksum = "test-vehicle+fnv1a64:0000000000000000";
    bytes = read_file(path);
    bytes.replace(suffix_at, suffix.size(), "+fnv1a64:0000000000000000");
    const auto wrong_path = directory / "custom-parts-wrong-catalog.replay";
    write_file(wrong_path, resign_recording(bytes));
    rejects([&] { verify_physics_replay(wrong_path, wrong_identity, {}, {}, {}, nullptr); });
}
} // namespace

int main()
{
    try {
        Scratch scratch;
        test_actual_host_and_strict_reader(scratch.path);
        test_recording_guards_and_cli(scratch.path);
        test_contact_policy_round_trip(scratch.path);
        test_explicit_runtime_catalog_replay(scratch.path);
        test_named_loadout_replay(scratch.path);
        test_custom_parts_replay(scratch.path);
        std::cout << "physics replay tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
