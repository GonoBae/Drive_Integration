#include "physics/chassis_ground_contact.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace simcore_host {
namespace {

constexpr double kDegreesToRadians = std::numbers::pi_v<double> / 180.0;
constexpr double kQueryLiftM = 4.0;
constexpr double kQueryDepthM = 24.0;
constexpr double kPenetrationToleranceM = 1e-6;
constexpr double kContactSlopM = 1e-4;

using Vector3 = std::array<double, 3>;

struct BodyBasis {
    Vector3 forward{};
    Vector3 right{};
    Vector3 up{};
};

struct ContactConstraint {
    Vector3 local_offset{};
    Vector3 normal{};
    GroundPointEnu point{};
};

[[nodiscard]] double dot(const Vector3& lhs, const Vector3& rhs)
{
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

[[nodiscard]] Vector3 cross(const Vector3& lhs, const Vector3& rhs)
{
    return {
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0]};
}

[[nodiscard]] BodyBasis make_body_basis(
    double heading_rad, double pitch_rad, double roll_rad)
{
    const double heading_sine = std::sin(heading_rad);
    const double heading_cosine = std::cos(heading_rad);
    const double pitch_sine = std::sin(pitch_rad);
    const double pitch_cosine = std::cos(pitch_rad);
    const double roll_sine = std::sin(roll_rad);
    const double roll_cosine = std::cos(roll_rad);
    const Vector3 horizontal_forward{
        heading_sine, heading_cosine, 0.0};
    const Vector3 horizontal_right{
        heading_cosine, -heading_sine, 0.0};
    const Vector3 pitch_up{
        -pitch_sine * horizontal_forward[0],
        -pitch_sine * horizontal_forward[1],
        pitch_cosine};
    return {
        {
            pitch_cosine * horizontal_forward[0],
            pitch_cosine * horizontal_forward[1],
            pitch_sine,
        },
        {
            roll_cosine * horizontal_right[0] - roll_sine * pitch_up[0],
            roll_cosine * horizontal_right[1] - roll_sine * pitch_up[1],
            -roll_sine * pitch_up[2],
        },
        {
            roll_sine * horizontal_right[0] + roll_cosine * pitch_up[0],
            roll_sine * horizontal_right[1] + roll_cosine * pitch_up[1],
            roll_cosine * pitch_up[2],
        }};
}

[[nodiscard]] Vector3 make_world_offset(
    const BodyBasis& basis, const Vector3& local)
{
    return {
        local[0] * basis.forward[0] + local[1] * basis.right[0]
            + local[2] * basis.up[0],
        local[0] * basis.forward[1] + local[1] * basis.right[1]
            + local[2] * basis.up[1],
        local[0] * basis.forward[2] + local[1] * basis.right[2]
            + local[2] * basis.up[2]};
}

[[nodiscard]] Vector3 make_horizontal_right(double heading_rad)
{
    return {std::cos(heading_rad), -std::sin(heading_rad), 0.0};
}

[[nodiscard]] double effective_pitch_inertia(
    const ChassisContactBox& box, double roll_rad)
{
    const double cosine = std::cos(roll_rad);
    const double sine = std::sin(roll_rad);
    return box.pitch_inertia_kg_m2 * cosine * cosine
        + box.yaw_inertia_kg_m2 * sine * sine;
}

[[nodiscard]] bool finite_pose(const ChassisGroundPose& pose)
{
    return std::isfinite(pose.east_m) && std::isfinite(pose.north_m)
        && std::isfinite(pose.up_m) && std::isfinite(pose.heading_rad)
        && std::isfinite(pose.pitch_rad) && std::isfinite(pose.roll_rad)
        && std::isfinite(pose.root_up_velocity_mps)
        && std::isfinite(pose.pitch_rate_rad_s)
        && std::isfinite(pose.roll_rate_rad_s);
}

[[nodiscard]] bool valid_box(const ChassisContactBox& box)
{
    return std::isfinite(box.center_up_offset_m)
        && std::isfinite(box.half_length_m)
        && std::isfinite(box.half_width_m)
        && std::isfinite(box.half_height_m)
        && std::isfinite(box.mass_kg)
        && std::isfinite(box.roll_inertia_kg_m2)
        && std::isfinite(box.pitch_inertia_kg_m2)
        && std::isfinite(box.yaw_inertia_kg_m2)
        && box.half_length_m > 0.0 && box.half_width_m > 0.0
        && box.half_height_m > 0.0 && box.mass_kg > 0.0
        && box.roll_inertia_kg_m2 > 0.0
        && box.pitch_inertia_kg_m2 > 0.0
        && box.yaw_inertia_kg_m2 > 0.0;
}

[[nodiscard]] std::vector<Vector3> make_shell_samples(
    const ChassisContactBox& box)
{
    std::vector<Vector3> samples;
    samples.reserve(14);
    for (const double x : {-box.half_length_m, box.half_length_m}) {
        for (const double y : {-box.half_width_m, box.half_width_m}) {
            for (const double z : {
                     box.center_up_offset_m - box.half_height_m,
                     box.center_up_offset_m + box.half_height_m}) {
                samples.push_back({x, y, z});
            }
        }
    }
    samples.push_back({-box.half_length_m, 0.0, box.center_up_offset_m});
    samples.push_back({box.half_length_m, 0.0, box.center_up_offset_m});
    samples.push_back({0.0, -box.half_width_m, box.center_up_offset_m});
    samples.push_back({0.0, box.half_width_m, box.center_up_offset_m});
    samples.push_back(
        {0.0, 0.0, box.center_up_offset_m - box.half_height_m});
    samples.push_back(
        {0.0, 0.0, box.center_up_offset_m + box.half_height_m});
    return samples;
}

[[nodiscard]] std::vector<ContactConstraint> sample_constraints(
    const ChassisGroundPose& pose,
    const ChassisContactBox& box,
    const GroundQuery& ground_query,
    double& maximum_penetration_m)
{
    const BodyBasis basis = make_body_basis(
        pose.heading_rad, pose.pitch_rad, pose.roll_rad);
    std::vector<ContactConstraint> constraints;
    constraints.reserve(14);
    for (const auto& local : make_shell_samples(box)) {
        const auto offset = make_world_offset(basis, local);
        const Vector3 world{
            pose.east_m + offset[0],
            pose.north_m + offset[1],
            pose.up_m + offset[2]};
        const auto hit = ground_query.query_down({
            {world[0], world[1], world[2] + kQueryLiftM}, kQueryDepthM});
        if (!hit || !std::isfinite(hit->point_enu.east_m)
            || !std::isfinite(hit->point_enu.north_m)
            || !std::isfinite(hit->point_enu.up_m)
            || !std::isfinite(hit->normal_enu.east_m)
            || !std::isfinite(hit->normal_enu.north_m)
            || !std::isfinite(hit->normal_enu.up_m)) {
            continue;
        }
        const double normal_length = std::hypot(
            hit->normal_enu.east_m,
            hit->normal_enu.north_m,
            hit->normal_enu.up_m);
        if (!std::isfinite(normal_length) || normal_length <= 1e-9
            || hit->normal_enu.up_m <= 0.0) {
            continue;
        }
        const Vector3 normal{
            hit->normal_enu.east_m / normal_length,
            hit->normal_enu.north_m / normal_length,
            hit->normal_enu.up_m / normal_length};
        const double clearance = normal[0] * (world[0] - hit->point_enu.east_m)
            + normal[1] * (world[1] - hit->point_enu.north_m)
            + normal[2] * (world[2] - hit->point_enu.up_m);
        maximum_penetration_m = std::max(
            maximum_penetration_m, std::max(0.0, -clearance));
        // Distant terrain cannot constrain the body. Keep a small positive
        // band so an exactly resting shell still receives a velocity impulse.
        if (clearance <= kContactSlopM) {
            constraints.push_back({local, normal, hit->point_enu});
        }
    }
    return constraints;
}

[[nodiscard]] double clearance_for(
    const ChassisGroundPose& pose,
    const BodyBasis& basis,
    const ContactConstraint& constraint,
    Vector3* world_offset = nullptr)
{
    const auto offset = make_world_offset(basis, constraint.local_offset);
    if (world_offset) *world_offset = offset;
    return constraint.normal[0]
            * (pose.east_m + offset[0] - constraint.point.east_m)
        + constraint.normal[1]
            * (pose.north_m + offset[1] - constraint.point.north_m)
        + constraint.normal[2]
            * (pose.up_m + offset[2] - constraint.point.up_m);
}

} // namespace

