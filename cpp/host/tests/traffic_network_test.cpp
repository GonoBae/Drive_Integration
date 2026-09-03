#include "traffic/traffic_network.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr const char* map_checksum = "fnv1a64:0123456789abcdef";
constexpr std::uint64_t second_ns = 1000000000ULL;
constexpr std::uint64_t millisecond_ns = 1000000ULL;

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

std::string lane(std::uint32_t id, const std::string& points,
                 const std::string& successors = "[]", std::uint32_t group = 0,
                 bool terminal = true)
{
    return "{\"id\":" + std::to_string(id)
        + ",\"width_m\":4,\"speed_limit_mps\":10,\"signal_group_id\":"
        + std::to_string(group) + ",\"terminal\":" + (terminal ? "true" : "false")
        + ",\"points\":" + points + ",\"successors\":" + successors + "}";
}

std::string signal(std::uint32_t id, std::uint32_t group, const std::string& position)
{
    return "{\"id\":" + std::to_string(id) + ",\"group_id\":"
        + std::to_string(group) + ",\"position_enu\":" + position
        + ",\"heading_deg\":90}";
}

std::string signal_v2(std::uint32_t id, std::uint32_t group,
                      std::uint32_t controller, const std::string& position,
                      const std::string& kind = {})
{
    return "{\"id\":" + std::to_string(id) + ",\"group_id\":"
        + std::to_string(group) + ",\"controller_id\":" + std::to_string(controller)
        + (kind.empty() ? "" : ",\"kind\":\"" + kind + "\"")
        + ",\"position_enu\":" + position + ",\"heading_deg\":90}";
}

std::string phase(std::uint32_t duration_ms, const std::string& green = "[]",
                  const std::string& yellow = "[]")
{
    return "{\"duration_ms\":" + std::to_string(duration_ms)
        + ",\"green_groups\":" + green + ",\"yellow_groups\":" + yellow + "}";
}

std::string plan(std::uint32_t id, std::uint32_t offset_ms,
                 const std::string& groups, const std::string& phases)
{
    return "{\"id\":" + std::to_string(id) + ",\"offset_ms\":"
        + std::to_string(offset_ms) + ",\"groups\":" + groups
        + ",\"phases\":" + phases + "}";
}

std::string network_json(const std::string& lanes, const std::string& signals = "[]")
{
    return std::string("{\"format_version\":1,\"source_map_checksum\":\"")
        + map_checksum + "\",\"lanes\":" + lanes + ",\"signals\":" + signals + "}";
}

std::string network_json_v2(const std::string& lanes, const std::string& signals,
                            const std::string& plans)
{
    return std::string("{\"format_version\":2,\"source_map_checksum\":\"")
        + map_checksum + "\",\"lanes\":" + lanes + ",\"signals\":" + signals
        + ",\"signal_plans\":" + plans + "}";
}

std::string valid_json()
{
    // Deliberately unsorted input must become deterministic ID order. Two
    // approaches meet an uncontrolled outgoing connector at the same junction.
    return network_json("["
        + lane(20, "[[10,0,0],[20,0,0]]") + ","
        + lane(30, "[[10,-10,0],[10,0,0]]", "[20]", 2, false) + ","
        + lane(10, "[[0,0,0],[10,0,0]]", "[20]", 1, false) + "]",
        "[" + signal(2, 2, "[11,-1,0]") + "," + signal(1, 1, "[10,1,0]") + "]");
}

std::string valid_v2_json()
{
    return network_json_v2("["
        + lane(20, "[[10,0,0],[20,0,0]]") + ","
        + lane(30, "[[10,-10,0],[10,0,0]]", "[20]", 200, false) + ","
        + lane(10, "[[0,0,0],[10,0,0]]", "[20]", 100, false) + "]",
        "[" + signal_v2(2, 200, 8, "[11,-1,0]") + ","
            + signal_v2(1, 100, 7, "[10,1,0]") + "]",
        "[" + plan(8, 500, "[200]", "["
                + phase(1000, "[200]") + "," + phase(500, "[]", "[200]") + ","
                + phase(1500) + "]") + ","
            + plan(7, 0, "[100]", "[" + phase(1000) + ","
                + phase(2000, "[100]") + "," + phase(500, "[]", "[100]") + ","
                + phase(500) + "]") + "]");
}

std::string replace_once(std::string text, const std::string& before,
                         const std::string& after)
{
    const auto found = text.find(before);
    require(found != std::string::npos, "test mutation must match its source");
    text.replace(found, before.size(), after);
    return text;
}

