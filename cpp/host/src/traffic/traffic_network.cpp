#include "traffic/traffic_network.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace simcore_host {
namespace {

using Tree = boost::property_tree::ptree;
constexpr std::size_t max_file_bytes = 4 * 1024 * 1024;
constexpr std::size_t max_lanes = 256;
constexpr std::size_t max_signals = 32;
constexpr std::size_t max_signal_plans = 32;
constexpr std::size_t max_groups_per_plan = 64;
constexpr std::size_t max_phases_per_plan = 64;
constexpr std::size_t max_total_phases = 512;
constexpr std::size_t max_total_points = 16384;
constexpr std::size_t max_ground_queries = 100000;
constexpr std::uint32_t max_signal_group_id = 4096;
constexpr std::uint32_t max_controller_id = 64;
constexpr std::uint32_t min_phase_duration_ms = 100;
constexpr std::uint32_t max_phase_duration_ms = 120000;
constexpr std::uint64_t max_plan_cycle_ms = 3600000;
constexpr std::uint64_t second_ns = 1000000000ULL;
constexpr std::uint64_t millisecond_ns = 1000000ULL;
constexpr std::uint64_t cycle_ns = 30 * second_ns;

[[noreturn]] void fail(const std::string& reason)
{
    throw std::runtime_error("Traffic network: " + reason);
}

enum class JsonKind { Object, Array, String, Number, Boolean, Null };

struct JsonShape {
    JsonKind kind = JsonKind::Null;
    std::vector<JsonShape> children;
};

// property_tree intentionally erases JSON types ("1" and 1, {} and [] become
// indistinguishable). Keep a bounded lexical shape alongside it so a strict
// external contract cannot silently coerce strings, booleans or containers.
class JsonShapeReader {
public:
    explicit JsonShapeReader(std::string_view bytes) : bytes_(bytes) {}

    JsonShape read()
    {
        auto result = value(0);
        whitespace();
        if (offset_ != bytes_.size()) { fail("trailing JSON data"); }
        return result;
    }

private:
    void whitespace()
    {
        while (offset_ < bytes_.size()
               && (bytes_[offset_] == ' ' || bytes_[offset_] == '\t'
                   || bytes_[offset_] == '\r' || bytes_[offset_] == '\n')) {
            ++offset_;
        }
    }

    bool take(char expected)
    {
        whitespace();
        if (offset_ < bytes_.size() && bytes_[offset_] == expected) {
            ++offset_;
            return true;
        }
        return false;
    }

    void expect(char expected)
    {
        if (!take(expected)) { fail("invalid JSON delimiter"); }
    }

    void string()
    {
        expect('"');
        while (offset_ < bytes_.size()) {
            const unsigned char c = static_cast<unsigned char>(bytes_[offset_++]);
            if (c == '"') { return; }
            if (c < 0x20) { fail("JSON string contains a control byte"); }
            if (c != '\\') { continue; }
            if (offset_ == bytes_.size()) { fail("truncated JSON escape"); }
            const char escape = bytes_[offset_++];
            if (escape == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if (offset_ == bytes_.size()) { fail("truncated Unicode escape"); }
                    const char digit = bytes_[offset_++];
                    if (!((digit >= '0' && digit <= '9')
                          || (digit >= 'a' && digit <= 'f')
                          || (digit >= 'A' && digit <= 'F'))) {
                        fail("invalid Unicode escape");
                    }
                }
            } else if (std::string_view("\"\\/bfnrt").find(escape)
                       == std::string_view::npos) {
                fail("invalid JSON escape");
            }
        }
        fail("unterminated JSON string");
    }

    bool digit() const
    {
        return offset_ < bytes_.size()
            && bytes_[offset_] >= '0' && bytes_[offset_] <= '9';
    }

    void number()
    {
        if (bytes_[offset_] == '-') { ++offset_; }
        if (!digit()) { fail("invalid JSON number"); }
        if (bytes_[offset_] == '0') {
            ++offset_;
        } else {
            while (digit()) { ++offset_; }
        }
        if (offset_ < bytes_.size() && bytes_[offset_] == '.') {
            ++offset_;
            if (!digit()) { fail("invalid JSON fractional number"); }
            while (digit()) { ++offset_; }
        }
        if (offset_ < bytes_.size()
            && (bytes_[offset_] == 'e' || bytes_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < bytes_.size()
                && (bytes_[offset_] == '+' || bytes_[offset_] == '-')) {
                ++offset_;
            }
            if (!digit()) { fail("invalid JSON exponent"); }
            while (digit()) { ++offset_; }
        }
    }

