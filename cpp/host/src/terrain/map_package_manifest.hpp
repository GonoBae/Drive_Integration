#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace simcore_host {

inline constexpr int kMapPackageManifestFormatVersion = 1;

struct MapPackageManifest {
    std::filesystem::path package_directory;
    std::filesystem::path source_path;
    std::string map_id;
    std::string coordinate_frame;
    std::vector<std::string> collision_files;
    std::string collision_checksum;
};

// Loads a strict MapPackage manifest and verifies the checksum of every
// authoritative collision payload before any simulation state is created.
[[nodiscard]] MapPackageManifest load_map_package_manifest(
    const std::filesystem::path& package_directory);

// Cross-platform package identity. Files are hashed in manifest order as
// UTF-8 filename, NUL, raw file bytes, NUL using FNV-1a 64.
[[nodiscard]] std::string compute_map_package_collision_checksum(
    const std::filesystem::path& package_directory,
    const std::vector<std::string>& collision_files);

} // namespace simcore_host