class TemporaryNetwork {
public:
    TemporaryNetwork()
    {
        static std::atomic<unsigned> serial{0};
        path = std::filesystem::temp_directory_path()
            / ("simcore-traffic-network-"
               + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
               + "-" + std::to_string(serial++) + ".json");
    }
    ~TemporaryNetwork()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    TemporaryNetwork(const TemporaryNetwork&) = delete;
    TemporaryNetwork& operator=(const TemporaryNetwork&) = delete;

    void write(const std::string& bytes) const
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        require(output.good(), "temporary traffic fixture must be writable");
    }

    simcore_host::TrafficNetwork load(const std::string& bytes,
                                      const simcore_host::GroundQuery& ground) const
    {
        write(bytes);
        return simcore_host::load_traffic_network(path, map_checksum, ground);
    }

    std::filesystem::path path;
};

void rejects(const std::string& bytes, const char* reason,
             const simcore_host::GroundQuery& ground = simcore_host::FlatGroundQuery{})
{
    TemporaryNetwork fixture;
    bool rejected = false;
    try {
        (void)fixture.load(bytes, ground);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, reason);
}

void test_valid_network_and_file_provenance()
{
    const simcore_host::FlatGroundQuery ground;
    TemporaryNetwork fixture;
    const auto loaded = fixture.load(valid_json(), ground);
    require(loaded.format_version == 1 && loaded.source_map_checksum == map_checksum,
            "validated network must retain version and collision provenance");
    require(loaded.lanes.size() == 3 && loaded.lanes[0].id == 10
                && loaded.lanes[1].id == 20 && loaded.lanes[2].id == 30,
            "lanes must be stored in canonical ID order");
    require(loaded.signals.size() == 2 && loaded.signals[0].id == 1
                && loaded.signals[1].id == 2,
            "signals must be stored in canonical ID order");
    require(loaded.lanes[0].successors == std::vector<std::uint32_t>{20}
                && loaded.lanes[0].signal_group_id == 1 && loaded.lanes[1].terminal,
            "directed successor and entry-control semantics must survive load");
    require(loaded.checksum.size() == 24 && loaded.checksum.starts_with("fnv1a64:"),
            "network must expose its separate exact-byte checksum");
    require(fixture.load(valid_json(), ground).checksum == loaded.checksum,
            "identical network bytes must have identical checksum");
    require(fixture.load(valid_json() + "\n", ground).checksum != loaded.checksum,
            "network checksum must cover bytes, not normalized JSON");

    const auto unsignalled = fixture.load(network_json("["
        + lane(1, "[[0,0,0],[10,0,0]]") + "]"), ground);
    require(unsignalled.signals.empty() && unsignalled.signals_at(0).empty(),
            "a terminal unsignalled lane is a valid bounded network");

    const auto data_driven = fixture.load(valid_v2_json(), ground);
    require(data_driven.format_version == 2 && data_driven.signal_plans.size() == 2
                && data_driven.signal_plans[0].id == 7
                && data_driven.signal_plans[1].id == 8,
            "version 2 plans must load and be stored in canonical controller order");
    require(data_driven.signals[0].controller_id == 7
                && data_driven.signals[1].controller_id == 8,
            "version 2 signal heads must retain their owning controller identity");
    require(data_driven.signal_plans[0].cycle_ms == 4000
                && data_driven.signal_plans[1].cycle_ms == 3000,
            "validated plans must retain exact bounded integer cycle lengths");
    require(simcore_host::describe_signal_timing(loaded) == "cycle_seconds=30",
            "version 1 startup timing diagnostic must remain exactly compatible");
    require(simcore_host::describe_signal_timing(data_driven)
                == "controllers=[id=7,cycle_seconds=4,offset_seconds=0;"
                   "id=8,cycle_seconds=3,offset_seconds=0.5]",
            "version 2 startup timing must identify every controller's exact cycle and offset");
}

