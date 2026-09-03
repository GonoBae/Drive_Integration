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
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kRadians = std::numbers::pi_v<double> / 180.0;
constexpr double kDt = 1.0 / 60.0;
constexpr char kExpectedCollisionChecksum[] =
    "fnv1a64:7446108adad3e25b";

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

// drive_route.csv is an authoring-QA sidecar. It is intentionally not part of
// the authoritative collision payload loaded by the runtime.
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
            require(span > 0.001 && span <= 1.51,
                    "route checkpoints must be distinct and at most 1.5 m apart");
            point.distance = route.back().distance + span;
        }
        route.push_back(point);
    }
    require(saw_header, "drive_route.csv is missing its header");
    require(route.size() == 394,
            "signal city must retain exactly 394 QA route checkpoints");
    require(std::hypot(route.front().east + 80.0,
                       route.front().north - 40.0) < 0.01
                && std::abs(route.front().up) < 0.01
                && std::abs(route.front().heading - 90.0) < 0.01,
            "signal-city route must start eastbound at E=-80,N=40");
    require(std::hypot(route.back().east - route.front().east,
                       route.back().north - route.front().north) < 0.01,
            "signal-city route must close at its first checkpoint");
    require(std::abs(route.back().distance - 566.123) < 0.02,
            "394-point drive_route.csv must remain 566.123 m long");
    return route;
}

simcore_host::GroundHit query_surface(
    const simcore_host::GroundQuery& ground, double east, double north,
    const std::string& context)
{
    const auto hit = ground.query_down({{east, north, 30.0}, 60.0});
    require(hit.has_value(), context + " has no authored ground support at E="
                + std::to_string(east) + " N=" + std::to_string(north));
    require(std::isfinite(hit->point_enu.up_m)
                && std::isfinite(hit->normal_enu.east_m)
                && std::isfinite(hit->normal_enu.north_m)
                && std::isfinite(hit->normal_enu.up_m)
                && hit->normal_enu.up_m > 0.98,
            context + " must have a finite upward ground normal");
    require(std::isfinite(hit->friction_multiplier)
                && hit->friction_multiplier > 0.0,
            context + " must retain positive authored friction");
    return *hit;
}

void test_package_contract(
    const std::filesystem::path& directory,
    const simcore_host::RuntimeMapPackage& package)
{
    const auto manifest = simcore_host::load_map_package_manifest(directory);
    require(manifest.map_id == "signal_city_v2"
                && package.map_id == manifest.map_id,
            "wrong Signal City MapPackage ID");
    require(manifest.collision_checksum == kExpectedCollisionChecksum,
            "Signal City authored collision checksum changed unexpectedly");
    require(package.collision_checksum == manifest.collision_checksum,
            "runtime package and verified manifest checksum must agree");
    require(std::find(manifest.collision_files.begin(),
                      manifest.collision_files.end(), "drive_route.csv")
                == manifest.collision_files.end(),
            "QA route must not be an authoritative collision payload");
    require(package.ground_diagnostics.payload_kind
                == simcore_host::GroundPayloadKind::PackedHeightfieldV2,
            "Signal City must use its material-aware SIMGHF2 payload");
    require(package.ground_query && package.collision_world,
            "Signal City runtime package is missing collision data");

    std::size_t curb_count = 0;
    std::size_t wall_count = 0;
    std::size_t barrier_count = 0;
    std::set<std::string> collider_ids;
    std::set<std::string> building_ids;
    for (const auto& collider : package.collision_world->static_colliders()) {
        require(collider_ids.insert(collider.collider_id).second,
                "duplicate Signal City collider ID " + collider.collider_id);
        switch (collider.semantic) {
        case simcore_host::StaticColliderSemantic::Curb:
            ++curb_count;
            break;
        case simcore_host::StaticColliderSemantic::Wall:
            ++wall_count;
            require(collider.collider_id.starts_with("Building_"),
                    "Signal City Wall must be a Building_* collider, got "
                        + collider.collider_id);
            building_ids.insert(collider.collider_id);
            break;
        case simcore_host::StaticColliderSemantic::Barrier:
            ++barrier_count;
            break;
        }
    }
    require(package.collision_world->static_collider_count() == 49,
            "Signal City must retain exactly 49 static colliders");
    require(curb_count == 42, "Signal City must retain all 42 semantic curbs");
    require(wall_count == 7 && building_ids.size() == 7,
            "Signal City must retain exactly seven Building_* walls");
    for (int index = 0; index < 7; ++index) {
        const std::string expected = "Building_0" + std::to_string(index);
        require(building_ids.contains(expected),
                "missing Signal City wall " + expected);
    }
    require(barrier_count == 0,
            "Signal City perimeter is ground-covered by contract and must not add Barrier colliders");

    std::cout << "signal-city package: checksum=" << package.collision_checksum
              << " curbs=" << curb_count << " building_walls=" << wall_count
              << " perimeter_barriers=" << barrier_count << '\n';
}

