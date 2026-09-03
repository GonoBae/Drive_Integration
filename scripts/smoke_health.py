"""Exercise server-authoritative Health over a real, isolated WebSocket.

Requires the built Release host, Python protobuf and websockets. Starts its own
localhost server on a free port, never connects to/stops the user's live server,
and terminates only the child process it created. No map files are modified.
"""

import argparse
import asyncio
from datetime import datetime
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import uuid

import websockets
from websockets.exceptions import ConnectionClosed

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python" / "relay_server" / "generated"))
import vehicle_pb2 as pb  # noqa: E402


def require(condition, message):
    if not condition:
        raise AssertionError(message)


class Controller:
    def __init__(self, socket_connection, play_id, expected_source):
        self.ws = socket_connection
        self.play_id = play_id
        self.expected_source = expected_source
        self.session = "health-" + uuid.uuid4().hex
        self.sequence = 0
        self.checksum = ""
        self.last_world_sequence = 0
        self.last_control_sent_at = 0

    def envelope(self):
        self.sequence += 1
        return pb.Envelope(
            schema_version=2,
            sequence=self.sequence,
            source_id="health-smoke",
            session_id=self.session,
            map_package_checksum=self.checksum,
        )

    async def hello(self):
        data = await asyncio.wait_for(self.ws.recv(), 3)
        server = pb.Envelope.FromString(data)
        require(server.WhichOneof("payload") == "hello", "first frame must be Hello")
        # Verify the per-run nonce before sending Hello, Reset or EStop. Even
        # if another server wins the free-port race, it receives no app frames.
        require(server.source_id == self.expected_source, "port does not belong to our isolated host")
        require(server.schema_version == 2, "schema mismatch")
        require("world-health.v1" in server.hello.capabilities, "Health capability missing")
        self.checksum = server.map_package_checksum
        request = self.envelope()
        request.hello.build = "health-smoke-v1"
        request.hello.schema = server.hello.schema
        request.hello.capabilities.extend([
            "world-state.v2", "control.v2", "simulation-reset.v1",
            "map-package-checksum.v1", "world-health.v1",
        ])
        await self.ws.send(request.SerializeToString())

    async def reset(self):
        request = self.envelope()
        request.simulation_reset.play_session_id = self.play_id
        request.simulation_reset.client_time_ns = time.monotonic_ns()
        await self.ws.send(request.SerializeToString())

    async def control(self, estop=False):
        request = self.envelope()
        request.control_command.mode = pb.CONTROL_MODE_ESTOP if estop else pb.CONTROL_MODE_MANUAL
        request.control_command.estop = estop
        request.control_command.brake = 1
        request.control_command.gear = pb.VEHICLE_GEAR_NEUTRAL
        request.control_command.client_time_ns = time.monotonic_ns()
        self.last_control_sent_at = time.monotonic()
        await self.ws.send(request.SerializeToString())

    async def receive_state(self):
        message = pb.Envelope.FromString(await self.ws.recv())
        require(message.WhichOneof("payload") == "world_state", "Health must travel inside WorldState")
        require(message.schema_version == 2, "state schema mismatch")
        require(message.source_id == self.expected_source, "state from unexpected server")
        require(message.map_package_checksum == self.checksum, "state checksum mismatch")
        require(message.sequence > self.last_world_sequence, "state sequence regression")
        self.last_world_sequence = message.sequence
        require(message.world_state.HasField("health"), "state omitted Health")
        return message

    async def wait_status(self, status, timeout=3):
        async def receive():
            while True:
                envelope = await self.receive_state()
                if envelope.world_state.health.status == status:
                    if status != "awaiting_reset":
                        require(envelope.play_session_id == self.play_id, "wrong play identity")
                    return envelope.world_state.health
        health = await asyncio.wait_for(receive(), timeout)
        print(f"PASS {status}: has_command={health.has_control_command} "
              f"age_ms={health.last_command_age_ns / 1e6:.1f} "
              f"overruns={health.tick_overrun_count}")
        return health


