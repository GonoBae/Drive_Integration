#include "coordinates/body_frame_adapter.hpp"
#include "coordinates/geo_transform.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

constexpr double kVectorTolerance = 1.0e-11;
constexpr double kAngleToleranceRadians = 2.0e-10;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_vector_near(
    const simcore_host::CoordinateVector3& actual,
    const simcore_host::CoordinateVector3& expected,
    double tolerance,
    const std::string& message)
{
    require(
        std::abs(actual.x - expected.x) <= tolerance
            && std::abs(actual.y - expected.y) <= tolerance
            && std::abs(actual.z - expected.z) <= tolerance,
        message + ": actual=(" + std::to_string(actual.x) + ","
            + std::to_string(actual.y) + "," + std::to_string(actual.z)
            + ") expected=(" + std::to_string(expected.x) + ","
            + std::to_string(expected.y) + "," + std::to_string(expected.z)
            + ")");
}

double wrapped_angle_error(double actual, double expected)
{
    return std::abs(std::remainder(
        actual - expected, 2.0 * std::numbers::pi_v<double>));
}

void test_left_positive_steering_crosses_the_solver_boundary_once()
{
    constexpr float canonical_left = 0.4f;
    constexpr float solver_right =
        simcore_host::BodyFrameAdapter::canonical_steering_to_solver(canonical_left);

    static_assert(solver_right == -0.4f);
    static_assert(
        simcore_host::BodyFrameAdapter::solver_steering_to_canonical(solver_right)
        == canonical_left);
}

void test_solver_lateral_and_yaw_values_publish_as_flu()
{
    constexpr float solver_right_speed = 2.f;
    constexpr float solver_clockwise_yaw = 0.3f;

    static_assert(
        simcore_host::BodyFrameAdapter::solver_lateral_to_canonical(solver_right_speed)
        == -2.f);
    static_assert(
        simcore_host::BodyFrameAdapter::solver_heading_rate_to_canonical_yaw_rate(
            solver_clockwise_yaw)
        == -0.3f);
    static_assert(
        simcore_host::BodyFrameAdapter::canonical_yaw_rate_to_heading_rate(-0.3f)
        == solver_clockwise_yaw);
}

void test_map_enu_unreal_polar_and_axial_golden_values()
{
    using Adapter = simcore_host::GeoTransformAdapter;
    using Vector = simcore_host::CoordinateVector3;

    static_assert(Adapter::map_enu_polar_to_unreal_world({1.0, 0.0, 0.0}).x == 0.0);
    static_assert(Adapter::map_enu_polar_to_unreal_world({1.0, 0.0, 0.0}).y == 1.0);
    static_assert(Adapter::map_enu_polar_to_unreal_world({0.0, 1.0, 0.0}).x == 1.0);
    static_assert(Adapter::base_link_polar_to_unreal_actor({1.0, 2.0, 3.0}).y == -2.0);
    static_assert(
        Adapter::base_link_axial_angular_velocity_to_unreal_actor(
            {1.0, 2.0, 3.0}).z == -3.0);

    const Vector origin_cm{1000.0, -250.0, 50.0};
    const Vector position_enu_m{12.345, -6.789, 1.25};
    const Vector position_unreal_cm =
        Adapter::map_enu_position_m_to_unreal_world_cm(position_enu_m, origin_cm);
    require_vector_near(
        position_unreal_cm,
        {321.1, 984.5, 175.0},
        kVectorTolerance,
        "map_enu position must use UE X=North, Y=East, Z=Up plus origin");
    require_vector_near(
        Adapter::unreal_world_position_cm_to_map_enu_m(
            position_unreal_cm, origin_cm),
        position_enu_m,
        kVectorTolerance,
        "position round-trip must be much tighter than five centimetres");

    const Vector velocity_enu_mps{2.0, -3.0, 0.5};
    require_vector_near(
        Adapter::map_enu_velocity_mps_to_unreal_world_cmps(velocity_enu_mps),
        {-300.0, 200.0, 50.0},
        kVectorTolerance,
        "ENU velocity must be a scaled polar vector without origin");
    require_vector_near(
        Adapter::unreal_world_velocity_cmps_to_map_enu_mps(
            {-300.0, 200.0, 50.0}),
        velocity_enu_mps,
        kVectorTolerance,
        "velocity round-trip must preserve SI values");

    const Vector acceleration_enu_mps2{-1.25, 4.5, -9.81};
    const Vector acceleration_unreal =
        Adapter::map_enu_acceleration_mps2_to_unreal_world_cmps2(
            acceleration_enu_mps2);
    require_vector_near(
        acceleration_unreal,
        {450.0, -125.0, -981.0},
        kVectorTolerance,
        "ENU acceleration must follow the same polar basis and SI scale");
    require_vector_near(
        Adapter::unreal_world_acceleration_cmps2_to_map_enu_mps2(
            acceleration_unreal),
        acceleration_enu_mps2,
        kVectorTolerance,
        "acceleration round-trip must preserve SI values");

    const Vector angular_velocity_enu{1.0, 2.0, 3.0};
    const Vector angular_velocity_unreal =
        Adapter::map_enu_axial_angular_velocity_to_unreal_world(
            angular_velocity_enu);
    require_vector_near(
        angular_velocity_unreal,
        {-2.0, -1.0, -3.0},
        0.0,
        "axial angular velocity must include det(A), unlike a polar vector");
    require_vector_near(
        Adapter::unreal_world_axial_angular_velocity_to_map_enu(
            angular_velocity_unreal),
        angular_velocity_enu,
        0.0,
        "world axial-vector basis map must be involutory");
    require_vector_near(
        Adapter::base_link_axial_angular_velocity_to_unreal_actor(
            {1.0, 2.0, 3.0}),
        {-1.0, 2.0, -3.0},
        0.0,
        "FLU to Actor axial vector must use det(B)B");
}