    JsonShape value(std::size_t depth)
    {
        if (depth > 12 || ++nodes_ > 100000) { fail("JSON nesting/node budget exceeded"); }
        whitespace();
        if (offset_ == bytes_.size()) { fail("missing JSON value"); }
        JsonShape result;
        const char first = bytes_[offset_];
        if (first == '{' || first == '[') {
            ++offset_;
            const bool object = first == '{';
            result.kind = object ? JsonKind::Object : JsonKind::Array;
            const char close = object ? '}' : ']';
            if (take(close)) { return result; }
            do {
                if (object) { string(); expect(':'); }
                result.children.push_back(value(depth + 1));
                if (take(close)) { return result; }
            } while (take(','));
            fail("invalid JSON container");
        }
        if (first == '"') {
            result.kind = JsonKind::String;
            string();
        } else if (first == '-' || digit()) {
            result.kind = JsonKind::Number;
            number();
        } else {
            bool matched = false;
            for (const auto literal : {std::string_view("true"),
                                       std::string_view("false"),
                                       std::string_view("null")}) {
                if (bytes_.substr(offset_, literal.size()) == literal) {
                    offset_ += literal.size();
                    result.kind = literal == "null" ? JsonKind::Null : JsonKind::Boolean;
                    matched = true;
                    break;
                }
            }
            if (!matched) { fail("invalid JSON literal"); }
        }
        return result;
    }

    std::string_view bytes_;
    std::size_t offset_ = 0;
    std::size_t nodes_ = 0;
};

using Kinds = std::map<const Tree*, JsonKind>;

void pair_shapes(const Tree& tree, const JsonShape& shape, Kinds& kinds)
{
    if (tree.size() != shape.children.size()) { fail("JSON shape mismatch"); }
    kinds.emplace(&tree, shape.kind);
    std::size_t i = 0;
    for (const auto& child : tree) { pair_shapes(child.second, shape.children[i++], kinds); }
}

void kind(const Tree& tree, JsonKind expected, const Kinds& kinds)
{
    if (kinds.at(&tree) != expected) { fail("JSON field has the wrong primitive/container type"); }
}

void object_fields(const Tree& tree, const Kinds& kinds,
                   std::initializer_list<std::string_view> expected)
{
    kind(tree, JsonKind::Object, kinds);
    std::set<std::string_view> seen;
    for (const auto& [name, value] : tree) {
        (void)value;
        if (std::find(expected.begin(), expected.end(), name) == expected.end()) {
            fail("unknown field: " + name);
        }
        if (!seen.insert(name).second) { fail("duplicate field: " + name); }
    }
    if (seen.size() != expected.size()) { fail("required field is missing"); }
}

std::uint32_t unsigned_number(const Tree& tree, const Kinds& kinds)
{
    kind(tree, JsonKind::Number, kinds);
    const auto& text = tree.data();
    std::uint32_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) {
        fail("unsigned integer field must be an exact uint32 JSON integer");
    }
    return result;
}

std::vector<std::uint32_t> group_array(const Tree& tree, const Kinds& kinds,
                                       std::size_t maximum, const char* field)
{
    kind(tree, JsonKind::Array, kinds);
    if (tree.size() > maximum) { fail(std::string(field) + " exceeds its resource bound"); }
    std::set<std::uint32_t> unique;
    std::vector<std::uint32_t> result;
    result.reserve(tree.size());
    for (const auto& item : tree) {
        const auto group = unsigned_number(item.second, kinds);
        if (group == 0 || group > max_signal_group_id || !unique.insert(group).second) {
            fail(std::string(field) + " contains a zero, duplicate or unsupported group");
        }
        result.push_back(group);
    }
    std::sort(result.begin(), result.end());
    return result;
}

double finite_number(const Tree& tree, const Kinds& kinds)
{
    kind(tree, JsonKind::Number, kinds);
    const auto& text = tree.data();
    double result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()
        || !std::isfinite(result)) {
        fail("number must be finite and representable");
    }
    return result;
}

GroundPointEnu point(const Tree& tree, const Kinds& kinds)
{
    kind(tree, JsonKind::Array, kinds);
    if (tree.size() != 3) { fail("ENU point must contain exactly three numbers"); }
    std::array<double, 3> values{};
    std::size_t i = 0;
    for (const auto& child : tree) {
        const double value = finite_number(child.second, kinds);
        if (std::abs(value) > 1000000.0) { fail("ENU point exceeds local-world coordinate limit"); }
        values[i++] = value;
    }
    return {values[0], values[1], values[2]};
}

bool checksum_valid(const std::string& value)
{
    return value.size() == 24 && value.starts_with("fnv1a64:")
        && std::all_of(value.begin() + 8, value.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
}

std::string file_checksum(std::string_view bytes)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) { hash = (hash ^ byte) * 1099511628211ULL; }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

double distance(const GroundPointEnu& a, const GroundPointEnu& b)
{
    return std::hypot(std::hypot(a.east_m - b.east_m, a.north_m - b.north_m),
                      a.up_m - b.up_m);
}

