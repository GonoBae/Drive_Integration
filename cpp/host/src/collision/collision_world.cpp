#include "collision/collision_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iterator>
#include <map>
#include <numeric>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace simcore_host {
namespace {

constexpr double kAxisEpsilon = 1e-12;
constexpr double kSeparationSlopM = 1e-6;
constexpr double kMaximumTranslationPerSubstepM = 0.10;
constexpr double kMaximumRotationPerSubstepRad =
    std::numbers::pi_v<double> / 180.0;
constexpr std::size_t kMaximumSubsteps = 64;
constexpr std::size_t kSolverIterations = 8;
constexpr double kBroadPhaseCellSizeM = 8.0;
constexpr std::size_t kMaximumGridCellsPerObject = 4096;
constexpr std::size_t kMaximumGridCellsPerQuery = 4096;
constexpr double kMinimumFiniteProxyMassKg = 1.0;
constexpr double kMaximumFiniteProxyMassKg = 100'000.0;
constexpr double kMinimumFiniteProxyYawInertiaKgM2 = 1.0;
constexpr double kMaximumFiniteProxyYawInertiaKgM2 = 100'000'000.0;

struct HorizontalAabb {
    double minimum_east_m = 0.0;
    double minimum_north_m = 0.0;
    double maximum_east_m = 0.0;
    double maximum_north_m = 0.0;
};

struct GridCellKey {
    std::int64_t east = 0;
    std::int64_t north = 0;

