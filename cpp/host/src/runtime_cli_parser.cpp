#include "runtime_options_internal.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace simcore_host::runtime_options_internal {
namespace {

struct CliOption {
    std::string canonical_key;
    std::optional<std::string> value;
};

struct CliSpecification {
    const char* canonical_key;
    bool requires_value;
};

const std::unordered_map<std::string, CliSpecification>& cli_specifications()
{
    static const std::unordered_map<std::string, CliSpecification> specs{
        {"--runtime-config", {"runtime_config", true}},
        {"--vehicle-config", {"vehicle_config", true}},
        {"--map-package", {"map_package", true}},
        {"--traffic-network", {"traffic_network", true}},
        {"--npc-route", {"npc_route", true}},
        {"--npc-alternate-route", {"npc_alternate_route", true}},
        {"--npc-loop", {"npc_route_loop", true}},
        {"--npc-start-offset", {"npc_start_offset_m", true}},
        {"--npc-max-speed", {"npc_max_speed_mps", true}},
        {"--npc-count", {"npc_count", true}},
        {"--npc-spacing", {"npc_spacing_m", true}},
        {"--ws-port", {"ws_port", true}},
        {"--physics-hz", {"physics_hz", true}},
        {"--origin-lat", {"origin_lat_deg", true}},
        {"--origin-lon", {"origin_lon_deg", true}},
        {"--origin-alt", {"origin_alt_m", true}},
        {"--spawn-heading", {"spawn_heading_deg", true}},
        {"--command-timeout-ms", {"command_timeout_ms", true}},
        {"--max-command-queue-age-ms", {"max_command_queue_age_ms", true}},
        {"--hard-command-timeout-ms", {"hard_command_timeout_ms", true}},
        {"--source-id", {"source_id", true}},
        {"--demo-entities", {"demo_entities", false}},
        {"--no-demo-entities", {"demo_entities", false}},
        {"--help", {"help", false}},
        {"-h", {"help", false}},
    };
    return specs;
}

std::vector<CliOption> parse_cli(const std::vector<std::string>& arguments)
{
    std::vector<CliOption> parsed;
    std::unordered_set<std::string> seen;
    const auto& specifications = cli_specifications();
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        const auto specification = specifications.find(argument);
        if (specification == specifications.end()) {
            throw std::invalid_argument("Unknown argument: " + argument);
        }
        const std::string canonical = specification->second.canonical_key;
        if (!seen.insert(canonical).second) {
            throw std::invalid_argument(
                "Duplicate command-line option: " + argument);
        }

        std::optional<std::string> value;
        if (specification->second.requires_value) {
            if (index + 1 >= arguments.size()) {
                throw std::invalid_argument(argument + " requires a value");
            }
            value = arguments[++index];
            if (value->empty()) {
                throw std::invalid_argument(argument + " requires a non-empty value");
            }
        }
        parsed.push_back({canonical, std::move(value)});
    }
    return parsed;
}

const CliOption* find_cli_option(
    const std::vector<CliOption>& options,
    std::string_view key)
{
    const auto found = std::find_if(
        options.begin(), options.end(), [&](const CliOption& option) {
            return option.canonical_key == key;
        });
    return found == options.end() ? nullptr : &*found;
}

