#pragma once

#include "physics/vehicle_physics.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace simcore_host {

inline constexpr std::uint32_t kProtocolSchemaVersion = 1;

struct EnvelopeMetadata {
    std::uint64_t sequence = 0;
    std::uint64_t simulation_time_ns = 0;
    std::string_view source_id;
    std::string_view map_package_checksum;
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

std::string serialize_entity_state_packet(const VehicleState& state);
std::string serialize_world_state_envelope(const VehicleState& state,
                                           const EnvelopeMetadata& metadata);

std::optional<ParsedControlCommand> parse_control_command_envelope(
    std::string_view data,
    std::string* error = nullptr);

} // namespace simcore_host
