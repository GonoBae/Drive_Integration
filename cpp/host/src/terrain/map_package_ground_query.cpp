#include "terrain/map_package_ground_query.hpp"

#include "terrain/map_package_manifest.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace simcore_host {
namespace {

constexpr double kProjectedAreaEpsilon = 1e-12;
constexpr double kBarycentricEpsilon = 1e-9;
constexpr double kMinimumGridCellSizeM = 0.25;
constexpr double kGridBoundsEpsilonM = 1e-9;
constexpr std::size_t kGroundGridReferencesPerTriangleBudget = 32;
constexpr std::size_t kMinimumGroundGridReferenceBudget = 4096;
constexpr std::size_t kMaximumGroundGridReferenceBudget = 4'000'000;
constexpr std::size_t kMaximumGroundGridCellsPerTriangle = 4096;

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::vector<std::string> split_csv_row(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto separator = line.find(',', start);
        fields.push_back(trim(std::string_view(line).substr(
            start,
            separator == std::string::npos ? std::string::npos : separator - start)));
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return fields;
}

double parse_finite_double(
    const std::filesystem::path& path,
    std::size_t line_number,
    const std::string& text)
{
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0'
        || !std::isfinite(value)) {
        throw std::runtime_error(
            path.string() + ":" + std::to_string(line_number)
            + " contains an invalid finite coordinate: " + text);
    }
    return value;
}

GroundPointEnu subtract(const GroundPointEnu& lhs, const GroundPointEnu& rhs)
{
    return {
        lhs.east_m - rhs.east_m,
        lhs.north_m - rhs.north_m,
        lhs.up_m - rhs.up_m};
}

GroundPointEnu cross(const GroundPointEnu& lhs, const GroundPointEnu& rhs)
{
    return {
        lhs.north_m * rhs.up_m - lhs.up_m * rhs.north_m,
        lhs.up_m * rhs.east_m - lhs.east_m * rhs.up_m,
        lhs.east_m * rhs.north_m - lhs.north_m * rhs.east_m};
}

bool finite_point(const GroundPointEnu& point)
{
    return std::isfinite(point.east_m)
        && std::isfinite(point.north_m)
        && std::isfinite(point.up_m);
}

double projected_area_twice(const GroundTriangle& triangle)
{
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    return (b.north_m - c.north_m) * (a.east_m - c.east_m)
         + (c.east_m - b.east_m) * (a.north_m - c.north_m);
}

double triangle_min_east(const GroundTriangle& triangle)
{
    return std::min({triangle.vertices[0].east_m,
                     triangle.vertices[1].east_m,
                     triangle.vertices[2].east_m});
}

double triangle_max_east(const GroundTriangle& triangle)
{
    return std::max({triangle.vertices[0].east_m,
                     triangle.vertices[1].east_m,
                     triangle.vertices[2].east_m});
}

double triangle_min_north(const GroundTriangle& triangle)
{
    return std::min({triangle.vertices[0].north_m,
                     triangle.vertices[1].north_m,
                     triangle.vertices[2].north_m});
}

double triangle_max_north(const GroundTriangle& triangle)
{
    return std::max({triangle.vertices[0].north_m,
                     triangle.vertices[1].north_m,
                     triangle.vertices[2].north_m});
}

struct GroundHorizontalBounds {
    double minimum_east_m = 0.0;
    double maximum_east_m = 0.0;
    double minimum_north_m = 0.0;
    double maximum_north_m = 0.0;
};

