#include "runtime_options.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef SIMCORE_TEST_RUNTIME_CONFIG_PATH
#error SIMCORE_TEST_RUNTIME_CONFIG_PATH must identify the tracked runtime config
#endif

namespace {

using simcore_host::RuntimeOptions;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void require_rejected(Callable&& callable, const char* message)
{
    bool rejected = false;
    try {
        callable();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, message);
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read test fixture: " + path.string());
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

class TemporaryConfig {
public:
    explicit TemporaryConfig(std::string contents)
    {
        static std::atomic<std::uint64_t> sequence{0};
        path = std::filesystem::temp_directory_path()
            / ("simcore-runtime-options-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count())
               + "-" + std::to_string(++sequence) + ".cfg");
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "failed to create temporary runtime configuration");
        }
        output << contents;
    }

    ~TemporaryConfig()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    TemporaryConfig(const TemporaryConfig&) = delete;
    TemporaryConfig& operator=(const TemporaryConfig&) = delete;

    std::filesystem::path path;
};

RuntimeOptions defaults()
{
    return simcore_host::make_runtime_option_defaults(
        "default-vehicle.cfg", "default-map");
}

std::string replace_config_value(
    std::string bytes,
    std::string_view key,
    std::string_view value)
{
    const std::string prefix = std::string(key) + "=";
    const auto start = bytes.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("runtime fixture key not found");
    }
    const auto value_start = start + prefix.size();
    const auto value_end = bytes.find_first_of("\r\n", value_start);
    bytes.replace(
        value_start,
        value_end == std::string::npos
            ? std::string::npos
            : value_end - value_start,
        value);
    return bytes;
}

std::string remove_config_key(std::string bytes, std::string_view key)
{
    const std::string prefix = std::string(key) + "=";
    const auto start = bytes.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("runtime fixture key not found");
    }
    auto end = bytes.find('\n', start);
    end = end == std::string::npos ? bytes.size() : end + 1;
    bytes.erase(start, end - start);
    return bytes;
}

void test_no_arguments_preserve_historical_defaults()
{
    const auto options = simcore_host::parse_runtime_options({}, defaults());
    require(options.vehicle_config_path == "default-vehicle.cfg",
            "no-argument vehicle path must remain compatible");
    require(options.map_package_path == "default-map",
            "no-argument MapPackage path must remain compatible");
    require(options.ws_port == 9000 && options.physics_frequency_hz == 60.0,
            "no-argument port and physics frequency must remain compatible");
    require(options.origin_lat_deg == 40.70694
            && options.origin_lon_deg == -74.01083
            && options.origin_alt_m == 0.0,
            "no-argument origin must remain compatible");
    require(options.spawn_heading_deg == 0.0f,
            "no-argument heading must remain compatible");
    require(options.command_timeout == std::chrono::milliseconds(250)
            && options.max_command_queue_age == std::chrono::milliseconds(100)
            && options.hard_command_timeout == std::chrono::seconds(1),
            "no-argument lease timing must remain compatible");
    require(options.source_id == "simcore-cpp-host" && !options.demo_entities,
            "no-argument source and demo mode must remain compatible");
}

void test_tracked_config_is_complete_and_resolves_relative_paths()
{
    const auto tracked = std::filesystem::absolute(
        SIMCORE_TEST_RUNTIME_CONFIG_PATH).lexically_normal();
    const auto options = simcore_host::parse_runtime_options(
        {"--runtime-config", tracked.string()}, defaults());
    require(options.runtime_config_path == tracked,
            "loaded runtime config path must be retained for diagnostics");
    require(options.vehicle_config_path
                == (tracked.parent_path() / "vehicle_sedan.cfg").lexically_normal(),
            "vehicle config must resolve relative to the runtime cfg");
    require(options.map_package_path
                == (tracked.parent_path()
                    / "../../../map_packages/wall_broad_v1").lexically_normal(),
            "MapPackage must resolve relative to the runtime cfg");
    require(options.ws_port == 9000 && options.physics_frequency_hz == 60.0,
            "tracked scalar runtime values must load");
}

void test_cli_overrides_config_for_every_runtime_value()
{
    const auto options = simcore_host::parse_runtime_options(
        {
            "--runtime-config", SIMCORE_TEST_RUNTIME_CONFIG_PATH,
            "--vehicle-config", "override-vehicle.cfg",
            "--map-package", "override-map",
            "--ws-port", "9101",
            "--physics-hz", "120",
            "--origin-lat", "41.25",
            "--origin-lon", "-73.5",
            "--origin-alt", "12.75",
            "--spawn-heading", "45",
            "--command-timeout-ms", "300",
            "--max-command-queue-age-ms", "120",
            "--hard-command-timeout-ms", "1500",
            "--source-id", "simcore-test-host",
            "--demo-entities",
        },
        defaults());
    require(options.vehicle_config_path == "override-vehicle.cfg"
            && options.map_package_path == "override-map",
            "CLI paths must override cfg paths");
    require(options.ws_port == 9101 && options.physics_frequency_hz == 120.0,
            "CLI network and tick values must override cfg values");
    require(options.origin_lat_deg == 41.25
            && options.origin_lon_deg == -73.5
            && options.origin_alt_m == 12.75
            && options.spawn_heading_deg == 45.0f,
            "CLI origin and heading must override cfg values");
    require(options.command_timeout == std::chrono::milliseconds(300)
            && options.max_command_queue_age == std::chrono::milliseconds(120)
            && options.hard_command_timeout == std::chrono::milliseconds(1500),
            "CLI lease values must override cfg values");
    require(options.source_id == "simcore-test-host" && options.demo_entities,
            "CLI source and demo mode must override cfg values");
}

