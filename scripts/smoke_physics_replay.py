"""Record actual WebSocket driving, then offline re-simulate every Ego physics tick.

Starts only a nonce-verified child on an ephemeral loopback port. Its bounded
--record-ticks recording closes normally; existing hosts/maps/logs are untouched.
Recorded NPC/pedestrian collision inputs are reused during offline replay.
"""

import argparse
import asyncio
from contextlib import suppress
from datetime import datetime
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time
import uuid

from websockets.exceptions import ConnectionClosed

from smoke_health import Controller, ROOT, pb, require
from smoke_host import child_host, connect_child
from smoke_signal_city import build_spec


class ReplayController(Controller):
    async def drive(self, throttle=0.65, steering=0.07,
                    gear=pb.VEHICLE_GEAR_DRIVE, handbrake=False, estop=False):
        request = self.envelope()
        command = request.control_command
        command.mode = pb.CONTROL_MODE_ESTOP if estop else pb.CONTROL_MODE_MANUAL
        command.estop = estop
        command.throttle = throttle
        command.steering = steering
        command.gear = gear
        command.handbrake = handbrake
        command.client_time_ns = time.monotonic_ns()
        await self.ws.send(request.SerializeToString())


async def drive_for(controller, seconds, **input_values):
    deadline = time.monotonic() + seconds
    maximum_speed = 0.0
    frames = 0
    while time.monotonic() < deadline:
        # Include input changes below the human-readable 0.001 logging threshold.
        command = dict(input_values)
        command["throttle"] = command.get("throttle", 0.65) + (0.0004 if frames % 2 else 0)
        await controller.drive(**command)
        message = await asyncio.wait_for(controller.receive_state(), 1)
        entities = {entity.entity_id: entity for entity in message.world_state.entities}
        require(set(entities) == {1, *range(1001, 1011), *range(2001, 2009)},
                "recording must exercise Ego + 10 NPCs + 8 pedestrians")
        maximum_speed = max(maximum_speed, abs(entities[1].speed))
        frames += 1
        await asyncio.sleep(0.01)
    return maximum_speed, frames


async def open_controller(url, process, source, play):
    connection = await connect_child(url, process)
    controller = ReplayController(connection, play, source)
    await controller.hello()  # Checks per-run server nonce before any writes.
    await controller.reset()
    return controller


async def exercise(url, process, source):
    play = "replay-play-" + uuid.uuid4().hex
    controller = await open_controller(url, process, source, play)
    frames = 0
    try:
        speed, observed = await drive_for(controller, 1.2)
        frames += observed
        require(speed > 0.2, "recording has no actual driving motion")
        await controller.wait_status("safe_stop", timeout=2)
    finally:
        await controller.ws.close()
    # Same PIE, new connection: preserve physics while clearing the control lease.
    controller = await open_controller(url, process, source, play)
    try:
        _, observed = await drive_for(controller, 0.8, steering=-0.11)
        frames += observed
        _, observed = await drive_for(controller, 0.35, handbrake=True)
        frames += observed
    finally:
        await controller.ws.close()
    # New PIE must emit a real physics RESET, not a visual pose replacement.
    controller = await open_controller(url, process, source, "replay-play-" + uuid.uuid4().hex)
    try:
        _, observed = await drive_for(controller, 0.6, gear=pb.VEHICLE_GEAR_REVERSE,
                                      steering=-0.09)
        frames += observed
        await controller.drive(throttle=0, estop=True)
        await controller.wait_status("estop_latched")
        # Drain until the bounded child finalizes its log and exits normally.
        with suppress(ConnectionClosed):
            while process.poll() is None:
                await asyncio.wait_for(controller.receive_state(), 2)
    finally:
        await controller.ws.close()
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
                        default=ROOT / "cpp/host/build/Release/simcore_publisher.exe")
    parser.add_argument("--runtime-config", type=Path,
                        default=ROOT / "cpp/host/config/signal_city_server.cfg")
    args = parser.parse_args()
    require(args.host.is_file(), "build the current Release host first")
    spec = build_spec(args.runtime_config)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    require(port != 9000, "refusing the user's standard port")
    source = "replay-host-" + uuid.uuid4().hex
    output = ROOT / "runtime_logs"
    output.mkdir(exist_ok=True)
    stem = "physics-replay-" + datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    recording = output / (stem + ".replay")
    base = [str(args.host.resolve()), "--runtime-config", str(spec.runtime_config)]
    capture = base + ["--ws-port", str(port), "--source-id", source,
                      "--record-physics", str(recording), "--record-ticks", "480"]
    with (output / (stem + ".stdout.log")).open("xb") as stdout, \
            (output / (stem + ".stderr.log")).open("xb") as stderr, \
            child_host(capture, cwd=ROOT, stdout=stdout, stderr=stderr) as process:
        print(f"Isolated replay capture PID={process.pid}, port={port}; recording={recording}", flush=True)
        observed = asyncio.run(asyncio.wait_for(
            exercise(f"ws://127.0.0.1:{port}", process, source), 25))
        require(process.wait(timeout=5) == 0, "bounded recorder did not exit successfully")
    verify = subprocess.run(base + ["--verify-physics-replay", str(recording)],
                            cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=30,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    (output / (stem + ".verify.log")).write_text(verify.stdout + verify.stderr, encoding="utf-8")
    require(verify.returncode == 0, "offline replay failed: " + verify.stdout + verify.stderr)
    match = re.search(r"\[Replay\] PASS frames=(\d+) events=(\d+) resets=(\d+) "
                      r"dynamic_proxy_frames=(\d+) maximum_position_error_m=(\S+) "
                      r"maximum_yaw_error_deg=(\S+)", verify.stdout)
    require(match is not None, "missing strict offline verification report")
    frames, events, resets, proxy_frames = map(int, match.groups()[:4])
    require(frames == 480 and resets == 2 and proxy_frames == frames and events >= 5,
            "recording did not cover driving/reset/reconnect/safety/dynamic proxies")
    event_names = {line.split()[2] for line in recording.read_text().splitlines()
                   if line.startswith("EVENT ")}
    require({"RESET", "RECONNECT", "SAFE_STOP", "ESTOP"}.issubset(event_names),
            "required actual lifecycle events missing")
    summary = {"status": "pass", "scope": "ego_physics_with_recorded_dynamic_collision_inputs",
               "frames": frames, "events": events, "resets": resets,
               "dynamic_proxy_frames": proxy_frames, "observed_wire_frames": observed,
               "maximum_position_error_m": float(match[5]),
               "maximum_yaw_error_deg": float(match[6]), "recording": str(recording)}
    with (output / (stem + ".summary.json")).open("x", encoding="utf-8") as report:
        json.dump(summary, report, indent=2)
        report.write("\n")
    print(match[0])
    print("PASS real-wire applied-input physics replay; artifacts=" + str(output / stem))


if __name__ == "__main__":
    main()
