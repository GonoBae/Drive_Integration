#include "replay/physics_replay.hpp"

#include "player_vehicle_profile.hpp"
#include "terrain/live_ground_patch.hpp"
#include "vehicle_catalog/runtime_vehicle_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace simcore_host {
namespace {
constexpr std::size_t kMaximumLineBytes = 4096;
constexpr std::uint64_t kMaximumEvents = PhysicsReplayRecorder::kMaximumFrames * 16;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(std::string("physics replay: ") + message);
}

void hash_bytes(std::uint64_t& hash, std::string_view bytes)
{
    for (unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
}

std::string hash_text(std::uint64_t hash)
{
    std::ostringstream text;
    text << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

std::ostringstream output_line()
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    return stream;
}

std::istringstream input_line(const std::string& line)
{
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    return stream;
}

void end_line(std::istringstream& stream)
{
    require(!stream.fail(), "missing or malformed field");
    stream >> std::ws;
    require(stream.eof(), "unexpected trailing fields");
}

bool identifier(const std::string& value)
{
    return !value.empty() && value.size() <= 256
        && std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return c >= 33 && c <= 126 && c != '"' && c != '\\';
        });
}

void validate_identity(const PhysicsReplayIdentity& identity)
{
    require(identifier(identity.executable_checksum)
            && identifier(identity.vehicle_checksum) && identifier(identity.map_checksum),
            "invalid identity");
    require(std::isfinite(identity.origin_lat) && std::abs(identity.origin_lat) <= 90
            && std::isfinite(identity.origin_lon) && std::abs(identity.origin_lon) <= 180
            && std::isfinite(identity.origin_alt) && std::abs(identity.origin_alt) <= 100'000
            && std::isfinite(identity.spawn_heading) && identity.spawn_heading >= 0
            && identity.spawn_heading < 360 && std::isfinite(identity.physics_hz)
            && identity.physics_hz >= 1 && identity.physics_hz <= 1000,
            "invalid spawn or tick rate");
}

std::string identity_line(const PhysicsReplayIdentity& identity)
{
    auto output = output_line();
    output << "IDENTITY " << identity.executable_checksum << ' '
           << identity.vehicle_checksum << ' ' << identity.map_checksum << ' '
           << identity.origin_lat << ' ' << identity.origin_lon << ' '
           << identity.origin_alt << ' ' << identity.spawn_heading << ' '
           << identity.physics_hz;
    return output.str();
}

const char* event_name(PhysicsReplayEvent event)
{
    switch (event) {
    case PhysicsReplayEvent::Reset: return "RESET";
    case PhysicsReplayEvent::Reconnect: return "RECONNECT";
    case PhysicsReplayEvent::SafeStop: return "SAFE_STOP";
    case PhysicsReplayEvent::HardTimeout: return "HARD_TIMEOUT";
    case PhysicsReplayEvent::EmergencyStop: return "ESTOP";
    }
    throw std::runtime_error("physics replay: unknown event");
}

void validate_input(const VehicleInput& input)
{
    require(std::isfinite(input.throttle) && input.throttle >= 0 && input.throttle <= 1
            && std::isfinite(input.brake) && input.brake >= 0 && input.brake <= 1
            && std::isfinite(input.steering) && std::abs(input.steering) <= 1
            && static_cast<unsigned>(input.gear) <= 2, "invalid applied input");
}

void validate_ground_sample(const GroundReplaySample& sample)
{
    const auto& request = sample.request;
    const std::array values{request.origin_enu.east_m, request.origin_enu.north_m,
        request.origin_enu.up_m, request.max_distance_m};
    require(std::all_of(values.begin(), values.end(), [](double value) {
        return std::isfinite(value) && std::abs(value) <= 1e8;
    }) && request.max_distance_m > 0, "invalid recorded ground request");
    require(sample.outcome == GroundReplayOutcome::Hit
        || sample.outcome == GroundReplayOutcome::NoHit
        || sample.outcome == GroundReplayOutcome::Unavailable, "unknown ground outcome");
    if (sample.outcome != GroundReplayOutcome::Hit) return;
    const auto& hit = sample.hit;
    const std::array hit_values{hit.point_enu.east_m, hit.point_enu.north_m, hit.point_enu.up_m,
        hit.normal_enu.east_m, hit.normal_enu.north_m, hit.normal_enu.up_m,
        hit.distance_m, hit.friction_multiplier};
    const double norm = std::hypot(hit.normal_enu.east_m, hit.normal_enu.north_m, hit.normal_enu.up_m);
    require(std::all_of(hit_values.begin(), hit_values.end(), [](double value) {
        return std::isfinite(value) && std::abs(value) <= 1e8;
    }) && std::abs(norm - 1.0) <= .01 && hit.normal_enu.up_m > 0
        && hit.distance_m >= 0 && hit.distance_m <= request.max_distance_m + 1e-6
        && hit.friction_multiplier >= 0 && hit.friction_multiplier <= 100
        && std::abs(hit.point_enu.east_m - request.origin_enu.east_m) <= 1e-6
        && std::abs(hit.point_enu.north_m - request.origin_enu.north_m) <= 1e-6
        && std::abs(request.origin_enu.up_m - hit.point_enu.up_m - hit.distance_m) <= 1e-6,
        "invalid recorded ground hit");
}