    friend bool operator<(const GridCellKey& lhs, const GridCellKey& rhs)
    {
        return lhs.east < rhs.east
            || (lhs.east == rhs.east && lhs.north < rhs.north);
    }
};

struct GridCellRange {
    std::int64_t minimum_east = 0;
    std::int64_t minimum_north = 0;
    std::int64_t maximum_east = 0;
    std::int64_t maximum_north = 0;
};

std::optional<std::int64_t> coordinate_to_cell(double coordinate_m)
{
    if (!std::isfinite(coordinate_m)) {
        return std::nullopt;
    }
    const long double cell = std::floor(
        static_cast<long double>(coordinate_m) / kBroadPhaseCellSizeM);
    if (cell < static_cast<long double>(
                   std::numeric_limits<std::int64_t>::min())
        || cell > static_cast<long double>(
                   std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(cell);
}

std::optional<GridCellRange> grid_cell_range(
    const HorizontalAabb& aabb, std::size_t maximum_cells)
{
    // Expand by one representable value so geometry exactly on a grid boundary
    // is indexed on both sides. This may add false positives but never misses
    // an overlap whose AABBs meet at a boundary.
    const auto minimum_east = coordinate_to_cell(std::nextafter(
        aabb.minimum_east_m, -std::numeric_limits<double>::infinity()));
    const auto minimum_north = coordinate_to_cell(std::nextafter(
        aabb.minimum_north_m, -std::numeric_limits<double>::infinity()));
    const auto maximum_east = coordinate_to_cell(std::nextafter(
        aabb.maximum_east_m, std::numeric_limits<double>::infinity()));
    const auto maximum_north = coordinate_to_cell(std::nextafter(
        aabb.maximum_north_m, std::numeric_limits<double>::infinity()));
    if (!minimum_east || !minimum_north || !maximum_east || !maximum_north
        || *minimum_east > *maximum_east
        || *minimum_north > *maximum_north) {
        return std::nullopt;
    }

    const long double east_count =
        static_cast<long double>(*maximum_east)
        - static_cast<long double>(*minimum_east) + 1.0L;
    const long double north_count =
        static_cast<long double>(*maximum_north)
        - static_cast<long double>(*minimum_north) + 1.0L;
    if (east_count <= 0.0L || north_count <= 0.0L
        || east_count > static_cast<long double>(maximum_cells)
        || north_count > static_cast<long double>(maximum_cells)
        || east_count * north_count
            > static_cast<long double>(maximum_cells)) {
        return std::nullopt;
    }
    return GridCellRange{
        *minimum_east, *minimum_north, *maximum_east, *maximum_north};
}

class UniformGridIndex {
public:
    explicit UniformGridIndex(const std::vector<HorizontalAabb>& object_aabbs)
        : object_count_(object_aabbs.size())
    {
        for (std::size_t index = 0; index < object_aabbs.size(); ++index) {
            const auto range = grid_cell_range(
                object_aabbs[index], kMaximumGridCellsPerObject);
            if (!range) {
                global_indices_.push_back(index);
                continue;
            }
            for (std::int64_t east = range->minimum_east;; ++east) {
                for (std::int64_t north = range->minimum_north;; ++north) {
                    cells_[{east, north}].push_back(index);
                    if (north == range->maximum_north) {
                        break;
                    }
                }
                if (east == range->maximum_east) {
                    break;
                }
            }
        }
    }

    [[nodiscard]] std::vector<std::size_t> query(
        const HorizontalAabb& query_aabb) const
    {
        if (object_count_ == 0) {
            return {};
        }
        const auto range = grid_cell_range(
            query_aabb, kMaximumGridCellsPerQuery);
        if (!range) {
            std::vector<std::size_t> all(object_count_);
            std::iota(all.begin(), all.end(), std::size_t{0});
            return all;
        }

        std::vector<std::size_t> candidates = global_indices_;
        for (std::int64_t east = range->minimum_east;; ++east) {
            for (std::int64_t north = range->minimum_north;; ++north) {
                const auto cell = cells_.find({east, north});
                if (cell != cells_.end()) {
                    candidates.insert(
                        candidates.end(),
                        cell->second.begin(),
                        cell->second.end());
                }
                if (north == range->maximum_north) {
                    break;
                }
            }
            if (east == range->maximum_east) {
                break;
            }
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(
            std::unique(candidates.begin(), candidates.end()),
            candidates.end());
        return candidates;
    }

private:
    std::size_t object_count_ = 0;
    std::map<GridCellKey, std::vector<std::size_t>> cells_;
    std::vector<std::size_t> global_indices_;
};

struct ObbBasis {
    CollisionVector2 forward;
    CollisionVector2 right;
};

double dot(const CollisionVector2& lhs, const CollisionVector2& rhs)
{
    return lhs.east_m * rhs.east_m + lhs.north_m * rhs.north_m;
}

double cross(const CollisionVector2& lhs, const CollisionVector2& rhs)
{
    return lhs.east_m * rhs.north_m - lhs.north_m * rhs.east_m;
}

CollisionVector2 add(
    const CollisionVector2& lhs, const CollisionVector2& rhs)
{
    return {lhs.east_m + rhs.east_m, lhs.north_m + rhs.north_m};
}

CollisionVector2 subtract(
    const CollisionVector2& lhs, const CollisionVector2& rhs)
{
    return {lhs.east_m - rhs.east_m, lhs.north_m - rhs.north_m};
}

CollisionVector2 multiply(const CollisionVector2& value, double scalar)
{
    return {value.east_m * scalar, value.north_m * scalar};
}

double normalize_heading(double radians)
{
    const double full_turn = 2.0 * std::numbers::pi_v<double>;
    radians = std::fmod(radians, full_turn);
    return radians < 0.0 ? radians + full_turn : radians;
}

bool finite_vector(const CollisionVector2& value)
{
    return std::isfinite(value.east_m) && std::isfinite(value.north_m);
}

bool valid_shape(const ObbPrism& shape)
{
    return finite_vector(shape.center_enu)
        && std::isfinite(shape.center_up_m)
        && std::isfinite(shape.heading_rad)
        && std::isfinite(shape.half_length_m)
        && std::isfinite(shape.half_width_m)
        && std::isfinite(shape.half_height_m)
        && shape.half_length_m > 0.0
        && shape.half_width_m > 0.0
        && shape.half_height_m > 0.0;
}

bool valid_capsule(const VerticalCapsule& capsule)
{
    return finite_vector(capsule.center_enu)
        && std::isfinite(capsule.center_up_m)
        && std::isfinite(capsule.radius_m)
        && std::isfinite(capsule.half_height_m)
        && capsule.radius_m > 0.0
        && capsule.half_height_m >= capsule.radius_m;
}

bool valid_material(const CollisionMaterial& material)
{
    return std::isfinite(material.friction)
        && material.friction >= 0.0
        && std::isfinite(material.restitution)
        && material.restitution >= 0.0
        && material.restitution <= 1.0;
}

bool valid_semantic(StaticColliderSemantic semantic)
{
    switch (semantic) {
    case StaticColliderSemantic::Wall:
    case StaticColliderSemantic::Curb:
    case StaticColliderSemantic::Barrier:
        return true;
    }
    return false;
}

ObbBasis make_basis(double heading_rad)
{
    const double sine = std::sin(heading_rad);
    const double cosine = std::cos(heading_rad);
    return {{sine, cosine}, {cosine, -sine}};
}

HorizontalAabb horizontal_aabb(const ObbPrism& shape)
{
    const ObbBasis basis = make_basis(shape.heading_rad);
    const double east_radius =
        shape.half_length_m * std::abs(basis.forward.east_m)
        + shape.half_width_m * std::abs(basis.right.east_m);
    const double north_radius =
        shape.half_length_m * std::abs(basis.forward.north_m)
        + shape.half_width_m * std::abs(basis.right.north_m);
    return {
        shape.center_enu.east_m - east_radius,
        shape.center_enu.north_m - north_radius,
        shape.center_enu.east_m + east_radius,
        shape.center_enu.north_m + north_radius};
}

HorizontalAabb horizontal_aabb(const VerticalCapsule& capsule)
{
    return {
        capsule.center_enu.east_m - capsule.radius_m,
        capsule.center_enu.north_m - capsule.radius_m,
        capsule.center_enu.east_m + capsule.radius_m,
        capsule.center_enu.north_m + capsule.radius_m};
}

HorizontalAabb horizontal_aabb(const KinematicProxyShape& shape)
{
    return std::visit(
        [](const auto& value) { return horizontal_aabb(value); }, shape);
}

std::vector<std::size_t> all_indices(std::size_t count)
{
    std::vector<std::size_t> indices(count);
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    return indices;
}

double projected_radius(
    const ObbPrism& shape,
    const ObbBasis& basis,
    const CollisionVector2& axis)
{
    return shape.half_length_m * std::abs(dot(axis, basis.forward))
         + shape.half_width_m * std::abs(dot(axis, basis.right));
}

CollisionVector2 support_point(
    const ObbPrism& shape,
    const ObbBasis& basis,
    const CollisionVector2& direction)
{
    CollisionVector2 point = shape.center_enu;
    const auto add_axis = [&](const CollisionVector2& axis, double extent) {
        const double projection = dot(direction, axis);
        if (std::abs(projection) <= kAxisEpsilon) {
            return;
        }
        point = add(point, multiply(axis, std::copysign(extent, projection)));
    };
    add_axis(basis.forward, shape.half_length_m);
    add_axis(basis.right, shape.half_width_m);
    return point;
}

CollisionVector2 contact_velocity(
    const PlanarRigidBody& body,
    const CollisionVector2& contact_offset)
{
    // ENU cross products use counter-clockwise-positive angular velocity;
    // navigation heading rate has the opposite sign.
    const double angular_velocity_ccw = -body.heading_rate_rad_s;
    return {
        body.linear_velocity_enu_mps.east_m
            - angular_velocity_ccw * contact_offset.north_m,
        body.linear_velocity_enu_mps.north_m
            + angular_velocity_ccw * contact_offset.east_m};
}

CollisionVector2 kinematic_contact_velocity(
    const KinematicCollisionProxy& proxy,
    const CollisionVector2& contact_point)
{
    const CollisionVector2 center = std::visit(
        [](const auto& shape) { return shape.center_enu; }, proxy.shape);
    const CollisionVector2 contact_offset = subtract(contact_point, center);
    const double angular_velocity_ccw = -proxy.heading_rate_rad_s;
    return {
        proxy.linear_velocity_enu_mps.east_m
            - angular_velocity_ccw * contact_offset.north_m,
        proxy.linear_velocity_enu_mps.north_m
            + angular_velocity_ccw * contact_offset.east_m};
}

CollisionVector2 proxy_center(const KinematicCollisionProxy& proxy)
{
    return std::visit(
        [](const auto& shape) { return shape.center_enu; }, proxy.shape);
}

void translate_proxy(
    KinematicCollisionProxy& proxy, const CollisionVector2& translation)
{
    std::visit(
        [&](auto& shape) { shape.center_enu = add(shape.center_enu, translation); },
        proxy.shape);
}

double proxy_inverse_yaw_inertia(const KinematicCollisionProxy& proxy)
{
    return std::holds_alternative<ObbPrism>(proxy.shape)
            && proxy.yaw_inertia_kg_m2 > 0.0
        ? 1.0 / proxy.yaw_inertia_kg_m2
        : 0.0;
}

void apply_proxy_impulse(
    KinematicCollisionProxy& proxy,
    const CollisionVector2& impulse,
    const CollisionVector2& contact_offset)
{
    if (proxy.mass_kg <= 0.0) {
        return;
    }
    const double inverse_mass = 1.0 / proxy.mass_kg;
    proxy.linear_velocity_enu_mps = add(
        proxy.linear_velocity_enu_mps, multiply(impulse, inverse_mass));
    const double inverse_yaw_inertia = proxy_inverse_yaw_inertia(proxy);
    if (inverse_yaw_inertia > 0.0) {
        const double angular_velocity_ccw = -proxy.heading_rate_rad_s
            + cross(contact_offset, impulse) * inverse_yaw_inertia;
        proxy.heading_rate_rad_s = std::clamp(
            -angular_velocity_ccw,
            -kMaximumFiniteProxyHeadingRateRadS,
            kMaximumFiniteProxyHeadingRateRadS);
    }
}

void clamp_finite_proxy_speed(KinematicCollisionProxy& proxy)
{
    if (proxy.mass_kg <= 0.0 || proxy.maximum_linear_speed_mps <= 0.0) {
        return;
    }
    const double speed = std::hypot(
        proxy.linear_velocity_enu_mps.east_m,
        proxy.linear_velocity_enu_mps.north_m);
    if (speed > proxy.maximum_linear_speed_mps) {
        proxy.linear_velocity_enu_mps = multiply(
            proxy.linear_velocity_enu_mps,
            proxy.maximum_linear_speed_mps / speed);
    }
}

void advance_proxy(KinematicCollisionProxy& proxy, double dt_seconds)
{
    if (proxy.breakaway_impulse_n_s > 0.0 && !proxy.breakaway_released) return;
    std::visit(
        [&](auto& shape) {
            shape.center_enu = add(
                shape.center_enu,
                multiply(proxy.linear_velocity_enu_mps, dt_seconds));
            using Shape = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<Shape, ObbPrism>) {
                shape.heading_rad = normalize_heading(
                    shape.heading_rad + proxy.heading_rate_rad_s * dt_seconds);
            }
        },
        proxy.shape);
}

void record_contact(
    std::vector<CollisionContact>& contacts,
    const std::string& collider_id,
    const CollisionManifold& manifold,
    double normal_impulse)
{
    const auto existing = std::lower_bound(
        contacts.begin(), contacts.end(), collider_id,
        [](const CollisionContact& contact, const std::string& searched_id) {
            return contact.collider_id < searched_id;
        });
    if (existing == contacts.end() || existing->collider_id != collider_id) {
        contacts.insert(existing, CollisionContact{
            collider_id,
            manifold.normal_enu,
            manifold.contact_point_enu,
            manifold.penetration_m,
            normal_impulse});
        return;
    }

    existing->normal_enu = manifold.normal_enu;
    existing->contact_point_enu = manifold.contact_point_enu;
    existing->maximum_penetration_m = std::max(
        existing->maximum_penetration_m, manifold.penetration_m);
    existing->accumulated_normal_impulse_n_s += normal_impulse;
}

double resolve_contact(
    PlanarRigidBody& body,
    const CollisionMaterial& material,
    const CollisionVector2& obstacle_contact_velocity,
    const CollisionManifold& manifold)
{
    body.shape.center_enu = add(
        body.shape.center_enu,
        multiply(
            manifold.normal_enu,
            manifold.penetration_m + kSeparationSlopM));

    const CollisionVector2 contact_offset = subtract(
        manifold.contact_point_enu, body.shape.center_enu);
    const CollisionVector2 velocity_at_contact = subtract(
        contact_velocity(body, contact_offset), obstacle_contact_velocity);
    const double normal_velocity = dot(
        velocity_at_contact, manifold.normal_enu);
    if (normal_velocity >= 0.0) {
        return 0.0;
    }

    const double inverse_mass = 1.0 / body.mass_kg;
    const double normal_arm = cross(contact_offset, manifold.normal_enu);
    const double normal_denominator = inverse_mass
        + normal_arm * normal_arm / body.yaw_inertia_kg_m2;
    const double normal_impulse =
        -(1.0 + material.restitution) * normal_velocity
        / normal_denominator;

    body.linear_velocity_enu_mps = add(
        body.linear_velocity_enu_mps,
        multiply(manifold.normal_enu, normal_impulse * inverse_mass));
    double angular_velocity_ccw = -body.heading_rate_rad_s
        + normal_arm * normal_impulse / body.yaw_inertia_kg_m2;

    const CollisionVector2 tangent{
        -manifold.normal_enu.north_m,
        manifold.normal_enu.east_m};
    const CollisionVector2 body_velocity_after_normal{
        body.linear_velocity_enu_mps.east_m
            - angular_velocity_ccw * contact_offset.north_m,
        body.linear_velocity_enu_mps.north_m
            + angular_velocity_ccw * contact_offset.east_m};
    const double tangent_velocity = dot(
        subtract(body_velocity_after_normal, obstacle_contact_velocity),
        tangent);
    const double tangent_arm = cross(contact_offset, tangent);
    const double tangent_denominator = inverse_mass
        + tangent_arm * tangent_arm / body.yaw_inertia_kg_m2;
    const double unconstrained_tangent_impulse =
        -tangent_velocity / tangent_denominator;
    const double maximum_tangent_impulse =
        material.friction * normal_impulse;
    const double tangent_impulse = std::clamp(
        unconstrained_tangent_impulse,
        -maximum_tangent_impulse,
        maximum_tangent_impulse);
    body.linear_velocity_enu_mps = add(
        body.linear_velocity_enu_mps,
        multiply(tangent, tangent_impulse * inverse_mass));
    angular_velocity_ccw +=
        tangent_arm * tangent_impulse / body.yaw_inertia_kg_m2;
    body.heading_rate_rad_s = -angular_velocity_ccw;
    return normal_impulse;
}

double resolve_finite_proxy_contact(
    PlanarRigidBody& body,
    KinematicCollisionProxy& proxy,
    const CollisionManifold& manifold)
{
    double anchor_impulse = 0.0;
    if (proxy.breakaway_impulse_n_s > 0.0 && !proxy.breakaway_released) {
        const auto offset = subtract(manifold.contact_point_enu, body.shape.center_enu);
        const double closing = dot(contact_velocity(body, offset), manifold.normal_enu);
        const double arm = cross(offset, manifold.normal_enu);
        const double denominator = 1.0 / body.mass_kg + arm * arm / body.yaw_inertia_kg_m2;
        const double required = std::max(0.0, -(1.0 + proxy.material.restitution) * closing / denominator);
        if (required + 1e-9 < proxy.breakaway_impulse_n_s) {
            const double applied = resolve_contact(body, proxy.material, {}, manifold);
            proxy.breakaway_impulse_n_s = std::max(0.0, proxy.breakaway_impulse_n_s - applied);
            return applied;
        }
        anchor_impulse = std::min(required, proxy.breakaway_impulse_n_s);
        body.linear_velocity_enu_mps = add(body.linear_velocity_enu_mps,
            multiply(manifold.normal_enu, anchor_impulse / body.mass_kg));
        body.heading_rate_rad_s -= arm * anchor_impulse / body.yaw_inertia_kg_m2;
        proxy.breakaway_impulse_n_s = 0.0;
        proxy.breakaway_released = true;
        // No full-stop solve/refund: the base absorbs only its resistance;
        // transfer the residual collision impulse to the now finite body.
    }
    const double body_inverse_mass = 1.0 / body.mass_kg;
    const double proxy_inverse_mass = 1.0 / proxy.mass_kg;
    const double inverse_mass_sum = body_inverse_mass + proxy_inverse_mass;
    const double separation = manifold.penetration_m + kSeparationSlopM;
    body.shape.center_enu = add(
        body.shape.center_enu,
        multiply(manifold.normal_enu,
                 separation * body_inverse_mass / inverse_mass_sum));
    translate_proxy(
        proxy,
        multiply(manifold.normal_enu,
                 -separation * proxy_inverse_mass / inverse_mass_sum));

    const CollisionVector2 body_contact_offset = subtract(
        manifold.contact_point_enu, body.shape.center_enu);
    const CollisionVector2 proxy_contact_offset = subtract(
        manifold.contact_point_enu, proxy_center(proxy));
    const CollisionVector2 relative_contact_velocity = subtract(
        contact_velocity(body, body_contact_offset),
        kinematic_contact_velocity(proxy, manifold.contact_point_enu));
    const double normal_velocity = dot(
        relative_contact_velocity, manifold.normal_enu);
    if (normal_velocity >= 0.0) {
        return anchor_impulse;
    }

    const double body_normal_arm = cross(
        body_contact_offset, manifold.normal_enu);
    const double proxy_normal_arm = cross(
        proxy_contact_offset, manifold.normal_enu);
    const double proxy_inverse_inertia = proxy_inverse_yaw_inertia(proxy);
    const double normal_denominator = body_inverse_mass + proxy_inverse_mass
        + body_normal_arm * body_normal_arm / body.yaw_inertia_kg_m2
        + proxy_normal_arm * proxy_normal_arm * proxy_inverse_inertia;
    const double normal_impulse =
        -(1.0 + proxy.material.restitution) * normal_velocity
        / normal_denominator;

    const CollisionVector2 body_normal_impulse = multiply(
        manifold.normal_enu, normal_impulse);
    body.linear_velocity_enu_mps = add(
        body.linear_velocity_enu_mps,
        multiply(body_normal_impulse, body_inverse_mass));
    double body_angular_velocity_ccw = -body.heading_rate_rad_s
        + cross(body_contact_offset, body_normal_impulse)
            / body.yaw_inertia_kg_m2;
    apply_proxy_impulse(
        proxy, multiply(body_normal_impulse, -1.0), proxy_contact_offset);

    const CollisionVector2 tangent{
        -manifold.normal_enu.north_m,
        manifold.normal_enu.east_m};
    const CollisionVector2 body_velocity_after_normal{
        body.linear_velocity_enu_mps.east_m
            - body_angular_velocity_ccw * body_contact_offset.north_m,
        body.linear_velocity_enu_mps.north_m
            + body_angular_velocity_ccw * body_contact_offset.east_m};
    const double tangent_velocity = dot(
        subtract(
            body_velocity_after_normal,
            kinematic_contact_velocity(proxy, manifold.contact_point_enu)),
        tangent);
    const double body_tangent_arm = cross(body_contact_offset, tangent);
    const double proxy_tangent_arm = cross(proxy_contact_offset, tangent);
    const double tangent_denominator = body_inverse_mass + proxy_inverse_mass
        + body_tangent_arm * body_tangent_arm / body.yaw_inertia_kg_m2
        + proxy_tangent_arm * proxy_tangent_arm * proxy_inverse_inertia;
    const double unconstrained_tangent_impulse =
        -tangent_velocity / tangent_denominator;
    const double maximum_tangent_impulse =
        proxy.material.friction * normal_impulse;
    const double tangent_impulse = std::clamp(
        unconstrained_tangent_impulse,
        -maximum_tangent_impulse,
        maximum_tangent_impulse);
    const CollisionVector2 body_tangent_impulse = multiply(
        tangent, tangent_impulse);
    body.linear_velocity_enu_mps = add(
        body.linear_velocity_enu_mps,
        multiply(body_tangent_impulse, body_inverse_mass));
    body_angular_velocity_ccw +=
        cross(body_contact_offset, body_tangent_impulse)
        / body.yaw_inertia_kg_m2;
    body.heading_rate_rad_s = -body_angular_velocity_ccw;
    apply_proxy_impulse(
        proxy, multiply(body_tangent_impulse, -1.0), proxy_contact_offset);
    clamp_finite_proxy_speed(proxy);
    return anchor_impulse + normal_impulse;
}

bool valid_proxy_shape(const KinematicProxyShape& shape)
{
    return std::visit(
        [](const auto& value) {
            using Shape = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Shape, ObbPrism>) {
                return valid_shape(value);
            } else {
                return valid_capsule(value);
            }
        },
        shape);
}

void validate_and_sort_dynamic_proxies(
    std::vector<KinematicCollisionProxy>& proxies,
    const std::vector<StaticObbCollider>& static_colliders,
    const std::string& body_id)
{
    std::unordered_set<std::string> ids;
    ids.insert(body_id);
    for (const auto& collider : static_colliders) {
        ids.insert(collider.collider_id);
    }

    for (auto& proxy : proxies) {
        if (proxy.proxy_id.empty()) {
            throw std::invalid_argument("Kinematic proxy ID cannot be empty");
        }
        if (!ids.insert(proxy.proxy_id).second) {
            throw std::invalid_argument(
                "Duplicate collision object ID: " + proxy.proxy_id);
        }
        if (!valid_proxy_shape(proxy.shape)
            || !finite_vector(proxy.linear_velocity_enu_mps)
            || !std::isfinite(proxy.heading_rate_rad_s)
            || !valid_material(proxy.material)
            || !std::isfinite(proxy.mass_kg)
            || !std::isfinite(proxy.yaw_inertia_kg_m2)
            || !std::isfinite(proxy.maximum_linear_speed_mps)
            || !std::isfinite(proxy.breakaway_impulse_n_s)
            || proxy.mass_kg < 0.0
            || proxy.yaw_inertia_kg_m2 < 0.0
            || proxy.maximum_linear_speed_mps < 0.0
            || proxy.breakaway_impulse_n_s < 0.0 || proxy.breakaway_impulse_n_s > 1e6
            || ((proxy.breakaway_impulse_n_s > 0.0 || proxy.breakaway_released)
                && (proxy.mass_kg <= 0.0 || !std::holds_alternative<ObbPrism>(proxy.shape)))
            || (proxy.breakaway_released && proxy.breakaway_impulse_n_s != 0.0)
            || proxy.supported_vehicle_id == proxy.proxy_id
            || proxy.supported_vehicle_id.size() > 256
            || !std::all_of(proxy.supported_vehicle_id.begin(), proxy.supported_vehicle_id.end(),
                [](unsigned char c) { return c >= 33 && c <= 126; })) {
            throw std::invalid_argument(
                "Kinematic proxy is invalid: " + proxy.proxy_id);
        }
        const bool finite_mass = proxy.mass_kg > 0.0;
        if ((finite_mass
                && (proxy.mass_kg < kMinimumFiniteProxyMassKg
                    || proxy.mass_kg > kMaximumFiniteProxyMassKg
                    || proxy.maximum_linear_speed_mps <= 0.0))
            || (!finite_mass
                && (proxy.yaw_inertia_kg_m2 != 0.0
                    || proxy.maximum_linear_speed_mps != 0.0))) {
            throw std::invalid_argument(
                "Finite proxy mass/speed contract is invalid: "
                + proxy.proxy_id);
        }
        if (std::holds_alternative<ObbPrism>(proxy.shape)) {
            if ((finite_mass
                    && (proxy.yaw_inertia_kg_m2
                            < kMinimumFiniteProxyYawInertiaKgM2
                        || proxy.yaw_inertia_kg_m2
                            > kMaximumFiniteProxyYawInertiaKgM2))
                || (!finite_mass && proxy.yaw_inertia_kg_m2 != 0.0)) {
                throw std::invalid_argument(
                    "Finite OBB proxy inertia is invalid: " + proxy.proxy_id);
            }
            if (finite_mass) {
                // A finite body's angular velocity can also arrive from an
                // authored kinematic discontinuity, not only from an impulse
                // solved below. Bound that input before it contributes to the
                // shared substep budget. Otherwise one malformed proxy can
                // truncate integrated_dt for every unrelated dynamic proxy.
                proxy.heading_rate_rad_s = std::clamp(
                    proxy.heading_rate_rad_s,
                    -kMaximumFiniteProxyHeadingRateRadS,
                    kMaximumFiniteProxyHeadingRateRadS);
            }
        } else if (proxy.yaw_inertia_kg_m2 != 0.0) {
            throw std::invalid_argument(
                "Capsule proxy cannot have yaw inertia: " + proxy.proxy_id);
        }
        if (auto* obb = std::get_if<ObbPrism>(&proxy.shape)) {
            obb->heading_rad = normalize_heading(obb->heading_rad);
        }
    }

    std::sort(
        proxies.begin(), proxies.end(),
        [](const KinematicCollisionProxy& lhs,
           const KinematicCollisionProxy& rhs) {
            return lhs.proxy_id < rhs.proxy_id;
        });
}

double proxy_required_substeps(
    const KinematicCollisionProxy& proxy,
    const PlanarRigidBody& body,
    double dt_seconds)
{
    const CollisionVector2 relative_velocity = subtract(
        body.linear_velocity_enu_mps, proxy.linear_velocity_enu_mps);
    const double relative_translation = std::hypot(
        relative_velocity.east_m * dt_seconds,
        relative_velocity.north_m * dt_seconds);
    double required = std::ceil(
        relative_translation / kMaximumTranslationPerSubstepM);
    if (std::holds_alternative<ObbPrism>(proxy.shape)) {
        const double relative_rotation = std::abs(
            (body.heading_rate_rad_s - proxy.heading_rate_rad_s)
            * dt_seconds);
        required = std::max(
            required,
            std::ceil(relative_rotation / kMaximumRotationPerSubstepRad));
    }
    return required;
}

std::optional<CollisionManifold> intersect_dynamic_proxy(
    const PlanarRigidBody& body,
    const KinematicCollisionProxy& proxy)
{
    const CollisionVector2 relative_velocity = subtract(
        body.linear_velocity_enu_mps, proxy.linear_velocity_enu_mps);
    return std::visit(
        [&](const auto& shape) -> std::optional<CollisionManifold> {
            using Shape = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<Shape, ObbPrism>) {
                return intersect_obb_prisms(
                    body.shape, shape, relative_velocity);
            } else {
                return intersect_obb_vertical_capsule(
                    body.shape, shape, relative_velocity);
            }
        },
        proxy.shape);
}

} // namespace

