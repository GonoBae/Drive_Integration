#pragma once

#include "physics/vehicle_physics.hpp"
#include "protocol/vehicle_messages.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace simcore_host {

class RuntimeVehicleCatalog;

// Deliberately an Ego-physics replay, not a second traffic AI authority. Each
// tick stores the exact external collision inputs supplied by the live host.
struct PhysicsReplayIdentity {
    std::string executable_checksum;
    std::string vehicle_checksum;
    std::string map_checksum;
    double origin_lat = 0.0;
    double origin_lon = 0.0;
    double origin_alt = 0.0;
    float spawn_heading = 0.f;
    double physics_hz = 60.0;
    bool measured_ground = false;
};

enum class GroundReplayOutcome { NoHit, Hit, Unavailable };

struct GroundReplaySample {
    GroundQueryRequest request;
    GroundReplayOutcome outcome = GroundReplayOutcome::Unavailable;
    GroundHit hit;
};

struct PhysicsReplayGroundFrame {
    static constexpr std::size_t kMaximumStaticColliders = 4096;
    bool attempted = false;
    bool initialize = false;
    bool accepted = false;
    bool collision_coverage_valid = true;
    std::uint64_t environment_epoch = 0;
    std::uint64_t world_revision = 0;
    std::vector<GroundReplaySample> samples;
    std::vector<StaticObbCollider> static_colliders;
};

// Records solver calls, not merely the received patch. This includes queries
// used by reset and unavailable results caught by optional tire-support checks.
class MeasuredGroundQueryRecorder final : public GroundQuery {
public:
    static constexpr std::size_t kMaximumQueriesPerFrame = 8192;
    explicit MeasuredGroundQueryRecorder(std::shared_ptr<const GroundQuery> source);
    void begin_frame() { samples_.clear(); }
    const std::vector<GroundReplaySample>& samples() const { return samples_; }
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override;
private:
    std::shared_ptr<const GroundQuery> source_;
    mutable std::vector<GroundReplaySample> samples_;
};

enum class PhysicsReplayEvent {
    Reset, Reconnect, SafeStop, HardTimeout, EmergencyStop
};

std::string physics_replay_file_checksum(const std::filesystem::path& path);

class PhysicsReplayRecorder {
public:
    static constexpr std::uint64_t kMaximumFrames = 216'000;
    static constexpr std::uint64_t kMaximumFileBytes = 1024ULL * 1024 * 1024;
    PhysicsReplayRecorder(const std::filesystem::path& path,
                          const PhysicsReplayIdentity& identity,
                          std::uint64_t frame_limit,
                          std::uint64_t byte_limit = kMaximumFileBytes);
    void event(PhysicsReplayEvent event,
               RuntimeVehicleClass vehicle_class = RuntimeVehicleClass::Unspecified,
               std::string_view loadout_id = {});
    void tick(const VehicleInput& input,
              const std::vector<KinematicCollisionProxy>& proxies,
              const VehicleState& state);
    void tick_measured(const VehicleInput& input,
              const std::vector<KinematicCollisionProxy>& proxies,
              const VehicleState& state, const PhysicsReplayGroundFrame& ground);
    void finish();
    void invalidate(std::string_view reason);
    bool complete() const { return complete_; }
    bool valid() const { return !invalid_; }
    std::uint64_t frames() const { return frames_; }
    bool measured_ground() const { return measured_ground_; }

private:
    void write_line(const std::string& line);
    void write_tick(const VehicleInput& input,
              const std::vector<KinematicCollisionProxy>& proxies,
              const VehicleState& state, const PhysicsReplayGroundFrame* ground);
    std::ofstream output_;
    std::uint64_t frame_limit_;
    std::uint64_t byte_limit_;
    std::uint64_t bytes_written_ = 0;
    std::uint64_t frames_ = 0;
    std::uint64_t events_ = 0;
    std::uint64_t hash_ = 14695981039346656037ULL;
    bool complete_ = false;
    bool invalid_ = false;
    bool measured_ground_ = false;
};

struct PhysicsReplayVerification {
    std::uint64_t frames = 0;
    std::uint64_t events = 0;
    std::uint64_t resets = 0;
    std::uint64_t dynamic_proxy_frames = 0;
    std::uint64_t measured_ground_frames = 0;
    std::uint64_t measured_ground_waits = 0;
    std::uint64_t measured_ground_queries = 0;
    double maximum_position_error_m = 0.0;
    double maximum_yaw_error_deg = 0.0;
};

// Streams bounded v1 (file ground) or v2 (measured ground) records; rejects truncated/invalid records,
// wrong executable/config/map, non-finite values and unrecognized events.
// RESET records additionally preserve the selected deterministic vehicle profile.
// v2 never consults the supplied ground or static collision world. It records
// the exact tick-local collision snapshot, not the NPC AI/world presentation.
PhysicsReplayVerification verify_physics_replay(
    const std::filesystem::path& path,
    const PhysicsReplayIdentity& expected_identity,
    const VehicleParameters& parameters,
    std::shared_ptr<const GroundQuery> ground,
    std::shared_ptr<const CollisionWorld> collision,
    const RuntimeVehicleCatalog* vehicle_catalog = nullptr);

} // namespace simcore_host
