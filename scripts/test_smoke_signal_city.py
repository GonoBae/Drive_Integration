"""Pure Signal City geometry checks; no server, socket or file mutation."""

from types import SimpleNamespace
import asyncio
import json
import math
import struct
import unittest
from unittest.mock import AsyncMock, Mock, patch

from smoke_signal_city import (build_lane_change_cells, build_spec, build_segment_index,
                               distance_to_route, point_near_route,
                               inside_lane_change, point_at_station,
                               validate_npc_path, validate_npc_spawn_offsets,
                               validate_npc_profile, npc_vehicle_profile,
                               validate_pedestrian_profile, pedestrian_profile,
                               validate_walk_windows, Crossing,
                               MeasuredRoad, NpcBypassContinuity, SignalCityController,
                               within_route_distance,
                               lane_change_velocity_bounded,
                               parse_key_value, ROOT, pb)


def fixture(autonomous=True):
    lanes = {
        10: ((0.0, 0.0, 0.0), (8.0, 0.0, 0.0), (20.0, 0.0, 0.0)),
        20: ((0.0, 2.5, 0.0), (7.0, 2.5, 0.0), (20.0, 2.5, 0.0)),
        30: ((0.0, 10.0, 0.0), (20.0, 10.0, 0.0)),
    }
    network = {"lanes": [{"id": 10, "lane_changes": [{
        "target_lane_id": 20, "source_begin_m": 4.0, "source_end_m": 12.0,
        "target_begin_m": 4.0, "target_end_m": 12.0,
    }]}]}
    segments = tuple((a, b) for points in lanes.values() for a, b in zip(points, points[1:]))
    fixed = tuple(zip(lanes[10], lanes[10][1:]))
    return SimpleNamespace(npc_autonomous=autonomous, authored_segments=segments,
                           lane_change_cells=build_lane_change_cells(network, lanes),
                           authored_segment_index=build_segment_index(segments),
                           routes=((10,),), route_segments=(fixed,), npc_max_speed_mps=6.0)


def entity(x, y, speed=4.0, entity_id=1001, velocity=None):
    if velocity is None:
        velocity = (speed, 0.0)
    else:
        speed = math.hypot(*velocity)
    return SimpleNamespace(entity_id=entity_id, speed=speed,
                           position_enu=SimpleNamespace(x=x, y=y, z=0.75),
                           linear_velocity_enu=SimpleNamespace(x=velocity[0], y=velocity[1], z=0.0))


def measured_road_fixture(material=1):
    count = 81
    header = struct.pack("<8s6I6d", b"SIMGHF2\0", 2, 80, count, count, 20, 12,
                         -20.0, -20.0, 0.5, 0.0, 0.0, 0.5)
    samples = struct.pack("<I4f", 1, 0.0, 0.0, 0.0, 1.0) * (count * count)
    cells = b"".join(struct.pack("<IIf", 1,
                                material if abs(-20 + (row + .5) * .5) <= 4.5 else 3, 1.0)
                     for row in range(count - 1) for _ in range(count - 1))
    return MeasuredRoad(header + samples + cells)


def bypass_entity(x=15.0, y=3.0, speed=2.0):
    sample = pb.EntityState(entity_id=1001, entity_kind=pb.ENTITY_KIND_NPC_VEHICLE,
                            runtime_vehicle_class=pb.RUNTIME_VEHICLE_CLASS_SEDAN,
                            collision_half_length=2.2, collision_half_width=1.0,
                            collision_half_height=.75, heading=90, speed=speed,
                            npc_local_bypass_active=True)
    sample.position_enu.x, sample.position_enu.y, sample.position_enu.z = x, y, .85
    sample.linear_velocity_enu.x = speed
    return sample


