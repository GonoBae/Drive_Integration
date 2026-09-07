#include "physics/vehicle_physics.hpp"
#include "physics/vehicle_config.hpp"
#include "physics/runtime_tire_support.hpp"
#include "terrain/map_package_ground_query.hpp"
#include "terrain/map_package_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr double kInitialLat = 40.7069;
constexpr double kInitialLon = -74.0095;

class PlaneGroundQuery final : public simcore_host::GroundQuery {
public:
    PlaneGroundQuery(
        double east_slope,
        double north_slope,
        double intercept,
        simcore_host::GroundSurfaceMaterialId material_id =
            simcore_host::GroundSurfaceMaterialId::Default,
        double friction_multiplier = 1.0)
        : east_slope_(east_slope)
        , north_slope_(north_slope)
        , intercept_(intercept)
        , material_id_(material_id)
        , friction_multiplier_(friction_multiplier)
    {
    }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const double height = intercept_
            + east_slope_ * request.origin_enu.east_m
            + north_slope_ * request.origin_enu.north_m;
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        // Deliberately return a non-unit normal; VehiclePhysics owns the
        // normalization at the GroundQuery boundary.
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {-east_slope_, -north_slope_, 1.0},
            distance,
            material_id_,
            friction_multiplier_};
    }

private:
    double east_slope_ = 0.0;
    double north_slope_ = 0.0;
    double intercept_ = 0.0;
    simcore_host::GroundSurfaceMaterialId material_id_ =
        simcore_host::GroundSurfaceMaterialId::Default;
    double friction_multiplier_ = 1.0;
};

class RampPlateauGroundQuery final : public simcore_host::GroundQuery {
public:
    explicit RampPlateauGroundQuery(
        double rise_m,
        bool retain_far_plateau = true,
        bool raise_opposite_far_support = false,
        double curb_north_m = 5.0,
        double near_road_raster_undershoot_m = 0.0)
        : rise_m_(rise_m)
        , retain_far_plateau_(retain_far_plateau)
        , raise_opposite_far_support_(raise_opposite_far_support)
        , curb_north_m_(curb_north_m)
        , near_road_raster_undershoot_m_(near_road_raster_undershoot_m) {}

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const double ramp_start_north_m = curb_north_m_ - 0.25;
        const double ramp_end_north_m = curb_north_m_ + 0.25;
        const double alpha = std::clamp(
            (request.origin_enu.north_m - ramp_start_north_m)
                / (ramp_end_north_m - ramp_start_north_m),
            0.0, 1.0);
        double height = rise_m_ * alpha;
        double slope = request.origin_enu.north_m > ramp_start_north_m
                && request.origin_enu.north_m < ramp_end_north_m
            ? rise_m_ / (ramp_end_north_m - ramp_start_north_m)
            : 0.0;
        if (!retain_far_plateau_
            && request.origin_enu.north_m > curb_north_m_ + 0.45) {
            const double descent_alpha = std::clamp(
                (request.origin_enu.north_m - (curb_north_m_ + 0.45))
                    / 0.30,
                0.0,
                1.0);
            height *= 1.0 - descent_alpha;
            slope = descent_alpha > 0.0 && descent_alpha < 1.0
                ? -rise_m_ / 0.30
                : 0.0;
        }
        if (raise_opposite_far_support_
            && std::abs(
                request.origin_enu.north_m
                    - (curb_north_m_ - (0.075 + 0.32 * 3.0))) < 1e-6) {
            // Deliberately expose a probe-only false friend on the side
            // opposite the near rise. A max(far +/-) implementation would
            // authorize it; same-side topology must reject it.
            height = rise_m_;
            slope = 0.0;
        }
        if (near_road_raster_undershoot_m_ > 0.0
            && std::abs(request.origin_enu.north_m
                - (curb_north_m_ - (0.075 + 0.32))) < 1e-6) {
            // Model a 50 cm heightfield cell diagonal whose road-side near
            // sample is slightly below the true road plane. The far plateau
            // and semantic collider still carry the authored step height.
            height -= near_road_raster_undershoot_m_;
        }
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m)
            return std::nullopt;
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {0.0, -slope, 1.0}, distance};
    }

private:
    double rise_m_ = 0.0;
    bool retain_far_plateau_ = true;
    bool raise_opposite_far_support_ = false;
    double curb_north_m_ = 5.0;
    double near_road_raster_undershoot_m_ = 0.0;
};

class NoGroundQuery final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest&) const override
    {
        return std::nullopt;
    }
};

class WestHalfGroundQuery final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (request.origin_enu.east_m >= 0.0) {
            return std::nullopt;
        }
        const double distance = request.origin_enu.up_m;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, 0.0},
            {0.0, 0.0, 1.0},
            distance};
    }
};

class DownwardNormalGroundQuery final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, 0.0},
            {0.0, 0.0, -1.0},
            request.origin_enu.up_m};
    }
};

class NorthBoundedGroundQuery final : public simcore_host::GroundQuery {
public:
    explicit NorthBoundedGroundQuery(double maximum_north_m)
        : maximum_north_m_(maximum_north_m)
    {
    }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (request.origin_enu.north_m >= maximum_north_m_) {
            return std::nullopt;
        }
        const double distance = request.origin_enu.up_m;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, 0.0},
            {0.0, 0.0, 1.0},
            distance};
    }

private:
    double maximum_north_m_ = 0.0;
};

// A flat approach followed by a progressively steeper compound grade. At the
// 20 m mark this approximates the first locally baked Unreal Landscape that
// exposed downward-ray penetration lockout (about 18 degrees uphill and
// 20 degrees of cross slope).
class RisingCompoundGroundQuery final : public simcore_host::GroundQuery {
public:
    static double height_at(double east, double north)
    {
        const double run = std::max(0.0, north - 7.0);
        return 0.012 * run * run + 0.02 * run * east;
    }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const double run = std::max(0.0, request.origin_enu.north_m - 7.0);
        const double height = height_at(
            request.origin_enu.east_m,
            request.origin_enu.north_m);
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        const double east_slope = 0.02 * run;
        const double north_slope = run > 0.0
            ? 0.024 * run + 0.02 * request.origin_enu.east_m
            : 0.0;
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {-east_slope, -north_slope, 1.0},
            distance};
    }
};

class ToggleableFlatGroundQuery final : public simcore_host::GroundQuery {
public:
    explicit ToggleableFlatGroundQuery(bool enabled)
        : enabled_(enabled)
    {
    }

    void set_enabled(bool enabled)
    {
        enabled_ = enabled;
    }

    void set_west_half_only(bool west_half_only)
    {
        west_half_only_ = west_half_only;
    }

    void set_height(double height_m) { height_m_ = height_m; }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (!enabled_
            || (west_half_only_ && request.origin_enu.east_m >= 0.0)) {
            return std::nullopt;
        }
        const double distance = request.origin_enu.up_m - height_m_;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height_m_},
            {0.0, 0.0, 1.0},
            distance};
    }

private:
    bool enabled_ = false;
    bool west_half_only_ = false;
    double height_m_ = 0.0;
};

// Starts flat, then raises only the canonical front-left quadrant.  The
// midlines are captured from the vehicle immediately before activation, so
// the fixture remains deterministic without reaching into VehiclePhysics.
class ToggleableFrontLeftStepGroundQuery final
    : public simcore_host::GroundQuery {
public:
    void activate(double centre_east_m, double centre_north_m, double height_m)
    {
        active_ = true;
        centre_east_m_ = centre_east_m;
        centre_north_m_ = centre_north_m;
        height_m_ = height_m;
    }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const bool on_front_left_step = active_
            && request.origin_enu.east_m < centre_east_m_
            && request.origin_enu.north_m > centre_north_m_;
        const double height = on_front_left_step ? height_m_ : 0.0;
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {0.0, 0.0, 1.0},
            distance};
    }

private:
    bool active_ = false;
    double centre_east_m_ = 0.0;
    double centre_north_m_ = 0.0;
    double height_m_ = 0.0;
};

// Gives only the canonical front-right wheel a different local tangent plane.
// Its height is anchored ahead of the wheel when enabled so switching the
// normal does not itself create a suspension hard-stop.
class ToggleableFrontRightSlopeGroundQuery final
    : public simcore_host::GroundQuery {
public:
    void activate(
        double centre_east_m,
        double centre_north_m,
        double slope_anchor_north_m,
        double north_slope)
    {
        active_ = true;
        centre_east_m_ = centre_east_m;
        centre_north_m_ = centre_north_m;
        slope_anchor_north_m_ = slope_anchor_north_m;
        north_slope_ = north_slope;
    }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const bool on_front_right_slope = active_
            && request.origin_enu.east_m > centre_east_m_
            && request.origin_enu.north_m > centre_north_m_;
        const double height = on_front_right_slope
            ? north_slope_
                * (request.origin_enu.north_m - slope_anchor_north_m_)
            : 0.0;
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            on_front_right_slope
                ? simcore_host::GroundPointEnu{0.0, -north_slope_, 1.0}
                : simcore_host::GroundPointEnu{0.0, 0.0, 1.0},
            distance};
    }

private:
    bool active_ = false;
    double centre_east_m_ = 0.0;
    double centre_north_m_ = 0.0;
    double slope_anchor_north_m_ = 0.0;
    double north_slope_ = 0.0;
};

// Four independent, horizontal support pads. Their normals remain vertical,
// so a chassis attitude inferred only from averaged contact normals stays
// falsely level even though the wheel centres form a pitched and rolled
// support plane. This models a car straddling uneven blocks or a baked terrain
// transition whose individual tire triangles are locally flat.
class AdjustableFourPostGroundQuery final : public simcore_host::GroundQuery {
public:
    AdjustableFourPostGroundQuery(
        double front_minus_rear_height_m,
        double right_minus_left_height_m)
        : front_half_height_m_(front_minus_rear_height_m * 0.5)
        , right_half_height_m_(right_minus_left_height_m * 0.5)
    {
    }

    void set_blend(double blend)
    {
        blend_ = std::clamp(blend, 0.0, 1.0);
    }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        // At the canonical spawn heading, north selects the axle and east
        // selects the vehicle's right side. The quadrant boundaries remain
        // well inside the wheel footprint throughout this regression.
        const double axle_height = request.origin_enu.north_m >= 0.0
            ? front_half_height_m_ : -front_half_height_m_;
        const double side_height = request.origin_enu.east_m >= 0.0
            ? right_half_height_m_ : -right_half_height_m_;
        const double height = blend_ * (axle_height + side_height);
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {0.0, 0.0, 1.0},
            distance};
    }

private:
    double front_half_height_m_ = 0.0;
    double right_half_height_m_ = 0.0;
    double blend_ = 0.0;
};

// A C1-continuous flat-to-grade transition. The target slope is reached over
// transition_length_m, avoiding a vertical corner while still exercising the
// rapid attitude/contact change that occurs at the foot of an Unreal ramp.
class SteepRampGroundQuery final : public simcore_host::GroundQuery {
public:
    SteepRampGroundQuery(
        double grade_degrees,
        double transition_start_north_m,
        double transition_length_m)
        : target_slope_(std::tan(grade_degrees
              * std::numbers::pi_v<double> / 180.0))
        , transition_start_north_m_(transition_start_north_m)
        , transition_length_m_(transition_length_m)
    {
    }

    double height_at(double, double north) const
    {
        const double run = north - transition_start_north_m_;
        if (run <= 0.0) {
            return 0.0;
        }
        if (run >= transition_length_m_) {
            return target_slope_
                * (run - 0.5 * transition_length_m_);
        }

        const double t = run / transition_length_m_;
        // Integral of smoothstep(t) = 3t^2 - 2t^3.
        return target_slope_ * transition_length_m_
            * (t * t * t - 0.5 * t * t * t * t);
    }

    double north_slope_at(double north) const
    {
        const double run = north - transition_start_north_m_;
        if (run <= 0.0) {
            return 0.0;
        }
        if (run >= transition_length_m_) {
            return target_slope_;
        }

        const double t = run / transition_length_m_;
        return target_slope_ * (3.0 * t * t - 2.0 * t * t * t);
    }

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        const double height = height_at(
            request.origin_enu.east_m,
            request.origin_enu.north_m);
        const double distance = request.origin_enu.up_m - height;
        if (distance < 0.0 || distance > request.max_distance_m) {
            return std::nullopt;
        }
        return simcore_host::GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {0.0, -north_slope_at(request.origin_enu.north_m), 1.0},
            distance};
    }

private:
    double target_slope_ = 0.0;
    double transition_start_north_m_ = 0.0;
    double transition_length_m_ = 1.0;
};

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void advance(VehiclePhysics& physics, int steps)
{
    for (int i = 0; i < steps; ++i) {
        physics.update(kDt);
    }
}

VehiclePhysics make_vehicle()
{
    return VehiclePhysics(kInitialLat, kInitialLon, 0.0, 0.f);
}

double signed_heading_delta_radians(float current_degrees, float previous_degrees)
{
    double delta = std::fmod(
        static_cast<double>(current_degrees - previous_degrees) + 540.0,
        360.0) - 180.0;
    return delta * std::numbers::pi_v<double> / 180.0;
}

struct TestBodyBasis {
    std::array<double, 3> forward{};
    std::array<double, 3> right{};
    std::array<double, 3> up{};
};

TestBodyBasis make_test_body_basis(
    double heading_radians,
    double pitch_radians,
    double roll_radians)
{
    const double sin_heading = std::sin(heading_radians);
    const double cos_heading = std::cos(heading_radians);
    const double sin_pitch = std::sin(pitch_radians);
    const double cos_pitch = std::cos(pitch_radians);
    const double sin_roll = std::sin(roll_radians);
    const double cos_roll = std::cos(roll_radians);

    // ENU basis for the canonical vehicle axes. wheel_y is right-positive in
    // the private solver, so a negative canonical roll raises the right side.
    const std::array<double, 3> flat_forward{
        sin_heading, cos_heading, 0.0};
    const std::array<double, 3> flat_right{
        cos_heading, -sin_heading, 0.0};
    const std::array<double, 3> pitch_up{
        -sin_pitch * flat_forward[0],
        -sin_pitch * flat_forward[1],
        cos_pitch};

    return TestBodyBasis{
        {
            cos_pitch * flat_forward[0],
            cos_pitch * flat_forward[1],
            sin_pitch,
        },
        {
            cos_roll * flat_right[0] - sin_roll * pitch_up[0],
            cos_roll * flat_right[1] - sin_roll * pitch_up[1],
            -sin_roll * pitch_up[2],
        },
        {
            sin_roll * flat_right[0] + cos_roll * pitch_up[0],
            sin_roll * flat_right[1] + cos_roll * pitch_up[1],
            cos_roll * pitch_up[2],
        },
    };
}

double test_dot(
    const std::array<double, 3>& lhs,
    const std::array<double, 3>& rhs)
{
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

struct TestGroundBasis {
    std::array<double, 3> forward{};
    std::array<double, 3> right{};
    std::array<double, 3> normal{};
};

TestGroundBasis make_test_ground_basis_from_normal(
    double heading_radians,
    std::array<double, 3> normal)
{
    const double normal_length = std::hypot(
        normal[0], normal[1], normal[2]);
    for (double& component : normal) {
        component /= normal_length;
    }

    const std::array<double, 3> horizontal_forward{
        std::sin(heading_radians), std::cos(heading_radians), 0.0};
    const double forward_normal_dot = test_dot(horizontal_forward, normal);
    std::array<double, 3> forward{
        horizontal_forward[0] - forward_normal_dot * normal[0],
        horizontal_forward[1] - forward_normal_dot * normal[1],
        -forward_normal_dot * normal[2]};
    const double forward_length = std::hypot(
        forward[0], forward[1], forward[2]);
    for (double& component : forward) {
        component /= forward_length;
    }

    return TestGroundBasis{
        forward,
        {
            forward[1] * normal[2] - forward[2] * normal[1],
            forward[2] * normal[0] - forward[0] * normal[2],
            forward[0] * normal[1] - forward[1] * normal[0],
        },
        normal};
}

std::array<double, 4> reconstruct_suspension_lengths(
    const VehicleState& state,
    const VehicleParameters& parameters)
{
    const auto body_basis = make_test_body_basis(
        state.heading * std::numbers::pi_v<double> / 180.0,
        state.pitch * std::numbers::pi_v<double> / 180.0,
        state.roll * std::numbers::pi_v<double> / 180.0);
    const double front_axle_offset = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double rear_axle_offset = parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const std::array<double, 4> wheel_x{
        front_axle_offset, front_axle_offset,
        -rear_axle_offset, -rear_axle_offset};
    const std::array<double, 4> wheel_y{
        -parameters.front_track_m * 0.5,
        parameters.front_track_m * 0.5,
        -parameters.rear_track_m * 0.5,
        parameters.rear_track_m * 0.5};

    std::array<double, 4> lengths{};
    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        if (!wheel.in_contact) {
            lengths[index] = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        const std::array<double, 3> mount{
            state.east
                + wheel_x[index] * body_basis.forward[0]
                + wheel_y[index] * body_basis.right[0],
            state.north
                + wheel_x[index] * body_basis.forward[1]
                + wheel_y[index] * body_basis.right[1],
            state.position_enu.z
                + wheel_x[index] * body_basis.forward[2]
                + wheel_y[index] * body_basis.right[2]};
        const std::array<double, 3> wheel_center{
            wheel.contact_point_enu.x
                + wheel.contact_normal_enu.x * parameters.tire_radius_m,
            wheel.contact_point_enu.y
                + wheel.contact_normal_enu.y * parameters.tire_radius_m,
            wheel.contact_point_enu.z
                + wheel.contact_normal_enu.z * parameters.tire_radius_m};
        lengths[index] =
            (mount[0] - wheel_center[0]) * body_basis.up[0]
            + (mount[1] - wheel_center[1]) * body_basis.up[1]
            + (mount[2] - wheel_center[2]) * body_basis.up[2];
    }
    return lengths;
}

double distance_between(
    const Vector3State& lhs,
    const Vector3State& rhs)
{
    return std::hypot(
        std::hypot(lhs.x - rhs.x, lhs.y - rhs.y),
        lhs.z - rhs.z);
}

template <typename HeightAt, typename NorthSlopeAt>
void require_vehicle_above_surface(
    const VehicleState& state,
    const VehicleParameters& parameters,
    HeightAt height_at,
    NorthSlopeAt north_slope_at,
    const std::string& context)
{
    constexpr double tolerance_m = 0.015;
    const double heading = state.heading
        * std::numbers::pi_v<double> / 180.0;
    const double pitch = state.pitch
        * std::numbers::pi_v<double> / 180.0;
    const double roll = state.roll
        * std::numbers::pi_v<double> / 180.0;
    const auto body_basis = make_test_body_basis(heading, pitch, roll);
    const double front_axle_offset = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double rear_axle_offset = parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const std::array<double, 4> wheel_x{
        front_axle_offset, front_axle_offset,
        -rear_axle_offset, -rear_axle_offset};
    const std::array<double, 4> wheel_y{
        -parameters.front_track_m * 0.5,
        parameters.front_track_m * 0.5,
        -parameters.rear_track_m * 0.5,
        parameters.rear_track_m * 0.5};
    const double minimum_suspension_length =
        parameters.suspension.rest_length_m
        - parameters.suspension.max_compression_m;

    const double center_height = height_at(state.east, state.north);
    const double center_normal_up = 1.0 / std::hypot(
        north_slope_at(state.north), 1.0);
    const double chassis_normal_clearance =
        (state.position_enu.z - center_height) * center_normal_up;
    require(chassis_normal_clearance + tolerance_m >= parameters.cg_height_m,
            context + ": chassis reference penetrated the terrain surface"
                + " (normal clearance="
                + std::to_string(chassis_normal_clearance)
                + " m, required=" + std::to_string(parameters.cg_height_m)
                + " m)");

    for (std::size_t index = 0; index < wheel_x.size(); ++index) {
        const double mount_east = state.east
            + wheel_x[index] * body_basis.forward[0]
            + wheel_y[index] * body_basis.right[0];
        const double mount_north = state.north
            + wheel_x[index] * body_basis.forward[1]
            + wheel_y[index] * body_basis.right[1];
        const double mount_up = state.position_enu.z
            + wheel_x[index] * body_basis.forward[2]
            + wheel_y[index] * body_basis.right[2];
        const double ground_height = height_at(mount_east, mount_north);
        const double normal_up = 1.0 / std::hypot(
            north_slope_at(mount_north), 1.0);

        // The suspension may compress to its configured lower bound, but a
        // round tire still needs radius/normal_up of vertical clearance from a
        // sloped surface. Otherwise its downhill shoulder is below terrain.
        const double minimum_vertical_clearance = minimum_suspension_length
            + parameters.tire_radius_m / normal_up;
        const double vertical_clearance = mount_up - ground_height;
        require(vertical_clearance + tolerance_m
                    >= minimum_vertical_clearance,
                context + ": wheel " + std::to_string(index)
                    + " penetrated the terrain surface"
                    + " (vertical clearance="
                    + std::to_string(vertical_clearance)
                    + " m, required="
                    + std::to_string(minimum_vertical_clearance) + " m)");
    }
}

void test_initial_state_is_a_complete_four_wheel_snapshot()
{
    VehicleParameters parameters;
    VehiclePhysics vehicle(kInitialLat, kInitialLon, 7.0, 0.f, parameters);
    const auto state = vehicle.get_state();

    require(state.position_enu.z == parameters.cg_height_m,
            "initial chassis position must include CG height");
    float total_load = 0.f;
    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        require(wheel.wheel_index == index,
                "initial wheel indices must be unique and canonical");
        require(wheel.in_contact, "initial flat-ground wheels must be in contact");
        require(wheel.normal_load > 0.f,
                "initial wheels must publish their static normal load");
        require(wheel.contact_point_enu.z == 0.0,
                "default FlatGroundQuery must publish a zero-height hit");
        require(wheel.contact_normal_enu.x == 0.0
                && wheel.contact_normal_enu.y == 0.0
                && wheel.contact_normal_enu.z == 1.0,
                "default FlatGroundQuery must publish an ENU up normal");
        total_load += wheel.normal_load;
    }
    require(std::abs(total_load - parameters.mass_kg * 9.80665f) < 1.f,
            "initial wheel loads must sum to vehicle weight");
}

void test_suspension_model_clamps_stroke_and_force()
{
    simcore_host::SuspensionParameters parameters;
    parameters.rest_length_m = 0.4f;
    parameters.max_compression_m = 0.1f;
    parameters.max_extension_m = 0.2f;
    parameters.spring_rate_n_per_m = 10000.f;
    parameters.damper_rate_n_s_per_m = 500.f;
    parameters.max_force_n = 750.f;

    const auto compressed = simcore_host::evaluate_suspension(
        parameters, -10.f, 0.f, false, 0.01f);
    require(std::abs(compressed.bounded_length_m - 0.3f) < 1e-6f,
            "suspension length must stop at the compression stroke bound");
    require(std::abs(compressed.compression_m - 0.1f) < 1e-6f,
            "suspension compression must be bounded");
    require(compressed.normal_force_n == parameters.max_force_n,
            "spring force must stop at the configured maximum");

    const auto extended = simcore_host::evaluate_suspension(
        parameters, 10.f, compressed.compression_m, true, 0.1f);
    require(std::abs(extended.bounded_length_m - 0.6f) < 1e-6f,
            "suspension length must stop at the extension stroke bound");
    require(std::abs(extended.compression_m + 0.2f) < 1e-6f,
            "extension must be represented as bounded negative compression");
    require(extended.normal_force_n == 0.f,
            "an extended spring cannot pull the ground toward the vehicle");

    const auto repeated = simcore_host::evaluate_suspension(
        parameters, 0.35f, 0.02f, true, 0.1f);
    const auto repeated_again = simcore_host::evaluate_suspension(
        parameters, 0.35f, 0.02f, true, 0.1f);
    require(repeated.bounded_length_m == repeated_again.bounded_length_m
            && repeated.compression_m == repeated_again.compression_m
            && repeated.compression_velocity_mps
                == repeated_again.compression_velocity_mps
            && repeated.normal_force_n == repeated_again.normal_force_n,
            "identical suspension inputs must produce bit-identical results");
}