void validate_ground(const TrafficLane& lane, const GroundQuery& ground,
                     std::size_t& query_count)
{
    for (std::size_t i = 1; i < lane.points.size(); ++i) {
        const auto& a = lane.points[i - 1];
        const auto& b = lane.points[i];
        const double dx = b.east_m - a.east_m;
        const double dy = b.north_m - a.north_m;
        const double planar_length = std::hypot(dx, dy);
        const double length = distance(a, b);
        if (planar_length < 0.05 || length < 0.05 || length > 100.0) {
            fail("lane " + std::to_string(lane.id) + " segment must be 0.05..100m and nonvertical");
        }
        const std::size_t steps = static_cast<std::size_t>(std::ceil(length));
        const std::size_t requests = (steps + 1) * 3;
        if (requests > max_ground_queries - query_count) { fail("lane footprint query budget exceeded"); }
        query_count += requests;
        for (std::size_t step = 0; step <= steps; ++step) {
            const double t = static_cast<double>(step) / static_cast<double>(steps);
            for (double side : {-0.5, 0.0, 0.5}) {
                const GroundPointEnu sample{
                    a.east_m + dx * t - dy / planar_length * lane.width_m * side,
                    a.north_m + dy * t + dx / planar_length * lane.width_m * side,
                    a.up_m + (b.up_m - a.up_m) * t};
                const auto hit = ground.query_down({
                    {sample.east_m, sample.north_m, sample.up_m + 2.0}, 4.0});
                if (!hit || !std::isfinite(hit->point_enu.up_m)
                    || !std::isfinite(hit->point_enu.east_m)
                    || !std::isfinite(hit->point_enu.north_m)
                    || !std::isfinite(hit->normal_enu.east_m)
                    || !std::isfinite(hit->normal_enu.north_m)
                    || !std::isfinite(hit->normal_enu.up_m)
                    || !std::isfinite(hit->distance_m)
                    || hit->distance_m < 0.0 || hit->distance_m > 4.0
                    || hit->normal_enu.up_m < 0.5
                    || std::abs(hit->point_enu.east_m - sample.east_m) > 0.01
                    || std::abs(hit->point_enu.north_m - sample.north_m) > 0.01
                    || std::abs(hit->point_enu.up_m - sample.up_m) > 0.20) {
                    fail("lane " + std::to_string(lane.id)
                         + " center/width footprint disagrees with authoritative ground");
                }
            }
        }
    }
}

void validate_topology(const TrafficNetwork& network)
{
    std::map<std::uint32_t, const TrafficLane*> by_id;
    for (const auto& lane : network.lanes) { by_id.emplace(lane.id, &lane); }
    for (const auto& lane : network.lanes) {
        if (lane.terminal != lane.successors.empty()) {
            fail("terminal must be true exactly when successors is empty");
        }
        for (const auto successor : lane.successors) {
            const auto found = by_id.find(successor);
            if (found == by_id.end()) { fail("successor references an unknown lane ID"); }
            if (distance(lane.points.back(), found->second->points.front()) > 0.15) {
                fail("successor endpoints are disconnected");
            }
            if (lane.signal_group_id != 0 && found->second->signal_group_id != 0) {
                fail("a controlled stopline must enter an uncontrolled connector");
            }
        }
        if (lane.signal_group_id == 0) { continue; }
        if (lane.terminal) { fail("terminal lane cannot define an entry stopline"); }
        const bool signal_near_stopline = std::any_of(network.signals.begin(), network.signals.end(),
            [&](const TrafficSignal& signal) {
                return signal.kind == TrafficSignalKind::Vehicle
                    && signal.group_id == lane.signal_group_id
                    && std::hypot(signal.position_enu.east_m - lane.points.back().east_m,
                                  signal.position_enu.north_m - lane.points.back().north_m) <= 15.0;
            });
        if (!signal_near_stopline) { fail("controlled lane has no nearby signal in its stopline group"); }
    }
    for (const auto& signal : network.signals) {
        if (signal.kind != TrafficSignalKind::Vehicle) continue;
        const bool controlled_stopline = std::any_of(network.lanes.begin(), network.lanes.end(),
            [&](const TrafficLane& lane) {
                return lane.signal_group_id == signal.group_id
                    && std::hypot(signal.position_enu.east_m - lane.points.back().east_m,
                                  signal.position_enu.north_m - lane.points.back().north_m) <= 15.0;
            });
        if (!controlled_stopline) { fail("signal has no matching nearby controlled stopline"); }
    }
}

struct LaneSample {
    GroundPointEnu point;
    double east_tangent = 0.0;
    double north_tangent = 0.0;
};

double lane_length(const TrafficLane& lane)
{
    double length = 0.0;
    for (std::size_t i = 1; i < lane.points.size(); ++i) {
        length += distance(lane.points[i - 1], lane.points[i]);
    }
    return length;
}

