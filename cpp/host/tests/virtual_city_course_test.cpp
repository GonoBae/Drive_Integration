#include "collision/collision_world.hpp"
#include "physics/vehicle_config.hpp"
#include "physics/vehicle_physics.hpp"
#include "terrain/map_package_manifest.hpp"
#include "terrain/map_package_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kRadians = std::numbers::pi_v<double> / 180.0;
constexpr double kDt = 1.0 / 60.0;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct RoutePoint {
    double east = 0.0;
    double north = 0.0;
    double up = 0.0;
    double heading = 0.0;
    double distance = 0.0;
};

// This route is a QA sidecar, NOT an authoritative runtime collision payload
// and NOT a production LaneGraph or autonomous driving implementation.
std::vector<RoutePoint> load_route(const std::filesystem::path& directory)
{
    std::ifstream input(directory / "drive_route.csv");
    require(input.good(), "missing drive_route.csv QA sidecar");
    std::vector<RoutePoint> route;
    std::string line;
    bool saw_header = false;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (!saw_header) {
            require(line == "east_m,north_m,up_m,heading_deg",
                    "drive_route.csv has an unexpected header");
            saw_header = true;
            continue;
        }
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream fields(line);
        RoutePoint point;
        require(static_cast<bool>(fields >> point.east >> point.north
                                  >> point.up >> point.heading),
                "invalid route point at line " + std::to_string(line_number));
        std::string extra;
        require(!(fields >> extra), "extra route column at line "
                    + std::to_string(line_number));
        require(std::isfinite(point.east) && std::isfinite(point.north)
                    && std::isfinite(point.up) && std::isfinite(point.heading),
                "non-finite route point at line " + std::to_string(line_number));
        require(point.heading >= 0.0 && point.heading <= 360.0,
                "route heading must use navigation degrees");
        if (!route.empty()) {
            const double span = std::hypot(point.east - route.back().east,
                                           point.north - route.back().north);
            require(span > 0.001 && span <= 2.01,
                    "route must have distinct checkpoints at most 2 m apart");
            point.distance = route.back().distance + span;
        }
        route.push_back(point);
    }
    require(saw_header && route.size() >= 200,
            "route must describe a complete dense city loop");
    require(std::hypot(route.front().east, route.front().north) < 0.01
                && std::abs(route.front().up) < 0.01
                && std::abs(route.front().heading - 90.0) < 0.01,
            "route must begin at the configured east-facing origin spawn");
    require(std::hypot(route.back().east - route.front().east,
                       route.back().north - route.front().north) < 0.01,
            "route must close at its spawn checkpoint");
    require(route.back().distance > 600.0 && route.back().distance < 700.0,
            "route must cover the full planned city block, not a short stub");
    return route;
}

simcore_host::GroundHit query_surface(
    const simcore_host::GroundQuery& ground, double east, double north,
    const std::string& context)
{
    const auto hit = ground.query_down({{east, north, 30.0}, 60.0});
    require(hit.has_value(), context + " has no authored ground coverage at E="
                + std::to_string(east) + " N=" + std::to_string(north));
    require(std::isfinite(hit->point_enu.up_m)
                && std::isfinite(hit->normal_enu.east_m)
                && std::isfinite(hit->normal_enu.north_m)
                && std::isfinite(hit->normal_enu.up_m)
                && hit->normal_enu.up_m > 0.98,
            context + " must have a finite upward drivable ground normal");
    require(std::isfinite(hit->friction_multiplier)
                && hit->friction_multiplier > 0.0,
            context + " must retain a positive terrain friction multiplier");
    return *hit;
}

simcore_host::ObbPrism sedan_body(
    const VehicleState& state, const VehicleParameters& parameters)
{
    // Match VehiclePhysics's current conservative sedan collision envelope.
    return {{state.east, state.north},
            state.position_enu.z - parameters.cg_height_m + 0.10 + 0.75,
            state.heading * kRadians,
            parameters.wheelbase_m * 0.5 + 0.80,
            std::max(parameters.front_track_m, parameters.rear_track_m) * 0.5
                + 0.15,
            0.75};
}

std::vector<RoutePoint> dense_coverage_route(const std::vector<RoutePoint>& route)
{
    std::vector<RoutePoint> dense;
    for (std::size_t index = 0; index + 1 < route.size(); ++index) {
        const auto& start = route[index];
        const auto& end = route[index + 1];
        const int count = static_cast<int>(std::ceil(
            (end.distance - start.distance) / 0.25));
        const double heading_delta = std::remainder(end.heading - start.heading, 360.0);
        for (int sample = 0; sample < count; ++sample) {
            const double alpha = static_cast<double>(sample) / count;
            dense.push_back({start.east + alpha * (end.east - start.east),
                             start.north + alpha * (end.north - start.north),
                             start.up + alpha * (end.up - start.up),
                             start.heading + alpha * heading_delta,
                             start.distance + alpha * (end.distance - start.distance)});
        }
    }
    dense.push_back(route.back());
    return dense;
}