void test_invalid_suspension_parameters_are_rejected()
{
    VehicleParameters parameters;
    parameters.suspension.rest_length_m =
        parameters.suspension.max_compression_m - 0.01f;
    bool rejected = false;
    try {
        VehiclePhysics vehicle(
            kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "vehicle construction must reject an impossible suspension stroke");
}

void test_ground_query_publishes_sloped_hit_and_normal()
{
    constexpr double east_slope = 0.02;
    constexpr double north_slope = -0.01;
    auto ground = std::make_shared<PlaneGroundQuery>(east_slope, north_slope, 0.0);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    const auto state = vehicle.get_state();

    for (const auto& wheel : state.wheels) {
        require(wheel.in_contact, "a shallow plane must contact every wheel");
        const double expected_height = east_slope * wheel.contact_point_enu.x
            + north_slope * wheel.contact_point_enu.y;
        require(std::abs(wheel.contact_point_enu.z - expected_height) < 1e-9,
                "GroundQuery hit height must be published without flattening");
        const double normal_length = std::hypot(
            wheel.contact_normal_enu.x,
            wheel.contact_normal_enu.y,
            wheel.contact_normal_enu.z);
        require(std::abs(normal_length - 1.0) < 1e-9,
                "published ground normal must be normalized");
        require(wheel.contact_normal_enu.x < 0.0
                && wheel.contact_normal_enu.y > 0.0
                && wheel.contact_normal_enu.z > 0.0,
                "published normal must retain the slope direction");
    }
}

void test_sloped_contact_patch_reconstructs_authoritative_wheel_center()
{
    constexpr double grade_degrees = 20.0;
    const double north_slope = std::tan(
        grade_degrees * std::numbers::pi_v<double> / 180.0);
    auto ground = std::make_shared<PlaneGroundQuery>(0.0, north_slope, 0.0);
    VehicleParameters parameters;
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    const auto state = vehicle.get_state();

    const double heading = state.heading
        * std::numbers::pi_v<double> / 180.0;
    const auto body_basis = make_test_body_basis(
        heading,
        state.pitch * std::numbers::pi_v<double> / 180.0,
        state.roll * std::numbers::pi_v<double> / 180.0);
    const double cg_to_rear_axle = parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const double cg_to_front_axle = parameters.wheelbase_m
        - cg_to_rear_axle;
    const std::array<double, 4> wheel_x{
        cg_to_front_axle, cg_to_front_axle,
        -cg_to_rear_axle, -cg_to_rear_axle};
    const std::array<double, 4> wheel_y{
        -parameters.front_track_m * 0.5,
        parameters.front_track_m * 0.5,
        -parameters.rear_track_m * 0.5,
        parameters.rear_track_m * 0.5};

    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        require(wheel.in_contact,
                "20-degree plane must retain every tire contact");
        const double reconstructed_center_east = wheel.contact_point_enu.x
            + wheel.contact_normal_enu.x * parameters.tire_radius_m;
        const double reconstructed_center_north = wheel.contact_point_enu.y
            + wheel.contact_normal_enu.y * parameters.tire_radius_m;
        const double reconstructed_center_up = wheel.contact_point_enu.z
            + wheel.contact_normal_enu.z * parameters.tire_radius_m;
        const double expected_mount_east = state.east
            + wheel_x[index] * body_basis.forward[0]
            + wheel_y[index] * body_basis.right[0];
        const double expected_mount_north = state.north
            + wheel_x[index] * body_basis.forward[1]
            + wheel_y[index] * body_basis.right[1];
        const double expected_mount_up = state.position_enu.z
            + wheel_x[index] * body_basis.forward[2]
            + wheel_y[index] * body_basis.right[2];
        const Vector3State mount_to_center{
            expected_mount_east - reconstructed_center_east,
            expected_mount_north - reconstructed_center_north,
            expected_mount_up - reconstructed_center_up};
        const double suspension_length =
            mount_to_center.x * body_basis.up[0]
            + mount_to_center.y * body_basis.up[1]
            + mount_to_center.z * body_basis.up[2];
        const double suspension_axis_error = std::hypot(
            std::hypot(
                mount_to_center.x - suspension_length * body_basis.up[0],
                mount_to_center.y - suspension_length * body_basis.up[1]),
            mount_to_center.z - suspension_length * body_basis.up[2]);
        require(suspension_axis_error < 1e-8,
                "contact patch plus normal times radius must reconstruct the"
                " wheel centre on its body-up suspension axis; wheel="
                    + std::to_string(index)
                    + ", error=" + std::to_string(suspension_axis_error)
                    + ", length=" + std::to_string(suspension_length));

        const double expected_patch_up =
            north_slope * wheel.contact_point_enu.y;
        require(std::abs(wheel.contact_point_enu.z - expected_patch_up) < 1e-9,
                "published round-tire contact patch must remain on the plane");
    }
}

void require_stationary_slope_snapshot(
    const VehicleState& state,
    const VehicleParameters& parameters,
    double east_slope,
    double north_slope,
    double expected_pitch_degrees,
    double expected_roll_degrees,
    double attitude_tolerance_degrees,
    double footprint_tolerance_m,
    const std::string& context)
{
    require(std::abs(state.pitch - expected_pitch_degrees)
                <= attitude_tolerance_degrees,
            context + ": chassis pitch did not match static equilibrium"
                + "; actual=" + std::to_string(state.pitch)
                + ", expected=" + std::to_string(expected_pitch_degrees));
    require(std::abs(state.roll - expected_roll_degrees)
                <= attitude_tolerance_degrees,
            context + ": chassis roll did not match static equilibrium"
                + "; actual=" + std::to_string(state.roll)
                + ", expected=" + std::to_string(expected_roll_degrees));

    const double heading = state.heading
        * std::numbers::pi_v<double> / 180.0;
    const double pitch = state.pitch
        * std::numbers::pi_v<double> / 180.0;
    const double roll = state.roll
        * std::numbers::pi_v<double> / 180.0;
    const auto body_basis = make_test_body_basis(heading, pitch, roll);
    const double front_axle_offset = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double rear_axle_offset = parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const std::array<double, 4> wheel_x{
        front_axle_offset, front_axle_offset,
        -rear_axle_offset, -rear_axle_offset};
    const std::array<double, 4> wheel_y{
        -parameters.front_track_m * 0.5,
        parameters.front_track_m * 0.5,
        -parameters.rear_track_m * 0.5,
        parameters.rear_track_m * 0.5};
    std::array<Vector3State, 4> wheel_centers{};

    const double normal_scale = 1.0 / std::sqrt(
        east_slope * east_slope
        + north_slope * north_slope
        + 1.0);
    const Vector3State expected_normal{
        -east_slope * normal_scale,
        -north_slope * normal_scale,
        normal_scale};

    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        require(wheel.in_contact,
                context + ": all four tires must remain in contact; wheel="
                    + std::to_string(index));

        const double patch_plane_error = wheel.contact_point_enu.z
            - east_slope * wheel.contact_point_enu.x
            - north_slope * wheel.contact_point_enu.y;
        require(std::abs(patch_plane_error) < 1e-8,
                context + ": published tire patch left the authored plane"
                    + "; wheel=" + std::to_string(index)
                    + ", error=" + std::to_string(patch_plane_error));
        require(std::abs(wheel.contact_normal_enu.x - expected_normal.x) < 1e-8
                && std::abs(
                    wheel.contact_normal_enu.y - expected_normal.y) < 1e-8
                && std::abs(
                    wheel.contact_normal_enu.z - expected_normal.z) < 1e-8,
                context + ": published contact normal did not match the plane"
                    + "; wheel=" + std::to_string(index));

        // The transport contract publishes the true tangent patch. Unreal can
        // therefore reconstruct the rendered wheel centre with patch+n*radius.
        wheel_centers[index] = Vector3State{
            wheel.contact_point_enu.x
                + wheel.contact_normal_enu.x * parameters.tire_radius_m,
            wheel.contact_point_enu.y
                + wheel.contact_normal_enu.y * parameters.tire_radius_m,
            wheel.contact_point_enu.z
                + wheel.contact_normal_enu.z * parameters.tire_radius_m};

        const Vector3State suspension_mount{
            state.east
            + wheel_x[index] * body_basis.forward[0]
            + wheel_y[index] * body_basis.right[0],
            state.north
            + wheel_x[index] * body_basis.forward[1]
            + wheel_y[index] * body_basis.right[1],
            state.position_enu.z
            + wheel_x[index] * body_basis.forward[2]
            + wheel_y[index] * body_basis.right[2]};
        const Vector3State mount_to_center{
            suspension_mount.x - wheel_centers[index].x,
            suspension_mount.y - wheel_centers[index].y,
            suspension_mount.z - wheel_centers[index].z};
        const double suspension_length =
            mount_to_center.x * body_basis.up[0]
            + mount_to_center.y * body_basis.up[1]
            + mount_to_center.z * body_basis.up[2];
        const double suspension_axis_error = std::hypot(
            std::hypot(
                mount_to_center.x
                    - suspension_length * body_basis.up[0],
                mount_to_center.y
                    - suspension_length * body_basis.up[1]),
            mount_to_center.z - suspension_length * body_basis.up[2]);
        require(suspension_axis_error < 1e-6,
                context + ": reconstructed wheel centre did not lie on the"
                    " pitched/rolled body-up suspension axis; wheel="
                    + std::to_string(index)
                    + ", error=" + std::to_string(suspension_axis_error));
        const double minimum_suspension_length =
            parameters.suspension.rest_length_m
            - parameters.suspension.max_compression_m;
        const double maximum_suspension_length =
            parameters.suspension.rest_length_m
            + parameters.suspension.max_extension_m;
        require(suspension_length >= minimum_suspension_length - 1e-6
                && suspension_length <= maximum_suspension_length + 1e-6,
                context + ": reconstructed suspension length left its stroke"
                    "; wheel=" + std::to_string(index)
                    + ", length=" + std::to_string(suspension_length));
    }

    require(std::abs(distance_between(wheel_centers[0], wheel_centers[1])
                     - parameters.front_track_m) <= footprint_tolerance_m,
            context + ": reconstructed front track width changed on the slope");
    require(std::abs(distance_between(wheel_centers[2], wheel_centers[3])
                     - parameters.rear_track_m) <= footprint_tolerance_m,
            context + ": reconstructed rear track width changed on the slope");
    require(std::abs(distance_between(wheel_centers[0], wheel_centers[2])
                     - parameters.wheelbase_m) <= footprint_tolerance_m,
            context + ": reconstructed left wheelbase changed on the slope");
    require(std::abs(distance_between(wheel_centers[1], wheel_centers[3])
                     - parameters.wheelbase_m) <= footprint_tolerance_m,
            context + ": reconstructed right wheelbase changed on the slope");
}

double stationary_sprung_body_pitch_degrees(
    const VehicleParameters& parameters,
    double ground_pitch_degrees)
{
    constexpr double gravity_mps2 = 9.80665;
    const double front_arm_m = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double rear_arm_m = parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const double pitch_stiffness_n_m_per_rad =
        2.0 * parameters.suspension.spring_rate_n_per_m
        * (front_arm_m * front_arm_m + rear_arm_m * rear_arm_m);
    const double ground_pitch_radians = ground_pitch_degrees
        * std::numbers::pi_v<double> / 180.0;
    const double contact_moment_n_m = parameters.mass_kg * gravity_mps2
        * std::sin(ground_pitch_radians) * parameters.cg_height_m;
    const double sprung_lean_degrees = contact_moment_n_m
        / pitch_stiffness_n_m_per_rad
        * 180.0 / std::numbers::pi_v<double>;
    return ground_pitch_degrees + sprung_lean_degrees;
}

double stationary_sprung_body_roll_degrees(
    const VehicleParameters& parameters,
    double ground_roll_degrees)
{
    constexpr double gravity_mps2 = 9.80665;
    const double front_half_track_m = parameters.front_track_m * 0.5;
    const double rear_half_track_m = parameters.rear_track_m * 0.5;
    const double roll_stiffness_n_m_per_rad =
        2.0 * parameters.suspension.spring_rate_n_per_m
        * (front_half_track_m * front_half_track_m
            + rear_half_track_m * rear_half_track_m);
    const double ground_roll_radians = ground_roll_degrees
        * std::numbers::pi_v<double> / 180.0;
    const double contact_moment_n_m = parameters.mass_kg * gravity_mps2
        * std::sin(ground_roll_radians) * parameters.cg_height_m;
    const double sprung_lean_degrees = contact_moment_n_m
        / roll_stiffness_n_m_per_rad
        * 180.0 / std::numbers::pi_v<double>;
    return ground_roll_degrees + sprung_lean_degrees;
}

void test_stationary_reset_preserves_four_wheel_geometry_on_steep_planes()
{
    struct SlopeCase {
        const char* label;
        double east_slope;
        double north_slope;
        double expected_pitch_degrees;
        double expected_roll_degrees;
    };

    const auto slope = [](double degrees) {
        return std::tan(degrees
            * std::numbers::pi_v<double> / 180.0);
    };
    const std::array<SlopeCase, 4> cases{{
        {"20-degree longitudinal", 0.0, slope(20.0), 20.0, 0.0},
        {"30-degree longitudinal", 0.0, slope(30.0), 30.0, 0.0},
        {"40-degree longitudinal", 0.0, slope(40.0), 40.0, 0.0},
        {"30-degree cross-slope", slope(30.0), 0.0, 0.0, -30.0},
    }};

    const VehicleParameters parameters;
    for (const auto& slope_case : cases) {
        auto ground = std::make_shared<PlaneGroundQuery>(
            slope_case.east_slope,
            slope_case.north_slope,
            0.0);
        VehiclePhysics vehicle(
            kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);

        // Exercise the explicit reset path used when PIE restarts, not only
        // the constructor's initial snapshot.
        vehicle.reset();
        const auto reset = vehicle.get_state();
        require_stationary_slope_snapshot(
            reset,
            parameters,
            slope_case.east_slope,
            slope_case.north_slope,
            slope_case.expected_pitch_degrees,
            slope_case.expected_roll_degrees,
            0.25,
            1e-6,
            std::string(slope_case.label) + " reset");

        VehicleInput input;
        input.brake = 1.f;
        input.handbrake = true;
        vehicle.set_input(input);
        for (int step = 0; step < 180; ++step) {
            const auto held = vehicle.update(kDt);
            const auto contact_count = std::count_if(
                held.wheels.begin(), held.wheels.end(),
                [](const WheelState& wheel) { return wheel.in_contact; });
            require(contact_count == 4,
                    std::string(slope_case.label)
                        + ": stationary hold lost wheel contact at step "
                        + std::to_string(step)
                        + "; contacts=" + std::to_string(contact_count));
        }

        const auto held = vehicle.get_state();
        require(held.speed == 0.f
                && std::hypot(held.east - reset.east, held.north - reset.north)
                    < 1e-6,
                std::string(slope_case.label)
                    + ": brake hold must not creep after reset");
        const double expected_sprung_pitch_degrees =
            stationary_sprung_body_pitch_degrees(
                parameters, slope_case.expected_pitch_degrees);
        const double expected_sprung_roll_degrees =
            stationary_sprung_body_roll_degrees(
                parameters, slope_case.expected_roll_degrees);
        require_stationary_slope_snapshot(
            held,
            parameters,
            slope_case.east_slope,
            slope_case.north_slope,
            expected_sprung_pitch_degrees,
            expected_sprung_roll_degrees,
            0.5,
            0.10,
            std::string(slope_case.label) + " settled");
    }
}

void test_chassis_attitude_follows_uneven_four_wheel_support()
{
    constexpr double front_minus_rear_height_m = 0.14;
    constexpr double right_minus_left_height_m = 0.10;
    constexpr int transition_steps = 180;
    constexpr int settling_steps = 240;
    constexpr double max_attitude_step_degrees = 0.75;

    auto ground = std::make_shared<AdjustableFourPostGroundQuery>(
        front_minus_rear_height_m,
        right_minus_left_height_m);
    VehicleParameters parameters;
    // Isolate the physical four-spring moment. The wheel support target still
    // supplies safety bounds, but no synthetic attitude spring may create the
    // expected pitch/roll for this regression.
    parameters.attitude_spring_n_m_rad = 0.f;
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);

    VehicleInput input;
    input.brake = 1.f;
    input.handbrake = true;
    vehicle.set_input(input);

    auto previous = vehicle.get_state();
    const auto initial = previous;
    double largest_pitch_step = 0.0;
    double largest_roll_step = 0.0;
    for (int step = 0; step < transition_steps + settling_steps; ++step) {
        if (step < transition_steps) {
            const double t = static_cast<double>(step + 1)
                / static_cast<double>(transition_steps);
            ground->set_blend(t * t * (3.0 - 2.0 * t));
        } else {
            ground->set_blend(1.0);
        }

        const auto state = vehicle.update(kDt);
        const auto contact_count = std::count_if(
            state.wheels.begin(), state.wheels.end(),
            [](const WheelState& wheel) { return wheel.in_contact; });
        require(contact_count == 4,
                "uneven four-post support lost a wheel at step "
                    + std::to_string(step)
                    + "; contacts=" + std::to_string(contact_count));
        require(std::isfinite(state.pitch) && std::isfinite(state.roll),
                "uneven four-post attitude must remain finite");

        largest_pitch_step = std::max(
            largest_pitch_step,
            std::abs(static_cast<double>(state.pitch - previous.pitch)));
        largest_roll_step = std::max(
            largest_roll_step,
            std::abs(static_cast<double>(state.roll - previous.roll)));
        previous = state;
    }

    require(largest_pitch_step <= max_attitude_step_degrees,
            "chassis pitch snapped while wheel support heights blended; max step="
                + std::to_string(largest_pitch_step));
    require(largest_roll_step <= max_attitude_step_degrees,
            "chassis roll snapped while wheel support heights blended; max step="
                + std::to_string(largest_roll_step));

    const auto state = vehicle.get_state();
    require(state.speed == 0.f
            && std::hypot(state.east - initial.east, state.north - initial.north)
                < 1e-6,
            "wheel-height support attitude must not become a fictitious"
                " terrain-gravity plane");
    std::array<Vector3State, 4> wheel_centers{};
    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        wheel_centers[index] = {
            wheel.contact_point_enu.x
                + wheel.contact_normal_enu.x * parameters.tire_radius_m,
            wheel.contact_point_enu.y
                + wheel.contact_normal_enu.y * parameters.tire_radius_m,
            wheel.contact_point_enu.z
                + wheel.contact_normal_enu.z * parameters.tire_radius_m};
        require(std::abs(wheel.contact_normal_enu.x) < 1e-9
                && std::abs(wheel.contact_normal_enu.y) < 1e-9
                && std::abs(wheel.contact_normal_enu.z - 1.0) < 1e-9,
                "four-post pads must stay locally horizontal so the chassis"
                " cannot infer attitude from contact normals alone");
    }

    const Vector3State front_center{
        0.5 * (wheel_centers[0].x + wheel_centers[1].x),
        0.5 * (wheel_centers[0].y + wheel_centers[1].y),
        0.5 * (wheel_centers[0].z + wheel_centers[1].z)};
    const Vector3State rear_center{
        0.5 * (wheel_centers[2].x + wheel_centers[3].x),
        0.5 * (wheel_centers[2].y + wheel_centers[3].y),
        0.5 * (wheel_centers[2].z + wheel_centers[3].z)};
    const Vector3State left_center{
        0.5 * (wheel_centers[0].x + wheel_centers[2].x),
        0.5 * (wheel_centers[0].y + wheel_centers[2].y),
        0.5 * (wheel_centers[0].z + wheel_centers[2].z)};
    const Vector3State right_center{
        0.5 * (wheel_centers[1].x + wheel_centers[3].x),
        0.5 * (wheel_centers[1].y + wheel_centers[3].y),
        0.5 * (wheel_centers[1].z + wheel_centers[3].z)};

    const double axle_horizontal_separation = std::hypot(
        front_center.x - rear_center.x,
        front_center.y - rear_center.y);
    const double side_horizontal_separation = std::hypot(
        right_center.x - left_center.x,
        right_center.y - left_center.y);
    const double wheel_support_pitch_degrees = std::atan2(
        front_center.z - rear_center.z,
        axle_horizontal_separation) * 180.0 / std::numbers::pi_v<double>;
    // Canonical roll is positive when the left side is higher.
    const double wheel_support_roll_degrees = std::atan2(
        left_center.z - right_center.z,
        side_horizontal_separation) * 180.0 / std::numbers::pi_v<double>;

    require(wheel_support_pitch_degrees > 2.0,
            "four-post fixture must create a meaningful front/rear wheel angle");
    require(wheel_support_roll_degrees < -2.0,
            "four-post fixture must create a meaningful left/right wheel angle");
    require(std::abs(state.pitch - wheel_support_pitch_degrees) <= 1.0,
            "chassis pitch stayed detached from front/rear wheel support; actual="
                + std::to_string(state.pitch)
                + ", wheel support="
                + std::to_string(wheel_support_pitch_degrees));
    require(std::abs(state.roll - wheel_support_roll_degrees) <= 1.0,
            "chassis roll stayed detached from left/right wheel support; actual="
                + std::to_string(state.roll)
                + ", wheel support="
                + std::to_string(wheel_support_roll_degrees));
}

struct ReconstructedSupportAttitude {
    double pitch_degrees = 0.0;
    double roll_degrees = 0.0;
};

ReconstructedSupportAttitude reconstruct_support_attitude(
    const VehicleState& state,
    const VehicleParameters& parameters)
{
    std::array<Vector3State, 4> wheel_centers{};
    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        wheel_centers[index] = {
            wheel.contact_point_enu.x
                + wheel.contact_normal_enu.x * parameters.tire_radius_m,
            wheel.contact_point_enu.y
                + wheel.contact_normal_enu.y * parameters.tire_radius_m,
            wheel.contact_point_enu.z
                + wheel.contact_normal_enu.z * parameters.tire_radius_m};
    }

    const Vector3State front_center{
        0.5 * (wheel_centers[0].x + wheel_centers[1].x),
        0.5 * (wheel_centers[0].y + wheel_centers[1].y),
        0.5 * (wheel_centers[0].z + wheel_centers[1].z)};
    const Vector3State rear_center{
        0.5 * (wheel_centers[2].x + wheel_centers[3].x),
        0.5 * (wheel_centers[2].y + wheel_centers[3].y),
        0.5 * (wheel_centers[2].z + wheel_centers[3].z)};
    const Vector3State left_center{
        0.5 * (wheel_centers[0].x + wheel_centers[2].x),
        0.5 * (wheel_centers[0].y + wheel_centers[2].y),
        0.5 * (wheel_centers[0].z + wheel_centers[2].z)};
    const Vector3State right_center{
        0.5 * (wheel_centers[1].x + wheel_centers[3].x),
        0.5 * (wheel_centers[1].y + wheel_centers[3].y),
        0.5 * (wheel_centers[1].z + wheel_centers[3].z)};

    const double axle_horizontal_separation = std::hypot(
        front_center.x - rear_center.x,
        front_center.y - rear_center.y);
    const double side_horizontal_separation = std::hypot(
        right_center.x - left_center.x,
        right_center.y - left_center.y);
    return {
        std::atan2(
            front_center.z - rear_center.z,
            axle_horizontal_separation)
            * 180.0 / std::numbers::pi_v<double>,
        std::atan2(
            left_center.z - right_center.z,
            side_horizontal_separation)
            * 180.0 / std::numbers::pi_v<double>};
}

