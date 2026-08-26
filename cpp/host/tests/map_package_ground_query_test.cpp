#include "terrain/map_package_ground_query.hpp"

#include "terrain/map_package_manifest.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef SIMCORE_TEST_MAP_PACKAGE_PATH
#error SIMCORE_TEST_MAP_PACKAGE_PATH must identify the tracked test MapPackage
#endif

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryPackage {
public:
    TemporaryPackage()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        path = std::filesystem::temp_directory_path()
            / ("simcore-map-package-" + unique);
        std::filesystem::create_directories(path);
    }

    ~TemporaryPackage()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    TemporaryPackage(const TemporaryPackage&) = delete;
    TemporaryPackage& operator=(const TemporaryPackage&) = delete;

    std::filesystem::path path;
};

void test_tracked_map_package_bootstrap_loads()
{
    const auto ground = simcore_host::MapPackageGroundQuery::load(
        SIMCORE_TEST_MAP_PACKAGE_PATH);
    require(ground.triangle_count() == 2,
            "bootstrap MapPackage must load its two ground triangles");
    require(ground.package_id() == "wall_broad_v1",
            "tracked MapPackage must publish its manifest identity");
    require(ground.collision_checksum().starts_with("fnv1a64:")
            && ground.collision_checksum().size() == 24,
            "tracked MapPackage must publish a verified collision checksum");
    const auto hit = ground.query_down({{0.0, 0.0, 0.55}, 1.0});
    require(hit.has_value(), "bootstrap MapPackage must cover the spawn origin");
    require(hit->point_enu.up_m == 0.0 && hit->distance_m == 0.55,
            "bootstrap MapPackage must publish its authoritative height");
    require(hit->normal_enu.east_m == 0.0
            && hit->normal_enu.north_m == 0.0
            && hit->normal_enu.up_m == 1.0,
            "bootstrap MapPackage must publish an ENU-up normal");
}

