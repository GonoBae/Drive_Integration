"""Real-wire Signal City v2 acceptance smoke.

This test starts one dedicated Release host on an ephemeral loopback port. It
never connects to port 9000, never stops an existing server, and terminates only
the child process it created. Do not start Unreal or the normal server first.

Requirements:
  * a current cpp/host/build/Release/simcore_publisher.exe build;
  * Python protobuf generated from the current protocol/vehicle.proto;
  * the ``websockets`` Python package.

Run the complete WebSocket acceptance:

    python scripts/smoke_signal_city.py

Validate only the configuration/package/protobuf inputs (no process started):

    python scripts/smoke_signal_city.py --preflight-only
"""

import argparse
import asyncio
from contextlib import suppress
from dataclasses import dataclass
from datetime import datetime
import json
import math
import os
from pathlib import Path
import re
import socket
import subprocess
import time
import uuid

import websockets

from smoke_traffic import (SECOND_NS, ROOT, TrafficController, heartbeat,
                           network_expectations, pb, require,
                           reset_to_all_red, wait_state)


FIRST_NPC_ID = 1001
FIRST_PEDESTRIAN_ID = 2001
EXPECTED_NPC_COUNT = 10
EXPECTED_PEDESTRIAN_COUNT = 8
NPC_HALF_EXTENTS_M = (2.2, 1.0, 0.75)
PEDESTRIAN_RADIUS_M = 0.35
PEDESTRIAN_HALF_HEIGHT_M = 0.9
PEDESTRIAN_MAX_SPEED_MPS = 1.36


@dataclass(frozen=True)
class Crossing:
    key: tuple
    head_ids: tuple
    start: tuple
    end: tuple


@dataclass(frozen=True)
class SignalCitySpec:
    runtime_config: Path
    map_package: Path
    traffic_network: Path
    expected_traffic: object
    signal_kinds: dict
    crossings: dict
    routes: tuple
    route_segments: tuple
    npc_count: int
    npc_max_speed_mps: float
    command_timeout_ms: int
    hard_command_timeout_ms: int
    map_id: str
    npc_autonomous: bool = False
    authored_segments: tuple = ()
    lane_change_cells: tuple = ()
    authored_segment_index: object = None

    @property
    def npc_ids(self):
        return tuple(range(FIRST_NPC_ID, FIRST_NPC_ID + self.npc_count))

    @property
    def pedestrian_ids(self):
        return tuple(range(FIRST_PEDESTRIAN_ID,
                           FIRST_PEDESTRIAN_ID + EXPECTED_PEDESTRIAN_COUNT))

    @property
    def runtime_ids(self):
        return self.npc_ids + self.pedestrian_ids


def parse_key_value(path):
    """Parse the same comment-stripped, duplicate-rejecting cfg shape as C++."""
    require(path.is_file(), f"configuration file missing: {path}")
    result = {}
    for number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        require("=" in line, f"{path}:{number}: expected key=value")
        key, value = (part.strip() for part in line.split("=", 1))
        require(key and value, f"{path}:{number}: key/value must not be empty")
        require(key not in result, f"{path}:{number}: duplicate key {key!r}")
        result[key] = value
    return result


def strict_json(data):
    def object_fields(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"duplicate JSON field {key!r}")
            result[key] = value
        return result

    def invalid_constant(value):
        raise AssertionError("nonfinite JSON constant: " + value)

    return json.loads(data.decode("utf-8"), object_pairs_hook=object_fields,
                      parse_constant=invalid_constant)


def integer_value(values, key, minimum=0):
    text = values.get(key, "")
    require(re.fullmatch(r"[0-9]+", text) is not None,
            f"runtime {key} must be an unsigned integer")
    value = int(text)
    require(value >= minimum, f"runtime {key} must be >= {minimum}")
    return value


def float_value(values, key):
    try:
        value = float(values.get(key, ""))
    except ValueError as error:
        raise AssertionError(f"runtime {key} must be numeric") from error
    require(math.isfinite(value), f"runtime {key} must be finite")
    return value


def route_value(values, key):
    tokens = values.get(key, "").split(",")
    require(tokens and all(re.fullmatch(r"[0-9]+", token.strip()) for token in tokens),
            f"runtime {key} must be a comma-separated lane ID list")
    route = tuple(int(token.strip()) for token in tokens)
    require(all(identity > 0 for identity in route) and len(set(route)) == len(route),
            f"runtime {key} contains an invalid/duplicate lane ID")
    return route


def config_path(config_path, raw_value):
    value = Path(raw_value)
    if not value.is_absolute():
        value = config_path.parent / value
    return value.resolve()


def finite_point(value, context):
    require(type(value) is list and len(value) == 3
            and all(type(component) in (int, float) and math.isfinite(component)
                    for component in value), f"invalid {context} point")
    return tuple(float(component) for component in value)