void test_schema_primitives_and_unknown_duplicate_fields()
{
    const auto source = valid_json();
    rejects(replace_once(source, "\"format_version\":1", "\"format_version\":3"),
            "foreign schema version must fail closed");
    rejects(replace_once(source, "\"format_version\":1", "\"format_version\":\"1\""),
            "quoted integers must not be coerced by property_tree");
    rejects(replace_once(source, "\"width_m\":4", "\"width_m\":\"4\""),
            "quoted floats must not be coerced by property_tree");
    rejects(replace_once(source, "\"terminal\":true", "\"terminal\":\"true\""),
            "quoted booleans must not be coerced by property_tree");
    rejects(replace_once(source, "\"terminal\":true", "\"terminal\":1"),
            "numeric booleans must fail closed");
    rejects(replace_once(source, "\"successors\":[]", "\"successors\":{}"),
            "empty JSON object is not an empty successor array");
    rejects(replace_once(source, "\"width_m\":4", "\"width_m\":null"),
            "null is not a numeric width");
    rejects(replace_once(source, "\"id\":20", "\"id\":20.0"),
            "lane IDs must be integer JSON tokens");
    rejects(replace_once(source, "\"id\":20", "\"id\":4294967296"),
            "lane IDs must fit uint32");
    rejects(replace_once(source, "\"id\":20", "\"id\":-1"),
            "lane IDs must be nonnegative");
    rejects(replace_once(source, "\"width_m\":4", "\"widht_m\":4"),
            "unknown lane fields must fail closed");
    rejects(replace_once(source, "\"format_version\":1", "\"unknown\":0,\"format_version\":1"),
            "unknown root fields must fail closed");
    rejects(replace_once(source, "\"heading_deg\":90", "\"heading\":90"),
            "unknown signal fields must fail closed");
    rejects(replace_once(source, "\"format_version\":1", "\"format_version\":1,\"format_version\":1"),
            "duplicate root fields must fail closed");
    rejects(replace_once(source, "\"id\":20", "\"id\":20,\"id\":20"),
            "duplicate lane fields must fail closed");
    rejects(replace_once(source, "\"group_id\":2", "\"group_id\":2,\"group_id\":2"),
            "duplicate signal fields must fail closed");
    rejects(replace_once(source, "\"format_version\":1,", ""),
            "missing required root field must fail closed");
    rejects(replace_once(source, "\"width_m\":4,", ""),
            "missing required lane field must fail closed");
    rejects(replace_once(source, "\"heading_deg\":90", "\"heading_deg\":90,\"heading_\\u0064eg\":90"),
            "duplicate decoded JSON field names must fail closed");
}

