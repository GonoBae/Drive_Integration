"""Read-only checks for the smoke runner's pre-mutation isolation fence."""

import unittest

from smoke_health import Controller, pb
from runtime_vehicle_catalog import catalog_capability


class FakeConnection:
    def __init__(self, message):
        self.message = message.SerializeToString()
        self.sent = []

    async def recv(self):
        return self.message

    async def send(self, data):
        self.sent.append(data)


def server_hello(source, vehicle_catalog=None):
    message = pb.Envelope(schema_version=2, source_id=source,
                          map_package_checksum="fnv1a64:0123456789abcdef")
    message.hello.schema = "simcore-envelope-v2"
    message.hello.capabilities.append("world-health.v1")
    message.hello.capabilities.append(
        catalog_capability() if vehicle_catalog is None else vehicle_catalog)
    return message


class SmokeIsolationTest(unittest.IsolatedAsyncioTestCase):
    async def test_foreign_server_never_receives_application_frames(self):
        connection = FakeConnection(server_hello("another-perfectly-compatible-server"))
        controller = Controller(connection, "test-play", "our-random-server-nonce")
        with self.assertRaisesRegex(AssertionError, "does not belong"):
            await controller.hello()
        self.assertEqual(connection.sent, [])

    async def test_matching_child_identity_can_negotiate(self):
        connection = FakeConnection(server_hello("our-random-server-nonce"))
        controller = Controller(connection, "test-play", "our-random-server-nonce")
        await controller.hello()
        self.assertEqual(len(connection.sent), 1)
        self.assertEqual(pb.Envelope.FromString(connection.sent[0]).WhichOneof("payload"), "hello")
        self.assertIn(catalog_capability(), pb.Envelope.FromString(connection.sent[0]).hello.capabilities)

    async def test_mismatched_vehicle_catalog_sends_no_application_frames(self):
        connection = FakeConnection(server_hello("our-random-server-nonce", "vehicle-catalog-fnv1a64-mismatch"))
        controller = Controller(connection, "test-play", "our-random-server-nonce")
        with self.assertRaisesRegex(AssertionError, "vehicle catalog differs"):
            await controller.hello()
        self.assertEqual(connection.sent, [])

    async def test_state_from_other_source_is_rejected(self):
        message = pb.Envelope(schema_version=2, source_id="another-server", sequence=1)
        message.world_state.health.status = "active"
        connection = FakeConnection(message)
        controller = Controller(connection, "test-play", "our-random-server-nonce")
        with self.assertRaisesRegex(AssertionError, "unexpected server"):
            await controller.receive_state()
        self.assertEqual(connection.sent, [])

    async def test_loadout_reset_preserves_play_and_selection(self):
        connection = FakeConnection(server_hello("our-random-server-nonce"))
        controller = Controller(connection, "garage-play", "our-random-server-nonce")
        await controller.hello()
        self.assertIn("vehicle-loadout.v1",
                      pb.Envelope.FromString(connection.sent[0]).hello.capabilities)
        self.assertIn("vehicle-parts.v1",
                      pb.Envelope.FromString(connection.sent[0]).hello.capabilities)
        await controller.reset("sedan_modular_comfort")
        reset = pb.Envelope.FromString(connection.sent[-1]).simulation_reset
        self.assertEqual(reset.play_session_id, "garage-play")
        self.assertEqual(reset.requested_loadout_id, "sedan_modular_comfort")
        await controller.reset()
        self.assertEqual(pb.Envelope.FromString(connection.sent[-1])
                         .simulation_reset.requested_loadout_id, "")


if __name__ == "__main__":
    unittest.main()
