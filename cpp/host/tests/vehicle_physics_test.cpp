#include "physics/vehicle_physics.hpp"

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
    PlaneGroundQuery(double east_slope, double north_slope, double intercept)
        : east_slope_(east_slope)
        , north_slope_(north_slope)
        , intercept_(intercept)
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
            distance};
    }

private:
    double east_slope_ = 0.0;
    double north_slope_ = 0.0;
    double intercept_ = 0.0;
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

    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (!enabled_
            || (west_half_only_ && request.origin_enu.east_m >= 0.0)) {
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
    bool enabled_ = false;
    bool west_half_only_ = false;
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

    const double held_north = stopped.north;
    const double held_up = stopped.position_enu.z;
    advance(vehicle, 300);
    const auto held = vehicle.get_state();
    require(std::abs(held.north - held_north) < 1e-6
            && std::abs(held.position_enu.z - held_up) < 0.01,
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

void test_steering_response_is_rate_limited_and_speed_aware()
{
    auto vehicle = make_vehicle();
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);

    VehicleState state;
    for (int step = 0; step < 600 && state.speed < 13.5f; ++step) {
        state = vehicle.update(kDt);
    }
    require(state.speed >= 13.5f,
            "steering response setup must reach at least 13.5 m/s");

    input.throttle = 0.f;
    input.steering = 1.f;
    vehicle.set_input(input);
    const float steering_before_step = state.steering_angle;
    state = vehicle.update(kDt);

    constexpr float maximum_one_tick_steering_change_rad = 0.05f;
    require(state.steering_angle > steering_before_step,
            "a positive steering step must begin moving the road wheels left");
    require(state.steering_angle - steering_before_step
                <= maximum_one_tick_steering_change_rad,
            "road-wheel steering must be rate limited; one-tick change="
                + std::to_string(state.steering_angle - steering_before_step));

    // A keyboard command is an instantaneous full-scale step. At road speed it
    // must represent a bounded lateral-acceleration request rather than parking
    // lock, otherwise the requested turn exceeds the tire friction circle and
    // produces a guaranteed slide. Ten degrees is intentionally looser than
    // the tuned sedan's roughly four-degree limit at 50 km/h.
    constexpr float maximum_high_speed_steering_angle_rad = 0.18f;
    for (int step = 1; step < 60; ++step) {
        state = vehicle.update(kDt);
        if (state.speed >= 10.f) {
            require(std::abs(state.steering_angle)
                        <= maximum_high_speed_steering_angle_rad,
                    "full keyboard steering must not apply parking-lock angle"
                    " at road speed; angle="
                        + std::to_string(state.steering_angle)
                        + ", speed=" + std::to_string(state.speed));
        }
    }
    require(state.speed >= 10.f,
            "steering limit setup must remain at road speed");
    require(state.steering_angle > 0.03f
                && state.steering_angle <= maximum_high_speed_steering_angle_rad,
            "speed-aware steering must retain a useful, bounded left angle");

    const float steering_before_release = state.steering_angle;
    input.steering = 0.f;
    vehicle.set_input(input);
    state = vehicle.update(kDt);
    require(state.steering_angle >= 0.f
                && state.steering_angle < steering_before_release,
            "released steering must start returning without changing sign");
    require(steering_before_release - state.steering_angle
                <= maximum_one_tick_steering_change_rad,
            "steering return must also be rate limited");

    float previous_angle = state.steering_angle;
    for (int step = 0; step < 60; ++step) {
        state = vehicle.update(kDt);
        require(state.steering_angle >= -1e-5f
                    && state.steering_angle <= previous_angle + 1e-5f,
                "released steering must return monotonically without overshoot");
        previous_angle = state.steering_angle;
    }
    require(std::abs(state.steering_angle) < 1e-3f,
            "released steering must settle at center within one second");
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

} // namespace

int main()
{
    try {
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
        test_cross_slope_sets_canonical_roll_sign();
        test_idle_and_handbrake_are_stable();
        test_reset_restores_the_configured_spawn_snapshot();
        test_throttle_accelerates_and_input_is_clamped();
        test_steering_response_is_rate_limited_and_speed_aware();
        test_drive_coastdown_has_more_drag_than_neutral();
        test_full_throttle_launch_avoids_sustained_rear_wheelspin();
        test_flat_ground_launch_keeps_driven_wheels_road_coupled();
        test_full_throttle_turn_preserves_drive_and_rear_axle_grip();
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
        test_empty_collision_world_matches_no_collision_path_on_a_grade();
        test_glancing_wall_collision_preserves_the_fixed_enu_grade();
        std::cout << "vehicle_physics_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vehicle_physics_tests: " << error.what() << '\n';
        return 1;
    }
}
