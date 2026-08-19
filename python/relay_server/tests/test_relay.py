import asyncio
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

import zmq

from python.relay_server import __main__ as relay_entrypoint
from python.relay_server import main as relay_main
from python.relay_server.config import Settings
from python.relay_server.generated.vehicle_pb2 import EntityStatePacket
from python.relay_server.src import database, zmq_subscriber
from python.relay_server.src.websocket_manager import WebSocketManager
from python.relay_server.src.zmq_subscriber import (
    SubscriberStatus,
    _decode_packet,
    _deliver_packet,
    _state_to_dict,
)


class PackageImportTest(unittest.TestCase):
    def test_package_import_does_not_depend_on_working_directory(self) -> None:
        repository_dir = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..", "..")
        )
        environment = os.environ.copy()
        existing_path = environment.get("PYTHONPATH")
        environment["PYTHONPATH"] = (
            repository_dir
            if not existing_path
            else repository_dir + os.pathsep + existing_path
        )

        with tempfile.TemporaryDirectory() as working_dir:
            result = subprocess.run(
                [
                    sys.executable,
                    "-B",
                    "-c",
                    "from python.relay_server.main import app; print(app.title)",
                ],
                cwd=working_dir,
                env=environment,
                check=True,
                capture_output=True,
                text=True,
            )
        self.assertEqual("SimCore Relay Server", result.stdout.strip())

    def test_module_entrypoint_uses_validated_server_settings(self) -> None:
        with patch.object(relay_entrypoint.uvicorn, "run") as uvicorn_run:
            relay_entrypoint.main()

        uvicorn_run.assert_called_once_with(
            relay_entrypoint.app,
            host=relay_entrypoint.settings.host,
            port=relay_entrypoint.settings.port,
        )


class ProtocolAdapterTest(unittest.TestCase):
    def test_wheel_contact_vectors_are_in_debug_dto(self) -> None:
        packet = EntityStatePacket()
        state = packet.entities.add()
        wheel = state.wheels.add()
        wheel.contact_point_enu.x = 1.0
        wheel.contact_point_enu.y = 2.0
        wheel.contact_point_enu.z = 3.0
        wheel.contact_normal_enu.x = 0.0
        wheel.contact_normal_enu.y = 0.0
        wheel.contact_normal_enu.z = 1.0

        value = _state_to_dict(state)["wheels"][0]

        self.assertEqual(
            {"x": 1.0, "y": 2.0, "z": 3.0},
            value["contact_point_enu"],
        )
        self.assertEqual(
            {"x": 0.0, "y": 0.0, "z": 1.0},
            value["contact_normal_enu"],
        )

    def test_malformed_protobuf_is_dropped_without_raising(self) -> None:
        status = SubscriberStatus()

        packet = _decode_packet(b"\x0a\xff", status)

        self.assertIsNone(packet)
        self.assertEqual(1, status.decode_errors)


class WebSocketManagerTest(unittest.IsolatedAsyncioTestCase):
    async def test_slow_client_times_out_and_is_removed(self) -> None:
        class SlowWebSocket:
            def __init__(self) -> None:
                self.close_code = None
                self.close_reason = None

            async def accept(self) -> None:
                return None

            async def send_text(self, _message: str) -> None:
                await asyncio.Event().wait()

            async def close(self, code: int, reason: str) -> None:
                self.close_code = code
                self.close_reason = reason

        websocket = SlowWebSocket()
        manager = WebSocketManager(send_timeout_seconds=0.01)
        await manager.connect(websocket)

        failed = await manager.broadcast("state")

        self.assertEqual(1, failed)
        self.assertEqual(0, manager.connection_count)
        self.assertEqual(1013, websocket.close_code)
        self.assertEqual("relay send failed", websocket.close_reason)


class SubscriberDeliveryTest(unittest.IsolatedAsyncioTestCase):
    async def test_websocket_and_database_failures_are_isolated(self) -> None:
        packet = EntityStatePacket()
        packet.entities.add().entity_id = 1
        status = SubscriberStatus()
        websocket_manager = AsyncMock()
        websocket_manager.broadcast.side_effect = RuntimeError("websocket down")
        database_writer = AsyncMock(side_effect=RuntimeError("database down"))
        relay_settings = Settings(
            _env_file=None,
            db_enabled=True,
            db_error_backoff_seconds=10.0,
        )

        with self.assertLogs(
            "python.relay_server.src.zmq_subscriber", level="ERROR"
        ):
            await _deliver_packet(
                packet,
                status,
                websocket_manager,
                relay_settings,
                database_writer,
            )

        self.assertEqual(1, status.websocket_errors)
        self.assertEqual(1, status.database_errors)
        database_writer.assert_awaited_once()


