#include "runtime_options.hpp"

#include "config.hpp"
#include "runtime_options_internal.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace simcore_host {
namespace {

using Nanoseconds = std::chrono::nanoseconds;

constexpr double kMinimumPhysicsHz = 1.0;
constexpr double kMaximumPhysicsHz = 1'000.0;
constexpr double kMinimumOriginAltitudeM = -12'000.0;
constexpr double kMaximumOriginAltitudeM = 100'000.0;
constexpr std::uint64_t kMinimumTimeoutMs = 1;
constexpr std::uint64_t kMaximumSoftTimeoutMs = 60'000;
constexpr std::uint64_t kMaximumHardTimeoutMs = 300'000;
constexpr std::size_t kMaximumSourceIdBytes = 64;

std::uint64_t milliseconds(Nanoseconds duration)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

} // namespace

RuntimeOptions make_runtime_option_defaults(
    std::filesystem::path vehicle_config_path,
    std::filesystem::path map_package_path)
{
    RuntimeOptions options;
    options.vehicle_config_path = std::move(vehicle_config_path);
    options.map_package_path = std::move(map_package_path);
    options.ws_port = Config::WS_PORT;
    options.physics_frequency_hz = Config::PHYSICS_HZ;
    options.origin_lat_deg = Config::DEFAULT_ORIGIN_LAT;
    options.origin_lon_deg = Config::DEFAULT_ORIGIN_LON;
    options.origin_alt_m = Config::DEFAULT_ORIGIN_ALT;
    options.spawn_heading_deg = Config::DEFAULT_SPAWN_HEADING;
    options.command_timeout = Nanoseconds(Config::COMMAND_TIMEOUT_NS);
    options.max_command_queue_age = Nanoseconds(Config::MAX_COMMAND_QUEUE_AGE_NS);
    options.hard_command_timeout = Nanoseconds(Config::HARD_COMMAND_TIMEOUT_NS);
    options.source_id = Config::SOURCE_ID;
    options.demo_entities = false;
    return options;
}

void validate_runtime_options(const RuntimeOptions& options)
{
    if (!options.npc_route.empty() && !options.traffic_network_path) {
        throw std::invalid_argument("npc_route requires traffic_network");
    }
    if (!options.npc_route.empty() && options.demo_entities) {
        throw std::invalid_argument("lane NPC and legacy demo_entities are mutually exclusive");
    }
    if (options.npc_route.size() > 64 || std::any_of(options.npc_route.begin(),
            options.npc_route.end(), [](std::uint32_t id) { return id == 0; })) {
        throw std::invalid_argument("npc_route requires positive lane IDs with at most 64 entries");
    }
    if (options.npc_alternate_route.size() > 64
        || std::any_of(options.npc_alternate_route.begin(), options.npc_alternate_route.end(),
            [](std::uint32_t id) { return id == 0; })) {
        throw std::invalid_argument("npc_alternate_route requires positive lane IDs with at most 64 entries");
    }
    if (!options.npc_alternate_route.empty() && options.npc_route.empty()) {
        throw std::invalid_argument("npc_alternate_route requires npc_route");
    }
    if (!std::isfinite(options.npc_start_offset_m) || options.npc_start_offset_m < 0.0
        || options.npc_start_offset_m > 1e6) {
        throw std::invalid_argument("npc_start_offset_m must be finite and in [0,1000000]");
    }
    if (!std::isfinite(options.npc_max_speed_mps) || options.npc_max_speed_mps <= 0.0
        || options.npc_max_speed_mps > 25.0) {
        throw std::invalid_argument("npc_max_speed_mps must be finite and in (0,25]");
    }
    if (options.npc_count < 1 || options.npc_count > 16) {
        throw std::invalid_argument("npc_count must be in [1,16]");
    }
    if (!std::isfinite(options.npc_spacing_m) || options.npc_spacing_m < 8.0
        || options.npc_spacing_m > 1e6) {
        throw std::invalid_argument("npc_spacing_m must be finite and in [8,1000000]");
    }
    if (options.traffic_network_path && options.traffic_network_path->empty()) {
        throw std::invalid_argument("traffic_network path must not be empty");
    }
    if (options.vehicle_config_path.empty()) {
        throw std::invalid_argument("vehicle_config path must not be empty");
    }
    if (options.map_package_path.empty()) {
        throw std::invalid_argument("map_package path must not be empty");
    }
    if (options.ws_port == 0) {
        throw std::invalid_argument("ws_port must be in [1, 65535]");
    }
    if (!std::isfinite(options.physics_frequency_hz)
        || options.physics_frequency_hz < kMinimumPhysicsHz
        || options.physics_frequency_hz > kMaximumPhysicsHz) {
        throw std::invalid_argument("physics_hz must be finite and in [1, 1000]");
    }
    if (!std::isfinite(options.origin_lat_deg)
        || options.origin_lat_deg < -90.0 || options.origin_lat_deg > 90.0) {
        throw std::invalid_argument("origin_lat_deg must be finite and in [-90, 90]");
    }
    if (!std::isfinite(options.origin_lon_deg)
        || options.origin_lon_deg < -180.0 || options.origin_lon_deg > 180.0) {
        throw std::invalid_argument("origin_lon_deg must be finite and in [-180, 180]");
    }
    if (!std::isfinite(options.origin_alt_m)
        || options.origin_alt_m < kMinimumOriginAltitudeM
        || options.origin_alt_m > kMaximumOriginAltitudeM) {
        throw std::invalid_argument(
            "origin_alt_m must be finite and in [-12000, 100000]");
    }
    if (!std::isfinite(options.spawn_heading_deg)
        || options.spawn_heading_deg < 0.0f
        || options.spawn_heading_deg >= 360.0f) {
        throw std::invalid_argument(
            "spawn_heading_deg must be finite and in [0, 360)");
    }

    const auto command_timeout_ms = milliseconds(options.command_timeout);
    const auto maximum_queue_age_ms = milliseconds(options.max_command_queue_age);
    const auto hard_timeout_ms = milliseconds(options.hard_command_timeout);
    if (options.command_timeout <= Nanoseconds::zero()
        || command_timeout_ms < kMinimumTimeoutMs
        || command_timeout_ms > kMaximumSoftTimeoutMs) {
        throw std::invalid_argument(
            "command_timeout_ms must be in [1, 60000]");
    }
    if (options.max_command_queue_age <= Nanoseconds::zero()
        || maximum_queue_age_ms < kMinimumTimeoutMs
        || maximum_queue_age_ms > command_timeout_ms) {
        throw std::invalid_argument(
            "max_command_queue_age_ms must be in [1, command_timeout_ms]");
    }
    if (options.hard_command_timeout <= options.command_timeout
        || hard_timeout_ms > kMaximumHardTimeoutMs) {
        throw std::invalid_argument(
            "hard_command_timeout_ms must exceed command_timeout_ms and be at most 300000");
    }

    if (options.source_id.empty()
        || options.source_id.size() > kMaximumSourceIdBytes) {
        throw std::invalid_argument("source_id must contain 1 to 64 bytes");
    }
    const bool source_id_valid = std::all_of(
        options.source_id.begin(), options.source_id.end(), [](unsigned char value) {
            return value >= 33 && value <= 126;
        });
    if (!source_id_valid) {
        throw std::invalid_argument(
            "source_id must contain printable ASCII without whitespace");
    }
}

