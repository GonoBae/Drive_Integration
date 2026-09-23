#include "simulation_host.hpp"
#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Harness {
    boost::asio::io_context ioc;
    SimulationHost host;
    std::uint64_t sequence = 0;
    std::string session = "garage-socket-1";
    std::uint64_t generation = 1;

    static SimulationHostConfig config(bool require_catalog = true)
    {
        SimulationHostConfig config;
        config.source_id = "garage-host-test";
        config.map_package_checksum = "test-map";
        config.physics_frequency_hz = 200;
        config.command_timeout = std::chrono::seconds(3);
        config.hard_command_timeout = std::chrono::seconds(8);
        config.ground_query = std::make_shared<const simcore_host::FlatGroundQuery>();
        config.collision_world = std::make_shared<const simcore_host::CollisionWorld>();
        auto network = std::make_shared<simcore_host::TrafficNetwork>();
        network->source_map_checksum = "test-map";
        network->checksum = "garage-test-road";
        network->lanes = {{10, 4, 8, 0, true,
            {{100, 0, 0}, {100, 100, 0}, {100, 200, 0}, {100, 300, 0}, {100, 400, 0}}, {}}};
        config.traffic_network = network;
        config.npc_route = {10};
        config.npc_count = 4;
        config.npc_spacing_m = 80;
        config.require_client_hello = true;
        config.require_vehicle_catalog_identity = require_catalog;
        return config;
    }

    explicit Harness(bool require_catalog = true) : host(ioc, config(require_catalog), {}) {}

    simcore::Envelope envelope()
    {
        simcore::Envelope e;
        e.set_schema_version(2);
        e.set_sequence(++sequence);
        e.set_source_id("garage-test");
        e.set_session_id(session);
        e.set_map_package_checksum("test-map");
        return e;
    }

    static std::uint64_t now()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            SimulationHost::Clock::now().time_since_epoch()).count();
    }

    ClientMessageResult negotiate(bool supports_loadouts, bool supports_parts, bool include_catalog)
    {
        auto e = envelope();
        auto* hello = e.mutable_hello();
        hello->set_build("garage-test");
        hello->set_schema("simcore-envelope-v2");
        for (const auto* capability : {"world-state.v2", "control.v2", "simulation-reset.v1", "map-package-checksum.v1"})
            hello->add_capabilities(capability);
        if (include_catalog) hello->add_capabilities("vehicle-catalog-fnv1a64-"
            + simcore_host::default_runtime_vehicle_catalog().checksum().substr(8));
        if (supports_loadouts) hello->add_capabilities("vehicle-loadout.v1");
        if (supports_parts) hello->add_capabilities("vehicle-parts.v1");
        return host.handle_client_message(e.SerializeAsString(), generation);
    }

    void hello(bool supports_loadouts = true, bool supports_parts = false)
    {
        require(negotiate(supports_loadouts, supports_parts, true) == ClientMessageResult::HelloAccepted,
            "garage Hello rejected");
    }

    ClientMessageResult reset(const std::string& play, const std::string& loadout,
        simcore::RuntimeVehicleClass kind = simcore::RUNTIME_VEHICLE_CLASS_SEDAN)
    {
        auto e = envelope();
        auto* reset = e.mutable_simulation_reset();
        reset->set_play_session_id(play);
        reset->set_client_time_ns(now());
        reset->set_requested_vehicle_class(kind);
        reset->set_requested_loadout_id(loadout);
        return host.handle_client_message(e.SerializeAsString(), generation);
    }

    void control(float throttle, float brake = 0)
    {
        auto e = envelope();
        auto* command = e.mutable_control_command();
        command->set_throttle(throttle);
        command->set_brake(brake);
        command->set_client_time_ns(now());
        require(host.handle_client_message(e.SerializeAsString(), generation) == ClientMessageResult::ControlAccepted,
            "valid controller was poisoned by rejected garage request");
    }

    void tick()
    {
        host.start();
        require(ioc.run_one() == 1, "garage host tick did not execute");
        host.stop(); ioc.poll(); ioc.restart();
    }

    simcore::Envelope world()
    {
        simcore::Envelope result;
        require(result.ParseFromString(host.make_initial_world_state()), "garage world did not parse");
        return result;
    }
};