void validate_static_collider(const StaticObbCollider& collider)
{
    const auto& shape = collider.shape;
    const std::array values{shape.center_enu.east_m, shape.center_enu.north_m, shape.center_up_m,
        shape.heading_rad, shape.half_length_m, shape.half_width_m, shape.half_height_m,
        collider.material.friction, collider.material.restitution};
    require(identifier(collider.collider_id)
        && (collider.semantic == StaticColliderSemantic::Wall
            || collider.semantic == StaticColliderSemantic::Curb
            || collider.semantic == StaticColliderSemantic::Barrier)
        && std::all_of(values.begin(), values.end(), [](double value) {
            return std::isfinite(value) && std::abs(value) <= 1e8;
        }) && shape.half_length_m > 0 && shape.half_length_m <= 1e6
        && shape.half_width_m > 0 && shape.half_width_m <= 1e6
        && shape.half_height_m > 0 && shape.half_height_m <= 1e6
        && collider.material.friction >= 0 && collider.material.restitution >= 0
        && collider.material.restitution <= 1, "invalid recorded static collider");
}

std::string static_collider_line(const StaticObbCollider& collider)
{
    const auto& shape = collider.shape;
    auto output = output_line();
    output << "STATIC " << collider.collider_id << ' ' << static_cast<unsigned>(collider.semantic)
        << ' ' << shape.center_enu.east_m << ' ' << shape.center_enu.north_m << ' '
        << shape.center_up_m << ' ' << shape.heading_rad << ' ' << shape.half_length_m
        << ' ' << shape.half_width_m << ' ' << shape.half_height_m << ' '
        << collider.material.friction << ' ' << collider.material.restitution;
    return output.str();
}

StaticObbCollider parse_static_collider(const std::string& line)
{
    auto input = input_line(line);
    StaticObbCollider collider;
    std::string kind;
    unsigned semantic = 3;
    auto& shape = collider.shape;
    input >> kind >> collider.collider_id >> semantic >> shape.center_enu.east_m >> shape.center_enu.north_m
        >> shape.center_up_m >> shape.heading_rad >> shape.half_length_m >> shape.half_width_m
        >> shape.half_height_m >> collider.material.friction >> collider.material.restitution;
    end_line(input);
    require(kind == "STATIC" && semantic <= 2, "invalid static collider record");
    collider.semantic = static_cast<StaticColliderSemantic>(semantic);
    validate_static_collider(collider);
    return collider;
}

void validate_ground_frame(const PhysicsReplayGroundFrame& frame)
{
    require(frame.samples.size() <= MeasuredGroundQueryRecorder::kMaximumQueriesPerFrame,
        "ground query count exceeds frame bound");
    require(frame.attempted ? !frame.samples.empty() && frame.environment_epoch != 0
        : !frame.accepted && frame.samples.empty(), "invalid measured-ground tick flags");
    require(frame.collision_coverage_valid || (frame.attempted && !frame.accepted),
        "invalid recorded collision coverage outcome");
    for (const auto& sample : frame.samples) validate_ground_sample(sample);
    require(frame.static_colliders.size() <= PhysicsReplayGroundFrame::kMaximumStaticColliders,
        "static collider count exceeds frame bound");
    std::unordered_set<std::string> ids;
    for (const auto& collider : frame.static_colliders) {
        validate_static_collider(collider);
        require(ids.insert(collider.collider_id).second, "duplicate recorded static collider ID");
    }
}

std::string ground_sample_line(const GroundReplaySample& sample)
{
    auto output = output_line();
    output << "QUERY " << sample.request.origin_enu.east_m << ' '
        << sample.request.origin_enu.north_m << ' ' << sample.request.origin_enu.up_m
        << ' ' << sample.request.max_distance_m << ' ';
    if (sample.outcome == GroundReplayOutcome::Hit) {
        const auto& hit = sample.hit;
        output << "HIT " << hit.point_enu.east_m << ' ' << hit.point_enu.north_m << ' '
            << hit.point_enu.up_m << ' ' << hit.normal_enu.east_m << ' '
            << hit.normal_enu.north_m << ' ' << hit.normal_enu.up_m << ' '
            << hit.distance_m << ' ' << static_cast<std::uint32_t>(hit.surface_material_id)
            << ' ' << hit.friction_multiplier;
    } else output << (sample.outcome == GroundReplayOutcome::NoHit ? "NO_HIT" : "UNAVAILABLE");
    return output.str();
}