void require_reconstructed_suspension_geometry(
    const VehicleState& state,
    const VehicleParameters& parameters,
    const std::string& context)
{
    const auto body_basis = make_test_body_basis(
        state.heading * std::numbers::pi_v<double> / 180.0,
        state.pitch * std::numbers::pi_v<double> / 180.0,
        state.roll * std::numbers::pi_v<double> / 180.0);
    const double front_axle_offset = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double rear_axle_offset = parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const std::array<double, 4> wheel_x{
        front_axle_offset, front_axle_offset,
        -rear_axle_offset, -rear_axle_offset};
    const std::array<double, 4> wheel_y{
        -parameters.front_track_m * 0.5,
        parameters.front_track_m * 0.5,
        -parameters.rear_track_m * 0.5,
        parameters.rear_track_m * 0.5};
    const double minimum_length = parameters.suspension.rest_length_m
        - parameters.suspension.max_compression_m;
    const double maximum_length = parameters.suspension.rest_length_m
        + parameters.suspension.max_extension_m;

    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        require(wheel.in_contact,
                context + ": wheel lost contact; index="
                    + std::to_string(index));
        const Vector3State wheel_center{
            wheel.contact_point_enu.x
                + wheel.contact_normal_enu.x * parameters.tire_radius_m,
            wheel.contact_point_enu.y
                + wheel.contact_normal_enu.y * parameters.tire_radius_m,
            wheel.contact_point_enu.z
                + wheel.contact_normal_enu.z * parameters.tire_radius_m};
        const Vector3State suspension_mount{
            state.east
                + wheel_x[index] * body_basis.forward[0]
                + wheel_y[index] * body_basis.right[0],
            state.north
                + wheel_x[index] * body_basis.forward[1]
                + wheel_y[index] * body_basis.right[1],
            state.position_enu.z
                + wheel_x[index] * body_basis.forward[2]
                + wheel_y[index] * body_basis.right[2]};
        const Vector3State mount_to_center{
            suspension_mount.x - wheel_center.x,
            suspension_mount.y - wheel_center.y,
            suspension_mount.z - wheel_center.z};
        const double suspension_length =
            mount_to_center.x * body_basis.up[0]
            + mount_to_center.y * body_basis.up[1]
            + mount_to_center.z * body_basis.up[2];
        const double suspension_axis_error = std::hypot(
            std::hypot(
                mount_to_center.x
                    - suspension_length * body_basis.up[0],
                mount_to_center.y
                    - suspension_length * body_basis.up[1]),
            mount_to_center.z - suspension_length * body_basis.up[2]);
        require(suspension_axis_error < 1e-6,
                context + ": wheel centre left its chassis suspension axis"
                    + "; index=" + std::to_string(index)
                    + ", error=" + std::to_string(suspension_axis_error));
        require(suspension_length >= minimum_length - 1e-6
                && suspension_length <= maximum_length + 1e-6,
                context + ": wheel support exceeded suspension stroke"
                    + "; index=" + std::to_string(index)
                    + ", length=" + std::to_string(suspension_length)
                    + ", range=[" + std::to_string(minimum_length)
                    + ", " + std::to_string(maximum_length) + "]");
    }
}

void test_raised_front_and_left_supports_set_canonical_attitude_signs()
{
    struct SupportCase {
        const char* label;
        double front_minus_rear_height_m;
        double right_minus_left_height_m;
        bool exercises_pitch;
    };
    const std::array<SupportCase, 2> cases{{
        {"raised front axle", 0.14, 0.0, true},
        // AdjustableFourPostGroundQuery takes right-minus-left, so a negative
        // value raises the canonical left side and must produce positive roll.
        {"raised left wheels", 0.0, -0.10, false},
    }};

    for (const auto& support_case : cases) {
        auto ground = std::make_shared<AdjustableFourPostGroundQuery>(
            support_case.front_minus_rear_height_m,
            support_case.right_minus_left_height_m);
        const VehicleParameters parameters;
        VehiclePhysics vehicle(
            kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
        VehicleInput input;
        input.brake = 1.f;
        input.handbrake = true;
        vehicle.set_input(input);

        VehicleState state = vehicle.get_state();
        bool observed_canonical_angular_velocity = false;
        for (int step = 0; step < 420; ++step) {
            if (step < 180) {
                const double t = static_cast<double>(step + 1) / 180.0;
                ground->set_blend(t * t * (3.0 - 2.0 * t));
            } else {
                ground->set_blend(1.0);
            }
            state = vehicle.update(kDt);
            observed_canonical_angular_velocity =
                observed_canonical_angular_velocity
                || (support_case.exercises_pitch
                        ? state.angular_velocity_body.y < -1e-4
                        : state.angular_velocity_body.x > 1e-4);
            require(std::all_of(
                        state.wheels.begin(), state.wheels.end(),
                        [](const WheelState& wheel) {
                            return wheel.in_contact;
                        }),
                    std::string(support_case.label)
                        + ": sign regression lost a wheel at step "
                        + std::to_string(step));
            require(std::isfinite(state.pitch) && std::isfinite(state.roll),
                    std::string(support_case.label)
                        + ": chassis attitude became non-finite");
        }

        const auto support = reconstruct_support_attitude(state, parameters);
        require(observed_canonical_angular_velocity,
                std::string(support_case.label)
                    + ": right-handed FLU angular velocity sign was not observed");
        require_reconstructed_suspension_geometry(
            state, parameters, support_case.label);
        if (support_case.exercises_pitch) {
            require(support.pitch_degrees > 2.0,
                    "raised front fixture must form a positive support pitch");
            require(state.pitch > 2.0f,
                    "raising the front axle must pitch the chassis nose up"
                        "; actual=" + std::to_string(state.pitch)
                        + ", support="
                        + std::to_string(support.pitch_degrees));
            require(std::abs(state.pitch - support.pitch_degrees) <= 1.0,
                    "raised front chassis pitch must follow its wheel support"
                        "; actual=" + std::to_string(state.pitch)
                        + ", support="
                        + std::to_string(support.pitch_degrees));
            require(std::abs(state.roll) <= 1.0,
                    "symmetric raised axles must not create chassis roll");
        } else {
            require(support.roll_degrees > 2.0,
                    "raised left fixture must form a positive support roll");
            require(state.roll > 2.0f,
                    "raising the left wheels must roll the chassis left-side up"
                        "; actual=" + std::to_string(state.roll)
                        + ", support="
                        + std::to_string(support.roll_degrees));
            require(std::abs(state.roll - support.roll_degrees) <= 1.0,
                    "raised left chassis roll must follow its wheel support"
                        "; actual=" + std::to_string(state.roll)
                        + ", support="
                        + std::to_string(support.roll_degrees));
            require(std::abs(state.pitch) <= 1.0,
                    "symmetric raised sides must not create chassis pitch");
        }
    }
}

void test_wheel_support_attitude_exceeds_legacy_absolute_limits_smoothly()
{
    struct SupportCase {
        const char* label;
        double front_minus_rear_height_m;
        double right_minus_left_height_m;
        double legacy_limit_degrees;
        bool exercises_pitch;
    };

    const VehicleParameters parameters;
    const double pitch_height_difference = parameters.wheelbase_m
        * std::tan(8.0 * std::numbers::pi_v<double> / 180.0);
    const double roll_height_difference = parameters.front_track_m
        * std::tan(10.0 * std::numbers::pi_v<double> / 180.0);
    const std::array<SupportCase, 2> cases{{
        {"eight-degree wheel pitch", pitch_height_difference, 0.0, 6.0, true},
        {"ten-degree wheel roll", 0.0, -roll_height_difference, 8.0, false},
    }};

    constexpr int transition_steps = 480;
    constexpr int settling_steps = 300;
    constexpr double maximum_step_degrees = 0.75;
    for (const auto& support_case : cases) {
        auto ground = std::make_shared<AdjustableFourPostGroundQuery>(
            support_case.front_minus_rear_height_m,
            support_case.right_minus_left_height_m);
        VehiclePhysics vehicle(
            kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
        VehicleInput input;
        input.brake = 1.f;
        input.handbrake = true;
        vehicle.set_input(input);

        VehicleState previous = vehicle.get_state();
        const VehicleState initial = previous;
        double largest_pitch_step = 0.0;
        double largest_roll_step = 0.0;
        for (int step = 0; step < transition_steps + settling_steps; ++step) {
            if (step < transition_steps) {
                const double t = static_cast<double>(step + 1)
                    / static_cast<double>(transition_steps);
                ground->set_blend(t * t * (3.0 - 2.0 * t));
            } else {
                ground->set_blend(1.0);
            }

            const auto state = vehicle.update(kDt);
            require(std::all_of(
                        state.wheels.begin(), state.wheels.end(),
                        [](const WheelState& wheel) {
                            return wheel.in_contact;
                        }),
                    std::string(support_case.label)
                        + ": valid suspension support lost contact at step "
                        + std::to_string(step));
            require(std::isfinite(state.pitch) && std::isfinite(state.roll)
                    && std::isfinite(state.angular_velocity_body.x)
                    && std::isfinite(state.angular_velocity_body.y),
                    std::string(support_case.label)
                        + ": attitude response became non-finite");
            largest_pitch_step = std::max(
                largest_pitch_step,
                std::abs(static_cast<double>(state.pitch - previous.pitch)));
            largest_roll_step = std::max(
                largest_roll_step,
                std::abs(static_cast<double>(state.roll - previous.roll)));
            previous = state;
        }

        const auto state = vehicle.get_state();
        const auto support = reconstruct_support_attitude(state, parameters);
        require_reconstructed_suspension_geometry(
            state, parameters, support_case.label);
        const double body_angle = support_case.exercises_pitch
            ? static_cast<double>(state.pitch)
            : static_cast<double>(state.roll);
        const double support_angle = support_case.exercises_pitch
            ? support.pitch_degrees
            : support.roll_degrees;
        require(support_angle > support_case.legacy_limit_degrees + 1.0,
                std::string(support_case.label)
                    + ": fixture did not exceed the legacy absolute boundary"
                    + "; support=" + std::to_string(support_angle));
        require(body_angle > support_case.legacy_limit_degrees + 0.5,
                std::string(support_case.label)
                    + ": chassis remained trapped at the legacy absolute boundary"
                    + "; body=" + std::to_string(body_angle)
                    + ", support=" + std::to_string(support_angle));
        require(std::abs(body_angle - support_angle) <= 1.0,
                std::string(support_case.label)
                    + ": chassis did not settle onto its wheel support plane"
                    + "; body=" + std::to_string(body_angle)
                    + ", support=" + std::to_string(support_angle));
        require(largest_pitch_step <= maximum_step_degrees
                && largest_roll_step <= maximum_step_degrees,
                std::string(support_case.label)
                    + ": chassis attitude snapped while support changed"
                    + "; max pitch step="
                    + std::to_string(largest_pitch_step)
                    + ", max roll step="
                    + std::to_string(largest_roll_step));
        require(state.speed == 0.f
                && std::hypot(
                    state.east - initial.east,
                    state.north - initial.north) < 1e-6,
                std::string(support_case.label)
                    + ": suspension attitude must not create planar motion");

        // Returning the four supports to a flat plane must be equally smooth
        // and settle the body without leaving a hidden angle at the old bound.
        previous = state;
        for (int step = 0; step < transition_steps + settling_steps; ++step) {
            if (step < transition_steps) {
                const double t = static_cast<double>(step + 1)
                    / static_cast<double>(transition_steps);
                const double smooth = t * t * (3.0 - 2.0 * t);
                ground->set_blend(1.0 - smooth);
            } else {
                ground->set_blend(0.0);
            }
            const auto flattened = vehicle.update(kDt);
            require(std::all_of(
                        flattened.wheels.begin(), flattened.wheels.end(),
                        [](const WheelState& wheel) {
                            return wheel.in_contact;
                        }),
                    std::string(support_case.label)
                        + ": flattening lost a wheel at step "
                        + std::to_string(step));
            require(std::abs(flattened.pitch - previous.pitch)
                        <= maximum_step_degrees
                    && std::abs(flattened.roll - previous.roll)
                        <= maximum_step_degrees,
                    std::string(support_case.label)
                        + ": flattening caused an attitude snap");
            previous = flattened;
        }
        const auto flattened = vehicle.get_state();
        require_reconstructed_suspension_geometry(
            flattened,
            parameters,
            std::string(support_case.label) + " flattened");
        require(std::abs(flattened.pitch) <= 1.0
                && std::abs(flattened.roll) <= 1.0,
                std::string(support_case.label)
                    + ": chassis did not return to stable flat attitude"
                    + "; pitch=" + std::to_string(flattened.pitch)
                    + ", roll=" + std::to_string(flattened.roll));
    }
}

void test_invalid_ground_normal_is_rejected()
{
    auto ground = std::make_shared<DownwardNormalGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    const auto state = vehicle.get_state();

    for (const auto& wheel : state.wheels) {
        require(!wheel.in_contact,
                "a non-up-facing ground normal must not create contact");
        require(wheel.normal_load == 0.f,
                "an invalid ground normal must not create suspension load");
    }
}

void test_no_ground_removes_all_tire_forces()
{
    auto ground = std::make_shared<NoGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    advance(vehicle, 60);

    const auto state = vehicle.get_state();
    require(state.speed == 0.f,
            "free-spinning driven wheels must not accelerate an airborne chassis");
    for (const auto& wheel : state.wheels) {
        require(!wheel.in_contact, "missing GroundQuery hits must publish no contact");
        require(wheel.normal_load == 0.f,
                "an airborne wheel must publish zero normal load");
        require(wheel.longitudinal_force == 0.f && wheel.lateral_force == 0.f,
                "an airborne wheel must not generate tire force");
        require(wheel.longitudinal_slip == 0.f && wheel.slip_angle == 0.f,
                "airborne tire slip is undefined and must publish zero");
    }
}

void test_partial_ground_only_loads_hit_wheels()
{
    auto ground = std::make_shared<WestHalfGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 3);
    const auto state = vehicle.get_state();

    require(state.wheels[0].in_contact && state.wheels[2].in_contact,
            "west-side left wheels must receive ground hits");
    require(!state.wheels[1].in_contact && !state.wheels[3].in_contact,
            "east-side right wheels must remain airborne");
    require(state.wheels[0].normal_load > 0.f
            && state.wheels[2].normal_load > 0.f,
            "each hit wheel must retain its independent suspension load");
    for (std::size_t index : {std::size_t{1}, std::size_t{3}}) {
        const auto& wheel = state.wheels[index];
        require(wheel.normal_load == 0.f
                && wheel.longitudinal_force == 0.f
                && wheel.lateral_force == 0.f,
                "each no-hit wheel must independently publish zero load and force");
    }
}

void test_airborne_throttle_does_not_store_hidden_wheel_spin()
{
    VehicleParameters parameters;
    auto ground = std::make_shared<ToggleableFlatGroundQuery>(false);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    VehicleInput input;
    input.gear = VehicleGear::Drive;
    input.throttle = 1.f;
    vehicle.set_input(input);

    // A stationary presentation publishes zero wheel speed, so exercise a
    // complete airborne interval and then restore contact. The first supported
    // frame exposes any angular velocity that was incorrectly accumulated
    // inside the solver while no tire could react the drive torque.
    // Half a second remains inside the solver's documented four-metre ground
    // recovery envelope while still being long enough for the former free
    // wheel integration to accumulate an unmistakable runaway speed.
    for (int step = 0; step < 30; ++step) {
        const auto airborne = vehicle.update(kDt);
        require(airborne.speed == 0.f,
                "airborne throttle must not accelerate the chassis");
        for (const auto& wheel : airborne.wheels) {
            require(!wheel.in_contact,
                    "disabled ground must keep every tire airborne");
            require(wheel.angular_speed == 0.f,
                    "stationary airborne wheel presentation must remain stopped");
        }
    }

    ground->set_enabled(true);
    const auto supported = vehicle.update(kDt);
    require(std::all_of(
                supported.wheels.begin(), supported.wheels.end(),
                [](const WheelState& wheel) { return wheel.in_contact; }),
            "restored flat ground must support all four wheels immediately");
    require(std::abs(supported.pitch) < 0.1f
            && std::abs(supported.roll) < 0.1f
            && std::abs(supported.angular_velocity_body.x) < 0.1
            && std::abs(supported.angular_velocity_body.y) < 0.1,
            "symmetric four-wheel hard-stop recovery must not inject an"
            " order-dependent pitch/roll response; pitch="
                + std::to_string(supported.pitch)
                + ", roll=" + std::to_string(supported.roll)
                + ", omega_x="
                + std::to_string(supported.angular_velocity_body.x)
                + ", omega_y="
                + std::to_string(supported.angular_velocity_body.y));
    for (std::size_t index : {std::size_t{2}, std::size_t{3}}) {
        const double surface_speed = std::abs(
            static_cast<double>(supported.wheels[index].angular_speed)
            * parameters.tire_radius_m);
        require(surface_speed <= std::abs(supported.speed) + 1.0,
                "airborne throttle must not store hidden driven-wheel spin"
                    " that erupts on re-contact; wheel="
                    + std::to_string(index)
                    + ", surface_speed=" + std::to_string(surface_speed)
                    + ", chassis_speed=" + std::to_string(supported.speed));
    }
}

void test_deep_one_side_hard_stop_recovery_uses_a_bounded_attitude_step()
{
    auto ground = std::make_shared<ToggleableFlatGroundQuery>(false);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    advance(vehicle, 30);

    ground->set_west_half_only(true);
    ground->set_enabled(true);
    const auto recovered = vehicle.update(kDt);
    require(std::isfinite(recovered.position_enu.z)
            && std::isfinite(recovered.pitch)
            && std::isfinite(recovered.roll),
            "deep one-side hard-stop recovery must remain finite");
    require(recovered.wheels[0].in_contact && recovered.wheels[2].in_contact,
            "deep west-side recovery must restore the two supported wheels");
    require(std::abs(recovered.pitch) <= 5.0f
            && std::abs(recovered.roll) <= 25.0f,
            "one deep supported side must not flip the chassis in one frame"
                "; pitch=" + std::to_string(recovered.pitch)
                + ", roll=" + std::to_string(recovered.roll));
}

void test_single_corner_hard_stop_does_not_lock_travel_at_other_corners()
{
    const VehicleParameters parameters;
    auto ground = std::make_shared<ToggleableFrontLeftStepGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    VehicleInput input;
    input.gear = VehicleGear::Neutral;
    input.brake = 1.f;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 120);

    const auto flat = vehicle.get_state();
    ground->activate(flat.east, flat.north, 0.08);
    VehicleState state = vehicle.update(kDt);
    auto previous_lengths = reconstruct_suspension_lengths(state, parameters);
    const double minimum_length = parameters.suspension.rest_length_m
        - parameters.suspension.max_compression_m;
    require(previous_lengths[0] <= minimum_length + 0.005,
            "raised front-left corner must activate its compression hard-stop"
                "; length=" + std::to_string(previous_lengths[0]));

    // A different spring must be allowed to compress while it is still well
    // inside its travel. Treating every valid ray as an active hard-stop
    // velocity constraint incorrectly pins this motion to zero.
    double greatest_rear_left_compression_step_m = 0.0;
    double least_rear_left_length_m = previous_lengths[2];
    for (int step = 0; step < 12; ++step) {
        state = vehicle.update(kDt);
        const auto lengths = reconstruct_suspension_lengths(state, parameters);
        require(std::all_of(
                    lengths.begin(), lengths.end(),
                    [](double length) { return std::isfinite(length); }),
                "single-corner hard-stop response must retain four contacts");
        if (previous_lengths[2] > minimum_length + 0.01
            && lengths[2] > minimum_length + 0.01) {
            greatest_rear_left_compression_step_m = std::max(
                greatest_rear_left_compression_step_m,
                previous_lengths[2] - lengths[2]);
        }
        least_rear_left_length_m = std::min(
            least_rear_left_length_m, lengths[2]);
        previous_lengths = lengths;
    }

    require(least_rear_left_length_m > minimum_length + 0.01,
            "rear-left corner must remain an inactive hard-stop with usable"
                " suspension travel; length="
                + std::to_string(least_rear_left_length_m));
    require(greatest_rear_left_compression_step_m > 1e-4,
            "one active hard-stop must not constrain heave/roll/pitch velocity"
                " at a different corner that still has suspension travel"
                "; observed compression step="
                + std::to_string(greatest_rear_left_compression_step_m));
}

void test_adjacent_wheels_use_their_own_contact_tangent_for_slip()
{
    VehicleParameters parameters;
    parameters.drag_coefficient = 0.f;
    parameters.rolling_resistance_coeff = 0.f;
    parameters.drivetrain_drag_n_per_mps = 0.f;
    auto ground = std::make_shared<ToggleableFrontRightSlopeGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    VehicleInput input;
    input.gear = VehicleGear::Drive;
    input.throttle = 1.f;
    vehicle.set_input(input);
    for (int step = 0;
         step < 600 && vehicle.get_state().speed < 8.f;
         ++step) {
        vehicle.update(kDt);
    }
    require(vehicle.get_state().speed >= 8.f,
            "local-tangent slip fixture must reach its probe speed");

    input.gear = VehicleGear::Neutral;
    input.throttle = 0.f;
    vehicle.set_input(input);
    const auto before = vehicle.get_state();
    require(std::abs(before.linear_velocity_body.y) < 1e-6
            && std::abs(before.angular_velocity_body.z) < 1e-6,
            "local-tangent slip fixture requires deterministic straight motion");

    constexpr double slope_degrees = 25.0;
    const double north_slope = std::tan(
        slope_degrees * std::numbers::pi_v<double> / 180.0);
    const double front_axle_offset = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    // Keep the new sloped patch below its hard-stop during this one-tick
    // kinematics probe. Only the normal/tangent distinction is under test.
    ground->activate(
        before.east,
        before.north,
        before.north + front_axle_offset + 0.25,
        north_slope);
    const auto after = vehicle.update(kDt);
    require(after.wheels[0].in_contact && after.wheels[1].in_contact,
            "both adjacent front wheels must remain supported in the tangent"
                " projection fixture");

    const double tilted_normal_scale = 1.0 / std::hypot(north_slope, 1.0);
    const std::array<double, 3> tilted_normal{
        0.0, -north_slope * tilted_normal_scale, tilted_normal_scale};
    const std::array<double, 3> averaged_normal{
        tilted_normal[0],
        tilted_normal[1],
        3.0 + tilted_normal[2]};
    const double heading_radians = before.heading
        * std::numbers::pi_v<double> / 180.0;
    const auto common_basis = make_test_ground_basis_from_normal(
        heading_radians, averaged_normal);
    const auto tilted_basis = make_test_ground_basis_from_normal(
        heading_radians, tilted_normal);
    const double expected_front_right_velocity = before.speed
        * test_dot(common_basis.forward, tilted_basis.forward);

    const auto& front_right = after.wheels[1];
    require(std::abs(front_right.longitudinal_slip) < 0.5f,
            "tangent projection probe must stay outside slip clamping");
    const double inferred_front_right_velocity =
        front_right.angular_speed * parameters.tire_radius_m
        / (1.0 + front_right.longitudinal_slip);
    require(std::abs(
                inferred_front_right_velocity
                - expected_front_right_velocity) < 2e-3,
            "front-right slip must use its own tilted tangent projection"
                "; inferred="
                + std::to_string(inferred_front_right_velocity)
                + ", expected="
                + std::to_string(expected_front_right_velocity));
    require(std::abs(expected_front_right_velocity - before.speed) > 0.2,
            "fixture must distinguish a wheel-local tangent projection from"
                " reusing the common averaged-ground longitudinal speed");
}