void test_no_demo_entities_overrides_true_config()
{
    TemporaryConfig config(replace_config_value(
        read_file(SIMCORE_TEST_RUNTIME_CONFIG_PATH),
        "demo_entities", "true"));
    const auto options = simcore_host::parse_runtime_options(
        {"--runtime-config", config.path.string(), "--no-demo-entities"},
        defaults());
    require(!options.demo_entities,
            "negative demo CLI flag must override cfg true");
}

void test_unknown_duplicate_and_missing_cli_are_rejected()
{
    require_rejected(
        [] {
            (void)simcore_host::parse_runtime_options(
                {"--not-a-runtime-option"}, defaults());
        },
        "unknown CLI options must fail closed");
    require_rejected(
        [] {
            (void)simcore_host::parse_runtime_options(
                {"--ws-port", "9000", "--ws-port", "9001"}, defaults());
        },
        "duplicate CLI options must fail closed");
    require_rejected(
        [] {
            (void)simcore_host::parse_runtime_options(
                {"--demo-entities", "--no-demo-entities"}, defaults());
        },
        "opposite demo CLI flags must count as duplicates");
    require_rejected(
        [] {
            (void)simcore_host::parse_runtime_options(
                {"--source-id"}, defaults());
        },
        "CLI options without required values must fail closed");
}

void test_config_unknown_duplicate_and_missing_keys_are_rejected()
{
    const auto tracked = read_file(SIMCORE_TEST_RUNTIME_CONFIG_PATH);
    TemporaryConfig unknown(tracked + "\nmisspelled_port=9000\n");
    require_rejected(
        [&] {
            (void)simcore_host::parse_runtime_options(
                {"--runtime-config", unknown.path.string()}, defaults());
        },
        "unknown runtime config keys must fail closed");

    TemporaryConfig duplicate(tracked + "\nws_port=9001\n");
    require_rejected(
        [&] {
            (void)simcore_host::parse_runtime_options(
                {"--runtime-config", duplicate.path.string()}, defaults());
        },
        "duplicate runtime config keys must fail closed");

    TemporaryConfig missing(remove_config_key(tracked, "source_id"));
    require_rejected(
        [&] {
            (void)simcore_host::parse_runtime_options(
                {"--runtime-config", missing.path.string()}, defaults());
        },
        "missing runtime config keys must fail closed");
}

void test_nonfinite_ranges_and_timeout_relationships_are_rejected()
{
    const auto tracked = read_file(SIMCORE_TEST_RUNTIME_CONFIG_PATH);
    for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{
             {"physics_hz", "nan"},
             {"ws_port", "0"},
             {"origin_lat_deg", "90.1"},
             {"origin_lon_deg", "-180.1"},
             {"origin_alt_m", "inf"},
             {"spawn_heading_deg", "360"},
             {"command_timeout_ms", "0"},
             {"max_command_queue_age_ms", "251"},
             {"hard_command_timeout_ms", "250"},
             {"source_id", "contains whitespace"},
         }) {
        TemporaryConfig invalid(replace_config_value(tracked, key, value));
        require_rejected(
            [&] {
                (void)simcore_host::parse_runtime_options(
                    {"--runtime-config", invalid.path.string()}, defaults());
            },
            "invalid finite/range/runtime relationship must fail closed");
    }
}

void test_optional_traffic_config_and_cli()
{
    TemporaryConfig configured(read_file(SIMCORE_TEST_RUNTIME_CONFIG_PATH)
        + "\ntraffic_network=networks/traffic.json\n");
    const auto loaded = simcore_host::parse_runtime_options(
        {"--runtime-config", configured.path.string()}, defaults());
    require(loaded.traffic_network_path.has_value(), "optional traffic key must load");
    require(*loaded.traffic_network_path
                == (configured.path.parent_path() / "networks/traffic.json").lexically_normal(),
            "traffic path must be config relative");
    const auto overridden = simcore_host::parse_runtime_options(
        {"--runtime-config", configured.path.string(), "--traffic-network", "cli.json"}, defaults());
    require(overridden.traffic_network_path == std::filesystem::path("cli.json"),
            "explicit traffic CLI must override cfg");
    require(!simcore_host::parse_runtime_options({}, defaults()).traffic_network_path,
            "historical defaults must not require traffic");
    require_rejected([] { (void)simcore_host::parse_runtime_options(
        {"--traffic-network", "a", "--traffic-network", "b"}, defaults()); },
        "duplicate traffic option must fail");
}

