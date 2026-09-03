"""Read-only checks for the smoke runner's pre-mutation isolation fence."""

import unittest

from smoke_health import Controller, pb


class FakeConnection:
    def __init__(self, message):
        self.message = message.SerializeToString()
        self.sent = []

    async def recv(self):
        return self.message

    async def send(self, data):
        self.sent.append(data)


def server_hello(source):
    message = pb.Envelope(schema_version=2, source_id=source,
                          map_package_checksum="fnv1a64:0123456789abcdef")
    message.hello.schema = "simcore-envelope-v2"
    message.hello.capabilities.append("world-health.v1")
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

    async def test_state_from_other_source_is_rejected(self):
        message = pb.Envelope(schema_version=2, source_id="another-server", sequence=1)
        message.world_state.health.status = "active"
        connection = FakeConnection(message)
        controller = Controller(connection, "test-play", "our-random-server-nonce")
        with self.assertRaisesRegex(AssertionError, "unexpected server"):
            await controller.receive_state()
        self.assertEqual(connection.sent, [])


if __name__ == "__main__":
    unittest.main()