RuntimeOptions parse_runtime_options(
    const std::vector<std::string>& arguments,
    RuntimeOptions defaults)
{
    return runtime_options_internal::parse_runtime_cli(
        arguments, std::move(defaults));
}

RuntimeOptions parse_runtime_options(
    int argc,
    char* argv[],
    RuntimeOptions defaults)
{
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return parse_runtime_options(arguments, std::move(defaults));
}

std::string runtime_options_help(const RuntimeOptions& defaults)
{
    std::ostringstream output;
    output
        << "Usage: simcore_publisher [OPTIONS]\n\n"
        << "Configuration precedence: built-in defaults < --runtime-config cfg < CLI.\n"
        << "A runtime cfg requires its base keys; traffic/NPC keys are optional. Unknown/duplicate keys fail.\n\n"
        << "  --runtime-config PATH           Load strict runtime cfg first\n"
        << "  --vehicle-config PATH           Vehicle cfg (default: "
        << defaults.vehicle_config_path.string() << ")\n"
        << "  --map-package DIRECTORY         MapPackage (default: "
        << defaults.map_package_path.string() << ")\n"
        << "  --traffic-network PATH          Optional authored lane/signal JSON\n"
        << "  --npc-route IDS|none            Primary NPC route, comma-separated lane IDs (default off)\n"
        << "  --npc-alternate-route IDS|none  Optional second route, alternated by entity ID\n"
        << "  --npc-loop true|false           Repeat a connected closed route (default false)\n"
        << "  --npc-start-offset M            Start distance on the first route lane\n"
        << "  --npc-max-speed MPS             NPC cruise limit (0,25], default 6 m/s\n"
        << "  --npc-count COUNT               Lane NPCs [1,16], default 1\n"
        << "  --npc-spacing M                 Same-route spawn spacing [8,1000000]m\n"
        << "  --ws-port PORT                  WebSocket port [1,65535] (default: "
        << defaults.ws_port << ")\n"
        << "  --physics-hz HZ                 Fixed simulation rate [1,1000] (default: "
        << defaults.physics_frequency_hz << ")\n"
        << "  --origin-lat DEG                ENU origin latitude [-90,90]\n"
        << "  --origin-lon DEG                ENU origin longitude [-180,180]\n"
        << "  --origin-alt M                  ENU origin altitude [-12000,100000]\n"
        << "  --spawn-heading DEG             Clockwise navigation heading [0,360)\n"
        << "  --command-timeout-ms MS         SafeStop deadline [1,60000]\n"
        << "  --max-command-queue-age-ms MS   Stale command limit <= SafeStop deadline\n"
        << "  --hard-command-timeout-ms MS    Reconnect deadline > SafeStop, <=300000\n"
        << "  --source-id ID                  Printable non-space ASCII, 1..64 bytes\n"
        << "  --demo-entities                 Enable built-in NPC/pedestrian proxies\n"
        << "  --no-demo-entities              Disable proxies set true by a cfg\n"
        << "  -h, --help                      Show this help\n";
    return output.str();
}

} // namespace simcore_host