struct CollisionWorld::StaticGridIndex {
    explicit StaticGridIndex(
        const std::vector<StaticObbCollider>& static_colliders)
        : grid(make_aabbs(static_colliders))
    {
    }

    static std::vector<HorizontalAabb> make_aabbs(
        const std::vector<StaticObbCollider>& static_colliders)
    {
        std::vector<HorizontalAabb> aabbs;
        aabbs.reserve(static_colliders.size());
        for (const auto& collider : static_colliders) {
            aabbs.push_back(horizontal_aabb(collider.shape));
        }
        return aabbs;
    }

    UniformGridIndex grid;
};

std::optional<CollisionManifold> intersect_obb_prisms(
    const ObbPrism& body,
    const ObbPrism& obstacle,
    CollisionVector2 preferred_body_velocity_enu)
{
    if (!valid_shape(body) || !valid_shape(obstacle)
        || !finite_vector(preferred_body_velocity_enu)) {
        throw std::invalid_argument("OBB-prism collision input is invalid");
    }

    const double body_bottom = body.center_up_m - body.half_height_m;
    const double body_top = body.center_up_m + body.half_height_m;
    const double obstacle_bottom = obstacle.center_up_m - obstacle.half_height_m;
    const double obstacle_top = obstacle.center_up_m + obstacle.half_height_m;
    if (std::min(body_top, obstacle_top)
        - std::max(body_bottom, obstacle_bottom) <= 0.0) {
        return std::nullopt;
    }

    const ObbBasis body_basis = make_basis(body.heading_rad);
    const ObbBasis obstacle_basis = make_basis(obstacle.heading_rad);
    const std::array<CollisionVector2, 4> axes{
        obstacle_basis.forward,
        obstacle_basis.right,
        body_basis.forward,
        body_basis.right};
    const CollisionVector2 center_delta = subtract(
        body.center_enu, obstacle.center_enu);

    double minimum_overlap = std::numeric_limits<double>::infinity();
    CollisionVector2 minimum_normal{};
    for (const auto& axis : axes) {
        const double signed_distance = dot(center_delta, axis);
        const double overlap =
            projected_radius(body, body_basis, axis)
            + projected_radius(obstacle, obstacle_basis, axis)
            - std::abs(signed_distance);
        if (overlap <= 0.0) {
            return std::nullopt;
        }
        if (overlap >= minimum_overlap) {
            continue;
        }

        double direction = 1.0;
        if (signed_distance > kAxisEpsilon) {
            direction = 1.0;
        } else if (signed_distance < -kAxisEpsilon) {
            direction = -1.0;
        } else {
            const double velocity_projection = dot(
                preferred_body_velocity_enu, axis);
            if (velocity_projection > kAxisEpsilon) {
                direction = -1.0;
            } else if (velocity_projection < -kAxisEpsilon) {
                direction = 1.0;
            }
        }
        minimum_overlap = overlap;
        minimum_normal = multiply(axis, direction);
    }

    const CollisionVector2 body_support = support_point(
        body, body_basis, multiply(minimum_normal, -1.0));
    return CollisionManifold{
        minimum_normal,
        // The impulse acts on the body's support face. Using the midpoint of
        // both shape supports would make torque depend on the arbitrary centre
        // of a long wall segment even for a centred, frictionless side hit.
        body_support,
        minimum_overlap};
}