void test_single_supported_drive_wheel_does_not_spin_either_half_shaft()
{
    VehicleParameters parameters;
    parameters.front_drive_torque_fraction = 0.f;
    auto ground = std::make_shared<WestHalfGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    VehicleInput input;
    input.gear = VehicleGear::Drive;
    input.throttle = 1.f;
    vehicle.set_input(input);

    VehicleState state;
    for (int step = 0; step < 6; ++step) {
        state = vehicle.update(kDt);
        require(state.wheels[2].in_contact && !state.wheels[3].in_contact,
                "partial-contact setup must support only the left driven wheel");
    }

    const double road_speed_scale = std::abs(state.speed) + 0.5;
    for (std::size_t index : {std::size_t{2}, std::size_t{3}}) {
        const double surface_speed = std::abs(
            static_cast<double>(state.wheels[index].angular_speed)
            * parameters.tire_radius_m);
        require(surface_speed <= road_speed_scale,
                "one unsupported half-shaft must not make either driven wheel"
                    " visibly run away; wheel=" + std::to_string(index)
                    + ", surface_speed=" + std::to_string(surface_speed)
                    + ", chassis_speed=" + std::to_string(state.speed));
    }
}

void test_baked_surface_boundary_stops_at_last_supported_pose()
{
    constexpr double boundary_north_m = 4.0;
    auto ground = std::make_shared<NorthBoundedGroundQuery>(boundary_north_m);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);

    advance(vehicle, 1200);
    const auto stopped = vehicle.get_state();
    require(stopped.speed == 0.f,
            "loss of baked ground coverage must settle the vehicle");
    require(stopped.position_enu.z >= 0.0,
            "coverage loss must never integrate the chassis below the last ground"
                "; z=" + std::to_string(stopped.position_enu.z)
                + ", north=" + std::to_string(stopped.north)
                + ", pitch=" + std::to_string(stopped.pitch)
                + ", roll=" + std::to_string(stopped.roll));
    require(stopped.north < boundary_north_m + 1.5,
            "vehicle must stop near the last supported axle, not cross the map gap");
    require(std::any_of(
                stopped.wheels.begin(), stopped.wheels.end(),
                [](const WheelState& wheel) { return wheel.in_contact; }),
            "fail-closed pose must retain at least one supported wheel");
    require(std::abs(stopped.pitch) < 30.f && std::abs(stopped.roll) < 30.f,
            "coverage fail-close must not leave the chassis tipped at the edge"
                "; pitch=" + std::to_string(stopped.pitch)
                + ", roll=" + std::to_string(stopped.roll));
    require(std::hypot(
                stopped.angular_velocity_body.x,
                stopped.angular_velocity_body.y) < 1e-6,
            "coverage fail-close must clear residual pitch/roll velocity");

    const double held_north = stopped.north;
    const double held_up = stopped.position_enu.z;
    const float held_pitch = stopped.pitch;
    const float held_roll = stopped.roll;
    advance(vehicle, 300);
    const auto held = vehicle.get_state();
    require(std::abs(held.north - held_north) < 1e-6
            && std::abs(held.position_enu.z - held_up) < 0.01
            && std::abs(held.pitch - held_pitch) < 1e-5f
            && std::abs(held.roll - held_roll) < 1e-5f,
            "continued throttle must not accumulate hidden motion or fall time at a map boundary"
                "; north delta=" + std::to_string(held.north - held_north)
                + ", up delta="
                + std::to_string(held.position_enu.z - held_up));
}

void test_vehicle_suspension_force_respects_configured_bound()
{
    VehicleParameters parameters;
    parameters.suspension.spring_rate_n_per_m = 100000.f;
    parameters.suspension.max_force_n = 5000.f;
    auto raised_ground = std::make_shared<simcore_host::FlatGroundQuery>(0.2);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, raised_ground);
    const auto state = vehicle.get_state();

    for (const auto& wheel : state.wheels) {
        require(wheel.in_contact, "raised flat ground must remain inside the query range");
        require(wheel.normal_load == parameters.suspension.max_force_n,
                "published suspension load must respect the configured force bound");
    }
}

void test_vehicle_follows_an_uphill_ground_plane()
{
    constexpr double north_slope = 0.04;
    auto ground = std::make_shared<PlaneGroundQuery>(0.0, north_slope, 0.0);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    VehicleInput input;
    input.throttle = 0.55f;
    vehicle.set_input(input);
    advance(vehicle, 360);

    const auto driven_state = vehicle.get_state();
    require(driven_state.north > 5.0,
            "throttle must move the vehicle uphill on the ground plane");
    const double driven_expected_height = 0.55
        + north_slope * driven_state.north;
    require(std::abs(driven_state.position_enu.z - driven_expected_height) < 0.08,
            "chassis heave must follow the authoritative slope height");
    for (const auto& wheel : driven_state.wheels) {
        require(wheel.in_contact,
                "all wheels must retain contact during the shallow uphill run");
    }

    input.throttle = 0.f;
    input.brake = 1.f;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 360);
    const auto settled_state = vehicle.get_state();
    const double expected_pitch_degrees = std::atan(north_slope)
        * 180.0 / std::numbers::pi_v<double>;
    require(std::abs(settled_state.pitch - expected_pitch_degrees) < 0.5,
            "settled chassis pitch must converge toward the ground normal");
}

void test_stationary_20_degree_grade_has_physical_axle_loads()
{
    constexpr double grade_degrees = 20.0;
    constexpr double gravity_mps2 = 9.80665;
    const double grade_radians = grade_degrees
        * std::numbers::pi_v<double> / 180.0;
    const double slope = std::tan(grade_radians);

    VehicleParameters parameters;
    parameters.front_drive_torque_fraction = 0.f;
    auto ground = std::make_shared<PlaneGroundQuery>(0.0, slope, 0.0);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    VehicleInput input;
    input.brake = 1.f;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 300);

    const auto state = vehicle.get_state();
    const double front_load = state.wheels[0].normal_load
        + state.wheels[1].normal_load;
    const double rear_load = state.wheels[2].normal_load
        + state.wheels[3].normal_load;
    const double total_load = front_load + rear_load;
    const double expected_surface_normal_load = parameters.mass_kg
        * gravity_mps2 * std::cos(grade_radians);

    require(std::abs(total_load - expected_surface_normal_load)
                <= expected_surface_normal_load * 0.05,
            "stationary tire loads must balance the gravity component normal"
                " to a 20 degree grade; total=" + std::to_string(total_load)
                + ", expected="
                + std::to_string(expected_surface_normal_load));
    require(front_load > expected_surface_normal_load * 0.2,
            "the front axle must retain a physical share of stationary grade load"
                "; front=" + std::to_string(front_load));
    require(rear_load > expected_surface_normal_load * 0.2,
            "the driven rear axle must not be assigned zero stationary grade load"
                "; rear=" + std::to_string(rear_load));
}

void test_default_rwd_vehicle_climbs_a_20_degree_grade()
{
    constexpr double grade_degrees = 20.0;
    const double slope = std::tan(
        grade_degrees * std::numbers::pi_v<double> / 180.0);

    VehicleParameters parameters;
    // This is deliberately the production RWD layout. The regression must be
    // solved by physical axle load equilibrium, not by silently changing the
    // vehicle to all-wheel drive.
    parameters.front_drive_torque_fraction = 0.f;
    auto ground = std::make_shared<PlaneGroundQuery>(0.0, slope, 0.0);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);

    VehicleInput input;
    input.brake = 1.f;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 180);
    const double starting_north = vehicle.get_state().north;

    input.brake = 0.f;
    input.handbrake = false;
    input.throttle = 1.f;
    vehicle.set_input(input);

    double minimum_rear_load = std::numeric_limits<double>::max();
    float maximum_uphill_accel = -std::numeric_limits<float>::max();
    for (int step = 0; step < 360; ++step) {
        const auto state = vehicle.update(kDt);
        const double rear_load = state.wheels[2].normal_load
            + state.wheels[3].normal_load;
        minimum_rear_load = std::min(minimum_rear_load, rear_load);
        maximum_uphill_accel = std::max(maximum_uphill_accel, state.accel);
    }

    const auto state = vehicle.get_state();
    require(minimum_rear_load > parameters.mass_kg * 9.80665 * 0.1,
            "RWD traction must retain rear-axle normal load throughout the climb"
                "; minimum rear=" + std::to_string(minimum_rear_load));
    require(maximum_uphill_accel > 0.1f,
            "full throttle must produce positive uphill acceleration on a"
                " climbable 20 degree grade; maximum accel="
                + std::to_string(maximum_uphill_accel));
    require(state.speed > 0.5f && state.north > starting_north + 2.0,
            "the default RWD vehicle must make sustained uphill progress on a"
                " 20 degree grade; speed=" + std::to_string(state.speed)
                + ", north delta="
                + std::to_string(state.north - starting_north));
}

void test_vehicle_recovers_contact_on_a_fast_rising_compound_grade()
{
    auto ground = std::make_shared<RisingCompoundGroundQuery>();
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    VehicleInput input;
    input.throttle = 0.35f;
    vehicle.set_input(input);

    std::size_t minimum_contact_count = 4;
    for (int step = 0; step < 360; ++step) {
        const auto state = vehicle.update(kDt);
        const auto contact_count = static_cast<std::size_t>(std::count_if(
            state.wheels.begin(), state.wheels.end(),
            [](const WheelState& wheel) { return wheel.in_contact; }));
        minimum_contact_count = std::min(minimum_contact_count, contact_count);
        require(std::isfinite(state.position_enu.z),
                "compound grade must never produce a non-finite chassis height");
        require(state.position_enu.z > -0.1,
                "ground penetration recovery must prevent terrain fall-through");
    }

    const auto state = vehicle.get_state();
    require(state.north > 18.0,
            "test vehicle must reach the steep section of the compound grade");
    require(minimum_contact_count >= 2,
            "at least one axle must retain contact throughout the compound grade");
    const double center_ground_height = RisingCompoundGroundQuery::height_at(
        state.east, state.north);
    require(state.position_enu.z >= center_ground_height + 0.2,
            "chassis must remain above the authoritative compound ground");
    require(std::abs(state.pitch) > 6.0f || std::abs(state.roll) > 8.0f,
            "body attitude must no longer be limited by the old shallow-grade clamps");
}

void test_stationary_vehicle_stays_above_25_to_40_degree_grades()
{
    const VehicleParameters parameters;
    for (const double grade_degrees : {25.0, 32.5, 40.0}) {
        const double slope = std::tan(
            grade_degrees * std::numbers::pi_v<double> / 180.0);
        auto ground = std::make_shared<PlaneGroundQuery>(0.0, slope, 0.0);
        VehiclePhysics vehicle(
            kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
        VehicleInput input;
        input.brake = 1.f;
        input.handbrake = true;
        vehicle.set_input(input);

        for (int step = 0; step < 360; ++step) {
            const auto state = vehicle.update(kDt);
            require_vehicle_above_surface(
                state,
                parameters,
                [slope](double, double north) { return slope * north; },
                [slope](double) { return slope; },
                "stationary " + std::to_string(grade_degrees)
                    + " degree grade at step " + std::to_string(step));
        }

        const auto state = vehicle.get_state();
        require(state.speed == 0.f,
                "handbrake must hold a stationary vehicle on a steep grade");
        require(std::abs(state.north) < 1e-9,
                "a held vehicle must not creep into the steep surface");
    }
}

void test_low_speed_40_degree_transition_never_enters_the_surface()
{
    VehicleParameters parameters;
    parameters.max_drive_force_n = 30000.f;
    parameters.max_forward_speed_mps = 3.f;
    parameters.max_reverse_speed_mps = 3.f;
    parameters.tire_friction = 3.f;
    // Keep the transition longer than the 2.7 m wheelbase so this regression
    // isolates steep-surface non-penetration from a split-level breakover and
    // gradeability limit. Abrupt transitions are covered by fail-closed tests.
    auto ground = std::make_shared<SteepRampGroundQuery>(40.0, 3.0, 8.0);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);

    bool observed_forward_motion = false;
    double maximum_north = 0.0;
    std::optional<VehicleState> first_forced_stop;
    int first_forced_stop_step = -1;
    for (int step = 0; step < 900; ++step) {
        const auto state = vehicle.update(kDt);
        observed_forward_motion = observed_forward_motion || state.speed > 0.05f;
        maximum_north = std::max(maximum_north, state.north);
        if (observed_forward_motion
            && state.speed == 0.f
            && !first_forced_stop.has_value()) {
            first_forced_stop = state;
            first_forced_stop_step = step;
        }
        // Begin the penetration assertion when the front axle reaches the
        // transition. Flat-road launch heave is covered by separate tests and
        // must not obscure the steep-grade regression signal.
        if (state.north >= 1.5) {
            require_vehicle_above_surface(
                state,
                parameters,
                [ground](double east, double north) {
                    return ground->height_at(east, north);
                },
                [ground](double north) {
                    return ground->north_slope_at(north);
                },
                "low-speed 40 degree transition at step "
                    + std::to_string(step));
        }
        require(std::abs(state.speed) <= parameters.max_forward_speed_mps + 1e-4f,
                "steep-transition regression must remain a low-speed run");
    }
    std::string stop_context;
    if (first_forced_stop.has_value()) {
        std::string contacts;
        for (const auto& wheel : first_forced_stop->wheels) {
            contacts.push_back(wheel.in_contact ? '1' : '0');
        }
        stop_context = "; first forced stop: step="
            + std::to_string(first_forced_stop_step)
            + ", east=" + std::to_string(first_forced_stop->east)
            + ", north=" + std::to_string(first_forced_stop->north)
            + ", up=" + std::to_string(first_forced_stop->position_enu.z)
            + ", contacts=" + contacts
            + ", pitch=" + std::to_string(first_forced_stop->pitch)
            + ", roll=" + std::to_string(first_forced_stop->roll)
            + ", heading=" + std::to_string(first_forced_stop->heading);
    }
    const auto transition_end_state = vehicle.get_state();
    stop_context += "; final north="
        + std::to_string(transition_end_state.north)
        + ", speed=" + std::to_string(transition_end_state.speed)
        + ", up=" + std::to_string(transition_end_state.position_enu.z)
        + ", pitch=" + std::to_string(transition_end_state.pitch);
    require(observed_forward_motion && maximum_north >= 6.0,
            "test vehicle must drive well into the 40 degree transition"
                + stop_context);
}

void test_handbrake_holds_at_low_speed_on_a_slope()
{
    auto ground = std::make_shared<PlaneGroundQuery>(0.0, 0.05, 0.0);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    VehicleInput input;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 300);

    const auto state = vehicle.get_state();
    require(state.speed == 0.f,
            "handbrake must hold the vehicle against grade force at low speed");
    require(std::abs(state.north) < 1e-12,
            "a held vehicle must not creep downhill");
    require(std::abs(state.pitch) > 1.f,
            "a stationary chassis must still align to the slope");
}

void test_surface_material_multiplier_limits_grade_friction()
{
    constexpr double slope = 0.4;
    VehicleInput input;
    input.handbrake = true;

    auto asphalt_ground = std::make_shared<PlaneGroundQuery>(
        0.0,
        slope,
        0.0,
        simcore_host::GroundSurfaceMaterialId::Asphalt,
        1.0);
    VehiclePhysics asphalt(
        kInitialLat,
        kInitialLon,
        0.0,
        0.f,
        VehicleParameters{},
        asphalt_ground);
    asphalt.set_input(input);
    advance(asphalt, 300);

    VehicleParameters low_friction_parameters;
    low_friction_parameters.surface_low_friction_scale = 0.5f;
    auto low_friction_ground = std::make_shared<PlaneGroundQuery>(
        0.0,
        slope,
        0.0,
        simcore_host::GroundSurfaceMaterialId::LowFriction,
        0.5);
    VehiclePhysics low_friction(
        kInitialLat,
        kInitialLon,
        0.0,
        0.f,
        low_friction_parameters,
        low_friction_ground);
    low_friction.set_input(input);
    advance(low_friction, 300);

    const auto asphalt_state = asphalt.get_state();
    const auto low_friction_state = low_friction.get_state();
    require(std::abs(asphalt_state.north) < 1e-9
            && asphalt_state.speed == 0.f,
            "asphalt material must retain enough static friction to hold the grade");
    require(std::abs(low_friction_state.north) > 0.5
            && std::abs(low_friction_state.speed) > 0.1f,
            "terrain multiplier and vehicle material scale must reduce real tire force");
}

void test_cross_slope_sets_canonical_roll_sign()
{
    auto ground = std::make_shared<PlaneGroundQuery>(0.04, 0.0, 0.0);
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, VehicleParameters{}, ground);
    advance(vehicle, 180);
    const auto state = vehicle.get_state();

    require(state.roll < -1.f,
            "east-up cross slope must lower the canonical left side");
    require(std::isfinite(state.angular_velocity_body.x),
            "cross-slope roll rate must remain finite");
}

void test_idle_and_handbrake_are_stable()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 120);

    const auto state = vehicle.get_state();
    require(state.speed == 0.f, "handbrake at rest must not create reverse speed");
    require(state.east == 0.0 && state.north == 0.0,
            "stationary vehicle must not move");
    for (const auto& wheel : state.wheels) {
        require(wheel.angular_speed == 0.f,
                "stationary chassis must publish zero wheel angular speed");
    }
}

void test_reset_restores_the_configured_spawn_snapshot()
{
    VehicleParameters parameters;
    constexpr double initial_alt = 7.0;
    constexpr float initial_heading = 35.f;
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, initial_alt, initial_heading, parameters);

    VehicleInput input;
    input.throttle = 1.f;
    input.steering = 0.2f;
    vehicle.set_input(input);
    advance(vehicle, 180);

    const auto moved = vehicle.get_state();
    require(moved.speed > 0.f && std::hypot(moved.east, moved.north) > 1.0,
            "test setup must move the vehicle away from spawn");
    require(moved.fuel < 100.f,
            "test setup must mutate persistent-looking vehicle state");

    vehicle.reset();
    const auto reset = vehicle.get_state();

    require(reset.lat == kInitialLat && reset.lon == kInitialLon
            && reset.alt == initial_alt,
            "reset must restore the configured geodetic spawn");
    require(reset.east == 0.0 && reset.north == 0.0
            && reset.position_enu.x == 0.0
            && reset.position_enu.y == 0.0
            && reset.position_enu.z == parameters.cg_height_m,
            "reset must restore the local ENU spawn pose");
    require(std::abs(reset.heading - initial_heading) < 1e-6f
            && reset.pitch == 0.f && reset.roll == 0.f,
            "reset must restore the configured heading and ground attitude");
    require(reset.speed == 0.f && reset.accel == 0.f
            && reset.yaw_rate == 0.f,
            "reset must clear chassis motion");
    require(reset.linear_velocity_body.x == 0.0
            && reset.linear_velocity_body.y == 0.0
            && reset.linear_velocity_body.z == 0.0
            && reset.angular_velocity_body.x == 0.0
            && reset.angular_velocity_body.y == 0.0
            && reset.angular_velocity_body.z == 0.0,
            "reset must clear published body velocities");
    require(reset.fuel == 100.f && reset.rpm == parameters.idle_rpm
            && reset.gear == VehicleGear::Drive,
            "reset must restore consumable and driveline defaults");
    require(std::isfinite(reset.timestamp) && reset.timestamp > 0.0,
            "reset snapshot must carry a current Unix timestamp");
    for (std::size_t index = 0; index < reset.wheels.size(); ++index) {
        const auto& wheel = reset.wheels[index];
        require(wheel.wheel_index == index && wheel.in_contact,
                "reset must rebuild the canonical wheel contact snapshot");
        require(wheel.angular_speed == 0.f && wheel.steering_angle == 0.f
                && wheel.normal_load > 0.f,
                "reset must clear wheel dynamics while restoring static load");
    }

    const auto after_one_tick = vehicle.update(kDt);
    require(after_one_tick.speed == 0.f
            && after_one_tick.east == 0.0
            && after_one_tick.north == 0.0,
            "pre-reset throttle must not resume after reset");
}

void test_throttle_accelerates_and_input_is_clamped()
{
    auto normal = make_vehicle();
    auto clamped = make_vehicle();

    VehicleInput normal_input;
    normal_input.throttle = 1.f;
    normal.set_input(normal_input);

    VehicleInput oversized_input;
    oversized_input.throttle = 10.f;
    clamped.set_input(oversized_input);

    advance(normal, 180);
    advance(clamped, 180);

    const auto normal_state = normal.get_state();
    const auto clamped_state = clamped.get_state();
    require(normal_state.speed > 10.f, "full throttle must accelerate the vehicle");
    require(std::abs(normal_state.speed - clamped_state.speed) < 1e-5f,
            "throttle input must be clamped to one");
    require(normal_state.north > 0.0 && normal_state.lat > kInitialLat,
            "heading zero must move north in local ENU and WGS84 output");
}

void require_ackermann_angles(const VehicleState& state,
                              const VehicleParameters& parameters)
{
    const double centre_angle = state.steering_angle;
    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        double expected = 0.0;
        if (index < 2 && std::abs(centre_angle) > 1e-6) {
            const double direction = std::copysign(1.0, centre_angle);
            const double centre_radius = parameters.wheelbase_m
                / std::abs(std::tan(centre_angle));
            const double canonical_left = index == 0
                ? parameters.front_track_m * .5 : -parameters.front_track_m * .5;
            expected = direction * std::atan(parameters.wheelbase_m
                / (centre_radius - direction * canonical_left));
        }
        require(std::abs(state.wheels[index].steering_angle - expected) < 1e-6,
                "each wheel must preserve its Ackermann angle, including at rest");
    }
}

