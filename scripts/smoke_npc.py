"""Real-wire lane-NPC acceptance: isolated child, immutable authored city inputs.

Runs the complete configured route up to its safe stop behind stationary Ego.
Checks naturally encountered red/green, pose/motion atomicity and fresh-PIE reset.
Never connects to port 9000 or terminates a pre-existing server.
"""

import asyncio
from contextlib import suppress
from datetime import datetime
import json
import math
import socket
import time
import uuid

import websockets

from smoke_host import child_host, connect_child

from smoke_traffic import (ROOT, pb, require, network_expectations, TrafficController,
                           reset_to_all_red, wait_state, heartbeat)

ROUTE = [260, 270, 280, 290, 200, 210, 220, 230, 240, 250]


def npc(message):
    entities = [entry for entry in message.world_state.entities if entry.entity_id == 1001]
    require(len(entities) == 1, "configured lane NPC must be published exactly once")
    result = entities[0]
    require(result.entity_kind == pb.ENTITY_KIND_NPC_VEHICLE, "NPC kind changed")
    require(all(math.isfinite(value) for value in (result.position_enu.x, result.position_enu.y,
        result.position_enu.z, result.heading, result.speed)), "nonfinite NPC pose")
    require(0 <= result.speed <= 6.01, "NPC exceeded configured speed")
    return result


def distance_to_segment(x, y, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = max(0, min(1, ((x - a[0]) * dx + (y - a[1]) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - a[0] - t * dx, y - a[1] - t * dy)


async def exercise(url, process, source, expected, lanes, trace):
    connection = await connect_child(url, process)
    controller = TrafficController(connection, "npc-play-" + uuid.uuid4().hex, source, expected)
    try:
        await controller.hello()
        await wait_state(controller, lambda msg: msg.world_state.health.status == "awaiting_reset")
        await reset_to_all_red(controller)
        initial = npc(await controller.receive_state())
        require(abs(initial.position_enu.x - 20) < .01 and abs(initial.position_enu.y) < .01,
                "NPC spawn must be 20m ahead of Ego")
        visited, red_stop, restarted = set(), False, False
        previous = None
        frames = 0
        sender = asyncio.create_task(heartbeat(controller))
        try:
            async def drive():
                nonlocal previous, frames, red_stop, restarted
                while True:
                    if sender.done():
                        sender.result()
                    message = await controller.receive_state()
                    require(message.world_state.health.status in ("awaiting_control", "active"),
                            "fresh controller lost authority")
                    entry = npc(message)
                    x, y = entry.position_enu.x, entry.position_enu.y
                    now = message.simulation_time_ns / 1e9
                    distances = {identity: min(distance_to_segment(x, y, a, b)
                        for a, b in zip(points, points[1:])) for identity, points in lanes.items()}
                    nearest = min(distances, key=distances.get)
                    require(distances[nearest] < .02, "NPC left its authored route")
                    visited.add(nearest)
                    require(entry.position_enu.z >= .80, "NPC fell below flat route ground")
                    if previous is not None:
                        old_time, old_x, old_y = previous
                        dt = now - old_time
                        if 0 < dt < .025:
                            measured_speed = math.hypot(x-old_x, y-old_y) / dt
                            require(abs(measured_speed - entry.speed) < .015,
                                    "published speed disagrees with accepted displacement (double step?)")
                    previous = (now, x, y)
                    aspect = next(head.aspect for head in message.world_state.traffic_signals if head.group_id == 1)
                    if nearest == 200 and x > 10.2 and abs(y - 144) < .01:
                        if aspect != pb.TRAFFIC_SIGNAL_GREEN and entry.speed < .02:
                            red_stop = True
                            require(x >= 10.69, "NPC front crossed the red stopline margin")
                        if red_stop and aspect == pb.TRAFFIC_SIGNAL_GREEN and entry.speed > .1:
                            restarted = True
                    frames += 1
                    trace.write(json.dumps({"t": now, "x": x, "y": y,
                        "speed": entry.speed, "lane": nearest, "signal1": aspect}) + "\n")
                    if frames % 1200 == 0:
                        print(f"Progress t={now:.1f}s lane={nearest} speed={entry.speed:.2f} visited={len(visited)}", flush=True)
                    if len(visited) == len(ROUTE) and red_stop and restarted and nearest == 260 and x < 0 and entry.speed < .02:
                        require(x < -4.5, "NPC did not keep a gap behind stationary Ego")
                        print(f"PASS route/red/restart/Ego stop: frames={frames} t={now:.2f}s x={x:.2f}", flush=True)
                        return
                    require(now < 150, f"route did not complete: visited={visited}, red={red_stop}, restart={restarted}")
            await asyncio.wait_for(drive(), 165)
        finally:
            sender.cancel()
            with suppress(asyncio.CancelledError):
                await sender
        safe = await wait_state(controller, lambda message: message.world_state.health.status == "safe_stop")
        safe_npc = npc(safe)
        safe_position = (safe_npc.position_enu.x, safe_npc.position_enu.y)
        for _ in range(4):
            stopped = npc(await controller.receive_state())
            require(stopped.speed == 0 and (stopped.position_enu.x, stopped.position_enu.y) == safe_position,
                    "NPC must freeze during lease SafeStop")
        # Real End PIE -> Play opens a new transport session. A new play on the
        # old active connection is intentionally rejected by the host contract.
        await connection.close()
        connection = await websockets.connect(url, open_timeout=1, close_timeout=1, max_queue=256)
        controller = TrafficController(connection, "npc-new-play-" + uuid.uuid4().hex, source, expected)
        await controller.hello()
        await reset_to_all_red(controller)
        reset_npc = npc(await controller.receive_state())
        require(abs(reset_npc.position_enu.x - 20) < .01 and reset_npc.speed == 0,
                "fresh Play did not reset NPC position/speed")
        print("PASS safe-stop freeze and fresh-Play NPC reset", flush=True)
    finally:
        await connection.close()


def main():
    network_path = ROOT / "map_packages/virtual_city_v1/traffic_network.json"
    data = network_path.read_bytes()
    expected = network_expectations(data)
    network = json.loads(data)
    lanes = {lane["id"]: lane["points"] for lane in network["lanes"] if lane["id"] in ROUTE}
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    require(port != 9000, "refusing user port")
    source = "npc-host-" + uuid.uuid4().hex
    directory = ROOT / "runtime_logs"
    directory.mkdir(exist_ok=True)
    stem = "npc-smoke-" + datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    command = [str(ROOT / "cpp/host/build/Release/simcore_publisher.exe"), "--runtime-config",
        str(ROOT / "cpp/host/config/virtual_city_server.cfg"), "--ws-port", str(port),
        "--source-id", source, "--npc-route", ",".join(map(str, ROUTE)),
        "--npc-loop", "true", "--npc-start-offset", "96", "--npc-max-speed", "6", "--no-demo-entities"]
    with (directory / (stem + ".stdout.log")).open("wb") as stdout, \
         (directory / (stem + ".stderr.log")).open("wb") as stderr, \
         (directory / (stem + ".jsonl")).open("w", encoding="utf-8") as trace, \
         child_host(command, cwd=ROOT, stdout=stdout, stderr=stderr) as process:
        print(f"Isolated NPC server PID={process.pid}, port={port}; evidence={directory / stem}", flush=True)
        asyncio.run(exercise(f"ws://127.0.0.1:{port}", process, source, expected, lanes, trace))
        print("PASS NPC WebSocket smoke (isolated child only)", flush=True)


if __name__ == "__main__":
    main()