void apply_cli_option(RuntimeOptions& options, const CliOption& option)
{
    const auto require_value = [&]() -> const std::string& {
        if (!option.value) {
            throw std::logic_error(
                "Runtime option parser lost a required command-line value");
        }
        return *option.value;
    };

    if (option.canonical_key == "runtime_config"
        || option.canonical_key == "help") {
        return;
    }
    if (option.canonical_key == "vehicle_config") {
        options.vehicle_config_path = require_nonempty_path(
            "vehicle_config", require_value());
    } else if (option.canonical_key == "map_package") {
        options.map_package_path = require_nonempty_path(
            "map_package", require_value());
    } else if (option.canonical_key == "traffic_network") {
        options.traffic_network_path = require_nonempty_path(
            "traffic_network", require_value());
    } else if (option.canonical_key == "npc_route") {
        options.npc_route = parse_npc_route(require_value());
        if (options.npc_route.empty()) options.npc_alternate_route.clear();
    } else if (option.canonical_key == "npc_alternate_route") {
        options.npc_alternate_route = parse_npc_route(require_value());
    } else if (option.canonical_key == "npc_route_loop") {
        options.npc_route_loop = parse_boolean("npc_route_loop", require_value());
    } else if (option.canonical_key == "npc_start_offset_m") {
        options.npc_start_offset_m = parse_finite_double("npc_start_offset_m", require_value());
    } else if (option.canonical_key == "npc_max_speed_mps") {
        options.npc_max_speed_mps = parse_finite_double("npc_max_speed_mps", require_value());
    } else if (option.canonical_key == "npc_count") {
        const auto count = parse_unsigned_integer("npc_count", require_value());
        if (count > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("npc_count exceeds uint32 range");
        }
        options.npc_count = static_cast<std::uint32_t>(count);
    } else if (option.canonical_key == "npc_spacing_m") {
        options.npc_spacing_m = parse_finite_double("npc_spacing_m", require_value());
    } else if (option.canonical_key == "ws_port") {
        const auto port = parse_unsigned_integer("ws_port", require_value());
        if (port > std::numeric_limits<std::uint16_t>::max()) {
            throw std::runtime_error("Runtime option 'ws_port' exceeds 65535");
        }
        options.ws_port = static_cast<std::uint16_t>(port);
    } else if (option.canonical_key == "physics_hz") {
        options.physics_frequency_hz = parse_finite_double(
            "physics_hz", require_value());
    } else if (option.canonical_key == "origin_lat_deg") {
        options.origin_lat_deg = parse_finite_double(
            "origin_lat_deg", require_value());
    } else if (option.canonical_key == "origin_lon_deg") {
        options.origin_lon_deg = parse_finite_double(
            "origin_lon_deg", require_value());
    } else if (option.canonical_key == "origin_alt_m") {
        options.origin_alt_m = parse_finite_double(
            "origin_alt_m", require_value());
    } else if (option.canonical_key == "spawn_heading_deg") {
        options.spawn_heading_deg = static_cast<float>(parse_finite_double(
            "spawn_heading_deg", require_value()));
    } else if (option.canonical_key == "command_timeout_ms") {
        options.command_timeout = parse_milliseconds(
            "command_timeout_ms", require_value());
    } else if (option.canonical_key == "max_command_queue_age_ms") {
        options.max_command_queue_age = parse_milliseconds(
            "max_command_queue_age_ms", require_value());
    } else if (option.canonical_key == "hard_command_timeout_ms") {
        options.hard_command_timeout = parse_milliseconds(
            "hard_command_timeout_ms", require_value());
    } else if (option.canonical_key == "source_id") {
        options.source_id = require_value();
    } else if (option.canonical_key == "demo_entities") {
        // The original token is represented by the absence/presence of a
        // value, so recover the requested state from the public parse list is
        // intentionally avoided. parse_runtime_options handles this option.
        throw std::logic_error("demo_entities must be applied with its CLI token");
    } else {
        throw std::logic_error(
            "Unhandled command-line runtime option: " + option.canonical_key);
    }
}

} // namespace

RuntimeOptions parse_runtime_cli(
    const std::vector<std::string>& arguments,
    RuntimeOptions defaults)
{
    const auto parsed = parse_cli(arguments);
    if (find_cli_option(parsed, "help") != nullptr) {
        if (parsed.size() != 1) {
            throw std::invalid_argument(
                "--help cannot be combined with runtime options");
        }
        defaults.show_help = true;
        return defaults;
    }

    if (const auto* config = find_cli_option(parsed, "runtime_config")) {
        defaults = load_runtime_config(*config->value, std::move(defaults));
    }

    // Preserve the original token solely for the paired bool flags. All other
    // options use their canonical parsed representation.
    std::size_t parsed_index = 0;
    for (std::size_t argument_index = 0;
         argument_index < arguments.size(); ++argument_index, ++parsed_index) {
        const auto& argument = arguments[argument_index];
        const auto& specification = cli_specifications().at(argument);
        const auto& option = parsed.at(parsed_index);
        if (option.canonical_key == "demo_entities") {
            defaults.demo_entities = argument == "--demo-entities";
        } else {
            apply_cli_option(defaults, option);
        }
        if (specification.requires_value) {
            ++argument_index;
        }
    }

    validate_runtime_options(defaults);
    return defaults;
}

} // namespace simcore_host::runtime_options_internal
