#pragma once

#include "control/control_lease.hpp"
#include "physics/vehicle_physics.hpp"
#include "protocol/vehicle_messages.hpp"
#include "replay/physics_replay.hpp"
#include "simulation_clock.hpp"
#include "runtime_collision_reaction.hpp"
#include "terrain/map_package_runtime.hpp"
#include "traffic/npc_horn_policy.hpp"
#include "traffic/npc_lane_follower.hpp"
#include "traffic/npc_lane_change.hpp"
#include "traffic/npc_route_planner.hpp"
#include "traffic/impact_recovery.hpp"
#include "traffic/impact_tumble.hpp"
#include "traffic/pedestrian_impact.hpp"
#include "collision/structure_damage.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
    // Production WebSocket clients must complete the application Hello before
    // lifecycle reset/control. Unit integrations can leave this disabled.
    bool require_client_hello = false;
    std::shared_ptr<const simcore_host::TrafficNetwork> traffic_network;
    std::vector<std::uint32_t> npc_route;
    std::vector<std::uint32_t> npc_alternate_route;
    bool npc_route_loop = false;
    bool npc_autonomous = false;
    double npc_start_offset_m = 0.0;
    double npc_max_speed_mps = 6.0;
    std::uint32_t npc_count = 1;
    double npc_spacing_m = 120.0;
    // Optional offline-verifiable recording of actual applied fixed-tick input.
    std::shared_ptr<simcore_host::PhysicsReplayRecorder> physics_replay;
};

struct SimulationHostCallbacks {
    std::function<void(const std::string&)> broadcast_world_state;
    std::function<void(const std::string&)> publish_observer_state;
    std::function<void(const std::string&)> close_control_connections;
};

enum class ClientMessageResult {
    HelloAccepted,
    DuplicateHello,
    ControlAccepted,
    Rejected,
    EmergencyStopLatched,
    SimulationReset,
    DuplicateSimulationReset,
    Accepted = ControlAccepted,
};

using ControlMessageResult = ClientMessageResult;

// Local diagnostics, intentionally not a new wire-protocol requirement.
struct NpcNavigationSnapshot {
    std::uint32_t entity_id = 0;
    std::uint32_t lane_id = 0;
    std::uint32_t destination_lane_id = 0;
    std::uint64_t destinations_selected = 0;
    std::uint64_t reroutes = 0;
    std::uint64_t lane_changes_completed = 0;
    bool changing_lane = false;
    std::vector<std::uint32_t> route;
    std::string collision_state = "driving";
    double collision_damage_percent = 0.0;
    double collision_hold_remaining_s = 0.0;
    double lane_offset_m = 0.0;
    double route_distance_travelled_m = 0.0;
    std::string lane_change_wait_reason;
    bool preserving_bypass_space = false;
    // Local graph-planner diagnostics. "backing" is a bounded reverse on the
    // current lane, never a reverse graph edge or free-space autonomous drive.
    std::string avoidance_phase = "none";
    double avoidance_reverse_remaining_m = 0.0;
    std::string vehicle_profile = "sedan";
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
    std::string make_initial_hello();
    // May be called from the MapPackage loader thread. The immutable candidate
    // is only installed by run_tick(), never concurrently with physics.
    void queue_map_package_reload(
        simcore_host::RuntimeMapPackage package,
        std::optional<std::vector<simcore_host::RuntimeEntityState>>
            replacement_runtime_entities = std::nullopt);
    // Immutable, off-thread validated network; only matching map identities
    // can replace the current network at a tick boundary.
    void queue_traffic_network_reload(
        std::shared_ptr<const simcore_host::TrafficNetwork> network);

