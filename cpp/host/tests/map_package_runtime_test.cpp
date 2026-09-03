#include "terrain/map_package_manifest.hpp"
#include "terrain/map_package_runtime.hpp"

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TempPackage {
public:
    explicit TempPackage(const std::filesystem::path& source)
    {
        static std::atomic<std::uint64_t> sequence{0};
        path = std::filesystem::temp_directory_path()
            / ("simcore-map-reload-test-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count())
               + "-" + std::to_string(++sequence));
        std::filesystem::create_directories(path);
        for (const auto& entry : std::filesystem::directory_iterator(source)) {
            std::filesystem::copy(
                entry.path(),
                path / entry.path().filename(),
                std::filesystem::copy_options::recursive
                    | std::filesystem::copy_options::overwrite_existing);
        }
    }

    ~TempPackage()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    std::filesystem::path path;
};

void append_payload_comment(const std::filesystem::path& package)
{
    std::ofstream output(
        package / "ground_surface.csv",
        std::ios::binary | std::ios::app);
    if (!output) {
        throw std::runtime_error("failed to mutate temporary ground payload");
    }
    output << "# hot reload generation\n";
}

void rewrite_manifest_checksum(
    const std::filesystem::path& package,
    const std::string& checksum)
{
    const auto path = package / "manifest.cfg";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read temporary manifest");
    }
    std::string bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const std::string prefix = "collision_checksum=";
    const auto start = bytes.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("temporary manifest has no checksum field");
    }
    const auto value_start = start + prefix.size();
    const auto value_end = bytes.find_first_of("\r\n", value_start);
    bytes.replace(
        value_start,
        value_end == std::string::npos
            ? std::string::npos
            : value_end - value_start,
        checksum);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to replace temporary manifest");
    }
    output << bytes;
}