class SignalCityAutonomousGeometryTests(unittest.TestCase):
    def test_local_bypass_wire_field_is_additive(self):
        self.assertEqual(pb.EntityState.DESCRIPTOR.fields_by_name["npc_local_bypass_active"].number, 40)
        sample = bypass_entity()
        self.assertTrue(pb.EntityState.FromString(sample.SerializeToString()).npc_local_bypass_active)
        sample.npc_local_bypass_active = False
        inactive = sample.SerializeToString()
        sample.ClearField("npc_local_bypass_active")
        self.assertEqual(sample.SerializeToString(), inactive)

    def test_only_explicit_local_bypass_uses_measured_road_not_lane_tolerance(self):
        spec = fixture()
        spec.measured_road = measured_road_fixture()
        sample = bypass_entity()
        validate_npc_path(sample, spec)
        sample.npc_local_bypass_active = False
        with self.assertRaisesRegex(AssertionError, "left authored lanes"):
            validate_npc_path(sample, spec)
        # The original 3cm test remains unchanged, including a 3.059cm failure.
        with self.assertRaisesRegex(AssertionError, "left authored lanes"):
            validate_npc_path(entity(15, .030590), spec)
        sample.npc_local_bypass_active = True
        spec.npc_autonomous = False
        with self.assertRaisesRegex(AssertionError, "requires autonomous"):
            validate_npc_path(sample, spec)

    def test_local_bypass_rejects_unsupported_body_height_and_nonroad(self):
        spec = fixture()
        spec.measured_road = measured_road_fixture()
        for x, y, z in ((19, 3, .85), (15, 4, .85), (15, 3, 1.15), (15, 5, .85)):
            with self.subTest(point=(x, y, z)):
                sample = bypass_entity(x, y)
                sample.position_enu.z = z
                with self.assertRaisesRegex(AssertionError, "support"):
                    validate_npc_path(sample, spec)
        for material in (0, 3, 99):
            spec.measured_road = measured_road_fixture(material)
            with self.assertRaisesRegex(AssertionError, "support"):
                validate_npc_path(bypass_entity(), spec)
        spec.measured_road = None
        with self.assertRaisesRegex(AssertionError, "requires measured"):
            validate_npc_path(bypass_entity(), spec)

    def test_local_bypass_preserves_profile_finite_and_velocity_checks(self):
        spec = fixture()
        spec.measured_road = measured_road_fixture()
        for field, value, reason in (("speed", 6.06, "configured speed"),
                                      ("heading", math.nan, "finite"),
                                      ("collision_half_width", .5, "shape changed")):
            sample = bypass_entity()
            setattr(sample, field, value)
            with self.assertRaisesRegex(AssertionError, reason):
                validate_npc_path(sample, spec)
        sample = bypass_entity()
        sample.linear_velocity_enu.y = 1
        with self.assertRaisesRegex(AssertionError, "disagrees"):
            validate_npc_path(sample, spec)
        spec.authored_segments = (((0, 0, 0), (20, 0, 0)),)
        spec.authored_segment_index = build_segment_index(spec.authored_segments)
        with self.assertRaisesRegex(AssertionError, "offset envelope"):
            validate_npc_path(bypass_entity(y=5.3), spec)

    def test_bypass_entry_exit_continuity_deceleration_and_play_reset(self):
        spec = fixture()
        spec.npc_ids = (1001,)
        tracker = NpcBypassContinuity(spec)
        tracker.observe("play-a", 0, {1001: bypass_entity(speed=4)})
        tracker.observe("play-a", 100_000_000, {1001: bypass_entity(x=15.4, speed=4)})
        with self.assertRaisesRegex(AssertionError, "jumped"):
            tracker.observe("play-a", 200_000_000, {1001: bypass_entity(x=17.0)})
        with self.assertRaisesRegex(AssertionError, "did not settle"):
            tracker.observe("play-a", 2_000_000_000, {1001: bypass_entity(x=16.0, speed=4)})
        tracker.observe("play-a", 2_000_000_000, {1001: bypass_entity(x=16.0)})
        stopped = bypass_entity(x=16.0, speed=0)
        tracker.observe("play-a", 2_000_000_000, {1001: stopped})
        stopped.npc_local_bypass_active = False
        stopped.position_enu.x = 18
        with self.assertRaisesRegex(AssertionError, "jumped"):
            tracker.observe("play-a", 2_100_000_000, {1001: stopped})
        tracker.observe("play-b", 0, {1001: stopped})
        self.assertEqual(tracker.started_ns, {})

    def test_measured_road_reader_uses_triangle_height_and_rejects_invalid_cells(self):
        road = measured_road_fixture()
        self.assertEqual(road.support(0, 0), (0.0, 1.0))
        self.assertIsNone(road.support(0, 5))
        self.assertIsNone(road.support(20.001, 0))
        self.assertIsNone(road.support(math.inf, 0))
        for size in (0, 79, len(road.data) - 1):
            with self.assertRaises(AssertionError):
                MeasuredRoad(road.data[:size])
        # The upper-right sample is 1m higher: triangular, not bilinear interpolation.
        data = bytearray(road.data)
        struct.pack_into("<I4f", data, 80 + (41 * 81 + 41) * 20, 1, 1, 0, 0, 1)
        sloped = MeasuredRoad(data)
        self.assertAlmostEqual(sloped.support(.375, .125)[0], .25)
        self.assertAlmostEqual(sloped.support(.125, .375)[0], .25)
        struct.pack_into("<I", data, sloped.cell_offset + (40 * 80 + 40) * 12, 0)
        self.assertIsNone(MeasuredRoad(data).support(.25, .25))

    def test_local_envelope_index_preserves_exact_distance_at_cell_edges(self):
        spec = fixture()
        for x in (-8.01, -8, -5.28, 0, 7.999, 8, 8.001, 16, 20, 25.28, 25.281):
            for y in (-5.281, -5.28, 0, 2.5, 5.28, 7.78, 8, 15.28, 15.281):
                point = (x, y, .85)
                expected = distance_to_route(point, spec.authored_segments) <= 5.28
                self.assertEqual(within_route_distance(point, spec.authored_segments,
                    spec.authored_segment_index, 5.28), expected, (x, y))
                self.assertEqual(within_route_distance(point, spec.authored_segments,
                    None, 5.28), expected, (x, y))

    def test_one_validation_per_received_snapshot_without_accepting_other_frames(self):
        spec = fixture()
        spec.expected_traffic = None
        controller = SignalCityController(None, "play", "source", spec)
        controller.bypass_continuity = Mock()
        first = pb.Envelope(sequence=1, play_session_id="play", simulation_time_ns=1)
        second = pb.Envelope(sequence=1, play_session_id="play", simulation_time_ns=1)
        entities = {1001: bypass_entity()}

        async def exercise():
            with patch("smoke_signal_city.TrafficController.receive_state",
                       new=AsyncMock(side_effect=(first, second))), \
                    patch("smoke_signal_city.validate_signal_kinds"), \
                    patch("smoke_signal_city.runtime_entities", return_value=entities) as validate:
                self.assertIs(await controller.receive_state(), first)
                self.assertIs(controller.validated_entities(first), entities)
                self.assertIs(controller.validated_entities(first), entities)
                validate.assert_called_once_with(first, spec)
                with self.assertRaisesRegex(AssertionError, "unvalidated snapshot"):
                    controller.validated_entities(second)
                self.assertIs(await controller.receive_state(), second)
                self.assertEqual(validate.call_count, 2)
                with self.assertRaisesRegex(AssertionError, "unvalidated snapshot"):
                    controller.validated_entities(first)
        asyncio.run(exercise())

    def test_walk_phase_serves_slow_population_not_only_adults(self):
        crossing = Crossing((1, 105), (1, 2), (0, 0, 0), (0, 23, 0))
        def plan(seconds):
            return {1: (0, ((seconds * 1_000_000_000, frozenset({105}), frozenset()),),
                        seconds * 1_000_000_000)}
        validate_walk_windows({(1, 105): crossing}, plan(24))
        for seconds in (18, 21, 23):
            with self.assertRaisesRegex(AssertionError, "cannot serve the slowest pedestrian"):
                validate_walk_windows({(1, 105): crossing}, plan(seconds))

    def test_pedestrian_population_shapes_and_speeds_follow_identity(self):
        # IDs 2001..2008 cycle adult, small body, older body, independent of
        # the wire values so accidentally publishing all adults is detected.
        profiles = ((0.9, 0.35, 1.35), (0.625, 0.22, 1.10), (0.825, 0.28, 1.00))
        for identity in range(2001, 2009):
            with self.subTest(entity_id=identity):
                height, radius, speed = profiles[identity % 3]
                sample = pb.EntityState(entity_id=identity, collision_half_height=height,
                                        collision_radius=radius, speed=speed)
                validate_pedestrian_profile(sample)
                sample.speed = speed + 0.02
                with self.assertRaisesRegex(AssertionError, "exceeded walking speed"):
                    validate_pedestrian_profile(sample)
                sample.speed = speed
                sample.collision_half_height = profiles[(identity + 1) % 3][0]
                with self.assertRaisesRegex(AssertionError, "collision shape changed"):
                    validate_pedestrian_profile(sample)
        for identity in (2000, 2009, 1001):
            with self.assertRaisesRegex(AssertionError, "unexpected pedestrian entity ID"):
                pedestrian_profile(identity)

    def test_all_ten_ids_publish_their_expected_class_and_shape(self):
        classes = (pb.RUNTIME_VEHICLE_CLASS_SEDAN, pb.RUNTIME_VEHICLE_CLASS_COMPACT,
                   pb.RUNTIME_VEHICLE_CLASS_TRUCK, pb.RUNTIME_VEHICLE_CLASS_MOTORCYCLE)
        shapes = ((2.20, 1.00, 0.75), (1.75, 0.86, 0.70),
                  (3.10, 1.05, 0.965), (1.10, 0.42, 0.68))
        for index in range(10):
            with self.subTest(entity_id=1001 + index):
                sample = pb.EntityState(entity_id=1001 + index,
                                        runtime_vehicle_class=classes[index % 4])
                (sample.collision_half_length, sample.collision_half_width,
                 sample.collision_half_height) = shapes[index % 4]
                validate_npc_profile(sample)
                sample.runtime_vehicle_class = classes[(index + 1) % 4]
                with self.assertRaisesRegex(AssertionError, "vehicle class changed"):
                    validate_npc_profile(sample)
                # A different class's self-consistent shape is also invalid
                # because fleet identity, not received class, is authoritative.
                (sample.collision_half_length, sample.collision_half_width,
                 sample.collision_half_height) = shapes[(index + 1) % 4]
                with self.assertRaisesRegex(AssertionError, "vehicle class changed"):
                    validate_npc_profile(sample)
                sample.runtime_vehicle_class = classes[index % 4]
                with self.assertRaisesRegex(AssertionError, "collision shape changed"):
                    validate_npc_profile(sample)

    def test_invalid_profile_ids_and_collision_radius_are_rejected(self):
        for identity in (1000, 1011, 2001):
            with self.assertRaisesRegex(AssertionError, "unexpected NPC entity ID"):
                npc_vehicle_profile(identity)
        sample = pb.EntityState(entity_id=1001,
                                runtime_vehicle_class=pb.RUNTIME_VEHICLE_CLASS_SEDAN,
                                collision_half_length=2.2, collision_half_width=1.0,
                                collision_half_height=0.75, collision_radius=0.1)
        with self.assertRaisesRegex(AssertionError, "collision shape changed"):
            validate_npc_profile(sample)
        sample.runtime_vehicle_class = pb.RUNTIME_VEHICLE_CLASS_UNSPECIFIED
        with self.assertRaisesRegex(AssertionError, "vehicle class changed"):
            validate_npc_profile(sample)

    def test_class_speed_limits_preserve_strict_lane_and_transition_bounds(self):
        spec = fixture()
        for index, scale in enumerate((1.00, 1.08, 0.72, 1.12)):
            identity = 1001 + index
            for point in ((8, 0), (8, 1.25)):
                limit = 6.0 * scale + 0.05
                validate_npc_path(entity(*point, speed=limit, entity_id=identity), spec)
                with self.assertRaisesRegex(AssertionError, "exceeded"):
                    validate_npc_path(entity(*point, speed=limit + 0.001,
                                             entity_id=identity), spec)
            lateral_limit = 6.0 * scale * 1.875 / 8.0 * 2.5 + 0.05
            validate_npc_path(entity(8, 1.25, entity_id=identity,
                                     velocity=(6.0 * scale, lateral_limit - 1e-8)), spec)
            with self.assertRaisesRegex(AssertionError, "lane-change velocity bounds"):
                validate_npc_path(entity(8, 1.25, entity_id=identity,
                    velocity=(6.0 * scale, lateral_limit + 0.001)), spec)

    def test_spawn_clearance_accounts_for_the_truck_front_extent(self):
        network = {"lanes": [{"id": 10, "points": [(0, 0, 0), (10, 0, 0)],
                              "signal_group_id": 1}]}
        validate_npc_spawn_offsets(network, ((10,),), 2, 6.5, 0.0)
        with self.assertRaisesRegex(AssertionError, "NPC 1003.*required=3.600m"):
            validate_npc_spawn_offsets(network, ((10,),), 3, 6.5, 0.0)

    def test_compact_spawn_uses_its_own_smaller_front_extent(self):
        network = {"lanes": [
            {"id": 10, "points": [(0, 0, 0), (10, 0, 0)], "signal_group_id": 1},
            {"id": 20, "points": [(0, 2, 0), (9.5, 2, 0)], "signal_group_id": 1},
        ]}
        starts = validate_npc_spawn_offsets(network, ((10,), (20,)), 2, 7.0, 0.0)
        self.assertEqual(starts, ((1001, 10, 7.0), (1002, 20, 7.0)))

    def test_default_runtime_config_uses_autonomous_wire_acceptance(self):
        spec = build_spec(ROOT / "cpp/host/config/signal_city_server.cfg")
        self.assertTrue(spec.npc_autonomous)
        self.assertGreater(len(spec.authored_segments), 0)
        self.assertGreater(len(spec.lane_change_cells), 0)

    def test_all_ten_default_spawns_respect_controlled_stopping_margins(self):
        config = ROOT / "cpp/host/config/signal_city_server.cfg"
        spec = build_spec(config)
        values = parse_key_value(config)
        network = json.loads(spec.traffic_network.read_text(encoding="utf-8"))
        starts = validate_npc_spawn_offsets(network, spec.routes, spec.npc_count,
                                           float(values["npc_start_offset_m"]),
                                           float(values["npc_spacing_m"]))
        self.assertEqual(tuple(row[0] for row in starts), spec.npc_ids)
        self.assertEqual(len(starts), 10)

    def test_authored_stopline_rejects_body_front_overlap_before_connecting(self):
        spec = build_spec(ROOT / "cpp/host/config/signal_city_server.cfg")
        network = json.loads(spec.traffic_network.read_text(encoding="utf-8"))
        lanes = {lane["id"]: lane for lane in network["lanes"]}
        # Derive the stop station from the current authored curve instead of
        # treating one older map's 20m spawn offset as universally unsafe.
        for route in spec.routes:
            with self.subTest(route=route):
                stop_station = 0.0
                stop_lane = None
                for identity in route:
                    lane = lanes[identity]
                    stop_station += sum(math.dist(a, b) for a, b
                                        in zip(lane["points"], lane["points"][1:]))
                    if lane.get("signal_group_id", 0):
                        stop_lane = identity
                        break
                self.assertIsNotNone(stop_lane, "route must contain a controlled stop line")
                # NPC 1001 is the sedan: 2.20m body front + 0.50m stop margin.
                safe_offset = stop_station - 2.70
                starts = validate_npc_spawn_offsets(network, (route,), 1, safe_offset, 0.0)
                self.assertEqual(starts[0][0:2], (1001, stop_lane))
                with self.assertRaisesRegex(AssertionError, "NPC 1001.*controlled stopping margin"):
                    validate_npc_spawn_offsets(network, (route,), 1, safe_offset + 0.01, 0.0)

    def test_fixed_mode_retains_exact_original_route_rule(self):
        spec = fixture(False)
        validate_npc_path(entity(8, 0), spec)
        for point in ((8, 2.5), (8, 1.25), (8, 10)):
            with self.assertRaises(AssertionError):
                validate_npc_path(entity(*point), spec)

    def test_autonomous_routes_include_other_authored_lanes(self):
        spec = fixture()
        validate_npc_path(entity(8, 0), spec)
        validate_npc_path(entity(8, 2.5), spec)
        validate_npc_path(entity(8, 10), spec)

    def test_only_authored_station_window_allows_lateral_band(self):
        spec = fixture()
        for point in ((4, 1.25), (8, 1.25), (12, 1.25)):
            validate_npc_path(entity(*point), spec)
        for point in ((3.9, 1.25), (12.1, 1.25), (8, 3.0), (8, 8.0), (25, 0)):
            with self.assertRaises(AssertionError):
                validate_npc_path(entity(*point), spec)

    def test_speed_allowance_is_lateral_only_and_bounded(self):
        spec = fixture()
        validate_npc_path(entity(8, 1.25, velocity=(6.0, 2.5)), spec)
        for sample in (entity(8, 0, 6.1), entity(8, 2.5, 6.1),
                       entity(8, 1.25, 6.1), entity(8, 10, 6.1),
                       entity(8, 1.25, velocity=(6.0, 4.0)),
                       entity(8, 1.25, velocity=(-0.1, 0.0))):
            with self.assertRaises(AssertionError):
                validate_npc_path(sample, spec)

    def test_transition_velocity_bound_uses_local_rotated_tangent(self):
        # A northbound piece of a curved route must not use the earlier east
        # tangent. Corresponding target/source lengths also encode station scale.
        cells = (((0, 0, 0), (8, 0, 0), (8, 2.5, 0), (0, 2.5, 0)),
                 ((8, 0, 0), (8, 8, 0), (5.5, 8.4, 0), (5.5, 0, 0)))
        self.assertTrue(lane_change_velocity_bounded(
            entity(6.75, 4, velocity=(-2.5, 6.0)), cells, 6.0))
        self.assertFalse(lane_change_velocity_bounded(
            entity(6.75, 4, velocity=(-2.5, 7.0)), cells, 6.0))
        self.assertFalse(lane_change_velocity_bounded(
            entity(6.75, 4, velocity=(-4.0, 6.0)), cells, 6.0))

    def test_cells_split_at_both_lanes_vertices(self):
        spec = fixture()
        self.assertEqual(len(spec.lane_change_cells), 3)
        self.assertTrue(inside_lane_change((7.5, 1.25, 0.0), spec.lane_change_cells))
        self.assertFalse(inside_lane_change((7.5, 3.0, 0.0), spec.lane_change_cells))

    def test_nearly_identical_vertex_cuts_do_not_create_zero_length_cells(self):
        lanes = {
            10: ((0, 0, 0), (1, 0, 0), (2, 0, 0)),
            20: ((0, 2.5, 0), (1 + 1e-15, 2.5, 0), (2, 2.5, 0)),
        }
        network = {"lanes": [{"id": 10, "lane_changes": [{
            "target_lane_id": 20, "source_begin_m": 0.0, "source_end_m": 2.0,
            "target_begin_m": 0.0, "target_end_m": 2.0,
        }]}]}
        cells = build_lane_change_cells(network, lanes)
        self.assertEqual(len(cells), 2)
        self.assertTrue(all(math.dist(a, b) > 0 and math.dist(c, d) > 0
                            for a, b, c, d in cells))
        self.assertTrue(inside_lane_change((1, 1.25, 0), cells))
        self.assertFalse(inside_lane_change((1, 3, 0), cells))

    def test_station_uses_authored_three_dimensional_arc_length(self):
        points = ((0, 0, 0), (3, 0, 4), (6, 0, 8))
        self.assertEqual(point_at_station(points, 5), (3.0, 0.0, 4.0))
        self.assertAlmostEqual(point_at_station(points, 8)[0], 4.8)
        with self.assertRaises(AssertionError):
            point_at_station(points, 11)

    def test_segment_broadphase_preserves_exact_boundary_decisions(self):
        segments = (((-9.0, -8.0, 0.0), (17.0, 8.0, 0.0)),
                    ((0.0, 0.0, 0.0), (24.0, 0.0, 0.0)),
                    ((8.0, -17.0, 0.0), (8.0, 17.0, 0.0)))
        index = build_segment_index(segments)
        for x in (-9.031, -9, -8.031, -8, -0.031, 0, 7.969, 8, 8.03, 16, 24, 24.031):
            for y in (-17.031, -16, -8, -0.031, -0.03, -0.029, 0, 0.029, 0.03, 8, 17.031):
                point = (x, y, 0.0)
                expected = distance_to_route(point, segments) < 0.03
                self.assertEqual(point_near_route(point, segments), expected)
                self.assertEqual(point_near_route(point, segments, index), expected)


if __name__ == "__main__":
    unittest.main()