LaneSample lane_sample(const TrafficLane& lane, double station)
{
    for (std::size_t i = 1; i < lane.points.size(); ++i) {
        const auto& a = lane.points[i - 1];
        const auto& b = lane.points[i];
        const double span = distance(a, b);
        if (station <= span || i + 1 == lane.points.size()) {
            const double t = std::clamp(station / span, 0.0, 1.0);
            const double planar = std::hypot(b.east_m - a.east_m, b.north_m - a.north_m);
            return {{a.east_m + (b.east_m - a.east_m) * t,
                     a.north_m + (b.north_m - a.north_m) * t,
                     a.up_m + (b.up_m - a.up_m) * t},
                    (b.east_m - a.east_m) / planar,
                    (b.north_m - a.north_m) / planar};
        }
        station -= span;
    }
    fail("cannot sample an empty lane");
}

void validate_lane_changes(const TrafficNetwork& network, const GroundQuery& ground,
                           std::size_t& query_count)
{
    std::map<std::uint32_t, const TrafficLane*> by_id;
    for (const auto& lane : network.lanes) { by_id.emplace(lane.id, &lane); }
    for (const auto& lane : network.lanes) {
        for (const auto& change : lane.lane_changes) {
            const auto found = by_id.find(change.target_lane_id);
            if (found == by_id.end() || change.target_lane_id == lane.id) {
                fail("lane change references an unknown or identical lane");
            }
            const auto& target = *found->second;
            const auto reciprocal = std::find_if(target.lane_changes.begin(), target.lane_changes.end(),
                [&](const auto& other) { return other.target_lane_id == lane.id; });
            if (reciprocal == target.lane_changes.end()
                || std::abs(change.source_begin_m - reciprocal->target_begin_m) > 1.e-4
                || std::abs(change.source_end_m - reciprocal->target_end_m) > 1.e-4
                || std::abs(change.target_begin_m - reciprocal->source_begin_m) > 1.e-4
                || std::abs(change.target_end_m - reciprocal->source_end_m) > 1.e-4) {
                fail("lane change must have a matching reciprocal neighbor/window");
            }
            const double source_span = change.source_end_m - change.source_begin_m;
            const double target_span = change.target_end_m - change.target_begin_m;
            if (change.source_begin_m < 5.0 || change.target_begin_m < 5.0
                || change.source_end_m > lane_length(lane) - 5.0
                || change.target_end_m > lane_length(target) - 5.0
                || source_span < 8.0 || target_span < 8.0
                || std::abs(source_span - target_span) > 0.5
                || lane.signal_group_id != target.signal_group_id) {
                fail("lane change window must be aligned, at least 8m and clear of junctions");
            }
            const auto steps = static_cast<std::size_t>(std::ceil(source_span));
            if (steps > max_ground_queries - query_count) {
                fail("lane change sampling budget exceeded");
            }
            TrafficLane corridor;
            corridor.id = lane.id;
            double side_sign = 0.0;
            for (std::size_t step = 0; step <= steps; ++step) {
                const double t = static_cast<double>(step) / static_cast<double>(steps);
                const auto a = lane_sample(lane, change.source_begin_m + source_span * t);
                const auto b = lane_sample(target, change.target_begin_m + target_span * t);
                const double dx = b.point.east_m - a.point.east_m;
                const double dy = b.point.north_m - a.point.north_m;
                const double across = a.east_tangent * dy - a.north_tangent * dx;
                const double separation = std::hypot(dx, dy);
                const double expected = (lane.width_m + target.width_m) * 0.5;
                if (a.east_tangent * b.east_tangent + a.north_tangent * b.north_tangent < 0.9848
                    || std::abs(dx * a.east_tangent + dy * a.north_tangent) > 0.5
                    || std::abs(a.point.up_m - b.point.up_m) > 0.1
                    || separation < expected - 0.10 || separation > expected + 0.75
                    || (side_sign != 0.0 && across * side_sign <= 0.0)) {
                    fail("lane change target must remain adjacent, parallel and same-direction");
                }
                side_sign = across;
                corridor.width_m = std::max(corridor.width_m,
                    separation + std::max(lane.width_m, target.width_m));
                corridor.points.push_back({(a.point.east_m + b.point.east_m) * 0.5,
                    (a.point.north_m + b.point.north_m) * 0.5,
                    (a.point.up_m + b.point.up_m) * 0.5});
            }
            // Validate the union's two edges and middle, not only the separately
            // supported lanes: a median, hole or raised divider is not drivable.
            validate_ground(corridor, ground, query_count);
        }
    }
}