void test_route_ground_and_tire_support(
    const simcore_host::RuntimeMapPackage& package,
    const std::vector<RoutePoint>& route,
    const VehicleParameters& parameters)
{
    const double front = parameters.wheelbase_m
        * (1.0 - parameters.front_static_load_fraction);
    const double rear = -parameters.wheelbase_m
        * parameters.front_static_load_fraction;
    const std::array<double, 4> longitudinal{front, front, rear, rear};
    const std::array<double, 4> lateral{
        -parameters.front_track_m * 0.5, parameters.front_track_m * 0.5,
        -parameters.rear_track_m * 0.5, parameters.rear_track_m * 0.5};
    double maximum_height_error = 0.0;

    for (std::size_t index = 0; index < route.size(); ++index) {
        const auto& point = route[index];
        const std::string context = "route checkpoint " + std::to_string(index);
        const auto centre = query_surface(*package.ground_query, point.east,
                                          point.north, context + " centre");
        maximum_height_error = std::max(maximum_height_error,
            std::abs(centre.point_enu.up_m - point.up));
        require(std::abs(centre.point_enu.up_m - point.up) <= 0.08,
                context + " differs from authored route height");
        require(centre.surface_material_id
                    == simcore_host::GroundSurfaceMaterialId::Asphalt,
                context + " centre must remain Asphalt");

        const double heading = point.heading * kRadians;
        for (std::size_t wheel = 0; wheel < longitudinal.size(); ++wheel) {
            const double east = point.east
                + std::sin(heading) * longitudinal[wheel]
                + std::cos(heading) * lateral[wheel];
            const double north = point.north
                + std::cos(heading) * longitudinal[wheel]
                - std::sin(heading) * lateral[wheel];
            const auto support = query_surface(*package.ground_query, east, north,
                context + " wheel " + std::to_string(wheel));
            require(std::abs(support.point_enu.up_m - centre.point_enu.up_m) <= 0.08,
                    context + " has discontinuous four-wheel support");
            require(support.surface_material_id
                        == simcore_host::GroundSurfaceMaterialId::Asphalt,
                    context + " wheel " + std::to_string(wheel)
                        + " must remain Asphalt");
        }
    }

    std::cout << "signal-city route support: checkpoints=" << route.size()
              << " wheel_queries=" << route.size() * 4
              << " length_m=" << route.back().distance
              << " max_height_error_m=" << maximum_height_error << '\n';
}