def validate_npc_spawn_offsets(network, routes, count, start_offset, spacing):
    """Mirror the host's alternating-route/reset stop clearance before connecting."""
    lanes = {lane["id"]: lane for lane in network["lanes"]}
    route_spans = []
    for route in routes:
        spans = []
        station = 0.0
        for identity in route:
            lane = lanes[identity]
            length = sum(math.dist(a, b) for a, b in zip(lane["points"], lane["points"][1:]))
            spans.append((identity, station, station + length, lane.get("signal_group_id", 0)))
            station += length
        require(station > 0, "NPC spawn route must have positive length")
        route_spans.append(spans)
    result = []
    # NpcLaneFollowerConfig: 2.2m body-front extent plus 0.5m stopping margin.
    clearance = NPC_HALF_EXTENTS_M[0] + 0.5
    for index in range(count):
        spans = route_spans[index % len(routes)]
        offset = (start_offset + (index // len(routes)) * spacing) % spans[-1][2]
        identity, begin, end, group = next(span for span in spans if span[1] <= offset < span[2])
        require(not group or offset <= end - clearance + 1e-9,
                f"NPC {FIRST_NPC_ID + index} spawn enters controlled stopping margin: "
                f"lane={identity}, route_offset={offset:.3f}m, "
                f"remaining={end - offset:.3f}m, required={clearance:.3f}m")
        result.append((FIRST_NPC_ID + index, identity, offset - begin))
    return tuple(result)


def build_spec(runtime_config):
    runtime_config = runtime_config.resolve()
    values = parse_key_value(runtime_config)
    required = {
        "format_version", "vehicle_config", "map_package", "traffic_network",
        "npc_route", "npc_alternate_route", "npc_route_loop",
        "npc_start_offset_m", "npc_max_speed_mps", "npc_count", "npc_spacing_m",
        "ws_port", "physics_hz", "origin_lat_deg", "origin_lon_deg",
        "origin_alt_m", "spawn_heading_deg", "command_timeout_ms",
        "max_command_queue_age_ms", "hard_command_timeout_ms", "source_id",
        "demo_entities",
    }
    require(required.issubset(values),
            "runtime config missing acceptance keys: " + ", ".join(sorted(required - values.keys())))
    require(values["format_version"] == "1", "runtime config format_version must be 1")
    require(values["npc_route_loop"].lower() == "true", "Signal City NPC routes must loop")
    require(values["demo_entities"].lower() == "false",
            "legacy demo entities must be disabled for Signal City")
    autonomous_value = values.get("npc_autonomous", "false").lower()
    require(autonomous_value in ("true", "false"), "npc_autonomous must be true or false")
    npc_autonomous = autonomous_value == "true"

    map_package = config_path(runtime_config, values["map_package"])
    traffic_network = config_path(runtime_config, values["traffic_network"])
    vehicle_config = config_path(runtime_config, values["vehicle_config"])
    require(map_package.is_dir(), f"map package missing: {map_package}")
    require(traffic_network.is_file(), f"traffic network missing: {traffic_network}")
    require(vehicle_config.is_file(), f"vehicle config missing: {vehicle_config}")

    manifest_path = map_package / "manifest.cfg"
    manifest = parse_key_value(manifest_path)
    require(manifest.get("format_version") == "1", "map manifest format_version must be 1")
    require(manifest.get("coordinate_frame") == "map_enu",
            "Signal City package must use map_enu")
    map_id = manifest.get("map_id", "")
    require(map_id == "signal_city_v2", "runtime config did not select signal_city_v2")
    collision_checksum = manifest.get("collision_checksum", "")
    require(re.fullmatch(r"fnv1a64:[0-9a-f]{16}", collision_checksum) is not None,
            "invalid map collision checksum")
    collision_files = tuple(part.strip() for part in
                            manifest.get("collision_files", "").split(","))
    require(collision_files and all(name and (map_package / name).is_file()
                                    for name in collision_files),
            "one or more manifest collision files are missing")

    network_bytes = traffic_network.read_bytes()
    expected_traffic = network_expectations(network_bytes)
    network = strict_json(network_bytes)
    require(network.get("format_version") == 2, "Signal City traffic network must be v2")
    require(expected_traffic.map_checksum == collision_checksum,
            "traffic source checksum does not match the map package")

    kind_values = {
        "vehicle": pb.TRAFFIC_SIGNAL_KIND_VEHICLE,
        "pedestrian": pb.TRAFFIC_SIGNAL_KIND_PEDESTRIAN,
    }
    signal_kinds = {}
    crossing_heads = {}
    vehicle_heads = 0
    pedestrian_heads = 0
    for signal in network.get("signals", []):
        identity = signal.get("id")
        kind_name = signal.get("kind")
        require(kind_name in kind_values, f"signal {identity} has invalid/missing kind")
        signal_kinds[identity] = kind_values[kind_name]
        if kind_name == "vehicle":
            vehicle_heads += 1
            continue
        pedestrian_heads += 1
        key = (signal.get("controller_id"), signal.get("group_id"))
        crossing_heads.setdefault(key, []).append(
            (identity, finite_point(signal.get("position_enu"), f"signal {identity}")))
    require(vehicle_heads == 8 and pedestrian_heads == 8,
            f"Signal City requires 8 vehicle/8 pedestrian heads; got {vehicle_heads}/{pedestrian_heads}")
    require(len(signal_kinds) == len(expected_traffic.heads),
            "signal kind table does not cover every authored signal")

    crossings = {}
    for key, heads in crossing_heads.items():
        require(len(heads) == 2, f"pedestrian crossing {key} must have exactly two heads")
        heads.sort(key=lambda entry: entry[0])
        require(math.dist(heads[0][1][:2], heads[1][1][:2]) >= 2.0,
                f"pedestrian crossing {key} is too short")
        crossings[key] = Crossing(key, (heads[0][0], heads[1][0]),
                                  heads[0][1], heads[1][1])
    require(len(crossings) * 2 == EXPECTED_PEDESTRIAN_COUNT,
            f"Signal City requires four crossings/eight pedestrians; got {len(crossings)}/"
            f"{len(crossings) * 2}")
    require(set(crossings).issubset(expected_traffic.group_keys),
            "each pedestrian crossing must belong to its authored controller/group")
    for plan in network.get("signal_plans", []):
        vehicle_groups = {head["group_id"] for head in network["signals"]
                          if head["controller_id"] == plan["id"] and head["kind"] == "vehicle"}
        pedestrian_groups = {key[1] for key in crossings if key[0] == plan["id"]}
        for phase in plan["phases"]:
            active = set(phase["green_groups"]) | set(phase["yellow_groups"])
            require(not (active & vehicle_groups and active & pedestrian_groups),
                    "dedicated-lane WALK must not overlap vehicle-turn authority")

    lanes = {}
    for lane in network.get("lanes", []):
        identity = lane.get("id")
        require(type(identity) is int and identity > 0 and identity not in lanes,
                "lane IDs must be distinct positive integers")
        points = tuple(finite_point(point, f"lane {identity}")
                       for point in lane.get("points", []))
        require(len(points) >= 2, f"lane {identity} needs at least two points")
        lanes[identity] = points

    routes = (route_value(values, "npc_route"),
              route_value(values, "npc_alternate_route"))
    route_segments = []
    for route in routes:
        require(all(identity in lanes for identity in route),
                f"configured NPC route references a missing lane: {route}")
        segments = tuple((start, end) for identity in route
                         for start, end in zip(lanes[identity], lanes[identity][1:]))
        require(segments, f"configured NPC route has no segments: {route}")
        route_segments.append(segments)

    npc_count = integer_value(values, "npc_count", 1)
    require(npc_count == EXPECTED_NPC_COUNT,
            f"Signal City acceptance requires npc_count=10; got {npc_count}")
    npc_max_speed = float_value(values, "npc_max_speed_mps")
    require(0 < npc_max_speed <= 25, "npc_max_speed_mps must be in (0,25]")
    require(float_value(values, "npc_spacing_m") >= 8, "npc_spacing_m must be >= 8")
    require(float_value(values, "npc_start_offset_m") >= 0,
            "npc_start_offset_m must be non-negative")
    validate_npc_spawn_offsets(network, routes, npc_count,
                               float_value(values, "npc_start_offset_m"),
                               float_value(values, "npc_spacing_m"))
    soft_timeout = integer_value(values, "command_timeout_ms", 1)
    hard_timeout = integer_value(values, "hard_command_timeout_ms", 1)
    require(hard_timeout >= soft_timeout + 250,
            "hard command timeout leaves too little SafeStop recovery time")

    require(hasattr(pb.TrafficSignalState(), "signal_kind"),
            "Python protobuf is stale; regenerate vehicle_pb2.py")
    authored_segments = tuple((start, end) for points in lanes.values()
                              for start, end in zip(points, points[1:]))
    lane_change_cells = (build_lane_change_cells(network, lanes)
                         if npc_autonomous else ())
    return SignalCitySpec(runtime_config, map_package, traffic_network,
                          expected_traffic, signal_kinds, crossings, routes,
                          tuple(route_segments), npc_count, npc_max_speed,
                          soft_timeout, hard_timeout, map_id, npc_autonomous,
                          authored_segments, lane_change_cells,
                          build_segment_index(authored_segments) if npc_autonomous else None)


def position(entity):
    return (entity.position_enu.x, entity.position_enu.y, entity.position_enu.z)


def planar_distance(left, right):
    return math.hypot(left[0] - right[0], left[1] - right[1])


def distance_to_segment(point, start, end):
    dx, dy = end[0] - start[0], end[1] - start[1]
    denominator = dx * dx + dy * dy
    require(denominator > 0, "authored segment must have positive length")
    along = max(0.0, min(1.0,
        ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / denominator))
    return math.hypot(point[0] - start[0] - along * dx,
                      point[1] - start[1] - along * dy)


def distance_to_route(point, segments):
    return min(distance_to_segment(point, start, end) for start, end in segments)


PATH_TOLERANCE_M = 0.03
SEGMENT_INDEX_CELL_M = 8.0


def build_segment_index(segments):
    """Conservative AABB broadphase; the original exact distance is the gate."""
    index = {}
    for start, end in segments:
        x0 = math.floor((min(start[0], end[0]) - PATH_TOLERANCE_M) / SEGMENT_INDEX_CELL_M)
        x1 = math.floor((max(start[0], end[0]) + PATH_TOLERANCE_M) / SEGMENT_INDEX_CELL_M)
        y0 = math.floor((min(start[1], end[1]) - PATH_TOLERANCE_M) / SEGMENT_INDEX_CELL_M)
        y1 = math.floor((max(start[1], end[1]) + PATH_TOLERANCE_M) / SEGMENT_INDEX_CELL_M)
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                index.setdefault((x, y), []).append((start, end))
    return {key: tuple(value) for key, value in index.items()}


def point_near_route(point, segments, index=None):
    if index is not None:
        key = (math.floor(point[0] / SEGMENT_INDEX_CELL_M),
               math.floor(point[1] / SEGMENT_INDEX_CELL_M))
        segments = index.get(key, ())
    for start, end in segments:
        if (point[0] < min(start[0], end[0]) - PATH_TOLERANCE_M
                or point[0] > max(start[0], end[0]) + PATH_TOLERANCE_M
                or point[1] < min(start[1], end[1]) - PATH_TOLERANCE_M
                or point[1] > max(start[1], end[1]) + PATH_TOLERANCE_M):
            continue
        if distance_to_segment(point, start, end) < PATH_TOLERANCE_M:
            return True
    return False


def point_at_station(points, station):
    require(math.isfinite(station) and station >= 0, "invalid lane-change station")
    for start, end in zip(points, points[1:]):
        length = math.dist(start, end)
        require(length > 0, "lane-change lane contains a zero-length segment")
        if station <= length + 1e-8:
            fraction = min(1.0, station / length)
            return tuple(a + (b - a) * fraction for a, b in zip(start, end))
        station -= length
    raise AssertionError("lane-change station exceeds its authored lane")


def build_lane_change_cells(network, lanes):
    """Exact piecewise-linear station windows, not a blanket lane-width tolerance."""
    cells = []
    for lane in network.get("lanes", []):
        source = lanes[lane["id"]]
        for change in lane.get("lane_changes", []):
            target_id = change.get("target_lane_id")
            require(target_id in lanes, "lane-change target is absent")
            target = lanes[target_id]
            sb, se, tb, te = (change.get(key) for key in
                              ("source_begin_m", "source_end_m",
                               "target_begin_m", "target_end_m"))
            require(all(type(value) in (int, float) and math.isfinite(value)
                        for value in (sb, se, tb, te))
                    and 0 <= sb < se and 0 <= tb < te,
                    "invalid authored lane-change station window")
            # Split at EVERY source/target polyline vertex in the window. Each
            # resulting ruled strip is exactly a quad, even for curved lanes.
            cuts = {0.0, 1.0}
            for points, begin, end in ((source, sb, se), (target, tb, te)):
                station = 0.0
                for start, finish in zip(points, points[1:]):
                    station += math.dist(start, finish)
                    if begin < station < end:
                        cuts.add((station - begin) / (end - begin))
            cuts = sorted(cuts)
            for begin, end in zip(cuts, cuts[1:]):
                cells.append((point_at_station(source, sb + (se - sb) * begin),
                              point_at_station(source, sb + (se - sb) * end),
                              point_at_station(target, tb + (te - tb) * end),
                              point_at_station(target, tb + (te - tb) * begin)))
    return tuple(cells)


def inside_lane_change(point, cells):
    def cross(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def triangle(a, b, c):
        signs = (cross(a, b, point), cross(b, c, point), cross(c, a, point))
        return (min(signs) >= 0 or max(signs) <= 0) and abs(cross(a, b, c)) > 1e-12

    for a, b, c, d in cells:
        if (point[0] < min(a[0], b[0], c[0], d[0]) - PATH_TOLERANCE_M
                or point[0] > max(a[0], b[0], c[0], d[0]) + PATH_TOLERANCE_M
                or point[1] < min(a[1], b[1], c[1], d[1]) - PATH_TOLERANCE_M
                or point[1] > max(a[1], b[1], c[1], d[1]) + PATH_TOLERANCE_M):
            continue
        if triangle(a, b, c) or triangle(a, c, d):
            return True
        if any(planar_distance(start, end) > 1e-9
               and distance_to_segment(point, start, end) < 0.03
               for start, end in ((a, b), (b, c), (c, d), (d, a))):
            return True
    return False


def validate_npc_path(entity, spec):
    point = position(entity)
    if spec.npc_autonomous:
        on_lane = point_near_route(point, spec.authored_segments,
                                  getattr(spec, "authored_segment_index", None))
        changing_lane = not on_lane and inside_lane_change(point, spec.lane_change_cells)
        if not on_lane and not changing_lane:
            route_distance = distance_to_route(point, spec.authored_segments)
            raise AssertionError(
                f"NPC {entity.entity_id} left authored lanes/lane-change windows: "
                f"{route_distance:.6f} m")
    else:
        route_index = (entity.entity_id - FIRST_NPC_ID) % len(spec.routes)
        segments = spec.route_segments[route_index]
        if not point_near_route(point, segments):
            route_distance = distance_to_route(point, segments)
            raise AssertionError(
                f"NPC {entity.entity_id} left its configured route: {route_distance:.6f} m")
        changing_lane = False
    # Only measured lateral motion inside an authored transition may add up to
    # 10% to the longitudinal limit. Normal lanes retain the original bound.
    speed_limit = spec.npc_max_speed_mps * (1.1 if changing_lane else 1.0) + 0.05
    require(0 <= entity.speed <= speed_limit,
            f"NPC {entity.entity_id} exceeded configured speed: "
            f"{entity.speed:.6f} m/s > {speed_limit:.6f} m/s")


def runtime_entities(message, spec):
    entities = [entity for entity in message.world_state.entities
                if entity.entity_kind != pb.ENTITY_KIND_EGO_VEHICLE]
    require([entity.entity_id for entity in entities] == list(spec.runtime_ids),
            "runtime entity IDs/order changed; expected ten NPCs then eight pedestrians")
    for entity in entities:
        require(entity.HasField("position_enu") and entity.HasField("linear_velocity_enu"),
                f"entity {entity.entity_id} omitted ENU pose/velocity")
        values = (*position(entity), entity.heading, entity.speed,
                  entity.linear_velocity_enu.x, entity.linear_velocity_enu.y,
                  entity.linear_velocity_enu.z, entity.collision_half_length,
                  entity.collision_half_width, entity.collision_half_height,
                  entity.collision_radius)
        require(all(math.isfinite(value) for value in values),
                f"entity {entity.entity_id} contains a nonfinite value")
        measured_speed = math.hypot(entity.linear_velocity_enu.x,
                                    entity.linear_velocity_enu.y)
        require(abs(measured_speed - entity.speed) < 0.0002,
                f"entity {entity.entity_id} speed disagrees with ENU velocity")
        if entity.entity_id in spec.npc_ids:
            require(entity.entity_kind == pb.ENTITY_KIND_NPC_VEHICLE,
                    f"entity {entity.entity_id} is not an NPC vehicle")
            validate_npc_path(entity, spec)
            actual = (entity.collision_half_length, entity.collision_half_width,
                      entity.collision_half_height)
            require(all(abs(value - expected) < 1e-5
                        for value, expected in zip(actual, NPC_HALF_EXTENTS_M))
                    and abs(entity.collision_radius) < 1e-8,
                    f"NPC {entity.entity_id} collision shape changed")
        else:
            require(entity.entity_kind == pb.ENTITY_KIND_PEDESTRIAN,
                    f"entity {entity.entity_id} is not a pedestrian")
            require(0 <= entity.speed <= PEDESTRIAN_MAX_SPEED_MPS,
                    f"pedestrian {entity.entity_id} exceeded walking speed: "
                    f"{entity.speed:.6f} m/s > {PEDESTRIAN_MAX_SPEED_MPS:.6f} m/s")
            require(abs(entity.collision_radius - PEDESTRIAN_RADIUS_M) < 1e-5
                    and abs(entity.collision_half_height - PEDESTRIAN_HALF_HEIGHT_M) < 1e-5
                    and abs(entity.collision_half_length) < 1e-8
                    and abs(entity.collision_half_width) < 1e-8,
                    f"pedestrian {entity.entity_id} collision shape changed")
    return {entity.entity_id: entity for entity in entities}


def validate_signal_kinds(message, spec):
    actual = {head.signal_id: head.signal_kind
              for head in message.world_state.traffic_signals}
    require(actual == spec.signal_kinds,
            "wire signal_kind values do not match authored vehicle/pedestrian roles")


def crossing_aspects(message):
    aspects = {}
    for head in message.world_state.traffic_signals:
        if head.signal_kind != pb.TRAFFIC_SIGNAL_KIND_PEDESTRIAN:
            continue
        key = (head.controller_id, head.group_id)
        require(key not in aspects or aspects[key] == head.aspect,
                f"pedestrian heads in crossing {key} disagree")
        aspects[key] = head.aspect
    return aspects


def assign_pedestrian_crossings(entities, spec):
    assignments = {}
    counts = {key: 0 for key in spec.crossings}
    for identity in spec.pedestrian_ids:
        point = position(entities[identity])
        key = min(spec.crossings,
                  key=lambda candidate: distance_to_segment(
                      point, spec.crossings[candidate].start,
                      spec.crossings[candidate].end))
        distance = distance_to_segment(point, spec.crossings[key].start,
                                       spec.crossings[key].end)
        # Server paths run 0.60m beside the signal-head segment so the 0.35m
        # body capsule no longer starts inside the 0.14m pole. Allow 0.11m for
        # collision displacement/serialization, as the prior 0.45+0.11 bound did.
        require(distance <= 0.71,
                f"pedestrian {identity} did not spawn beside an authored crossing")
        assignments[identity] = key
        counts[key] += 1
    require(all(count == 2 for count in counts.values()),
            f"expected two pedestrians per crossing; got {counts}")
    return assignments


def require_pedestrians_on_crossings(entities, assignments, spec):
    for identity, key in assignments.items():
        crossing = spec.crossings[key]
        distance = distance_to_segment(position(entities[identity]), crossing.start,
                                       crossing.end)
        require(distance <= 0.71,
                f"pedestrian {identity} left authored crossing {key}: "
                f"{distance:.6f} m")


class SignalCityController(TrafficController):
    def __init__(self, connection, play_id, expected_source, spec):
        super().__init__(connection, play_id, expected_source, spec.expected_traffic)
        self.session = "signal-city-" + uuid.uuid4().hex
        self.spec = spec

    def envelope(self):
        message = super().envelope()
        message.source_id = "signal-city-smoke"
        return message

    async def hello(self):
        server = pb.Envelope.FromString(await asyncio.wait_for(self.ws.recv(), 3))
        require(server.WhichOneof("payload") == "hello", "first frame must be Hello")
        require(server.source_id == self.expected_source,
                "port does not belong to our isolated host")
        require(server.schema_version == 2, "schema mismatch")
        require(server.map_package_checksum == self.expected.map_checksum,
                "child loaded the wrong collision map")
        capabilities = set(server.hello.capabilities)
        required = {"world-health.v1", "traffic-signals.v1", "pedestrian-signals.v1"}
        require(required.issubset(capabilities),
                "server build lacks Signal City traffic/pedestrian capabilities")
        self.checksum = server.map_package_checksum
        request = self.envelope()
        request.hello.build = "signal-city-smoke-v2"
        request.hello.schema = server.hello.schema
        request.hello.capabilities.extend([
            "world-state.v2", "control.v2", "simulation-reset.v1",
            "map-package-checksum.v1", "world-health.v1", "traffic-signals.v1",
            "pedestrian-signals.v1",
        ])
        await self.ws.send(request.SerializeToString())

    async def receive_state(self):
        message = await super().receive_state()
        validate_signal_kinds(message, self.spec)
        runtime_entities(message, self.spec)
        return message


async def connect_child(url, process):
    deadline = time.monotonic() + 10
    while True:
        require(process.poll() is None,
                "isolated host exited during startup; inspect its logs")
        try:
            return await websockets.connect(url, open_timeout=1, close_timeout=1,
                                            max_queue=256)
        except (OSError, TimeoutError):
            if time.monotonic() >= deadline:
                raise
            await asyncio.sleep(0.1)


async def observe_active_cycle(controller, baseline, assignments, spec):
    seen = {identity: set() for identity in spec.signal_kinds}
    moved_npcs = set()
    moved_pedestrians = set()
    previous_moving = {identity: False for identity in spec.pedestrian_ids}
    previous_aspects = {}
    frames = 0
    sender = asyncio.create_task(heartbeat(controller))
    try:
        while True:
            if sender.done():
                sender.result()
            message = await asyncio.wait_for(controller.receive_state(), 3)
            require(message.play_session_id == controller.play_id,
                    "active cycle used the wrong play identity")
            if message.world_state.health.status == "awaiting_control":
                continue
            require(message.world_state.health.status == "active",
                    "20Hz heartbeat failed to maintain the control lease")
            entities = runtime_entities(message, spec)
            require_pedestrians_on_crossings(entities, assignments, spec)
            aspects = crossing_aspects(message)
            for head in message.world_state.traffic_signals:
                seen[head.signal_id].add(head.aspect)
            for identity in spec.npc_ids:
                if planar_distance(position(entities[identity]), baseline[identity]) > 0.05:
                    moved_npcs.add(identity)
            for identity in spec.pedestrian_ids:
                entity = entities[identity]
                moving = entity.speed > 0.1
                key = assignments[identity]
                if moving and not previous_moving[identity]:
                    require(aspects.get(key) == pb.TRAFFIC_SIGNAL_GREEN
                            or previous_aspects.get(key) == pb.TRAFFIC_SIGNAL_GREEN,
                            f"pedestrian {identity} started without a green walk phase")
                previous_moving[identity] = moving
                if planar_distance(position(entity), baseline[identity]) > 0.05:
                    moved_pedestrians.add(identity)
            previous_aspects = aspects
            frames += 1
            if message.simulation_time_ns >= spec.expected_traffic.observation_ns:
                break
    finally:
        sender.cancel()
        with suppress(asyncio.CancelledError):
            await sender

    for identity, aspects in seen.items():
        expected = {pb.TRAFFIC_SIGNAL_RED, pb.TRAFFIC_SIGNAL_GREEN}
        if spec.signal_kinds[identity] == pb.TRAFFIC_SIGNAL_KIND_VEHICLE:
            expected.add(pb.TRAFFIC_SIGNAL_YELLOW)
        require(aspects == expected,
                f"signal {identity} did not publish its complete vehicle R/Y/G or pedestrian STOP/WALK cycle")
    require(moved_npcs == set(spec.npc_ids),
            f"not all configured NPCs moved: {sorted(moved_npcs)}")
    require(moved_pedestrians == set(spec.pedestrian_ids),
            f"not all configured pedestrians moved: {sorted(moved_pedestrians)}")
    print(f"PASS full Signal City cycle: {frames} atomic states; "
          f"NPCs={len(moved_npcs)}, pedestrians={len(moved_pedestrians)}, "
          f"vehicle/pedestrian heads=8/8", flush=True)


async def verify_safe_stop_and_recovery(controller, assignments, spec):
    safe = await wait_state(
        controller, lambda message: message.world_state.health.status == "safe_stop",
        timeout=spec.command_timeout_ms / 1000 + 1.0)
    require(safe.world_state.health.last_command_age_ns
            > spec.command_timeout_ms * 1_000_000,
            "SafeStop fired before the configured soft timeout")
    require(all(head.aspect == pb.TRAFFIC_SIGNAL_RED
                for head in safe.world_state.traffic_signals),
            "SafeStop did not force every vehicle/pedestrian signal red")
    entities = runtime_entities(safe, spec)
    require_pedestrians_on_crossings(entities, assignments, spec)
    frozen = {identity: position(entity) for identity, entity in entities.items()}
    for _ in range(4):
        message = await asyncio.wait_for(controller.receive_state(), 1)
        require(message.world_state.health.status == "safe_stop",
                "SafeStop did not remain observable during the freeze check")
        current = runtime_entities(message, spec)
        for identity, entity in current.items():
            require(entity.speed == 0
                    and abs(entity.linear_velocity_enu.x) < 1e-10
                    and abs(entity.linear_velocity_enu.y) < 1e-10
                    and position(entity) == frozen[identity],
                    f"entity {identity} did not freeze atomically in SafeStop")
    print(f"PASS SafeStop: all {len(spec.runtime_ids)} dynamic entities frozen and all signals red", flush=True)

    await controller.control()
    sender = asyncio.create_task(heartbeat(controller))
    try:
        recovered = await wait_state(
            controller, lambda message: message.world_state.health.status == "active", timeout=2)
        require(recovered.simulation_time_ns >= safe.simulation_time_ns,
                "same-PIE recovery regressed simulation time")

        async def movement_resumed():
            while True:
                message = await controller.receive_state()
                require(message.world_state.health.status == "active",
                        "recovered control lease became inactive")
                current = runtime_entities(message, spec)
                if any(planar_distance(position(entity), frozen[identity]) > 0.02
                       for identity, entity in current.items()):
                    return message

        await asyncio.wait_for(movement_resumed(), 3)
    finally:
        sender.cancel()
        with suppress(asyncio.CancelledError):
            await sender
    print("PASS same-PIE recovery: fresh control restored authoritative movement", flush=True)
    return frozen


async def exercise(url, process, source, spec):
    connection = await connect_child(url, process)
    first_play = "signal-city-play-" + uuid.uuid4().hex
    controller = SignalCityController(connection, first_play, source, spec)
    try:
        await controller.hello()
        await wait_state(controller,
                         lambda message: message.world_state.health.status == "awaiting_reset")
        await reset_to_all_red(controller)
        initial = await wait_state(
            controller, lambda message: message.world_state.health.status == "awaiting_control")
        initial_entities = runtime_entities(initial, spec)
        require(all(entity.speed == 0 for entity in initial_entities.values()),
                "new PIE dynamic entities must start stationary")
        baseline = {identity: position(entity)
                    for identity, entity in initial_entities.items()}
        assignments = assign_pedestrian_crossings(initial_entities, spec)
        await observe_active_cycle(controller, baseline, assignments, spec)
        await verify_safe_stop_and_recovery(controller, assignments, spec)
    finally:
        await connection.close()

    # End PIE -> Play is a fresh socket/session/play identity. The previous
    # connection cannot change play identity in-place by design.
    connection = await websockets.connect(url, open_timeout=2, close_timeout=1,
                                          max_queue=256)
    second_play = "signal-city-new-play-" + uuid.uuid4().hex
    controller = SignalCityController(connection, second_play, source, spec)
    try:
        await controller.hello()
        # The host still publishes the completed previous play until this new
        # connection supplies its new play identity. Send Reset immediately;
        # waiting for awaiting_reset here would let the old lease hard-expire.
        await reset_to_all_red(controller)
        reset = await wait_state(
            controller, lambda message: message.world_state.health.status == "awaiting_control")
        reset_entities = runtime_entities(reset, spec)
        for identity, entity in reset_entities.items():
            require(entity.speed == 0 and all(abs(actual - expected) < 1e-6
                    for actual, expected in zip(position(entity), baseline[identity])),
                    f"fresh PIE did not reset entity {identity} to its initial state")
        require(assign_pedestrian_crossings(reset_entities, spec) == assignments,
                "fresh PIE changed pedestrian/crossing ownership")
        print("PASS fresh PIE reset: all 10 NPCs/8 pedestrians restored to initial state",
              flush=True)
    finally:
        await connection.close()


def print_preflight(spec):
    kinds = list(spec.signal_kinds.values())
    print("PASS Signal City v2 preflight", flush=True)
    print(f"  runtime_config={spec.runtime_config}", flush=True)
    print(f"  map={spec.map_id} checksum={spec.expected_traffic.map_checksum}", flush=True)
    print(f"  traffic_checksum={spec.expected_traffic.network_checksum}", flush=True)
    print(f"  routes={len(spec.routes)} NPCs={spec.npc_count}; "
          f"crossings={len(spec.crossings)} pedestrians={EXPECTED_PEDESTRIAN_COUNT}", flush=True)
    print(f"  signal_heads vehicle={kinds.count(pb.TRAFFIC_SIGNAL_KIND_VEHICLE)} "
          f"pedestrian={kinds.count(pb.TRAFFIC_SIGNAL_KIND_PEDESTRIAN)}", flush=True)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--runtime-config", type=Path,
        default=ROOT / "cpp/host/config/signal_city_server.cfg",
        help="Signal City runtime cfg (paths and acceptance counts are read from it)")
    parser.add_argument(
        "--host", type=Path,
        default=ROOT / "cpp/host/build/Release/simcore_publisher.exe",
        help="current Release host executable")
    parser.add_argument(
        "--preflight-only", action="store_true",
        help="validate config/package/protobuf inputs without starting a server")
    args = parser.parse_args()

    spec = build_spec(args.runtime_config)
    print_preflight(spec)
    if args.preflight_only:
        return
    require(args.host.is_file(), f"build the current Release host first: {args.host}")

    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    require(port != 9000, "refusing the user's standard server port")
    source = "signal-city-host-" + uuid.uuid4().hex
    directory = ROOT / "runtime_logs"
    directory.mkdir(exist_ok=True)
    stem = "signal-city-smoke-" + datetime.now().strftime("%Y%m%d-%H%M%S") \
        + "-" + uuid.uuid4().hex[:8]
    command = [str(args.host.resolve()), "--runtime-config", str(spec.runtime_config),
               "--ws-port", str(port), "--source-id", source]
    with (directory / (stem + ".stdout.log")).open("wb") as stdout, \
            (directory / (stem + ".stderr.log")).open("wb") as stderr:
        process = subprocess.Popen(
            command, cwd=ROOT, stdout=stdout, stderr=stderr,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        print(f"Isolated Signal City server PID={process.pid}, port={port}; "
              f"logs={directory / stem}", flush=True)
        try:
            overall_timeout = spec.expected_traffic.observation_ns / SECOND_NS + 25
            asyncio.run(asyncio.wait_for(
                exercise(f"ws://127.0.0.1:{port}", process, source, spec),
                overall_timeout))
            print("PASS Signal City v2 WebSocket smoke (isolated child only)", flush=True)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


if __name__ == "__main__":
    main()