void validate_signal_plans(const TrafficNetwork& network)
{
    if (network.format_version == 1) {
        if (!network.signal_plans.empty()) { fail("version 1 cannot contain signal plans"); }
        return;
    }
    std::map<std::uint32_t, std::uint32_t> group_controllers;
    for (const auto& plan : network.signal_plans) {
        for (const auto group : plan.groups) {
            if (!group_controllers.emplace(group, plan.id).second) {
                fail("signal groups must be globally unique across plans");
            }
        }
    }
    for (const auto& lane : network.lanes) {
        if (lane.signal_group_id != 0
            && !group_controllers.contains(lane.signal_group_id)) {
            fail("controlled lane group must belong to exactly one signal plan");
        }
    }
    for (const auto& signal : network.signals) {
        const auto owner = group_controllers.find(signal.group_id);
        if (owner == group_controllers.end() || owner->second != signal.controller_id) {
            fail("signal controller/group must identify one owning signal plan");
        }
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>,
             std::vector<const TrafficSignal*>> pedestrian_heads;
    for (const auto& signal : network.signals) {
        if (signal.kind == TrafficSignalKind::Pedestrian) {
            pedestrian_heads[{signal.controller_id, signal.group_id}].push_back(&signal);
        }
    }
    for (const auto& [identity, heads] : pedestrian_heads) {
        if (heads.size() != 2) {
            fail("each pedestrian crossing requires exactly two opposing heads");
        }
        const bool has_vehicle_head = std::any_of(network.signals.begin(), network.signals.end(),
            [&](const TrafficSignal& signal) {
                return signal.kind == TrafficSignalKind::Vehicle
                    && signal.controller_id == identity.first
                    && signal.group_id == identity.second;
            });
        const double crossing_length = distance(
            heads[0]->position_enu, heads[1]->position_enu);
        if (crossing_length < 2.0 || crossing_length > 40.0) {
            fail("pedestrian crossing must span 2..40m");
        }
        if (!has_vehicle_head) {
            const auto plan = std::find_if(network.signal_plans.begin(), network.signal_plans.end(),
                [&](const auto& value) { return value.id == identity.first; });
            if (plan == network.signal_plans.end()) fail("pedestrian crossing requires an owning signal plan");
            for (const auto& phase : plan->phases) {
                const auto active = [&](std::uint32_t group) {
                    return std::find(phase.green_groups.begin(), phase.green_groups.end(), group) != phase.green_groups.end()
                        || std::find(phase.yellow_groups.begin(), phase.yellow_groups.end(), group) != phase.yellow_groups.end();
                };
                if (!active(identity.second)) continue;
                for (const auto& signal : network.signals) {
                    if (signal.controller_id == identity.first && signal.kind == TrafficSignalKind::Vehicle
                        && active(signal.group_id)) fail("exclusive pedestrian WALK requires all vehicle approaches red");
                }
            }
        }
    }
}

} // namespace

