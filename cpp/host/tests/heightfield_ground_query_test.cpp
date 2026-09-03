#include "terrain/heightfield_ground_query.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

bool close(double lhs, double rhs, double tolerance = 1e-6)
{
    return std::abs(lhs - rhs) <= tolerance;
}

class TemporaryFile {
public:
    TemporaryFile()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        path = std::filesystem::temp_directory_path()
            / ("simcore-heightfield-" + unique + ".bin");
    }

    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    std::filesystem::path path;
};

struct Sample {
    std::uint32_t flags = 1;
    float up = 0.0f;
    float normal_east = 0.0f;
    float normal_north = 0.0f;
    float normal_up = 1.0f;
};

struct Cell {
    std::uint32_t flags = 1;
    std::uint32_t material_id = 0;
    float friction_multiplier = 1.0f;
};

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

std::vector<std::uint8_t> make_heightfield_bytes(
    std::uint32_t columns,
    std::uint32_t rows,
    const std::vector<Sample>& samples,
    const std::vector<std::uint32_t>& cells,
    double origin_east = 0.0,
    double origin_north = 0.0,
    double column_step_east = 1.0,
    double column_step_north = 0.0,
    double row_step_east = 0.0,
    double row_step_north = 1.0)
{
    std::vector<std::uint8_t> bytes{
        'S', 'I', 'M', 'G', 'H', 'F', '1', '\0'};
    append_u32(bytes, 1);
    append_u32(bytes, 80);
    append_u32(bytes, columns);
    append_u32(bytes, rows);
    append_u32(bytes, 20);
    append_u32(bytes, 4);
    append_f64(bytes, origin_east);
    append_f64(bytes, origin_north);
    append_f64(bytes, column_step_east);
    append_f64(bytes, column_step_north);
    append_f64(bytes, row_step_east);
    append_f64(bytes, row_step_north);
    for (const auto& sample : samples) {
        append_u32(bytes, sample.flags);
        append_f32(bytes, sample.up);
        append_f32(bytes, sample.normal_east);
        append_f32(bytes, sample.normal_north);
        append_f32(bytes, sample.normal_up);
    }
    for (const auto flags : cells) {
        append_u32(bytes, flags);
    }
    return bytes;
}

std::vector<std::uint8_t> make_heightfield_v2_bytes(
    std::uint32_t columns,
    std::uint32_t rows,
    const std::vector<Sample>& samples,
    const std::vector<Cell>& cells)
{
    std::vector<std::uint8_t> bytes{
        'S', 'I', 'M', 'G', 'H', 'F', '2', '\0'};
    append_u32(bytes, 2);
    append_u32(bytes, 80);
    append_u32(bytes, columns);
    append_u32(bytes, rows);
    append_u32(bytes, 20);
    append_u32(bytes, 12);
    append_f64(bytes, 0.0);
    append_f64(bytes, 0.0);
    append_f64(bytes, 1.0);
    append_f64(bytes, 0.0);
    append_f64(bytes, 0.0);
    append_f64(bytes, 1.0);
    for (const auto& sample : samples) {
        append_u32(bytes, sample.flags);
        append_f32(bytes, sample.up);
        append_f32(bytes, sample.normal_east);
        append_f32(bytes, sample.normal_north);
        append_f32(bytes, sample.normal_up);
    }
    for (const auto& cell : cells) {
        append_u32(bytes, cell.flags);
        append_u32(bytes, cell.material_id);
        append_f32(bytes, cell.friction_multiplier);
    }
    return bytes;
}

void write_bytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to create heightfield test fixture");
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