void test_steering_rack_is_speed_and_gear_independent()
{
    const auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    // Use only public pedals to establish distinct speeds. No test writes
    // velocity/pose. The same rack history must be identical at every tick,
    // independent of the subsequent physical speed/slip/gear response.
    for (const float entry_speed : {0.f, 3.f, 10.f, 25.f, 50.f, -5.f}) {
        VehiclePhysics moving(kInitialLat, kInitialLon, 0, 0, parameters);
        VehiclePhysics stationary(kInitialLat, kInitialLon, 0, 0, parameters);
        VehicleInput input;
        input.gear = entry_speed < 0.f ? VehicleGear::Reverse : VehicleGear::Drive;
        input.throttle = 1.f;
        moving.set_input(input);
        auto state = moving.get_state();
        for (int step = 0; step < 12000 && std::abs(state.speed) < std::abs(entry_speed); ++step) {
            state = moving.update(kDt);
        }
        require(std::abs(state.speed) >= std::abs(entry_speed),
                "rack test must establish its requested entry speed");
        input.throttle = 0.f;
        for (const float fraction : {1.f, .25f, 0.f, -1.f, .5f, 0.f}) {
            input.steering = fraction;
            moving.set_input(input);
            VehicleInput parked_input;
            parked_input.steering = fraction;
            stationary.set_input(parked_input);
            for (int step = 0; step < 60; ++step) {
                const double before = state.steering_angle;
                state = moving.update(kDt);
                const auto parked = stationary.update(kDt);
                const double target = fraction * parameters.max_steering_angle_rad;
                const bool returning = before * target <= 0
                    || std::abs(target) < std::abs(before);
                const double maximum_step = kDt * (returning
                    ? parameters.steering_return_rate_rad_s
                    : parameters.steering_rate_rad_s);
                const double expected = before
                    + std::clamp(target - before, -maximum_step, maximum_step);
                require(std::abs(state.steering_angle - expected) < 1e-6,
                        "rack must obey the configured finite slew without speed scaling");
                require(std::abs(state.steering_angle - parked.steering_angle) < 1e-6,
                        "identical steering history must produce identical rack angles at all speeds/gears");
                require_ackermann_angles(state, parameters);
                for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                    require(std::abs(state.wheels[wheel].steering_angle
                                - parked.wheels[wheel].steering_angle) < 1e-6,
                            "speed must not attenuate either front road-wheel angle");
                }
                if (fraction == 1.f && step == 20) {
                    require(std::abs(state.steering_angle
                                - parameters.max_steering_angle_rad) < 1e-6,
                            "full lock must be reached within 350 ms, even at 180 km/h entry");
                    if (entry_speed >= 25.f) {
                        require(state.speed > entry_speed * .8f,
                                "high-speed full-lock test must not pass after slowing to parking speed");
                    }
                }
            }
            require(std::abs(state.steering_angle
                        - fraction * parameters.max_steering_angle_rad) < 1e-6,
                    "settled steering angle must equal normalized input times maximum angle");
        }
    }
    std::cout << "steering-contract: entry_mps=0,3,10,25,50,-5"
        << " fractions=1,.25,0,-1,.5,0; identical rack/per-wheel angles; full_lock_ms<=350\n";
}

void test_steering_clamp_and_boundary_stop_preserve_ackermann()
{
    const VehicleParameters parameters;
    for (const float input_value : {-2.f, 2.f}) {
        auto vehicle = make_vehicle();
        VehicleInput input;
        input.steering = input_value;
        vehicle.set_input(input);
        advance(vehicle, 60);
        const auto state = vehicle.get_state();
        require(std::abs(state.steering_angle - std::copysign(
                    parameters.max_steering_angle_rad, input_value)) < 1e-6,
                "oversized normalized steering must clamp to the mechanical limit");
        require_ackermann_angles(state, parameters);
    }

    auto ground = std::make_shared<NorthBoundedGroundQuery>(5.0);
    VehiclePhysics vehicle(kInitialLat, kInitialLon, 0, 0, parameters, ground);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    advance(vehicle, 300);
    const auto stopped = vehicle.get_state();
    require(stopped.speed == 0.f && stopped.north > 2.0,
            "boundary test must actually reach the finite-map stop");
    input.steering = 1.f;
    vehicle.set_input(input);
    for (int step = 0; step < 60; ++step) {
        const auto state = vehicle.update(kDt);
        require_ackermann_angles(state, parameters);
    }
}

struct ConstantSpeedTurnSample {
    double speed_mps = 0.0;
    double minimum_sample_speed_mps = std::numeric_limits<double>::max();
    double maximum_sample_speed_mps = 0.0;
    double path_speed_mps = 0.0;
    double signed_path_turn_rate_rad_s = 0.0;
    double radius_m = 0.0;
    double lateral_accel_mps2 = 0.0;
    double road_wheel_degrees = 0.0;
    double maximum_body_slip_degrees = 0.0;
    double maximum_roll_degrees = 0.0;
    double maximum_tire_slip_degrees = 0.0;
    double average_normal_load_n = 0.0;
    double minimum_height_m = std::numeric_limits<double>::max();
    double maximum_height_m = -std::numeric_limits<double>::max();
    std::size_t minimum_contact_count = 4;
};

ConstantSpeedTurnSample measure_constant_speed_turn(
    const VehicleParameters& parameters, float target_speed_mps,
    float steering_fraction = 1.f)
{
    VehiclePhysics vehicle(kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    VehicleInput input;
    double speed_error_integral = 0.0;
    auto state = vehicle.get_state();
    const auto tick_cruise = [&]() {
        const double error = target_speed_mps - state.speed;
        speed_error_integral = std::clamp(
            speed_error_integral + error * kDt, -1.0, 4.0);
        const double requested_pedal = 0.06 + 0.45 * error
            + 0.20 * speed_error_integral;
        input.throttle = static_cast<float>(
            std::clamp(requested_pedal, 0.0, 1.0));
        input.brake = static_cast<float>(
            std::clamp(-requested_pedal * 0.20, 0.0, 0.25));
        vehicle.set_input(input);
        state = vehicle.update(kDt);
    };

    // The controller only operates the accelerator/brake. It never edits the
    // vehicle pose or velocity, so measured curvature comes from tire forces.
    for (int step = 0; step < 1500; ++step) {
        tick_cruise();
    }
    require(std::abs(state.speed - target_speed_mps) < 0.20f,
            "constant-speed turning diagnostic must establish the entry speed");
    ConstantSpeedTurnSample result;
    const auto check_transient_and_steady_stability = [&]() {
        result.maximum_body_slip_degrees = std::max(
            result.maximum_body_slip_degrees,
            std::abs(std::atan2(state.linear_velocity_body.y,
                               state.linear_velocity_body.x))
                * 180.0 / std::numbers::pi_v<double>);
        result.maximum_roll_degrees = std::max(
            result.maximum_roll_degrees,
            std::abs(static_cast<double>(state.roll)));
        result.minimum_contact_count = std::min(
            result.minimum_contact_count,
            static_cast<std::size_t>(std::count_if(
                state.wheels.begin(), state.wheels.end(),
                [](const WheelState& wheel) { return wheel.in_contact; })));
        for (const auto& wheel : state.wheels) {
            result.maximum_tire_slip_degrees = std::max(
                result.maximum_tire_slip_degrees,
                std::abs(static_cast<double>(wheel.slip_angle))
                    * 180.0 / std::numbers::pi_v<double>);
            const double force_magnitude = std::hypot(
                wheel.longitudinal_force, wheel.lateral_force);
            require(force_magnitude <= parameters.tire_friction
                        * wheel.normal_load * 1.01 + 0.01,
                    "sharper steering must not bypass the tire friction circle");
        }
        require(std::isfinite(state.east) && std::isfinite(state.north)
                    && std::isfinite(state.yaw_rate),
                "sustained steering must keep integrated motion finite");
    };

    input.steering = steering_fraction;
    double previous_course_rad = 0.0;
    // Allow the pedal integrator to settle after the added cornering drag.
    for (int step = 0; step < 720; ++step) {
        const auto previous = state;
        tick_cruise();
        check_transient_and_steady_stability();
        previous_course_rad = std::atan2(
            state.east - previous.east, state.north - previous.north);
    }

    constexpr int sample_count = 180;
    for (int step = 0; step < sample_count; ++step) {
        const auto previous = state;
        tick_cruise();
        check_transient_and_steady_stability();
        // Measure the actual ENU CG path, not vx/body-Z angular rate. At low
        // speed the CG legitimately has a velocity angle relative to the body.
        const double east_step = state.east - previous.east;
        const double north_step = state.north - previous.north;
        const double distance_m = std::hypot(east_step, north_step);
        const double course_rad = std::atan2(east_step, north_step);
        const double course_step = std::remainder(
            course_rad - previous_course_rad, 2.0 * std::numbers::pi_v<double>);
        previous_course_rad = course_rad;
        const double path_speed = distance_m / kDt;
        const double turn_rate = std::abs(course_step) / kDt;
        result.speed_mps += state.speed;
        result.minimum_sample_speed_mps = std::min(result.minimum_sample_speed_mps,
            static_cast<double>(state.speed));
        result.maximum_sample_speed_mps = std::max(result.maximum_sample_speed_mps,
            static_cast<double>(state.speed));
        result.path_speed_mps += path_speed;
        result.signed_path_turn_rate_rad_s += -course_step / kDt;
        result.radius_m += distance_m / std::max(2.0 * std::sin(std::abs(course_step) * .5), 1e-9);
        result.lateral_accel_mps2 += path_speed * turn_rate;
        result.road_wheel_degrees += std::abs(state.steering_angle)
            * 180.0 / std::numbers::pi_v<double>;
        result.minimum_height_m = std::min(result.minimum_height_m, state.position_enu.z);
        result.maximum_height_m = std::max(result.maximum_height_m, state.position_enu.z);
        for (const auto& wheel : state.wheels) result.average_normal_load_n += wheel.normal_load;
    }
    result.speed_mps /= sample_count;
    result.path_speed_mps /= sample_count;
    result.signed_path_turn_rate_rad_s /= sample_count;
    result.radius_m /= sample_count;
    result.lateral_accel_mps2 /= sample_count;
    result.road_wheel_degrees /= sample_count;
    result.average_normal_load_n /= sample_count;
    return result;
}

void print_turn_sample(const ConstantSpeedTurnSample& sample,
                       float target_speed, float fraction)
{
    std::cout << "steering-diagnostic: target_kph=" << target_speed * 3.6f
        << " input=" << fraction << " longitudinal_kph=" << sample.speed_mps * 3.6
        << " path_kph=" << sample.path_speed_mps * 3.6
        << " speed_range_mps=" << sample.minimum_sample_speed_mps
        << ".." << sample.maximum_sample_speed_mps
        << " radius_m=" << sample.radius_m << " ay=" << sample.lateral_accel_mps2
        << " centre_deg=" << sample.road_wheel_degrees
        << " max_body_slip_deg=" << sample.maximum_body_slip_degrees
        << " max_tire_slip_deg=" << sample.maximum_tire_slip_degrees
        << " max_roll_deg=" << sample.maximum_roll_degrees
        << " mean_normal_load_n=" << sample.average_normal_load_n
        << " min_contacts=" << sample.minimum_contact_count << '\n';
}

void test_fixed_angle_turns_are_measured_from_tire_driven_paths()
{
    const auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    struct TurnCase { float speed_mps; float fraction; };
    // Parking full-lock and modest fixed angles on the road are separate
    // maneuvers. A 90 km/h full-lock step is NOT a no-slip acceptance test.
    const TurnCase cases[]{
        {3.f, 1.f}, {5.f, 1.f},
        {30.f / 3.6f, .15f}, {40.f / 3.6f, .15f},
        {50.f / 3.6f, .15f}, {25.f, .04f}};
    for (const auto& entry : cases) {
        const auto left = measure_constant_speed_turn(
            parameters, entry.speed_mps, entry.fraction);
        const auto right = measure_constant_speed_turn(
            parameters, entry.speed_mps, -entry.fraction);
        print_turn_sample(left, entry.speed_mps, entry.fraction);
        for (const auto& sample : {left, right}) {
            require(std::abs(sample.speed_mps - entry.speed_mps) < .20
                        && sample.maximum_sample_speed_mps
                            - sample.minimum_sample_speed_mps < .50,
                    "constant-speed path comparison must actually hold its target speed");
            const double target_degrees = entry.fraction
                * parameters.max_steering_angle_rad * 180.0 / std::numbers::pi_v<double>;
            require(std::abs(sample.road_wheel_degrees - target_degrees) < 1e-4,
                    "steady road-wheel angle must stay fixed, not track speed or grip");
            require(std::isfinite(sample.radius_m) && sample.radius_m > 0.0
                        && sample.minimum_contact_count == 4
                        && sample.maximum_roll_degrees < 8.0,
                    "modest road steering and parking maneuvers must remain finite and grounded");
            const double geometric_cg_radius = std::hypot(
                parameters.wheelbase_m / std::tan(
                    entry.fraction * parameters.max_steering_angle_rad),
                parameters.wheelbase_m * parameters.front_static_load_fraction);
            require(sample.radius_m >= geometric_cg_radius * .85
                        && sample.radius_m <= geometric_cg_radius * 1.5,
                    "modest fixed-angle road turns must retain useful physical cornering authority");
        }
        require(left.signed_path_turn_rate_rad_s > 0.0
                    && right.signed_path_turn_rate_rad_s < 0.0
                    && std::abs(right.radius_m - left.radius_m) < left.radius_m * .02,
                "left/right physical ENU paths must have opposite curvature and symmetric radii");
        if (entry.speed_mps <= 5.f) {
            const double rear_axle_radius = parameters.wheelbase_m
                / std::tan(parameters.max_steering_angle_rad);
            const double geometric_cg_radius = std::hypot(rear_axle_radius,
                parameters.wheelbase_m * parameters.front_static_load_fraction);
            require(left.radius_m > geometric_cg_radius * .9
                        && left.radius_m < geometric_cg_radius * 1.25,
                    "low-speed full lock should approach Ackermann geometry without forced yaw");
        }
    }
}

void test_steady_flat_corner_preserves_total_normal_support()
{
    const auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    const double weight_n = parameters.mass_kg * 9.80665;
    for (const float direction : {-1.f, 1.f}) {
        const auto sample = measure_constant_speed_turn(parameters, 50.f / 3.6f, direction * .15f);
        require(sample.minimum_contact_count == 4
                    && sample.maximum_height_m - sample.minimum_height_m < .003,
                "support balance requires a settled, flat, four-contact turn, not airborne acceleration");
        // This is a force-balance assertion, never a runtime normalization to
        // mg: a flat, vertically settled chassis must expose its whole support
        // (spring/damper AND the active compression stops) to the tire budget.
        require(std::abs(sample.average_normal_load_n - weight_n) < weight_n * .02,
                "steady flat turning must not lose compression-stop support from tire normal loads; used="
                    + std::to_string(sample.average_normal_load_n)
                    + ", expected=" + std::to_string(weight_n));
    }
}

void test_sedan_small_signal_turning_balance()
{
    const auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    // Offline small-signal identification (0.525 degrees), not a keyboard
    // input or a full-lock grip test. No runtime speed-to-angle mapping exists.
    const auto low = measure_constant_speed_turn(parameters, 30.f / 3.6f, .015f);
    const auto high = measure_constant_speed_turn(parameters, 25.f, .015f);
    require(std::abs(low.speed_mps - 30.0 / 3.6) < .2
                && std::abs(high.speed_mps - 25.0) < .2,
            "small-signal handling comparison must actually hold 30 and 90 km/h");
    const double a = parameters.wheelbase_m * (1.0 - parameters.front_static_load_fraction);
    const double b = parameters.wheelbase_m * parameters.front_static_load_fraction;
    const double gradient = parameters.mass_kg / parameters.wheelbase_m
        * (b / (2.0 * parameters.front_tire_corner_stiffness_n_rad)
            - a / (2.0 * parameters.rear_tire_corner_stiffness_n_rad));
    require(gradient >= 0.0 && gradient < .0002,
            "tracked sedan should retain mild positive small-signal understeer, not oversteer");
    for (const auto& sample : {low, high}) {
        const double predicted_radius = (parameters.wheelbase_m
            + gradient * sample.speed_mps * sample.speed_mps)
            / std::tan(.015 * parameters.max_steering_angle_rad);
        require(std::abs(sample.radius_m - predicted_radius) < predicted_radius * .02,
                "unsaturated tire-driven path should agree with the independently derived axle-stiffness model");
    }
    require(high.radius_m >= low.radius_m * .99 && high.radius_m < low.radius_m * 1.05,
            "small-signal radius must not widen excessively with speed in the selected sedan calibration");
    std::cout << "steering-small-signal: radius30_m=" << low.radius_m
        << " radius90_m=" << high.radius_m << " understeer_rad_per_mps2=" << gradient << '\n';
}

void test_compression_stop_support_uses_actual_impulse_and_clears_on_reset()
{
    VehicleParameters parameters;
    parameters.front_static_load_fraction = .5f;
    // Deliberately weak springs isolate the missing rigid-stop reaction. Its
    // load is not limited by the spring actuator's 1000 N maximum.
    parameters.suspension.spring_rate_n_per_m = 10000.f;
    parameters.suspension.max_force_n = 1000.f;
    auto ground = std::make_shared<ToggleableFlatGroundQuery>(true);
    VehiclePhysics vehicle(kInitialLat, kInitialLon, 0, 0, parameters, ground);
    VehicleInput input;
    input.gear = VehicleGear::Neutral;
    vehicle.set_input(input);
    advance(vehicle, 240);
    const auto settled = vehicle.get_state();
    const auto previous = vehicle.get_wheel_contact_support_diagnostics();
    const auto lengths = reconstruct_suspension_lengths(settled, parameters);
    double total_load = 0, total_reaction = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        total_load += settled.wheels[i].normal_load;
        total_reaction += previous[i].hard_stop_impulse_n_s / kDt;
        require(std::abs(lengths[i] - (parameters.suspension.rest_length_m
                    - parameters.suspension.max_compression_m)) < .001,
                "weak-spring fixture must actually rest on all four compression stops");
        require(std::abs(settled.wheels[i].normal_load
                    - previous[i].suspension_normal_force_n
                    - previous[i].tire_hard_stop_normal_force_n) < .01,
                "published tire capacity must use both spring and stop support");
    }
    require(total_reaction > 10000.0
                && std::abs(total_load - parameters.mass_kg * 9.80665) < 5.0,
            "rigid stops must supply the missing vertical support without a spring-force clamp");

    vehicle.update(kDt * .5);
    const auto changed_dt = vehicle.get_wheel_contact_support_diagnostics();
    for (std::size_t i = 0; i < 4; ++i) {
        require(std::abs(changed_dt[i].tire_hard_stop_normal_force_n
                    - previous[i].hard_stop_impulse_n_s / kDt) < .1,
                "carried support is J divided by its producing dt, not the receiving dt");
    }
    ground->set_height(-.01);
    const auto released = vehicle.update(kDt);
    const auto release_support = vehicle.get_wheel_contact_support_diagnostics();
    for (std::size_t i = 0; i < 4; ++i) {
        require(released.wheels[i].in_contact
                    && release_support[i].tire_hard_stop_normal_force_n == 0.f,
                "leaving the compression stop must clear its grip even when tire contact remains");
    }
    ground->set_height(0.0);
    advance(vehicle, 120);
    ground->set_enabled(false);
    vehicle.update(kDt);
    for (const auto& support : vehicle.get_wheel_contact_support_diagnostics()) {
        require(support.hard_stop_impulse_n_s == 0.0
                    && support.tire_hard_stop_normal_force_n == 0.f,
                "missing contact/coverage must discard all carried rigid-stop support");
    }
    ground->set_enabled(true);
    vehicle.update(kDt);
    for (const auto& support : vehicle.get_wheel_contact_support_diagnostics()) {
        require(support.tire_hard_stop_normal_force_n == 0.f,
                "contact reacquisition must not reuse an impulse from before contact loss");
    }
    advance(vehicle, 120);
    vehicle.reset();
    for (const auto& support : vehicle.get_wheel_contact_support_diagnostics()) {
        require(support.hard_stop_impulse_n_s == 0.0
                    && support.tire_hard_stop_normal_force_n == 0.f,
                "reset must clear hard-stop diagnostics and cached reactions");
    }
    vehicle.update(kDt);
    for (const auto& support : vehicle.get_wheel_contact_support_diagnostics()) {
        require(support.tire_hard_stop_normal_force_n == 0.f,
                "the first post-reset tick must not inherit old stop grip");
    }
}

void test_rotating_body_frame_does_not_create_force_free_energy()
{
    VehicleParameters parameters;
    parameters.rolling_resistance_coeff = 0.f;
    parameters.drivetrain_drag_n_per_mps = 0.f;
    parameters.drag_coefficient = 0.f;
    VehiclePhysics vehicle(kInitialLat, kInitialLon, 0, 0, parameters);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    auto state = vehicle.get_state();
    for (int i = 0; i < 12000 && state.speed < 40.f; ++i) state = vehicle.update(kDt);
    require(state.speed >= 40.f, "rotating-frame fixture must reach road speed through pedals");
    input.steering = 1.f;
    input.gear = VehicleGear::Neutral;
    input.throttle = 0.f;
    vehicle.set_input(input);
    double maximum_euler_energy_error_j = 0.0;
    double maximum_rotation_energy_error_j = 0.0;
    for (int i = 0; i < 60; ++i) {
        const auto before = state;
        state = vehicle.update(kDt);
        const double roll = before.roll * std::numbers::pi_v<double> / 180.0;
        const double pitch = before.pitch * std::numbers::pi_v<double> / 180.0;
        const double clockwise_yaw_rate = -(before.angular_velocity_body.y * std::sin(roll)
            + before.angular_velocity_body.z * std::cos(roll)) / std::cos(pitch);
        double force_x = 0.0, force_y = 0.0;
        for (const auto& wheel : state.wheels) {
            require(wheel.in_contact, "rotating-frame fixture must remain grounded");
            const double sine = std::sin(-wheel.steering_angle);
            const double cosine = std::cos(wheel.steering_angle);
            force_x += cosine * wheel.longitudinal_force + sine * wheel.lateral_force;
            force_y += sine * wheel.longitudinal_force - cosine * wheel.lateral_force;
        }
        // Remove the analytically integrated, frozen sampled force. What is
        // left is only a coordinate rotation and must preserve kinetic energy.
        // There is no gravity tangent, aero, rolling, or driveline force here.
        const double angle = clockwise_yaw_rate * kDt;
        const double sinc = std::abs(angle) < 1e-8 ? 1.0 : std::sin(angle) / angle;
        const double cosc = std::abs(angle) < 1e-8 ? angle * .5
            : (1.0 - std::cos(angle)) / angle;
        const double residual_forward = state.linear_velocity_body.x
            - kDt / parameters.mass_kg * (sinc * force_x + cosc * force_y);
        const double residual_right = -state.linear_velocity_body.y
            - kDt / parameters.mass_kg * (-cosc * force_x + sinc * force_y);
        const double initial_speed_squared = before.linear_velocity_body.x * before.linear_velocity_body.x
            + before.linear_velocity_body.y * before.linear_velocity_body.y;
        const double initial_energy = .5 * parameters.mass_kg * initial_speed_squared;
        const double residual_energy = .5 * parameters.mass_kg
            * (residual_forward * residual_forward + residual_right * residual_right);
        maximum_rotation_energy_error_j = std::max(maximum_rotation_energy_error_j,
            std::abs(residual_energy - initial_energy));
        maximum_euler_energy_error_j = std::max(maximum_euler_energy_error_j,
            initial_energy * angle * angle);
    }
    require(maximum_euler_energy_error_j > 20.0,
        "rotating-frame regression must exercise meaningful yaw, not a straight zero-force no-op");
    std::cout << "rotating-frame-energy: peak_old_euler_error_j=" << maximum_euler_energy_error_j
        << " peak_rotation_error_j=" << maximum_rotation_energy_error_j << '\n';
    require(maximum_rotation_energy_error_j < 1.0,
        "coordinate rotation must preserve frozen-force residual energy; peak_error_j="
            + std::to_string(maximum_rotation_energy_error_j));
}

