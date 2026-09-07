#pragma once

#include "physics/vehicle_physics.hpp"
#include "collision/structure_damage.hpp"
#include "traffic/traffic_network.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace simcore_host {

inline constexpr std::uint32_t kProtocolSchemaVersion = 2;
inline constexpr std::string_view kProtocolSchemaName = "simcore-envelope-v2";
// format v2 signal plans are bounded to a one-hour cycle. Legacy v1 still
// produces at most 30 seconds; the additive wire field must accept either.
inline constexpr double kMaxTrafficSignalCountdownSeconds = 3600.0;

struct EnvelopeMetadata {
    std::uint64_t sequence = 0;
    std::uint64_t simulation_time_ns = 0;
    std::string_view source_id;
    std::string_view map_package_checksum;
    std::string_view play_session_id;
    std::string_view session_id;
};

enum class HealthStatus : std::uint8_t {
    AwaitingReset,
    AwaitingControl,
    Active,
    SafeStop,
    ReconnectRequired,
    EstopLatched,
};

// Tick-local authority sent atomically with the matching vehicle snapshot.
// This never gates control recovery; clients use it for presentation only.
struct HealthSnapshot {
    HealthStatus status = HealthStatus::AwaitingReset;
    std::uint32_t tick_overrun_count = 0;
    std::uint64_t last_command_age_ns = 0;
    std::string_view message;
    bool has_control_command = false;
};

struct ParsedHello {
    std::uint64_t sequence = 0;
    std::string source_id;
    std::string session_id;
    std::string map_package_checksum;
    std::string build;
    std::string schema;
    std::vector<std::string> capabilities;
};

enum class RuntimeEntityKind : std::uint8_t {
    NpcVehicle = 2,
    Pedestrian = 3,
};

enum class RuntimeVehicleClass : std::uint8_t {
    Unspecified = 0,
    Sedan = 1,
    Compact = 2,
    Truck = 3,
    Motorcycle = 4,
};

// Authoritative tick-local state for non-Ego runtime entities. The collision
// proxy remains the single collision shape/planar velocity source used by
// physics and wire publication; entity_id is the stable protocol identity.
// A tilted NPC's yaw-aligned proxy holds conservative projected extents, not
// the unrotated visual mesh dimensions. Body attitude is published separately.
struct RuntimeEntityState {
    std::uint32_t entity_id = 0;
    RuntimeEntityKind kind = RuntimeEntityKind::NpcVehicle;
    KinematicCollisionProxy collision_proxy;
    // Existing schema-v2 Euler convention: nose-up pitch / left-up roll.
    double pitch_rad = 0.0;
    double roll_rad = 0.0;
    double pitch_rate_rad_s = 0.0;
    double roll_rate_rad_s = 0.0;
    // Capsules have no shape heading; publish their intended walking direction.
    double heading_rad = 0.0;
    float damage_percent = 0.0f;
    float last_impact_impulse_n_s = 0.0f;
    VehicleDamageZone damage_zone = VehicleDamageZone::None;
    std::uint32_t collision_event_sequence = 0;
    std::uint32_t recovery_phase = 0; // Driving/Settling/Holding/Recovering/Disabled = 0..4
    CollisionVector2 impact_direction_enu;
    bool pedestrian_downed = false;
    bool pedestrian_airborne = false;
    double vertical_velocity_mps = 0.0;
    std::uint32_t turn_indicator = 0; // off / left / right
    std::vector<VehicleDentPatch> dent_patches;
    // NPC-only monotonic event sequence; reset to zero with the Play lifecycle.
    std::uint32_t horn_event_sequence = 0;
    RuntimeVehicleClass vehicle_class = RuntimeVehicleClass::Unspecified;
};

struct ParsedControlCommand {
    VehicleInput input;
    std::uint64_t sequence = 0;
    std::uint64_t client_time_ns = 0;
    std::string source_id;
    std::string session_id;
    std::string map_package_checksum;
    bool estop = false;
};

struct ParsedSimulationReset {
    std::uint64_t sequence = 0;
    std::uint64_t client_time_ns = 0;
    std::string source_id;
    std::string session_id;
    std::string map_package_checksum;
    std::string play_session_id;
    RuntimeVehicleClass requested_vehicle_class = RuntimeVehicleClass::Unspecified;
};

using ParsedClientMessage = std::variant<ParsedHello,
                                          ParsedControlCommand,
                                          ParsedSimulationReset>;

std::string serialize_entity_state_packet(
    const VehicleState& state,
    RuntimeVehicleClass ego_vehicle_class = RuntimeVehicleClass::Unspecified);
std::string serialize_world_state_envelope(const VehicleState& state,
                                           const EnvelopeMetadata& metadata,
                                           std::optional<HealthSnapshot> health = std::nullopt,
                                           RuntimeVehicleClass ego_vehicle_class = RuntimeVehicleClass::Unspecified);
std::string serialize_world_state_envelope(
    const VehicleState& state,
    const std::vector<RuntimeEntityState>& runtime_entities,
    const EnvelopeMetadata& metadata,
    std::optional<HealthSnapshot> health = std::nullopt,
    const std::vector<TrafficSignalSnapshot>& traffic_signals = {},
    std::string_view traffic_network_checksum = {},
    const std::vector<StructureDamageSnapshot>& structures = {},
    RuntimeVehicleClass ego_vehicle_class = RuntimeVehicleClass::Unspecified);

std::string serialize_hello_envelope(
    const EnvelopeMetadata& metadata,
    std::string_view build,
    const std::vector<std::string>& capabilities);

std::optional<ParsedClientMessage> parse_client_message_envelope(
    std::string_view data,
    std::string* error = nullptr);

std::optional<ParsedControlCommand> parse_control_command_envelope(
    std::string_view data,
    std::string* error = nullptr);

} // namespace simcore_host
