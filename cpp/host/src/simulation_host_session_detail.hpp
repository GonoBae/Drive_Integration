#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace simcore_host::simulation_host_session_detail {

inline constexpr std::size_t kMaxLifecycleIdentifierBytes = 256;

inline bool is_printable_ascii(std::string_view value)
{
    return std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x20u && byte <= 0x7eu;
    });
}

inline bool is_log_safe_identifier(
    std::string_view value,
    std::size_t maximum_bytes,
    bool allow_empty = false)
{
    return (allow_empty || !value.empty())
        && value.size() <= maximum_bytes
        && is_printable_ascii(value);
}

} // namespace simcore_host::simulation_host_session_detail