    VehicleState state() const { return physics_.get_state(); }
    const std::vector<simcore_host::RuntimeEntityState>& runtime_entities() const
    {
        return runtime_entities_;
    }
    bool running() const { return running_; }
    std::vector<NpcNavigationSnapshot> npc_navigation() const;
    const std::string& map_package_checksum() const {
        return config_.map_package_checksum;
    }
    std::size_t static_collider_count() const {
        return config_.collision_world
            ? config_.collision_world->static_collider_count()
            : 0;
    }

private:
    void apply_vehicle_input(const VehicleInput& input)
    {
        applied_input_ = input;
        physics_.set_input(input);
    }
    static VehicleInput make_safe_stop_input();
    static std::string make_play_session_key(std::string_view source_id,
                                             std::string_view play_session_id);
    simcore_host::EnvelopeMetadata make_metadata();
    simcore_host::HealthSnapshot make_health_snapshot(Clock::time_point now) const;
    ClientMessageResult handle_control_command(
        const simcore_host::ParsedControlCommand& command,
        Clock::time_point received_at,
        std::uint64_t connection_generation);
    ClientMessageResult handle_hello(
        const simcore_host::ParsedHello& hello,
        std::uint64_t connection_generation);
    ClientMessageResult handle_simulation_reset(
        const simcore_host::ParsedSimulationReset& reset,
        std::uint64_t connection_generation);
    void publish_current_state();
    std::string serialize_current_world_state(Clock::time_point now);
    void apply_pending_traffic_network_reload();
    std::vector<simcore_host::KinematicCollisionProxy>
        make_runtime_collision_snapshot() const;
    void advance_runtime_entities(double dt_seconds);
    void finish_runtime_impact_contacts(const std::vector<simcore_host::CollisionContact>& contacts);
    void rebuild_structure_damage();
    std::vector<simcore_host::TrafficSignalSnapshot> current_traffic_signals(bool enabled) const;
    void rebuild_lane_npc();
    void prepare_lane_npc(double dt_seconds, Clock::time_point now);
    struct LaneNpcRuntime;
    bool choose_npc_destination(LaneNpcRuntime& npc);
    void update_npc_navigation(LaneNpcRuntime& npc, double dt_seconds);
    std::optional<simcore_host::NpcLaneSample> sample_lane_npc(
        const LaneNpcRuntime& npc, double distance_m) const;
    bool npc_sample_blocked(const LaneNpcRuntime& npc,
        const simcore_host::NpcLaneSample& sample, bool stationary_only = false,
        const simcore_host::ObbPrism* collision_shape = nullptr,
        bool ignore_ego = false, std::uint32_t* downed_pedestrian_id = nullptr) const;
    bool try_npc_lane_change(LaneNpcRuntime& npc, double retreat_m = 0.0, bool commit = true);
    bool npc_reverse_path_clear(const LaneNpcRuntime& npc, double distance_m) const;
    bool begin_npc_escape_reverse(LaneNpcRuntime& npc, double obstruction_distance_m,
        std::uint32_t downed_pedestrian_id = 0);
    struct NpcRouteObstacle {
        double distance_m = 0.0;
        std::uint32_t downed_pedestrian_id = 0;
    };
    std::optional<NpcRouteObstacle> lane_npc_obstacle(
        const LaneNpcRuntime& npc, double lookahead_m, bool stationary_only = false,
        bool route_end_is_blocker = true) const;
    std::optional<double> lane_npc_blocked_distance(
        const LaneNpcRuntime& npc, double lookahead_m, bool stationary_only = false,
        bool route_end_is_blocker = true) const;
    void rebuild_pedestrians();
    void prepare_pedestrians(double dt_seconds, Clock::time_point now);
    void schedule_tick();
    void run_tick();
    bool apply_pending_map_package_reload(Clock::time_point tick_started_at);
    void reset_player_vehicle(simcore_host::RuntimeVehicleClass vehicle_class);

    SimulationHostConfig config_;
    VehicleParameters base_vehicle_parameters_;
    SimulationHostCallbacks callbacks_;
    VehiclePhysics physics_;
    SimulationClock simulation_clock_;
    ControlLease control_lease_;
    boost::asio::steady_timer timer_;
    std::shared_ptr<int> lifetime_token_ = std::make_shared<int>(0);

