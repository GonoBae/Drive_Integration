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
#include <unordered_set>
#include <vector>

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
    VehicleParameters vehicle_parameters;
    std::shared_ptr<const simcore_host::GroundQuery> ground_query;
    std::shared_ptr<const simcore_host::CollisionWorld> collision_world;
    // Empty by default. The command-line demo scenario opts in explicitly.
    std::vector<simcore_host::RuntimeEntityState> runtime_entities;
    // SafeStop is applied at command_timeout. The owner is retired and its
    // WebSocket is closed only at this later hard deadline.
    std::chrono::nanoseconds hard_command_timeout{1'000'000'000};
};

struct SimulationHostCallbacks {
    std::function<void(const std::string&)> broadcast_world_state;
    std::function<void(const std::string&)> publish_observer_state;
    std::function<void(const std::string&)> close_control_connections;
};

enum class ClientMessageResult {
    ControlAccepted,
    Rejected,
    EmergencyStopLatched,
    SimulationReset,
    DuplicateSimulationReset,
    Accepted = ControlAccepted,
};

using ControlMessageResult = ClientMessageResult;

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
    ClientMessageResult handle_client_message(
        std::string_view message,
        std::uint64_t connection_generation = 0,
        Clock::time_point received_at = Clock::now());
    ClientMessageResult handle_control_message(
        std::string_view message,
        Clock::time_point received_at = Clock::now())
    {
        return handle_client_message(message, 0, received_at);
    }
    std::string make_initial_world_state();

    VehicleState state() const { return physics_.get_state(); }
    const std::vector<simcore_host::RuntimeEntityState>& runtime_entities() const
    {
        return runtime_entities_;
    }
    bool running() const { return running_; }

private:
    static VehicleInput make_safe_stop_input();
    static std::string make_play_session_key(std::string_view source_id,
                                             std::string_view play_session_id);
    simcore_host::EnvelopeMetadata make_metadata();
    ClientMessageResult handle_control_command(
        const simcore_host::ParsedControlCommand& command,
        Clock::time_point received_at,
        std::uint64_t connection_generation);
    ClientMessageResult handle_simulation_reset(
        const simcore_host::ParsedSimulationReset& reset,
        std::uint64_t connection_generation);
    void publish_current_state();
    std::vector<simcore_host::KinematicCollisionProxy>
        make_runtime_collision_snapshot() const;
    void advance_runtime_entities(double dt_seconds);
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
    std::vector<simcore_host::RuntimeEntityState> initial_runtime_entities_;
    std::vector<simcore_host::RuntimeEntityState> runtime_entities_;
    std::unordered_set<std::string> seen_play_sessions_;
    std::string active_play_session_key_;
    std::string active_play_session_id_;
    std::string active_controller_source_id_;
    std::string active_connection_session_id_;
    std::uint64_t active_connection_generation_ = 0;
    std::uint64_t lifecycle_highest_sequence_ = 0;
    std::uint64_t lifecycle_last_client_time_ns_ = 0;
    bool lifecycle_active_ = false;
    bool running_ = false;
    bool estop_latched_ = false;
    std::uint32_t last_reported_overrun_count_ = 0;
    Clock::time_point last_overrun_log_time_ = Clock::time_point::min();
    VehicleInput last_logged_input_;
    Clock::time_point last_control_change_time_ = Clock::time_point::min();
    std::uint64_t control_change_revision_ = 0;
    std::uint64_t reported_control_change_revision_ = 0;
};