GroundReplaySample parse_ground_sample(const std::string& line)
{
    auto input = input_line(line);
    std::string kind, outcome;
    GroundReplaySample sample;
    input >> kind >> sample.request.origin_enu.east_m >> sample.request.origin_enu.north_m
        >> sample.request.origin_enu.up_m >> sample.request.max_distance_m >> outcome;
    require(kind == "QUERY", "expected measured ground QUERY");
    if (outcome == "HIT") {
        sample.outcome = GroundReplayOutcome::Hit;
        std::uint64_t material = std::numeric_limits<std::uint64_t>::max();
        auto& hit = sample.hit;
        input >> hit.point_enu.east_m >> hit.point_enu.north_m >> hit.point_enu.up_m
            >> hit.normal_enu.east_m >> hit.normal_enu.north_m >> hit.normal_enu.up_m
            >> hit.distance_m >> material >> hit.friction_multiplier;
        require(material <= std::numeric_limits<std::uint32_t>::max(), "invalid ground material");
        hit.surface_material_id = static_cast<GroundSurfaceMaterialId>(material);
    } else {
        require(outcome == "NO_HIT" || outcome == "UNAVAILABLE", "unknown ground outcome");
        sample.outcome = outcome == "NO_HIT" ? GroundReplayOutcome::NoHit : GroundReplayOutcome::Unavailable;
    }
    end_line(input);
    validate_ground_sample(sample);
    return sample;
}

class ReplayGroundQuery final : public GroundQuery {
public:
    explicit ReplayGroundQuery(const std::vector<GroundReplaySample>& samples) : samples_(samples) {}
    std::optional<GroundHit> query_down(const GroundQueryRequest& request) const override
    {
        require(cursor_ < samples_.size(), "solver requested an unrecorded ground query");
        const auto& sample = samples_[cursor_++];
        const auto& expected = sample.request;
        // The same executable and max_digits10 round-trip preserve exact input
        // doubles. A mismatch is not missing terrain: it must fail verification.
        require(request.origin_enu.east_m == expected.origin_enu.east_m
            && request.origin_enu.north_m == expected.origin_enu.north_m
            && request.origin_enu.up_m == expected.origin_enu.up_m
            && request.max_distance_m == expected.max_distance_m,
            "solver ground query differs from recording");
        if (sample.outcome == GroundReplayOutcome::Unavailable) throw GroundMeasurementUnavailable{};
        if (sample.outcome == GroundReplayOutcome::NoHit) return std::nullopt;
        return sample.hit;
    }
    void require_consumed() const { require(cursor_ == samples_.size(), "unused recorded ground queries"); }
private:
    const std::vector<GroundReplaySample>& samples_;
    mutable std::size_t cursor_ = 0;
};

void validate_proxy(const KinematicCollisionProxy& proxy)
{
    require(identifier(proxy.proxy_id), "invalid proxy identity");
    const std::array values{proxy.linear_velocity_enu_mps.east_m,
        proxy.linear_velocity_enu_mps.north_m, proxy.heading_rate_rad_s,
        proxy.material.friction, proxy.material.restitution, proxy.mass_kg,
        proxy.yaw_inertia_kg_m2, proxy.maximum_linear_speed_mps, proxy.breakaway_impulse_n_s};
    require(std::all_of(values.begin(), values.end(), [](double v) {
        return std::isfinite(v) && std::abs(v) <= 100'000'000;
    }), "non-finite or out-of-range proxy");
    require(proxy.material.friction >= 0 && proxy.material.restitution >= 0
            && proxy.material.restitution <= 1 && proxy.mass_kg >= 0
            && proxy.mass_kg <= 100'000 && proxy.yaw_inertia_kg_m2 >= 0
            && proxy.maximum_linear_speed_mps >= 0, "invalid proxy material or mass");
    require(proxy.mass_kg == 0
        ? proxy.yaw_inertia_kg_m2 == 0 && proxy.maximum_linear_speed_mps == 0
        : proxy.mass_kg >= 1 && proxy.maximum_linear_speed_mps > 0,
        "invalid finite proxy parameters");
    require(proxy.breakaway_impulse_n_s >= 0 && proxy.breakaway_impulse_n_s <= 1e6
        && (!(proxy.breakaway_impulse_n_s > 0 || proxy.breakaway_released)
            || (proxy.mass_kg > 0 && std::holds_alternative<ObbPrism>(proxy.shape)))
        && (!proxy.breakaway_released || proxy.breakaway_impulse_n_s == 0)
        && (proxy.supported_vehicle_id.empty() || identifier(proxy.supported_vehicle_id))
        && proxy.supported_vehicle_id != proxy.proxy_id, "invalid contact policy");
    std::visit([&](const auto& shape) {
        const std::array position{shape.center_enu.east_m, shape.center_enu.north_m,
                                  shape.center_up_m, shape.half_height_m};
        require(std::all_of(position.begin(), position.end(), [](double v) {
            return std::isfinite(v) && std::abs(v) <= 100'000'000;
        }) && shape.half_height_m > 0, "invalid proxy shape");
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, ObbPrism>) {
            require(std::isfinite(shape.heading_rad) && std::abs(shape.heading_rad) <= 1e8
                && std::isfinite(shape.half_length_m) && shape.half_length_m > 0
                && shape.half_length_m <= 1e6 && std::isfinite(shape.half_width_m)
                && shape.half_width_m > 0 && shape.half_width_m <= 1e6
                && (proxy.mass_kg == 0 || proxy.yaw_inertia_kg_m2 >= 1),
                "invalid OBB");
        } else {
            require(std::isfinite(shape.radius_m) && shape.radius_m > 0
                && shape.radius_m <= shape.half_height_m && proxy.yaw_inertia_kg_m2 == 0,
                "invalid capsule");
        }
    }, proxy.shape);
}