void test_every_curb_has_near_and_far_support(
    const simcore_host::RuntimeMapPackage& package,
    const VehicleParameters& parameters)
{
    const double maximum_supported_rise = parameters.tire_radius_m * 0.75;
    const double height_tolerance = parameters.tire_radius_m * 0.30;
    const double body_half_width =
        std::max(parameters.front_track_m, parameters.rear_track_m) * 0.5
        + 0.15;
    std::size_t curb_count = 0;
    std::size_t support_samples = 0;

    for (const auto& curb : package.collision_world->static_colliders()) {
        if (curb.semantic != simcore_host::StaticColliderSemantic::Curb) {
            continue;
        }
        ++curb_count;
        const double sine = std::sin(curb.shape.heading_rad);
        const double cosine = std::cos(curb.shape.heading_rad);
        const std::array<double, 2> forward{sine, cosine};
        const std::array<double, 2> right{cosine, -sine};
        const double near_offset = curb.shape.half_width_m
            + parameters.tire_radius_m;
        const double far_offset = curb.shape.half_width_m
            + parameters.tire_radius_m * 3.0;
        const double collider_height = curb.shape.half_height_m * 2.0;
        require(collider_height <= maximum_supported_rise + 1e-6,
                curb.collider_id + " exceeds the production curb-climb height gate");

        const double interval_min = std::max(
            -curb.shape.half_length_m, -body_half_width);
        const double interval_max = std::min(
            curb.shape.half_length_m, body_half_width);
        const double interval_span = interval_max - interval_min;
        const int segment_count = std::max(1, static_cast<int>(std::ceil(
            interval_span / parameters.tire_radius_m)));
        for (int sample = 0; sample <= segment_count; ++sample) {
            const double along = interval_min
                + interval_span * static_cast<double>(sample) / segment_count;
            const auto query = [&](double side, double offset,
                                   const std::string& label) {
                return query_surface(*package.ground_query,
                    curb.shape.center_enu.east_m + along * forward[0]
                        + side * offset * right[0],
                    curb.shape.center_enu.north_m + along * forward[1]
                        + side * offset * right[1],
                    curb.collider_id + " " + label);
            };
            const auto near_negative = query(-1.0, near_offset, "near-negative");
            const auto near_positive = query(1.0, near_offset, "near-positive");
            const auto far_negative = query(-1.0, far_offset, "far-negative");
            const auto far_positive = query(1.0, far_offset, "far-positive");
            const double near_delta = near_positive.point_enu.up_m
                - near_negative.point_enu.up_m;
            const double far_delta = far_positive.point_enu.up_m
                - far_negative.point_enu.up_m;
            const bool same_raised_side =
                (near_delta > 0.0 && far_delta > 0.0)
                || (near_delta < 0.0 && far_delta < 0.0);
            require(std::abs(near_delta) >= 0.02
                        && std::abs(near_delta)
                            <= maximum_supported_rise + height_tolerance + 1e-6,
                    curb.collider_id + " near support does not describe a climbable curb"
                        + " at local=" + std::to_string(along)
                        + " delta=" + std::to_string(near_delta));
            require(same_raised_side,
                    curb.collider_id + " near/far probes disagree on the raised side"
                        + " at local=" + std::to_string(along));
            require(std::abs(collider_height - std::abs(far_delta))
                        <= height_tolerance,
                    curb.collider_id + " far support does not match collider height"
                        + " at local=" + std::to_string(along)
                        + " delta=" + std::to_string(far_delta));
            ++support_samples;
        }
    }
    require(curb_count == 42,
            "near/far support test must exercise every one of the 42 curbs");
    std::cout << "signal-city curb support: curbs=" << curb_count
              << " perpendicular_samples=" << support_samples << '\n';
}

void test_all_building_walls_block_motion(
    const simcore_host::RuntimeMapPackage& package)
{
    std::size_t wall_count = 0;
    for (const auto& wall : package.collision_world->static_colliders()) {
        if (wall.semantic != simcore_host::StaticColliderSemantic::Wall) {
            continue;
        }
        ++wall_count;
        simcore_host::CollisionWorld isolated_world({wall});
        const double forward_east = std::sin(wall.shape.heading_rad);
        const double forward_north = std::cos(wall.shape.heading_rad);
        constexpr double body_half_length = 2.15;
        const double initial_distance = wall.shape.half_length_m
            + body_half_length + 5.0;
        simcore_host::PlanarRigidBody body;
        body.body_id = "signal-city-" + wall.collider_id + "-probe";
        body.shape = {{
            wall.shape.center_enu.east_m - forward_east * initial_distance,
            wall.shape.center_enu.north_m - forward_north * initial_distance},
            0.85, wall.shape.heading_rad, body_half_length, 0.94, 0.75};
        body.linear_velocity_enu_mps = {forward_east * 10.0,
                                        forward_north * 10.0};
        body.mass_kg = 1500.0;
        body.yaw_inertia_kg_m2 = 2850.0;
        bool saw_contact = false;
        for (int step = 0; step < 120; ++step) {
            const auto result = isolated_world.integrate(body, kDt);
            body = result.body;
            saw_contact = saw_contact || !result.contacts.empty();
            const double signed_distance =
                (body.shape.center_enu.east_m - wall.shape.center_enu.east_m)
                    * forward_east
                + (body.shape.center_enu.north_m - wall.shape.center_enu.north_m)
                    * forward_north;
            require(signed_distance
                        <= -wall.shape.half_length_m - body_half_length + 0.02,
                    wall.collider_id + " allowed frontal penetration");
            const auto overlap = simcore_host::intersect_obb_prisms(
                body.shape, wall.shape, body.linear_velocity_enu_mps);
            require(!overlap || overlap->penetration_m < 0.001,
                    wall.collider_id + " retained a penetrating final pose");
        }
        require(saw_contact, wall.collider_id + " never generated contact");
        const double normal_speed = body.linear_velocity_enu_mps.east_m
                * forward_east
            + body.linear_velocity_enu_mps.north_m * forward_north;
        require(normal_speed < 0.1,
                wall.collider_id + " did not stop inward wall motion");
    }
    require(wall_count == 7,
            "wall probe must exercise all seven Building_* colliders");
    std::cout << "signal-city wall probes: nonpenetrating_buildings="
              << wall_count << '\n';
}

