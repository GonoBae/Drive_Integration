#pragma once

#include "physics/vehicle_physics.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace simcore_host {

inline constexpr std::uint32_t kProtocolSchemaVersion = 2;

struct EnvelopeMetadata {
    std::uint64_t sequence = 0;
    std::uint64_t simulation_time_ns = 0;
    std::string_view source_id;
    std::string_view map_package_checksum;
    std::string_view play_session_id;
};

enum class RuntimeEntityKind : std::uint8_t {
    NpcVehicle = 2,
    Pedestrian = 3,
};

// Authoritative tick-local state for non-Ego runtime entities. The collision
// proxy remains the single shape/velocity source used by physics and wire
// publication; entity_id is the stable protocol identity.
struct RuntimeEntityState {
    std::uint32_t entity_id = 0;
    RuntimeEntityKind kind = RuntimeEntityKind::NpcVehicle;
    KinematicCollisionProxy collision_proxy;
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
};

using ParsedClientMessage = std::variant<ParsedControlCommand,
                                         ParsedSimulationReset>;

std::string serialize_entity_state_packet(const VehicleState& state);
std::string serialize_world_state_envelope(const VehicleState& state,
                                           const EnvelopeMetadata& metadata);
std::string serialize_world_state_envelope(
    const VehicleState& state,
    const std::vector<RuntimeEntityState>& runtime_entities,
    const EnvelopeMetadata& metadata);

std::optional<ParsedClientMessage> parse_client_message_envelope(
    std::string_view data,
    std::string* error = nullptr);

std::optional<ParsedControlCommand> parse_control_command_envelope(
    std::string_view data,
    std::string* error = nullptr);

} // namespace simcore_host