std::optional<CollisionManifold> intersect_obb_vertical_capsule(
    const ObbPrism& body,
    const VerticalCapsule& capsule,
    CollisionVector2 preferred_relative_velocity_enu)
{
    if (!valid_shape(body) || !valid_capsule(capsule)
        || !finite_vector(preferred_relative_velocity_enu)) {
        throw std::invalid_argument("OBB-capsule collision input is invalid");
    }

    const double body_bottom = body.center_up_m - body.half_height_m;
    const double body_top = body.center_up_m + body.half_height_m;
    const double capsule_bottom = capsule.center_up_m - capsule.half_height_m;
    const double capsule_top = capsule.center_up_m + capsule.half_height_m;
    if (std::min(body_top, capsule_top)
        - std::max(body_bottom, capsule_bottom) <= 0.0) {
        return std::nullopt;
    }

    const ObbBasis body_basis = make_basis(body.heading_rad);
    const CollisionVector2 capsule_from_body = subtract(
        capsule.center_enu, body.center_enu);
    const double local_forward = dot(capsule_from_body, body_basis.forward);
    const double local_right = dot(capsule_from_body, body_basis.right);
    const double clamped_forward = std::clamp(
        local_forward, -body.half_length_m, body.half_length_m);
    const double clamped_right = std::clamp(
        local_right, -body.half_width_m, body.half_width_m);
    const CollisionVector2 closest_body_point = add(
        body.center_enu,
        add(
            multiply(body_basis.forward, clamped_forward),
            multiply(body_basis.right, clamped_right)));
    const CollisionVector2 capsule_to_body = subtract(
        closest_body_point, capsule.center_enu);
    const double distance = std::hypot(
        capsule_to_body.east_m, capsule_to_body.north_m);

    if (distance > kAxisEpsilon) {
        const double penetration = capsule.radius_m - distance;
        if (penetration <= 0.0) {
            return std::nullopt;
        }
        return CollisionManifold{
            multiply(capsule_to_body, 1.0 / distance),
            closest_body_point,
            penetration};
    }

    // The circle centre lies inside or exactly on the OBB projection. Select
    // the nearest face, then move the body away from that face. Relative
    // approach velocity breaks geometrical ties before the fixed axis order.
    struct FaceCandidate {
        double distance;
        CollisionVector2 outward;
    };
    const std::array<FaceCandidate, 4> faces{{
        {body.half_length_m - local_forward, body_basis.forward},
        {body.half_length_m + local_forward,
         multiply(body_basis.forward, -1.0)},
        {body.half_width_m - local_right, body_basis.right},
        {body.half_width_m + local_right,
         multiply(body_basis.right, -1.0)},
    }};

    const FaceCandidate* selected = &faces[0];
    double selected_approach = dot(
        preferred_relative_velocity_enu,
        multiply(selected->outward, -1.0));
    for (std::size_t index = 1; index < faces.size(); ++index) {
        const double approach = dot(
            preferred_relative_velocity_enu,
            multiply(faces[index].outward, -1.0));
        if (faces[index].distance < selected->distance - kAxisEpsilon
            || (std::abs(faces[index].distance - selected->distance)
                    <= kAxisEpsilon
                && approach < selected_approach)) {
            selected = &faces[index];
            selected_approach = approach;
        }
    }

    return CollisionManifold{
        multiply(selected->outward, -1.0),
        add(capsule.center_enu,
            multiply(selected->outward, selected->distance)),
        capsule.radius_m + selected->distance};
}

