#include "terrain/map_package_runtime.hpp"

#include "collision/map_package_static_collision.hpp"
#include "terrain/heightfield_ground_query.hpp"
#include "terrain/map_package_ground_query.hpp"
#include "terrain/map_package_manifest.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace simcore_host {
namespace {

constexpr std::chrono::milliseconds kInitialRejectedRetryDelay{250};
constexpr std::chrono::milliseconds kMaximumRejectedRetryDelay{5000};

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

void validate_heightfield_ground_sentinel(
    const std::filesystem::path& package_directory)
{
    const auto path = package_directory / "ground_surface.csv";
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "MapPackage ground sentinel not found: " + path.string());
    }
    static constexpr std::string_view expected_header =
        "surface_id,e0,n0,u0,e1,n1,u1,e2,n2,u2";
    bool header_seen = false;
    std::string line;
    std::size_t line_number = 0;
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
        if (!header_seen && line == expected_header) {
            header_seen = true;
            continue;
        }
        throw std::runtime_error(
            path.string() + ":" + std::to_string(line_number)
            + " must be a header-only sentinel when ground_heightfield.bin is declared");
    }
    if (!header_seen) {
        throw std::runtime_error(
            "MapPackage heightfield ground sentinel is missing its header: "
            + path.string());
    }
}

} // namespace

const char* ground_payload_kind_name(GroundPayloadKind kind)
{
    switch (kind) {
    case GroundPayloadKind::TriangleCsvV1:
        return "triangle_csv_v1";
    case GroundPayloadKind::PackedHeightfieldV1:
        return "packed_heightfield_v1";
    case GroundPayloadKind::PackedHeightfieldV2:
        return "packed_heightfield_v2";
    }
    return "unknown";
}

RuntimeMapPackage load_runtime_map_package(
    const std::filesystem::path& package_directory)
{
    const auto manifest = load_map_package_manifest(package_directory);
    std::shared_ptr<const GroundQuery> ground_query;
    GroundSnapshotDiagnostics ground_diagnostics;
    const bool has_heightfield = std::find(
        manifest.collision_files.begin(),
        manifest.collision_files.end(),
        kGroundHeightfieldFileName) != manifest.collision_files.end();
    if (has_heightfield) {
        validate_heightfield_ground_sentinel(manifest.package_directory);
        auto heightfield = std::make_shared<HeightfieldGroundQuery>(
            HeightfieldGroundQuery::load(
                manifest.package_directory / kGroundHeightfieldFileName));
        ground_diagnostics.payload_kind = heightfield->format_version()
                == kGroundHeightfieldFormatVersion
            ? GroundPayloadKind::PackedHeightfieldV2
            : GroundPayloadKind::PackedHeightfieldV1;
        ground_diagnostics.bounds_enu = heightfield->bounds_enu();
        ground_diagnostics.sample_count = heightfield->sample_count();
        ground_diagnostics.cell_count = heightfield->cell_count();
        ground_diagnostics.drivable_cell_count =
            heightfield->drivable_cell_count();
        ground_diagnostics.lattice_columns = heightfield->columns();
        ground_diagnostics.lattice_rows = heightfield->rows();
        ground_query = std::move(heightfield);
    } else {
        auto triangles = std::make_shared<MapPackageGroundQuery>(
            MapPackageGroundQuery::load(package_directory));
        if (triangles->collision_checksum() != manifest.collision_checksum) {
            throw std::runtime_error(
                "MapPackage changed between manifest and ground loading");
        }
        ground_diagnostics.payload_kind = GroundPayloadKind::TriangleCsvV1;
        ground_diagnostics.bounds_enu = triangles->bounds_enu();
        ground_diagnostics.triangle_count = triangles->triangle_count();
        ground_diagnostics.spatial_cell_count =
            triangles->spatial_cell_count();
        ground_diagnostics.spatial_global_triangle_count =
            triangles->spatial_global_triangle_count();
        ground_diagnostics.maximum_query_candidate_count =
            triangles->maximum_query_candidate_count();
        ground_query = std::move(triangles);
    }

    auto collision_world = std::make_shared<CollisionWorld>(
        load_static_collision_world(manifest));
    const auto checksum_after_collision_load =
        compute_map_package_collision_checksum(
            manifest.package_directory,
            manifest.collision_files);
    if (checksum_after_collision_load != manifest.collision_checksum) {
        throw std::runtime_error(
            "MapPackage changed while static collision was loading");
    }

    return {
        manifest.map_id,
        manifest.collision_checksum,
        std::move(ground_query),
        std::move(collision_world),
        ground_diagnostics,
    };
}

