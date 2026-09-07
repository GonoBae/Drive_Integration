#pragma once

#include "physics/vehicle_physics.hpp"
#include "protocol/vehicle_messages.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace simcore_host {

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
               RuntimeVehicleClass vehicle_class = RuntimeVehicleClass::Unspecified);
    void tick(const VehicleInput& input,
              const std::vector<KinematicCollisionProxy>& proxies,
              const VehicleState& state);
    void finish();
    void invalidate(std::string_view reason);
    bool complete() const { return complete_; }
    bool valid() const { return !invalid_; }
    std::uint64_t frames() const { return frames_; }

private:
    void write_line(const std::string& line);
    std::ofstream output_;
    std::uint64_t frame_limit_;
    std::uint64_t byte_limit_;
    std::uint64_t bytes_written_ = 0;
    std::uint64_t frames_ = 0;
    std::uint64_t events_ = 0;
    std::uint64_t hash_ = 14695981039346656037ULL;
    bool complete_ = false;
    bool invalid_ = false;
};

struct PhysicsReplayVerification {
    std::uint64_t frames = 0;
    std::uint64_t events = 0;
    std::uint64_t resets = 0;
    std::uint64_t dynamic_proxy_frames = 0;
    double maximum_position_error_m = 0.0;
    double maximum_yaw_error_deg = 0.0;
};

// Streams a bounded, strict v1 recording; rejects truncated/invalid records,
// wrong executable/config/map, non-finite values and unrecognized events.
// RESET records additionally preserve the selected deterministic vehicle profile.
PhysicsReplayVerification verify_physics_replay(
    const std::filesystem::path& path,
    const PhysicsReplayIdentity& expected_identity,
    const VehicleParameters& parameters,
    std::shared_ptr<const GroundQuery> ground,
    std::shared_ptr<const CollisionWorld> collision);

} // namespace simcore_host
