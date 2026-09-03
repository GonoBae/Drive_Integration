"""Pure fixture tests: never starts a host, opens a socket or writes a map."""

import json
import unittest

from smoke_traffic import (CYCLE_NS, SECOND_NS, TrafficController, expected_phase, expected_signal,
                           network_expectations, pb, validate_world)
from test_smoke_health import FakeConnection, server_hello


def fixture_bytes():
    return json.dumps({
        "format_version": 1,
        "source_map_checksum": "fnv1a64:0123456789abcdef",
        "lanes": [],
        "signals": [
            {"id": 1, "group_id": 1, "position_enu": [-8, 137, 0], "heading_deg": 90},
            {"id": 2, "group_id": 1, "position_enu": [8, 147, 0], "heading_deg": 270},
            {"id": 3, "group_id": 2, "position_enu": [-5, 152, 0], "heading_deg": 180},
        ],
    }).encode()


def fixture_v2_bytes():
    return json.dumps({
        "format_version": 2,
        "source_map_checksum": "fnv1a64:0123456789abcdef",
        "lanes": [],
        "signals": [
            {"id": 1, "group_id": 101, "controller_id": 7,
             "position_enu": [-8, 37, 0], "heading_deg": 90},
            {"id": 2, "group_id": 102, "controller_id": 7,
             "position_enu": [5, 32, 0], "heading_deg": 0},
            {"id": 3, "group_id": 201, "controller_id": 8,
             "position_enu": [-8, 117, 0], "heading_deg": 90},
            {"id": 4, "group_id": 202, "controller_id": 8,
             "position_enu": [5, 112, 0], "heading_deg": 0},
        ],
        "signal_plans": [
            {"id": 7, "offset_ms": 0, "groups": [101, 102], "phases": [
                {"duration_ms": 1000, "green_groups": [], "yellow_groups": []},
                {"duration_ms": 2000, "green_groups": [101], "yellow_groups": []},
                {"duration_ms": 1000, "green_groups": [], "yellow_groups": [101]},
                {"duration_ms": 1000, "green_groups": [102], "yellow_groups": []},
                {"duration_ms": 1000, "green_groups": [], "yellow_groups": [102]},
            ]},
            {"id": 8, "offset_ms": 1000, "groups": [201, 202], "phases": [
                {"duration_ms": 1000, "green_groups": [], "yellow_groups": []},
                {"duration_ms": 2000, "green_groups": [201], "yellow_groups": []},
                {"duration_ms": 1000, "green_groups": [], "yellow_groups": [201]},
                {"duration_ms": 1000, "green_groups": [202], "yellow_groups": []},
                {"duration_ms": 1000, "green_groups": [], "yellow_groups": [202]},
            ]},
        ],
    }).encode()


def world_state(expected, elapsed_ns=5 * SECOND_NS, status="active"):
    message = pb.Envelope(schema_version=2, source_id="our-nonce", sequence=1,
                          simulation_time_ns=elapsed_ns, map_package_checksum=expected.map_checksum)
    world = message.world_state
    world.health.status = status
    world.traffic_network_checksum = expected.network_checksum
    ego = world.entities.add(entity_id=1, entity_kind=pb.ENTITY_KIND_EGO_VEHICLE)
    ego.position_enu.z = 0.55
    for identity, (controller, group, position, heading) in sorted(expected.heads.items()):
        aspect, remaining = expected_signal(expected, controller, group, elapsed_ns,
                                            status == "active")
        head = world.traffic_signals.add(signal_id=identity, controller_id=controller,
                                         group_id=group, aspect=aspect,
                                         heading_deg=heading, remaining_seconds=remaining)
        head.position_enu.x, head.position_enu.y, head.position_enu.z = position
    return message


