#pragma once

#include "collision/collision_types.hpp"
#include "terrain/ground_query.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace simcore_host {
// Only explicitly fallen bodies can become tire support. Buildings, upright
// people/poles and airborne bodies retain ordinary collision. This is a rounded
// reduced-body surface, not an extension of the baked map's vertical ray API.
inline bool is_low_tire_obstacle(const KinematicCollisionProxy& proxy,
    const GroundQuery& ground, double radius)
{
    const auto* shape = std::get_if<ObbPrism>(&proxy.shape);
    if (!proxy.tire_support_candidate || !shape || !std::isfinite(radius) || radius <= 0.0
        || !std::isfinite(shape->half_height_m) || shape->half_height_m <= 0.0
        || shape->half_height_m > radius || shape->half_width_m <= 0.0
        || shape->half_length_m < shape->half_width_m
        || std::hypot(proxy.linear_velocity_enu_mps.east_m,
                      proxy.linear_velocity_enu_mps.north_m) > 2.0) return false;
    const double s = std::sin(shape->heading_rad), c = std::cos(shape->heading_rad);
    // A long fallen pole must be supported along its whole length, not bridge a
    // hole or get classified from one conveniently low sample at its centre.
    for (const double offset : {-shape->half_length_m, 0.0, shape->half_length_m}) {
        const auto hit = ground.query_down({{shape->center_enu.east_m + offset*s,
            shape->center_enu.north_m + offset*c, shape->center_up_m + 2.0}, 4.0});
        if (!hit || !std::isfinite(hit->point_enu.up_m) || hit->normal_enu.up_m <= 0.0)
            return false;
        const double bottom = shape->center_up_m - shape->half_height_m;
        const double top = shape->center_up_m + shape->half_height_m;
        if (std::abs(bottom - hit->point_enu.up_m) > .06
            || top - hit->point_enu.up_m > 2.0*radius) return false;
    }
    return true;
}

inline std::optional<GroundHit> runtime_tire_contact(const GroundQueryRequest& request,
    const std::optional<GroundHit>& terrain, double radius,
    const std::vector<KinematicCollisionProxy>& supports)
{
    // A dynamic prop must never manufacture missing map coverage.
    if (!terrain) return terrain;
    auto result = terrain;
    double best_center_up = terrain->point_enu.up_m + radius;
    for (const auto& proxy : supports) {
        const auto& shape = std::get<ObbPrism>(proxy.shape);
        const double s = std::sin(shape.heading_rad), c = std::cos(shape.heading_rad);
        const double ex = request.origin_enu.east_m - shape.center_enu.east_m;
        const double ny = request.origin_enu.north_m - shape.center_enu.north_m;
        const double axis_half = std::max(0.0, shape.half_length_m - shape.half_width_m);
        const double along = std::clamp(ex*s + ny*c, -axis_half, axis_half);
        const double ax = shape.center_enu.east_m + along*s;
        const double ay = shape.center_enu.north_m + along*c;
        const double dx = request.origin_enu.east_m - ax;
        const double dy = request.origin_enu.north_m - ay;
        const double distance = std::hypot(dx,dy);
        const double width = shape.half_width_m, height = shape.half_height_m;
        if (distance >= width + radius) continue;
        // Minkowski contact of a circular tire and elliptical capsule surface.
        // Maximise wheel centre height, giving a continuous round approach and
        // exit, instead of teleporting a wheel onto a flat OBB top at its edge.
        double lo = std::max(0.0, distance-radius);
        double hi = std::min(width, distance+radius);
        const auto centre_height = [&](double u) {
            return shape.center_up_m + height*std::sqrt(std::max(0.0, 1.0-u*u/(width*width)))
                + std::sqrt(std::max(0.0, radius*radius-(distance-u)*(distance-u)));
        };
        for (int iteration=0; iteration<32; ++iteration) {
            const double left=(2.0*lo+hi)/3.0, right=(lo+2.0*hi)/3.0;
            if (centre_height(left) < centre_height(right)) lo=left; else hi=right;
        }
        const double u=(lo+hi)*.5;
        const double centre_up=centre_height(u);
        if (centre_up <= best_center_up + 1e-6) continue;
        const double horizontal_normal=(distance-u)/radius;
        const double vertical_normal=std::sqrt(std::max(0.0, 1.0-horizontal_normal*horizontal_normal));
        if (vertical_normal <= .02) continue;
        const double direction_x=distance>1e-9 ? dx/distance : 0.0;
        const double direction_y=distance>1e-9 ? dy/distance : 0.0;
        const double point_up=centre_up-radius*vertical_normal;
        const double ray_depth=request.origin_enu.up_m-point_up;
        if (ray_depth < 0.0 || ray_depth > request.max_distance_m) continue;
        result=GroundHit{{ax+u*direction_x,ay+u*direction_y,point_up},
            {direction_x*horizontal_normal,direction_y*horizontal_normal,vertical_normal},
            ray_depth,GroundSurfaceMaterialId::Default,std::clamp(proxy.material.friction/.8,.2,1.25)};
        best_center_up=centre_up;
    }
    return result;
}
} // namespace simcore_host
