#pragma once

#include "physics/vehicle_physics.hpp"

#include <filesystem>
#include <string>

namespace simcore_host {

inline constexpr int kVehicleConfigFormatVersion = 4;

struct LoadedVehicleParameters {
    VehicleParameters parameters;
    std::filesystem::path source_path;
    std::string checksum;
};

// Loads a complete key=value vehicle configuration. Unknown, duplicate,
// missing, non-finite, and physically invalid values are rejected so a typo
// cannot silently fall back to a compiled default.
[[nodiscard]] LoadedVehicleParameters load_vehicle_parameters(
    const std::filesystem::path& path);

} // namespace simcore_host
