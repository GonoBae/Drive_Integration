#pragma once

#include "physics/vehicle_physics.hpp"

#include <filesystem>
#include <string>

namespace simcore_host {

inline constexpr int kVehicleConfigFormatVersion = 7;

struct LoadedVehicleParameters {
    VehicleParameters parameters;
    std::filesystem::path source_path;
    std::string checksum;
};

// Loads a complete key=value vehicle configuration. Unknown, duplicate,
// missing, non-finite, and physically invalid values are rejected so a typo
// cannot silently fall back to a compiled default.
// Version 7 requires separate front/rear per-tire cornering stiffnesses and
// retains v6's speed-independent steering. Older configurations require an
// explicit migration; legacy keys are not ignored.
[[nodiscard]] LoadedVehicleParameters load_vehicle_parameters(
    const std::filesystem::path& path);

} // namespace simcore_host