    std::uint64_t message_sequence_ = 1;
    std::vector<simcore_host::RuntimeEntityState> initial_runtime_entities_;
    std::vector<simcore_host::RuntimeEntityState> runtime_entities_;
    simcore_host::StructureDamageRuntime structure_damage_;
    struct LaneNpcRuntime {
        enum class AvoidancePhase : std::uint8_t {
            None,
            WaitingForStop,
            Backing,
            ReadyToPass,
        };
        std::uint32_t entity_id = 0;
        double start_offset_m = 0.0;
        std::uint32_t vehicle_profile_index = 0;
        const char* vehicle_profile_name = "sedan";
        double body_half_length_m = 2.2;
        double body_half_width_m = 1.0;
        double body_half_height_m = 0.75;
        double mass_kg = 1500.0;
        double yaw_inertia_kg_m2 = 2600.0;
        double maximum_reaction_speed_mps = 30.0;
        simcore_host::ImpactTumbleDimensions tumble_dimensions{
            2.2, 1.0, 0.75, 1500.0, 0.35};
        simcore_host::NpcLaneFollower follower;
        std::optional<simcore_host::RuntimeEntityState> pending;
        std::optional<simcore_host::RuntimeEntityState> nominal_pending;
        simcore_host::RuntimeCollisionReaction reaction;
        simcore_host::ImpactTumbleState tumble;
        simcore_host::NpcHornPolicy horn;
        std::vector<std::uint32_t> navigation_route;
        std::uint32_t destination_lane_id = 0;
        std::uint64_t destinations_selected = 0;
        std::uint64_t reroutes = 0;
        std::uint64_t lane_changes_completed = 0;
        double navigation_cooldown_s = 0.0;
        double obstruction_seconds = 0.0;
        AvoidancePhase avoidance_phase = AvoidancePhase::None;
        double avoidance_reverse_remaining_m = 0.0;
        double avoidance_reverse_speed_mps = 0.0;
        double avoidance_reverse_total_m = 0.0;
        // Absolute travelled distance at which to wait, leaving steering room
        // around a persistent crash while an adjacent traffic gap opens.
        std::optional<double> avoidance_stop_distance_m;
        std::string lane_change_wait_reason;
        std::optional<simcore_host::NpcLaneChangePlan> lane_change;
        bool lane_change_fault_reported = false;
        std::vector<std::uint32_t> lane_change_route;
    };
    struct PedestrianRuntime {
        std::uint32_t entity_id = 0;
        std::uint32_t controller_id = 0;
        std::uint32_t group_id = 0;
        simcore_host::GroundPointEnu start;
        simcore_host::GroundPointEnu end;
        double progress = 0.0;
        int direction = 1;
        bool crossing = false;
        std::optional<simcore_host::RuntimeEntityState> pending;
        std::optional<simcore_host::RuntimeEntityState> nominal_pending;
        simcore_host::RuntimeCollisionReaction reaction;
        simcore_host::PedestrianImpactState body;
    };
    std::vector<LaneNpcRuntime> lane_npcs_;
    std::optional<simcore_host::NpcRoutePlanner> npc_route_planner_;
    std::vector<PedestrianRuntime> pedestrians_;
    std::unordered_set<std::string> seen_play_sessions_;
    enum class ClientPayloadKind : std::uint8_t {
        Hello,
        ControlCommand,
        SimulationReset,
    };
    struct HelloConnectionState {
        std::string source_id;
        std::string session_id;
        // Canonical build/schema/map/capability identity accepted by the first
        // Hello. Capability order is normalized because it is a negotiated set.
        std::string negotiated_hello_fingerprint;
        std::uint64_t highest_sequence = 0;
        ClientPayloadKind last_payload_kind = ClientPayloadKind::Hello;
        // Canonical semantic identity excluding sequence. An equal sequence is
        // valid only when this kind and fingerprint are also equal.
        std::string last_payload_fingerprint;
    };
    // The transport generation alone is not an application identity. Bind the
    // accepted Hello identity and global envelope ordering to that generation
    // so later Reset/Control frames cannot change identity or replay an older
    // sequence on the same socket.
    std::unordered_map<std::uint64_t, HelloConnectionState>
        hello_connection_states_;
    std::string active_play_session_key_;
    std::string active_play_session_id_;
    std::string active_controller_source_id_;
    std::string active_connection_session_id_;
    simcore_host::RuntimeVehicleClass active_vehicle_class_ =
        simcore_host::RuntimeVehicleClass::Sedan;
    std::uint64_t active_connection_generation_ = 0;
    std::uint64_t lifecycle_highest_sequence_ = 0;
    std::uint64_t lifecycle_last_client_time_ns_ = 0;
    bool lifecycle_active_ = false;
    bool running_ = false;
    bool estop_latched_ = false;
    std::uint32_t last_reported_overrun_count_ = 0;
    Clock::time_point last_overrun_log_time_ = Clock::time_point::min();
    VehicleInput last_logged_input_;
    // Unlike last_logged_input_, this never drops sub-0.001 input changes.
    VehicleInput applied_input_;
    Clock::time_point last_control_change_time_ = Clock::time_point::min();
    std::uint64_t control_change_revision_ = 0;
    std::uint64_t reported_control_change_revision_ = 0;
    struct PendingMapPackageReload {
        simcore_host::RuntimeMapPackage package;
        std::optional<std::vector<simcore_host::RuntimeEntityState>>
            replacement_runtime_entities;
    };
    std::mutex pending_map_package_mutex_;
    std::optional<PendingMapPackageReload> pending_map_package_;
    std::mutex pending_traffic_mutex_;
    std::shared_ptr<const simcore_host::TrafficNetwork> pending_traffic_network_;
};