void test_help_is_available_and_not_mixed_with_runtime_changes()
{
    const auto options = simcore_host::parse_runtime_options(
        {"--help"}, defaults());
    require(options.show_help, "--help must return a successful help result");
    const auto help = simcore_host::runtime_options_help(defaults());
    require(help.find("built-in defaults < --runtime-config cfg < CLI")
                != std::string::npos
            && help.find("--hard-command-timeout-ms") != std::string::npos
            && help.find("--no-demo-entities") != std::string::npos,
            "help must document precedence and all safety/runtime overrides");
    require_rejected(
        [] {
            (void)simcore_host::parse_runtime_options(
                {"--help", "--ws-port", "9001"}, defaults());
        },
        "help mixed with mutations must be rejected");
}

void test_optional_lane_npc_options_are_strict_and_overridable()
{
    require(simcore_host::parse_runtime_options({}, defaults()).npc_route.empty(),
            "legacy runtimes must not spawn lane traffic implicitly");
    TemporaryConfig configured(read_file(SIMCORE_TEST_RUNTIME_CONFIG_PATH)
        + "\ntraffic_network=traffic.json\nnpc_route=260,270,280\nnpc_route_loop=true\n"
          "npc_alternate_route=360,370,380\nnpc_start_offset_m=96\nnpc_max_speed_mps=6\n"
          "npc_count=4\nnpc_spacing_m=180\nnpc_autonomous=true\n");
    const auto loaded = simcore_host::parse_runtime_options(
        {"--runtime-config", configured.path.string()}, defaults());
    require(loaded.npc_route == std::vector<std::uint32_t>{260, 270, 280}
                && loaded.npc_alternate_route == std::vector<std::uint32_t>{360, 370, 380}
                && loaded.npc_route_loop && loaded.npc_start_offset_m == 96
                && loaded.npc_max_speed_mps == 6 && loaded.npc_count == 4
                && loaded.npc_spacing_m == 180 && loaded.npc_autonomous,
            "optional NPC route configuration must preserve ordered lane IDs and geometry units");
    const auto disabled = simcore_host::parse_runtime_options(
        {"--runtime-config", configured.path.string(), "--npc-route", "none"}, defaults());
    require(disabled.npc_route.empty() && disabled.npc_alternate_route.empty(),
            "CLI none must disable every configured lane NPC route");
    const auto changed = simcore_host::parse_runtime_options(
        {"--runtime-config", configured.path.string(), "--npc-route", "310,340",
         "--npc-alternate-route", "410,440", "--npc-loop", "false",
         "--npc-start-offset", "3", "--npc-max-speed", "4",
         "--npc-count", "3", "--npc-spacing", "90", "--npc-autonomous", "false"}, defaults());
    require(changed.npc_route == std::vector<std::uint32_t>{310, 340}
                && changed.npc_alternate_route == std::vector<std::uint32_t>{410, 440}
                && !changed.npc_route_loop && changed.npc_start_offset_m == 3
                && changed.npc_max_speed_mps == 4 && changed.npc_count == 3
                && changed.npc_spacing_m == 90 && !changed.npc_autonomous,
            "all NPC command-line values must override the optional cfg values");
    for (const std::vector<std::string> arguments : {
            std::vector<std::string>{"--npc-route", "100"},
            {"--traffic-network", "a", "--npc-route", "0"},
            {"--traffic-network", "a", "--npc-route", "100,"},
            {"--traffic-network", "a", "--npc-route", "100,,110"},
            {"--traffic-network", "a", "--npc-route", "4294967296"},
            {"--traffic-network", "a", "--npc-route", "100.0"},
            {"--traffic-network", "a", "--npc-route", "100", "--demo-entities"},
            {"--npc-loop", "1"}, {"--npc-start-offset", "-1"},
            {"--npc-autonomous", "yes"},
            {"--npc-autonomous", "true", "--npc-autonomous", "false"},
            {"--npc-max-speed", "nan"}, {"--npc-max-speed", "0"},
            {"--npc-max-speed", "26"}, {"--npc-count", "0"},
            {"--npc-count", "17"}, {"--npc-spacing", "7"},
            {"--traffic-network", "a", "--npc-alternate-route", "100,110"},
            {"--npc-route", "none", "--npc-route", "none"}}) {
        require_rejected([&] { (void)simcore_host::parse_runtime_options(arguments, defaults()); },
            "malformed, duplicate or unsafe NPC options must be rejected");
    }
}

} // namespace

int main()
{
    try {
        test_no_arguments_preserve_historical_defaults();
        test_tracked_config_is_complete_and_resolves_relative_paths();
        test_cli_overrides_config_for_every_runtime_value();
        test_no_demo_entities_overrides_true_config();
        test_unknown_duplicate_and_missing_cli_are_rejected();
        test_config_unknown_duplicate_and_missing_keys_are_rejected();
        test_nonfinite_ranges_and_timeout_relationships_are_rejected();
        test_help_is_available_and_not_mixed_with_runtime_changes();
        test_optional_traffic_config_and_cli();
        test_optional_lane_npc_options_are_strict_and_overridable();
        std::cout << "Runtime options tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime options test failure: " << error.what() << "\n";
        return 1;
    }
}
