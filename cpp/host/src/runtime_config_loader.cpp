#include "runtime_options_internal.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace simcore_host::runtime_options_internal {
namespace {

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::filesystem::path resolve_config_relative_path(
    const std::filesystem::path& config_path,
    const std::filesystem::path& value)
{
    if (value.is_absolute()) {
        return value.lexically_normal();
    }
    return (config_path.parent_path() / value).lexically_normal();
}

void apply_values(
    RuntimeOptions& options,
    const std::unordered_map<std::string, std::string>& values,
    const std::optional<std::filesystem::path>& config_path)
{
    const auto resolve_path = [&](const std::string& key) {
        auto path = require_nonempty_path(key, values.at(key));
        if (config_path) {
            path = resolve_config_relative_path(*config_path, path);
        }
        return path;
    };

    options.vehicle_config_path = resolve_path("vehicle_config");
    options.map_package_path = resolve_path("map_package");
    if (values.contains("traffic_network")) {
        options.traffic_network_path = resolve_path("traffic_network");
    }
    if (values.contains("npc_route")) options.npc_route = parse_npc_route(values.at("npc_route"));
    if (values.contains("npc_alternate_route")) options.npc_alternate_route =
        parse_npc_route(values.at("npc_alternate_route"));
    if (values.contains("npc_route_loop")) options.npc_route_loop = parse_boolean(
        "npc_route_loop", values.at("npc_route_loop"));
    if (values.contains("npc_autonomous")) options.npc_autonomous = parse_boolean(
        "npc_autonomous", values.at("npc_autonomous"));
    if (values.contains("npc_start_offset_m")) options.npc_start_offset_m = parse_finite_double(
        "npc_start_offset_m", values.at("npc_start_offset_m"));
    if (values.contains("npc_max_speed_mps")) options.npc_max_speed_mps = parse_finite_double(
        "npc_max_speed_mps", values.at("npc_max_speed_mps"));
    if (values.contains("npc_count")) {
        const auto count = parse_unsigned_integer("npc_count", values.at("npc_count"));
        if (count > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("npc_count exceeds uint32 range");
        }
        options.npc_count = static_cast<std::uint32_t>(count);
    }
    if (values.contains("npc_spacing_m")) options.npc_spacing_m = parse_finite_double(
        "npc_spacing_m", values.at("npc_spacing_m"));

    const auto port = parse_unsigned_integer("ws_port", values.at("ws_port"));
    if (port > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("Runtime option 'ws_port' exceeds 65535");
    }
    options.ws_port = static_cast<std::uint16_t>(port);
    options.physics_frequency_hz = parse_finite_double(
        "physics_hz", values.at("physics_hz"));
    options.origin_lat_deg = parse_finite_double(
        "origin_lat_deg", values.at("origin_lat_deg"));
    options.origin_lon_deg = parse_finite_double(
        "origin_lon_deg", values.at("origin_lon_deg"));
    options.origin_alt_m = parse_finite_double(
        "origin_alt_m", values.at("origin_alt_m"));
    options.spawn_heading_deg = static_cast<float>(parse_finite_double(
        "spawn_heading_deg", values.at("spawn_heading_deg")));
    options.command_timeout = parse_milliseconds(
        "command_timeout_ms", values.at("command_timeout_ms"));
    options.max_command_queue_age = parse_milliseconds(
        "max_command_queue_age_ms", values.at("max_command_queue_age_ms"));
    options.hard_command_timeout = parse_milliseconds(
        "hard_command_timeout_ms", values.at("hard_command_timeout_ms"));
    options.source_id = values.at("source_id");
    options.demo_entities = parse_boolean(
        "demo_entities", values.at("demo_entities"));
}

} // namespace

double parse_finite_double(const std::string& key, const std::string& text)
{
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0'
        || !std::isfinite(value)) {
        throw std::runtime_error(
            "Invalid finite number for runtime option '" + key + "': " + text);
    }
    return value;
}

std::uint64_t parse_unsigned_integer(
    const std::string& key,
    const std::string& text)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::runtime_error(
            "Invalid unsigned integer for runtime option '" + key + "': "
            + text);
    }
    return value;
}

