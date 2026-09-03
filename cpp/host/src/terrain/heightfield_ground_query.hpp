#pragma once

#include "terrain/ground_query.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace simcore_host {

inline constexpr char kGroundHeightfieldFileName[] =
    "ground_heightfield.bin";
inline constexpr std::uint32_t kGroundHeightfieldFormatVersionV1 = 1;
inline constexpr std::uint32_t kGroundHeightfieldFormatVersion = 2;
inline constexpr std::uint32_t kGroundHeightfieldHeaderSize = 80;
inline constexpr std::uint32_t kGroundHeightfieldSampleStride = 20;
inline constexpr std::uint32_t kGroundHeightfieldCellStrideV1 = 4;
inline constexpr std::uint32_t kGroundHeightfieldCellStride = 12;
inline constexpr std::size_t kMaximumGroundHeightfieldSamples = 2'000'000;

// Read-only query over the packed global lattice authored by Unreal. SIMGHF2
// keeps the v1 80-byte header and 20-byte sample layout, and extends each cell
// from a 4-byte drivable flag to flags + stable material ID + finite positive
// friction multiplier. SIMGHF1 remains accepted as default material/1.0.
// Unreal owns collision measurement/classification; this class only validates
// and queries the immutable ENU snapshot for the server-side vehicle solver.
class HeightfieldGroundQuery final : public GroundQuery {
public:
    [[nodiscard]] static HeightfieldGroundQuery load(
        const std::filesystem::path& path);

    [[nodiscard]] std::optional<GroundHit> query_down(
        const GroundQueryRequest& request) const override;

    [[nodiscard]] std::uint32_t columns() const { return columns_; }
    [[nodiscard]] std::uint32_t rows() const { return rows_; }
    [[nodiscard]] std::size_t sample_count() const { return samples_.size(); }
    [[nodiscard]] std::size_t cell_count() const { return cells_.size(); }
    [[nodiscard]] std::size_t drivable_cell_count() const {
        return drivable_cell_count_;
    }
    [[nodiscard]] std::uint32_t format_version() const {
        return format_version_;
    }
    [[nodiscard]] const GroundBoundsEnu& bounds_enu() const {
        return ground_bounds_enu_;
    }

private:
    struct Sample {
        float up_m = 0.0f;
        float normal_east = 0.0f;
        float normal_north = 0.0f;
        float normal_up = 0.0f;
        bool valid = false;
    };

    struct Cell {
        bool drivable = false;
        GroundSurfaceMaterialId surface_material_id =
            GroundSurfaceMaterialId::Default;
        float friction_multiplier = 1.0f;
    };

    [[nodiscard]] std::size_t sample_index(
        std::uint32_t column, std::uint32_t row) const;
    [[nodiscard]] GroundPointEnu horizontal_point(
        std::uint32_t column, std::uint32_t row, double up_m) const;

    std::uint32_t columns_ = 0;
    std::uint32_t rows_ = 0;
    double origin_east_m_ = 0.0;
    double origin_north_m_ = 0.0;
    double column_step_east_m_ = 0.0;
    double column_step_north_m_ = 0.0;
    double row_step_east_m_ = 0.0;
    double row_step_north_m_ = 0.0;
    double inverse_basis_determinant_ = 0.0;
    std::vector<Sample> samples_;
    std::vector<Cell> cells_;
    std::size_t drivable_cell_count_ = 0;
    std::uint32_t format_version_ = kGroundHeightfieldFormatVersionV1;
    GroundBoundsEnu ground_bounds_enu_{};
};

} // namespace simcore_host