void test_v2_plan_schema_relationships_and_bounds()
{
    const auto source = valid_v2_json();
    rejects(replace_once(source, "\"signal_plans\":", "\"plans\":"),
            "version 2 requires the exact signal_plans root field");
    rejects(network_json_v2("[" + lane(1, "[[0,0,0],[10,0,0]]") + "]", "[]", "{}"),
            "signal_plans must be an array rather than an object");
    rejects(replace_once(valid_json(), "\"format_version\":1",
                         "\"format_version\":1,\"signal_plans\":[]"),
            "version 1 must retain its exact root schema without signal_plans");
    rejects(replace_once(source, "\"controller_id\":7,", ""),
            "version 2 signal heads require controller_id");
    rejects(replace_once(source, "\"controller_id\":7", "\"controller_id\":8"),
            "a signal group must belong to the named controller plan");
    rejects(replace_once(source, "\"controller_id\":7", "\"controller_id\":65"),
            "controller identity must fit the bounded wire contract");
    rejects(replace_once(source, "\"signal_group_id\":100", "\"signal_group_id\":4097"),
            "version 2 lane groups must remain within 1..4096");
    rejects(replace_once(source, "\"group_id\":100", "\"group_id\":4097"),
            "version 2 head groups must remain within 1..4096");
    rejects(replace_once(source, "\"groups\":[100]", "\"groups\":[100,100]"),
            "groups within a plan must be unique");
    rejects(replace_once(source, "\"groups\":[200]", "\"groups\":[100,200]"),
            "groups must be globally unique across controllers");
    rejects(replace_once(source, "\"groups\":[100]", "\"groups\":[101]"),
            "every controlled lane/head group must belong to exactly one plan");
    rejects(replace_once(source, "\"duration_ms\":1000", "\"duration_ms\":99"),
            "a phase shorter than 100ms must fail closed");
    rejects(replace_once(source, "\"duration_ms\":1000", "\"duration_ms\":120001"),
            "a phase longer than 120000ms must fail closed");
    rejects(replace_once(source, "\"id\":8,\"offset_ms\":500",
                         "\"id\":8,\"offset_ms\":3000"),
            "offset must be strictly smaller than its plan cycle");
    rejects(replace_once(source,
                         "\"duration_ms\":2000,\"green_groups\":[100],\"yellow_groups\":[]",
                         "\"duration_ms\":2000,\"green_groups\":[100],\"yellow_groups\":[100]"),
            "green and yellow group sets must be disjoint");
    rejects(replace_once(source, "\"green_groups\":[100]", "\"green_groups\":[101]"),
            "phase groups must be a subset of the plan groups");
    rejects(replace_once(source, "\"green_groups\":[100]", "\"green_groups\":{\"x\":100}"),
            "phase group collections must preserve strict JSON array types");
    rejects(replace_once(source, "\"duration_ms\":1000", "\"duration_ms\":\"1000\""),
            "phase duration cannot be a quoted integer");
    rejects(replace_once(source, "\"duration_ms\":1000",
                         "\"unexpected\":0,\"duration_ms\":1000"),
            "unknown phase fields must fail closed");
    rejects(replace_once(source, "\"duration_ms\":1000",
                         "\"duration_ms\":1000,\"duration_ms\":1000"),
            "duplicate phase fields must fail closed");
    rejects(replace_once(source, "\"offset_ms\":500",
                         "\"unexpected\":0,\"offset_ms\":500"),
            "unknown plan fields must fail closed");
    rejects(replace_once(source, "\"offset_ms\":500",
                         "\"offset_ms\":500,\"offset_ms\":500"),
            "duplicate plan fields must fail closed");

    const std::string old_plan = plan(7, 0, "[100]", "[" + phase(1000) + ","
        + phase(2000, "[100]") + "," + phase(500, "[]", "[100]") + ","
        + phase(500) + "]");
    std::string long_phases = "[";
    for (unsigned index = 0; index < 31; ++index) {
        if (index != 0) { long_phases += ','; }
        long_phases += phase(120000, index == 0 ? "[100]" : "[]");
    }
    long_phases += ']';
    rejects(replace_once(source, old_plan, plan(7, 0, "[100]", long_phases)),
            "a data-driven cycle longer than one hour must fail closed");

    std::string too_many_phases = "[";
    for (unsigned index = 0; index < 65; ++index) {
        if (index != 0) { too_many_phases += ','; }
        too_many_phases += phase(100);
    }
    too_many_phases += ']';
    rejects(replace_once(source, old_plan, plan(7, 0, "[100]", too_many_phases)),
            "phase count must remain bounded independently of file size");
    rejects(replace_once(source, old_plan, plan(7, 0, "[]", "[" + phase(1000) + "]")),
            "a plan must own at least one group");
    rejects(replace_once(source, old_plan, plan(7, 0, "[100]", "[]")),
            "a plan must contain at least one phase");

    std::string too_many_groups = "[";
    for (unsigned group = 1; group <= 65; ++group) {
        if (group != 1) { too_many_groups += ','; }
        too_many_groups += std::to_string(group);
    }
    too_many_groups += ']';
    rejects(replace_once(source, old_plan,
                         plan(7, 0, too_many_groups, "[" + phase(1000) + "]")),
            "groups per controller must have an independent resource bound");

    std::string too_many_plans = "[";
    for (unsigned id = 1; id <= 33; ++id) {
        if (id != 1) { too_many_plans += ','; }
        too_many_plans += plan(id, 0, "[" + std::to_string(id) + "]",
                               "[" + phase(100) + "]");
    }
    too_many_plans += ']';
    rejects(network_json_v2("[" + lane(1, "[[0,0,0],[10,0,0]]") + "]", "[]",
                            too_many_plans),
            "signal controller count must remain bounded independently of file size");
}

void test_pedestrian_signal_pairs_and_runtime_kind()
{
    const auto vehicle = signal_v2(1, 100, 7, "[10,1,0]");
    const auto first = signal_v2(101, 100, 7, "[10,3,0]", "pedestrian");
    const auto second = signal_v2(102, 100, 7, "[10,-3,0]", "pedestrian");
    const auto source = replace_once(valid_v2_json(), vehicle,
                                     vehicle + "," + first + "," + second);
    TemporaryNetwork fixture;
    const auto network = fixture.load(source, simcore_host::FlatGroundQuery{});
    require(network.signals.size() == 4
                && network.signals[2].kind == simcore_host::TrafficSignalKind::Pedestrian
                && network.signals[3].kind == simcore_host::TrafficSignalKind::Pedestrian,
            "paired pedestrian heads must load with explicit runtime kind");
    const auto snapshots = network.signals_at(1500 * millisecond_ns);
    require(snapshots[2].kind == simcore_host::TrafficSignalKind::Pedestrian
                && snapshots[2].aspect == snapshots[0].aspect,
            "pedestrian WALK authority must share its safe parallel signal group phase");
    rejects(replace_once(source, "," + second, ""),
            "one unpaired pedestrian head must fail closed");
    rejects(replace_once(source, "\"kind\":\"pedestrian\"", "\"kind\":\"cyclist\""),
            "unknown signal kind must fail closed");
}

