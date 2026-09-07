"""Child ownership, startup retry, and failure cleanup without launching hosts."""

import asyncio
import subprocess
import unittest
from unittest.mock import AsyncMock, Mock, call, patch

import smoke_host


class ChildHostTest(unittest.TestCase):
    def test_running_child_is_stopped_on_success_failure_and_interruption(self):
        for failure in (None, AssertionError("acceptance failed"), KeyboardInterrupt()):
            with self.subTest(failure=failure):
                process = Mock()
                process.poll.return_value = None
                stdout, stderr = object(), object()
                with patch.object(smoke_host.subprocess, "Popen", return_value=process) as launch:
                    try:
                        with smoke_host.child_host(
                                ["owned-host", "--ws-port", "49152"],
                                cwd="workspace", stdout=stdout, stderr=stderr) as child:
                            self.assertIs(child, process)
                            if failure is not None:
                                raise failure
                    except BaseException as error:
                        self.assertIs(error, failure)
                    else:
                        self.assertIsNone(failure)

                launch.assert_called_once_with(
                    ["owned-host", "--ws-port", "49152"], cwd="workspace",
                    stdout=stdout, stderr=stderr,
                    creationflags=(subprocess.CREATE_NO_WINDOW
                                   if smoke_host.os.name == "nt" else 0))
                process.terminate.assert_called_once_with()
                process.wait.assert_called_once_with(timeout=5)
                process.kill.assert_not_called()

    def test_finished_child_is_never_stopped_again(self):
        for exit_code in (0, 1):
            with self.subTest(exit_code=exit_code):
                process = Mock()
                process.poll.return_value = exit_code
                with patch.object(smoke_host.subprocess, "Popen", return_value=process):
                    with smoke_host.child_host(["owned-host"], cwd=".", stdout=None, stderr=None):
                        pass
                process.terminate.assert_not_called()
                process.kill.assert_not_called()
                process.wait.assert_not_called()

    def test_unresponsive_child_is_killed_and_reaped_with_bounded_waits(self):
        process = Mock()
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired("owned-host", 5), 0]
        with patch.object(smoke_host.subprocess, "Popen", return_value=process):
            with smoke_host.child_host(["owned-host"], cwd=".", stdout=None, stderr=None):
                pass
        self.assertEqual(process.method_calls, [
            call.poll(), call.terminate(), call.wait(timeout=5),
            call.kill(), call.wait(timeout=5),
        ])

    def test_launch_failure_propagates_without_attempting_cleanup(self):
        with patch.object(smoke_host.subprocess, "Popen", side_effect=OSError("launch failed")):
            with self.assertRaisesRegex(OSError, "launch failed"):
                with smoke_host.child_host(["owned-host"], cwd=".", stdout=None, stderr=None):
                    self.fail("failed launch entered the child context")


class ConnectChildTest(unittest.IsolatedAsyncioTestCase):
    async def test_startup_retries_transport_failures_without_sending_frames(self):
        process = Mock()
        process.poll.return_value = None
        connection = AsyncMock()
        connect = AsyncMock(side_effect=[OSError("not listening"), TimeoutError(), connection])
        with patch.object(smoke_host.websockets, "connect", connect), \
                patch.object(smoke_host.asyncio, "sleep", new_callable=AsyncMock) as sleep:
            self.assertIs(await smoke_host.connect_child("ws://127.0.0.1:49152", process), connection)
        self.assertEqual(process.poll.call_count, 3)
        self.assertEqual(connect.await_args_list, [
            call("ws://127.0.0.1:49152", open_timeout=1, close_timeout=1, max_queue=256),
        ] * 3)
        self.assertEqual(sleep.await_args_list, [call(0.1), call(0.1)])
        connection.send.assert_not_awaited()

    async def test_exited_child_prevents_connection_attempt(self):
        process = Mock()
        process.poll.return_value = 1
        with patch.object(smoke_host.websockets, "connect", new_callable=AsyncMock) as connect:
            with self.assertRaisesRegex(AssertionError, "exited during startup"):
                await smoke_host.connect_child("ws://127.0.0.1:49152", process)
        connect.assert_not_awaited()

    async def test_retry_deadline_propagates_the_last_transport_error(self):
        process = Mock()
        process.poll.return_value = None
        with patch.object(smoke_host.websockets, "connect", AsyncMock(side_effect=OSError("deadline"))), \
                patch.object(smoke_host.time, "monotonic", side_effect=[100, 110]), \
                patch.object(smoke_host.asyncio, "sleep", new_callable=AsyncMock) as sleep:
            with self.assertRaisesRegex(OSError, "deadline"):
                await smoke_host.connect_child("ws://127.0.0.1:49152", process)
        sleep.assert_not_awaited()

    async def test_cancellation_and_protocol_failure_are_not_retried(self):
        process = Mock()
        process.poll.return_value = None
        for failure in (asyncio.CancelledError(), ValueError("invalid handshake")):
            with self.subTest(failure=failure):
                with patch.object(smoke_host.websockets, "connect", AsyncMock(side_effect=failure)), \
                        patch.object(smoke_host.asyncio, "sleep", new_callable=AsyncMock) as sleep:
                    with self.assertRaises(type(failure)):
                        await smoke_host.connect_child("ws://127.0.0.1:49152", process)
                sleep.assert_not_awaited()

    async def test_health_connection_keeps_its_existing_close_timeout(self):
        process = Mock()
        process.poll.return_value = None
        with patch.object(smoke_host.websockets, "connect", new_callable=AsyncMock) as connect:
            await smoke_host.connect_child("ws://127.0.0.1:49152", process, close_timeout=10)
        self.assertEqual(connect.await_args.kwargs["close_timeout"], 10)


if __name__ == "__main__":
    unittest.main()
