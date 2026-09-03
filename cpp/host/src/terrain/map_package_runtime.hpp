#pragma once

#include "collision/collision_world.hpp"
#include "terrain/ground_query.hpp"
#include "terrain/map_package_ground_query.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace simcore_host {

enum class GroundPayloadKind {
    TriangleCsvV1,
    PackedHeightfieldV1,
    PackedHeightfieldV2,
};

struct GroundSnapshotDiagnostics {
    GroundPayloadKind payload_kind = GroundPayloadKind::TriangleCsvV1;
    GroundBoundsEnu bounds_enu{};
    std::size_t triangle_count = 0;
    std::size_t sample_count = 0;
    std::size_t cell_count = 0;
    std::size_t drivable_cell_count = 0;
    std::size_t spatial_cell_count = 0;
    std::size_t spatial_global_triangle_count = 0;
    std::size_t maximum_query_candidate_count = 0;
    std::uint32_t lattice_columns = 0;
    std::uint32_t lattice_rows = 0;
};

[[nodiscard]] const char* ground_payload_kind_name(GroundPayloadKind kind);

// A completely verified, immutable collision snapshot. The manifest, ground
// mesh, and static colliders are loaded before this value is published, and a
// final checksum pass rejects a package that changed during that work.
struct RuntimeMapPackage {
    std::string map_id;
    std::string collision_checksum;
    std::shared_ptr<const GroundQuery> ground_query;
    std::shared_ptr<const CollisionWorld> collision_world;
    GroundSnapshotDiagnostics ground_diagnostics{};
};

[[nodiscard]] RuntimeMapPackage load_runtime_map_package(
    const std::filesystem::path& package_directory);

// Polls manifest.cfg on a worker thread. Validation is completed on that
// worker; the callback only receives an immutable, self-consistent snapshot.
// A failed candidate is never acknowledged, so an unchanged invalid manifest
// is retried after its payload files finish committing.
class MapPackageHotReloader {
public:
    using ReadyCallback = std::function<void(RuntimeMapPackage)>;

    MapPackageHotReloader(
        std::filesystem::path package_directory,
        std::string active_collision_checksum,
        ReadyCallback on_ready,
        std::chrono::milliseconds poll_interval =
            std::chrono::milliseconds(250));
    ~MapPackageHotReloader();

    MapPackageHotReloader(const MapPackageHotReloader&) = delete;
    MapPackageHotReloader& operator=(const MapPackageHotReloader&) = delete;

    void start();
    void stop();
    [[nodiscard]] bool running() const { return running_.load(); }

    // Deterministic single scan for tests and diagnostics. Do not call while
    // the background worker is running.
    [[nodiscard]] bool poll_once();

private:
    [[nodiscard]] std::string read_manifest_bytes() const;
    void run();

    std::filesystem::path package_directory_;
    std::string active_collision_checksum_;
    ReadyCallback on_ready_;
    std::chrono::milliseconds poll_interval_;
    std::string accepted_manifest_bytes_;
    std::string rejected_manifest_bytes_;
    std::string last_failure_;
    std::chrono::steady_clock::time_point rejected_retry_after_{};
    std::chrono::milliseconds rejected_retry_delay_{0};
    bool has_accepted_manifest_bytes_ = false;
    bool has_rejected_manifest_bytes_ = false;

    std::atomic<bool> running_{false};
    std::mutex wait_mutex_;
    std::condition_variable wait_condition_;
    std::thread worker_;
};

} // namespace simcore_host