class TrafficSmokeHelpersTest(unittest.IsolatedAsyncioTestCase):
    async def test_foreign_server_receives_no_application_frames(self):
        expected = network_expectations(fixture_bytes())
        connection = FakeConnection(server_hello("foreign-server"))
        controller = TrafficController(connection, "play", "our-nonce", expected)
        with self.assertRaisesRegex(AssertionError, "does not belong"):
            await controller.hello()
        self.assertEqual(connection.sent, [])

    async def test_missing_capability_sends_nothing_and_matching_child_can_negotiate(self):
        expected = network_expectations(fixture_bytes())
        message = server_hello("our-nonce")
        connection = FakeConnection(message)
        with self.assertRaisesRegex(AssertionError, "capability missing"):
            await TrafficController(connection, "play", "our-nonce", expected).hello()
        self.assertEqual(connection.sent, [])
        message.hello.capabilities.append("traffic-signals.v1")
        connection = FakeConnection(message)
        await TrafficController(connection, "play", "our-nonce", expected).hello()
        self.assertEqual(len(connection.sent), 1)
        sent = pb.Envelope.FromString(connection.sent[0])
        self.assertEqual(sent.source_id, "traffic-smoke")
        self.assertIn("traffic-signals.v1", sent.hello.capabilities)

    async def test_new_pie_has_fresh_connection_session_and_reconnect_preserves_play(self):
        expected = network_expectations(fixture_bytes())
        message = server_hello("our-nonce")
        message.hello.capabilities.append("traffic-signals.v1")
        first = TrafficController(FakeConnection(message), "first-pie", "our-nonce", expected)
        second = TrafficController(FakeConnection(message), "second-pie", "our-nonce", expected)
        reconnected = TrafficController(FakeConnection(message), "second-pie", "our-nonce", expected)
        for controller in (first, second, reconnected):
            await controller.hello()
            await controller.reset()
            reset = pb.Envelope.FromString(controller.ws.sent[-1])
            self.assertEqual(reset.simulation_reset.play_session_id, controller.play_id)
            self.assertEqual(reset.session_id, controller.session)
        self.assertIsNot(first.ws, second.ws)
        self.assertEqual(len({first.session, second.session, reconnected.session}), 3)
        self.assertNotEqual(first.play_id, second.play_id)
        self.assertEqual(second.play_id, reconnected.play_id)

    def test_expected_ids_provenance_and_malformed_authoring(self):
        expected = network_expectations(fixture_bytes())
        self.assertEqual(sorted(expected.heads), [1, 2, 3])
        self.assertEqual(expected.format_version, 1)
        self.assertEqual({head[0] for head in expected.heads.values()}, {1})
        self.assertEqual(expected.network_checksum, network_expectations(fixture_bytes()).network_checksum)
        self.assertNotEqual(expected.network_checksum,
                            network_expectations(fixture_bytes() + b"\n").network_checksum)
        malformed = json.loads(fixture_bytes())
        malformed["signals"][2]["id"] = 1
        with self.assertRaisesRegex(AssertionError, "distinct"):
            network_expectations(json.dumps(malformed).encode())
        with self.assertRaisesRegex(AssertionError, "duplicate JSON"):
            network_expectations(fixture_bytes().replace(b'"format_version": 1',
                                                         b'"format_version": 1, "format_version": 1'))
        with self.assertRaisesRegex(AssertionError, "nonfinite"):
            network_expectations(fixture_bytes().replace(b'"heading_deg": 90', b'"heading_deg": NaN'))

    def test_v2_data_driven_offsets_controllers_and_relationships(self):
        expected = network_expectations(fixture_v2_bytes())
        self.assertEqual(expected.format_version, 2)
        self.assertEqual(set(expected.plans), {7, 8})
        self.assertEqual(expected.group_keys, {(7, 101), (7, 102), (8, 201), (8, 202)})
        self.assertEqual(expected_signal(expected, 7, 101, 0, True),
                         (pb.TRAFFIC_SIGNAL_RED, 1.0))
        self.assertEqual(expected_signal(expected, 8, 201, 0, True),
                         (pb.TRAFFIC_SIGNAL_GREEN, 2.0))
        self.assertEqual(expected_signal(expected, 7, 101, SECOND_NS - 1, True)[0],
                         pb.TRAFFIC_SIGNAL_RED)
        self.assertEqual(expected_signal(expected, 7, 101, SECOND_NS, True),
                         (pb.TRAFFIC_SIGNAL_GREEN, 2.0))
        self.assertEqual(expected_signal(expected, 8, 201, 2 * SECOND_NS, False),
                         (pb.TRAFFIC_SIGNAL_RED, 0.0))
        validate_world(world_state(expected, elapsed_ns=SECOND_NS), expected)

        malformed = json.loads(fixture_v2_bytes())
        malformed["signals"][0]["controller_id"] = 8
        with self.assertRaisesRegex(AssertionError, "does not own"):
            network_expectations(json.dumps(malformed).encode())
        malformed = json.loads(fixture_v2_bytes())
        malformed["signal_plans"][1]["groups"][0] = 101
        with self.assertRaisesRegex(AssertionError, "one controller"):
            network_expectations(json.dumps(malformed).encode())

    def test_exact_phase_boundaries_large_time_and_disabled(self):
        phases = [(0, pb.TRAFFIC_SIGNAL_RED, pb.TRAFFIC_SIGNAL_RED),
                  (2, pb.TRAFFIC_SIGNAL_GREEN, pb.TRAFFIC_SIGNAL_RED),
                  (14, pb.TRAFFIC_SIGNAL_YELLOW, pb.TRAFFIC_SIGNAL_RED),
                  (17, pb.TRAFFIC_SIGNAL_RED, pb.TRAFFIC_SIGNAL_RED),
                  (19, pb.TRAFFIC_SIGNAL_RED, pb.TRAFFIC_SIGNAL_GREEN),
                  (27, pb.TRAFFIC_SIGNAL_RED, pb.TRAFFIC_SIGNAL_YELLOW),
                  (30, pb.TRAFFIC_SIGNAL_RED, pb.TRAFFIC_SIGNAL_RED)]
        for seconds, first, second in phases:
            self.assertEqual(expected_phase(1, seconds * SECOND_NS, True)[0], first)
            self.assertEqual(expected_phase(2, seconds * SECOND_NS, True)[0], second)
        self.assertEqual(expected_phase(1, 2 * SECOND_NS - 1, True)[0], pb.TRAFFIC_SIGNAL_RED)
        self.assertEqual(expected_phase(2, 30 * SECOND_NS - 1, True)[0], pb.TRAFFIC_SIGNAL_YELLOW)
        for group in (1, 2):
            huge = (1 << 64) - 1
            self.assertEqual(expected_phase(group, huge, True), expected_phase(group, huge % CYCLE_NS, True))
            self.assertEqual(expected_phase(group, 5 * SECOND_NS, False), (pb.TRAFFIC_SIGNAL_RED, 0))

    def test_world_snapshot_atomic_identity_pose_and_safe_stop(self):
        expected = network_expectations(fixture_bytes())
        validate_world(world_state(expected), expected)
        validate_world(world_state(expected, status="safe_stop"), expected)
        for mutate in (
                lambda state: state.world_state.ClearField("health"),
                lambda state: state.world_state.ClearField("entities"),
                lambda state: setattr(state.world_state.traffic_signals[0], "signal_id", 99),
                lambda state: setattr(state.world_state.traffic_signals[2], "aspect", pb.TRAFFIC_SIGNAL_GREEN),
                lambda state: setattr(state.world_state.traffic_signals[0], "remaining_seconds", float("nan")),
                lambda state: setattr(state.world_state.traffic_signals[0].position_enu, "z", 5),
                lambda state: setattr(state.world_state, "traffic_network_checksum", "foreign-checksum"),
                lambda state: setattr(state.world_state.health, "status", "safe_stop")):
            state = world_state(expected)
            mutate(state)
            with self.assertRaises(AssertionError):
                validate_world(state, expected)


if __name__ == "__main__":
    unittest.main()
