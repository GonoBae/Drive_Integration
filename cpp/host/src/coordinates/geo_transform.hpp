#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace simcore_host {

// Numeric coordinate primitives used only at frame boundaries. Quaternion
// components are scalar-last (x, y, z, w), matching Unreal's FQuat constructor
// and the convention reserved for any future orientation serialization.
struct CoordinateVector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct CoordinateQuaternion {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

struct CanonicalAttitudeRadians {
    // Navigation heading: North=0, clockwise-positive.
    double heading_clockwise = 0.0;
    // Existing schema-v2 Euler display convention: nose-up and left-up are
    // positive. This is Rz(ENU yaw) * Ry(-pitch) * Rx(roll) in RH FLU math.
    double pitch_nose_up = 0.0;
    double roll_left_up = 0.0;
};

// Prototype GeoTransform contract shared with SimCoreCoordinateFrames in UE.
//
// map_enu is RH (East, North, Up), in metres. The current Unreal world is LH
// (X=North, Y=East, Z=Up), in centimetres. A is the polar-vector basis map:
//
//     A = [0 1 0; 1 0 0; 0 0 1], det(A) = -1
//
// Positions include the presentation origin. Displacements, velocity, and
// acceleration use A and the relevant metre-to-centimetre scale but no origin.
// Axial angular velocity uses det(A)A = -A and remains in rad/s.
//
// base_link is RH FLU while Unreal Actor local coordinates are FRU. Their polar
// basis map is B=diag(1,-1,1), and axial vectors use det(B)B.
class GeoTransformAdapter final {
public:
    static constexpr double kMetersToCentimeters = 100.0;

    static constexpr CoordinateVector3 map_enu_polar_to_unreal_world(
        const CoordinateVector3& enu)
    {
        return {enu.y, enu.x, enu.z};
    }

    static constexpr CoordinateVector3 unreal_world_polar_to_map_enu(
        const CoordinateVector3& unreal)
    {
        return {unreal.y, unreal.x, unreal.z};
    }

    static constexpr CoordinateVector3 map_enu_position_m_to_unreal_world_cm(
        const CoordinateVector3& position_enu_m,
        const CoordinateVector3& unreal_origin_cm = {})
    {
        const auto unreal = map_enu_polar_to_unreal_world(position_enu_m);
        return {
            unreal.x * kMetersToCentimeters + unreal_origin_cm.x,
            unreal.y * kMetersToCentimeters + unreal_origin_cm.y,
            unreal.z * kMetersToCentimeters + unreal_origin_cm.z};
    }

    static constexpr CoordinateVector3 unreal_world_position_cm_to_map_enu_m(
        const CoordinateVector3& position_unreal_cm,
        const CoordinateVector3& unreal_origin_cm = {})
    {
        return unreal_world_polar_to_map_enu({
            (position_unreal_cm.x - unreal_origin_cm.x) / kMetersToCentimeters,
            (position_unreal_cm.y - unreal_origin_cm.y) / kMetersToCentimeters,
            (position_unreal_cm.z - unreal_origin_cm.z) / kMetersToCentimeters});
    }

    static constexpr CoordinateVector3 map_enu_velocity_mps_to_unreal_world_cmps(
        const CoordinateVector3& velocity_enu_mps)
    {
        const auto unreal = map_enu_polar_to_unreal_world(velocity_enu_mps);
        return {
            unreal.x * kMetersToCentimeters,
            unreal.y * kMetersToCentimeters,
            unreal.z * kMetersToCentimeters};
    }

    static constexpr CoordinateVector3 unreal_world_velocity_cmps_to_map_enu_mps(
        const CoordinateVector3& velocity_unreal_cmps)
    {
        return unreal_world_polar_to_map_enu({
            velocity_unreal_cmps.x / kMetersToCentimeters,
            velocity_unreal_cmps.y / kMetersToCentimeters,
            velocity_unreal_cmps.z / kMetersToCentimeters});
    }

    static constexpr CoordinateVector3
    map_enu_acceleration_mps2_to_unreal_world_cmps2(
        const CoordinateVector3& acceleration_enu_mps2)
    {
        return map_enu_velocity_mps_to_unreal_world_cmps(acceleration_enu_mps2);
    }

    static constexpr CoordinateVector3
    unreal_world_acceleration_cmps2_to_map_enu_mps2(
        const CoordinateVector3& acceleration_unreal_cmps2)
    {
        return unreal_world_velocity_cmps_to_map_enu_mps(
            acceleration_unreal_cmps2);
    }

    static constexpr CoordinateVector3
    map_enu_axial_angular_velocity_to_unreal_world(
        const CoordinateVector3& angular_velocity_enu_rad_s)
    {
        return {
            -angular_velocity_enu_rad_s.y,
            -angular_velocity_enu_rad_s.x,
            -angular_velocity_enu_rad_s.z};
    }