TrafficNetwork load_traffic_network(const std::filesystem::path& path,
                                    const std::string& expected_map_checksum,
                                    const GroundQuery& ground)
{
    if (!checksum_valid(expected_map_checksum)) { fail("expected map checksum is malformed"); }
    std::ifstream input(path, std::ios::binary);
    if (!input) { fail("cannot open " + path.string()); }
    std::string bytes(max_file_bytes + 1, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    if (input.bad() || bytes.empty() || bytes.size() > max_file_bytes) {
        fail("file is unreadable, empty or over the 4MiB budget");
    }
    const JsonShape shape = JsonShapeReader(bytes).read();
    Tree tree;
    try {
        std::istringstream json(bytes);
        boost::property_tree::read_json(json, tree);
    } catch (const boost::property_tree::json_parser_error&) {
        fail("malformed JSON");
    }
    Kinds kinds;
    pair_shapes(tree, shape, kinds);
    TrafficNetwork result;
    const auto version = tree.get_child_optional("format_version");
    if (!version) { fail("required field is missing"); }
    result.format_version = unsigned_number(*version, kinds);
    if (result.format_version == 1) {
        object_fields(tree, kinds, {"format_version", "source_map_checksum", "lanes", "signals"});
    } else if (result.format_version == 2) {
        object_fields(tree, kinds,
                      {"format_version", "source_map_checksum", "lanes", "signals", "signal_plans"});
    } else {
        fail("unsupported format_version");
    }
    const auto& source = tree.get_child("source_map_checksum");
    kind(source, JsonKind::String, kinds);
    result.source_map_checksum = source.data();
    if (result.source_map_checksum != expected_map_checksum) { fail("source map checksum mismatch"); }
    result.checksum = file_checksum(bytes);

    const auto& lanes = tree.get_child("lanes");
    kind(lanes, JsonKind::Array, kinds);
    if (lanes.empty() || lanes.size() > max_lanes) { fail("lane count must be 1..256"); }
    std::set<std::uint32_t> lane_ids;
    std::size_t total_points = 0;
    for (const auto& child : lanes) {
        const auto& node = child.second;
        if (node.get_child_optional("lane_changes")) {
            object_fields(node, kinds, {"id", "width_m", "speed_limit_mps", "signal_group_id",
                                       "terminal", "points", "successors", "lane_changes"});
        } else {
            object_fields(node, kinds, {"id", "width_m", "speed_limit_mps", "signal_group_id",
                                       "terminal", "points", "successors"});
        }
        TrafficLane lane;
        lane.id = unsigned_number(node.get_child("id"), kinds);
        if (lane.id == 0 || !lane_ids.insert(lane.id).second) { fail("lane ID is zero or duplicate"); }
        lane.width_m = finite_number(node.get_child("width_m"), kinds);
        lane.speed_limit_mps = finite_number(node.get_child("speed_limit_mps"), kinds);
        if (lane.width_m < 2.0 || lane.width_m > 8.0
            || lane.speed_limit_mps <= 0.0 || lane.speed_limit_mps > 55.6) {
            fail("lane width/speed exceeds the supported road envelope");
        }
        lane.signal_group_id = unsigned_number(node.get_child("signal_group_id"), kinds);
        const auto group_limit = result.format_version == 1 ? 2U : max_signal_group_id;
        if (lane.signal_group_id > group_limit) { fail("lane signal group is unsupported"); }
        const auto& terminal = node.get_child("terminal");
        kind(terminal, JsonKind::Boolean, kinds);
        lane.terminal = terminal.data() == "true";
        const auto& points = node.get_child("points");
        kind(points, JsonKind::Array, kinds);
        if (points.size() < 2 || points.size() > 2048
            || points.size() > max_total_points - total_points) {
            fail("lane point count exceeds the per-lane/total budget");
        }
        total_points += points.size();
        for (const auto& item : points) { lane.points.push_back(point(item.second, kinds)); }
        const auto& successors = node.get_child("successors");
        kind(successors, JsonKind::Array, kinds);
        if (successors.size() > 8) { fail("lane has too many successors"); }
        std::set<std::uint32_t> successor_ids;
        for (const auto& item : successors) {
            const auto id = unsigned_number(item.second, kinds);
            if (id == 0 || !successor_ids.insert(id).second) { fail("successor ID is zero or duplicate"); }
            lane.successors.push_back(id);
        }
        std::sort(lane.successors.begin(), lane.successors.end());
        if (const auto changes = node.get_child_optional("lane_changes")) {
            kind(*changes, JsonKind::Array, kinds);
            if (changes->size() > 2) { fail("lane has more than two lane-change neighbors"); }
            std::set<std::uint32_t> targets;
            for (const auto& item : *changes) {
                const auto& change_node = item.second;
                object_fields(change_node, kinds, {"target_lane_id", "source_begin_m",
                    "source_end_m", "target_begin_m", "target_end_m"});
                TrafficLaneChange change;
                change.target_lane_id = unsigned_number(change_node.get_child("target_lane_id"), kinds);
                if (change.target_lane_id == 0 || !targets.insert(change.target_lane_id).second) {
                    fail("lane change target is zero or duplicate");
                }
                change.source_begin_m = finite_number(change_node.get_child("source_begin_m"), kinds);
                change.source_end_m = finite_number(change_node.get_child("source_end_m"), kinds);
                change.target_begin_m = finite_number(change_node.get_child("target_begin_m"), kinds);
                change.target_end_m = finite_number(change_node.get_child("target_end_m"), kinds);
                lane.lane_changes.push_back(change);
            }
            std::sort(lane.lane_changes.begin(), lane.lane_changes.end(),
                [](const auto& a, const auto& b) { return a.target_lane_id < b.target_lane_id; });
        }
        result.lanes.push_back(std::move(lane));
    }

    const auto& signals = tree.get_child("signals");
    kind(signals, JsonKind::Array, kinds);
    if (signals.size() > max_signals) { fail("signal count exceeds 32"); }
    std::set<std::uint32_t> signal_ids;
    for (const auto& child : signals) {
        const auto& node = child.second;
        if (result.format_version == 1) {
            object_fields(node, kinds, {"id", "group_id", "position_enu", "heading_deg"});
        } else {
            if (node.get_child_optional("kind")) {
                object_fields(node, kinds, {"id", "group_id", "controller_id", "kind",
                                            "position_enu", "heading_deg"});
            } else {
                // Early format-v2 packages predate the additive kind field and
                // remain vehicle-only. Unknown fields are still rejected.
                object_fields(node, kinds,
                              {"id", "group_id", "controller_id", "position_enu", "heading_deg"});
            }
        }
        TrafficSignal signal;
        signal.id = unsigned_number(node.get_child("id"), kinds);
        signal.group_id = unsigned_number(node.get_child("group_id"), kinds);
        signal.controller_id = result.format_version == 1
            ? 1U : unsigned_number(node.get_child("controller_id"), kinds);
        if (const auto signal_kind = node.get_child_optional("kind")) {
            kind(*signal_kind, JsonKind::String, kinds);
            if (signal_kind->data() == "vehicle") {
                signal.kind = TrafficSignalKind::Vehicle;
            } else if (signal_kind->data() == "pedestrian") {
                signal.kind = TrafficSignalKind::Pedestrian;
            } else {
                fail("signal kind must be vehicle or pedestrian");
            }
        }
        const auto group_limit = result.format_version == 1 ? 2U : max_signal_group_id;
        if (signal.id == 0 || !signal_ids.insert(signal.id).second
            || signal.group_id < 1 || signal.group_id > group_limit
            || signal.controller_id == 0 || signal.controller_id > max_controller_id) {
            fail("signal ID/group is zero, duplicate or unsupported");
        }
        signal.position_enu = point(node.get_child("position_enu"), kinds);
        signal.heading_deg = finite_number(node.get_child("heading_deg"), kinds);
        if (signal.heading_deg < -360.0 || signal.heading_deg > 360.0) { fail("signal heading exceeds one revolution"); }
        signal.heading_deg = std::fmod(signal.heading_deg + 360.0, 360.0);
        result.signals.push_back(signal);
    }

    if (result.format_version == 2) {
        const auto& plans = tree.get_child("signal_plans");
        kind(plans, JsonKind::Array, kinds);
        if (plans.empty() || plans.size() > max_signal_plans) {
            fail("signal plan count must be 1..32");
        }
        std::set<std::uint32_t> plan_ids;
        std::size_t total_phases = 0;
        for (const auto& child : plans) {
            const auto& node = child.second;
            object_fields(node, kinds, {"id", "offset_ms", "groups", "phases"});
            TrafficSignalPlan plan;
            plan.id = unsigned_number(node.get_child("id"), kinds);
            plan.offset_ms = unsigned_number(node.get_child("offset_ms"), kinds);
            if (plan.id == 0 || plan.id > max_controller_id
                || !plan_ids.insert(plan.id).second) {
                fail("signal plan ID is zero or duplicate");
            }
            plan.groups = group_array(node.get_child("groups"), kinds,
                                      max_groups_per_plan, "plan groups");
            if (plan.groups.empty()) { fail("signal plan groups cannot be empty"); }
            const auto& phases = node.get_child("phases");
            kind(phases, JsonKind::Array, kinds);
            if (phases.empty() || phases.size() > max_phases_per_plan
                || phases.size() > max_total_phases - total_phases) {
                fail("signal plan phase count exceeds its resource bound");
            }
            total_phases += phases.size();
            std::uint64_t cycle = 0;
            for (const auto& phase_child : phases) {
                const auto& phase_node = phase_child.second;
                object_fields(phase_node, kinds,
                              {"duration_ms", "green_groups", "yellow_groups"});
                TrafficSignalPhase phase;
                phase.duration_ms = unsigned_number(phase_node.get_child("duration_ms"), kinds);
                if (phase.duration_ms < min_phase_duration_ms
                    || phase.duration_ms > max_phase_duration_ms) {
                    fail("signal phase duration must be 100..120000ms");
                }
                phase.green_groups = group_array(phase_node.get_child("green_groups"), kinds,
                                                 max_groups_per_plan, "green_groups");
                phase.yellow_groups = group_array(phase_node.get_child("yellow_groups"), kinds,
                                                  max_groups_per_plan, "yellow_groups");
                for (const auto group : phase.green_groups) {
                    if (!std::binary_search(plan.groups.begin(), plan.groups.end(), group)) {
                        fail("green group does not belong to its signal plan");
                    }
                }
                for (const auto group : phase.yellow_groups) {
                    if (!std::binary_search(plan.groups.begin(), plan.groups.end(), group)) {
                        fail("yellow group does not belong to its signal plan");
                    }
                    if (std::binary_search(phase.green_groups.begin(), phase.green_groups.end(), group)) {
                        fail("a signal group cannot be green and yellow in one phase");
                    }
                }
                cycle += phase.duration_ms;
                if (cycle > max_plan_cycle_ms) { fail("signal plan cycle exceeds one hour"); }
                plan.phases.push_back(std::move(phase));
            }
            plan.cycle_ms = cycle;
            if (plan.offset_ms >= plan.cycle_ms) {
                fail("signal plan offset must be smaller than its cycle");
            }
            result.signal_plans.push_back(std::move(plan));
        }
    }
    std::sort(result.lanes.begin(), result.lanes.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(result.signals.begin(), result.signals.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(result.signal_plans.begin(), result.signal_plans.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    validate_signal_plans(result);
    validate_topology(result);
    std::size_t ground_queries = 0;
    for (const auto& lane : result.lanes) { validate_ground(lane, ground, ground_queries); }
    validate_lane_changes(result, ground, ground_queries);
    return result;
}

std::vector<TrafficSignalSnapshot> TrafficNetwork::signals_at(
    std::uint64_t elapsed_ns, bool enabled) const
{
    std::vector<TrafficSignalSnapshot> result;
    result.reserve(signals.size());
    for (const auto& signal : signals) {
        TrafficSignalSnapshot sample;
        sample.id = signal.id;
        sample.group_id = signal.group_id;
        sample.controller_id = signal.controller_id;
        sample.kind = signal.kind;
        sample.position_enu = signal.position_enu;
        sample.heading_deg = signal.heading_deg;
        sample.aspect = SignalAspect::Red;
        std::uint64_t remaining_ns = 0;
        if (enabled && format_version == 1) {
            // Reduce in integer nanoseconds before converting to double: uptime
            // near UINT64_MAX must not round a legacy boundary into another phase.
            const std::uint64_t phase = elapsed_ns % cycle_ns;
            if (signal.group_id == 1) {
                if (phase < 2 * second_ns) {
                    remaining_ns = 2 * second_ns - phase;
                } else if (phase < 14 * second_ns) {
                    sample.aspect = SignalAspect::Green;
                    remaining_ns = 14 * second_ns - phase;
                } else if (phase < 17 * second_ns) {
                    sample.aspect = SignalAspect::Yellow;
                    remaining_ns = 17 * second_ns - phase;
                } else {
                    remaining_ns = cycle_ns - phase + 2 * second_ns;
                }
            } else if (signal.group_id == 2) {
                if (phase < 19 * second_ns) {
                    remaining_ns = 19 * second_ns - phase;
                } else if (phase < 27 * second_ns) {
                    sample.aspect = SignalAspect::Green;
                    remaining_ns = 27 * second_ns - phase;
                } else {
                    sample.aspect = SignalAspect::Yellow;
                    remaining_ns = cycle_ns - phase;
                }
            }
        } else if (enabled && format_version == 2) {
            const auto plan = std::find_if(signal_plans.begin(), signal_plans.end(),
                [&](const TrafficSignalPlan& candidate) {
                    return candidate.id == signal.controller_id;
                });
            if (plan != signal_plans.end() && plan->cycle_ms != 0 && !plan->phases.empty()) {
                const std::uint64_t plan_cycle_ns = plan->cycle_ms * millisecond_ns;
                const std::uint64_t position_ns =
                    ((elapsed_ns % plan_cycle_ns)
                     + static_cast<std::uint64_t>(plan->offset_ms) * millisecond_ns)
                    % plan_cycle_ns;
                std::uint64_t phase_start_ns = 0;
                std::size_t phase_index = 0;
                for (; phase_index < plan->phases.size(); ++phase_index) {
                    const std::uint64_t end = phase_start_ns
                        + static_cast<std::uint64_t>(plan->phases[phase_index].duration_ms)
                            * millisecond_ns;
                    if (position_ns < end) { break; }
                    phase_start_ns = end;
                }
                const auto aspect_for = [&](std::size_t index) {
                    const auto& phase = plan->phases[index];
                    if (std::binary_search(phase.green_groups.begin(), phase.green_groups.end(),
                                           signal.group_id)) return SignalAspect::Green;
                    if (std::binary_search(phase.yellow_groups.begin(), phase.yellow_groups.end(),
                                           signal.group_id)) return SignalAspect::Yellow;
                    return SignalAspect::Red;
                };
                sample.aspect = aspect_for(phase_index);
                remaining_ns = static_cast<std::uint64_t>(plan->phases[phase_index].duration_ms)
                    * millisecond_ns - (position_ns - phase_start_ns);
                std::size_t next = (phase_index + 1) % plan->phases.size();
                while (next != phase_index && aspect_for(next) == sample.aspect) {
                    remaining_ns += static_cast<std::uint64_t>(plan->phases[next].duration_ms)
                        * millisecond_ns;
                    next = (next + 1) % plan->phases.size();
                }
                // A permanently unchanged group has no meaningful finite change
                // countdown, matching disabled all-red's zero convention.
                if (next == phase_index && aspect_for(next) == sample.aspect) {
                    remaining_ns = 0;
                }
            }
        }
        sample.remaining_seconds = static_cast<double>(remaining_ns) / static_cast<double>(second_ns);
        result.push_back(sample);
    }
    return result;
}

std::string describe_signal_timing(const TrafficNetwork& network)
{
    if (network.format_version == 1) {
        return "cycle_seconds=30";
    }

    const auto seconds = [](std::uint64_t milliseconds) {
        std::string result = std::to_string(milliseconds / 1000);
        const auto remainder = milliseconds % 1000;
        if (remainder == 0) {
            return result;
        }
        std::string fraction = std::to_string(1000 + remainder).substr(1);
        while (fraction.back() == '0') {
            fraction.pop_back();
        }
        return result + "." + fraction;
    };

    std::ostringstream output;
    output << "controllers=[";
    for (std::size_t index = 0; index < network.signal_plans.size(); ++index) {
        if (index != 0) {
            output << ';';
        }
        const auto& plan = network.signal_plans[index];
        output << "id=" << plan.id
               << ",cycle_seconds=" << seconds(plan.cycle_ms)
               << ",offset_seconds=" << seconds(plan.offset_ms);
    }
    output << ']';
    return output.str();
}

} // namespace simcore_host