void test_package_and_route(
    const std::filesystem::path& directory,
    const simcore_host::RuntimeMapPackage& package,
    const std::vector<RoutePoint>& route,
    const VehicleParameters& parameters)
{
    const auto manifest = simcore_host::load_map_package_manifest(directory);
    require(package.map_id == "virtual_city_v1", "wrong city MapPackage ID");
    require(package.collision_checksum == manifest.collision_checksum,
            "runtime package and verified manifest identities must agree");
    require(std::find(manifest.collision_files.begin(),
                      manifest.collision_files.end(), "drive_route.csv")
                == manifest.collision_files.end(),
            "QA route must not be advertised as a server collision payload");
    require(package.ground_diagnostics.payload_kind
                == simcore_host::GroundPayloadKind::PackedHeightfieldV2,
            "virtual city must use the material-aware SIMGHF2 export");
    require(package.collision_world
                && package.collision_world->static_collider_count() > 0,
            "city must export actual static collision markers");
    const double front = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double rear = -parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const std::array<double, 4> longitudinal{front, front, rear, rear};
    const std::array<double, 4> lateral{
        -parameters.front_track_m * 0.5, parameters.front_track_m * 0.5,
        -parameters.rear_track_m * 0.5, parameters.rear_track_m * 0.5};
    // Metadata checkpoints are <=2 m apart; validate the spaces BETWEEN them
    // too, so narrow road-slab seams cannot hide between sampled tire poses.
    const auto coverage_route = dense_coverage_route(route);
    double maximum_height_error = 0.0;
    double maximum_ground_height = 0.0;
    for (std::size_t index = 0; index < coverage_route.size(); ++index) {
        const auto& point = coverage_route[index];
        const auto context = "route coverage sample " + std::to_string(index)
            + " progress=" + std::to_string(point.distance);
        const auto centre = query_surface(*package.ground_query, point.east,
                                          point.north, context);
        maximum_height_error = std::max(maximum_height_error,
            std::abs(centre.point_enu.up_m - point.up));
        maximum_ground_height = std::max(maximum_ground_height,
                                         centre.point_enu.up_m);
        require(std::abs(centre.point_enu.up_m - point.up) <= 0.08,
                context + " ground differs from intended road height by "
                    + std::to_string(centre.point_enu.up_m - point.up) + " m");
        require(centre.surface_material_id
                    == simcore_host::GroundSurfaceMaterialId::Asphalt,
                context + " driving surface must be exported as Asphalt");
        VehicleState intended_pose;
        intended_pose.east = point.east;
        intended_pose.north = point.north;
        intended_pose.position_enu.z = centre.point_enu.up_m
            + parameters.cg_height_m;
        intended_pose.heading = static_cast<float>(point.heading);
        const auto intended_body = sedan_body(intended_pose, parameters);
        for (const auto& obstacle : package.collision_world->static_colliders()) {
            const auto overlap = simcore_host::intersect_obb_prisms(
                intended_body, obstacle.shape);
            require(!overlap || overlap->penetration_m < 0.001,
                    context + " sedan envelope overlaps collider "
                        + obstacle.collider_id);
        }
        const double heading = point.heading * kRadians;
        for (std::size_t wheel = 0; wheel < longitudinal.size(); ++wheel) {
            const double east = point.east
                + std::sin(heading) * longitudinal[wheel]
                + std::cos(heading) * lateral[wheel];
            const double north = point.north
                + std::cos(heading) * longitudinal[wheel]
                - std::sin(heading) * lateral[wheel];
            const auto hit = query_surface(*package.ground_query, east, north,
                context + " wheel " + std::to_string(wheel));
            require(std::abs(hit.point_enu.up_m - centre.point_enu.up_m) < 0.25,
                    context + " tire footprint crosses a discontinuous road");
            require(hit.surface_material_id
                        == simcore_host::GroundSurfaceMaterialId::Asphalt,
                    context + " tire footprint must remain on the road");
        }
    }
    require(maximum_ground_height > 1.35,
            "route must actually include the raised 4-degree grade");
    const auto& bounds = package.ground_diagnostics.bounds_enu;
    const auto& diagnostics = package.ground_diagnostics;
    require(diagnostics.sample_count == 401u * 481u
                && std::min(diagnostics.lattice_columns, diagnostics.lattice_rows) == 401u
                && std::max(diagnostics.lattice_columns, diagnostics.lattice_rows) == 481u,
            "city export must retain the full nominal 240 x 200 m / 0.5 m lattice");
    // Unreal's primitive-box trace omits the two exact E=+/-120 m faces.
    // Distinguish that documented 0.5 m outer sample margin from the nominal
    // lattice/site size. Boundary walls stop a sedan BEFORE its wheels reach
    // this margin; test_boundary_wall_ground_support checks all four walls.
    require(std::abs(bounds.minimum.east_m + 119.5) < 0.01
                && std::abs(bounds.maximum.east_m - 119.5) < 0.01
                && std::abs(bounds.minimum.north_m + 20.0) < 0.01
                && std::abs(bounds.maximum.north_m - 180.0) < 0.01,
            "city valid ground bounds must remain E[-119.5,119.5], N[-20,180] m");
    std::cout << "city geometry: checkpoints=" << route.size()
              << " footprint_samples=" << coverage_route.size()
              << " length_m=" << route.back().distance
              << " max_height_error_m=" << maximum_height_error
              << " valid_ground_span_m=" << bounds.east_span_m()
              << 'x' << bounds.north_span_m()
              << " colliders=" << package.collision_world->static_collider_count()
              << " checksum=" << package.collision_checksum << '\n';
}

void test_spawn_and_reverse(
    const simcore_host::RuntimeMapPackage& package,
    const VehicleParameters& parameters)
{
    VehiclePhysics vehicle(0.0, 0.0, 0.0, 90.f, parameters,
                           package.ground_query, package.collision_world);
    const auto initial = vehicle.get_state();
    const auto body = sedan_body(initial, parameters);
    for (const auto& obstacle : package.collision_world->static_colliders()) {
        const auto collision = simcore_host::intersect_obb_prisms(body,
                                                                  obstacle.shape);
        require(!collision || collision->penetration_m < 0.001,
                "spawn overlaps static collider " + obstacle.collider_id);
    }
    VehicleInput hold;
    hold.brake = 1.f;
    hold.handbrake = true;
    vehicle.set_input(hold);
    for (int step = 0; step < 600; ++step) {
        const auto state = vehicle.update(kDt);
        require(std::hypot(state.east, state.north) < 0.01,
                "braked spawn must remain stationary");
        require(std::abs(state.pitch) < 1.0 && std::abs(state.roll) < 1.0,
                "flat spawn must not tip or roll while stopped");
        require(std::all_of(state.wheels.begin(), state.wheels.end(),
                            [](const WheelState& wheel) {
                                return wheel.in_contact
                                    && std::abs(wheel.angular_speed) < 0.01;
                            }),
                "stationary spawn requires four supported stationary tires");
    }
    VehicleInput reverse;
    reverse.gear = VehicleGear::Reverse;
    reverse.throttle = 0.30f;
    vehicle.set_input(reverse);
    VehicleState state;
    for (int step = 0; step < 300; ++step) {
        state = vehicle.update(kDt);
    }
    require(state.east < -2.0 && std::abs(state.north) < 0.05,
            "reverse must travel backwards on the authored spawn road");
    vehicle.set_input(hold);
    for (int step = 0; step < 300; ++step) {
        state = vehicle.update(kDt);
    }
    require(std::abs(state.speed) < 0.01,
            "service brake must bring the reversing city vehicle to rest");
    vehicle.reset();
    state = vehicle.get_state();
    require(std::hypot(state.east, state.north) < 0.001
                && std::abs(state.heading - 90.f) < 0.001,
            "reset must return to the city origin and east-facing heading");
}