std::string proxy_line(const KinematicCollisionProxy& proxy)
{
    validate_proxy(proxy);
    auto output = output_line();
    output << "PROXY " << proxy.proxy_id << ' ';
    std::visit([&](const auto& shape) {
        using Shape = std::decay_t<decltype(shape)>;
        output << (std::is_same_v<Shape, ObbPrism> ? "B " : "C ")
               << shape.center_enu.east_m << ' ' << shape.center_enu.north_m << ' '
               << shape.center_up_m << ' ' << shape.half_height_m << ' ';
        if constexpr (std::is_same_v<Shape, ObbPrism>) {
            output << shape.heading_rad << ' ' << shape.half_length_m << ' '
                   << shape.half_width_m << ' ';
        } else {
            output << shape.radius_m << ' ';
        }
    }, proxy.shape);
    output << proxy.linear_velocity_enu_mps.east_m << ' '
           << proxy.linear_velocity_enu_mps.north_m << ' ' << proxy.heading_rate_rad_s << ' '
           << proxy.material.friction << ' ' << proxy.material.restitution << ' '
           << proxy.mass_kg << ' ' << proxy.yaw_inertia_kg_m2 << ' '
           << proxy.maximum_linear_speed_mps
           << " CONTACT " << proxy.tire_support_candidate << ' ' << proxy.breakaway_impulse_n_s
           << ' ' << proxy.breakaway_released << ' ' << std::quoted(proxy.supported_vehicle_id);
    return output.str();
}

KinematicCollisionProxy parse_proxy(const std::string& line)
{
    auto input = input_line(line);
    std::string kind, shape_kind;
    KinematicCollisionProxy proxy;
    input >> kind >> proxy.proxy_id >> shape_kind;
    require(kind == "PROXY", "expected PROXY");
    if (shape_kind == "B") {
        ObbPrism shape;
        input >> shape.center_enu.east_m >> shape.center_enu.north_m >> shape.center_up_m
              >> shape.half_height_m >> shape.heading_rad >> shape.half_length_m >> shape.half_width_m;
        proxy.shape = shape;
    } else {
        require(shape_kind == "C", "unknown proxy shape");
        VerticalCapsule shape;
        input >> shape.center_enu.east_m >> shape.center_enu.north_m >> shape.center_up_m
              >> shape.half_height_m >> shape.radius_m;
        proxy.shape = shape;
    }
    input >> proxy.linear_velocity_enu_mps.east_m >> proxy.linear_velocity_enu_mps.north_m
          >> proxy.heading_rate_rad_s >> proxy.material.friction >> proxy.material.restitution
          >> proxy.mass_kg >> proxy.yaw_inertia_kg_m2 >> proxy.maximum_linear_speed_mps;
    // Older v1 recordings have no contact-policy extension and retain the
    // original collision defaults. New recordings preserve all solve inputs.
    require(!input.fail(), "missing proxy fields");
    if (!input.eof()) input >> std::ws;
    if (!input.eof()) {
        std::string extension;
        int supported = 0, released = 0;
        input >> extension >> supported >> proxy.breakaway_impulse_n_s >> released
              >> std::quoted(proxy.supported_vehicle_id);
        require(extension == "CONTACT" && (supported == 0 || supported == 1)
            && (released == 0 || released == 1), "invalid contact policy extension");
        proxy.tire_support_candidate = supported != 0;
        proxy.breakaway_released = released != 0;
    }
    end_line(input);
    validate_proxy(proxy);
    return proxy;
}