template <typename Mutator>
void require_rejected(
    std::vector<std::uint8_t> bytes,
    Mutator mutate,
    const char* message)
{
    TemporaryFile fixture;
    mutate(bytes);
    write_bytes(fixture.path, bytes);
    bool rejected = false;
    try {
        (void)simcore_host::HeightfieldGroundQuery::load(fixture.path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, message);
}

void test_fixed_diagonal_interpolates_measured_height_and_normal()
{
    TemporaryFile fixture;
    const std::vector<Sample> samples{
        {1, 0.0f, 0.0f, 0.0f, 1.0f},
        {1, 10.0f, 0.2f, 0.0f, 0.98f},
        {1, 20.0f, 0.0f, 0.2f, 0.98f},
        {1, 40.0f, 0.2f, 0.2f, 0.96f},
    };
    write_bytes(
        fixture.path,
        make_heightfield_bytes(
            2, 2, samples, {1}, 10.0, 20.0, 2.0, 0.0, 0.0, 3.0));
    const auto ground = simcore_host::HeightfieldGroundQuery::load(
        fixture.path);
    require(ground.columns() == 2 && ground.rows() == 2
            && ground.sample_count() == 4
            && ground.cell_count() == 1
            && ground.drivable_cell_count() == 1,
            "heightfield diagnostics must retain the global lattice shape");

    // u=.75, v=.25 selects P00/P10/P11: .25*0 + .5*10 + .25*40.
    const auto lower = ground.query_down({{11.5, 20.75, 50.0}, 50.0});
    require(lower.has_value() && close(lower->point_enu.up_m, 15.0)
            && close(lower->distance_m, 35.0)
            && lower->surface_material_id
                == simcore_host::GroundSurfaceMaterialId::Default
            && close(lower->friction_multiplier, 1.0),
            "v1 must preserve interpolation and default material metadata");
    const double lower_normal_length = std::hypot(
        lower->normal_enu.east_m,
        lower->normal_enu.north_m,
        lower->normal_enu.up_m);
    require(close(lower_normal_length, 1.0)
            && lower->normal_enu.east_m > 0.0
            && lower->normal_enu.up_m > 0.0,
            "sample normals must be interpolated and normalized");

    // u=.25, v=.75 selects P00/P11/P01: .25*0 + .25*40 + .5*20.
    const auto upper = ground.query_down({{10.5, 22.25, 50.0}, 50.0});
    require(upper.has_value() && close(upper->point_enu.up_m, 20.0),
            "v>u must use the P00/P11/P01 diagonal");
    const auto& bounds = ground.bounds_enu();
    require(close(bounds.minimum.east_m, 10.0)
            && close(bounds.maximum.east_m, 12.0)
            && close(bounds.minimum.north_m, 20.0)
            && close(bounds.maximum.north_m, 23.0)
            && close(bounds.minimum.up_m, 0.0)
            && close(bounds.maximum.up_m, 40.0),
            "heightfield bounds must cover every drivable sample");
}

void test_v2_transports_stable_material_and_friction()
{
    TemporaryFile fixture;
    const std::vector<Sample> samples(6, Sample{});
    write_bytes(
        fixture.path,
        make_heightfield_v2_bytes(
            3,
            2,
            samples,
            {
                {1, static_cast<std::uint32_t>(
                        simcore_host::GroundSurfaceMaterialId::LowFriction),
                    0.35f},
                {1, 42u, 1.25f},
            }));
    const auto ground = simcore_host::HeightfieldGroundQuery::load(
        fixture.path);
    require(ground.format_version() == 2,
            "SIMGHF2 format must remain visible in diagnostics");
    const auto low = ground.query_down({{0.5, 0.5, 1.0}, 2.0});
    require(low.has_value()
            && low->surface_material_id
                == simcore_host::GroundSurfaceMaterialId::LowFriction
            && close(low->friction_multiplier, 0.35, 1e-5),
            "SIMGHF2 must return the cell material and friction multiplier");
    const auto future = ground.query_down({{1.5, 0.5, 1.0}, 2.0});
    require(future.has_value()
            && static_cast<std::uint32_t>(future->surface_material_id) == 42u
            && close(future->friction_multiplier, 1.25),
            "unknown append-only material IDs must round-trip for future profiles");
}

void test_rotated_basis_outer_edges_and_query_limits()
{
    TemporaryFile fixture;
    const std::vector<Sample> samples{
        {1, 0.0f}, {1, 1.0f}, {1, 2.0f}, {1, 3.0f},
    };
    // Column moves north, row moves west.
    write_bytes(
        fixture.path,
        make_heightfield_bytes(
            2, 2, samples, {1}, 5.0, 7.0, 0.0, 2.0, -3.0, 0.0));
    const auto ground = simcore_host::HeightfieldGroundQuery::load(
        fixture.path);
    const auto interior = ground.query_down({{2.75, 7.5, 5.0}, 5.0});
    require(interior.has_value() && close(interior->point_enu.up_m, 1.75),
            "rotated exporter lattices must invert into column/row coordinates");
    const auto outer_corner = ground.query_down({{2.0, 9.0, 5.0}, 5.0});
    require(outer_corner.has_value()
            && close(outer_corner->point_enu.up_m, 3.0),
            "the inclusive maximum lattice edge must remain queryable");
    require(!ground.query_down({{1.99, 9.0, 5.0}, 5.0}).has_value(),
            "a point outside the rotated lattice must miss");
    require(!ground.query_down({{2.75, 7.5, 1.0}, 5.0}).has_value(),
            "a vertical ray originating below the surface must miss");
    require(!ground.query_down({{2.75, 7.5, 5.0}, 1.0}).has_value(),
            "a surface beyond max_distance must miss");
}

void test_holes_are_fail_closed()
{
    TemporaryFile fixture;
    const std::vector<Sample> samples(6, Sample{});
    write_bytes(
        fixture.path,
        make_heightfield_bytes(3, 2, samples, {1, 0}));
    const auto ground = simcore_host::HeightfieldGroundQuery::load(
        fixture.path);
    require(ground.drivable_cell_count() == 1
            && ground.query_down({{0.5, 0.5, 1.0}, 2.0}).has_value()
            && ground.query_down({{0.999999, 0.5, 1.0}, 2.0}).has_value()
            && !ground.query_down({{1.0, 0.5, 1.0}, 2.0}).has_value()
            && !ground.query_down({{1.5, 0.5, 1.0}, 2.0}).has_value(),
            "non-drivable cells and their owned internal edge must never synthesize ground");
}

void test_malformed_payloads_are_rejected()
{
    const std::vector<Sample> valid_samples(4, Sample{});
    const auto valid = make_heightfield_bytes(2, 2, valid_samples, {1});

    require_rejected(valid, [](auto& bytes) { bytes[0] = 'X'; },
                     "bad heightfield magic must be rejected");
    require_rejected(valid, [](auto& bytes) { bytes[12] = 79; },
                     "wrong heightfield header size must be rejected");
    require_rejected(valid, [](auto& bytes) { bytes.push_back(0); },
                     "trailing heightfield bytes must be rejected");
    require_rejected(valid, [](auto& bytes) { bytes.pop_back(); },
                     "truncated heightfield bytes must be rejected");
    require_rejected(valid, [](auto& bytes) {
        // First sample flags at byte 80.
        bytes[80] = 2;
    }, "unknown sample flags must be rejected");
    require_rejected(valid, [](auto& bytes) {
        // First sample up_m begins at byte 84.
        const auto nan = std::bit_cast<std::uint32_t>(
            std::numeric_limits<float>::quiet_NaN());
        for (unsigned shift = 0; shift < 32; shift += 8) {
            bytes[84 + shift / 8] = static_cast<std::uint8_t>(nan >> shift);
        }
    }, "non-finite sample values must be rejected");
    require_rejected(valid, [](auto& bytes) {
        // row_step duplicates column_step, making the 2x2 basis singular.
        for (std::size_t index = 0; index < 16; ++index) {
            bytes[64 + index] = bytes[48 + index];
        }
    }, "a singular lattice basis must be rejected");

    auto overflowing_corner = make_heightfield_bytes(
        2,
        2,
        valid_samples,
        {1},
        1.0e308,
        0.0,
        1.0e308,
        0.0,
        0.0,
        1.0);
    require_rejected(
        std::move(overflowing_corner), [](auto&) {},
        "a finite header whose derived lattice corner overflows must be rejected");

    auto invalid_reference = valid;
    invalid_reference[80] = 0;
    require_rejected(
        std::move(invalid_reference), [](auto&) {},
        "a drivable cell referencing an invalid sample must be rejected");

    auto no_ground = make_heightfield_bytes(2, 2, valid_samples, {0});
    require_rejected(
        std::move(no_ground), [](auto&) {},
        "a heightfield with no drivable cells must be rejected");

    auto over_limit = make_heightfield_bytes(2, 2, valid_samples, {1});
    // columns=2,000,001 and rows=2 reject before any size-dependent parse.
    const std::uint32_t columns = 2'000'001;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        over_limit[16 + shift / 8] =
            static_cast<std::uint8_t>(columns >> shift);
        over_limit[20 + shift / 8] =
            static_cast<std::uint8_t>(2u >> shift);
    }
    require_rejected(
        std::move(over_limit), [](auto&) {},
        "the global lattice sample limit must be enforced before allocation");

    const auto valid_v2 = make_heightfield_v2_bytes(
        2, 2, valid_samples, {{1, 1, 1.0f}});
    require_rejected(valid_v2, [](auto& bytes) {
        // Cell friction is the last float in a one-cell payload.
        const auto nan = std::bit_cast<std::uint32_t>(
            std::numeric_limits<float>::quiet_NaN());
        const std::size_t offset = bytes.size() - sizeof(std::uint32_t);
        for (unsigned shift = 0; shift < 32; shift += 8) {
            bytes[offset + shift / 8] =
                static_cast<std::uint8_t>(nan >> shift);
        }
    }, "non-finite SIMGHF2 friction must be rejected");
    require_rejected(valid_v2, [](auto& bytes) {
        const std::size_t cell_offset = bytes.size() - 12;
        bytes[cell_offset] = 0;
    }, "a non-drivable SIMGHF2 cell must not retain material metadata");
    require_rejected(valid_v2, [](auto& bytes) {
        bytes[6] = '1';
    }, "SIMGHF2 layout must not masquerade behind SIMGHF1 magic");
}

} // namespace

int main()
{
    try {
        test_fixed_diagonal_interpolates_measured_height_and_normal();
        test_v2_transports_stable_material_and_friction();
        test_rotated_basis_outer_edges_and_query_limits();
        test_holes_are_fail_closed();
        test_malformed_payloads_are_rejected();
        std::cout << "heightfield_ground_query_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "heightfield_ground_query_tests: " << error.what() << '\n';
        return 1;
    }
}