void test_high_throttle_climbs_authored_south_curb(
    const simcore_host::RuntimeMapPackage& package,
    const VehicleParameters& parameters)
{
    const auto& colliders = package.collision_world->static_colliders();
    const auto found = std::find_if(colliders.begin(), colliders.end(),
        [](const auto& collider) {
            return collider.collider_id == "Curb_South_1";
        });
    require(found != colliders.end(),
            "city package must export the north edge of the south-road curb");
    const auto& curb = *found;
    require(curb.semantic == simcore_host::StaticColliderSemantic::Curb,
            "Curb_South_1 must retain the fail-closed Curb semantic");
    require(std::abs(curb.shape.center_enu.east_m) < 0.01
                && std::abs(curb.shape.center_enu.north_m - 6.15) < 0.01
                && std::abs(curb.shape.half_height_m * 2.0 - 0.24) < 0.01,
            "Curb_South_1 must retain its authored 24 cm city geometry");

    const auto surface_at = [&](double east, double north,
                                const std::string& context) {
        const auto hit = package.ground_query->query_down(
            {{east, north, 30.0}, 60.0});
        require(hit.has_value(), context + " is missing baked ground support");
        require(std::isfinite(hit->point_enu.up_m)
                    && std::isfinite(hit->normal_enu.up_m)
                    && hit->normal_enu.up_m > 0.90,
                context + " must provide finite upward wheel support");
        return *hit;
    };

    // The selected curb runs east/west. Its north side is the raised sidewalk,
    // while the south side is the spawn road. Sample both broad authored
    // surfaces as well as the near/far support pairs used by VehiclePhysics at
    // this representative along-curb location.
    const auto road = surface_at(
        curb.shape.center_enu.east_m,
        curb.shape.center_enu.north_m - 1.0,
        "Curb_South_1 road side");
    const auto sidewalk = surface_at(
        curb.shape.center_enu.east_m,
        curb.shape.center_enu.north_m + 1.0,
        "Curb_South_1 sidewalk side");
    const double authored_rise = sidewalk.point_enu.up_m - road.point_enu.up_m;
    require(authored_rise > 0.10
                && authored_rise <= parameters.tire_radius_m * 0.75 + 1e-6,
            "south sidewalk must be a continuous, tire-climbable raised support");

    const double sine = std::sin(curb.shape.heading_rad);
    const double cosine = std::cos(curb.shape.heading_rad);
    const double right_east = cosine;
    const double right_north = -sine;
    require(std::abs(right_east) < 0.01 && right_north < -0.99,
            "Curb_South_1 must remain east/west with its raised side to north");
    const double near_support_offset = curb.shape.half_width_m
        + parameters.tire_radius_m;
    const auto near_north_support = surface_at(
        curb.shape.center_enu.east_m - right_east * near_support_offset,
        curb.shape.center_enu.north_m - right_north * near_support_offset,
        "Curb_South_1 north eligibility probe");
    const auto near_south_support = surface_at(
        curb.shape.center_enu.east_m + right_east * near_support_offset,
        curb.shape.center_enu.north_m + right_north * near_support_offset,
        "Curb_South_1 south eligibility probe");
    const double near_support_delta =
        near_north_support.point_enu.up_m
        - near_south_support.point_enu.up_m;
    const double support_rise = std::abs(near_support_delta);
    const double far_support_offset = curb.shape.half_width_m
        + parameters.tire_radius_m * 3.0;
    const auto far_north_support = surface_at(
        curb.shape.center_enu.east_m - right_east * far_support_offset,
        curb.shape.center_enu.north_m - right_north * far_support_offset,
        "Curb_South_1 north far-support probe");
    const auto far_south_support = surface_at(
        curb.shape.center_enu.east_m + right_east * far_support_offset,
        curb.shape.center_enu.north_m + right_north * far_support_offset,
        "Curb_South_1 south far-support probe");
    const double far_support_delta =
        far_north_support.point_enu.up_m
        - far_south_support.point_enu.up_m;
    const bool far_support_rises_on_same_side =
        (near_support_delta > 0.0 && far_support_delta > 0.0)
        || (near_support_delta < 0.0 && far_support_delta < 0.0);
    const double collider_height = curb.shape.half_height_m * 2.0;
    require(support_rise >= 0.02
                && support_rise <= parameters.tire_radius_m * (0.75 + 0.30) + 1e-6
                && collider_height
                    <= parameters.tire_radius_m * 0.75 + 1e-6
                && far_support_rises_on_same_side
                && std::abs(collider_height - std::abs(far_support_delta))
                    <= parameters.tire_radius_m * 0.30,
            "baked south curb must satisfy the production tire-support gate");

    VehiclePhysics vehicle(0.0, 0.0, 0.0, 0.f, parameters,
                           package.ground_query, package.collision_world);
    VehicleInput input;
    input.throttle = 1.f;
    vehicle.set_input(input);

    VehicleState state = vehicle.get_state();
    const double initial_chassis_height = state.position_enu.z;
    double previous_chassis_height = initial_chassis_height;
    double maximum_vertical_step = 0.0;
    double maximum_approach_speed = 0.0;
    double maximum_front_over_rear_support = 0.0;
    double maximum_chassis_height = initial_chassis_height;
    double maximum_abs_pitch = 0.0;
    std::size_t minimum_contacts = state.wheels.size();
    bool saw_front_axle_on_sidewalk = false;
    bool saw_rear_axle_on_sidewalk = false;
    double final_rear_support_height = road.point_enu.up_m;
    bool crossed = false;
    for (int step = 0; step < 600; ++step) {
        state = vehicle.update(kDt);
        require(std::isfinite(state.east) && std::isfinite(state.north)
                    && std::isfinite(state.position_enu.z)
                    && std::isfinite(state.speed)
                    && std::isfinite(state.pitch),
                "authored curb climb must keep the vehicle state finite");
        require(std::abs(state.east) < 0.10,
                "straight curb approach must not create lateral motion");
        maximum_vertical_step = std::max(maximum_vertical_step,
            std::abs(state.position_enu.z - previous_chassis_height));
        previous_chassis_height = state.position_enu.z;
        maximum_chassis_height = std::max(maximum_chassis_height,
                                           state.position_enu.z);
        maximum_abs_pitch = std::max(maximum_abs_pitch,
                                     std::abs(static_cast<double>(state.pitch)));
        if (state.north < curb.shape.center_enu.north_m - 2.30) {
            maximum_approach_speed = std::max(
                maximum_approach_speed, static_cast<double>(state.speed));
        }
        const auto contacts = static_cast<std::size_t>(std::count_if(
            state.wheels.begin(), state.wheels.end(),
            [](const WheelState& wheel) { return wheel.in_contact; }));
        minimum_contacts = std::min(minimum_contacts, contacts);
        if (state.wheels[0].in_contact && state.wheels[1].in_contact
            && state.wheels[2].in_contact && state.wheels[3].in_contact) {
            const double front_height = 0.5 * (
                state.wheels[0].contact_point_enu.z
                + state.wheels[1].contact_point_enu.z);
            const double rear_height = 0.5 * (
                state.wheels[2].contact_point_enu.z
                + state.wheels[3].contact_point_enu.z);
            maximum_front_over_rear_support = std::max(
                maximum_front_over_rear_support, front_height - rear_height);
            saw_front_axle_on_sidewalk = saw_front_axle_on_sidewalk
                || front_height >= sidewalk.point_enu.up_m - 0.05;
            saw_rear_axle_on_sidewalk = saw_rear_axle_on_sidewalk
                || rear_height >= sidewalk.point_enu.up_m - 0.05;
            final_rear_support_height = rear_height;
        }
        if (state.north > curb.shape.center_enu.north_m + 0.50) {
            crossed = true;
        }
        // The sidewalk is only 2.5 m wide. Continue far enough for the rear
        // axle to leave the curb raster transition and reach its 24 cm plateau,
        // but stop before the whole vehicle runs off the outer edge.
        if (state.north > curb.shape.center_enu.north_m + 2.60) {
            break;
        }
    }

    // A normal planar OBB solve stops this sedan's centre near N=3.85 m.
    // Reaching north of the 6.15 m marker therefore proves the real package's
    // curb was conditionally released and climbed through wheel/suspension
    // support, without a test-side pose or velocity injection.
    std::cout << "authored south curb climb: approach_mps="
              << maximum_approach_speed
              << " support_rise_m=" << support_rise
              << " axle_split_m=" << maximum_front_over_rear_support
              << " chassis_rise_m="
              << maximum_chassis_height - initial_chassis_height
              << " max_vertical_step_m=" << maximum_vertical_step
              << " max_abs_pitch_deg=" << maximum_abs_pitch
              << " min_tire_contacts=" << minimum_contacts << '\n';
    require(maximum_approach_speed > 4.0,
            "curb regression must exercise a meaningful approach speed");
    require(crossed,
            "high throttle must carry the production vehicle over Curb_South_1");
    require(saw_front_axle_on_sidewalk && saw_rear_axle_on_sidewalk
                && maximum_front_over_rear_support > 0.08,
            "front and rear suspension support must rise sequentially onto the sidewalk");
    require(std::abs(final_rear_support_height - sidewalk.point_enu.up_m) < 0.10,
            "rear axle must finish the observed crossing on sidewalk-height support");
    require(maximum_chassis_height > initial_chassis_height + 0.08,
            "chassis height must follow the raised wheel support");
    require(minimum_contacts >= 2 && maximum_abs_pitch < 35.0,
            "curb climb must retain bounded tire support and chassis pitch");
    require(maximum_vertical_step < 0.08,
            "curb climb must remain continuous rather than teleporting the chassis");
}