bool parse_boolean(const std::string& key, const std::string& text)
{
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    throw std::runtime_error(
        "Invalid boolean for runtime option '" + key
        + "' (expected true or false): " + text);
}

std::vector<std::uint32_t> parse_npc_route(const std::string& text)
{
    if (text == "none") return {};
    std::vector<std::uint32_t> route;
    std::size_t begin = 0;
    do {
        const auto end = text.find(',', begin);
        const auto token = trim(std::string_view(text).substr(begin,
            end == std::string::npos ? text.size() - begin : end - begin));
        const auto id = parse_unsigned_integer("npc_route", token);
        if (id == 0 || id > std::numeric_limits<std::uint32_t>::max()
            || route.size() >= 64) {
            throw std::invalid_argument("npc_route requires 1..64 positive uint32 lane IDs, or none");
        }
        route.push_back(static_cast<std::uint32_t>(id));
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (begin <= text.size());
    return route;
}

Nanoseconds parse_milliseconds(const std::string& key, const std::string& text)
{
    const std::uint64_t value = parse_unsigned_integer(key, text);
    constexpr std::uint64_t maximum_convertible =
        static_cast<std::uint64_t>(
            std::numeric_limits<Nanoseconds::rep>::max())
        / 1'000'000ull;
    if (value > maximum_convertible) {
        throw std::runtime_error(
            "Runtime option '" + key + "' exceeds nanosecond range");
    }
    return std::chrono::duration_cast<Nanoseconds>(
        std::chrono::milliseconds(value));
}

std::filesystem::path require_nonempty_path(
    const std::string& key,
    const std::string& text)
{
    if (text.empty()) {
        throw std::runtime_error(
            "Runtime option '" + key + "' requires a non-empty path");
    }
    return std::filesystem::path(text);
}

RuntimeOptions load_runtime_config(
    const std::filesystem::path& input_path,
    RuntimeOptions defaults)
{
    const auto config_path = std::filesystem::absolute(input_path).lexically_normal();
    std::ifstream input(config_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "Runtime configuration not found: " + config_path.string());
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error(
                "Runtime configuration line " + std::to_string(line_number)
                + " must use key=value");
        }
        const std::string key = trim(
            std::string_view(line).substr(0, separator));
        const std::string value = trim(
            std::string_view(line).substr(separator + 1));
        if (key.empty() || value.empty()) {
            throw std::runtime_error(
                "Runtime configuration line " + std::to_string(line_number)
                + " has an empty key or value");
        }
        if (!values.emplace(key, value).second) {
            throw std::runtime_error(
                "Duplicate runtime configuration key '" + key + "'");
        }
    }

    static const std::unordered_set<std::string> expected_keys{
        "format_version",
        "vehicle_config",
        "map_package",
        "ws_port",
        "physics_hz",
        "origin_lat_deg",
        "origin_lon_deg",
        "origin_alt_m",
        "spawn_heading_deg",
        "command_timeout_ms",
        "max_command_queue_age_ms",
        "hard_command_timeout_ms",
        "source_id",
        "demo_entities",
    };
    for (const auto& [key, value] : values) {
        (void)value;
        if (!expected_keys.contains(key) && key != "traffic_network"
            && key != "npc_route" && key != "npc_alternate_route"
            && key != "npc_route_loop" && key != "npc_start_offset_m"
            && key != "npc_max_speed_mps" && key != "npc_count"
            && key != "npc_spacing_m" && key != "npc_autonomous") {
            throw std::runtime_error(
                "Unknown runtime configuration key '" + key + "'");
        }
    }
    for (const auto& key : expected_keys) {
        if (!values.contains(key)) {
            throw std::runtime_error(
                "Missing runtime configuration key '" + key + "'");
        }
    }

    const auto format_version = parse_unsigned_integer(
        "format_version", values.at("format_version"));
    if (format_version != kRuntimeConfigFormatVersion) {
        throw std::runtime_error(
            "Unsupported runtime configuration format_version: "
            + values.at("format_version"));
    }

    apply_values(defaults, values, config_path);
    defaults.runtime_config_path = config_path;
    validate_runtime_options(defaults);
    return defaults;
}

} // namespace simcore_host::runtime_options_internal