GroundHorizontalBounds conservative_horizontal_bounds(
    const GroundTriangle& triangle)
{
    const double minimum_east = triangle_min_east(triangle);
    const double maximum_east = triangle_max_east(triangle);
    const double minimum_north = triangle_min_north(triangle);
    const double maximum_north = triangle_max_north(triangle);
    const double east_span = maximum_east - minimum_east;
    const double north_span = maximum_north - minimum_north;
    const double east_rounding = 16.0 * std::numeric_limits<double>::epsilon()
        * std::max({1.0, std::abs(minimum_east), std::abs(maximum_east)});
    const double north_rounding = 16.0 * std::numeric_limits<double>::epsilon()
        * std::max({1.0, std::abs(minimum_north), std::abs(maximum_north)});
    const double east_padding =
        2.0 * kBarycentricEpsilon * east_span + east_rounding;
    const double north_padding =
        2.0 * kBarycentricEpsilon * north_span + north_rounding;
    GroundHorizontalBounds bounds{
        minimum_east - east_padding,
        maximum_east + east_padding,
        minimum_north - north_padding,
        maximum_north + north_padding};
    if (!std::isfinite(east_span) || !std::isfinite(north_span)
        || !std::isfinite(bounds.minimum_east_m)
        || !std::isfinite(bounds.maximum_east_m)
        || !std::isfinite(bounds.minimum_north_m)
        || !std::isfinite(bounds.maximum_north_m)) {
        throw std::invalid_argument(
            "Ground triangle bounds overflow their finite ENU range");
    }
    return bounds;
}

} // namespace

MapPackageGroundQuery::MapPackageGroundQuery(
    std::vector<GroundTriangle> triangles)
    : MapPackageGroundQuery(std::move(triangles), {}, {})
{
}

MapPackageGroundQuery::MapPackageGroundQuery(
    std::vector<GroundTriangle> triangles,
    std::string package_id,
    std::string collision_checksum)
    : triangles_(std::move(triangles))
    , package_id_(std::move(package_id))
    , collision_checksum_(std::move(collision_checksum))
{
    if (triangles_.empty()) {
        throw std::invalid_argument(
            "MapPackage ground surface must contain at least one triangle");
    }
    for (const auto& triangle : triangles_) {
        if (triangle.surface_id.empty()) {
            throw std::invalid_argument("Ground triangle surface_id cannot be empty");
        }
        if (!std::all_of(
                triangle.vertices.begin(), triangle.vertices.end(), finite_point)) {
            throw std::invalid_argument("Ground triangle coordinates must be finite");
        }
        const double projected_area = projected_area_twice(triangle);
        const auto normal = cross(
            subtract(triangle.vertices[1], triangle.vertices[0]),
            subtract(triangle.vertices[2], triangle.vertices[0]));
        const double normal_length = std::hypot(
            normal.east_m, normal.north_m, normal.up_m);
        if (!std::isfinite(projected_area)
            || std::abs(projected_area) <= kProjectedAreaEpsilon
            || !finite_point(normal)
            || !std::isfinite(normal_length)
            || normal_length <= kProjectedAreaEpsilon) {
            throw std::invalid_argument(
                "Ground triangle geometry must have finite non-zero area");
        }
        (void)conservative_horizontal_bounds(triangle);
    }
    build_spatial_index();
}

