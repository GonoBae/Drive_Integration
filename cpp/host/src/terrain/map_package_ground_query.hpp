#pragma once

#include "terrain/ground_query.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace simcore_host {

struct GroundTriangle {
    std::string surface_id;
    std::array<GroundPointEnu, 3> vertices;
};

// Deterministic vertical-ray provider backed by the MapPackage
// ground_surface.csv collision source. Triangles are indexed into an adaptive
// uniform ENU grid at load time; each vertical query preserves source-order
// tie behaviour while visiting only the containing cell's candidates.
class MapPackageGroundQuery final : public GroundQuery {
public:
    explicit MapPackageGroundQuery(std::vector<GroundTriangle> triangles);

    [[nodiscard]] static MapPackageGroundQuery load(
        const std::filesystem::path& package_directory);

    [[nodiscard]] std::optional<GroundHit> query_down(
        const GroundQueryRequest& request) const override;

    [[nodiscard]] std::size_t triangle_count() const { return triangles_.size(); }
    [[nodiscard]] std::size_t spatial_cell_count() const {
        return triangle_indices_by_cell_.size();
    }
    [[nodiscard]] std::size_t spatial_global_triangle_count() const {
        return global_triangle_indices_.size();
    }
    [[nodiscard]] std::size_t maximum_query_candidate_count() const;
    [[nodiscard]] const std::string& package_id() const { return package_id_; }
    [[nodiscard]] const std::string& collision_checksum() const {
        return collision_checksum_;
    }

private:
    MapPackageGroundQuery(
        std::vector<GroundTriangle> triangles,
        std::string package_id,
        std::string collision_checksum);

    void build_spatial_index();
    [[nodiscard]] const std::vector<std::size_t>* query_candidates(
        double east_m, double north_m) const;

    std::vector<GroundTriangle> triangles_;
    std::vector<std::vector<std::size_t>> triangle_indices_by_cell_;
    std::vector<std::size_t> global_triangle_indices_;
    double grid_min_east_m_ = 0.0;
    double grid_min_north_m_ = 0.0;
    double grid_max_east_m_ = 0.0;
    double grid_max_north_m_ = 0.0;
    double grid_cell_size_m_ = 1.0;
    std::size_t grid_columns_ = 0;
    std::size_t grid_rows_ = 0;
    std::string package_id_;
    std::string collision_checksum_;
};

} // namespace simcore_host
