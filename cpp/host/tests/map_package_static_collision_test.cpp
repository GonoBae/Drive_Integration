#include "collision/map_package_static_collision.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef SIMCORE_TEST_MAP_PACKAGE_PATH
#error SIMCORE_TEST_MAP_PACKAGE_PATH must identify the tracked test MapPackage
#endif

namespace {

constexpr const char* kHeader =
    "collider_id,semantic,shape,center_e_m,center_n_m,center_u_m,"
    "heading_rad,half_length_m,half_width_m,half_height_m,friction,restitution\n";

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
            / ("simcore-static-collision-" + unique);
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

simcore_host::MapPackageManifest make_manifest(
    const std::filesystem::path& path, bool declare_static_file = true)
{
    simcore_host::MapPackageManifest manifest;
    manifest.package_directory = path;
    manifest.map_id = "test";
    manifest.coordinate_frame = "map_enu";
    manifest.collision_files = declare_static_file
        ? std::vector<std::string>{"ground_surface.csv", "static_colliders.csv"}
        : std::vector<std::string>{"ground_surface.csv"};
    return manifest;
}

void write_static_csv(
    const std::filesystem::path& directory, const std::string& contents)
{
    std::ofstream output(directory / "static_colliders.csv", std::ios::binary);
    output << contents;
}

void expect_rejected(const std::string& contents, const char* message)
{
    TemporaryPackage package;
    write_static_csv(package.path, contents);
    bool rejected = false;
    try {
        (void)simcore_host::load_static_collision_world(
            make_manifest(package.path));
    } catch (const std::runtime_error&) {
        rejected = true;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

void test_tracked_header_only_package_loads()
{
    const auto manifest = simcore_host::load_map_package_manifest(
        SIMCORE_TEST_MAP_PACKAGE_PATH);
    const auto world = simcore_host::load_static_collision_world(manifest);
    require(world.static_collider_count() == 0,
            "tracked bootstrap package must expose an empty static world");
}

void test_valid_rows_are_loaded_and_sorted_by_id()
{
    TemporaryPackage package;
    write_static_csv(
        package.path,
        std::string(kHeader)
            + "wall_z,wall,obb,0,20,1,1.57079632679,10,0.1,1,0.8,0\n"
              "curb_a,curb,obb,0,5,0.075,1.57079632679,10,0.075,0.075,0.9,0\n");
    const auto world = simcore_host::load_static_collision_world(
        make_manifest(package.path));

    require(world.static_collider_count() == 2,
            "valid static collider rows must load");
    require(world.static_colliders()[0].collider_id == "curb_a"
            && world.static_colliders()[1].collider_id == "wall_z",
            "loaded colliders must be sorted by stable ID");
    require(world.static_colliders()[0].semantic
                == simcore_host::StaticColliderSemantic::Curb,
            "loader must preserve collider semantics");
}

void test_undeclared_payload_is_rejected()
{
    TemporaryPackage package;
    write_static_csv(package.path, kHeader);
    bool rejected = false;
    try {
        (void)simcore_host::load_static_collision_world(
            make_manifest(package.path, false));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected,
            "static collision loader must not read an undeclared payload");
}

void test_modified_static_payload_is_rejected_by_manifest()
{
    TemporaryPackage package;
    const auto source = std::filesystem::path(SIMCORE_TEST_MAP_PACKAGE_PATH);
    std::filesystem::copy_file(
        source / "manifest.cfg", package.path / "manifest.cfg");
    std::filesystem::copy_file(
        source / "ground_surface.csv", package.path / "ground_surface.csv");
    std::filesystem::copy_file(
        source / "static_colliders.csv", package.path / "static_colliders.csv");
    {
        std::ofstream output(
            package.path / "static_colliders.csv",
            std::ios::binary | std::ios::app);
        output << "# modified after manifest verification data was authored\n";
    }

    bool rejected = false;
    try {
        (void)simcore_host::load_map_package_manifest(package.path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected,
            "a modified static collision payload must fail manifest verification");
}

void test_strict_schema_and_values_fail_closed()
{
    expect_rejected(
        "id,semantic,shape,center_e_m,center_n_m,center_u_m,heading_rad,"
        "half_length_m,half_width_m,half_height_m,friction,restitution\n",
        "an unsupported header must be rejected");
    expect_rejected(
        std::string(kHeader)
            + "same,wall,obb,0,0,1,0,1,1,1,0.8,0\n"
              "same,curb,obb,1,0,1,0,1,1,1,0.8,0\n",
        "duplicate collider IDs must be rejected");
    expect_rejected(
        std::string(kHeader)
            + "bad,wall,sphere,0,0,1,0,1,1,1,0.8,0\n",
        "unknown static shape must be rejected");
    expect_rejected(
        std::string(kHeader)
            + "bad,ramp,obb,0,0,1,0,1,1,1,0.8,0\n",
        "unknown semantic must be rejected");
    expect_rejected(
        std::string(kHeader)
            + "bad,wall,obb,nan,0,1,0,1,1,1,0.8,0\n",
        "non-finite numeric values must be rejected");
    expect_rejected(
        std::string(kHeader)
            + "bad,wall,obb,0,0,1,0,1,0,1,0.8,0\n",
        "non-positive half extents must be rejected");
    expect_rejected(
        std::string(kHeader)
            + "bad,wall,obb,0,0,1,0,1,1,1,-0.1,0\n",
        "negative friction must be rejected");
    expect_rejected(
        std::string(kHeader)
            + "bad,wall,obb,0,0,1,0,1,1,1,0.8,1.1\n",
        "restitution above one must be rejected");
    expect_rejected(
        "# comments without a header\n",
        "a missing static collision header must be rejected");
}

} // namespace

int main()
{
    try {
        test_tracked_header_only_package_loads();
        test_valid_rows_are_loaded_and_sorted_by_id();
        test_undeclared_payload_is_rejected();
        test_modified_static_payload_is_rejected_by_manifest();
        test_strict_schema_and_values_fail_closed();
        std::cout << "map_package_static_collision_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "map_package_static_collision_tests: "
                  << error.what() << '\n';
        return 1;
    }
}