void test_malformed_json_checksums_and_finite_values()
{
    const auto source = valid_json();
    rejects("", "empty network must fail closed");
    rejects(source + "{}", "trailing JSON objects must fail closed");
    rejects(source + "garbage", "trailing non-JSON data must fail closed");
    rejects(source.substr(0, source.size() - 1), "truncated JSON must fail closed");
    rejects(replace_once(source, "\"width_m\":4", "\"width_m\":04"),
            "noncanonical leading-zero JSON numbers must fail closed");
    rejects(replace_once(source, "\"width_m\":4", "\"width_m\":NaN"),
            "non-JSON NaN must fail closed");
    rejects(replace_once(source, "\"width_m\":4", "\"width_m\":1e309"),
            "unrepresentable floating point values must fail closed");
    rejects(replace_once(source, "[10,0,0]", "[10,0,\"nan\"]"),
            "nonfinite string coordinates must fail closed");
    rejects(replace_once(source, "[10,0,0]", "[10,0]"),
            "short ENU points must fail closed");
    rejects(replace_once(source, "[10,0,0]", "[10,0,0,0]"),
            "long ENU points must fail closed");
    rejects(replace_once(source, map_checksum, "fnv1a64:1123456789abcdef"),
            "traffic from another collision package must fail closed");
    rejects(replace_once(source, "\"heading_deg\":90", "\"heading_deg\":1000"),
            "invalid signal headings must fail closed");
    rejects(network_json("[" + lane(1, "[[1000001,0,0],[1000010,0,0]]") + "]"),
            "coordinates beyond local-world limit must fail closed");
    TemporaryNetwork fixture;
    fixture.write(source);
    bool malformed_expected = false;
    try {
        (void)simcore_host::load_traffic_network(fixture.path, "not-a-checksum",
                                               simcore_host::FlatGroundQuery{});
    } catch (const std::runtime_error&) {
        malformed_expected = true;
    }
    require(malformed_expected, "malformed expected map identity must fail closed");
    TemporaryNetwork missing;
    bool absent = false;
    try {
        (void)simcore_host::load_traffic_network(missing.path, map_checksum,
                                               simcore_host::FlatGroundQuery{});
    } catch (const std::runtime_error&) {
        absent = true;
    }
    require(absent, "missing traffic network must fail closed");
}

void test_topology_lane_envelope_and_stopline_validation()
{
    const auto source = valid_json();
    rejects(replace_once(source, "\"id\":30", "\"id\":20"),
            "duplicate lane IDs must fail closed");
    rejects(replace_once(source, "\"id\":20", "\"id\":0"),
            "zero lane IDs must fail closed");
    rejects(replace_once(source, "\"successors\":[20]", "\"successors\":[999]"),
            "unknown successor references must fail closed");
    rejects(replace_once(source, "\"successors\":[20]", "\"successors\":[20,20]"),
            "duplicate successor references must fail closed");
    rejects(replace_once(source, "[[10,0,0],[20,0,0]]", "[[11,0,0],[20,0,0]]"),
            "successor endpoints more than 15cm apart must fail closed");
    rejects(replace_once(source, "\"terminal\":true", "\"terminal\":false"),
            "nonterminal lanes without a successor must fail closed");
    rejects(replace_once(source, "\"terminal\":false", "\"terminal\":true"),
            "terminal lanes with successors must fail closed");
    rejects(replace_once(source, "\"signal_group_id\":0", "\"signal_group_id\":1"),
            "controlled approach cannot enter another controlled connector");
    rejects(replace_once(source, "\"signal_group_id\":2", "\"signal_group_id\":3"),
            "unknown stopline groups must fail closed");
    rejects(replace_once(source, "\"group_id\":2", "\"group_id\":1"),
            "stopline group without a signal must fail closed");
    rejects(replace_once(source, "\"id\":2,\"group_id\":2", "\"id\":1,\"group_id\":2"),
            "duplicate signal IDs must fail closed");
    rejects(replace_once(source, "[11,-1,0]", "[100,-1,0]"),
            "signal remote from its matching stopline must fail closed");
    rejects(network_json("[" + lane(1, "[[0,0,0],[10,0,0]]") + "]",
                         "[" + signal(1, 1, "[10,0,0]") + "]"),
            "orphan signal must fail closed");
    for (const std::string width : {"0", "1.99", "8.01"}) {
        rejects(replace_once(source, "\"width_m\":4", "\"width_m\":" + width),
                "unsupported lane widths must fail closed");
    }
    for (const std::string speed : {"0", "-1", "55.61"}) {
        rejects(replace_once(source, "\"speed_limit_mps\":10", "\"speed_limit_mps\":" + speed),
                "unsupported speed limits must fail closed");
    }
    rejects(network_json("[" + lane(1, "[[0,0,0],[0,0,0]]") + "]"),
            "zero length segments must fail closed");
    rejects(network_json("[" + lane(1, "[[0,0,0],[0,0,1]]") + "]"),
            "vertical lane segments must fail closed");
    rejects(network_json("[" + lane(1, "[[0,0,0],[101,0,0]]") + "]"),
            "segments beyond 100m sampling envelope must fail closed");
}

