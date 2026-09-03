#include "terrain/heightfield_ground_query.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace simcore_host {
namespace {

constexpr std::array<std::uint8_t, 8> kMagicV1{
    'S', 'I', 'M', 'G', 'H', 'F', '1', '\0'};
constexpr std::array<std::uint8_t, 8> kMagicV2{
    'S', 'I', 'M', 'G', 'H', 'F', '2', '\0'};
constexpr std::uint32_t kSampleValid = 1u << 0;
constexpr std::uint32_t kCellDrivable = 1u << 0;
constexpr double kGridCoordinateEpsilon = 1e-9;
constexpr double kNormalEpsilon = 1e-12;
constexpr float kMinimumFrictionMultiplier = 0.05f;
constexpr float kMaximumFrictionMultiplier = 4.0f;

std::size_t checked_add(
    std::size_t lhs, std::size_t rhs, const char* description)
{
    if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
        throw std::runtime_error(
            std::string("Heightfield ") + description + " size overflows");
    }
    return lhs + rhs;
}

std::size_t checked_multiply(
    std::size_t lhs, std::size_t rhs, const char* description)
{
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        throw std::runtime_error(
            std::string("Heightfield ") + description + " size overflows");
    }
    return lhs * rhs;
}

class LittleEndianReader {
public:
    LittleEndianReader(
        const std::vector<std::uint8_t>& bytes,
        const std::filesystem::path& path)
        : bytes_(bytes), path_(path)
    {
    }

    [[nodiscard]] std::array<std::uint8_t, 8> read_magic()
    {
        require(8);
        std::array<std::uint8_t, 8> result{};
        std::copy_n(bytes_.begin() + offset_, result.size(), result.begin());
        offset_ += result.size();
        return result;
    }

    [[nodiscard]] std::uint32_t read_u32()
    {
        require(4);
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
        }
        return value;
    }

    [[nodiscard]] float read_f32()
    {
        return std::bit_cast<float>(read_u32());
    }

    [[nodiscard]] double read_f64()
    {
        require(8);
        std::uint64_t bits = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            bits |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
        }
        return std::bit_cast<double>(bits);
    }

    [[nodiscard]] std::size_t offset() const { return offset_; }

private:
    void require(std::size_t count) const
    {
        if (offset_ > bytes_.size() || count > bytes_.size() - offset_) {
            throw std::runtime_error(
                "Heightfield payload is truncated: " + path_.string());
        }
    }

    const std::vector<std::uint8_t>& bytes_;
    const std::filesystem::path& path_;
    std::size_t offset_ = 0;
};

bool finite(double value)
{
    return std::isfinite(value);
}

void expand_bounds(GroundBoundsEnu& bounds, const GroundPointEnu& point)
{
    bounds.minimum.east_m = std::min(bounds.minimum.east_m, point.east_m);
    bounds.minimum.north_m = std::min(bounds.minimum.north_m, point.north_m);
    bounds.minimum.up_m = std::min(bounds.minimum.up_m, point.up_m);
    bounds.maximum.east_m = std::max(bounds.maximum.east_m, point.east_m);
    bounds.maximum.north_m = std::max(bounds.maximum.north_m, point.north_m);
    bounds.maximum.up_m = std::max(bounds.maximum.up_m, point.up_m);
}

} // namespace