class SubscriberRuntimeTest(unittest.IsolatedAsyncioTestCase):
    async def test_socket_options_and_cleanup_are_applied(self) -> None:
        class FakeSocket:
            def __init__(self) -> None:
                self.options = []
                self.string_options = []
                self.endpoint = None
                self.closed_with_linger = None

            def setsockopt(self, option, value) -> None:
                self.options.append((option, value))

            def setsockopt_string(self, option, value) -> None:
                self.string_options.append((option, value))

            def connect(self, endpoint) -> None:
                self.endpoint = endpoint

            async def recv(self) -> bytes:
                await asyncio.Event().wait()

            def close(self, linger=None) -> None:
                self.closed_with_linger = linger

        class FakeContext:
            def __init__(self, socket) -> None:
                self._socket = socket
                self.terminated = False

            def socket(self, _socket_type):
                return self._socket

            def term(self) -> None:
                self.terminated = True

        socket = FakeSocket()
        context = FakeContext(socket)
        status = SubscriberStatus()
        relay_settings = Settings(
            _env_file=None,
            zmq_receive_hwm=3,
            zmq_conflate=True,
            zmq_linger_ms=7,
            db_enabled=False,
        )

        with patch.object(zmq_subscriber.zmq.asyncio, "Context", return_value=context):
            task = asyncio.create_task(
                zmq_subscriber.zmq_subscriber_task(
                    status,
                    WebSocketManager(),
                    relay_settings,
                )
            )
            for _ in range(10):
                if status.socket_ready:
                    break
                await asyncio.sleep(0)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task

        self.assertIn((zmq.RCVHWM, 3), socket.options)
        self.assertIn((zmq.CONFLATE, 1), socket.options)
        self.assertIn((zmq.LINGER, 7), socket.options)
        self.assertIn((zmq.SUBSCRIBE, ""), socket.string_options)
        self.assertEqual("tcp://127.0.0.1:5555", socket.endpoint)
        self.assertEqual(7, socket.closed_with_linger)
        self.assertTrue(context.terminated)
        self.assertFalse(status.running)
        self.assertFalse(status.socket_ready)

    async def test_slow_database_write_times_out_without_blocking_forever(self) -> None:
        packet = EntityStatePacket()
        packet.entities.add().entity_id = 1
        status = SubscriberStatus()
        websocket_manager = AsyncMock()
        websocket_manager.broadcast.return_value = 0

        async def slow_database_writer(_states) -> None:
            await asyncio.Event().wait()

        relay_settings = Settings(
            _env_file=None,
            db_enabled=True,
            db_write_timeout_seconds=0.01,
        )

        with self.assertLogs(
            "python.relay_server.src.zmq_subscriber", level="ERROR"
        ):
            await asyncio.wait_for(
                _deliver_packet(
                    packet,
                    status,
                    websocket_manager,
                    relay_settings,
                    slow_database_writer,
                ),
                timeout=0.1,
            )

        self.assertEqual(1, status.database_errors)


class LifespanTest(unittest.IsolatedAsyncioTestCase):
    async def test_lifespan_cancels_and_awaits_subscriber(self) -> None:
        started = asyncio.Event()
        finalized = asyncio.Event()

        async def subscriber(status, _manager, _settings) -> None:
            status.running = True
            status.socket_ready = True
            started.set()
            try:
                await asyncio.Event().wait()
            finally:
                status.running = False
                status.socket_ready = False
                finalized.set()

        with (
            patch.object(relay_main.settings, "db_enabled", False),
            patch.object(relay_main, "zmq_subscriber_task", subscriber),
        ):
            async with relay_main.lifespan(relay_main.app):
                await asyncio.wait_for(started.wait(), timeout=1.0)
                health = await relay_main.health()
                self.assertEqual("running", health["status"])

        self.assertTrue(finalized.is_set())
        self.assertIsNone(relay_main.app.state.subscriber_task)

    async def test_database_closes_even_if_subscriber_task_failed(self) -> None:
        async def failed_subscriber(_status, _manager, _settings) -> None:
            raise RuntimeError("subscriber failed")

        init_db = AsyncMock()
        close_db = AsyncMock()
        with (
            patch.object(relay_main.settings, "db_enabled", True),
            patch.object(relay_main, "init_db", init_db),
            patch.object(relay_main, "close_db", close_db),
            patch.object(relay_main, "zmq_subscriber_task", failed_subscriber),
        ):
            with self.assertRaisesRegex(RuntimeError, "subscriber failed"):
                async with relay_main.lifespan(relay_main.app):
                    await asyncio.sleep(0)

        init_db.assert_awaited_once()
        close_db.assert_awaited_once()
        self.assertIsNone(relay_main.app.state.subscriber_task)

    async def test_websocket_endpoint_always_disconnects(self) -> None:
        websocket = AsyncMock()
        websocket.receive_text.side_effect = RuntimeError("receive failed")
        manager = AsyncMock()

        with patch.object(relay_main, "manager", manager):
            with self.assertRaisesRegex(RuntimeError, "receive failed"):
                await relay_main.websocket_endpoint(websocket)

        manager.connect.assert_awaited_once_with(websocket)
        manager.disconnect.assert_awaited_once_with(websocket)


class DatabasePoolTest(unittest.IsolatedAsyncioTestCase):
    async def asyncTearDown(self) -> None:
        database._pool = None

    async def test_uninitialized_pool_is_rejected(self) -> None:
        database._pool = None
        with self.assertRaisesRegex(RuntimeError, "not initialized"):
            database._require_pool()

    async def test_close_resets_pool(self) -> None:
        pool = AsyncMock()
        database._pool = pool

        await database.close_db()

        self.assertIsNone(database._pool)
        pool.close.assert_awaited_once()


if __name__ == "__main__":
    unittest.main()