void test_representative_24cm_curb_climb(
    const simcore_host::RuntimeMapPackage& package,
    const VehicleParameters& parameters)
{
    const auto& colliders = package.collision_world->static_colliders();
    const auto found = std::find_if(colliders.begin(), colliders.end(),
        [](const auto& collider) {
            return collider.collider_id == "CentralSouth_Curb_R";
        });
    require(found != colliders.end(),
            "missing representative CentralSouth_Curb_R");
    const auto& curb = *found;
    require(curb.semantic == simcore_host::StaticColliderSemantic::Curb,
            "CentralSouth_Curb_R must retain Curb semantics");
    require(std::abs(curb.shape.center_enu.east_m - 5.15) < 0.01
                && std::abs(curb.shape.center_enu.north_m - 8.50) < 0.01
                && std::abs(curb.shape.heading_rad) < 0.001
                && std::abs(curb.shape.half_height_m * 2.0 - 0.24) < 0.01,
            "CentralSouth_Curb_R must retain its authored 24 cm geometry");

    // N=0 intersects the long curb and is reachable from VehiclePhysics's
    // normal east-facing origin without pose or velocity injection.
    constexpr double crossing_north = 0.0;
    require(std::abs(crossing_north - curb.shape.center_enu.north_m)
                < curb.shape.half_length_m - 1.0,
            "representative origin crossing left the curb's authored length");
    const double near_offset = curb.shape.half_width_m
        + parameters.tire_radius_m;
    const double far_offset = curb.shape.half_width_m
        + parameters.tire_radius_m * 3.0;
    const auto near_west = query_surface(*package.ground_query,
        curb.shape.center_enu.east_m - near_offset, crossing_north,
        "CentralSouth_Curb_R west support");
    const auto near_east = query_surface(*package.ground_query,
        curb.shape.center_enu.east_m + near_offset, crossing_north,
        "CentralSouth_Curb_R east support");
    const auto far_west = query_surface(*package.ground_query,
        curb.shape.center_enu.east_m - far_offset, crossing_north,
        "CentralSouth_Curb_R far-west support");
    const auto far_east = query_surface(*package.ground_query,
        curb.shape.center_enu.east_m + far_offset, crossing_north,
        "CentralSouth_Curb_R far-east support");
    const double near_delta = near_east.point_enu.up_m
        - near_west.point_enu.up_m;
    const double far_delta = far_east.point_enu.up_m
        - far_west.point_enu.up_m;
    require(near_delta > 0.10
                && near_delta <= parameters.tire_radius_m * 1.05 + 1e-6
                && far_delta > 0.10
                && std::abs(far_delta - curb.shape.half_height_m * 2.0)
                    <= parameters.tire_radius_m * 0.30,
            "CentralSouth_Curb_R must rise eastward through production support probes");

    VehiclePhysics vehicle(0.0, 0.0, 0.0, 90.f, parameters,
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
    double final_rear_support_height = near_west.point_enu.up_m;
    bool crossed = false;

    for (int step = 0; step < 600; ++step) {
        state = vehicle.update(kDt);
        require(std::isfinite(state.east) && std::isfinite(state.north)
                    && std::isfinite(state.position_enu.z)
                    && std::isfinite(state.speed) && std::isfinite(state.pitch),
                "representative curb climb published a non-finite state");
        require(std::abs(state.north) < 0.10,
                "straight eastbound curb approach created lateral motion");
        maximum_vertical_step = std::max(maximum_vertical_step,
            std::abs(state.position_enu.z - previous_chassis_height));
        previous_chassis_height = state.position_enu.z;
        maximum_chassis_height = std::max(maximum_chassis_height,
                                           state.position_enu.z);
        maximum_abs_pitch = std::max(maximum_abs_pitch,
                                     std::abs(static_cast<double>(state.pitch)));
        if (state.east < curb.shape.center_enu.east_m - 2.30) {
            maximum_approach_speed = std::max(
                maximum_approach_speed, static_cast<double>(state.speed));
        }
        const auto contacts = static_cast<std::size_t>(std::count_if(
            state.wheels.begin(), state.wheels.end(),
            [](const WheelState& wheel) { return wheel.in_contact; }));
        minimum_contacts = std::min(minimum_contacts, contacts);
        if (std::all_of(state.wheels.begin(), state.wheels.end(),
                        [](const WheelState& wheel) { return wheel.in_contact; })) {
            const double front_height = 0.5 * (
                state.wheels[0].contact_point_enu.z
                + state.wheels[1].contact_point_enu.z);
            const double rear_height = 0.5 * (
                state.wheels[2].contact_point_enu.z
                + state.wheels[3].contact_point_enu.z);
            maximum_front_over_rear_support = std::max(
                maximum_front_over_rear_support, front_height - rear_height);
            saw_front_axle_on_sidewalk = saw_front_axle_on_sidewalk
                || front_height >= near_east.point_enu.up_m - 0.05;
            saw_rear_axle_on_sidewalk = saw_rear_axle_on_sidewalk
                || rear_height >= near_east.point_enu.up_m - 0.05;
            final_rear_support_height = rear_height;
        }
        crossed = crossed
            || state.east > curb.shape.center_enu.east_m + 0.50;
        if (state.east > curb.shape.center_enu.east_m + 2.60) {
            break;
        }
    }

    std::cout << "signal-city 24cm curb climb: approach_mps="
              << maximum_approach_speed
              << " support_rise_m=" << near_delta
              << " axle_split_m=" << maximum_front_over_rear_support
              << " chassis_rise_m="
              << maximum_chassis_height - initial_chassis_height
              << " max_vertical_step_m=" << maximum_vertical_step
              << " max_abs_pitch_deg=" << maximum_abs_pitch
              << " min_tire_contacts=" << minimum_contacts << '\n';
    require(maximum_approach_speed > 2.0,
            "representative curb test did not reach a meaningful approach speed");
    require(crossed,
            "production VehiclePhysics did not climb CentralSouth_Curb_R");
    require(saw_front_axle_on_sidewalk && saw_rear_axle_on_sidewalk
                && maximum_front_over_rear_support > 0.08,
            "front and rear tire support must rise sequentially over the curb");
    require(std::abs(final_rear_support_height - near_east.point_enu.up_m) < 0.10,
            "rear tires did not finish on sidewalk-height support");
    require(maximum_chassis_height > initial_chassis_height + 0.08,
            "chassis did not follow the raised tire support");
    require(minimum_contacts >= 2 && maximum_abs_pitch < 35.0,
            "curb climb lost bounded tire support or chassis pitch");
    require(maximum_vertical_step < 0.08,
            "curb climb teleported the chassis vertically");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc <= 2,
                "usage: signal_city_course_tests [MapPackage directory]");
        const std::filesystem::path directory = argc == 2
            ? std::filesystem::path(argv[1])
            : std::filesystem::path(SIMCORE_TEST_SIGNAL_CITY_MAP_PACKAGE_PATH);
        if (!std::filesystem::exists(directory)) {
            std::cout << "signal_city_course_tests: SKIPPED; generate Signal City "
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
        run("package/checksum/static contract", [&] {
            test_package_contract(directory, package);
        });
        run("394-point route and four-tire support", [&] {
            test_route_ground_and_tire_support(package, route, parameters);
        });
        run("all-curb near/far support", [&] {
            test_every_curb_has_near_and_far_support(package, parameters);
        });
        run("seven Building wall probes", [&] {
            test_all_building_walls_block_motion(package);
        });
        run("representative 24 cm curb climb", [&] {
            test_representative_24cm_curb_climb(package, parameters);
        });
        if (failures > 0) {
            std::cerr << "signal_city_course_tests: " << failures
                      << " test groups failed\n";
            return 1;
        }
        std::cout << "signal_city_course_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "signal_city_course_tests: " << error.what() << '\n';
        return 1;
    }
}