MapPackageHotReloader::MapPackageHotReloader(
    std::filesystem::path package_directory,
    std::string active_collision_checksum,
    ReadyCallback on_ready,
    std::chrono::milliseconds poll_interval)
    : package_directory_(std::filesystem::absolute(
          std::move(package_directory)).lexically_normal())
    , active_collision_checksum_(std::move(active_collision_checksum))
    , on_ready_(std::move(on_ready))
    , poll_interval_(poll_interval)
{
    if (active_collision_checksum_.empty() || !on_ready_) {
        throw std::invalid_argument(
            "MapPackage hot reload requires an active checksum and callback");
    }
    if (poll_interval_ <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument(
            "MapPackage hot reload poll interval must be positive");
    }

    // Do not acknowledge any manifest bytes in the constructor. A Bake can
    // commit between separately reading/parsing the marker, which could pair
    // the old active checksum with newer bytes and make the first poll skip
    // that generation forever. poll_once() performs the complete
    // bytes-before/load/bytes-after transaction and acknowledges a stable
    // generation, including the unchanged startup package, without a callback.
}

MapPackageHotReloader::~MapPackageHotReloader()
{
    stop();
}

void MapPackageHotReloader::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    worker_ = std::thread([this] { run(); });
}

void MapPackageHotReloader::stop()
{
    if (!running_.exchange(false)) {
        return;
    }
    wait_condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string MapPackageHotReloader::read_manifest_bytes() const
{
    const auto path = package_directory_ / "manifest.cfg";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "MapPackage manifest not readable: " + path.string());
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

bool MapPackageHotReloader::poll_once()
{
    std::string manifest_bytes;
    bool manifest_read = false;
    try {
        manifest_bytes = read_manifest_bytes();
        manifest_read = true;
        if (has_accepted_manifest_bytes_
            && manifest_bytes == accepted_manifest_bytes_) {
            return false;
        }
        if (has_rejected_manifest_bytes_
            && manifest_bytes == rejected_manifest_bytes_
            && std::chrono::steady_clock::now() < rejected_retry_after_) {
            return false;
        }

        auto candidate = load_runtime_map_package(package_directory_);
        const std::string verified_manifest_bytes = read_manifest_bytes();
        if (verified_manifest_bytes != manifest_bytes) {
            throw std::runtime_error(
                "MapPackage manifest changed while reload candidate was loading");
        }
        if (candidate.collision_checksum == active_collision_checksum_) {
            accepted_manifest_bytes_ = verified_manifest_bytes;
            has_accepted_manifest_bytes_ = true;
            rejected_manifest_bytes_.clear();
            has_rejected_manifest_bytes_ = false;
            rejected_retry_delay_ = std::chrono::milliseconds::zero();
            last_failure_.clear();
            return false;
        }

        const std::string candidate_checksum = candidate.collision_checksum;
        const GroundSnapshotDiagnostics candidate_ground_diagnostics =
            candidate.ground_diagnostics;
        on_ready_(std::move(candidate));
        active_collision_checksum_ = candidate_checksum;
        accepted_manifest_bytes_ = verified_manifest_bytes;
        has_accepted_manifest_bytes_ = true;
        rejected_manifest_bytes_.clear();
        has_rejected_manifest_bytes_ = false;
        rejected_retry_delay_ = std::chrono::milliseconds::zero();
        last_failure_.clear();
        std::cout << "[MapReload] verified candidate checksum="
                  << active_collision_checksum_
                  << " ground_format="
                  << ground_payload_kind_name(
                         candidate_ground_diagnostics.payload_kind)
                  << " ground_samples="
                  << candidate_ground_diagnostics.sample_count
                  << " ground_cells="
		           << (candidate_ground_diagnostics.payload_kind
		                       != GroundPayloadKind::TriangleCsvV1
                          ? candidate_ground_diagnostics.cell_count
                          : candidate_ground_diagnostics.spatial_cell_count)
                  << " queued for tick boundary\n";
        return true;
    } catch (const std::exception& error) {
        const std::string failure = error.what();
        if (manifest_read) {
            if (has_rejected_manifest_bytes_
                && manifest_bytes == rejected_manifest_bytes_) {
                rejected_retry_delay_ = std::min(
                    rejected_retry_delay_ * 2,
                    kMaximumRejectedRetryDelay);
            } else {
                rejected_manifest_bytes_ = manifest_bytes;
                has_rejected_manifest_bytes_ = true;
                rejected_retry_delay_ = kInitialRejectedRetryDelay;
            }
            if (rejected_retry_delay_ <= std::chrono::milliseconds::zero()) {
                rejected_retry_delay_ = kInitialRejectedRetryDelay;
            }
            rejected_retry_after_ =
                std::chrono::steady_clock::now() + rejected_retry_delay_;
        }
        if (failure != last_failure_) {
            std::cerr << "[MapReload] candidate rejected: " << failure
                      << "; active map retained\n";
            last_failure_ = failure;
        }
        return false;
    }
}

void MapPackageHotReloader::run()
{
    while (running_.load()) {
        (void)poll_once();
        std::unique_lock lock(wait_mutex_);
        wait_condition_.wait_for(
            lock,
            poll_interval_,
            [this] { return !running_.load(); });
    }
}

} // namespace simcore_host
