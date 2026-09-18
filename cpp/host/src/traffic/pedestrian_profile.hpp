#pragma once

#include <cstdint>

namespace simcore_host {
struct PedestrianProfile {
    double half_height_m = 0.9;
    double radius_m = 0.35;
    double mass_kg = 80.0;
    double walking_speed_mps = 1.35;
};

// Stable across reset/replay without extending the wire schema. The visual
// client uses the same ID order and fits its model to these dimensions.
[[nodiscard]] constexpr PedestrianProfile pedestrian_profile(std::uint32_t entity_id) noexcept
{
    switch (entity_id % 3) {
    case 1: return {0.625, 0.22, 32.0, 1.10};
    case 2: return {0.825, 0.28, 62.0, 1.00};
    default: return {};
    }
}
}