void test_navigation_heading_and_quaternion_golden_values()
{
    using Adapter = simcore_host::GeoTransformAdapter;
    using Attitude = simcore_host::CanonicalAttitudeRadians;
    using Quaternion = simcore_host::CoordinateQuaternion;
    using Vector = simcore_host::CoordinateVector3;

    constexpr double half_pi = std::numbers::pi_v<double> * 0.5;
    require(
        std::abs(Adapter::navigation_heading_to_enu_yaw(0.0) - half_pi)
            <= 1.0e-15,
        "North heading must be +90 degrees ENU yaw");
    require(
        std::abs(Adapter::navigation_heading_to_enu_yaw(half_pi)) <= 1.0e-15,
        "East heading must be zero ENU yaw");
    require(
        wrapped_angle_error(
            Adapter::enu_yaw_to_navigation_heading(
                Adapter::navigation_heading_to_enu_yaw(5.7)),
            5.7) <= 1.0e-15,
        "navigation heading and ENU yaw must round-trip");

    const double root_half = std::sqrt(0.5);
    const Quaternion north_map =
        Adapter::canonical_attitude_to_map_enu_from_base_link({0.0, 0.0, 0.0});
    require(
        std::abs(north_map.x) <= 1.0e-15
            && std::abs(north_map.y) <= 1.0e-15
            && std::abs(north_map.z - root_half) <= 1.0e-15
            && std::abs(north_map.w - root_half) <= 1.0e-15,
        "level North canonical quaternion must be a +90 degree ENU Z rotation");
    const Quaternion north_unreal =
        Adapter::map_enu_from_base_link_to_unreal_world_from_actor(north_map);
    require(
        Adapter::quaternion_angular_distance(north_unreal, {})
            <= kAngleToleranceRadians,
        "level North must be Unreal Actor identity");

    const Quaternion east_map =
        Adapter::canonical_attitude_to_map_enu_from_base_link(
            {half_pi, 0.0, 0.0});
    require(
        Adapter::quaternion_angular_distance(east_map, {})
            <= kAngleToleranceRadians,
        "level East must be canonical map quaternion identity");
    const Quaternion east_unreal =
        Adapter::map_enu_from_base_link_to_unreal_world_from_actor(east_map);
    require(
        Adapter::quaternion_angular_distance(
            east_unreal, {0.0, 0.0, root_half, root_half})
            <= kAngleToleranceRadians,
        "level East must be +90 degree Unreal yaw");

    const Quaternion pitched_map =
        Adapter::canonical_attitude_to_map_enu_from_base_link(
            {0.0, std::numbers::pi_v<double> / 6.0, 0.0});
    const Vector forward_enu = Adapter::rotate_polar_vector(
        pitched_map, {1.0, 0.0, 0.0});
    require_vector_near(
        forward_enu,
        {0.0, std::sqrt(0.75), 0.5},
        kVectorTolerance,
        "positive canonical pitch must point a North-facing nose upward");
    const Quaternion pitched_unreal =
        Adapter::map_enu_from_base_link_to_unreal_world_from_actor(pitched_map);
    require_vector_near(
        Adapter::rotate_polar_vector(pitched_unreal, {1.0, 0.0, 0.0}),
        {std::sqrt(0.75), 0.0, 0.5},
        kVectorTolerance,
        "basis-changed quaternion must preserve positive Unreal nose-up pitch");

    const std::array<Attitude, 7> attitudes{{
        {0.0, 0.0, 0.0},
        {half_pi, 0.0, 0.0},
        {std::numbers::pi_v<double>, 0.0, 0.0},
        {3.0 * half_pi, 0.0, 0.0},
        {0.37, 0.21, -0.14},
        {4.91, -0.72, 0.83},
        {6.27, 1.0, -1.2},
    }};
    for (const Attitude& expected : attitudes) {
        const Quaternion map =
            Adapter::canonical_attitude_to_map_enu_from_base_link(expected);
        const Attitude decoded =
            Adapter::map_enu_from_base_link_to_canonical_attitude(map);
        require(
            wrapped_angle_error(decoded.heading_clockwise, expected.heading_clockwise)
                    <= kAngleToleranceRadians
                && std::abs(decoded.pitch_nose_up - expected.pitch_nose_up)
                    <= kAngleToleranceRadians
                && wrapped_angle_error(decoded.roll_left_up, expected.roll_left_up)
                    <= kAngleToleranceRadians,
            "canonical heading/pitch/roll quaternion round-trip exceeded tolerance");

        const Quaternion unreal =
            Adapter::map_enu_from_base_link_to_unreal_world_from_actor(map);
        const Quaternion restored =
            Adapter::unreal_world_from_actor_to_map_enu_from_base_link(unreal);
        require(
            Adapter::quaternion_angular_distance(map, restored)
                <= kAngleToleranceRadians,
            "map_enu/FLU to Unreal/Actor quaternion basis round-trip exceeded tolerance");

        const Quaternion negated{-map.x, -map.y, -map.z, -map.w};
        require(
            Adapter::quaternion_angular_distance(map, negated)
                <= kAngleToleranceRadians,
            "q and -q must remain the same orientation");
    }
}

} // namespace

int main()
{
    try {
        test_left_positive_steering_crosses_the_solver_boundary_once();
        test_solver_lateral_and_yaw_values_publish_as_flu();
        test_map_enu_unreal_polar_and_axial_golden_values();
        test_navigation_heading_and_quaternion_golden_values();
        std::cout << "body_frame_adapter_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "body_frame_adapter_tests: " << error.what() << '\n';
        return 1;
    }
}