void test_authored_curb_perpendicular_footprints_have_expected_support(
    const simcore_host::RuntimeMapPackage& package,
    const VehicleParameters& parameters)
{
    std::vector<std::string> ineligible_curbs;
    std::vector<std::string> ineligible_details;
    std::size_t curb_count = 0;
    std::size_t eligible_count = 0;
    const double maximum_supported_rise = parameters.tire_radius_m * 0.75;
    const double maximum_height_delta_error =
        parameters.tire_radius_m * 0.30;
    // This is VehiclePhysics' exact along-curb footprint when a stationary
    // sedan is centred on and perpendicular to a curb. Production samples the
    // same interval at no more than one tire radius apart.
    const double perpendicular_body_half_extent =
        std::max(parameters.front_track_m, parameters.rear_track_m) * 0.5
        + 0.15;

    for (const auto& curb : package.collision_world->static_colliders()) {
        if (curb.semantic != simcore_host::StaticColliderSemantic::Curb) {
            continue;
        }
        ++curb_count;
        const double sine = std::sin(curb.shape.heading_rad);
        const double cosine = std::cos(curb.shape.heading_rad);
        const double forward_east = sine;
        const double forward_north = cosine;
        const double right_east = cosine;
        const double right_north = -sine;
        const auto query_support = [&](double local_forward,
                                       double side, double offset) {
            return package.ground_query->query_down({{
                curb.shape.center_enu.east_m
                    + local_forward * forward_east
                    + side * right_east * offset,
                curb.shape.center_enu.north_m
                    + local_forward * forward_north
                    + side * right_north * offset,
                30.0}, 60.0});
        };
        const auto valid_support = [](const auto& hit) {
            return hit
                && std::isfinite(hit->distance_m)
                && hit->distance_m >= 0.0
                && hit->distance_m <= 60.0
                && std::isfinite(hit->point_enu.up_m)
                && std::isfinite(hit->normal_enu.up_m)
                && hit->normal_enu.up_m > 0.0;
        };

        const double near_offset = curb.shape.half_width_m
            + parameters.tire_radius_m;
        const double far_offset = curb.shape.half_width_m
            + parameters.tire_radius_m * 3.0;
        const double collider_height = curb.shape.half_height_m * 2.0;
        std::string failure_detail;
        const auto perpendicular_footprint_is_eligible =
            [&](double centre_local_forward) {
            bool eligible = collider_height
                <= maximum_supported_rise + 1e-6;
            const double interval_min = std::max(
                -curb.shape.half_length_m,
                centre_local_forward - perpendicular_body_half_extent);
            const double interval_max = std::min(
                curb.shape.half_length_m,
                centre_local_forward + perpendicular_body_half_extent);
            const double interval_span = interval_max - interval_min;
            const int segment_count = std::max(
                1,
                static_cast<int>(std::ceil(
                    interval_span / parameters.tire_radius_m)));
            for (int sample = 0;
                 eligible && sample <= segment_count;
                 ++sample) {
                const double local_forward = interval_min
                    + interval_span * static_cast<double>(sample)
                        / segment_count;
                const auto near_negative = query_support(
                    local_forward, -1.0, near_offset);
                const auto near_positive = query_support(
                    local_forward, 1.0, near_offset);
                const auto far_negative = query_support(
                    local_forward, -1.0, far_offset);
                const auto far_positive = query_support(
                    local_forward, 1.0, far_offset);
                if (!valid_support(near_negative) || !valid_support(near_positive)
                    || !valid_support(far_negative) || !valid_support(far_positive)) {
                    failure_detail = "missing support at local="
                        + std::to_string(local_forward);
                    eligible = false;
                    break;
                }
                const double near_delta = near_positive->point_enu.up_m
                    - near_negative->point_enu.up_m;
                const double far_delta = far_positive->point_enu.up_m
                    - far_negative->point_enu.up_m;
                const double near_rise = std::abs(near_delta);
                const bool far_rises_on_same_side =
                    (near_delta > 0.0 && far_delta > 0.0)
                    || (near_delta < 0.0 && far_delta < 0.0);
                eligible = near_rise >= 0.02
                    && near_rise <= maximum_supported_rise
                        + maximum_height_delta_error + 1e-6
                    && far_rises_on_same_side
                    && std::abs(collider_height - std::abs(far_delta))
                        <= maximum_height_delta_error;
                if (!eligible) {
                    failure_detail = "local=" + std::to_string(local_forward)
                        + " near_delta=" + std::to_string(near_delta)
                        + " far_delta=" + std::to_string(far_delta)
                        + " collider_height=" + std::to_string(collider_height);
                }
            }
            return eligible;
        };
        const bool eligible = perpendicular_footprint_is_eligible(0.0);
        if (eligible) {
            ++eligible_count;
        } else {
            ineligible_curbs.push_back(curb.collider_id);
            ineligible_details.push_back(
                curb.collider_id + "{" + failure_detail + "}");
        }
    }

    require(curb_count == 215,
            "virtual city must retain all 215 authored semantic curbs");
    std::ostringstream ineligible_summary;
    for (std::size_t index = 0; index < ineligible_curbs.size(); ++index) {
        if (index > 0) {
            ineligible_summary << ',';
        }
        ineligible_summary << ineligible_curbs[index];
    }
    std::ostringstream detail_summary;
    for (std::size_t index = 0; index < ineligible_details.size(); ++index) {
        if (index > 0) {
            detail_summary << ';';
        }
        detail_summary << ineligible_details[index];
    }
    require(eligible_count == 214
                && ineligible_curbs.size() == 1
                && ineligible_curbs.front() == "Curb_BayEnd",
            "214 road/sidewalk curb perpendicular footprints must satisfy the support contract"
                "; eligible=" + std::to_string(eligible_count)
                + ", ineligible=" + ineligible_summary.str()
                + ", details=" + detail_summary.str());
    const auto bay_barrier = std::find_if(
        package.collision_world->static_colliders().begin(),
        package.collision_world->static_colliders().end(),
        [](const auto& collider) {
            return collider.collider_id == "Barrier_BayEnd";
        });
    require(bay_barrier != package.collision_world->static_colliders().end()
                && bay_barrier->semantic
                    == simcore_host::StaticColliderSemantic::Barrier
                && std::hypot(
                       bay_barrier->shape.center_enu.east_m,
                       bay_barrier->shape.center_enu.north_m - 172.15) < 1.0,
            "the sole non-sidewalk bay-end curb must remain backed by its Barrier");
    std::cout << "city curb footprint contract: semantic_curbs=" << curb_count
              << " road_sidewalk_perpendicular_footprints_eligible=" << eligible_count
              << " intentional_barrier_backed_exceptions="
              << ineligible_curbs.size() << '\n';
}