CollisionWorld::CollisionWorld(
    std::vector<StaticObbCollider> static_colliders)
    : CollisionWorld(
        std::move(static_colliders), CollisionBroadPhaseMode::UniformGrid)
{
}

CollisionWorld::CollisionWorld(
    std::vector<StaticObbCollider> static_colliders,
    CollisionBroadPhaseMode broad_phase_mode)
    : static_colliders_(std::move(static_colliders))
    , broad_phase_mode_(broad_phase_mode)
{
    if (broad_phase_mode_ != CollisionBroadPhaseMode::UniformGrid
        && broad_phase_mode_
            != CollisionBroadPhaseMode::BruteForceReference) {
        throw std::invalid_argument("Collision broad-phase mode is invalid");
    }
    std::unordered_set<std::string> collider_ids;
    for (auto& collider : static_colliders_) {
        if (collider.collider_id.empty()) {
            throw std::invalid_argument("Static collider ID cannot be empty");
        }
        if (!collider_ids.insert(collider.collider_id).second) {
            throw std::invalid_argument(
                "Duplicate static collider ID: " + collider.collider_id);
        }
        if (!valid_semantic(collider.semantic) || !valid_shape(collider.shape)
            || !valid_material(collider.material)) {
            throw std::invalid_argument(
                "Static collider is invalid: " + collider.collider_id);
        }
        collider.shape.heading_rad = normalize_heading(
            collider.shape.heading_rad);
    }
    std::sort(
        static_colliders_.begin(), static_colliders_.end(),
        [](const StaticObbCollider& lhs, const StaticObbCollider& rhs) {
            return lhs.collider_id < rhs.collider_id;
        });
    static_grid_index_ = std::make_shared<const StaticGridIndex>(
        static_colliders_);
}