void test_high_speed_full_lock_saturates_tires_not_steering()
{
    const auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    struct PedalCase { const char* name; VehicleGear gear; float throttle; };
    const PedalCase modes[]{
        {"drive-full", VehicleGear::Drive, 1.f},
        {"drive-coast", VehicleGear::Drive, 0.f},
        {"neutral", VehicleGear::Neutral, 0.f}};
    const auto run_case = [&](float target_speed, const PedalCase& mode, float direction) {
        VehiclePhysics vehicle(kInitialLat, kInitialLon, 0, 0, parameters);
        VehicleInput input;
        input.throttle = 1.f;
        vehicle.set_input(input);
        auto state = vehicle.get_state();
        for (int step = 0; step < 12000 && state.speed < target_speed; ++step) {
            state = vehicle.update(kDt);
        }
        require(state.speed >= target_speed,
                "saturation test must actually reach its requested 90/180 km/h entry speed");
        const double entry_speed = state.speed;
        const double entry_energy = .5 * parameters.mass_kg * entry_speed * entry_speed;
        input.throttle = mode.throttle;
        input.gear = mode.gear;
        input.steering = direction;
        vehicle.set_input(input);
        double peak_slip_degrees = 0.0;
        double peak_force_utilization = 0.0;
        for (int step = 0; step < 180; ++step) {
            const auto before = state;
            state = vehicle.update(kDt);
            require(std::isfinite(state.east) && std::isfinite(state.north)
                        && std::isfinite(state.yaw_rate)
                        && std::isfinite(state.pitch) && std::isfinite(state.roll),
                    "saturated steering must not create non-finite chassis motion");
            if (step >= 20) {
                require(std::abs(state.steering_angle
                            - direction * parameters.max_steering_angle_rad) < 1e-6,
                        "tire saturation must never reduce the driver's rack angle");
            }
            require_ackermann_angles(state, parameters);
            const auto navigation_yaw_rate = [](const VehicleState& sample) {
                const double roll = sample.roll * std::numbers::pi_v<double> / 180.0;
                const double pitch = sample.pitch * std::numbers::pi_v<double> / 180.0;
                return (sample.angular_velocity_body.y * std::sin(roll)
                    + sample.angular_velocity_body.z * std::cos(roll)) / std::cos(pitch);
            };
            const double previous_navigation_yaw = navigation_yaw_rate(before);
            double contact_yaw_moment = 0.0;
            for (std::size_t wheel_index = 0; wheel_index < state.wheels.size(); ++wheel_index) {
                const auto& wheel = state.wheels[wheel_index];
                const double capacity = parameters.tire_friction * wheel.normal_load;
                const double force = std::hypot(wheel.longitudinal_force, wheel.lateral_force);
                require(force <= capacity * 1.01 + .01,
                        "full lock must not synthesize additional tire grip");
                peak_force_utilization = std::max(peak_force_utilization,
                    force / std::max(capacity, 1.0));
                peak_slip_degrees = std::max(peak_slip_degrees,
                    std::abs(wheel.slip_angle) * 180.0 / std::numbers::pi_v<double>);
                const double x = parameters.wheelbase_m * (wheel_index < 2
                    ? 1.0 - parameters.front_static_load_fraction : -parameters.front_static_load_fraction);
                const double y = (wheel_index % 2 == 0 ? .5 : -.5)
                    * (wheel_index < 2 ? parameters.front_track_m : parameters.rear_track_m);
                const double sine = std::sin(wheel.steering_angle);
                const double cosine = std::cos(wheel.steering_angle);
                const double body_fx = cosine * wheel.longitudinal_force - sine * wheel.lateral_force;
                const double body_fy = sine * wheel.longitudinal_force + cosine * wheel.lateral_force;
                contact_yaw_moment += x * body_fy - y * body_fx;
                const double lateral_patch_velocity =
                    -sine * (before.linear_velocity_body.x - previous_navigation_yaw * y)
                    + cosine * (before.linear_velocity_body.y + previous_navigation_yaw * x);
                require(wheel.lateral_force * lateral_patch_velocity <= .1,
                    "saturated lateral tires must oppose slip rather than supply energy");
            }
            const double integrated_yaw_moment = parameters.yaw_inertia_kg_m2
                * (navigation_yaw_rate(state) - previous_navigation_yaw) / kDt;
            require(std::abs(integrated_yaw_moment - contact_yaw_moment) < .5,
                "even a reversing yaw transient must follow actual contact moments, not a sign clamp");
            const double planar_energy = .5 * parameters.mass_kg
                * (state.linear_velocity_body.x * state.linear_velocity_body.x
                   + state.linear_velocity_body.y * state.linear_velocity_body.y)
                + .5 * parameters.yaw_inertia_kg_m2 * state.yaw_rate * state.yaw_rate;
            if (mode.throttle == 0.f) {
                require(planar_energy <= entry_energy * 1.05,
                        "unpowered steering must not inject unbounded planar kinetic energy");
            }
        }
        require(peak_slip_degrees > 10.0 && peak_force_utilization > .9,
                "90/180 km/h full lock must exercise tire saturation, not a hidden steering cap");
        std::cout << "steering-saturation: mode=" << mode.name << " input=" << direction
            << " entry_kph=" << entry_speed * 3.6
            << " final_kph=" << state.speed * 3.6
            << " centre_deg=" << state.steering_angle * 180.0 / std::numbers::pi_v<double>
            << " peak_tire_slip_deg=" << peak_slip_degrees
            << " peak_force_utilization=" << peak_force_utilization << '\n';
    };
    for (const float target_speed : {25.f, parameters.max_forward_speed_mps}) {
        for (const auto& mode : modes) {
            for (const float direction : {-1.f, 1.f}) {
                run_case(target_speed, mode, direction);
            }
        }
    }
}

void test_city_steering_keeps_authored_low_friction_limits()
{
    const auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    constexpr double ground_friction = .35;
    for (const float direction : {-1.f, 1.f}) {
        auto ground = std::make_shared<PlaneGroundQuery>(0, 0, 0,
            simcore_host::GroundSurfaceMaterialId::LowFriction, ground_friction);
        VehiclePhysics vehicle(kInitialLat, kInitialLon, 0, 0, parameters, ground);
        VehicleInput input;
        input.throttle = 1.f;
        auto state = vehicle.get_state();
        vehicle.set_input(input);
        for (int step = 0; step < 1500 && state.speed < 10.f; ++step) {
            state = vehicle.update(kDt);
        }
        require(state.speed >= 10.f, "low-friction steering test must reach road speed");
        input.steering = direction;
        input.throttle = .1f;
        vehicle.set_input(input);
        double peak_lateral_force = 0.0;
        for (int step = 0; step < 180; ++step) {
            state = vehicle.update(kDt);
            for (const auto& wheel : state.wheels) {
                require(wheel.in_contact, "low-friction steering must retain wheel contact");
                const double capacity = parameters.tire_friction * ground_friction
                    * parameters.surface_low_friction_scale * wheel.normal_load;
                require(std::hypot(wheel.longitudinal_force, wheel.lateral_force)
                            <= capacity * 1.01 + .01,
                        "additional steering authority must not increase the authored friction circle");
                peak_lateral_force = std::max(peak_lateral_force,
                    std::abs(static_cast<double>(wheel.lateral_force)));
            }
            require(std::isfinite(state.east) && std::isfinite(state.north)
                        && std::isfinite(state.yaw_rate) && std::abs(state.roll) < 8.f,
                    "low-friction steering must remain finite with bounded body roll");
        }
        require(peak_lateral_force > 100.0,
                "low-friction regression must exercise lateral tire forces");
    }
}

void test_drive_coastdown_has_more_drag_than_neutral()
{
    auto drive_vehicle = make_vehicle();
    auto neutral_vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 1.f;
    input.gear = VehicleGear::Drive;
    drive_vehicle.set_input(input);
    neutral_vehicle.set_input(input);

    VehicleState drive_state;
    VehicleState neutral_state;
    for (int step = 0; step < 600 && drive_state.speed < 13.5f; ++step) {
        drive_state = drive_vehicle.update(kDt);
        neutral_state = neutral_vehicle.update(kDt);
    }
    require(drive_state.speed >= 13.5f,
            "coastdown setup must reach at least 13.5 m/s");
    require(std::abs(drive_state.speed - neutral_state.speed) < 1e-5f,
            "coastdown vehicles must begin from the same speed");

    const float starting_speed = drive_state.speed;
    VehicleInput drive_coast;
    drive_coast.gear = VehicleGear::Drive;
    VehicleInput neutral_coast;
    neutral_coast.gear = VehicleGear::Neutral;
    drive_vehicle.set_input(drive_coast);
    neutral_vehicle.set_input(neutral_coast);
    for (int step = 0; step < 300; ++step) {
        drive_state = drive_vehicle.update(kDt);
        neutral_state = neutral_vehicle.update(kDt);
    }

    const float drive_speed_loss = starting_speed - drive_state.speed;
    const float neutral_speed_loss = starting_speed - neutral_state.speed;
    require(drive_state.speed > 0.f && neutral_state.speed > 0.f,
            "five-second coastdown must slow without reversing either vehicle");
    require(drive_speed_loss >= neutral_speed_loss + 1.25f,
            "Drive coastdown must include material driveline drag beyond Neutral;"
            " drive_loss=" + std::to_string(drive_speed_loss)
                + ", neutral_loss=" + std::to_string(neutral_speed_loss));
}

void test_full_throttle_launch_avoids_sustained_rear_wheelspin()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 1.f;
    input.gear = VehicleGear::Drive;
    vehicle.set_input(input);

    constexpr float maximum_reasonable_launch_slip = 0.18f;
    float maximum_rear_slip = 0.f;
    VehicleState state;
    for (int step = 0; step < 120; ++step) {
        state = vehicle.update(kDt);
        for (std::size_t index = 2; index < state.wheels.size(); ++index) {
            require(state.wheels[index].in_contact,
                    "flat-ground launch must keep both driven rear tires in contact");
            maximum_rear_slip = std::max(
                maximum_rear_slip,
                std::abs(state.wheels[index].longitudinal_slip));
        }
    }

    require(state.speed > 5.f,
            "full-throttle sedan launch must produce useful forward motion");
    require(maximum_rear_slip <= maximum_reasonable_launch_slip,
            "a generic road sedan must not sustain excessive rear wheelspin;"
            " peak slip=" + std::to_string(maximum_rear_slip));
}

void test_flat_ground_launch_keeps_driven_wheels_road_coupled()
{
    VehicleParameters parameters;
    parameters.front_drive_torque_fraction = 0.f;
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    VehicleInput input;
    input.gear = VehicleGear::Drive;
    input.throttle = 1.f;
    vehicle.set_input(input);

    constexpr float slip_target_with_tolerance = 0.125f;
    float maximum_driven_slip = 0.f;
    double maximum_surface_speed_error = 0.0;
    VehicleState state;
    for (int step = 0; step < 180; ++step) {
        state = vehicle.update(kDt);
        for (std::size_t index : {std::size_t{2}, std::size_t{3}}) {
            const auto& wheel = state.wheels[index];
            require(wheel.in_contact,
                    "flat launch must keep each driven tire supported");
            maximum_driven_slip = std::max(
                maximum_driven_slip,
                std::abs(wheel.longitudinal_slip));
            const double surface_speed =
                static_cast<double>(wheel.angular_speed)
                * parameters.tire_radius_m;
            const double surface_speed_error = std::abs(
                surface_speed - static_cast<double>(state.speed));
            maximum_surface_speed_error = std::max(
                maximum_surface_speed_error, surface_speed_error);
            const double permitted_error = slip_target_with_tolerance
                * std::max(1.0, std::abs(static_cast<double>(state.speed)))
                + 0.02;
            require(surface_speed_error <= permitted_error,
                    "driven wheel surface speed must track road speed during"
                    " launch; wheel=" + std::to_string(index)
                    + ", surface_speed=" + std::to_string(surface_speed)
                    + ", chassis_speed=" + std::to_string(state.speed));
        }
    }

    require(state.speed > 7.f,
            "road-coupled launch must still provide useful acceleration");
    require(maximum_driven_slip <= slip_target_with_tolerance,
            "launch traction control must hold driven slip near the 0.12"
                " road-tire target; peak="
                + std::to_string(maximum_driven_slip)
                + ", peak surface-speed error="
                + std::to_string(maximum_surface_speed_error));
}

void test_full_throttle_turn_preserves_drive_and_rear_axle_grip()
{
    const auto run_turn = [](float entry_speed_mps) {
        VehicleParameters parameters;
        parameters.front_drive_torque_fraction = 0.f;
        VehiclePhysics vehicle(
            kInitialLat, kInitialLon, 0.0, 0.f, parameters);
        VehicleInput input;
        input.gear = VehicleGear::Drive;
        input.throttle = 1.f;
        vehicle.set_input(input);

        VehicleState state;
        for (int step = 0; step < 600 && state.speed < entry_speed_mps; ++step) {
            state = vehicle.update(kDt);
        }
        require(state.speed >= entry_speed_mps,
                "combined-grip setup must reach its requested entry speed");

        input.steering = 1.f;
        vehicle.set_input(input);
        constexpr float slip_target_with_tolerance = 0.125f;
        constexpr double maximum_rear_axle_slip_angle_rad =
            5.0 * std::numbers::pi_v<double> / 180.0;
        float maximum_driven_slip = 0.f;
        double maximum_rear_axle_slip_angle = 0.0;
        const double cg_to_rear_axle_m = parameters.wheelbase_m
            * parameters.front_static_load_fraction;
        for (int step = 0; step < 60; ++step) {
            state = vehicle.update(kDt);
            for (std::size_t index : {std::size_t{2}, std::size_t{3}}) {
                maximum_driven_slip = std::max(
                    maximum_driven_slip,
                    std::abs(state.wheels[index].longitudinal_slip));
            }

            // CG lateral velocity is naturally large in a tight, no-slip
            // turn. Evaluate the rear contact-patch velocity instead so this
            // test measures real tire sliding rather than rigid-body geometry.
            if (step >= 12) {
                const double rear_axle_lateral_speed =
                    state.linear_velocity_body.y
                    - cg_to_rear_axle_m * state.yaw_rate;
                const double rear_axle_slip_angle = std::abs(std::atan2(
                    rear_axle_lateral_speed,
                    std::max(1.0, std::abs(static_cast<double>(state.speed)))));
                maximum_rear_axle_slip_angle = std::max(
                    maximum_rear_axle_slip_angle, rear_axle_slip_angle);
            }
        }

        require(state.yaw_rate > 0.f,
                "positive steering under throttle must establish a left turn");
        require(maximum_driven_slip <= slip_target_with_tolerance,
                "full throttle plus steering must keep driven slip near 0.12;"
                    " entry_speed=" + std::to_string(entry_speed_mps)
                    + ", peak=" + std::to_string(maximum_driven_slip));
        require(maximum_rear_axle_slip_angle
                    <= maximum_rear_axle_slip_angle_rad,
                "full throttle plus steering must preserve realistic rear-axle"
                    " lateral grip; entry_speed="
                    + std::to_string(entry_speed_mps)
                    + ", peak slip angle rad="
                    + std::to_string(maximum_rear_axle_slip_angle));
    };

    run_turn(5.f);
    run_turn(10.f);
}

void test_rear_side_brake_unlocks_yaw_without_braking_front_wheels()
{
    const auto parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
    struct Sample {
        VehicleState state;
        double front_surface_ratio = 0.0;
        double rear_surface_ratio = 0.0;
        double rear_lateral_force = 0.0;
    };
    const auto measure = [&](bool use_side_brake) {
        VehiclePhysics vehicle(kInitialLat, kInitialLon, 0.0, 0.f, parameters);
        VehicleInput input;
        input.gear = VehicleGear::Drive;
        input.throttle = 1.f;
        vehicle.set_input(input);
        VehicleState state;
        for (int step = 0; step < 900 && state.speed < 15.f; ++step) {
            state = vehicle.update(kDt);
        }
        require(state.speed >= 15.f,
                "side-brake drift setup must reach road speed");
        input.throttle = 0.f;
        input.steering = .15f;
        vehicle.set_input(input);
        for (int step = 0; step < 12; ++step) state = vehicle.update(kDt);
        input.handbrake = use_side_brake;
        vehicle.set_input(input);
        for (int step = 0; step < 30; ++step) state = vehicle.update(kDt);

        Sample sample;
        sample.state = state;
        const double reference_speed = std::max(
            1.0, std::abs(static_cast<double>(state.speed)));
        for (std::size_t index = 0; index < state.wheels.size(); ++index) {
            const auto& wheel = state.wheels[index];
            const double surface_ratio = std::abs(
                wheel.angular_speed * parameters.tire_radius_m)
                / reference_speed;
            if (index < 2) sample.front_surface_ratio += surface_ratio * .5;
            else {
                sample.rear_surface_ratio += surface_ratio * .5;
                sample.rear_lateral_force +=
                    std::abs(static_cast<double>(wheel.lateral_force)) * .5;
            }
            const double force_magnitude = std::hypot(
                wheel.longitudinal_force, wheel.lateral_force);
            require(force_magnitude <= parameters.tire_friction
                        * wheel.normal_load * 1.01 + .01,
                    "side-brake tire force must remain inside the friction circle");
        }
        return sample;
    };

    const Sample rolling = measure(false);
    const Sample drifting = measure(true);
    require(drifting.rear_surface_ratio < .45,
            "Space must lock/slip the rear axle instead of all four wheels");
    require(drifting.front_surface_ratio > .65,
            "the front wheels must keep rolling under the rear side brake");
    require(drifting.rear_lateral_force
                < rolling.rear_lateral_force * .65,
            "a locked rear axle must surrender lateral grip for a drift");
    require(std::abs(drifting.state.linear_velocity_body.y)
                > std::abs(rolling.state.linear_velocity_body.y) + .10,
            "rear grip loss must create observable chassis sideslip");
}

void test_reverse_wheel_rotation_has_negative_sign_and_tracks_road_speed()
{
    VehicleParameters parameters;
    parameters.front_drive_torque_fraction = 0.f;
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    VehicleInput input;
    input.gear = VehicleGear::Reverse;
    input.throttle = 1.f;
    vehicle.set_input(input);

    constexpr float slip_target_with_tolerance = 0.125f;
    float maximum_driven_slip = 0.f;
    VehicleState state;
    for (int step = 0; step < 180; ++step) {
        state = vehicle.update(kDt);
        for (std::size_t index : {std::size_t{2}, std::size_t{3}}) {
            maximum_driven_slip = std::max(
                maximum_driven_slip,
                std::abs(state.wheels[index].longitudinal_slip));
        }
    }

    require(state.speed < -5.f,
            "full reverse throttle must establish useful backward road speed");
    for (const auto& wheel : state.wheels) {
        const double surface_speed =
            static_cast<double>(wheel.angular_speed)
            * parameters.tire_radius_m;
        require(wheel.in_contact,
                "flat reverse run must retain four-wheel contact");
        require(wheel.angular_speed < 0.f,
                "backward road motion must publish negative wheel angular speed");
        const double permitted_error = slip_target_with_tolerance
            * std::max(1.0, std::abs(static_cast<double>(state.speed)))
            + 0.02;
        require(std::abs(surface_speed - state.speed) <= permitted_error,
                "reverse wheel surface speed must remain coupled to backward"
                    " road speed; surface_speed="
                    + std::to_string(surface_speed)
                    + ", chassis_speed=" + std::to_string(state.speed));
    }
    require(maximum_driven_slip <= slip_target_with_tolerance,
            "reverse traction must hold driven slip near 0.12; peak="
                + std::to_string(maximum_driven_slip));
}

void test_low_speed_lateral_and_yaw_motion_settle_without_braking()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.7f;
    vehicle.set_input(input);

    VehicleState state;
    for (int step = 0; step < 300 && state.speed < 4.f; ++step) {
        state = vehicle.update(kDt);
    }
    require(state.speed >= 4.f,
            "low-speed settling setup must first establish forward motion");

    input.throttle = 0.f;
    input.steering = 1.f;
    vehicle.set_input(input);
    for (int step = 0; step < 45; ++step) {
        state = vehicle.update(kDt);
    }

    // Keep a modest brake and steering demand until the vehicle is below the
    // former lateral-force cutoff, but stop well before the brake-crossing
    // branch can force all planar state to zero. This leaves a reproducible
    // public-API-only lateral/yaw disturbance for the tire model to settle.
    input.brake = 0.08f;
    vehicle.set_input(input);
    bool reached_low_speed = false;
    for (int step = 0; step < 600; ++step) {
        state = vehicle.update(kDt);
        if (state.speed > 0.15f && state.speed <= 0.40f) {
            reached_low_speed = true;
            break;
        }
    }
    require(reached_low_speed,
            "low-speed settling setup must reach 0.15-0.40 m/s without stopping");

    const double initial_planar_residual = std::hypot(
        state.linear_velocity_body.y,
        2.7 * static_cast<double>(state.yaw_rate));
    require(initial_planar_residual > 0.05,
            "low-speed settling setup must retain measurable lateral/yaw motion");

    input.brake = 0.f;
    input.steering = 0.f;
    vehicle.set_input(input);
    for (int step = 0; step < 90; ++step) {
        state = vehicle.update(kDt);
    }

    const double settled_planar_residual = std::hypot(
        state.linear_velocity_body.y,
        2.7 * static_cast<double>(state.yaw_rate));
    require(settled_planar_residual < initial_planar_residual * 0.25
                && settled_planar_residual < 0.05,
            "low-speed tire forces must settle lateral/yaw motion without a"
            " brake-only zeroing path; initial="
                + std::to_string(initial_planar_residual)
                + ", settled=" + std::to_string(settled_planar_residual)
                + ", lateral="
                + std::to_string(state.linear_velocity_body.y)
                + ", body-yaw=" + std::to_string(state.yaw_rate)
                + ", speed=" + std::to_string(state.speed));
}

void test_service_brake_stops_without_reversing()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.throttle = 0.f;
    input.brake = 1.f;
    vehicle.set_input(input);
    for (int i = 0; i < 300; ++i) {
        const auto state = vehicle.update(kDt);
        require(state.speed >= 0.f, "service brake must not engage reverse");
    }

    require(vehicle.get_state().speed == 0.f, "service brake must stop the vehicle");
    for (const auto& wheel : vehicle.get_state().wheels) {
        require(wheel.angular_speed == 0.f,
                "stopped vehicle must not publish residual wheel rotation");
    }
}

void test_reverse_requires_reverse_gear()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.gear = VehicleGear::Reverse;
    input.throttle = 0.6f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    const auto state = vehicle.get_state();
    require(state.speed < -1.f, "reverse throttle must produce negative speed");
    require(state.north < 0.0, "reverse at heading zero must move south");
    require(state.gear == VehicleGear::Reverse, "state must expose the active gear");
}

void test_bicycle_model_turns_right()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = -0.15f;
    vehicle.set_input(input);
    advance(vehicle, 60);

    const auto before = vehicle.get_state();
    const auto state = vehicle.update(kDt);
    require(state.steering_angle < 0.f,
            "right steering must have a negative canonical wheel angle");
    require(state.yaw_rate < 0.f,
            "right steering must produce a negative canonical yaw rate");
    require(state.angular_velocity_body.z < 0.0,
            "canonical body angular Z must be negative for a right turn");
    const double pitch_radians = state.pitch
        * std::numbers::pi_v<double> / 180.0;
    const double roll_radians = state.roll
        * std::numbers::pi_v<double> / 180.0;
    const double reconstructed_navigation_yaw_rate =
        (state.angular_velocity_body.y * std::sin(roll_radians)
         + state.angular_velocity_body.z * std::cos(roll_radians))
        / std::cos(pitch_radians);
    require(std::abs(state.angular_velocity_body.z - state.yaw_rate) < 1e-6,
            "scalar yaw_rate must retain its schema-v2 FLU body-Z meaning");
    require(state.heading > 0.f && state.heading < 180.f,
            "right steering must rotate clockwise from north");
    require(state.east > 0.0, "right turn from north must move east");
    const double heading_rate = signed_heading_delta_radians(
        state.heading, before.heading) / kDt;
    require(std::abs(
                heading_rate + reconstructed_navigation_yaw_rate) < 1e-3,
            "clockwise heading rate must be the opposite reconstructed"
            " canonical navigation yaw rate");

    const double heading_rad = state.heading * std::numbers::pi_v<double> / 180.0;
    const double expected_east_velocity = state.linear_velocity_body.x * std::sin(heading_rad)
                                        - state.linear_velocity_body.y * std::cos(heading_rad);
    const double expected_north_velocity = state.linear_velocity_body.x * std::cos(heading_rad)
                                         + state.linear_velocity_body.y * std::sin(heading_rad);
    require(std::abs((state.east - before.east) / kDt - expected_east_velocity) < 1e-4,
            "Y-left body velocity must reconstruct ENU east velocity");
    require(std::abs((state.north - before.north) / kDt - expected_north_velocity) < 1e-4,
            "Y-left body velocity must reconstruct ENU north velocity");

    const double roll_rate_from_angle = (state.roll - before.roll)
        * std::numbers::pi_v<double> / 180.0 / kDt;
    require(std::abs(roll_rate_from_angle - state.angular_velocity_body.x) < 1e-3,
            "canonical roll angle and body angular X must use the same sign");
}