// Pose plus body velocities, controls, damage and per-wheel observable output.
// Timestamp is intentionally excluded: VehiclePhysics stamps wall-clock time.
std::vector<double> state_values(const VehicleState& state)
{
    std::vector<double> values{state.position_enu.x, state.position_enu.y,
        state.position_enu.z, state.heading, state.pitch, state.roll, state.speed,
        state.accel, state.fuel, state.rpm, state.yaw_rate, state.steering_angle,
        static_cast<double>(state.gear), state.damage_percent, state.last_impact_impulse_n_s,
        static_cast<double>(state.damage_zone), static_cast<double>(state.collision_event_sequence),
        state.linear_velocity_body.x, state.linear_velocity_body.y, state.linear_velocity_body.z,
        state.angular_velocity_body.x, state.angular_velocity_body.y, state.angular_velocity_body.z};
    for (const auto& wheel : state.wheels) {
        values.insert(values.end(), {static_cast<double>(wheel.wheel_index),
            wheel.in_contact ? 1.0 : 0.0, wheel.steering_angle, wheel.angular_speed,
            wheel.normal_load, wheel.longitudinal_slip, wheel.slip_angle,
            wheel.longitudinal_force, wheel.lateral_force, wheel.contact_point_enu.x,
            wheel.contact_point_enu.y, wheel.contact_point_enu.z,
            wheel.contact_normal_enu.x, wheel.contact_normal_enu.y, wheel.contact_normal_enu.z});
    }
    return values;
}

class Reader {
public:
    explicit Reader(const std::filesystem::path& path) : input_(path, std::ios::binary)
    {
        require(input_.is_open(), "cannot open input");
        require(std::filesystem::file_size(path) <= PhysicsReplayRecorder::kMaximumFileBytes,
                "file exceeds 1 GiB limit");
    }
    std::string line(bool hashed = true)
    {
        std::string value;
        char c;
        while (input_.get(c) && c != '\n') {
            require(value.size() < kMaximumLineBytes, "line exceeds bound");
            value.push_back(c);
        }
        require(input_ && !value.empty(), "truncated recording or empty line");
        if (hashed) add_hash(value);
        return value;
    }
    void add_hash(const std::string& value) { hash_bytes(hash_, value); hash_bytes(hash_, "\n"); }
    std::string checksum() const { return hash_text(hash_); }
    void require_end() { require(input_.peek() == std::char_traits<char>::eof(), "data after END"); }
private:
    std::ifstream input_;
    std::uint64_t hash_ = 14695981039346656037ULL;
};
} // namespace

MeasuredGroundQueryRecorder::MeasuredGroundQueryRecorder(std::shared_ptr<const GroundQuery> source)
    : source_(std::move(source))
{
    require(static_cast<bool>(source_), "measured ground recorder requires a provider");
}

std::optional<GroundHit> MeasuredGroundQueryRecorder::query_down(const GroundQueryRequest& request) const
{
    require(samples_.size() < kMaximumQueriesPerFrame, "ground query count exceeds frame bound");
    GroundReplaySample sample;
    sample.request = request;
    try {
        const auto hit = source_->query_down(request);
        sample.outcome = hit ? GroundReplayOutcome::Hit : GroundReplayOutcome::NoHit;
        if (hit) sample.hit = *hit;
        validate_ground_sample(sample);
        samples_.push_back(sample);
        return hit;
    } catch (const GroundMeasurementUnavailable&) {
        sample.outcome = GroundReplayOutcome::Unavailable;
        validate_ground_sample(sample);
        samples_.push_back(sample);
        throw;
    }
}

std::string physics_replay_file_checksum(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "cannot fingerprint executable");
    std::uint64_t hash = 14695981039346656037ULL;
    std::array<char, 65536> buffer;
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
        hash_bytes(hash, {buffer.data(), static_cast<std::size_t>(input.gcount())});
    }
    require(input.eof(), "executable fingerprint read failed");
    return hash_text(hash);
}

PhysicsReplayRecorder::PhysicsReplayRecorder(const std::filesystem::path& path,
    const PhysicsReplayIdentity& identity, std::uint64_t frame_limit, std::uint64_t byte_limit)
    : frame_limit_(frame_limit), byte_limit_(byte_limit), measured_ground_(identity.measured_ground)
{
    validate_identity(identity);
    require(frame_limit > 0 && frame_limit <= kMaximumFrames, "frame limit must be in [1,216000]");
    require(byte_limit >= 256 && byte_limit <= kMaximumFileBytes, "invalid recording byte limit");
    require(!std::filesystem::exists(path), "output already exists; refusing overwrite");
    output_.open(path, std::ios::binary | std::ios::out);
    require(output_.is_open(), "cannot create output (parent directory must exist)");
    write_line(measured_ground_ ? "SIMCORE_PHYSICS_REPLAY 2" : "SIMCORE_PHYSICS_REPLAY 1");
    write_line(identity_line(identity));
}