void test_selection_lifecycle_and_npc_identity()
{
    Harness h;
    h.hello();
    for (const auto& id : {std::string("missing"), std::string("../escape"), std::string(65, 'a')})
        require(h.reset("garage-play-1", id) == ClientMessageResult::Rejected, "invalid loadout accepted");
    require(h.reset("garage-play-1", "sedan_modular_standard", simcore::RUNTIME_VEHICLE_CLASS_TRUCK)
        == ClientMessageResult::Rejected, "cross-class loadout accepted");
    require(h.reset("garage-play-1", "sedan_modular_standard") == ClientMessageResult::SimulationReset,
        "stopped valid loadout must reset");
    auto world = h.world();
    require(world.world_state().entities(0).vehicle_loadout_id() == "sedan_modular_standard"
        && std::abs(world.world_state().entities(0).fuel() - 70.f) < 0.001f,
        "Ego must publish acknowledged loadout and initial fuel");
    int npcs = 0;
    for (const auto& entity : world.world_state().entities()) {
        if (entity.entity_kind() != simcore::ENTITY_KIND_NPC_VEHICLE) continue;
        ++npcs;
        if (entity.runtime_vehicle_class() == simcore::RUNTIME_VEHICLE_CLASS_SEDAN)
            require(entity.vehicle_loadout_id() == "sedan_modular_standard"
                && std::abs(entity.fuel() - 70.f) < 0.001f, "same-class NPC must use shared loadout");
        else require(entity.vehicle_loadout_id().empty(), "loadout leaked to another NPC class");
    }
    require(npcs == 4, "mixed fleet fixture must publish all four classes");
    require(h.reset("garage-play-1", "sedan_modular_comfort") == ClientMessageResult::Rejected,
        "same Play ID must not change parts");
    require(h.reset("garage-play-1", "sedan_modular_standard") == ClientMessageResult::DuplicateSimulationReset,
        "identical reset must remain idempotent");
    h.session = "garage-socket-2"; ++h.generation; h.sequence = 0; h.hello();
    require(h.reset("garage-play-1", "sedan_modular_comfort") == ClientMessageResult::Rejected,
        "reconnect must not change parts");
    require(h.reset("garage-play-1", "sedan_modular_standard") == ClientMessageResult::DuplicateSimulationReset,
        "same-loadout reconnect must preserve state");

    for (int i = 0; i < 400 && h.host.state().speed <= .3f; ++i) { h.control(1); h.tick(); }
    require(h.host.state().speed > .3f, "loadout did not accelerate enough to test moving rejection");
    const auto fuel = h.host.state().fuel;
    h.session = "garage-socket-3"; ++h.generation; h.sequence = 0; h.hello();
    require(h.reset("garage-play-2", "sedan_modular_comfort") == ClientMessageResult::Rejected,
        "moving loadout change accepted");
    require(h.reset("garage-play-1", "sedan_modular_standard") == ClientMessageResult::DuplicateSimulationReset
        && h.host.state().fuel == fuel, "reconnect reset modular fuel");
    for (int i = 0; i < 400 && std::abs(h.host.state().speed) > .01f; ++i) { h.control(0, 1); h.tick(); }
    require(std::abs(h.host.state().speed) <= .01f, "garage fixture failed to stop");
    h.session = "garage-socket-4"; ++h.generation; h.sequence = 0; h.hello();
    require(h.reset("garage-play-2", "sedan_modular_comfort") == ClientMessageResult::SimulationReset,
        "stopped new Play must accept new loadout");
    require(h.world().world_state().entities(0).vehicle_loadout_id() == "sedan_modular_comfort",
        "new loadout not acknowledged");
    h.session = "garage-socket-5"; ++h.generation; h.sequence = 0; h.hello();
    require(h.reset("garage-play-3", "") == ClientMessageResult::SimulationReset
        && h.world().world_state().entities(0).vehicle_loadout_id().empty()
        && h.host.state().fuel == 100.f, "default selection must restore legacy parameters");
}