async def exercise(url, process, expected_source):
    deadline = time.monotonic() + 10
    while True:
        require(process.poll() is None, "isolated host exited during startup; inspect its logs")
        try:
            connection = await websockets.connect(url, open_timeout=1, max_queue=256)
            break
        except (OSError, TimeoutError):
            if time.monotonic() >= deadline:
                raise
            await asyncio.sleep(0.1)

    play_id = "health-play-" + uuid.uuid4().hex
    try:
        controller = Controller(connection, play_id, expected_source)
        await controller.hello()
        health = await controller.wait_status("awaiting_reset")
        require(not health.has_control_command and health.last_command_age_ns == 0,
                "no command must not have a fabricated age")
        await controller.reset()
        health = await controller.wait_status("awaiting_control")
        require(not health.has_control_command, "reset must clear command age")
        await controller.control()
        health = await controller.wait_status("active")
        require(health.has_control_command, "active must report accepted command")
        health = await controller.wait_status("safe_stop")
        require(health.last_command_age_ns > 250_000_000, "soft timeout fired prematurely")
        # A current timestamp, not a queued packet, recovers within the same session.
        await controller.control()
        await controller.wait_status("active")
        await controller.wait_status("safe_stop")

        async def await_hard_close():
            try:
                while True:
                    await controller.receive_state()
            except ConnectionClosed as error:
                require(error.rcvd is not None and error.rcvd.code == 1008,
                        "hard timeout must fence the connection with 1008")
                require(error.rcvd.reason == "control lease hard timeout; reconnect required",
                        "unexpected reason for policy close")
                require(time.monotonic() - controller.last_control_sent_at >= 0.95,
                        "hard timeout occurred before the 1000ms deadline (50ms tolerance)")
                print("PASS hard timeout: connection closed 1008")
        # The reconnect_required snapshot need not reach the controller that is
        # being closed; host unit tests check it before reconnect resets state.
        await asyncio.wait_for(await_hard_close(), 3)
    finally:
        await connection.close()

    async with websockets.connect(url, max_queue=256) as connection:
        controller = Controller(connection, play_id, expected_source)
        await controller.hello()
        await controller.reset()  # same PIE identity, new connection session
        health = await controller.wait_status("awaiting_control")
        require(not health.has_control_command, "reconnect must clear retired command metrics")
        await controller.control()
        await controller.wait_status("active")
        await controller.control(estop=True)
        await controller.wait_status("estop_latched")
        await controller.control()
        # Include newly produced frames, not just frames queued before input.
        until = time.monotonic() + 0.15
        while time.monotonic() < until:
            state = await asyncio.wait_for(controller.receive_state(), 2)
            require(state.world_state.health.status == "estop_latched", "EStop was cleared")
        print("PASS EStop remains latched after normal input")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map-package", type=Path, default=ROOT / "map_packages/landscape_local_v1")
    parser.add_argument("--host", type=Path, default=ROOT / "cpp/host/build/Release/simcore_publisher.exe")
    args = parser.parse_args()
    require(args.host.is_file(), f"build the host first: {args.host}")
    require((args.map_package / "manifest.cfg").is_file(), "map package manifest missing")
    # The launch race is fenced by a unique server identity checked before any
    # application message. A bind failure also makes our child exit.
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    log_dir = ROOT / "runtime_logs"
    log_dir.mkdir(exist_ok=True)
    stem = "health-smoke-" + datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    expected_source = "health-host-" + uuid.uuid4().hex
    command = [str(args.host.resolve()), "--map-package", str(args.map_package.resolve()),
               "--vehicle-config", str(ROOT / "cpp/host/config/vehicle_sedan.cfg"),
               "--ws-port", str(port), "--command-timeout-ms", "250",
               "--hard-command-timeout-ms", "1000", "--source-id", expected_source]
    with (log_dir / (stem + ".stdout.log")).open("wb") as stdout, \
            (log_dir / (stem + ".stderr.log")).open("wb") as stderr:
        process = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        print(f"Isolated server PID={process.pid} port={port}; logs={log_dir / stem}")
        try:
            asyncio.run(exercise(f"ws://127.0.0.1:{port}", process, expected_source))
            print("PASS Health WebSocket smoke (isolated child only)")
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
