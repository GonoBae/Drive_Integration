#pragma once

#include "control/control_lease.hpp"
#include "physics/vehicle_physics.hpp"
#include "protocol/vehicle_messages.hpp"
#include "simulation_clock.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

struct SimulationHostConfig {
    double origin_lat = 0.0;
    double origin_lon = 0.0;
    double origin_alt = 0.0;
    float spawn_heading = 0.f;
    double physics_frequency_hz = 60.0;
    std::chrono::nanoseconds command_timeout{250'000'000};
    std::chrono::nanoseconds max_command_queue_age{100'000'000};
    std::string source_id;
    std::string map_package_checksum;
};

struct SimulationHostCallbacks {
    std::function<void(const std::string&)> broadcast_world_state;
    std::function<void(const std::string&)> publish_observer_state;
    std::function<void(const std::string&)> close_control_connections;
};

enum class ControlMessageResult {
    Accepted,
    Rejected,
    EmergencyStopLatched,
};

// Application-level simulation coordinator. Network transports are injected as
// callbacks, so command arbitration and the fixed-step loop can be exercised
// without opening WebSocket or ZMQ sockets.
class SimulationHost {
public:
    using Clock = SimulationClock::Clock;

    SimulationHost(boost::asio::io_context& ioc,
                   SimulationHostConfig config,
                   SimulationHostCallbacks callbacks);
    ~SimulationHost();

    SimulationHost(const SimulationHost&) = delete;
    SimulationHost& operator=(const SimulationHost&) = delete;

    void start();
    void stop();
    ControlMessageResult handle_control_message(
        std::string_view message,
        Clock::time_point received_at = Clock::now());
    std::string make_initial_world_state();

    VehicleState state() const { return physics_.get_state(); }
    bool running() const { return running_; }

private:
    static VehicleInput make_safe_stop_input();
    simcore_host::EnvelopeMetadata make_metadata();
    void schedule_tick();
    void run_tick();

    SimulationHostConfig config_;
    SimulationHostCallbacks callbacks_;
    VehiclePhysics physics_;
    SimulationClock simulation_clock_;
    ControlLease control_lease_;
    boost::asio::steady_timer timer_;
    std::shared_ptr<int> lifetime_token_ = std::make_shared<int>(0);

    std::uint64_t message_sequence_ = 1;
    bool running_ = false;
    bool estop_latched_ = false;
    std::uint32_t last_reported_overrun_count_ = 0;
    VehicleInput last_logged_input_;
    Clock::time_point last_control_change_time_ = Clock::time_point::min();
    std::uint64_t control_change_revision_ = 0;
    std::uint64_t reported_control_change_revision_ = 0;
};