HeightfieldGroundQuery HeightfieldGroundQuery::load(
    const std::filesystem::path& path)
{
    constexpr std::uintmax_t maximum_file_size =
        kGroundHeightfieldHeaderSize
        + static_cast<std::uintmax_t>(kMaximumGroundHeightfieldSamples)
            * (kGroundHeightfieldSampleStride + kGroundHeightfieldCellStride);
    std::error_code file_size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(
        path, file_size_error);
    if (file_size_error) {
        throw std::runtime_error(
            "MapPackage heightfield not readable: " + path.string());
    }
    if (file_size > maximum_file_size) {
        throw std::runtime_error(
            "MapPackage heightfield exceeds the packed file size limit: "
            + path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "MapPackage heightfield not found: " + path.string());
    }
    std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (input.bad()) {
        throw std::runtime_error(
            "Failed to read MapPackage heightfield: " + path.string());
    }

    LittleEndianReader reader(bytes, path);
    const auto magic = reader.read_magic();
    if (magic != kMagicV1 && magic != kMagicV2) {
        throw std::runtime_error(
            "Unsupported MapPackage heightfield magic: " + path.string());
    }
    const std::uint32_t version = reader.read_u32();
    const std::uint32_t header_size = reader.read_u32();
    const std::uint32_t columns = reader.read_u32();
    const std::uint32_t rows = reader.read_u32();
    const std::uint32_t sample_stride = reader.read_u32();
    const std::uint32_t cell_stride = reader.read_u32();
    const double origin_east = reader.read_f64();
    const double origin_north = reader.read_f64();
    const double column_step_east = reader.read_f64();
    const double column_step_north = reader.read_f64();
    const double row_step_east = reader.read_f64();
    const double row_step_north = reader.read_f64();

    const bool is_v1 = magic == kMagicV1
        && version == kGroundHeightfieldFormatVersionV1
        && sample_stride == kGroundHeightfieldSampleStride
        && cell_stride == kGroundHeightfieldCellStrideV1;
    const bool is_v2 = magic == kMagicV2
        && version == kGroundHeightfieldFormatVersion
        && sample_stride == kGroundHeightfieldSampleStride
        && cell_stride == kGroundHeightfieldCellStride;
    if ((!is_v1 && !is_v2)
        || header_size != kGroundHeightfieldHeaderSize) {
        throw std::runtime_error(
            "Unsupported MapPackage heightfield layout: " + path.string());
    }
    if (reader.offset() != kGroundHeightfieldHeaderSize) {
        throw std::logic_error("Heightfield parser header size is inconsistent");
    }
    if (columns < 2 || rows < 2) {
        throw std::runtime_error(
            "MapPackage heightfield requires at least a 2x2 lattice: "
            + path.string());
    }
    const std::size_t sample_count = checked_multiply(
        static_cast<std::size_t>(columns),
        static_cast<std::size_t>(rows),
        "sample count");
    if (sample_count > kMaximumGroundHeightfieldSamples) {
        throw std::runtime_error(
            "MapPackage heightfield exceeds the 2000000 sample limit: "
            + path.string());
    }
    const std::size_t cell_count = checked_multiply(
        static_cast<std::size_t>(columns - 1),
        static_cast<std::size_t>(rows - 1),
        "cell count");
    const std::size_t sample_bytes = checked_multiply(
        sample_count,
        static_cast<std::size_t>(sample_stride),
        "sample payload");
    const std::size_t cell_bytes = checked_multiply(
        cell_count,
        static_cast<std::size_t>(cell_stride),
        "cell payload");
    const std::size_t expected_size = checked_add(
        checked_add(header_size, sample_bytes, "file"),
        cell_bytes,
        "file");
    if (bytes.size() != expected_size) {
        throw std::runtime_error(
            "MapPackage heightfield byte size does not match its header: "
            + path.string());
    }

    if (!finite(origin_east) || !finite(origin_north)
        || !finite(column_step_east) || !finite(column_step_north)
        || !finite(row_step_east) || !finite(row_step_north)) {
        throw std::runtime_error(
            "MapPackage heightfield basis must be finite: " + path.string());
    }
    const double column_length = std::hypot(
        column_step_east, column_step_north);
    const double row_length = std::hypot(row_step_east, row_step_north);
    const double determinant =
        column_step_east * row_step_north
        - column_step_north * row_step_east;
    if (!finite(column_length) || !finite(row_length)
        || column_length <= 1e-6 || row_length <= 1e-6
        || !finite(determinant)
        || std::abs(determinant)
            <= 1e-12 * column_length * row_length) {
        throw std::runtime_error(
            "MapPackage heightfield basis must be finite and invertible: "
            + path.string());
    }
    const double maximum_column = static_cast<double>(columns - 1);
    const double maximum_row = static_cast<double>(rows - 1);
    const std::array<GroundPointEnu, 4> horizontal_corners{{
        {origin_east, origin_north, 0.0},
        {
            origin_east + maximum_column * column_step_east,
            origin_north + maximum_column * column_step_north,
            0.0,
        },
        {
            origin_east + maximum_row * row_step_east,
            origin_north + maximum_row * row_step_north,
            0.0,
        },
        {
            origin_east + maximum_column * column_step_east
                + maximum_row * row_step_east,
            origin_north + maximum_column * column_step_north
                + maximum_row * row_step_north,
            0.0,
        },
    }};
    if (!std::all_of(
            horizontal_corners.begin(),
            horizontal_corners.end(),
            [](const GroundPointEnu& corner) {
                return finite(corner.east_m) && finite(corner.north_m);
            })) {
        throw std::runtime_error(
            "MapPackage heightfield lattice corners must be finite: "
            + path.string());
    }
    const auto [minimum_east, maximum_east] = std::minmax_element(
        horizontal_corners.begin(),
        horizontal_corners.end(),
        [](const GroundPointEnu& lhs, const GroundPointEnu& rhs) {
            return lhs.east_m < rhs.east_m;
        });
    const auto [minimum_north, maximum_north] = std::minmax_element(
        horizontal_corners.begin(),
        horizontal_corners.end(),
        [](const GroundPointEnu& lhs, const GroundPointEnu& rhs) {
            return lhs.north_m < rhs.north_m;
        });
    if (!finite(maximum_east->east_m - minimum_east->east_m)
        || !finite(maximum_north->north_m - minimum_north->north_m)) {
        throw std::runtime_error(
            "MapPackage heightfield lattice spans must be finite: "
            + path.string());
    }

    HeightfieldGroundQuery result;
    result.format_version_ = version;
    result.columns_ = columns;
    result.rows_ = rows;
    result.origin_east_m_ = origin_east;
    result.origin_north_m_ = origin_north;
    result.column_step_east_m_ = column_step_east;
    result.column_step_north_m_ = column_step_north;
    result.row_step_east_m_ = row_step_east;
    result.row_step_north_m_ = row_step_north;
    result.inverse_basis_determinant_ = 1.0 / determinant;
    result.samples_.reserve(sample_count);

    for (std::size_t index = 0; index < sample_count; ++index) {
        const std::uint32_t flags = reader.read_u32();
        const float up = reader.read_f32();
        float normal_east = reader.read_f32();
        float normal_north = reader.read_f32();
        float normal_up = reader.read_f32();
        if ((flags & ~kSampleValid) != 0u
            || !std::isfinite(up)
            || !std::isfinite(normal_east)
            || !std::isfinite(normal_north)
            || !std::isfinite(normal_up)) {
            throw std::runtime_error(
                "MapPackage heightfield contains invalid sample data: "
                + path.string());
        }
        const bool valid_sample = (flags & kSampleValid) != 0u;
        if (valid_sample) {
            const double normal_length = std::hypot(
                static_cast<double>(normal_east),
                static_cast<double>(normal_north),
                static_cast<double>(normal_up));
            if (!finite(normal_length) || normal_length <= kNormalEpsilon
                || normal_up <= 0.0f) {
                throw std::runtime_error(
                    "MapPackage heightfield valid sample requires an ENU-up normal: "
                    + path.string());
            }
            normal_east = static_cast<float>(normal_east / normal_length);
            normal_north = static_cast<float>(normal_north / normal_length);
            normal_up = static_cast<float>(normal_up / normal_length);
        }
        result.samples_.push_back({
            up, normal_east, normal_north, normal_up, valid_sample});
    }

    result.cells_.reserve(cell_count);
    for (std::uint32_t row = 0; row < rows - 1; ++row) {
        for (std::uint32_t column = 0; column < columns - 1; ++column) {
            const std::uint32_t flags = reader.read_u32();
            const std::uint32_t material_id = is_v2
                ? reader.read_u32()
                : static_cast<std::uint32_t>(
                    GroundSurfaceMaterialId::Default);
            const float friction_multiplier = is_v2
                ? reader.read_f32()
                : 1.0f;
            if ((flags & ~kCellDrivable) != 0u) {
                throw std::runtime_error(
                    "MapPackage heightfield contains unsupported cell flags: "
                    + path.string());
            }
            const bool drivable = (flags & kCellDrivable) != 0u;
            if (!std::isfinite(friction_multiplier)
                || friction_multiplier < kMinimumFrictionMultiplier
                || friction_multiplier > kMaximumFrictionMultiplier) {
                throw std::runtime_error(
                    "MapPackage heightfield cell friction multiplier must be finite and in [0.05, 4]: "
                    + path.string());
            }
            if (!drivable
                && (material_id != static_cast<std::uint32_t>(
                        GroundSurfaceMaterialId::Default)
                    || friction_multiplier != 1.0f)) {
                throw std::runtime_error(
                    "MapPackage heightfield non-drivable cell must use default material metadata: "
                    + path.string());
            }
            if (drivable) {
                const auto p00 = result.sample_index(column, row);
                const auto p10 = result.sample_index(column + 1, row);
                const auto p01 = result.sample_index(column, row + 1);
                const auto p11 = result.sample_index(column + 1, row + 1);
                if (!result.samples_[p00].valid
                    || !result.samples_[p10].valid
                    || !result.samples_[p01].valid
                    || !result.samples_[p11].valid) {
                    throw std::runtime_error(
                        "MapPackage heightfield drivable cell references an invalid sample: "
                        + path.string());
                }
                ++result.drivable_cell_count_;
            }
            result.cells_.push_back({
                drivable,
                static_cast<GroundSurfaceMaterialId>(material_id),
                friction_multiplier});
        }
    }
    if (reader.offset() != bytes.size()) {
        throw std::logic_error("Heightfield parser did not consume its payload");
    }
    if (result.drivable_cell_count_ == 0) {
        throw std::runtime_error(
            "MapPackage heightfield contains no drivable cells: "
            + path.string());
    }

    const double infinity = std::numeric_limits<double>::infinity();
    result.ground_bounds_enu_.minimum = {infinity, infinity, infinity};
    result.ground_bounds_enu_.maximum = {-infinity, -infinity, -infinity};
    for (std::uint32_t row = 0; row < rows - 1; ++row) {
        for (std::uint32_t column = 0; column < columns - 1; ++column) {
            const std::size_t cell_index =
                static_cast<std::size_t>(row) * (columns - 1) + column;
            if (!result.cells_[cell_index].drivable) {
                continue;
            }
            for (std::uint32_t row_delta = 0; row_delta <= 1; ++row_delta) {
                for (std::uint32_t column_delta = 0;
                     column_delta <= 1;
                     ++column_delta) {
                    const std::uint32_t sample_column = column + column_delta;
                    const std::uint32_t sample_row = row + row_delta;
                    const auto& sample = result.samples_[
                        result.sample_index(sample_column, sample_row)];
                    expand_bounds(
                        result.ground_bounds_enu_,
                        result.horizontal_point(
                            sample_column, sample_row, sample.up_m));
                }
            }
        }
    }
    return result;
}