void test_curb_support_is_scoped_to_the_swept_local_location(
    const simcore_host::RuntimeMapPackage& package,
    const VehicleParameters& parameters)
{
    const auto& colliders = package.collision_world->static_colliders();
    const auto find_curb = [&](const char* id) -> const auto& {
        const auto found = std::find_if(
            colliders.begin(), colliders.end(),
            [&](const auto& collider) { return collider.collider_id == id; });
        require(found != colliders.end()
                    && found->semantic
                        == simcore_host::StaticColliderSemantic::Curb,
                std::string("missing location-scoped curb ") + id);
        return *found;
    };
    const auto eligible_at = [&](const auto& curb, double local_forward) {
        const double sine = std::sin(curb.shape.heading_rad);
        const double cosine = std::cos(curb.shape.heading_rad);
        const std::array<double, 2> forward{sine, cosine};
        const std::array<double, 2> right{cosine, -sine};
        local_forward = std::clamp(
            local_forward,
            -curb.shape.half_length_m,
            curb.shape.half_length_m);
        const double base_east = curb.shape.center_enu.east_m
            + forward[0] * local_forward;
        const double base_north = curb.shape.center_enu.north_m
            + forward[1] * local_forward;
        const auto query = [&](double side, double offset) {
            return package.ground_query->query_down({{
                base_east + side * right[0] * offset,
                base_north + side * right[1] * offset,
                30.0}, 60.0});
        };
        const auto valid = [](const auto& hit) {
            return hit && std::isfinite(hit->point_enu.up_m)
                && std::isfinite(hit->normal_enu.up_m)
                && hit->normal_enu.up_m > 0.0;
        };
        const double near_offset = curb.shape.half_width_m
            + parameters.tire_radius_m;
        const auto near_negative = query(-1.0, near_offset);
        const auto near_positive = query(1.0, near_offset);
        const double far_offset = curb.shape.half_width_m
            + parameters.tire_radius_m * 3.0;
        const auto far_negative = query(-1.0, far_offset);
        const auto far_positive = query(1.0, far_offset);
        if (!valid(near_negative) || !valid(near_positive)
            || !valid(far_negative) || !valid(far_positive)) {
            return false;
        }
        const double near_delta = near_positive->point_enu.up_m
            - near_negative->point_enu.up_m;
        const double far_delta = far_positive->point_enu.up_m
            - far_negative->point_enu.up_m;
        const double near_rise = std::abs(near_delta);
        const bool far_rises_on_same_side =
            (near_delta > 0.0 && far_delta > 0.0)
            || (near_delta < 0.0 && far_delta < 0.0);
        const double maximum_rise = parameters.tire_radius_m * 0.75;
        const double collider_height = curb.shape.half_height_m * 2.0;
        return near_rise >= 0.02
            && near_rise <= maximum_rise
                + parameters.tire_radius_m * 0.30 + 1e-6
            && collider_height <= maximum_rise + 1e-6
            && far_rises_on_same_side
            && std::abs(collider_height - std::abs(far_delta))
                <= parameters.tire_radius_m * 0.30;
    };

    struct NorthOpeningContract {
        const char* collider_id;
        double supported_endpoint;
        double opening_endpoint;
    };
    const std::array<NorthOpeningContract, 4> north_openings{{
        {"Curb_North_-1_-1", -35.0, 35.0},
        {"Curb_North_-1_1", 35.0, -35.0},
        {"Curb_North_1_-1", -35.0, 35.0},
        {"Curb_North_1_1", 35.0, -35.0},
    }};
    for (const auto& contract : north_openings) {
        const auto& curb = find_curb(contract.collider_id);
        require(std::abs(curb.shape.half_length_m - 35.0) < 0.01,
                std::string(contract.collider_id)
                    + " must retain its finite 70 m marker");
        require(eligible_at(curb, 0.0)
                    && eligible_at(curb, contract.supported_endpoint)
                    && !eligible_at(curb, contract.opening_endpoint),
                std::string(contract.collider_id)
                    + " must authorize its centre/opposite endpoint but fail closed at its T-junction opening");
    }

    const auto& uniform_south_curb = find_curb("Curb_South_1");
    require(eligible_at(uniform_south_curb, -76.0)
                && eligible_at(uniform_south_curb, 0.0)
                && eligible_at(uniform_south_curb, 30.0)
                && eligible_at(uniform_south_curb, 76.0),
            "uniform south curb support must remain eligible across endpoints and interior");
}

