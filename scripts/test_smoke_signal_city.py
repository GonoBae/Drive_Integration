"""Pure Signal City geometry checks; no server, socket or file mutation."""

from types import SimpleNamespace
import json
import unittest

from smoke_signal_city import (build_lane_change_cells, build_spec, build_segment_index,
                               distance_to_route, point_near_route,
                               inside_lane_change, point_at_station,
                               validate_npc_path, validate_npc_spawn_offsets,
                               parse_key_value, ROOT)


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


def entity(x, y, speed=4.0):
    return SimpleNamespace(entity_id=1001, speed=speed,
                           position_enu=SimpleNamespace(x=x, y=y, z=0.75))


class SignalCityAutonomousGeometryTests(unittest.TestCase):
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

    def test_pre_smoothing_twenty_meter_offset_is_rejected_before_connecting(self):
        spec = build_spec(ROOT / "cpp/host/config/signal_city_server.cfg")
        network = json.loads(spec.traffic_network.read_text(encoding="utf-8"))
        with self.assertRaisesRegex(AssertionError, "NPC 1009.*controlled stopping margin"):
            validate_npc_spawn_offsets(network, spec.routes, 10, 20.0, 90.0)

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
        validate_npc_path(entity(8, 1.25, 6.5), spec)
        for sample in (entity(8, 0, 6.1), entity(8, 2.5, 6.1),
                       entity(8, 1.25, 6.8), entity(8, 10, 6.1)):
            with self.assertRaises(AssertionError):
                validate_npc_path(sample, spec)

    def test_cells_split_at_both_lanes_vertices(self):
        spec = fixture()
        self.assertEqual(len(spec.lane_change_cells), 3)
        self.assertTrue(inside_lane_change((7.5, 1.25, 0.0), spec.lane_change_cells))
        self.assertFalse(inside_lane_change((7.5, 3.0, 0.0), spec.lane_change_cells))

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
