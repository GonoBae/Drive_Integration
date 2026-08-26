#pragma once

#include "collision/collision_world.hpp"
#include "terrain/map_package_manifest.hpp"

#include <string_view>

namespace simcore_host {

inline constexpr std::string_view kStaticCollidersFileName =
    "static_colliders.csv";

// Loads only the strict static collision payload explicitly declared by an
// already verified MapPackageManifest. An undeclared payload fails closed.
[[nodiscard]] CollisionWorld load_static_collision_world(
    const MapPackageManifest& manifest);

} // namespace simcore_host