ChassisGroundContactResult resolve_chassis_ground_contact(
    ChassisGroundPose pose,
    const ChassisContactBox& box,
    const GroundQuery& ground_query)
{
    if (!finite_pose(pose) || !valid_box(box)) {
        throw std::invalid_argument("Chassis ground-contact input is invalid");
    }

    ChassisGroundContactResult result;
    result.pose = pose;
    auto constraints = sample_constraints(
        pose, box, ground_query, result.maximum_penetration_m);
    if (constraints.empty()) return result;

    constexpr int kPositionIterations = 32;
    constexpr double kMaximumTranslationStepM = 0.15;
    constexpr double kMaximumRotationStepRad = 2.0 * kDegreesToRadians;
    for (int iteration = 0; iteration < kPositionIterations; ++iteration) {
        const auto basis = make_body_basis(
            pose.heading_rad, pose.pitch_rad, pose.roll_rad);
        const auto pitch_axis = make_horizontal_right(pose.heading_rad);
        const double pitch_inertia = effective_pitch_inertia(box, pose.roll_rad);
        double accumulated_up = 0.0;
        double accumulated_roll = 0.0;
        double accumulated_pitch = 0.0;
        std::size_t correction_count = 0;
        for (const auto& constraint : constraints) {
            Vector3 offset{};
            const double clearance = clearance_for(
                pose, basis, constraint, &offset);
            if (clearance >= -kPenetrationToleranceM) continue;
            const double jacobian_up = constraint.normal[2];
            const double jacobian_roll = dot(
                constraint.normal, cross(basis.forward, offset));
            const double jacobian_pitch = dot(
                constraint.normal, cross(pitch_axis, offset));
            const double inverse_effective_mass =
                jacobian_up * jacobian_up / box.mass_kg
                + jacobian_roll * jacobian_roll / box.roll_inertia_kg_m2
                + jacobian_pitch * jacobian_pitch / pitch_inertia;
            if (!std::isfinite(inverse_effective_mass)
                || inverse_effective_mass <= 1e-12) {
                continue;
            }
            const double multiplier = -clearance / inverse_effective_mass;
            accumulated_up += multiplier * jacobian_up / box.mass_kg;
            accumulated_roll += multiplier * jacobian_roll
                / box.roll_inertia_kg_m2;
            accumulated_pitch += multiplier * jacobian_pitch / pitch_inertia;
            ++correction_count;
        }
        if (correction_count == 0) break;
        const double inverse_count = 1.0 / correction_count;
        pose.up_m += std::clamp(
            accumulated_up * inverse_count,
            -kMaximumTranslationStepM,
            kMaximumTranslationStepM);
        pose.roll_rad += std::clamp(
            accumulated_roll * inverse_count,
            -kMaximumRotationStepRad,
            kMaximumRotationStepRad);
        pose.pitch_rad += std::clamp(
            accumulated_pitch * inverse_count,
            -kMaximumRotationStepRad,
            kMaximumRotationStepRad);
        result.corrected = true;
    }

    // Rotation is trust-region bounded. A final world-up lift guarantees that
    // no sampled shell point remains below an up-facing terrain plane.
    const auto residual_basis = make_body_basis(
        pose.heading_rad, pose.pitch_rad, pose.roll_rad);
    double residual_up_lift = 0.0;
    for (const auto& constraint : constraints) {
        const double clearance = clearance_for(
            pose, residual_basis, constraint);
        if (constraint.normal[2] > 1e-9) {
            residual_up_lift = std::max(
                residual_up_lift,
                (-clearance + kPenetrationToleranceM) / constraint.normal[2]);
        }
    }
    if (residual_up_lift > 0.0) {
        pose.up_m += residual_up_lift;
        result.corrected = true;
    }

    struct VelocityConstraint {
        double jacobian_up = 0.0;
        double jacobian_roll = 0.0;
        double jacobian_pitch = 0.0;
        double inverse_effective_mass = 0.0;
    };
    std::vector<VelocityConstraint> velocity_constraints;
    velocity_constraints.reserve(constraints.size());
    const auto velocity_basis = make_body_basis(
        pose.heading_rad, pose.pitch_rad, pose.roll_rad);
    const auto pitch_axis = make_horizontal_right(pose.heading_rad);
    const double pitch_inertia = effective_pitch_inertia(box, pose.roll_rad);
    for (const auto& constraint : constraints) {
        Vector3 offset{};
        const double clearance = clearance_for(
            pose, velocity_basis, constraint, &offset);
        if (clearance > kContactSlopM) continue;
        const double jacobian_up = constraint.normal[2];
        const double jacobian_roll = dot(
            constraint.normal, cross(velocity_basis.forward, offset));
        const double jacobian_pitch = dot(
            constraint.normal, cross(pitch_axis, offset));
        const double inverse_effective_mass =
            jacobian_up * jacobian_up / box.mass_kg
            + jacobian_roll * jacobian_roll / box.roll_inertia_kg_m2
            + jacobian_pitch * jacobian_pitch / pitch_inertia;
        if (inverse_effective_mass > 1e-12) {
            velocity_constraints.push_back({
                jacobian_up,
                jacobian_roll,
                jacobian_pitch,
                inverse_effective_mass});
        }
    }
    result.contact_count = velocity_constraints.size();

    std::vector<double> impulses(velocity_constraints.size(), 0.0);
    constexpr int kVelocityIterations = 32;
    for (int iteration = 0; iteration < kVelocityIterations; ++iteration) {
        double maximum_delta = 0.0;
        for (std::size_t sweep = 0; sweep < velocity_constraints.size(); ++sweep) {
            const std::size_t index = iteration % 2 == 0
                ? sweep : velocity_constraints.size() - 1 - sweep;
            const auto& constraint = velocity_constraints[index];
            const double approaching_rate =
                constraint.jacobian_up * pose.root_up_velocity_mps
                + constraint.jacobian_roll * pose.roll_rate_rad_s
                + constraint.jacobian_pitch * pose.pitch_rate_rad_s;
            const double previous = impulses[index];
            const double next = std::max(
                0.0, previous - approaching_rate
                    / constraint.inverse_effective_mass);
            const double delta = next - previous;
            impulses[index] = next;
            maximum_delta = std::max(maximum_delta, std::abs(delta));
            pose.root_up_velocity_mps +=
                delta * constraint.jacobian_up / box.mass_kg;
            pose.roll_rate_rad_s +=
                delta * constraint.jacobian_roll / box.roll_inertia_kg_m2;
            pose.pitch_rate_rad_s +=
                delta * constraint.jacobian_pitch / pitch_inertia;
        }
        if (maximum_delta <= 1e-8) break;
    }
    for (const double impulse : impulses) {
        result.accumulated_normal_impulse_n_s += impulse;
    }
    result.pose = pose;
    return result;
}

} // namespace simcore_host