void test_modified_collision_payload_is_rejected()
{
    TemporaryPackage package;
    const auto source = std::filesystem::path(SIMCORE_TEST_MAP_PACKAGE_PATH);
    std::filesystem::copy_file(
        source / "manifest.cfg", package.path / "manifest.cfg");
    std::filesystem::copy_file(
        source / "ground_surface.csv", package.path / "ground_surface.csv");
    {
        std::ofstream output(
            package.path / "ground_surface.csv", std::ios::binary | std::ios::app);
        output << "# tampered after manifest generation\n";
    }

    bool rejected = false;
    try {
        (void)simcore_host::MapPackageGroundQuery::load(package.path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected,
            "a collision payload modified after manifest generation must fail closed");
}

void test_missing_manifest_and_unsafe_payload_path_are_rejected()
{
    {
        TemporaryPackage package;
        std::ofstream(package.path / "ground_surface.csv")
            << "surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2\n";
        bool rejected = false;
        try {
            (void)simcore_host::MapPackageGroundQuery::load(package.path);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "a MapPackage without manifest.cfg must fail closed");
    }

    {
        TemporaryPackage package;
        std::ofstream(package.path / "manifest.cfg")
            << "format_version=1\n"
               "map_id=unsafe\n"
               "coordinate_frame=map_enu\n"
               "collision_files=../ground_surface.csv\n"
               "collision_checksum=fnv1a64:0000000000000000\n";
        bool rejected = false;
        try {
            (void)simcore_host::load_map_package_manifest(package.path);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected,
                "manifest collision payloads must not escape the package directory");
    }
}

void test_sloped_triangle_interpolates_height_and_normal()
{
    simcore_host::GroundTriangle slope{
        "grade",
        {{{-10.0, -10.0, -0.5}, {10.0, -10.0, -0.5}, {0.0, 10.0, 0.5}}}};
    simcore_host::MapPackageGroundQuery ground({slope});
    const auto hit = ground.query_down({{0.0, 0.0, 2.0}, 3.0});
    require(hit.has_value(), "vertical ray must hit the sloped triangle");
    require(std::abs(hit->point_enu.up_m) < 1e-12,
            "triangle height must use barycentric interpolation");
    require(hit->normal_enu.north_m < 0.0 && hit->normal_enu.up_m > 0.0,
            "triangle normal must preserve the uphill direction");
    const double length = std::hypot(
        hit->normal_enu.east_m,
        hit->normal_enu.north_m,
        hit->normal_enu.up_m);
    require(std::abs(length - 1.0) < 1e-12,
            "MapPackage provider must normalize the ground normal");
}

void test_nearest_surface_wins_and_bounds_are_enforced()
{
    const simcore_host::GroundTriangle lower{
        "lower", {{{-1.0, -1.0, 0.0}, {1.0, -1.0, 0.0}, {0.0, 1.0, 0.0}}}};
    const simcore_host::GroundTriangle upper{
        "upper", {{{-1.0, -1.0, 0.5}, {1.0, -1.0, 0.5}, {0.0, 1.0, 0.5}}}};
    simcore_host::MapPackageGroundQuery ground({lower, upper});
    const auto hit = ground.query_down({{0.0, 0.0, 1.0}, 2.0});
    require(hit.has_value() && hit->point_enu.up_m == 0.5,
            "overlapping surfaces must return the nearest hit below the ray origin");
    require(!ground.query_down({{5.0, 5.0, 1.0}, 2.0}).has_value(),
            "queries outside all projected triangles must miss");
    require(!ground.query_down({{0.0, 0.0, 1.0}, 0.1}).has_value(),
            "queries beyond max_distance must miss");
}

void test_spatial_index_preserves_dense_grid_hits_and_reduces_candidates()
{
    constexpr int grid_size = 24;
    std::vector<simcore_host::GroundTriangle> triangles;
    triangles.reserve(grid_size * grid_size * 2);
    for (int north = 0; north < grid_size; ++north) {
        for (int east = 0; east < grid_size; ++east) {
            const double e0 = static_cast<double>(east);
            const double n0 = static_cast<double>(north);
            const double e1 = e0 + 1.0;
            const double n1 = n0 + 1.0;
            const auto height = [](double e, double n) {
                return 0.02 * e + 0.01 * n;
            };
            triangles.push_back({
                "indexed_grade",
                {{{e0, n0, height(e0, n0)},
                  {e1, n0, height(e1, n0)},
                  {e1, n1, height(e1, n1)}}}});
            triangles.push_back({
                "indexed_grade",
                {{{e0, n0, height(e0, n0)},
                  {e1, n1, height(e1, n1)},
                  {e0, n1, height(e0, n1)}}}});
        }
    }

    simcore_host::MapPackageGroundQuery ground(std::move(triangles));
    require(ground.spatial_cell_count() > 1,
            "dense ground mesh must build more than one spatial cell");
    require(ground.maximum_query_candidate_count() < ground.triangle_count() / 4,
            "ground spatial index must materially reduce per-cell candidates");

    for (int north = 0; north < grid_size; north += 3) {
        for (int east = 0; east < grid_size; east += 3) {
            const double query_east = static_cast<double>(east) + 0.37;
            const double query_north = static_cast<double>(north) + 0.61;
            const double expected_height =
                0.02 * query_east + 0.01 * query_north;
            const auto hit = ground.query_down(
                {{query_east, query_north, 5.0}, 10.0});
            require(hit.has_value(),
                    "indexed vertical ray must preserve dense-grid coverage");
            require(std::abs(hit->point_enu.up_m - expected_height) < 1e-12,
                    "indexed query must preserve barycentric ground height");
        }
    }
}

void test_spatial_index_preserves_tolerant_edges_and_source_order_ties()
{
    const simcore_host::GroundTriangle tolerant_edge{
        "edge", {{{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {0.0, 10.0, 0.0}}}};
    simcore_host::MapPackageGroundQuery edge_ground({tolerant_edge});
    require(edge_ground.query_down({{-5e-9, 1.0, 1.0}, 2.0}).has_value(),
            "grid bounds must preserve the narrow phase barycentric tolerance");
    require(edge_ground.query_down({{0.0, 0.0, 1.0}, 2.0}).has_value()
            && edge_ground.query_down({{10.0, 0.0, 1.0}, 2.0}).has_value(),
            "grid bounds must preserve exact global vertices");

    const simcore_host::GroundTriangle north_slope{
        "first", {{{-2.0, -2.0, -0.2}, {2.0, -2.0, -0.2}, {0.0, 2.0, 0.2}}}};
    const simcore_host::GroundTriangle east_slope{
        "second", {{{-2.0, -2.0, -0.2}, {2.0, -2.0, 0.2}, {0.0, 2.0, 0.0}}}};
    simcore_host::MapPackageGroundQuery tied({north_slope, east_slope});
    const auto hit = tied.query_down({{0.0, 0.0, 1.0}, 2.0});
    require(hit.has_value() && hit->normal_enu.north_m < 0.0,
            "equal-distance surfaces must retain source-order tie selection");
}

void test_spatial_index_uses_bounded_global_fallback()
{
    std::vector<simcore_host::GroundTriangle> overlapping;
    for (int index = 0; index < 256; ++index) {
        overlapping.push_back({
            "overlap_" + std::to_string(index),
            {{{-100.0, -100.0, 0.0},
              {100.0, -100.0, 0.0},
              {0.0, 100.0, 0.0}}}});
    }
    simcore_host::MapPackageGroundQuery ground(std::move(overlapping));
    require(ground.spatial_global_triangle_count() > 0,
            "overlapping large triangles must use the bounded global fallback");
    require(ground.maximum_query_candidate_count() == ground.triangle_count(),
            "global fallback must retain every overlapping source candidate");
    require(ground.query_down({{0.0, 0.0, 1.0}, 2.0}).has_value(),
            "global fallback must preserve ground hits");
}

void test_overflowing_triangle_geometry_is_rejected()
{
    bool rejected = false;
    try {
        (void)simcore_host::MapPackageGroundQuery({{
            "overflow",
            {{{1e200, 1e200, 0.0},
              {1e200 + 1e190, 1e200, 1e200},
              {1e200, 1e200 + 1e190, -1e200}}}}});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "derived area or normal overflow must fail closed at load time");
}

} // namespace

int main()
{
    try {
        test_tracked_map_package_bootstrap_loads();
        test_modified_collision_payload_is_rejected();
        test_missing_manifest_and_unsafe_payload_path_are_rejected();
        test_sloped_triangle_interpolates_height_and_normal();
        test_nearest_surface_wins_and_bounds_are_enforced();
        test_spatial_index_preserves_dense_grid_hits_and_reduces_candidates();
        test_spatial_index_preserves_tolerant_edges_and_source_order_ties();
        test_spatial_index_uses_bounded_global_fallback();
        test_overflowing_triangle_geometry_is_rejected();
        std::cout << "map_package_ground_query_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "map_package_ground_query_tests: " << error.what() << '\n';
        return 1;
    }
}