class NarrowGround final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (std::abs(request.origin_enu.north_m) > 1.0) { return std::nullopt; }
        return simcore_host::FlatGroundQuery{}.query_down(request);
    }
};

class HoleGround final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        if (std::abs(request.origin_enu.east_m - 5.0) < 0.1) { return std::nullopt; }
        return simcore_host::FlatGroundQuery{}.query_down(request);
    }
};

class SlopeGround final : public simcore_host::GroundQuery {
public:
    std::optional<simcore_host::GroundHit> query_down(
        const simcore_host::GroundQueryRequest& request) const override
    {
        return simcore_host::FlatGroundQuery(request.origin_enu.east_m * 0.05).query_down(request);
    }
};

void test_authoritative_ground_centre_edges_and_segment_interiors()
{
    const std::string straight = network_json("[" + lane(1, "[[0,0,0],[10,0,0]]") + "]");
    rejects(straight, "ground height differing from authored lane by over 20cm must fail",
            simcore_host::FlatGroundQuery{0.201});
    rejects(straight, "covered centerline is insufficient when lane width leaves ground", NarrowGround{});
    rejects(straight, "covered endpoints are insufficient when segment crosses a ground hole", HoleGround{});
    TemporaryNetwork fixture;
    const auto slope = fixture.load(network_json("[" + lane(1, "[[0,0,0],[10,0,0.5]]") + "]"),
                                    SlopeGround{});
    require(slope.lanes.size() == 1, "finite lane points aligned to a real query slope must load");
}

void test_resource_limits()
{
    rejects(std::string(4 * 1024 * 1024 + 1, ' '), "overbudget file must fail before JSON parsing");
    rejects(network_json("[]"), "empty lane network must fail closed");
    std::string lanes = "[";
    for (unsigned i = 1; i <= 257; ++i) {
        if (i != 1) { lanes += ','; }
        lanes += lane(i, "[[0,0,0],[1,0,0]]");
    }
    rejects(network_json(lanes + "]"), "more than 256 lanes must fail closed");
    std::string signals = "[";
    for (unsigned i = 1; i <= 33; ++i) {
        if (i != 1) { signals += ','; }
        signals += signal(i, 1, "[0,0,0]");
    }
    rejects(network_json("[" + lane(1, "[[0,0,0],[1,0,0]]") + "]", signals + "]"),
            "more than 32 signals must fail closed");
    std::string points = "[";
    for (unsigned i = 0; i < 2049; ++i) {
        if (i != 0) { points += ','; }
        points += "[" + std::to_string(i) + ",0,0]";
    }
    rejects(network_json("[" + lane(1, points + "]") + "]"),
            "more than 2048 points in one lane must fail closed");
    points = "[";
    for (unsigned i = 0; i < 2048; ++i) {
        if (i != 0) { points += ','; }
        points += "[" + std::to_string(i) + ",0,0]";
    }
    lanes = "[";
    for (unsigned i = 1; i <= 9; ++i) {
        if (i != 1) { lanes += ','; }
        lanes += lane(i, points + "]");
    }
    rejects(network_json(lanes + "]"), "more than 16384 total lane points must fail closed");
    // The point budget alone cannot bound query work when segments are long.
    points = "[";
    for (unsigned i = 0; i <= 400; ++i) {
        if (i != 0) { points += ','; }
        points += "[" + std::to_string(i * 100) + ",0,0]";
    }
    rejects(network_json("[" + lane(1, points + "]") + "]"),
            "footprint validation must have an independent query-work bound");
    rejects(std::string(14, '[') + "0" + std::string(14, ']'),
            "deeply nested JSON must fail before recursive property_tree parsing");
}