std::optional<GroundHit> HeightfieldGroundQuery::query_down(
    const GroundQueryRequest& request) const
{
    if (!finite(request.origin_enu.east_m)
        || !finite(request.origin_enu.north_m)
        || !finite(request.origin_enu.up_m)
        || !finite(request.max_distance_m)
        || request.max_distance_m < 0.0) {
        return std::nullopt;
    }

    const double delta_east = request.origin_enu.east_m - origin_east_m_;
    const double delta_north = request.origin_enu.north_m - origin_north_m_;
    double lattice_column = (
        delta_east * row_step_north_m_
        - delta_north * row_step_east_m_) * inverse_basis_determinant_;
    double lattice_row = (
        column_step_east_m_ * delta_north
        - column_step_north_m_ * delta_east) * inverse_basis_determinant_;
    const auto snap_grid_coordinate = [](double value) {
        const double nearest_integer = std::round(value);
        return std::abs(value - nearest_integer) <= kGridCoordinateEpsilon
            ? nearest_integer
            : value;
    };
    lattice_column = snap_grid_coordinate(lattice_column);
    lattice_row = snap_grid_coordinate(lattice_row);
    const double maximum_column = static_cast<double>(columns_ - 1);
    const double maximum_row = static_cast<double>(rows_ - 1);
    if (!finite(lattice_column) || !finite(lattice_row)
        || lattice_column < -kGridCoordinateEpsilon
        || lattice_column > maximum_column + kGridCoordinateEpsilon
        || lattice_row < -kGridCoordinateEpsilon
        || lattice_row > maximum_row + kGridCoordinateEpsilon) {
        return std::nullopt;
    }
    lattice_column = std::clamp(lattice_column, 0.0, maximum_column);
    lattice_row = std::clamp(lattice_row, 0.0, maximum_row);
    const auto column = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(std::floor(lattice_column)),
        columns_ - 2);
    const auto row = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(std::floor(lattice_row)),
        rows_ - 2);
    const std::size_t cell_index =
        static_cast<std::size_t>(row) * (columns_ - 1) + column;
    const Cell& cell = cells_[cell_index];
    if (!cell.drivable) {
        return std::nullopt;
    }

    const double local_column = lattice_column - column;
    const double local_row = lattice_row - row;
    const Sample& p00 = samples_[sample_index(column, row)];
    const Sample& p10 = samples_[sample_index(column + 1, row)];
    const Sample& p01 = samples_[sample_index(column, row + 1)];
    const Sample& p11 = samples_[sample_index(column + 1, row + 1)];

    std::array<const Sample*, 3> triangle{};
    std::array<double, 3> weights{};
    if (local_row <= local_column) {
        triangle = {&p00, &p10, &p11};
        weights = {
            1.0 - local_column,
            local_column - local_row,
            local_row};
    } else {
        triangle = {&p00, &p11, &p01};
        weights = {
            1.0 - local_row,
            local_column,
            local_row - local_column};
    }

    double height = 0.0;
    GroundPointEnu normal{};
    for (std::size_t index = 0; index < triangle.size(); ++index) {
        height += weights[index] * triangle[index]->up_m;
        normal.east_m += weights[index] * triangle[index]->normal_east;
        normal.north_m += weights[index] * triangle[index]->normal_north;
        normal.up_m += weights[index] * triangle[index]->normal_up;
    }
    const double normal_length = std::hypot(
        normal.east_m, normal.north_m, normal.up_m);
    const double distance = request.origin_enu.up_m - height;
    if (!finite(height) || !finite(normal_length)
        || normal_length <= kNormalEpsilon || normal.up_m <= 0.0
        || !finite(distance) || distance < 0.0
        || distance > request.max_distance_m) {
        return std::nullopt;
    }
    normal.east_m /= normal_length;
    normal.north_m /= normal_length;
    normal.up_m /= normal_length;
    return GroundHit{
        {request.origin_enu.east_m, request.origin_enu.north_m, height},
        normal,
        distance,
        cell.surface_material_id,
        cell.friction_multiplier};
}

std::size_t HeightfieldGroundQuery::sample_index(
    std::uint32_t column, std::uint32_t row) const
{
    return static_cast<std::size_t>(row) * columns_ + column;
}

GroundPointEnu HeightfieldGroundQuery::horizontal_point(
    std::uint32_t column, std::uint32_t row, double up_m) const
{
    return {
        origin_east_m_
            + static_cast<double>(column) * column_step_east_m_
            + static_cast<double>(row) * row_step_east_m_,
        origin_north_m_
            + static_cast<double>(column) * column_step_north_m_
            + static_cast<double>(row) * row_step_north_m_,
        up_m};
}

} // namespace simcore_host