CollisionStepResult CollisionWorld::integrate(
    PlanarRigidBody body, double dt_seconds) const
{
    return integrate(std::move(body), dt_seconds, {});
}

CollisionStepResult CollisionWorld::integrate(
    PlanarRigidBody body,
    double dt_seconds,
    std::vector<KinematicCollisionProxy> dynamic_proxies) const
{
    return integrate_with_tire_supported_curbs(
        std::move(body), dt_seconds, std::move(dynamic_proxies), {});
}

CollisionStepResult CollisionWorld::integrate_with_tire_supported_curbs(
    PlanarRigidBody body,
    double dt_seconds,
    std::vector<KinematicCollisionProxy> dynamic_proxies,
    std::vector<std::string> tire_supported_curb_ids,
    std::vector<std::string> tire_supported_proxy_ids) const
{
    if (body.body_id.empty() || !valid_shape(body.shape)
        || !finite_vector(body.linear_velocity_enu_mps)
        || !std::isfinite(body.heading_rate_rad_s)
        || !std::isfinite(body.mass_kg) || body.mass_kg <= 0.0
        || !std::isfinite(body.yaw_inertia_kg_m2)
        || body.yaw_inertia_kg_m2 <= 0.0
        || !std::isfinite(dt_seconds) || dt_seconds <= 0.0
        || dt_seconds > 0.1) {
        throw std::invalid_argument("Planar collision step input is invalid");
    }

    body.shape.heading_rad = normalize_heading(body.shape.heading_rad);
    std::sort(tire_supported_curb_ids.begin(), tire_supported_curb_ids.end());
    tire_supported_curb_ids.erase(std::unique(
        tire_supported_curb_ids.begin(), tire_supported_curb_ids.end()),
        tire_supported_curb_ids.end());
    for (const auto& id : tire_supported_curb_ids) {
        const auto collider = std::lower_bound(
            static_colliders_.begin(), static_colliders_.end(), id,
            [](const StaticObbCollider& value, const std::string& key) {
                return value.collider_id < key;
            });
        if (collider == static_colliders_.end()
            || collider->collider_id != id
            || collider->semantic != StaticColliderSemantic::Curb) {
            throw std::invalid_argument(
                "Tire-supported collision bypass requires a Curb ID: " + id);
        }
    }
    validate_and_sort_dynamic_proxies(
        dynamic_proxies, static_colliders_, body.body_id);
    std::sort(tire_supported_proxy_ids.begin(), tire_supported_proxy_ids.end());
    tire_supported_proxy_ids.erase(std::unique(tire_supported_proxy_ids.begin(),
        tire_supported_proxy_ids.end()), tire_supported_proxy_ids.end());
    for (const auto& id : tire_supported_proxy_ids) {
        const auto proxy = std::lower_bound(dynamic_proxies.begin(), dynamic_proxies.end(), id,
            [](const KinematicCollisionProxy& p, const std::string& key) { return p.proxy_id < key; });
        if (proxy == dynamic_proxies.end() || proxy->proxy_id != id || !proxy->tire_support_candidate)
            throw std::invalid_argument("Tire support requires an eligible dynamic proxy ID: " + id);
    }
    const double requested_translation = std::hypot(
        body.linear_velocity_enu_mps.east_m * dt_seconds,
        body.linear_velocity_enu_mps.north_m * dt_seconds);
    const double requested_rotation = std::abs(
        body.heading_rate_rad_s * dt_seconds);
    double required_substeps = std::max({
        1.0,
        std::ceil(requested_translation / kMaximumTranslationPerSubstepM),
        std::ceil(requested_rotation / kMaximumRotationPerSubstepRad)});
    for (const auto& proxy : dynamic_proxies) {
        required_substeps = std::max(
            required_substeps,
            proxy_required_substeps(proxy, body, dt_seconds));
    }
    // Two proxies may approach one another faster than either approaches Ego.
    // Bound that relative movement too before using the shared microstep loop.
    for (std::size_t first = 0; first < dynamic_proxies.size(); ++first) {
        if (dynamic_proxies[first].mass_kg <= 0.0) continue;
        for (std::size_t second = first + 1; second < dynamic_proxies.size(); ++second) {
            if (dynamic_proxies[second].mass_kg <= 0.0) continue;
            const auto relative = subtract(dynamic_proxies[first].linear_velocity_enu_mps,
                dynamic_proxies[second].linear_velocity_enu_mps);
            required_substeps = std::max(required_substeps,
                std::ceil(std::hypot(relative.east_m, relative.north_m) * dt_seconds
                    / kMaximumTranslationPerSubstepM));
        }
    }

    CollisionStepResult result;
    result.motion_clamped = !std::isfinite(required_substeps)
        || required_substeps > static_cast<double>(kMaximumSubsteps);
    result.substep_count = result.motion_clamped
        ? kMaximumSubsteps
        : static_cast<std::size_t>(required_substeps);

    double integrated_dt = dt_seconds;
    if (result.motion_clamped) {
        integrated_dt = std::isfinite(required_substeps)
            ? dt_seconds * static_cast<double>(kMaximumSubsteps)
                / required_substeps
            : 0.0;
    }

    if (static_colliders_.empty() && dynamic_proxies.empty()) {
        body.shape.center_enu = add(
            body.shape.center_enu,
            multiply(body.linear_velocity_enu_mps, integrated_dt));
        body.shape.heading_rad = normalize_heading(
            body.shape.heading_rad + body.heading_rate_rad_s * integrated_dt);
        result.body = std::move(body);
        return result;
    }

    const double substep_dt = integrated_dt
        / static_cast<double>(result.substep_count);
    for (std::size_t substep = 0; substep < result.substep_count; ++substep) {
        body.shape.center_enu = add(
            body.shape.center_enu,
            multiply(body.linear_velocity_enu_mps, substep_dt));
        body.shape.heading_rad = normalize_heading(
            body.shape.heading_rad + body.heading_rate_rad_s * substep_dt);
        for (auto& proxy : dynamic_proxies) {
            advance_proxy(proxy, substep_dt);
        }
        std::vector<HorizontalAabb> dynamic_aabbs;
        dynamic_aabbs.reserve(dynamic_proxies.size());
        for (const auto& proxy : dynamic_proxies) {
            dynamic_aabbs.push_back(horizontal_aabb(proxy.shape));
        }
        const UniformGridIndex dynamic_grid(dynamic_aabbs);

        for (std::size_t iteration = 0; iteration < kSolverIterations; ++iteration) {
            bool overlap_found = false;
            const HorizontalAabb body_aabb = horizontal_aabb(body.shape);
            std::vector<std::size_t> static_candidates;
            if (!static_colliders_.empty()) {
                ++result.broad_phase_query_count;
                static_candidates = broad_phase_mode_
                        == CollisionBroadPhaseMode::UniformGrid
                    ? static_grid_index_->grid.query(body_aabb)
                    : all_indices(static_colliders_.size());
                result.narrow_phase_candidate_count +=
                    static_candidates.size();
            }
            for (const std::size_t collider_index : static_candidates) {
                const auto& collider = static_colliders_[collider_index];
                if (std::binary_search(
                        tire_supported_curb_ids.begin(),
                        tire_supported_curb_ids.end(),
                        collider.collider_id)) {
                    continue;
                }
                const auto manifold = intersect_obb_prisms(
                    body.shape,
                    collider.shape,
                    body.linear_velocity_enu_mps);
                if (!manifold) {
                    continue;
                }
                overlap_found = true;
                const double impulse = resolve_contact(
                    body, collider.material, {}, *manifold);
                record_contact(
                    result.contacts,
                    collider.collider_id,
                    *manifold,
                    impulse);
            }
            std::vector<std::size_t> dynamic_candidates;
            if (!dynamic_proxies.empty()) {
                ++result.broad_phase_query_count;
                const HorizontalAabb dynamic_body_aabb =
                    horizontal_aabb(body.shape);
                dynamic_candidates = broad_phase_mode_
                        == CollisionBroadPhaseMode::UniformGrid
                    ? dynamic_grid.query(dynamic_body_aabb)
                    : all_indices(dynamic_proxies.size());
                result.narrow_phase_candidate_count +=
                    dynamic_candidates.size();
            }
            for (const std::size_t proxy_index : dynamic_candidates) {
                auto& proxy = dynamic_proxies[proxy_index];
                if (proxy.supported_vehicle_id == body.body_id
                    || std::binary_search(tire_supported_proxy_ids.begin(),
                        tire_supported_proxy_ids.end(), proxy.proxy_id)) continue;
                const auto manifold = intersect_dynamic_proxy(body, proxy);
                if (!manifold) {
                    continue;
                }
                overlap_found = true;
                const double impulse = proxy.mass_kg > 0.0
                    ? resolve_finite_proxy_contact(body, proxy, *manifold)
                    : resolve_contact(
                        body,
                        proxy.material,
                        kinematic_contact_velocity(
                            proxy, manifold->contact_point_enu),
                        *manifold);
                record_contact(
                    result.contacts,
                    proxy.proxy_id,
                    *manifold,
                    impulse);
            }
            if (!overlap_found) {
                break;
            }
        }
        // Pair contacts use the same <=0.1m bounded microsteps as Ego. A fast
        // NPC crossing a pedestrian between tick endpoints cannot tunnel.
        auto pair_contacts = resolve_runtime_proxy_pairs(dynamic_proxies);
        result.runtime_proxy_contacts.insert(result.runtime_proxy_contacts.end(),
            std::make_move_iterator(pair_contacts.begin()), std::make_move_iterator(pair_contacts.end()));
    }

    result.body = std::move(body);
    result.resolved_dynamic_proxies = std::move(dynamic_proxies);
    return result;
}