void test_signal_phase_boundaries_reset_and_disabled_safety()
{
    TemporaryNetwork fixture;
    const auto network = fixture.load(valid_json(), simcore_host::FlatGroundQuery{});
    using Aspect = simcore_host::SignalAspect;
    struct Phase { std::uint64_t time; Aspect first; Aspect second; double first_remaining; double second_remaining; };
    const Phase phases[] = {
        {0, Aspect::Red, Aspect::Red, 2, 19},
        {2 * second_ns, Aspect::Green, Aspect::Red, 12, 17},
        {14 * second_ns, Aspect::Yellow, Aspect::Red, 3, 5},
        {17 * second_ns, Aspect::Red, Aspect::Red, 15, 2},
        {19 * second_ns, Aspect::Red, Aspect::Green, 13, 8},
        {27 * second_ns, Aspect::Red, Aspect::Yellow, 5, 3},
        {30 * second_ns, Aspect::Red, Aspect::Red, 2, 19},
    };
    for (const auto& phase : phases) {
        const auto snapshots = network.signals_at(phase.time);
        require(snapshots.size() == 2 && snapshots[0].aspect == phase.first
                    && snapshots[1].aspect == phase.second,
                "signal phase transition must take effect at its exact nanosecond boundary");
        require(snapshots[0].remaining_seconds == phase.first_remaining
                    && snapshots[1].remaining_seconds == phase.second_remaining,
                "signal countdown must reflect the next aspect change, including red across wrap");
        require(snapshots[0].id == 1 && snapshots[0].group_id == 1
                    && snapshots[0].position_enu.east_m == 10
                    && snapshots[0].heading_deg == 90,
                "signal snapshot must preserve authored identity and pose");
    }
    require(network.signals_at(2 * second_ns - 1)[0].aspect == Aspect::Red,
            "green cannot begin one nanosecond before its boundary");
    require(network.signals_at(14 * second_ns - 1)[0].aspect == Aspect::Green,
            "yellow cannot begin one nanosecond before its boundary");
    require(network.signals_at(17 * second_ns - 1)[0].aspect == Aspect::Yellow,
            "all-red cannot begin one nanosecond before its boundary");
    require(network.signals_at(19 * second_ns - 1)[1].aspect == Aspect::Red,
            "cross-traffic cannot begin before its clearance interval ends");
    require(network.signals_at(27 * second_ns - 1)[1].aspect == Aspect::Green,
            "cross-traffic yellow boundary must be exact");
    require(network.signals_at(30 * second_ns - 1)[1].aspect == Aspect::Yellow,
            "cycle wrapping must preserve the final yellow nanosecond");
    const auto huge = std::numeric_limits<std::uint64_t>::max();
    const auto large = network.signals_at(huge);
    const auto reduced = network.signals_at(huge % (30 * second_ns));
    for (std::size_t i = 0; i < large.size(); ++i) {
        require(large[i].aspect == reduced[i].aspect
                    && large[i].remaining_seconds == reduced[i].remaining_seconds,
                "huge timestamp must be reduced with integer arithmetic before conversion");
    }
    (void)network.signals_at(10 * second_ns);
    require(network.signals_at(0)[0].aspect == Aspect::Red,
            "a new simulation elapsed-time origin must reset to the all-red phase");
    for (std::uint64_t time = 0; time < 90 * second_ns; time += second_ns / 4) {
        const auto samples = network.signals_at(time);
        require(!(samples[0].aspect != Aspect::Red && samples[1].aspect != Aspect::Red),
                "conflicting groups must never have simultaneous green/yellow rights");
        const auto disabled = network.signals_at(time, false);
        for (const auto& sample : disabled) {
            require(sample.aspect == Aspect::Red && sample.remaining_seconds == 0,
                    "disabled or stale traffic must fail safe to indefinite all-red");
        }
    }
}