void PhysicsReplayRecorder::write_line(const std::string& line)
{
    require(line.size() <= kMaximumLineBytes, "output line exceeds bound");
    // Reserve room for END or INVALID so no completed file can exceed the
    // reader's own cap. The smaller injectable limit is for bounded tests.
    if (bytes_written_ + line.size() + 1 + 128 > byte_limit_) {
        invalidate("recording_byte_limit");
        throw std::runtime_error("physics replay: recording byte limit exceeded");
    }
    output_ << line << '\n';
    if (!output_) {
        invalid_ = true;
        output_.close();
        throw std::runtime_error("physics replay: recording write failed");
    }
    bytes_written_ += line.size() + 1;
    hash_bytes(hash_, line);
    hash_bytes(hash_, "\n");
}

void PhysicsReplayRecorder::event(
    PhysicsReplayEvent event, RuntimeVehicleClass vehicle_class, std::string_view loadout_id)
{
    if (complete_ || invalid_) return;
    require(events_ < kMaximumEvents, "event limit exceeded");
    std::string line = "EVENT " + std::to_string(frames_) + " " + event_name(event);
    if (event == PhysicsReplayEvent::Reset) {
        require(vehicle_class >= RuntimeVehicleClass::Sedan
                && vehicle_class <= RuntimeVehicleClass::Motorcycle,
                "RESET requires a supported vehicle class");
        line += " " + std::to_string(static_cast<unsigned>(vehicle_class));
        require(valid_vehicle_loadout_id(loadout_id), "invalid RESET loadout ID");
        if (!loadout_id.empty()) line += " LOADOUT " + std::string(loadout_id);
    } else {
        require(vehicle_class == RuntimeVehicleClass::Unspecified && loadout_id.empty(),
                "vehicle class is only valid for RESET");
    }
    write_line(line);
    ++events_;
}

void PhysicsReplayRecorder::tick(const VehicleInput& input,
    const std::vector<KinematicCollisionProxy>& proxies, const VehicleState& state)
{
    require(!measured_ground_, "measured-ground recorder requires tick_measured");
    write_tick(input, proxies, state, nullptr);
}

void PhysicsReplayRecorder::tick_measured(const VehicleInput& input,
    const std::vector<KinematicCollisionProxy>& proxies, const VehicleState& state,
    const PhysicsReplayGroundFrame& ground)
{
    require(measured_ground_, "file-ground recorder does not accept measured ticks");
    write_tick(input, proxies, state, &ground);
}

void PhysicsReplayRecorder::write_tick(const VehicleInput& input,
    const std::vector<KinematicCollisionProxy>& proxies, const VehicleState& state,
    const PhysicsReplayGroundFrame* ground)
{
    if (complete_ || invalid_) return;
    validate_input(input);
    require(proxies.size() <= 255, "proxy count exceeds 255");
    if (ground) validate_ground_frame(*ground);
    auto output = output_line();
    output << "TICK " << frames_ << ' ' << input.throttle << ' ' << input.brake << ' '
           << input.steering << ' ' << input.handbrake << ' '
           << static_cast<unsigned>(input.gear) << ' ' << proxies.size();
    write_line(output.str());
    for (const auto& proxy : proxies) write_line(proxy_line(proxy));
    if (ground) {
        output = output_line();
        output << "GROUND " << ground->attempted << ' ' << ground->initialize << ' '
            << ground->accepted << ' ' << ground->environment_epoch << ' '
            << ground->world_revision << ' ' << ground->samples.size() << ' ' << ground->static_colliders.size()
            << ' ' << ground->collision_coverage_valid;
        write_line(output.str());
        for (const auto& sample : ground->samples) write_line(ground_sample_line(sample));
        for (const auto& collider : ground->static_colliders) write_line(static_collider_line(collider));
    }
    output = output_line();
    output << "STATE";
    for (double value : state_values(state)) {
        require(std::isfinite(value), "non-finite physics state");
        output << ' ' << value;
    }
    write_line(output.str());
    ++frames_;
    if (frames_ == frame_limit_) finish();
}

void PhysicsReplayRecorder::finish()
{
    if (complete_ || invalid_) return;
    require(frames_ > 0, "empty recording");
    output_ << "END " << frames_ << ' ' << events_ << ' ' << hash_text(hash_) << '\n';
    output_.flush();
    require(static_cast<bool>(output_), "recording finalization failed");
    output_.close();
    complete_ = true;
}

void PhysicsReplayRecorder::invalidate(std::string_view reason)
{
    if (complete_ || invalid_) return;
    output_ << "INVALID " << reason << '\n';
    output_.flush();
    output_.close();
    invalid_ = true;
    std::cerr << "[Replay] recording invalidated: " << reason << "\n";
}

