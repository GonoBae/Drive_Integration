#pragma once

#include <cmath>
#include <optional>
#include <stdexcept>

namespace simcore_host {

struct GroundPointEnu {
    double east_m = 0.0;
    double north_m = 0.0;
    double up_m = 0.0;
};

struct GroundQueryRequest {
    GroundPointEnu origin_enu;
    double max_distance_m = 0.0;
};

struct GroundHit {
    GroundPointEnu point_enu;
    GroundPointEnu normal_enu{0.0, 0.0, 1.0};
    double distance_m = 0.0;
};

// Read-only boundary between vehicle dynamics and the authoritative terrain.
// Implementations cast a vertical ray along map_enu -Z and return the nearest
// hit. A future MapPackage implementation can replace FlatGroundQuery without
// changing the vehicle solver.
class GroundQuery {
public:
    virtual ~GroundQuery() = default;

    [[nodiscard]] virtual std::optional<GroundHit> query_down(
        const GroundQueryRequest& request) const = 0;
};

class FlatGroundQuery final : public GroundQuery {
public:
    explicit FlatGroundQuery(double height_enu_m = 0.0)
        : height_enu_m_(height_enu_m)
    {
        if (!std::isfinite(height_enu_m_)) {
            throw std::invalid_argument("Flat-ground height must be finite");
        }
    }

    [[nodiscard]] std::optional<GroundHit> query_down(
        const GroundQueryRequest& request) const override
    {
        if (!std::isfinite(request.origin_enu.east_m)
            || !std::isfinite(request.origin_enu.north_m)
            || !std::isfinite(request.origin_enu.up_m)
            || !std::isfinite(request.max_distance_m)
            || request.max_distance_m < 0.0) {
            return std::nullopt;
        }

        const double distance = request.origin_enu.up_m - height_enu_m_;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }

        return GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height_enu_m_},
            {0.0, 0.0, 1.0},
            distance};
    }

private:
    double height_enu_m_ = 0.0;
};

} // namespace simcore_host
