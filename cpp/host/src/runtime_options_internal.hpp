#pragma once

#include "runtime_options.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace simcore_host::runtime_options_internal {

using Nanoseconds = std::chrono::nanoseconds;

double parse_finite_double(const std::string& key, const std::string& text);
std::uint64_t parse_unsigned_integer(
    const std::string& key,
    const std::string& text);
bool parse_boolean(const std::string& key, const std::string& text);
std::vector<std::uint32_t> parse_npc_route(const std::string& text);
Nanoseconds parse_milliseconds(
    const std::string& key,
    const std::string& text);
std::filesystem::path require_nonempty_path(
    const std::string& key,
    const std::string& text);

RuntimeOptions load_runtime_config(
    const std::filesystem::path& input_path,
    RuntimeOptions defaults);

RuntimeOptions parse_runtime_cli(
    const std::vector<std::string>& arguments,
    RuntimeOptions defaults);

} // namespace simcore_host::runtime_options_internal