void MapPackageGroundQuery::build_spatial_index()
{
    ground_bounds_enu_.minimum = {
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    ground_bounds_enu_.maximum = {
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    grid_min_east_m_ = std::numeric_limits<double>::infinity();
    grid_min_north_m_ = std::numeric_limits<double>::infinity();
    grid_max_east_m_ = -std::numeric_limits<double>::infinity();
    grid_max_north_m_ = -std::numeric_limits<double>::infinity();
    for (const auto& triangle : triangles_) {
        for (const auto& vertex : triangle.vertices) {
            ground_bounds_enu_.minimum.east_m = std::min(
                ground_bounds_enu_.minimum.east_m, vertex.east_m);
            ground_bounds_enu_.minimum.north_m = std::min(
                ground_bounds_enu_.minimum.north_m, vertex.north_m);
            ground_bounds_enu_.minimum.up_m = std::min(
                ground_bounds_enu_.minimum.up_m, vertex.up_m);
            ground_bounds_enu_.maximum.east_m = std::max(
                ground_bounds_enu_.maximum.east_m, vertex.east_m);
            ground_bounds_enu_.maximum.north_m = std::max(
                ground_bounds_enu_.maximum.north_m, vertex.north_m);
            ground_bounds_enu_.maximum.up_m = std::max(
                ground_bounds_enu_.maximum.up_m, vertex.up_m);
        }
        const auto bounds = conservative_horizontal_bounds(triangle);
        grid_min_east_m_ = std::min(
            grid_min_east_m_, bounds.minimum_east_m);
        grid_min_north_m_ = std::min(
            grid_min_north_m_, bounds.minimum_north_m);
        grid_max_east_m_ = std::max(
            grid_max_east_m_, bounds.maximum_east_m);
        grid_max_north_m_ = std::max(
            grid_max_north_m_, bounds.maximum_north_m);
    }

    const double east_span = grid_max_east_m_ - grid_min_east_m_;
    const double north_span = grid_max_north_m_ - grid_min_north_m_;
    if (!std::isfinite(east_span) || !std::isfinite(north_span)
        || east_span <= 0.0 || north_span <= 0.0) {
        throw std::invalid_argument(
            "MapPackage ground bounds must be finite and two-dimensional");
    }

    // The longest axis is divided into roughly sqrt(N) cells. This keeps the
    // grid O(N) for both dense landscape meshes and a package made of only a
    // few very large bootstrap triangles.
    const double target_axis_cells = std::max(
        1.0, std::sqrt(static_cast<double>(triangles_.size())));
    grid_cell_size_m_ = std::max(
        kMinimumGridCellSizeM,
        std::max(east_span, north_span) / target_axis_cells);
    grid_columns_ = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(east_span / grid_cell_size_m_)));
    grid_rows_ = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(north_span / grid_cell_size_m_)));
    if (grid_columns_ > std::numeric_limits<std::size_t>::max() / grid_rows_) {
        throw std::invalid_argument("MapPackage ground spatial index is too large");
    }
    triangle_indices_by_cell_.assign(grid_columns_ * grid_rows_, {});
    global_triangle_indices_.clear();

    const std::size_t proportional_reference_budget =
        triangles_.size()
            > kMaximumGroundGridReferenceBudget
                / kGroundGridReferencesPerTriangleBudget
        ? kMaximumGroundGridReferenceBudget
        : triangles_.size() * kGroundGridReferencesPerTriangleBudget;
    const std::size_t reference_budget = std::min(
        kMaximumGroundGridReferenceBudget,
        std::max(
            kMinimumGroundGridReferenceBudget,
            proportional_reference_budget));
    std::size_t reference_count = 0;

    const auto coordinate_to_cell = [](
        double coordinate,
        double minimum,
        double cell_size,
        std::size_t cell_count) {
        const double raw = std::floor((coordinate - minimum) / cell_size);
        if (raw <= 0.0) {
            return std::size_t{0};
        }
        return std::min(
            static_cast<std::size_t>(raw), cell_count - 1);
    };

    for (std::size_t triangle_index = 0;
         triangle_index < triangles_.size();
         ++triangle_index) {
        const auto& triangle = triangles_[triangle_index];
        const auto bounds = conservative_horizontal_bounds(triangle);
        const std::size_t minimum_column = coordinate_to_cell(
            bounds.minimum_east_m,
            grid_min_east_m_, grid_cell_size_m_, grid_columns_);
        const std::size_t maximum_column = coordinate_to_cell(
            bounds.maximum_east_m,
            grid_min_east_m_, grid_cell_size_m_, grid_columns_);
        const std::size_t minimum_row = coordinate_to_cell(
            bounds.minimum_north_m,
            grid_min_north_m_, grid_cell_size_m_, grid_rows_);
        const std::size_t maximum_row = coordinate_to_cell(
            bounds.maximum_north_m,
            grid_min_north_m_, grid_cell_size_m_, grid_rows_);
        const std::size_t column_count = maximum_column - minimum_column + 1;
        const std::size_t row_count = maximum_row - minimum_row + 1;
        const bool cell_count_overflows =
            column_count > std::numeric_limits<std::size_t>::max() / row_count;
        const std::size_t triangle_reference_count = cell_count_overflows
            ? std::numeric_limits<std::size_t>::max()
            : column_count * row_count;
        if (cell_count_overflows
            || triangle_reference_count > kMaximumGroundGridCellsPerTriangle
            || triangle_reference_count > reference_budget - reference_count) {
            global_triangle_indices_.push_back(triangle_index);
            continue;
        }
        for (std::size_t row = minimum_row; row <= maximum_row; ++row) {
            for (std::size_t column = minimum_column;
                 column <= maximum_column;
                 ++column) {
                triangle_indices_by_cell_[row * grid_columns_ + column]
                    .push_back(triangle_index);
            }
        }
        reference_count += triangle_reference_count;
    }
}

