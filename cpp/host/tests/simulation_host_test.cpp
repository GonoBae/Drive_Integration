#include "simulation_host.hpp"

#include "vehicle.pb.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string make_control_message()
{
    simcore::Envelope envelope;
    envelope.set_schema_version(simcore_host::kProtocolSchemaVersion);
    envelope.set_sequence(1);
    envelope.set_source_id("unreal-test");
    envelope.set_session_id("session-1");
    envelope.set_map_package_checksum("test-map");
    auto* command = envelope.mutable_control_command();
    command->set_mode(simcore::CONTROL_MODE_MANUAL);
    command->set_throttle(0.5f);
    command->set_client_time_ns(1);
    return envelope.SerializeAsString();
}

} // namespace

int main()
{
    using namespace std::chrono_literals;
    boost::asio::io_context ioc;
    int world_messages = 0;
    int observer_messages = 0;
    int close_requests = 0;
    std::string last_world_message;

    SimulationHost host(
        ioc,
        {
            40.7069, -74.0095, 0.0, 0.f, 1000.0,
            1s, 100ms, "host-test", "test-map",
        },
        {
            [&](const std::string& message) {
                ++world_messages;
                last_world_message = message;
            },
            [&](const std::string&) { ++observer_messages; },
            [&](const std::string&) { ++close_requests; },
        });

    simcore::Envelope initial;
    require(initial.ParseFromString(host.make_initial_world_state()),
            "initial state envelope must parse");
    require(initial.world_state().entities(0).wheels_size() == 4,
            "initial state must expose all four wheels");
    require(host.handle_control_message(make_control_message())
                == ControlMessageResult::Accepted,
            "valid control message must be accepted by the host core");

    boost::asio::steady_timer stopper(ioc);
    stopper.expires_after(8ms);
    stopper.async_wait([&](const boost::system::error_code&) { host.stop(); });
    host.start();
    ioc.run();

    require(!host.running(), "stop must terminate the fixed-step loop");
    require(world_messages > 0, "fixed-step loop must publish world state");
    require(observer_messages == world_messages,
            "each tick must publish matching direct and observer state");
    require(close_requests == 0, "fresh control must not close connections");

    simcore::Envelope world;
    require(world.ParseFromString(last_world_message) && world.has_world_state(),
            "published world message must be a protobuf WorldState envelope");
    std::cout << "simulation_host_tests: all tests passed\n";
    return 0;
}