    static constexpr CoordinateVector3
    unreal_world_axial_angular_velocity_to_map_enu(
        const CoordinateVector3& angular_velocity_unreal_rad_s)
    {
        return {
            -angular_velocity_unreal_rad_s.y,
            -angular_velocity_unreal_rad_s.x,
            -angular_velocity_unreal_rad_s.z};
    }

    static constexpr CoordinateVector3 base_link_polar_to_unreal_actor(
        const CoordinateVector3& flu)
    {
        return {flu.x, -flu.y, flu.z};
    }

    static constexpr CoordinateVector3 unreal_actor_polar_to_base_link(
        const CoordinateVector3& fru)
    {
        return {fru.x, -fru.y, fru.z};
    }

    static constexpr CoordinateVector3
    base_link_axial_angular_velocity_to_unreal_actor(
        const CoordinateVector3& flu_rad_s)
    {
        return {-flu_rad_s.x, flu_rad_s.y, -flu_rad_s.z};
    }

    static constexpr CoordinateVector3
    unreal_actor_axial_angular_velocity_to_base_link(
        const CoordinateVector3& fru_rad_s)
    {
        return {-fru_rad_s.x, fru_rad_s.y, -fru_rad_s.z};
    }

    static double navigation_heading_to_enu_yaw(double heading_clockwise_rad)
    {
        return normalize_positive_radians(
            std::numbers::pi_v<double> * 0.5 - heading_clockwise_rad);
    }

    static double enu_yaw_to_navigation_heading(double yaw_ccw_rad)
    {
        return normalize_positive_radians(
            std::numbers::pi_v<double> * 0.5 - yaw_ccw_rad);
    }

    // Returns the proper RH rotation map_enu_from_base_link. This quaternion is
    // a mathematical frame value; it is not added to the schema-v2 payload.
    static CoordinateQuaternion canonical_attitude_to_map_enu_from_base_link(
        const CanonicalAttitudeRadians& attitude)
    {
        const double yaw = navigation_heading_to_enu_yaw(
            attitude.heading_clockwise);
        const double pitch = -attitude.pitch_nose_up;
        const double roll = attitude.roll_left_up;

        const double cy = std::cos(yaw);
        const double sy = std::sin(yaw);
        const double cp = std::cos(pitch);
        const double sp = std::sin(pitch);
        const double cr = std::cos(roll);
        const double sr = std::sin(roll);
        const Matrix3 rotation{{
            {{cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr}},
            {{sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr}},
            {{-sp, cp * sr, cp * cr}},
        }};
        return rotation_to_quaternion(rotation);
    }

    static CanonicalAttitudeRadians map_enu_from_base_link_to_canonical_attitude(
        const CoordinateQuaternion& quaternion)
    {
        const Matrix3 rotation = quaternion_to_rotation(quaternion);
        const double pitch_nose_up = std::asin(std::clamp(
            rotation[2][0], -1.0, 1.0));
        const double yaw_ccw = std::atan2(rotation[1][0], rotation[0][0]);
        return {
            enu_yaw_to_navigation_heading(yaw_ccw),
            pitch_nose_up,
            std::atan2(rotation[2][1], rotation[2][2])};
    }

    // Orientation basis change. R_enu_from_flu becomes
    // R_unreal_world_from_actor = A * R_enu_from_flu * B^-1. A and B are both
    // reflections, so the resulting matrix is a proper rotation.
    static CoordinateQuaternion
    map_enu_from_base_link_to_unreal_world_from_actor(
        const CoordinateQuaternion& map_enu_from_base_link)
    {
        return rotation_to_quaternion(change_orientation_basis(
            quaternion_to_rotation(map_enu_from_base_link)));
    }

    static CoordinateQuaternion
    unreal_world_from_actor_to_map_enu_from_base_link(
        const CoordinateQuaternion& unreal_world_from_actor)
    {
        // A^-1=A and B^-1=B, so the same matrix operation is its inverse.
        return rotation_to_quaternion(change_orientation_basis(
            quaternion_to_rotation(unreal_world_from_actor)));
    }

    static CoordinateVector3 rotate_polar_vector(
        const CoordinateQuaternion& quaternion,
        const CoordinateVector3& vector)
    {
        const Matrix3 rotation = quaternion_to_rotation(quaternion);
        return {
            rotation[0][0] * vector.x + rotation[0][1] * vector.y
                + rotation[0][2] * vector.z,
            rotation[1][0] * vector.x + rotation[1][1] * vector.y
                + rotation[1][2] * vector.z,
            rotation[2][0] * vector.x + rotation[2][1] * vector.y
                + rotation[2][2] * vector.z};
    }