void test_positive_steering_turns_left_in_the_public_contract()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = 0.15f;
    vehicle.set_input(input);
    advance(vehicle, 60);

    const auto before = vehicle.get_state();
    const auto state = vehicle.update(kDt);
    require(state.steering_angle > 0.f,
            "positive canonical steering must steer left");
    require(state.yaw_rate > 0.f,
            "a forward left turn must have positive FLU body yaw");
    const double pitch_radians = state.pitch
        * std::numbers::pi_v<double> / 180.0;
    const double roll_radians = state.roll
        * std::numbers::pi_v<double> / 180.0;
    const double reconstructed_navigation_yaw_rate =
        (state.angular_velocity_body.y * std::sin(roll_radians)
         + state.angular_velocity_body.z * std::cos(roll_radians))
        / std::cos(pitch_radians);
    require(std::abs(state.angular_velocity_body.z - state.yaw_rate) < 1e-6,
            "published scalar yaw_rate must equal true FLU body angular Z");
    require(state.heading > 180.f,
            "left steering from north must reduce clockwise heading through 360 degrees");
    require(state.east < 0.0, "left turn from north must move west");

    const double heading_rate = signed_heading_delta_radians(
        state.heading, before.heading) / kDt;
    require(std::abs(
                heading_rate + reconstructed_navigation_yaw_rate) < 1e-3,
            "navigation heading must oppose reconstructed canonical Euler yaw");

    const float left_load = state.wheels[0].normal_load + state.wheels[2].normal_load;
    const float right_load = state.wheels[1].normal_load + state.wheels[3].normal_load;
    require(right_load > left_load,
            "a left turn must load the outer right wheels");
    require(state.wheels[0].steering_angle > 0.f
            && state.wheels[1].steering_angle > 0.f,
            "front wheel state must publish left steering as positive");
}

void test_left_steering_publishes_wheel_local_force_signs()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = 0.15f;
    vehicle.set_input(input);
    const auto state = vehicle.update(kDt);

    for (std::size_t index = 0; index < 2; ++index) {
        require(state.wheels[index].lateral_force > 0.f,
                "left steering must initially produce left-positive front tire force");
        require(state.wheels[index].slip_angle < 0.f,
                "left steering must publish the opposing wheel-local slip angle");
    }
}

void test_reverse_keeps_command_sign_but_reverses_vehicle_yaw_response()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.gear = VehicleGear::Reverse;
    input.throttle = 0.5f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = 0.15f;
    vehicle.set_input(input);
    const auto state = vehicle.update(kDt);

    require(state.steering_angle > 0.f,
            "left steering command must remain positive while reversing");
    require(state.yaw_rate < 0.f,
            "reverse motion must invert the yaw response to a left steering angle");
}

void test_right_turn_loads_outer_left_wheels_without_doubling_transfer()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.6f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    input.steering = -0.12f;
    vehicle.set_input(input);
    advance(vehicle, 45);

    const auto state = vehicle.get_state();
    const float left_load = state.wheels[0].normal_load + state.wheels[2].normal_load;
    const float right_load = state.wheels[1].normal_load + state.wheels[3].normal_load;
    const float total_load = left_load + right_load;
    require(state.yaw_rate < 0.f, "test setup must produce a right turn");
    require(left_load > right_load,
            "a right turn must load the outer left wheels");
    const float static_weight = 1500.f * 9.80665f;
    // The hard minimum-clearance constraint can transiently unload the inside
    // wheels in this reduced heave/attitude model. Keep this bound focused on
    // catching doubled axle transfer while allowing that safe unloading.
    require(total_load > static_weight * 0.65f
            && total_load < static_weight * 1.25f,
            "dynamic suspension load must remain bounded around vehicle weight"
                "; total=" + std::to_string(total_load)
                + ", static=" + std::to_string(static_weight));
}

void test_four_wheel_contact_and_force_limits()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.7f;
    input.steering = 0.2f;
    vehicle.set_input(input);
    advance(vehicle, 120);

    const auto state = vehicle.get_state();
    require(state.linear_velocity_body.x > 0.0,
            "body-frame longitudinal velocity must be published");
    require(state.position_enu.y > 0.0,
            "3D ENU position must track north travel");
    for (std::size_t index = 0; index < state.wheels.size(); ++index) {
        const auto& wheel = state.wheels[index];
        require(wheel.in_contact, "flat-ground wheel must remain in contact");
        require(wheel.normal_load > 0.f,
                "contact wheel must carry normal load; index="
                    + std::to_string(index)
                    + ", load=" + std::to_string(wheel.normal_load));
        require(std::isfinite(wheel.slip_angle), "wheel slip angle must be finite");
        const float force = std::hypot(wheel.longitudinal_force, wheel.lateral_force);
        require(force <= wheel.normal_load + 1e-3f,
                "tire force must remain inside the friction circle");
        if (index < 2) {
            require(wheel.steering_angle > 0.f, "front wheels must steer left");
        } else {
            require(wheel.steering_angle == 0.f, "rear wheels must not steer");
        }
    }
    require(std::abs(state.wheels[2].longitudinal_slip) > 1e-5f ||
            std::abs(state.wheels[3].longitudinal_slip) > 1e-5f,
            "driven wheels must publish longitudinal slip under throttle");
    for (const auto& wheel : state.wheels) {
        require(std::isfinite(wheel.angular_speed),
                "independent wheel angular speeds must remain finite");
    }
}

void test_wheel_speeds_are_symmetric_straight_and_differ_in_a_turn()
{
    VehicleParameters parameters;
    parameters.front_drive_torque_fraction = 0.f;
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    VehicleInput input;
    input.throttle = 0.55f;
    vehicle.set_input(input);
    advance(vehicle, 180);

    const auto straight = vehicle.get_state();
    for (const auto& [left, right] :
         {std::pair<std::size_t, std::size_t>{0, 1}, {2, 3}}) {
        const double axle_speed_scale = std::max(
            {1.0,
             std::abs(static_cast<double>(straight.wheels[left].angular_speed)),
             std::abs(static_cast<double>(straight.wheels[right].angular_speed))});
        require(std::abs(straight.wheels[left].angular_speed
                         - straight.wheels[right].angular_speed)
                    <= axle_speed_scale * 1e-4,
                "a symmetric straight run must keep left/right wheel speeds"
                    " approximately equal");
    }

    // Positive canonical steering is a left turn: left wheels are inside and
    // right wheels are outside. Independent wheel rotation must therefore be
    // free to follow the different path lengths instead of being overwritten
    // by a bit-identical axle average every tick.
    input.throttle = 0.f;
    input.steering = 0.12f;
    vehicle.set_input(input);
    double inside_rear_speed_sum = 0.0;
    double outside_rear_speed_sum = 0.0;
    double peak_rear_speed_difference = 0.0;
    int sample_count = 0;
    for (int step = 0; step < 180; ++step) {
        const auto state = vehicle.update(kDt);
        if (step < 60) {
            continue;
        }
        const double inside_speed = std::abs(
            static_cast<double>(state.wheels[2].angular_speed));
        const double outside_speed = std::abs(
            static_cast<double>(state.wheels[3].angular_speed));
        inside_rear_speed_sum += inside_speed;
        outside_rear_speed_sum += outside_speed;
        peak_rear_speed_difference = std::max(
            peak_rear_speed_difference,
            std::abs(outside_speed - inside_speed));
        ++sample_count;
    }

    const auto turning = vehicle.get_state();
    require(turning.yaw_rate > 0.f,
            "test setup must establish a canonical left turn");
    require(sample_count > 0 && peak_rear_speed_difference > 0.01,
            "turning rear wheel speeds must not be forcibly bit-identical"
                "; peak difference="
                + std::to_string(peak_rear_speed_difference));
    require(outside_rear_speed_sum > inside_rear_speed_sum,
            "the outside rear wheel must average a greater angular speed than"
                " the inside rear wheel during a sustained left turn"
                "; outside average="
                + std::to_string(outside_rear_speed_sum / sample_count)
                + ", inside average="
                + std::to_string(inside_rear_speed_sum / sample_count));
}

void test_contact_points_use_the_published_post_integration_pose()
{
    VehicleParameters parameters;
    VehiclePhysics vehicle(kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    VehicleInput input;
    input.throttle = 0.7f;
    input.steering = 0.2f;
    vehicle.set_input(input);
    advance(vehicle, 180);

    const auto state = vehicle.get_state();
    const double heading = state.heading * std::numbers::pi_v<double> / 180.0;
    const auto body_basis = make_test_body_basis(
        heading,
        state.pitch * std::numbers::pi_v<double> / 180.0,
        state.roll * std::numbers::pi_v<double> / 180.0);
    const double front_axle_offset = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double front_left_right_offset = -parameters.front_track_m * 0.5;
    const Vector3State suspension_mount{
        state.east
            + front_axle_offset * body_basis.forward[0]
            + front_left_right_offset * body_basis.right[0],
        state.north
            + front_axle_offset * body_basis.forward[1]
            + front_left_right_offset * body_basis.right[1],
        state.position_enu.z
            + front_axle_offset * body_basis.forward[2]
            + front_left_right_offset * body_basis.right[2]};
    const auto& wheel = state.wheels[0];
    const Vector3State wheel_center{
        wheel.contact_point_enu.x
            + wheel.contact_normal_enu.x * parameters.tire_radius_m,
        wheel.contact_point_enu.y
            + wheel.contact_normal_enu.y * parameters.tire_radius_m,
        wheel.contact_point_enu.z
            + wheel.contact_normal_enu.z * parameters.tire_radius_m};
    const double suspension_length =
        (suspension_mount.z - wheel_center.z) / body_basis.up[2];
    const double expected_front_left_east = suspension_mount.x
        - body_basis.up[0] * suspension_length;
    const double expected_front_left_north = suspension_mount.y
        - body_basis.up[1] * suspension_length;

    require(std::abs(wheel_center.x
                     - expected_front_left_east) < 1e-6,
            "wheel centre east coordinate must use the published chassis pose");
    require(std::abs(wheel_center.y
                     - expected_front_left_north) < 1e-6,
            "wheel centre north coordinate must use the published chassis pose");
}

void test_braking_settles_lateral_and_yaw_motion()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 0.7f;
    vehicle.set_input(input);
    advance(vehicle, 180);

    input.steering = 0.25f;
    vehicle.set_input(input);
    advance(vehicle, 90);

    input.throttle = 0.f;
    input.steering = 0.f;
    input.brake = 1.f;
    input.handbrake = true;
    vehicle.set_input(input);
    advance(vehicle, 600);

    const auto state = vehicle.get_state();
    require(state.speed == 0.f, "brakes must settle longitudinal motion");
    require(state.linear_velocity_body.y == 0.0,
            "brakes must settle residual lateral motion");
    require(std::abs(state.yaw_rate) < 1e-4f,
            "brakes and suspension must settle residual body-Z motion; yaw="
                + std::to_string(state.yaw_rate)
                + ", omega-x="
                + std::to_string(state.angular_velocity_body.x)
                + ", omega-y="
                + std::to_string(state.angular_velocity_body.y));
}

void test_driveline_does_not_wind_up_at_speed_limiter()
{
    const auto run_to_limiter = [](VehicleGear gear) {
        VehicleParameters parameters;
        VehiclePhysics vehicle(kInitialLat, kInitialLon, 0.0, 0.f, parameters);
        VehicleInput input;
        input.throttle = 1.f;
        input.gear = gear;
        vehicle.set_input(input);
        advance(vehicle, 6000);

        const auto state = vehicle.get_state();
        const float speed_limit = gear == VehicleGear::Reverse
            ? parameters.max_reverse_speed_mps
            : parameters.max_forward_speed_mps;
        require(std::abs(state.speed) <= speed_limit + 1e-4f,
                "chassis speed must respect the configured limiter");
        const float wheel_speed_limit = speed_limit / parameters.tire_radius_m;
        for (const auto& wheel : state.wheels) {
            require(std::abs(wheel.angular_speed) < wheel_speed_limit * 2.f,
                    "wheel angular speed must remain bounded at the chassis limiter");
        }
    };

    run_to_limiter(VehicleGear::Drive);
    run_to_limiter(VehicleGear::Reverse);
}

void test_authoritative_static_wall_blocks_vehicle_without_losing_ground()
{
    VehicleParameters parameters;
    const simcore_host::StaticObbCollider wall{
        "north-wall",
        simcore_host::StaticColliderSemantic::Wall,
        {
            {0.0, 5.0},
            1.0,
            std::numbers::pi_v<double> * 0.5,
            10.0,
            0.10,
            1.0,
        },
        {0.8, 0.0},
    };
    auto collision_world = std::make_shared<simcore_host::CollisionWorld>(
        std::vector<simcore_host::StaticObbCollider>{wall});
    VehiclePhysics vehicle(
        kInitialLat,
        kInitialLon,
        0.0,
        0.f,
        parameters,
        {},
        collision_world);

    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    advance(vehicle, 600);

    const auto state = vehicle.get_state();
    const double ego_half_length =
        parameters.wheelbase_m * 0.5 + 0.80;
    require(state.north <= 5.0 - wall.shape.half_width_m
            - ego_half_length + 1e-4,
            "authoritative vehicle OBB must not pass through a static wall");
    require(std::all_of(
                state.wheels.begin(), state.wheels.end(),
                [](const WheelState& wheel) { return wheel.in_contact; }),
            "static wall solve must preserve four-wheel ground contact");
    require(std::isfinite(state.speed)
            && std::isfinite(state.position_enu.x)
            && std::isfinite(state.position_enu.y)
            && std::isfinite(state.position_enu.z),
            "static collision response must keep the vehicle state finite");
    require(state.damage_percent > 0.f
            && state.last_impact_impulse_n_s > 2500.f
            && state.damage_zone == VehicleDamageZone::Front
            && state.collision_event_sequence >= 1,
            "a damaging front-wall impact must publish authoritative crash state");
    require(!state.dent_patches.empty() && state.dent_patches.front().forward > .99f
            && simcore_host::valid_vehicle_dent(state.dent_patches.front()),
            "real physics contact must publish its localized front-shell dent");
    vehicle.reset();
    const auto reset_state = vehicle.get_state();
    require(reset_state.damage_percent == 0.f
            && reset_state.last_impact_impulse_n_s == 0.f
            && reset_state.damage_zone == VehicleDamageZone::None
            && reset_state.collision_event_sequence == 0 && reset_state.dent_patches.empty(),
            "simulation reset must restore an undamaged vehicle snapshot");
}

struct SupportedCurbRun {
    bool crossed = false;
    double crossing_time_s = 0.0;
    double maximum_abs_pitch_deg = 0.0;
    bool saw_split_axle_support = false;
    std::size_t minimum_contacts = 4;
    VehicleState state;
};

SupportedCurbRun run_supported_curb(
    double dt_seconds,
    double rise_m,
    float drive_force_n,
    simcore_host::StaticColliderSemantic semantic =
        simcore_host::StaticColliderSemantic::Curb,
    bool retain_far_plateau = true,
    bool raise_opposite_far_support = false,
    double near_road_raster_undershoot_m = 0.0)
{
    VehicleParameters parameters;
    parameters.max_drive_force_n = drive_force_n;
    parameters.max_drive_power_w = 100000.f;
    parameters.drive_force_rise_rate_n_per_s = 100000.f;
    const simcore_host::StaticObbCollider obstacle{
        "cross-road-step",
        semantic,
        {{0.0, 5.0}, rise_m * 0.5,
         std::numbers::pi_v<double> * 0.5, 8.0, 0.075, rise_m * 0.5},
        {0.9, 0.0}};
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters,
        std::make_shared<RampPlateauGroundQuery>(
            rise_m, retain_far_plateau, raise_opposite_far_support,
            5.0, near_road_raster_undershoot_m),
        std::make_shared<simcore_host::CollisionWorld>(
            std::vector<simcore_host::StaticObbCollider>{obstacle}));
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);
    SupportedCurbRun result;
    const int steps = static_cast<int>(std::lround(20.0 / dt_seconds));
    for (int step = 0; step < steps; ++step) {
        result.state = vehicle.update(dt_seconds);
        result.maximum_abs_pitch_deg = std::max(
            result.maximum_abs_pitch_deg,
            std::abs(static_cast<double>(result.state.pitch)));
        result.minimum_contacts = std::min(
            result.minimum_contacts,
            static_cast<std::size_t>(std::count_if(
                result.state.wheels.begin(), result.state.wheels.end(),
                [](const WheelState& wheel) { return wheel.in_contact; })));
        const double front_height = .5 * (
            result.state.wheels[0].contact_point_enu.z
            + result.state.wheels[1].contact_point_enu.z);
        const double rear_height = .5 * (
            result.state.wheels[2].contact_point_enu.z
            + result.state.wheels[3].contact_point_enu.z);
        result.saw_split_axle_support = result.saw_split_axle_support
            || std::abs(front_height - rear_height) > rise_m * 0.5;
        if (!result.crossed && result.state.north > 6.0) {
            result.crossed = true;
            result.crossing_time_s = (step + 1) * dt_seconds;
        }
    }
    return result;
}

void test_curb_requires_authored_wheel_support_and_climbs_by_suspension()
{
    const auto slow = run_supported_curb(kDt, 0.24, 300.f);
    const VehicleParameters default_parameters;
    const double slow_front_axle_north = slow.state.north
        + default_parameters.wheelbase_m * 0.5;
    require(!slow.crossed
                && slow_front_axle_north > 5.0 - 0.25
                && slow_front_axle_north < 5.0 + 0.25
                && std::abs(slow.state.speed) < 0.1f,
            "insufficient drive energy must reach and stall on the authored curb ramp"
                "; north=" + std::to_string(slow.state.north)
                + ", front_axle_north="
                    + std::to_string(slow_front_axle_north)
                + ", speed=" + std::to_string(slow.state.speed));

    std::array<SupportedCurbRun, 3> runs{};
    const std::array<double, 3> steps{
        1.0 / 60.0, 1.0 / 120.0, 1.0 / 240.0};
    for (std::size_t index = 0; index < steps.size(); ++index) {
        runs[index] = run_supported_curb(steps[index], 0.24, 6000.f);
        require(runs[index].crossed,
                "a 0.75R curb with ramp/plateau support must be climbable");
        require(runs[index].saw_split_axle_support,
                "front and rear axle support must rise sequentially");
        require(runs[index].minimum_contacts >= 2
                && runs[index].maximum_abs_pitch_deg < 35.0,
                "curb climb must retain bounded physical support and pitch");
        require(std::abs(runs[index].state.position_enu.z - (0.55 + 0.24)) < 0.08,
                "the settled chassis must remain on the authored sidewalk plateau");
    }
    require(std::abs(runs[0].crossing_time_s - runs[1].crossing_time_s) < 0.12
            && std::abs(runs[1].crossing_time_s - runs[2].crossing_time_s) < 0.12,
            "curb crossing must remain stable from 60 through 240Hz");

    const auto tall = run_supported_curb(kDt, 0.30, 6000.f);
    require(!tall.crossed,
            "a rise above the 0.75R curb contract must remain blocked");
    const auto wall = run_supported_curb(
        kDt, 0.24, 6000.f, simcore_host::StaticColliderSemantic::Wall);
    require(!wall.crossed,
            "continuous ground support must never bypass a wall semantic");
    const auto narrow_bump = run_supported_curb(
        kDt, 0.24, 6000.f,
        simcore_host::StaticColliderSemantic::Curb, false);
    require(!narrow_bump.crossed,
            "near curb-top support without a 3R far plateau must remain blocked");
    const auto opposite_far_support = run_supported_curb(
        kDt, 0.24, 6000.f,
        simcore_host::StaticColliderSemantic::Curb, false, true);
    require(!opposite_far_support.crossed,
            "far support on the side opposite the near rise must not authorize a curb");

    const auto bounded_raster_undershoot = run_supported_curb(
        kDt, 0.24, 6000.f,
        simcore_host::StaticColliderSemantic::Curb, true, false, 0.0288);
    require(bounded_raster_undershoot.crossed,
            "a bounded near-road heightfield undershoot must not reject a verified 24 cm curb plateau");
    const auto excessive_raster_undershoot = run_supported_curb(
        kDt, 0.24, 6000.f,
        simcore_host::StaticColliderSemantic::Curb, true, false, 0.10);
    require(!excessive_raster_undershoot.crossed,
            "near-road raster tolerance must remain bounded when the sampled rise is implausible");
}

struct HighSpeedCurbResult {
    VehicleState before_crossing_tick;
    VehicleState after_crossing_tick;
    VehicleState final_state;
    double curb_north_m = 0.0;
    bool crossed = false;
    bool all_states_finite = true;
    double maximum_vertical_step_m = 0.0;
    double maximum_abs_pitch_deg = 0.0;
    std::size_t minimum_tire_contacts = 4;
};