void test_negotiation_required_for_nondefault_loadout()
{
    Harness h;
    h.hello(false);
    require(h.reset("legacy-play", "sedan_modular_standard") == ClientMessageResult::Rejected,
        "nondefault loadout needs negotiated capability");
    require(h.reset("legacy-play", "") == ClientMessageResult::SimulationReset,
        "legacy default reset must remain accepted");
}

void test_custom_parts_lifecycle()
{
    constexpr auto custom = "parts_v1_01_0504070401080002";
    Harness h;
    h.hello(true, true);
    for (const auto* invalid : {"parts_v1_01_0504070401ff0002", "parts_v1_01_0504070401080009"})
        require(h.reset("parts-play-1", invalid) == ClientMessageResult::Rejected,
            "invalid individual part selection accepted");
    require(h.reset("parts-play-1", custom, simcore::RUNTIME_VEHICLE_CLASS_TRUCK) == ClientMessageResult::Rejected,
        "custom parts crossed the base preset class");
    require(h.reset("parts-play-1", custom) == ClientMessageResult::SimulationReset,
        "valid independent front tire selection must apply");
    auto world = h.world();
    require(world.world_state().entities(0).vehicle_loadout_id() == custom,
        "custom parts were not acknowledged by the server");
    for (const auto& entity : world.world_state().entities()) {
        if (entity.entity_kind() != simcore::ENTITY_KIND_NPC_VEHICLE) continue;
        require(entity.vehicle_loadout_id() == (entity.runtime_vehicle_class() == simcore::RUNTIME_VEHICLE_CLASS_SEDAN
            ? custom : ""), "individual parts did not follow same-class NPC sharing");
    }
    for (int tick = 0; tick < 400 && h.host.state().speed <= .3f; ++tick) { h.control(1); h.tick(); }
    require(h.host.state().speed > .3f, "custom parts did not drive the player");
    const float fuel = h.host.state().fuel;
    h.session = "parts-socket-2"; ++h.generation; h.sequence = 0; h.hello(true, true);
    require(h.reset("parts-play-2", "sedan_modular_standard") == ClientMessageResult::Rejected,
        "moving custom-to-preset change accepted");
    require(h.reset("parts-play-1", custom) == ClientMessageResult::DuplicateSimulationReset
        && h.host.state().fuel == fuel, "custom reconnect refilled fuel or lost identity");
    require(h.reset("parts-play-1", "sedan_modular_standard") == ClientMessageResult::Rejected,
        "same-play custom parts were replaced");
    for (int tick = 0; tick < 400 && std::abs(h.host.state().speed) > .01f; ++tick) { h.control(0, 1); h.tick(); }
    h.session = "parts-socket-3"; ++h.generation; h.sequence = 0; h.hello(true, true);
    require(h.reset("parts-play-2", "sedan_modular_standard") == ClientMessageResult::SimulationReset,
        "stopped custom-to-preset restore failed");
}

void test_custom_parts_require_capability_and_catalog()
{
    constexpr auto custom = "parts_v1_01_0504070401080002";
    Harness old_client;
    old_client.hello();
    require(old_client.reset("parts-play", custom) == ClientMessageResult::Rejected,
        "loadout capability alone enabled individual parts");
    require(old_client.reset("parts-play", "sedan_modular_standard") == ClientMessageResult::SimulationReset,
        "rejected custom request poisoned ordinary preset selection");
    Harness optional_catalog(false);
    require(optional_catalog.negotiate(true, true, false) == ClientMessageResult::Rejected,
        "custom capability without exact catalog identity was accepted");
    require(optional_catalog.negotiate(false, true, true) == ClientMessageResult::Rejected,
        "custom capability omitted its base loadout capability");
    optional_catalog.hello(true, true);
    require(optional_catalog.reset("parts-play", custom) == ClientMessageResult::SimulationReset,
        "matching catalog should permit individual parts even with legacy config");
}
}

int main()
{
    try {
        test_selection_lifecycle_and_npc_identity();
        test_negotiation_required_for_nondefault_loadout();
        test_custom_parts_lifecycle();
        test_custom_parts_require_capability_and_catalog();
        std::cout << "vehicle_loadout_host_tests: lifecycle, safety, NPC sharing and compatibility passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