const std::vector<std::size_t>* MapPackageGroundQuery::query_candidates(
    double east_m, double north_m) const
{
    if (east_m < grid_min_east_m_ - kGridBoundsEpsilonM
        || east_m > grid_max_east_m_ + kGridBoundsEpsilonM
        || north_m < grid_min_north_m_ - kGridBoundsEpsilonM
        || north_m > grid_max_north_m_ + kGridBoundsEpsilonM) {
        return nullptr;
    }

    const auto coordinate_to_cell = [](
        double coordinate,
        double minimum,
        double cell_size,
        std::size_t cell_count) {
        const double raw = std::floor((coordinate - minimum) / cell_size);
        if (raw <= 0.0) {
            return std::size_t{0};
        }
        return std::min(
            static_cast<std::size_t>(raw), cell_count - 1);
    };
    const std::size_t column = coordinate_to_cell(
        east_m, grid_min_east_m_, grid_cell_size_m_, grid_columns_);
    const std::size_t row = coordinate_to_cell(
        north_m, grid_min_north_m_, grid_cell_size_m_, grid_rows_);
    return &triangle_indices_by_cell_[row * grid_columns_ + column];
}

std::size_t MapPackageGroundQuery::maximum_query_candidate_count() const
{
    std::size_t maximum = global_triangle_indices_.size();
    for (const auto& candidates : triangle_indices_by_cell_) {
        maximum = std::max(
            maximum,
            global_triangle_indices_.size() + candidates.size());
    }
    return maximum;
}

MapPackageGroundQuery MapPackageGroundQuery::load(
    const std::filesystem::path& package_directory)
{
    const auto manifest = load_map_package_manifest(package_directory);
    const auto path = manifest.package_directory / "ground_surface.csv";
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "MapPackage ground surface not found: " + path.string());
    }

    std::vector<GroundTriangle> triangles;
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;
    while (std::getline(input, line)) {
        ++line_number;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto fields = split_csv_row(line);
        if (!header_seen) {
            static const std::vector<std::string> expected_header{
                "surface_id", "e0", "n0", "u0", "e1", "n1", "u1",
                "e2", "n2", "u2"};
            if (fields != expected_header) {
                throw std::runtime_error(
                    path.string() + ":" + std::to_string(line_number)
                    + " has an unsupported ground_surface.csv header");
            }
            header_seen = true;
            continue;
        }

        if (fields.size() != 10 || fields[0].empty()) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_number)
                + " must contain surface_id and nine ENU coordinates");
        }
        GroundTriangle triangle;
        triangle.surface_id = fields[0];
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            triangle.vertices[vertex] = {
                parse_finite_double(path, line_number, fields[1 + vertex * 3]),
                parse_finite_double(path, line_number, fields[2 + vertex * 3]),
                parse_finite_double(path, line_number, fields[3 + vertex * 3])};
        }
        triangles.push_back(std::move(triangle));
    }

    if (!header_seen) {
        throw std::runtime_error(
            "MapPackage ground surface is missing its header: " + path.string());
    }

    // The manifest verification above and this parser intentionally use
    // separate streams. Recompute after parsing so an editor export that
    // atomically replaces collision payloads during server startup cannot make
    // us advertise the old identity while simulating bytes from the new bake.
    const auto checksum_after_parse = compute_map_package_collision_checksum(
        manifest.package_directory, manifest.collision_files);
    if (checksum_after_parse != manifest.collision_checksum) {
        throw std::runtime_error(
            "MapPackage collision payload changed while it was loading: "
            + manifest.package_directory.string());
    }
    return MapPackageGroundQuery(
        std::move(triangles),
        manifest.map_id,
        manifest.collision_checksum);
}