HighSpeedCurbResult run_high_speed_curb_scenario(
    float target_speed_mps,
    double rise_m,
    simcore_host::StaticColliderSemantic semantic)
{
    VehicleParameters parameters;
    parameters.max_forward_speed_mps = target_speed_mps;
    parameters.max_drive_force_n = 12000.f;
    parameters.max_drive_power_w = 10000000.f;
    parameters.drive_force_rise_rate_n_per_s = 1000000.f;
    parameters.drive_force_fall_rate_n_per_s = 1000000.f;
    parameters.front_drive_torque_fraction = 0.5f;
    parameters.rolling_resistance_coeff = 0.f;
    parameters.drivetrain_drag_n_per_mps = 0.f;
    parameters.drag_coefficient = 0.f;

    VehicleInput input;
    input.throttle = 1.f;
    VehiclePhysics reference(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters);
    reference.set_input(input);
    int warmup_steps = 0;
    VehicleState reference_state;
    for (; warmup_steps < 2000; ++warmup_steps) {
        reference_state = reference.update(kDt);
        if (reference_state.speed >= target_speed_mps - 1e-3f) {
            ++warmup_steps;
            break;
        }
    }
    require(reference_state.speed >= target_speed_mps - 1e-3f,
            "high-speed curb fixture must reach its requested speed");

    constexpr double curb_half_width_m = 0.075;
    const double body_half_length_m = parameters.wheelbase_m * 0.5 + 0.80;
    // Start the decisive tick just outside the old current-CG candidate gate.
    // At both requested speeds, the unimpeded endpoint crosses the actual curb
    // OBB during that same 60 Hz tick.
    const double curb_north_m = reference_state.north
        + body_half_length_m + curb_half_width_m
        + parameters.tire_radius_m + 0.01;
    const simcore_host::StaticObbCollider obstacle{
        "high-speed-cross-road-step",
        semantic,
        {{0.0, curb_north_m}, rise_m * 0.5,
         std::numbers::pi_v<double> * 0.5, 8.0, curb_half_width_m,
         rise_m * 0.5},
        {0.9, 0.0}};
    VehiclePhysics vehicle(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters,
        std::make_shared<RampPlateauGroundQuery>(
            rise_m, true, false, curb_north_m),
        std::make_shared<simcore_host::CollisionWorld>(
            std::vector<simcore_host::StaticObbCollider>{obstacle}));
    vehicle.set_input(input);
    HighSpeedCurbResult result;
    result.curb_north_m = curb_north_m;
    double previous_height = vehicle.get_state().position_enu.z;
    const auto observe = [&](const VehicleState& observed) {
        result.all_states_finite = result.all_states_finite
            && std::isfinite(observed.east)
            && std::isfinite(observed.north)
            && std::isfinite(observed.position_enu.z)
            && std::isfinite(observed.speed)
            && std::isfinite(observed.pitch)
            && std::isfinite(observed.roll);
        result.maximum_vertical_step_m = std::max(
            result.maximum_vertical_step_m,
            std::abs(observed.position_enu.z - previous_height));
        previous_height = observed.position_enu.z;
        result.maximum_abs_pitch_deg = std::max(
            result.maximum_abs_pitch_deg,
            std::abs(static_cast<double>(observed.pitch)));
        result.minimum_tire_contacts = std::min(
            result.minimum_tire_contacts,
            static_cast<std::size_t>(std::count_if(
                observed.wheels.begin(), observed.wheels.end(),
                [](const WheelState& wheel) { return wheel.in_contact; })));
    };
    for (int step = 0; step < warmup_steps; ++step) {
        observe(vehicle.update(kDt));
    }

    result.before_crossing_tick = vehicle.get_state();
    require(std::abs(result.before_crossing_tick.north - reference_state.north)
                < 1e-5
                && std::abs(result.before_crossing_tick.speed
                            - reference_state.speed) < 1e-4f,
            "high-speed fixture must remain unobstructed before the engineered tick");
    result.after_crossing_tick = vehicle.update(kDt);
    observe(result.after_crossing_tick);
    result.final_state = result.after_crossing_tick;
    for (int step = 0; step < 180; ++step) {
        result.final_state = vehicle.update(kDt);
        observe(result.final_state);
        result.crossed = result.crossed
            || result.final_state.north > curb_north_m + 0.50;
    }
    return result;
}

void test_swept_curb_candidate_prevents_tunneling_and_velocity_erasure()
{
    for (const float target_speed_mps : {30.f, 50.f}) {
        const auto supported = run_high_speed_curb_scenario(
            target_speed_mps, 0.24,
            simcore_host::StaticColliderSemantic::Curb);
        require(supported.after_crossing_tick.speed
                    > target_speed_mps * 0.90f
                    && supported.after_crossing_tick.north
                        > supported.before_crossing_tick.north
                            + target_speed_mps * kDt * 0.80,
                "supported curb must not erase high-speed motion on the first swept tick");
        require(supported.crossed,
                "swept curb candidate must preserve supported motion at both stress speeds");
        std::cout << "swept-curb-stress: target_mps=" << target_speed_mps
                  << " first_tick_mps=" << supported.after_crossing_tick.speed
                  << " max_vertical_step_m="
                  << supported.maximum_vertical_step_m
                  << " max_abs_pitch_deg=" << supported.maximum_abs_pitch_deg
                  << " min_contacts=" << supported.minimum_tire_contacts
                  << " final_z_m=" << supported.final_state.position_enu.z
                  << '\n';
        // At 50 m/s the front axle traverses the authored 0.45 m ramp in a
        // single 60 Hz step.  The measured chassis response is 0.1901 m, so
        // keep a narrow deterministic ceiling below the 0.24 m curb rise: a
        // pass cannot hide a one-tick pose teleport through the step. This is
        // a deterministic swept-gate stress bound, not a ride-quality claim.
        require(supported.all_states_finite
                    && supported.maximum_vertical_step_m < 0.21
                    && supported.maximum_abs_pitch_deg < 35.0
                    && supported.minimum_tire_contacts >= 2,
                "swept-gate stress must remain finite, bounded and tire-supported");
        require(std::abs(
                    supported.final_state.position_enu.z - (0.55 + 0.24)) < 0.08,
                "swept-gate stress fixture must settle to the raised plateau height");

        const auto tall = run_high_speed_curb_scenario(
            target_speed_mps, 0.30,
            simcore_host::StaticColliderSemantic::Curb);
        require(tall.after_crossing_tick.speed < 0.1f
                    && tall.after_crossing_tick.north
                        <= tall.curb_north_m - 2.225 + 1e-3,
                "a curb above the 0.75R contract must block the same high-speed tick");

        const auto wall = run_high_speed_curb_scenario(
            target_speed_mps, 0.24,
            simcore_host::StaticColliderSemantic::Wall);
        require(wall.after_crossing_tick.speed < 0.1f
                    && wall.after_crossing_tick.north
                        <= wall.curb_north_m - 2.225 + 1e-3,
                "Wall semantic must block the same high-speed tick");

        const auto barrier = run_high_speed_curb_scenario(
            target_speed_mps, 0.24,
            simcore_host::StaticColliderSemantic::Barrier);
        require(barrier.after_crossing_tick.speed < 0.1f
                    && barrier.after_crossing_tick.north
                        <= barrier.curb_north_m - 2.225 + 1e-3,
                "Barrier semantic must block the same high-speed tick");
    }
}

void test_empty_collision_world_matches_no_collision_path_on_a_grade()
{
    constexpr double grade_degrees = 15.0;
    const double slope = std::tan(
        grade_degrees * std::numbers::pi_v<double> / 180.0);
    auto ground = std::make_shared<PlaneGroundQuery>(0.0, slope, 0.0);
    VehicleParameters parameters;
    VehiclePhysics without_world(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    VehiclePhysics with_empty_world(
        kInitialLat,
        kInitialLon,
        0.0,
        0.f,
        parameters,
        ground,
        std::make_shared<simcore_host::CollisionWorld>());
    VehicleInput input;
    input.throttle = 0.65f;
    input.steering = 0.35f;
    without_world.set_input(input);
    with_empty_world.set_input(input);

    for (int step = 0; step < 360; ++step) {
        const auto expected = without_world.update(kDt);
        const auto actual = with_empty_world.update(kDt);
        require(std::abs(actual.east - expected.east) < 1e-5
                && std::abs(actual.north - expected.north) < 1e-5
                && std::abs(actual.position_enu.z - expected.position_enu.z) < 1e-5
                && std::abs(actual.heading - expected.heading) < 1e-4f
                && std::abs(actual.speed - expected.speed) < 1e-4f,
                "an empty CollisionWorld must preserve graded steering motion");
    }
}

void test_glancing_wall_collision_preserves_the_fixed_enu_grade()
{
    constexpr double grade_degrees = 20.0;
    const double slope = std::tan(
        grade_degrees * std::numbers::pi_v<double> / 180.0);
    auto ground = std::make_shared<PlaneGroundQuery>(0.0, slope, 0.0);
    VehicleParameters parameters;
    const simcore_host::StaticObbCollider angled_wall{
        "angled-grade-wall",
        simcore_host::StaticColliderSemantic::Wall,
        {
            {0.0, 5.0},
            slope * 5.0 + 1.0,
            std::numbers::pi_v<double> * 0.25,
            50.0,
            0.10,
            2.0,
        },
        {0.0, 0.0},
    };
    auto collision_world = std::make_shared<simcore_host::CollisionWorld>(
        std::vector<simcore_host::StaticObbCollider>{angled_wall});
    VehiclePhysics blocked(
        kInitialLat,
        kInitialLon,
        0.0,
        0.f,
        parameters,
        ground,
        collision_world);
    VehiclePhysics unobstructed(
        kInitialLat, kInitialLon, 0.0, 0.f, parameters, ground);
    VehicleInput input;
    input.brake = 1.f;
    input.handbrake = true;
    blocked.set_input(input);
    unobstructed.set_input(input);

    advance(blocked, 180);
    advance(unobstructed, 180);
    input.brake = 0.f;
    input.handbrake = false;
    input.throttle = 1.f;
    blocked.set_input(input);
    unobstructed.set_input(input);

    const double initial_plane_relative_height =
        blocked.get_state().position_enu.z
        - slope * blocked.get_state().north;
    double maximum_height_error = 0.0;
    for (int step = 0; step < 600; ++step) {
        const auto state = blocked.update(kDt);
        (void)unobstructed.update(kDt);
        maximum_height_error = std::max(
            maximum_height_error,
            std::abs(
                state.position_enu.z - slope * state.north
                - initial_plane_relative_height));
        const auto contact_count = std::count_if(
            state.wheels.begin(), state.wheels.end(),
            [](const WheelState& wheel) { return wheel.in_contact; });
        require(contact_count == 4,
                "glancing collision on a grade must retain four-wheel contact"
                "; step=" + std::to_string(step)
                + " contacts=" + std::to_string(contact_count)
                + " north=" + std::to_string(state.north)
                + " east=" + std::to_string(state.east)
                + " speed=" + std::to_string(state.speed));
        for (const auto& wheel : state.wheels) {
            require(std::abs(
                        wheel.contact_point_enu.z
                        - slope * wheel.contact_point_enu.y) < 1e-6,
                    "collision yaw must not rotate the authored ENU ground plane");
        }
    }

    const auto blocked_state = blocked.get_state();
    const auto clear_state = unobstructed.get_state();
    require(clear_state.north > blocked_state.north + 1.0,
            "angled static wall must materially block the uphill trajectory");
    require(maximum_height_error < 0.75,
            "collision projection must keep chassis height tied to the grade");
}

void test_tracked_landscape_launch_keeps_body_attitude_bounded()
{
    // landscape_local_v1 is a live editor-baked artifact whose horizontal
    // extent intentionally changes when the author fits the exporter to the
    // Landscape. Keep this regression invariant to that size: the deterministic
    // finite-edge stop contract is covered separately by
    // test_baked_surface_boundary_stops_at_last_supported_pose().
    const auto loaded_parameters = simcore_host::load_vehicle_parameters(
        SIMCORE_TEST_VEHICLE_CONFIG_PATH);
    const auto map_package = simcore_host::load_runtime_map_package(
        SIMCORE_TEST_LANDSCAPE_MAP_PACKAGE_PATH);
    const auto ground = map_package.ground_query;
    VehiclePhysics vehicle(
        kInitialLat,
        kInitialLon,
        0.0,
        0.f,
        loaded_parameters.parameters,
        ground);
    VehicleInput input;
    input.throttle = 1.f;
    input.gear = VehicleGear::Drive;
    vehicle.set_input(input);

    const auto initial_state = vehicle.get_state();
    double maximum_absolute_pitch_degrees = 0.0;
    double maximum_absolute_roll_degrees = 0.0;
    double minimum_centre_clearance_m = std::numeric_limits<double>::infinity();
    double maximum_planar_displacement_m = 0.0;
    double maximum_absolute_speed_mps = 0.0;
    std::size_t minimum_contact_count = initial_state.wheels.size();
    for (int step = 0; step < 600; ++step) {
        const auto state = vehicle.update(kDt);
        require(std::isfinite(state.position_enu.z)
                && std::isfinite(state.pitch) && std::isfinite(state.roll),
                "tracked Landscape launch must keep body state finite");
        maximum_absolute_pitch_degrees = std::max(
            maximum_absolute_pitch_degrees,
            std::abs(static_cast<double>(state.pitch)));
        maximum_absolute_roll_degrees = std::max(
            maximum_absolute_roll_degrees,
            std::abs(static_cast<double>(state.roll)));
        maximum_planar_displacement_m = std::max(
            maximum_planar_displacement_m,
            std::hypot(
                state.east - initial_state.east,
                state.north - initial_state.north));
        maximum_absolute_speed_mps = std::max(
            maximum_absolute_speed_mps,
            std::abs(static_cast<double>(state.speed)));
        minimum_contact_count = std::min(
            minimum_contact_count,
            static_cast<std::size_t>(std::count_if(
                state.wheels.begin(), state.wheels.end(),
                [](const WheelState& wheel) { return wheel.in_contact; })));
        const auto centre_hit = ground->query_down({
            {state.east, state.north, state.position_enu.z + 20.0}, 40.0});
        require(centre_hit.has_value(),
                "every published tracked-Landscape pose must retain centre coverage"
                    "; north=" + std::to_string(state.north)
                    + ", east=" + std::to_string(state.east));
        minimum_centre_clearance_m = std::min(
            minimum_centre_clearance_m,
            state.position_enu.z - centre_hit->point_enu.up_m);
    }

    require(maximum_absolute_pitch_degrees < 45.0,
            "tracked Landscape launch must not tip the chassis nose-down; max pitch="
                + std::to_string(maximum_absolute_pitch_degrees));
    require(maximum_absolute_roll_degrees < 45.0,
            "tracked Landscape launch must not roll the chassis over; max roll="
                + std::to_string(maximum_absolute_roll_degrees));
    require(minimum_centre_clearance_m > 0.1,
            "tracked Landscape launch must keep the chassis reference above"
                " authored centre ground; min clearance="
                + std::to_string(minimum_centre_clearance_m));
    require(maximum_planar_displacement_m > 5.0
            && maximum_absolute_speed_mps > 0.5,
            "tracked Landscape full-throttle smoke must actually drive, not"
                " pass while fail-closed at spawn; displacement="
                + std::to_string(maximum_planar_displacement_m)
                + ", max speed=" + std::to_string(maximum_absolute_speed_mps));
    require(minimum_contact_count >= 2,
            "tracked Landscape drive must retain a solvable tire footprint"
                "; minimum contacts=" + std::to_string(minimum_contact_count));
}

} // namespace

void test_runtime_fallen_obstacles_support_tires_without_becoming_walls()
{
    using namespace simcore_host;
    const auto ground=std::make_shared<FlatGroundQuery>();
    const auto world=std::make_shared<CollisionWorld>();
    KinematicCollisionProxy pole;
    pole.proxy_id="fallen-pole";
    pole.shape=ObbPrism{{0,25},.10,std::numbers::pi*.5,2.8,.10,.10};
    pole.tire_support_candidate=true;
    require(is_low_tire_obstacle(pole,*ground,.32),"settled low pole must qualify for round tire support");
    auto upright=pole; upright.tire_support_candidate=false;
    require(!is_low_tire_obstacle(upright,*ground,.32),"unmarked actor must never bypass chassis collision");
    auto airborne=pole; std::get<ObbPrism>(airborne.shape).center_up_m+=.3;
    require(!is_low_tire_obstacle(airborne,*ground,.32),"airborne prop is not ground support");
    auto high=pole; auto& high_shape=std::get<ObbPrism>(high.shape);
    high_shape.center_up_m=.8; high_shape.half_height_m=.8;
    require(!is_low_tire_obstacle(high,*ground,.32),"oversize object retains blocking collision");
    GroundQueryRequest request{{0,25,4},10};
    require(!runtime_tire_contact(request,{},.32,{pole}),"prop must not fabricate missing map coverage");
    const auto top=runtime_tire_contact(request,ground->query_down(request),.32,{pole});
    require(top && std::abs(top->point_enu.up_m-.2)<.001 && top->normal_enu.up_m>.999,
        "wheel at rounded pole crown rests on actual top, not ground");

    for (const bool person : {false,true}) {
        auto obstacle=pole;
        if(person) {
            obstacle.proxy_id="downed-person";
            obstacle.shape=ObbPrism{{0,25},.25,std::numbers::pi*.5,.95,.35,.25};
        }
        require(is_low_tire_obstacle(obstacle,*ground,.32),"settled rounded body within tire-scale bound qualifies");
        VehiclePhysics vehicle(kInitialLat,kInitialLon,0.0,0.f,{},ground,world);
        VehicleInput input; input.throttle=1.f; vehicle.set_input(input);
        bool front_raised=false,rear_raised=false,passed=false;
        double max_pitch=0.0,min_crossing_speed=100.0;
        for(int step=0;step<720;++step) {
            const auto state=vehicle.update(kDt,{obstacle});
            require(std::isfinite(state.speed) && state.position_enu.z>.2,
                "fallen object must not cause invalid or underground pose");
            max_pitch=std::max(max_pitch,std::abs(static_cast<double>(state.pitch)));
            front_raised|=state.wheels[0].contact_point_enu.z>.06 || state.wheels[1].contact_point_enu.z>.06;
            rear_raised|=state.wheels[2].contact_point_enu.z>.06 || state.wheels[3].contact_point_enu.z>.06;
            if(state.north>22 && state.north<28) min_crossing_speed=std::min(min_crossing_speed,static_cast<double>(state.speed));
            if(state.north>30) {passed=true;break;}
        }
        require(passed && front_raised && rear_raised && min_crossing_speed>1.0 && max_pitch<60.0,
            std::string(person?"fallen person":"fallen pole")+" must roll under sequential axles, not erase velocity"
            +" passed="+std::to_string(passed)+" front="+std::to_string(front_raised)
            +" rear="+std::to_string(rear_raised)+" speed="+std::to_string(min_crossing_speed)
            +" pitch="+std::to_string(max_pitch));
        std::cout << "fallen-tire-support: kind=" << (person?"person":"pole")
            << " min_crossing_mps=" << min_crossing_speed << " max_pitch_deg=" << max_pitch << '\n';
        vehicle.reset();
        const auto reset=vehicle.update(kDt);
        for(const auto& wheel:reset.wheels) require(std::abs(wheel.contact_point_enu.z)<.001,
            "reset/new snapshot must discard all runtime tire support");
    }
    VehiclePhysics blocked(kInitialLat,kInitialLon,0.0,0.f,{},ground,world);
    VehicleInput input; input.throttle=1.f; blocked.set_input(input);
    for(int step=0;step<720;++step) blocked.update(kDt,{high});
    require(blocked.get_state().north<23.0 && std::abs(blocked.get_state().speed)<.2,
        "tall obstacles must still block; no global dynamic-collision bypass");
    auto straddled=pole;
    straddled.shape=ObbPrism{{0,25},.25,0,.95,.35,.25};
    VehiclePhysics belly_blocked(kInitialLat,kInitialLon,0.0,0.f,{},ground,world);
    belly_blocked.set_input(input);
    for(int step=0;step<720;++step) belly_blocked.update(kDt,{straddled});
    require(belly_blocked.get_state().north<24 && std::abs(belly_blocked.get_state().speed)<.2,
        "torso above belly clearance between both wheel tracks cannot disappear through chassis");
}

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "--steering-sweep") {
            // Offline calibration only: pedal control holds speed; no pose,
            // velocity, grip or yaw override is used to obtain these radii.
            const auto parameters = simcore_host::load_vehicle_parameters(
                SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
            for (const float fraction : {.04f, .15f, 1.f}) {
                for (const float kph : {10.8f, 18.f, 30.f, 40.f, 50.f, 90.f}) {
                    const auto sample = measure_constant_speed_turn(
                        parameters, kph / 3.6f, fraction);
                    // Report both requested and achieved speed: full-lock
                    // cornering drag can exceed available sustaining power.
                    print_turn_sample(sample, kph / 3.6f, fraction);
                }
            }
            return 0;
        }
        require(argc == 1, "usage: vehicle_physics_tests [--steering-sweep]");
        test_initial_state_is_a_complete_four_wheel_snapshot();
        test_suspension_model_clamps_stroke_and_force();
        test_invalid_suspension_parameters_are_rejected();
        test_ground_query_publishes_sloped_hit_and_normal();
        test_sloped_contact_patch_reconstructs_authoritative_wheel_center();
        test_raised_front_and_left_supports_set_canonical_attitude_signs();
        test_wheel_support_attitude_exceeds_legacy_absolute_limits_smoothly();
        test_stationary_reset_preserves_four_wheel_geometry_on_steep_planes();
        test_chassis_attitude_follows_uneven_four_wheel_support();
        test_invalid_ground_normal_is_rejected();
        test_no_ground_removes_all_tire_forces();
        test_partial_ground_only_loads_hit_wheels();
        test_airborne_throttle_does_not_store_hidden_wheel_spin();
        test_deep_one_side_hard_stop_recovery_uses_a_bounded_attitude_step();
        test_single_corner_hard_stop_does_not_lock_travel_at_other_corners();
        test_adjacent_wheels_use_their_own_contact_tangent_for_slip();
        test_single_supported_drive_wheel_does_not_spin_either_half_shaft();
        test_baked_surface_boundary_stops_at_last_supported_pose();
        test_vehicle_suspension_force_respects_configured_bound();
        test_vehicle_follows_an_uphill_ground_plane();
        test_stationary_20_degree_grade_has_physical_axle_loads();
        test_default_rwd_vehicle_climbs_a_20_degree_grade();
        test_vehicle_recovers_contact_on_a_fast_rising_compound_grade();
        test_low_speed_40_degree_transition_never_enters_the_surface();
        test_stationary_vehicle_stays_above_25_to_40_degree_grades();
        test_handbrake_holds_at_low_speed_on_a_slope();
        test_surface_material_multiplier_limits_grade_friction();
        test_cross_slope_sets_canonical_roll_sign();
        test_idle_and_handbrake_are_stable();
        test_reset_restores_the_configured_spawn_snapshot();
        test_throttle_accelerates_and_input_is_clamped();
        test_steering_rack_is_speed_and_gear_independent();
        test_steering_clamp_and_boundary_stop_preserve_ackermann();
        test_fixed_angle_turns_are_measured_from_tire_driven_paths();
        test_rotating_body_frame_does_not_create_force_free_energy();
        test_high_speed_full_lock_saturates_tires_not_steering();
        test_steady_flat_corner_preserves_total_normal_support();
        test_sedan_small_signal_turning_balance();
        test_compression_stop_support_uses_actual_impulse_and_clears_on_reset();
        test_city_steering_keeps_authored_low_friction_limits();
        test_drive_coastdown_has_more_drag_than_neutral();
        test_full_throttle_launch_avoids_sustained_rear_wheelspin();
        test_flat_ground_launch_keeps_driven_wheels_road_coupled();
        test_full_throttle_turn_preserves_drive_and_rear_axle_grip();
        test_rear_side_brake_unlocks_yaw_without_braking_front_wheels();
        test_reverse_wheel_rotation_has_negative_sign_and_tracks_road_speed();
        test_low_speed_lateral_and_yaw_motion_settle_without_braking();
        test_wheel_speeds_are_symmetric_straight_and_differ_in_a_turn();
        test_service_brake_stops_without_reversing();
        test_reverse_requires_reverse_gear();
        test_bicycle_model_turns_right();
        test_positive_steering_turns_left_in_the_public_contract();
        test_left_steering_publishes_wheel_local_force_signs();
        test_reverse_keeps_command_sign_but_reverses_vehicle_yaw_response();
        test_right_turn_loads_outer_left_wheels_without_doubling_transfer();
        test_four_wheel_contact_and_force_limits();
        test_contact_points_use_the_published_post_integration_pose();
        test_braking_settles_lateral_and_yaw_motion();
        test_driveline_does_not_wind_up_at_speed_limiter();
        test_authoritative_static_wall_blocks_vehicle_without_losing_ground();
        test_curb_requires_authored_wheel_support_and_climbs_by_suspension();
        test_runtime_fallen_obstacles_support_tires_without_becoming_walls();
        test_swept_curb_candidate_prevents_tunneling_and_velocity_erasure();
        test_empty_collision_world_matches_no_collision_path_on_a_grade();
        test_glancing_wall_collision_preserves_the_fixed_enu_grade();
        test_tracked_landscape_launch_keeps_body_attitude_bounded();
        std::cout << "vehicle_physics_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vehicle_physics_tests: " << error.what() << '\n';
        return 1;
    }
}