    static double quaternion_angular_distance(
        const CoordinateQuaternion& lhs,
        const CoordinateQuaternion& rhs)
    {
        const auto left = normalize_quaternion(lhs);
        const auto right = normalize_quaternion(rhs);
        const double dot = std::clamp(std::abs(
            left.x * right.x + left.y * right.y + left.z * right.z
                + left.w * right.w), 0.0, 1.0);
        return 2.0 * std::acos(dot);
    }

private:
    using Matrix3 = std::array<std::array<double, 3>, 3>;

    static double normalize_positive_radians(double radians)
    {
        const double full_turn = 2.0 * std::numbers::pi_v<double>;
        radians = std::fmod(radians, full_turn);
        return radians < 0.0 ? radians + full_turn : radians;
    }

    static CoordinateQuaternion normalize_quaternion(
        const CoordinateQuaternion& quaternion)
    {
        const double length = std::hypot(
            std::hypot(quaternion.x, quaternion.y),
            std::hypot(quaternion.z, quaternion.w));
        if (!std::isfinite(length) || length <= 0.0) {
            return {};
        }
        CoordinateQuaternion result{
            quaternion.x / length,
            quaternion.y / length,
            quaternion.z / length,
            quaternion.w / length};
        // q and -q encode the same rotation. Canonicalize for deterministic
        // golden values while angular comparisons still accept either sign.
        if (result.w < 0.0) {
            result = {-result.x, -result.y, -result.z, -result.w};
        }
        return result;
    }

    static Matrix3 quaternion_to_rotation(
        const CoordinateQuaternion& input)
    {
        const auto q = normalize_quaternion(input);
        const double xx = q.x * q.x;
        const double yy = q.y * q.y;
        const double zz = q.z * q.z;
        const double xy = q.x * q.y;
        const double xz = q.x * q.z;
        const double yz = q.y * q.z;
        const double xw = q.x * q.w;
        const double yw = q.y * q.w;
        const double zw = q.z * q.w;
        return {{
            {{1.0 - 2.0 * (yy + zz), 2.0 * (xy - zw), 2.0 * (xz + yw)}},
            {{2.0 * (xy + zw), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - xw)}},
            {{2.0 * (xz - yw), 2.0 * (yz + xw), 1.0 - 2.0 * (xx + yy)}},
        }};
    }

    static CoordinateQuaternion rotation_to_quaternion(const Matrix3& rotation)
    {
        CoordinateQuaternion result;
        const double trace = rotation[0][0] + rotation[1][1] + rotation[2][2];
        if (trace > 0.0) {
            const double scale = 2.0 * std::sqrt(trace + 1.0);
            result = {
                (rotation[2][1] - rotation[1][2]) / scale,
                (rotation[0][2] - rotation[2][0]) / scale,
                (rotation[1][0] - rotation[0][1]) / scale,
                0.25 * scale};
        } else if (rotation[0][0] > rotation[1][1]
                   && rotation[0][0] > rotation[2][2]) {
            const double scale = 2.0 * std::sqrt(
                1.0 + rotation[0][0] - rotation[1][1] - rotation[2][2]);
            result = {
                0.25 * scale,
                (rotation[0][1] + rotation[1][0]) / scale,
                (rotation[0][2] + rotation[2][0]) / scale,
                (rotation[2][1] - rotation[1][2]) / scale};
        } else if (rotation[1][1] > rotation[2][2]) {
            const double scale = 2.0 * std::sqrt(
                1.0 + rotation[1][1] - rotation[0][0] - rotation[2][2]);
            result = {
                (rotation[0][1] + rotation[1][0]) / scale,
                0.25 * scale,
                (rotation[1][2] + rotation[2][1]) / scale,
                (rotation[0][2] - rotation[2][0]) / scale};
        } else {
            const double scale = 2.0 * std::sqrt(
                1.0 + rotation[2][2] - rotation[0][0] - rotation[1][1]);
            result = {
                (rotation[0][2] + rotation[2][0]) / scale,
                (rotation[1][2] + rotation[2][1]) / scale,
                0.25 * scale,
                (rotation[1][0] - rotation[0][1]) / scale};
        }
        return normalize_quaternion(result);
    }

    static Matrix3 change_orientation_basis(const Matrix3& rotation)
    {
        // A * rotation * B. Left multiplication swaps East/North rows; right
        // multiplication flips the FLU-left column into Actor-right.
        return {{
            {{rotation[1][0], -rotation[1][1], rotation[1][2]}},
            {{rotation[0][0], -rotation[0][1], rotation[0][2]}},
            {{rotation[2][0], -rotation[2][1], rotation[2][2]}},
        }};
    }
};

} // namespace simcore_host