void test_v2_multi_controller_offsets_boundaries_and_countdowns()
{
    TemporaryNetwork fixture;
    const auto network = fixture.load(valid_v2_json(), simcore_host::FlatGroundQuery{});
    using Aspect = simcore_host::SignalAspect;
    struct Sample {
        std::uint64_t time_ns;
        Aspect first;
        Aspect second;
        double first_remaining;
        double second_remaining;
    };
    const Sample samples[] = {
        {0, Aspect::Red, Aspect::Green, 1.0, 0.5},
        {500 * millisecond_ns, Aspect::Red, Aspect::Yellow, 0.5, 0.5},
        {1000 * millisecond_ns, Aspect::Green, Aspect::Red, 2.0, 1.5},
        {2500 * millisecond_ns, Aspect::Green, Aspect::Green, 0.5, 1.0},
        {3000 * millisecond_ns, Aspect::Yellow, Aspect::Green, 0.5, 0.5},
        {3500 * millisecond_ns, Aspect::Red, Aspect::Yellow, 1.5, 0.5},
        {4000 * millisecond_ns, Aspect::Red, Aspect::Red, 1.0, 1.5},
    };
    for (const auto& expected : samples) {
        const auto state = network.signals_at(expected.time_ns);
        require(state.size() == 2 && state[0].aspect == expected.first
                    && state[1].aspect == expected.second,
                "version 2 controllers must apply independent data-driven phase offsets");
        require(state[0].remaining_seconds == expected.first_remaining
                    && state[1].remaining_seconds == expected.second_remaining,
                "version 2 countdown must coalesce same-aspect phases across cycle wrap");
        require(state[0].controller_id == 7 && state[1].controller_id == 8,
                "controller identity must survive into every runtime snapshot");
    }
    require(network.signals_at(1000 * millisecond_ns - 1)[0].aspect == Aspect::Red
                && network.signals_at(1000 * millisecond_ns)[0].aspect == Aspect::Green,
            "version 2 phase changes must occur on the exact integer-nanosecond boundary");
    require(network.signals_at(500 * millisecond_ns - 1)[1].aspect == Aspect::Green
                && network.signals_at(500 * millisecond_ns)[1].aspect == Aspect::Yellow,
            "a controller offset must preserve its exact shifted phase boundary");
    require(network.signals_at(2500 * millisecond_ns)[0].aspect == Aspect::Green
                && network.signals_at(2500 * millisecond_ns)[1].aspect == Aspect::Green,
            "independent controllers may grant non-conflicting groups simultaneously");
    const auto huge = std::numeric_limits<std::uint64_t>::max();
    const auto full = network.signals_at(huge);
    const auto reduced = network.signals_at(huge % (12 * second_ns));
    for (std::size_t index = 0; index < full.size(); ++index) {
        require(full[index].aspect == reduced[index].aspect
                    && full[index].remaining_seconds == reduced[index].remaining_seconds,
                "large timestamps must be reduced before offset arithmetic without overflow");
    }
    for (const auto& state : network.signals_at(2500 * millisecond_ns, false)) {
        require(state.aspect == Aspect::Red && state.remaining_seconds == 0,
                "disabled version 2 controllers must publish indefinite all-red");
    }

    const std::string old_plan = plan(7, 0, "[100]", "[" + phase(1000) + ","
        + phase(2000, "[100]") + "," + phase(500, "[]", "[100]") + ","
        + phase(500) + "]");
    const auto constant = fixture.load(
        replace_once(valid_v2_json(), old_plan,
                     plan(7, 0, "[100]", "[" + phase(1000, "[100]") + "]")),
        simcore_host::FlatGroundQuery{});
    require(constant.signals_at(123456789)[0].aspect == Aspect::Green
                && constant.signals_at(123456789)[0].remaining_seconds == 0,
            "a group whose aspect never changes must report an indefinite zero countdown");
}

} // namespace

int main()
{
    try {
        test_valid_network_and_file_provenance();
        test_schema_primitives_and_unknown_duplicate_fields();
        test_v2_plan_schema_relationships_and_bounds();
        test_pedestrian_signal_pairs_and_runtime_kind();
        test_malformed_json_checksums_and_finite_values();
        test_topology_lane_envelope_and_stopline_validation();
        test_authoritative_ground_centre_edges_and_segment_interiors();
        test_resource_limits();
        test_signal_phase_boundaries_reset_and_disabled_safety();
        test_v2_multi_controller_offsets_boundaries_and_countdowns();
        std::cout << "traffic_network_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "traffic_network_test: " << error.what() << '\n';
        return 1;
    }
}