PhysicsReplayVerification verify_physics_replay(const std::filesystem::path& path,
    const PhysicsReplayIdentity& expected_identity, const VehicleParameters& parameters,
    std::shared_ptr<const GroundQuery> ground, std::shared_ptr<const CollisionWorld> collision,
    const RuntimeVehicleCatalog* vehicle_catalog)
{
    validate_identity(expected_identity);
    Reader reader(path);
    require(reader.line() == (expected_identity.measured_ground
        ? "SIMCORE_PHYSICS_REPLAY 2" : "SIMCORE_PHYSICS_REPLAY 1"),
        "unsupported format/version or ground mode mismatch");
    // Canonical max_digits10 formatting gives an exact, locale-independent
    // scalar/config/map/executable match, not a permissive approximate one.
    require(reader.line() == identity_line(expected_identity), "build/config/map/spawn/tick identity mismatch");
    const auto& catalog = vehicle_catalog ? *vehicle_catalog : default_runtime_vehicle_catalog();
    if (vehicle_catalog) {
        require(expected_identity.vehicle_checksum.ends_with("+" + catalog.checksum()),
                "resolved runtime catalog does not match replay identity");
    }
    VehiclePhysics physics(expected_identity.origin_lat, expected_identity.origin_lon,
        expected_identity.origin_alt, expected_identity.spawn_heading,
        catalog.player_parameters(parameters, RuntimeVehicleClass::Sedan),
        expected_identity.measured_ground ? std::make_shared<FlatGroundQuery>() : std::move(ground),
        expected_identity.measured_ground ? std::make_shared<CollisionWorld>() : std::move(collision));
    PhysicsReplayVerification result;
    while (true) {
        const auto line = reader.line(false);
        auto input = input_line(line);
        std::string kind;
        input >> kind;
        if (kind == "END") {
            std::uint64_t frames, events;
            std::string checksum;
            input >> frames >> events >> checksum;
            end_line(input);
            require(frames == result.frames && frames > 0 && events == result.events,
                    "footer count mismatch or empty replay");
            require(checksum == reader.checksum(), "recording checksum mismatch");
            reader.require_end();
            return result;
        }
        reader.add_hash(line);
        std::uint64_t frame = std::numeric_limits<std::uint64_t>::max();
        input >> frame;
        require(frame == result.frames, "non-contiguous tick/event index");
        if (kind == "EVENT") {
            require(result.events < kMaximumEvents, "event limit exceeded");
            std::string event;
            input >> event;
            require(event == "RESET" || event == "RECONNECT" || event == "SAFE_STOP"
                    || event == "HARD_TIMEOUT" || event == "ESTOP", "unknown lifecycle event");
            if (event == "RESET") {
                // A bare RESET is the original v1 spelling and therefore means
                // the configured Sedan baseline. New recordings always append
                // the authoritative class so every profile switch is replayable.
                unsigned encoded_class = static_cast<unsigned>(RuntimeVehicleClass::Sedan);
                if (!input.eof()) {
                    input >> std::ws;
                    if (!input.eof()) input >> encoded_class;
                }
                std::string loadout_id;
                if (!input.eof()) {
                    input >> std::ws;
                    if (!input.eof()) {
                        std::string marker;
                        input >> marker >> loadout_id;
                        require(marker == "LOADOUT" && !loadout_id.empty()
                            && valid_vehicle_loadout_id(loadout_id), "invalid RESET loadout ID");
                    }
                }
                end_line(input);
                require(encoded_class >= static_cast<unsigned>(RuntimeVehicleClass::Sedan)
                        && encoded_class <= static_cast<unsigned>(RuntimeVehicleClass::Motorcycle),
                        "invalid RESET vehicle class");
                // Indexed part selections are meaningful only within the exact
                // catalog snapshot, including callers using the default catalog.
                if (loadout_id.starts_with("parts_v1_"))
                    require(expected_identity.vehicle_checksum.ends_with("+" + catalog.checksum()),
                        "custom parts require matching recorded catalog identity");
                physics.replace_parameters(catalog.player_parameters(
                    parameters, static_cast<RuntimeVehicleClass>(encoded_class), loadout_id));
                ++result.resets;
            } else {
                end_line(input);
            }
            ++result.events;
            continue;
        }
        require(kind == "TICK" && result.frames < PhysicsReplayRecorder::kMaximumFrames,
                "unknown record, invalidated replay or too many frames");
        VehicleInput command;
        unsigned handbrake = 2, gear = 3;
        std::size_t proxy_count = 256;
        input >> command.throttle >> command.brake >> command.steering >> handbrake >> gear >> proxy_count;
        end_line(input);
        require(handbrake <= 1 && gear <= 2 && proxy_count <= 255, "invalid tick fields");
        command.handbrake = handbrake != 0;
        command.gear = static_cast<VehicleGear>(gear);
        validate_input(command);
        std::vector<KinematicCollisionProxy> proxies;
        std::unordered_set<std::string> ids;
        for (std::size_t index = 0; index < proxy_count; ++index) {
            auto proxy = parse_proxy(reader.line());
            require(ids.insert(proxy.proxy_id).second, "duplicate collision proxy ID");
            proxies.push_back(std::move(proxy));
        }
        if (!proxies.empty()) ++result.dynamic_proxy_frames;
        physics.set_input(command);
        if (expected_identity.measured_ground) {
            PhysicsReplayGroundFrame frame;
            auto ground_input = input_line(reader.line());
            unsigned attempted = 2, initialize = 2, accepted = 2;
            unsigned coverage_valid = 2;
            std::size_t query_count = MeasuredGroundQueryRecorder::kMaximumQueriesPerFrame + 1;
            std::size_t collider_count = PhysicsReplayGroundFrame::kMaximumStaticColliders + 1;
            ground_input >> kind >> attempted >> initialize >> accepted
                >> frame.environment_epoch >> frame.world_revision >> query_count >> collider_count >> coverage_valid;
            end_line(ground_input);
            require(kind == "GROUND" && attempted <= 1 && initialize <= 1 && accepted <= 1
                && query_count <= MeasuredGroundQueryRecorder::kMaximumQueriesPerFrame
                && collider_count <= PhysicsReplayGroundFrame::kMaximumStaticColliders && coverage_valid <= 1,
                "invalid measured-ground frame header");
            frame.attempted = attempted != 0;
            frame.initialize = initialize != 0;
            frame.accepted = accepted != 0;
            frame.collision_coverage_valid = coverage_valid != 0;
            for (std::size_t index = 0; index < query_count; ++index)
                frame.samples.push_back(parse_ground_sample(reader.line()));
            for (std::size_t index = 0; index < collider_count; ++index)
                frame.static_colliders.push_back(parse_static_collider(reader.line()));
            validate_ground_frame(frame);
            if (frame.attempted) {
                auto recorded_ground = std::make_shared<ReplayGroundQuery>(frame.samples);
                bool coverage_checked = false;
                const bool actual_accepted = physics.try_update_with_ground(
                    1.0 / expected_identity.physics_hz, std::move(proxies), recorded_ground, frame.initialize,
                    std::make_shared<CollisionWorld>(frame.static_colliders),
                    [&](const VehicleState&) {
                        coverage_checked = true;
                        if (!frame.collision_coverage_valid) throw GroundMeasurementUnavailable{};
                    });
                require(frame.collision_coverage_valid || coverage_checked,
                    "recorded collision rejection did not reach coverage validation");
                require(actual_accepted == frame.accepted, "measured-ground tick outcome mismatch");
                recorded_ground->require_consumed();
            } else physics.clear_tick_contacts();
            ++result.measured_ground_frames;
            if (!frame.accepted) ++result.measured_ground_waits;
            result.measured_ground_queries += query_count;
        } else physics.update(1.0 / expected_identity.physics_hz, std::move(proxies));
        const auto state = physics.get_state();
        const auto actual = state_values(state);
        auto expected_input = input_line(reader.line());
        expected_input >> kind;
        require(kind == "STATE", "missing state after tick");
        std::vector<double> expected(actual.size());
        for (double& value : expected) {
            expected_input >> value;
            require(!expected_input.fail() && std::isfinite(value), "invalid/non-finite recorded state");
        }
        end_line(expected_input);
        const double position_error = std::hypot(actual[0] - expected[0],
            actual[1] - expected[1], actual[2] - expected[2]);
        const double yaw_error = std::abs(std::remainder(actual[3] - expected[3], 360.0));
        result.maximum_position_error_m = std::max(result.maximum_position_error_m, position_error);
        result.maximum_yaw_error_deg = std::max(result.maximum_yaw_error_deg, yaw_error);
        if (!std::isfinite(position_error) || position_error > 0.01
            || !std::isfinite(yaw_error) || yaw_error > 0.1) {
            throw std::runtime_error("physics replay: pose divergence at frame "
                + std::to_string(result.frames) + " position_error_m="
                + std::to_string(position_error) + " yaw_error_deg=" + std::to_string(yaw_error));
        }
        // Same executable/input should reproduce all other observables too.
        // A tiny roundoff allowance is scaled, but never used for NFR-007 pose.
        for (std::size_t index = 4; index < actual.size(); ++index) {
            if (!std::isfinite(actual[index])
                || std::abs(actual[index] - expected[index]) > 1e-6 * std::max(1.0, std::abs(expected[index]))) {
                throw std::runtime_error("physics replay: body/wheel/damage divergence at frame "
                    + std::to_string(result.frames) + " observable_index=" + std::to_string(index));
            }
        }
        ++result.frames;
    }
}
} // namespace simcore_host