void replace_manifest_value(
    const std::filesystem::path& package,
    const std::string& key,
    const std::string& value)
{
    const auto path = package / "manifest.cfg";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read temporary manifest");
    }
    std::string bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const std::string prefix = key + "=";
    const auto start = bytes.find(prefix);
    if (start == std::string::npos) {
        throw std::runtime_error("temporary manifest key not found: " + key);
    }
    const auto value_start = start + prefix.size();
    const auto value_end = bytes.find_first_of("\r\n", value_start);
    bytes.replace(
        value_start,
        value_end == std::string::npos
            ? std::string::npos
            : value_end - value_start,
        value);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to replace temporary manifest value");
    }
    output << bytes;
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_f32(std::vector<std::uint8_t>& bytes, float value)
{
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_f64(std::vector<std::uint8_t>& bytes, double value)
{
    append_u64(bytes, std::bit_cast<std::uint64_t>(value));
}

void write_flat_heightfield(
    const std::filesystem::path& package,
    float height_m,
    std::uint32_t first_sample_flags = 1,
    bool use_v2 = false,
    std::uint32_t material_id = 0,
    float friction_multiplier = 1.0f)
{
    std::vector<std::uint8_t> bytes{
        'S', 'I', 'M', 'G', 'H', 'F',
        static_cast<std::uint8_t>(use_v2 ? '2' : '1'), '\0'};
    append_u32(bytes, use_v2 ? 2u : 1u);
    append_u32(bytes, 80);
    append_u32(bytes, 2);
    append_u32(bytes, 2);
    append_u32(bytes, 20);
    append_u32(bytes, use_v2 ? 12u : 4u);
    append_f64(bytes, -50.0);
    append_f64(bytes, -50.0);
    append_f64(bytes, 100.0);
    append_f64(bytes, 0.0);
    append_f64(bytes, 0.0);
    append_f64(bytes, 100.0);
    for (std::size_t index = 0; index < 4; ++index) {
        append_u32(bytes, index == 0 ? first_sample_flags : 1u);
        append_f32(bytes, height_m);
        append_f32(bytes, 0.0f);
        append_f32(bytes, 0.0f);
        append_f32(bytes, 1.0f);
    }
    append_u32(bytes, 1);
    if (use_v2) {
        append_u32(bytes, material_id);
        append_f32(bytes, friction_multiplier);
    }
    std::ofstream output(
        package / "ground_heightfield.bin",
        std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to create heightfield payload");
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

const std::vector<std::string>& heightfield_collision_files()
{
    static const std::vector<std::string> files{
        "ground_surface.csv",
        "ground_heightfield.bin",
        "static_colliders.csv"};
    return files;
}

void commit_heightfield_generation(const std::filesystem::path& package)
{
    const auto checksum =
        simcore_host::compute_map_package_collision_checksum(
            package, heightfield_collision_files());
    rewrite_manifest_checksum(package, checksum);
}

void convert_to_heightfield_package(
    const std::filesystem::path& package,
    float height_m,
    bool use_v2 = false,
    std::uint32_t material_id = 0,
    float friction_multiplier = 1.0f)
{
    std::ofstream(package / "ground_surface.csv", std::ios::trunc)
        << "# Heightfield v1 compatibility sentinel.\n"
           "surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2\n";
    write_flat_heightfield(
        package,
        height_m,
        1,
        use_v2,
        material_id,
        friction_multiplier);
    replace_manifest_value(
        package,
        "collision_files",
        "ground_surface.csv,ground_heightfield.bin,static_colliders.csv");
    commit_heightfield_generation(package);
}

void commit_new_payload_generation(
    const std::filesystem::path& package,
    const simcore_host::MapPackageManifest& previous_manifest)
{
    append_payload_comment(package);
    const auto checksum =
        simcore_host::compute_map_package_collision_checksum(
            package,
            previous_manifest.collision_files);
    rewrite_manifest_checksum(package, checksum);
}

void test_constructor_detects_manifest_changed_since_active_snapshot()
{
    TempPackage package(SIMCORE_TEST_MAP_PACKAGE_PATH);
    const auto initial_manifest = simcore_host::load_map_package_manifest(
        package.path);
    commit_new_payload_generation(package.path, initial_manifest);
    const auto committed_manifest = simcore_host::load_map_package_manifest(
        package.path);
    require(committed_manifest.collision_checksum
                != initial_manifest.collision_checksum,
            "fixture must commit a new collision identity");

    int callbacks = 0;
    std::string callback_checksum;
    simcore_host::MapPackageHotReloader reloader(
        package.path,
        initial_manifest.collision_checksum,
        [&](simcore_host::RuntimeMapPackage candidate) {
            ++callbacks;
            callback_checksum = std::move(candidate.collision_checksum);
        });

    require(reloader.poll_once() && callbacks == 1
                && callback_checksum == committed_manifest.collision_checksum,
            "watcher construction must not lose a manifest committed after startup");
    require(!reloader.poll_once() && callbacks == 1,
            "an acknowledged manifest generation must not reload twice");
}

void test_manifest_last_failure_retains_active_then_applies_once()
{
    TempPackage package(SIMCORE_TEST_MAP_PACKAGE_PATH);
    const auto initial_manifest = simcore_host::load_map_package_manifest(
        package.path);
    int callbacks = 0;
    std::string callback_checksum;
    simcore_host::MapPackageHotReloader reloader(
        package.path,
        initial_manifest.collision_checksum,
        [&](simcore_host::RuntimeMapPackage candidate) {
            ++callbacks;
            callback_checksum = std::move(candidate.collision_checksum);
        });

    // Payload replacement alone is intentionally invisible: manifest.cfg is
    // the commit marker and the active in-memory map remains authoritative.
    append_payload_comment(package.path);
    require(!reloader.poll_once() && callbacks == 0,
            "payload bytes without a manifest commit must retain the active map");

    rewrite_manifest_checksum(
        package.path,
        "fnv1a64:0000000000000000");
    require(!reloader.poll_once() && callbacks == 0,
            "an invalid manifest candidate must fail closed");

    const auto actual_checksum =
        simcore_host::compute_map_package_collision_checksum(
            package.path,
            initial_manifest.collision_files);
    rewrite_manifest_checksum(package.path, actual_checksum);
    require(reloader.poll_once() && callbacks == 1
                && callback_checksum == actual_checksum,
            "the valid manifest-last commit must queue exactly one replacement");
    require(!reloader.poll_once() && callbacks == 1,
            "a stable valid manifest must not be delivered repeatedly");
}

void test_heightfield_v1_load_and_manifest_last_hot_reload()
{
    {
        TempPackage ambiguous(SIMCORE_TEST_MAP_PACKAGE_PATH);
        convert_to_heightfield_package(ambiguous.path, 1.0f);
        std::ofstream output(
            ambiguous.path / "ground_surface.csv",
            std::ios::binary | std::ios::app);
        output << "legacy,0,0,0,1,0,0,1,1,0\n";
        output.close();
        commit_heightfield_generation(ambiguous.path);
        bool rejected = false;
        try {
            (void)simcore_host::load_runtime_map_package(ambiguous.path);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected,
                "a heightfield package must reject a second authoritative triangle surface");
    }

    TempPackage package(SIMCORE_TEST_MAP_PACKAGE_PATH);
    convert_to_heightfield_package(package.path, 2.0f);
    const auto initial = simcore_host::load_runtime_map_package(package.path);
    require(initial.ground_diagnostics.payload_kind
                == simcore_host::GroundPayloadKind::PackedHeightfieldV1
            && initial.ground_diagnostics.sample_count == 4
            && initial.ground_diagnostics.cell_count == 1
            && initial.ground_diagnostics.drivable_cell_count == 1
            && initial.ground_diagnostics.lattice_columns == 2
            && initial.ground_diagnostics.lattice_rows == 2,
            "format v1 must select the declared packed heightfield payload");
    const auto initial_hit = initial.ground_query->query_down(
        {{0.0, 0.0, 10.0}, 20.0});
    require(initial_hit.has_value()
            && initial_hit->point_enu.up_m == 2.0,
            "runtime heightfield query must expose Unreal's baked height");

    int callbacks = 0;
    std::string callback_checksum;
    double callback_height = 0.0;
    simcore_host::MapPackageHotReloader reloader(
        package.path,
        initial.collision_checksum,
        [&](simcore_host::RuntimeMapPackage candidate) {
            ++callbacks;
            callback_checksum = candidate.collision_checksum;
            const auto hit = candidate.ground_query->query_down(
                {{0.0, 0.0, 10.0}, 20.0});
            callback_height = hit ? hit->point_enu.up_m : -1.0;
        });

    write_flat_heightfield(package.path, 3.0f);
    require(!reloader.poll_once() && callbacks == 0,
            "heightfield payload replacement without manifest commit must be invisible");

    write_flat_heightfield(package.path, 3.0f, 2u);
    commit_heightfield_generation(package.path);
    require(!reloader.poll_once() && callbacks == 0,
            "a checksum-valid but malformed heightfield candidate must retain the active snapshot");

    write_flat_heightfield(package.path, 4.0f);
    commit_heightfield_generation(package.path);
    const auto committed = simcore_host::load_map_package_manifest(package.path);
    require(reloader.poll_once() && callbacks == 1
            && callback_checksum == committed.collision_checksum
            && callback_height == 4.0,
            "a valid manifest-last heightfield generation must queue exactly once");
    require(!reloader.poll_once() && callbacks == 1,
            "an acknowledged heightfield generation must not reload repeatedly");
}

void test_heightfield_v2_runtime_diagnostics_and_material_contract()
{
    TempPackage package(SIMCORE_TEST_MAP_PACKAGE_PATH);
    convert_to_heightfield_package(
        package.path,
        2.5f,
        true,
        static_cast<std::uint32_t>(
            simcore_host::GroundSurfaceMaterialId::LowFriction),
        0.35f);
    const auto runtime = simcore_host::load_runtime_map_package(package.path);
    require(runtime.ground_diagnostics.payload_kind
                == simcore_host::GroundPayloadKind::PackedHeightfieldV2
            && std::string(simcore_host::ground_payload_kind_name(
                    runtime.ground_diagnostics.payload_kind))
                == "packed_heightfield_v2",
            "SIMGHF2 must remain distinguishable in runtime diagnostics");
    const auto hit = runtime.ground_query->query_down(
        {{0.0, 0.0, 10.0}, 20.0});
    require(hit.has_value()
            && hit->surface_material_id
                == simcore_host::GroundSurfaceMaterialId::LowFriction
            && std::abs(hit->friction_multiplier - 0.35) < 1e-5,
            "runtime snapshot must preserve SIMGHF2 material metadata");
}

} // namespace

int main()
{
    try {
        test_constructor_detects_manifest_changed_since_active_snapshot();
        test_manifest_last_failure_retains_active_then_applies_once();
        test_heightfield_v1_load_and_manifest_last_hot_reload();
        test_heightfield_v2_runtime_diagnostics_and_material_contract();
        std::cout << "map_package_runtime_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "map_package_runtime_tests: " << error.what() << '\n';
        return 1;
    }
}
