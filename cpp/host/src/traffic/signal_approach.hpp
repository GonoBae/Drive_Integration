#pragma once

#include "traffic/traffic_network.hpp"

namespace simcore_host {

// Different protected movements can share one physical approach head. A
// pre-stopline lane change adopts the destination lane's movement; it must
// never reinterpret a different approach's green as permission to enter.
inline bool same_signal_approach(const TrafficNetwork& network,
    std::uint32_t first, std::uint32_t second)
{
    if (first == second) return true;
    if (first == 0 || second == 0) return false;
    for (const auto& signal : network.signals) {
        const auto matches = [&](std::uint32_t group) {
            return signal.group_id == group || signal.left_group_id == group;
        };
        if (matches(first) && matches(second)) return true;
    }
    return false;
}

} // namespace simcore_host
