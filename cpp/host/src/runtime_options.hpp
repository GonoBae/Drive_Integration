#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace simcore_host {

inline constexpr std::uint32_t kRuntimeConfigFormatVersion = 1;

struct RuntimeOptions {
    std::filesystem::path vehicle_config_path;
    std::filesystem::path map_package_path;
    // Optional additive v1 key; old runtime configs remain valid.
    std::optional<std::filesystem::path> traffic_network_path;
    // Authored lane traffic. An empty primary route disables all lane NPCs.
    std::vector<std::uint32_t> npc_route;
    std::vector<std::uint32_t> npc_alternate_route;
    bool npc_route_loop = false;
    double npc_start_offset_m = 0.0;
    double npc_max_speed_mps = 6.0;
    std::uint32_t npc_count = 1;
    double npc_spacing_m = 120.0;
    std::uint16_t ws_port = 9000;
    double physics_frequency_hz = 60.0;
    double origin_lat_deg = 40.70694;
    double origin_lon_deg = -74.01083;
    double origin_alt_m = 0.0;
    float spawn_heading_deg = 0.0f;
    std::chrono::nanoseconds command_timeout{250'000'000};
    std::chrono::nanoseconds max_command_queue_age{100'000'000};
    std::chrono::nanoseconds hard_command_timeout{1'000'000'000};
    std::string source_id = "simcore-cpp-host";
    bool demo_entities = false;
    std::optional<std::filesystem::path> runtime_config_path;
    bool show_help = false;
};

// The executable supplies its build-time vehicle and MapPackage paths while
// every scalar remains compatible with the historical Config defaults.
RuntimeOptions make_runtime_option_defaults(
    std::filesystem::path vehicle_config_path,
    std::filesystem::path map_package_path);

// Arguments exclude argv[0]. A --runtime-config file is loaded first and every
// explicitly supplied CLI value then overrides it. Duplicate options fail
// closed, including --demo-entities/--no-demo-entities as one logical option.
RuntimeOptions parse_runtime_options(
    const std::vector<std::string>& arguments,
    RuntimeOptions defaults);

RuntimeOptions parse_runtime_options(
    int argc,
    char* argv[],
    RuntimeOptions defaults);

void validate_runtime_options(const RuntimeOptions& options);

std::string runtime_options_help(const RuntimeOptions& defaults);

} // namespace simcore_host