std::vector<RuntimeProxyContact> resolve_runtime_proxy_pairs(
    std::vector<KinematicCollisionProxy>& proxies)
{
    validate_and_sort_dynamic_proxies(proxies, {}, "__runtime_pair_solver__");
    std::map<std::pair<std::string, std::string>, CollisionContact> contacts;
    for (int pass = 0; pass < 4; ++pass) {
        bool changed = false;
        for (std::size_t a = 0; a < proxies.size(); ++a) {
            if (proxies[a].mass_kg <= 0.0) continue;
            for (std::size_t b = a + 1; b < proxies.size(); ++b) {
                const auto anchored = [](const KinematicCollisionProxy& p) {
                    return p.breakaway_impulse_n_s > 0.0 && !p.breakaway_released;
                };
                if (anchored(proxies[a]) && anchored(proxies[b])) continue;
                const bool swap = anchored(proxies[a]);
                auto& first = proxies[swap ? b : a];
                auto& second = proxies[swap ? a : b];
                if (second.mass_kg <= 0.0) continue;
                if (first.supported_vehicle_id == second.proxy_id
                    || second.supported_vehicle_id == first.proxy_id) continue;
                const auto relative = subtract(first.linear_velocity_enu_mps, second.linear_velocity_enu_mps);
                auto manifold = std::visit([&](const auto& lhs, const auto& rhs)
                    -> std::optional<CollisionManifold> {
                    using L = std::decay_t<decltype(lhs)>;
                    using R = std::decay_t<decltype(rhs)>;
                    if constexpr (std::is_same_v<L, ObbPrism> && std::is_same_v<R, ObbPrism>) {
                        return intersect_obb_prisms(lhs, rhs, relative);
                    } else if constexpr (std::is_same_v<L, ObbPrism>) {
                        return intersect_obb_vertical_capsule(lhs, rhs, relative);
                    } else if constexpr (std::is_same_v<R, ObbPrism>) {
                        auto result = intersect_obb_vertical_capsule(rhs, lhs, multiply(relative, -1.0));
                        if (result) result->normal_enu = multiply(result->normal_enu, -1.0);
                        return result;
                    } else {
                        if (std::abs(lhs.center_up_m - rhs.center_up_m) >= lhs.half_height_m + rhs.half_height_m)
                            return std::nullopt;
                        const auto delta = subtract(lhs.center_enu, rhs.center_enu);
                        const double distance = std::hypot(delta.east_m, delta.north_m);
                        const double penetration = lhs.radius_m + rhs.radius_m - distance;
                        if (penetration <= 0.0) return std::nullopt;
                        const auto normal = distance > 1e-9 ? multiply(delta, 1.0 / distance)
                            : CollisionVector2{1.0, 0.0};
                        return CollisionManifold{normal,
                            add(rhs.center_enu, multiply(normal, rhs.radius_m - penetration * 0.5)), penetration};
                    }
                }, first.shape, second.shape);
                if (!manifold) continue;
                changed = true;
                const auto first_box = std::visit([](const auto& shape) -> ObbPrism {
                    if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, ObbPrism>) return shape;
                    else return {shape.center_enu, shape.center_up_m, 0.0,
                        shape.radius_m, shape.radius_m, shape.half_height_m};
                }, first.shape);
                PlanarRigidBody body{first.proxy_id, first_box, first.linear_velocity_enu_mps,
                    first.heading_rate_rad_s, first.mass_kg,
                    std::holds_alternative<ObbPrism>(first.shape) ? first.yaw_inertia_kg_m2 : 1e30};
                const double impulse = resolve_finite_proxy_contact(body, second, *manifold);
                std::visit([&](auto& shape) {
                    shape.center_enu = body.shape.center_enu;
                    if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, ObbPrism>)
                        shape.heading_rad = body.shape.heading_rad;
                }, first.shape);
                first.linear_velocity_enu_mps = body.linear_velocity_enu_mps;
                first.heading_rate_rad_s = std::holds_alternative<ObbPrism>(first.shape)
                    ? body.heading_rate_rad_s : 0.0;
                clamp_finite_proxy_speed(first);
                const auto accumulate = [&](const std::string& receiver, const std::string& other,
                                            CollisionVector2 normal) {
                    auto& contact = contacts[{receiver, other}];
                    contact.collider_id = other;
                    contact.normal_enu = normal;
                    contact.contact_point_enu = manifold->contact_point_enu;
                    contact.maximum_penetration_m = std::max(contact.maximum_penetration_m, manifold->penetration_m);
                    contact.accumulated_normal_impulse_n_s += impulse;
                };
                accumulate(first.proxy_id, second.proxy_id, manifold->normal_enu);
                accumulate(second.proxy_id, first.proxy_id, multiply(manifold->normal_enu, -1.0));
            }
        }
        if (!changed) break;
    }
    std::vector<RuntimeProxyContact> result;
    for (auto& [identity, contact] : contacts) result.push_back({identity.first, std::move(contact)});
    return result;
}

} // namespace simcore_host