RoutePoint point_at(const std::vector<RoutePoint>& route, double distance)
{
    distance = std::fmod(distance, route.back().distance);
    const auto upper = std::upper_bound(route.begin(), route.end(), distance,
        [](double value, const RoutePoint& point) {
            return value < point.distance;
        });
    if (upper == route.end()) {
        return route.front();
    }
    const auto& before = *(upper - 1);
    const double alpha = (distance - before.distance)
        / (upper->distance - before.distance);
    return {before.east + alpha * (upper->east - before.east),
            before.north + alpha * (upper->north - before.north),
            before.up + alpha * (upper->up - before.up), 0.0, distance};
}

struct Projection {
    double progress = 0.0;
    double error = std::numeric_limits<double>::infinity();
    std::size_t segment = 0;
};

Projection project_near(
    const std::vector<RoutePoint>& route, std::size_t segment,
    double east, double north)
{
    Projection best;
    const std::size_t first = segment > 2 ? segment - 2 : 0;
    const std::size_t last = std::min(route.size() - 2, segment + 12);
    for (std::size_t index = first; index <= last; ++index) {
        const auto& a = route[index];
        const auto& b = route[index + 1];
        const double dx = b.east - a.east;
        const double dy = b.north - a.north;
        const double alpha = std::clamp(((east - a.east) * dx
            + (north - a.north) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
        const double error = std::hypot(east - (a.east + alpha * dx),
                                        north - (a.north + alpha * dy));
        if (error < best.error) {
            best = {a.distance + alpha * (b.distance - a.distance),
                    error, index};
        }
    }
    return best;
}

void test_full_lap(
    const simcore_host::RuntimeMapPackage& package,
    const std::vector<RoutePoint>& route,
    const VehicleParameters& parameters)
{
    VehiclePhysics vehicle(0.0, 0.0, 0.0, 90.f, parameters,
                           package.ground_query, package.collision_world);
    VehicleState state = vehicle.get_state();
    std::size_t segment = 0;
    double progress = 0.0;
    double maximum_lateral_error = 0.0;
    double minimum_clearance = std::numeric_limits<double>::infinity();
    double maximum_ground_height = 0.0;
    double maximum_pitch = 0.0;
    double minimum_pitch = 0.0;
    double maximum_roll = 0.0;
    std::size_t minimum_contacts = 4;
    int elapsed_steps = 0;
    bool completed = false;
    // A small fixed-speed pure-pursuit DRIVER is used only as a repeatable QA
    // input source. It never teleports, resets, edits velocity or injects poses;
    // all motion, tire contacts and static response use production physics.
    for (; elapsed_steps < 24000; ++elapsed_steps) {
        const auto projection = project_near(route, segment, state.east,
                                             state.north);
        segment = projection.segment;
        progress = projection.progress;
        maximum_lateral_error = std::max(maximum_lateral_error, projection.error);
        require(projection.error < 1.0,
                "city lap left its 4 m lane; progress=" + std::to_string(progress)
                    + " m cross_track=" + std::to_string(projection.error)
                    + " m E=" + std::to_string(state.east)
                    + " N=" + std::to_string(state.north));
        if (progress >= route.back().distance - 0.5
            && std::hypot(state.east, state.north) < 1.0) {
            completed = true;
            break;
        }
        constexpr double lookahead = 5.0;
        const auto target = point_at(route, progress + lookahead);
        const double heading = state.heading * kRadians;
        const double rear_offset = parameters.wheelbase_m
            * parameters.front_static_load_fraction;
        const double rear_east = state.east - std::sin(heading) * rear_offset;
        const double rear_north = state.north - std::cos(heading) * rear_offset;
        const double dx = target.east - rear_east;
        const double dy = target.north - rear_north;
        const double left = -std::cos(heading) * dx + std::sin(heading) * dy;
        const double angle = std::atan2(2.0 * parameters.wheelbase_m * left,
                                        dx * dx + dy * dy);
        VehicleInput input;
        input.steering = static_cast<float>(std::clamp(
            angle / parameters.max_steering_angle_rad, -1.0, 1.0));
        input.throttle = static_cast<float>(std::clamp(
            0.10 + 0.45 * (4.0 - state.speed), 0.0, 0.55));
        input.brake = static_cast<float>(std::clamp(
            0.30 * (state.speed - 4.35), 0.0, 0.5));
        vehicle.set_input(input);
        state = vehicle.update(kDt);
        require(std::isfinite(state.east) && std::isfinite(state.north)
                    && std::isfinite(state.position_enu.z)
                    && std::isfinite(state.pitch) && std::isfinite(state.roll)
                    && std::isfinite(state.speed),
                "city lap published a non-finite physical state");
        const auto centre = query_surface(*package.ground_query, state.east,
                                          state.north, "driven vehicle");
        const double clearance = state.position_enu.z - centre.point_enu.up_m;
        minimum_clearance = std::min(minimum_clearance, clearance);
        maximum_ground_height = std::max(maximum_ground_height,
                                         centre.point_enu.up_m);
        maximum_pitch = std::max(maximum_pitch, static_cast<double>(state.pitch));
        minimum_pitch = std::min(minimum_pitch, static_cast<double>(state.pitch));
        maximum_roll = std::max(maximum_roll, std::abs(static_cast<double>(state.roll)));
        const auto contacts = static_cast<std::size_t>(std::count_if(
            state.wheels.begin(), state.wheels.end(),
            [](const WheelState& wheel) { return wheel.in_contact; }));
        minimum_contacts = std::min(minimum_contacts, contacts);
        require(clearance > 0.15 && clearance < 1.1,
                "city lap body lost sensible ground clearance at progress="
                    + std::to_string(progress));
        require(contacts >= 2,
                "city lap lost a solvable tire footprint at progress="
                    + std::to_string(progress));
        require(std::abs(state.pitch) < 15.0 && std::abs(state.roll) < 15.0,
                "city lap tipped on the gentle authored road");
    }
    require(completed,
            "city lap driver timed out (400 simulated seconds); progress="
                + std::to_string(progress) + " of "
                + std::to_string(route.back().distance) + " m; speed="
                + std::to_string(state.speed));
    require(maximum_ground_height > 1.35
                && maximum_pitch > 2.0 && minimum_pitch < -2.0,
            "full lap must drive both uphill and downhill with body response");
    VehicleInput brake;
    brake.brake = 1.f;
    vehicle.set_input(brake);
    for (int step = 0; step < 300; ++step) {
        state = vehicle.update(kDt);
    }
    require(std::abs(state.speed) < 0.01,
            "full-lap vehicle must stop under the service brake");
    std::cout << "city physical lap: simulated_s=" << elapsed_steps * kDt
              << " progress_m=" << progress
              << " max_cross_track_m=" << maximum_lateral_error
              << " min_clearance_m=" << minimum_clearance
              << " min_tire_contacts=" << minimum_contacts
              << " pitch_deg=[" << minimum_pitch << ',' << maximum_pitch << ']'
              << " max_abs_roll_deg=" << maximum_roll << '\n';
}

void test_exported_marker_blocks_motion(
    const simcore_host::RuntimeMapPackage& package)
{
    // Isolate one REAL exported marker to avoid confusing its response with
    // neighbouring curb/building markers. The full-lap test uses the full world.
    const auto& obstacle = package.collision_world->static_colliders().front();
    simcore_host::CollisionWorld isolated_world({obstacle});
    const double heading = obstacle.shape.heading_rad;
    const double forward_east = std::sin(heading);
    const double forward_north = std::cos(heading);
    constexpr double half_length = 2.15;
    const double initial_distance = obstacle.shape.half_length_m
        + half_length + 5.0;
    simcore_host::PlanarRigidBody body;
    body.body_id = "city-exported-marker-impact-probe";
    body.shape = {{obstacle.shape.center_enu.east_m - forward_east * initial_distance,
                   obstacle.shape.center_enu.north_m - forward_north * initial_distance},
                  obstacle.shape.center_up_m, heading, half_length, 0.94, 0.75};
    body.linear_velocity_enu_mps = {forward_east * 10.0, forward_north * 10.0};
    body.mass_kg = 1500.0;
    body.yaw_inertia_kg_m2 = 2850.0;
    bool saw_contact = false;
    for (int step = 0; step < 120; ++step) {
        auto result = isolated_world.integrate(body, kDt);
        saw_contact = saw_contact || !result.contacts.empty();
        body = result.body;
        const double signed_distance =
            (body.shape.center_enu.east_m - obstacle.shape.center_enu.east_m)
                * forward_east
            + (body.shape.center_enu.north_m - obstacle.shape.center_enu.north_m)
                * forward_north;
        require(signed_distance <= -obstacle.shape.half_length_m - half_length + 0.02,
                "motion probe penetrated an exported static marker");
    }
    require(saw_contact, "exported static marker must generate physical contact");
    require(std::hypot(body.linear_velocity_enu_mps.east_m,
                       body.linear_velocity_enu_mps.north_m) < 0.1,
            "exported static marker must stop the frontal impact probe");
}

void test_boundary_wall_ground_support(
    const simcore_host::RuntimeMapPackage& package,
    const VehicleParameters& parameters)
{
    struct BoundaryCase {
        const char* id;
        double outward_east;
        double outward_north;
    };
    constexpr std::array<BoundaryCase, 4> cases{{
        {"Boundary_EastWest_1", 1.0, 0.0},
        {"Boundary_EastWest_-1", -1.0, 0.0},
        {"Boundary_NorthSouth_1", 0.0, 1.0},
        {"Boundary_NorthSouth_-1", 0.0, -1.0}}};
    const auto& colliders = package.collision_world->static_colliders();
    const double front = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double rear = -parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const std::array<double, 4> longitudinal{front, front, rear, rear};
    const std::array<double, 4> lateral{
        -parameters.front_track_m * 0.5, parameters.front_track_m * 0.5,
        -parameters.rear_track_m * 0.5, parameters.rear_track_m * 0.5};
    for (const auto& scenario : cases) {
        const auto found = std::find_if(colliders.begin(), colliders.end(),
            [&](const auto& collider) { return collider.collider_id == scenario.id; });
        require(found != colliders.end(),
                std::string("missing outer boundary wall ") + scenario.id);
        const auto& wall = *found;
        const double heading = std::atan2(scenario.outward_east,
                                          scenario.outward_north);
        simcore_host::PlanarRigidBody body;
        body.body_id = "city-boundary-ground-support-probe";
        body.shape.center_enu = {
            wall.shape.center_enu.east_m - 6.5 * scenario.outward_east,
            wall.shape.center_enu.north_m - 6.5 * scenario.outward_north};
        // Avoid the parking bay when approaching the north/south wall.
        if (scenario.outward_north != 0.0) {
            body.shape.center_enu.east_m = 60.0;
        }
        const auto start_ground = query_surface(*package.ground_query,
            body.shape.center_enu.east_m, body.shape.center_enu.north_m,
            scenario.id);
        body.shape.center_up_m = start_ground.point_enu.up_m + 0.85;
        body.shape.heading_rad = heading;
        body.shape.half_length_m = parameters.wheelbase_m * 0.5 + 0.80;
        body.shape.half_width_m = std::max(parameters.front_track_m,
                                          parameters.rear_track_m) * 0.5 + 0.15;
        body.shape.half_height_m = 0.75;
        body.mass_kg = parameters.mass_kg;
        body.yaw_inertia_kg_m2 = parameters.yaw_inertia_kg_m2;
        body.linear_velocity_enu_mps = {scenario.outward_east * 10.0,
                                        scenario.outward_north * 10.0};
        bool saw_boundary_contact = false;
        for (int step = 0; step < 120; ++step) {
            const auto result = package.collision_world->integrate(body, kDt);
            body = result.body;
            saw_boundary_contact = saw_boundary_contact || std::any_of(
                result.contacts.begin(), result.contacts.end(),
                [&](const auto& contact) { return contact.collider_id == scenario.id; });
            const double wall_distance =
                (body.shape.center_enu.east_m - wall.shape.center_enu.east_m)
                    * scenario.outward_east
                + (body.shape.center_enu.north_m - wall.shape.center_enu.north_m)
                    * scenario.outward_north;
            require(wall_distance <= -wall.shape.half_width_m
                        - body.shape.half_length_m + 0.02,
                    std::string(scenario.id) + " allowed sedan-envelope penetration");
            const double sine = std::sin(body.shape.heading_rad);
            const double cosine = std::cos(body.shape.heading_rad);
            query_surface(*package.ground_query, body.shape.center_enu.east_m,
                           body.shape.center_enu.north_m, scenario.id);
            for (std::size_t wheel = 0; wheel < longitudinal.size(); ++wheel) {
                query_surface(*package.ground_query,
                    body.shape.center_enu.east_m + sine * longitudinal[wheel]
                        + cosine * lateral[wheel],
                    body.shape.center_enu.north_m + cosine * longitudinal[wheel]
                        - sine * lateral[wheel],
                    std::string(scenario.id) + " approach wheel " + std::to_string(wheel));
            }
        }
        require(saw_boundary_contact,
                std::string(scenario.id) + " must stop the approach before ground loss");
        // The corner-contact manifold may leave lateral motion or rebound at
        // an off-centre hit. This support test requires blocked OUTWARD motion,
        // not an unphysical clamp of every tangential velocity component.
        const double outward_speed = body.linear_velocity_enu_mps.east_m
            * scenario.outward_east + body.linear_velocity_enu_mps.north_m
            * scenario.outward_north;
        require(outward_speed < 0.1,
                std::string(scenario.id) + " must block outward boundary motion; vE="
                    + std::to_string(body.linear_velocity_enu_mps.east_m)
                    + " vN=" + std::to_string(body.linear_velocity_enu_mps.north_m)
                    + " yaw=" + std::to_string(body.shape.heading_rad));
        const double final_energy = 0.5 * body.mass_kg
            * (body.linear_velocity_enu_mps.east_m * body.linear_velocity_enu_mps.east_m
               + body.linear_velocity_enu_mps.north_m * body.linear_velocity_enu_mps.north_m)
            + 0.5 * body.yaw_inertia_kg_m2
                * body.heading_rate_rad_s * body.heading_rate_rad_s;
        require(final_energy <= 0.5 * body.mass_kg * 100.0 * 1.01,
                std::string(scenario.id) + " must not add impact kinetic energy");
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc <= 2, "usage: virtual_city_course_tests [MapPackage directory]");
        const std::filesystem::path directory = argc == 2
            ? std::filesystem::path(argv[1])
            : std::filesystem::path(SIMCORE_TEST_VIRTUAL_CITY_MAP_PACKAGE_PATH);
        if (!std::filesystem::exists(directory)) {
            std::cout << "virtual_city_course_tests: SKIPPED; generate and Bake "
                      << directory.string() << " first\n";
            return 77;
        }
        const auto parameters = simcore_host::load_vehicle_parameters(
            SIMCORE_TEST_VEHICLE_CONFIG_PATH).parameters;
        const auto package = simcore_host::load_runtime_map_package(directory);
        const auto route = load_route(directory);
        std::cout << std::fixed << std::setprecision(4);
        int failures = 0;
        const auto run = [&failures](const char* name, const auto& test) {
            try {
                test();
                std::cout << name << ": passed\n";
            } catch (const std::exception& error) {
                ++failures;
                std::cerr << name << ": FAILED: " << error.what() << '\n';
            }
        };
        run("package/route geometry", [&] {
            test_package_and_route(directory, package, route, parameters);
        });
        run("spawn/reverse/brake/reset", [&] {
            test_spawn_and_reverse(package, parameters);
        });
        run("authored high-throttle curb climb", [&] {
            test_high_throttle_climbs_authored_south_curb(package, parameters);
        });
        run("authored curb full-footprint support", [&] {
            test_authored_curb_perpendicular_footprints_have_expected_support(
                package, parameters);
        });
        run("location-scoped curb support", [&] {
            test_curb_support_is_scoped_to_the_swept_local_location(
                package, parameters);
        });
        run("exported marker impact", [&] {
            test_exported_marker_blocks_motion(package);
        });
        run("boundary walls retain tire ground coverage", [&] {
            test_boundary_wall_ground_support(package, parameters);
        });
        run("input-only full lap", [&] {
            test_full_lap(package, route, parameters);
        });
        if (failures > 0) {
            std::cerr << "virtual_city_course_tests: " << failures
                      << " test groups failed\n";
            return 1;
        }
        std::cout << "virtual_city_course_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "virtual_city_course_tests: " << error.what() << '\n';
        return 1;
    }
}