std::optional<GroundHit> MapPackageGroundQuery::query_down(
    const GroundQueryRequest& request) const
{
    if (!finite_point(request.origin_enu)
        || !std::isfinite(request.max_distance_m)
        || request.max_distance_m < 0.0) {
        return std::nullopt;
    }

    const auto* candidates = query_candidates(
        request.origin_enu.east_m, request.origin_enu.north_m);
    if (candidates == nullptr) {
        return std::nullopt;
    }

    std::optional<GroundHit> nearest;
    std::size_t cell_index = 0;
    std::size_t global_index = 0;
    while (cell_index < candidates->size()
           || global_index < global_triangle_indices_.size()) {
        std::size_t triangle_index = 0;
        if (global_index >= global_triangle_indices_.size()
            || (cell_index < candidates->size()
                && (*candidates)[cell_index]
                    < global_triangle_indices_[global_index])) {
            triangle_index = (*candidates)[cell_index++];
        } else {
            triangle_index = global_triangle_indices_[global_index++];
        }
        const auto& triangle = triangles_[triangle_index];
        const auto& a = triangle.vertices[0];
        const auto& b = triangle.vertices[1];
        const auto& c = triangle.vertices[2];
        const double denominator = projected_area_twice(triangle);
        const double weight_a = (
            (b.north_m - c.north_m)
                * (request.origin_enu.east_m - c.east_m)
            + (c.east_m - b.east_m)
                * (request.origin_enu.north_m - c.north_m)) / denominator;
        const double weight_b = (
            (c.north_m - a.north_m)
                * (request.origin_enu.east_m - c.east_m)
            + (a.east_m - c.east_m)
                * (request.origin_enu.north_m - c.north_m)) / denominator;
        const double weight_c = 1.0 - weight_a - weight_b;
        if (!std::isfinite(weight_a)
            || !std::isfinite(weight_b)
            || !std::isfinite(weight_c)
            || weight_a < -kBarycentricEpsilon
            || weight_b < -kBarycentricEpsilon
            || weight_c < -kBarycentricEpsilon) {
            continue;
        }

        const double height = weight_a * a.up_m
                            + weight_b * b.up_m
                            + weight_c * c.up_m;
        const double distance = request.origin_enu.up_m - height;
        if (!std::isfinite(height) || !std::isfinite(distance)
            || distance < 0.0 || distance > request.max_distance_m
            || (nearest && distance >= nearest->distance_m)) {
            continue;
        }

        auto normal = cross(subtract(b, a), subtract(c, a));
        if (normal.up_m < 0.0) {
            normal.east_m = -normal.east_m;
            normal.north_m = -normal.north_m;
            normal.up_m = -normal.up_m;
        }
        const double length = std::hypot(
            normal.east_m, normal.north_m, normal.up_m);
        if (!finite_point(normal) || !std::isfinite(length)
            || length <= kProjectedAreaEpsilon || normal.up_m <= 0.0) {
            continue;
        }

        nearest = GroundHit{
            {request.origin_enu.east_m, request.origin_enu.north_m, height},
            {normal.east_m / length, normal.north_m / length, normal.up_m / length},
            distance};
    }
    return nearest;
}

} // namespace simcore_host
