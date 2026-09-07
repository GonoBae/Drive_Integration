"""Bounded real-WebSocket traffic smoke using only a dedicated child host.

Never connects to port 9000 or an existing PID. A per-run server nonce is
checked before any Hello/Reset/control frame, including a free-port race.
Only the Popen child is terminated; map/network inputs are never modified.
"""

import argparse
import asyncio
from contextlib import suppress
from dataclasses import dataclass
from datetime import datetime
import json
import math
from pathlib import Path
import re
import socket
import time
import uuid

import websockets
from websockets.exceptions import ConnectionClosed

from smoke_health import Controller, ROOT, pb, require
from smoke_host import child_host, connect_child

SECOND_NS = 1_000_000_000
CYCLE_NS = 30 * SECOND_NS


@dataclass(frozen=True)
class Expectations:
    format_version: int
    map_checksum: str
    network_checksum: str
    heads: dict
    plans: dict
    pedestrian_only_groups: frozenset

    @property
    def group_keys(self):
        return {(controller, group) for controller, group, _, _ in self.heads.values()}

    @property
    def observation_ns(self):
        if self.format_version == 1:
            return 32 * SECOND_NS
        return max(plan[2] for plan in self.plans.values()) + 2 * SECOND_NS


def network_expectations(data):
    """Read-only expected IDs/poses; C++ remains the authoritative full validator."""
    require(0 < len(data) <= 4 * 1024 * 1024, "traffic JSON size is invalid")

    def object_fields(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "duplicate JSON field")
            result[key] = value
        return result

    def invalid_constant(value):
        raise AssertionError("nonfinite JSON constant: " + value)

    network = json.loads(data.decode("utf-8"), object_pairs_hook=object_fields,
                         parse_constant=invalid_constant)
    version = network.get("format_version")
    require(type(version) is int and version in (1, 2), "expected traffic network version 1 or 2")
    checksum = network.get("source_map_checksum", "")
    require(re.fullmatch(r"fnv1a64:[0-9a-f]{16}", checksum), "invalid traffic map checksum")
    signals = network.get("signals", [])
    require(0 < len(signals) <= 32, "traffic smoke requires 1..32 authored signal heads")
    plans = {}
    group_owners = {}
    pedestrian_groups = {(signal.get("controller_id", 1), signal.get("group_id"))
                         for signal in signals if signal.get("kind") == "pedestrian"}
    vehicle_groups = {(signal.get("controller_id", 1), signal.get("group_id"))
                      for signal in signals if signal.get("kind", "vehicle") != "pedestrian"}
    pedestrian_only_groups = frozenset(pedestrian_groups - vehicle_groups)
    if version == 2:
        raw_plans = network.get("signal_plans")
        require(type(raw_plans) is list and 0 < len(raw_plans) <= 32,
                "version 2 requires 1..32 signal plans")
        for plan in raw_plans:
            identity, offset_ms = plan["id"], plan["offset_ms"]
            require(type(identity) is int and 1 <= identity <= 64 and identity not in plans,
                    "plan IDs must be distinct bounded integers")
            groups = plan["groups"]
            require(type(groups) is list and 0 < len(groups) <= 64
                    and all(type(group) is int and 1 <= group <= 4096 for group in groups)
                    and len(set(groups)) == len(groups), "invalid plan groups")
            for group in groups:
                require(group not in group_owners, "signal groups must belong to one controller")
                group_owners[group] = identity
            phases = []
            for phase in plan["phases"]:
                duration_ms = phase["duration_ms"]
                green, yellow = phase["green_groups"], phase["yellow_groups"]
                require(type(duration_ms) is int and 100 <= duration_ms <= 120000,
                        "invalid signal phase duration")
                require(type(green) is list and type(yellow) is list
                        and len(set(green)) == len(green) and len(set(yellow)) == len(yellow)
                        and not set(green).intersection(yellow)
                        and set(green).union(yellow).issubset(groups), "invalid phase group sets")
                # One protected vehicle approach at a time. Independent WALK
                # groups may coexist only when every vehicle approach is red.
                active_groups = {(identity, group) for group in [*green, *yellow]}
                require(len(active_groups) <= 1 or active_groups.issubset(pedestrian_only_groups),
                        "one controller phase grants conflicting groups together")
                phases.append((duration_ms * 1_000_000, frozenset(green), frozenset(yellow)))
            require(0 < len(phases) <= 64, "invalid signal phase count")
            cycle_ns = sum(phase[0] for phase in phases)
            require(cycle_ns <= 3600 * SECOND_NS, "signal plan cycle is unbounded")
            require(type(offset_ms) is int and 0 <= offset_ms * 1_000_000 < cycle_ns,
                    "signal plan offset is outside its cycle")
            plans[identity] = (offset_ms * 1_000_000, tuple(phases), cycle_ns)
    heads = {}
    for signal in signals:
        identity, group = signal["id"], signal["group_id"]
        require(type(identity) is int and identity > 0 and identity not in heads,
                "signal IDs must be distinct positive integers")
        controller = signal.get("controller_id", 1 if version == 1 else None)
        require(type(group) is int and 1 <= group <= (2 if version == 1 else 4096),
                "invalid signal group")
        require(type(controller) is int and 1 <= controller <= 64,
                "invalid signal controller")
        if version == 1:
            require(controller == 1, "legacy signal heads must use controller 1")
        else:
            require(group_owners.get(group) == controller,
                    "signal controller does not own its group")
        pose = signal["position_enu"]
        heading = signal["heading_deg"]
        require(len(pose) == 3 and all(type(value) in (int, float) and math.isfinite(value)
                                      for value in [*pose, heading]), "invalid authored signal pose")
        heads[identity] = (controller, group, tuple(pose), heading % 360)
    if version == 1:
        require({head[1] for head in heads.values()} == {1, 2}, "both legacy signal groups must exist")
    else:
        require(set(group_owners) == {head[1] for head in heads.values()},
                "each planned group must have an authored signal head")
    fnv = 14695981039346656037
    for byte in data:
        fnv = ((fnv ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return Expectations(version, checksum, f"fnv1a64:{fnv:016x}", heads, plans, pedestrian_only_groups)


def expected_phase(group, elapsed_ns, active):
    if not active:
        return pb.TRAFFIC_SIGNAL_RED, 0.0
    phase = elapsed_ns % CYCLE_NS
    if group == 1:
        if phase < 2 * SECOND_NS:
            return pb.TRAFFIC_SIGNAL_RED, (2 * SECOND_NS - phase) / SECOND_NS
        if phase < 14 * SECOND_NS:
            return pb.TRAFFIC_SIGNAL_GREEN, (14 * SECOND_NS - phase) / SECOND_NS
        if phase < 17 * SECOND_NS:
            return pb.TRAFFIC_SIGNAL_YELLOW, (17 * SECOND_NS - phase) / SECOND_NS
        return pb.TRAFFIC_SIGNAL_RED, (CYCLE_NS - phase + 2 * SECOND_NS) / SECOND_NS
    if phase < 19 * SECOND_NS:
        return pb.TRAFFIC_SIGNAL_RED, (19 * SECOND_NS - phase) / SECOND_NS
    if phase < 27 * SECOND_NS:
        return pb.TRAFFIC_SIGNAL_GREEN, (27 * SECOND_NS - phase) / SECOND_NS
    return pb.TRAFFIC_SIGNAL_YELLOW, (CYCLE_NS - phase) / SECOND_NS


def expected_signal(expected, controller, group, elapsed_ns, active):
    """Calculate the same integer-nanosecond state/countdown as the host."""
    if not active:
        return pb.TRAFFIC_SIGNAL_RED, 0.0
    if expected.format_version == 1:
        require(controller == 1, "legacy expectation received a foreign controller")
        return expected_phase(group, elapsed_ns, True)
    offset_ns, phases, cycle_ns = expected.plans[controller]
    position = ((elapsed_ns % cycle_ns) + offset_ns) % cycle_ns
    start = 0
    index = 0
    for index, (duration, _, _) in enumerate(phases):
        if position < start + duration:
            break
        start += duration

    def aspect(phase_index):
        _, green, yellow = phases[phase_index]
        if group in green:
            return pb.TRAFFIC_SIGNAL_GREEN
        if group in yellow:
            return pb.TRAFFIC_SIGNAL_YELLOW
        return pb.TRAFFIC_SIGNAL_RED

    current = aspect(index)
    remaining = phases[index][0] - (position - start)
    following = (index + 1) % len(phases)
    while following != index and aspect(following) == current:
        remaining += phases[following][0]
        following = (following + 1) % len(phases)
    if following == index and aspect(following) == current:
        remaining = 0
    return current, remaining / SECOND_NS


def validate_world(message, expected):
    """Validate traffic, pose and Health in the very same sequenced WorldState."""
    require(message.WhichOneof("payload") == "world_state", "traffic must be atomic with WorldState")
    require(message.map_package_checksum == expected.map_checksum, "collision checksum changed")
    world = message.world_state
    require(world.HasField("health"), "atomic traffic snapshot omitted Health")
    require(world.health.status in {"awaiting_reset", "awaiting_control", "active", "safe_stop",
                                    "reconnect_required", "estop_latched"}, "unknown Health status")
    ego = [entity for entity in world.entities if entity.entity_kind == pb.ENTITY_KIND_EGO_VEHICLE]
    require(len(ego) == 1 and ego[0].HasField("position_enu"), "atomic traffic snapshot omitted Ego pose")
    require(all(math.isfinite(value) for value in (ego[0].position_enu.x, ego[0].position_enu.y,
                                                  ego[0].position_enu.z, ego[0].heading, ego[0].speed)),
            "Ego pose/speed must be finite")
    require(world.traffic_network_checksum == expected.network_checksum, "network provenance changed")
    require([head.signal_id for head in world.traffic_signals] == sorted(expected.heads),
            "authored signal identity/order changed")
    groups = {}
    for head in world.traffic_signals:
        controller, group, position, heading = expected.heads[head.signal_id]
        require(head.controller_id == controller and head.group_id == group
                and head.HasField("position_enu"), "signal controller/group/pose changed")
        require(all(abs(actual - authored) < 1e-5 for actual, authored in zip(
            (head.position_enu.x, head.position_enu.y, head.position_enu.z), position)),
            "signal pose disagrees with the authored graph")
        require(abs(head.heading_deg - heading) < 0.001, "signal heading changed")
        maximum_countdown = (CYCLE_NS if expected.format_version == 1
                             else expected.plans[controller][2]) / SECOND_NS
        require(math.isfinite(head.remaining_seconds)
                and 0 <= head.remaining_seconds <= maximum_countdown,
                "signal countdown must be finite and bounded")
        phase, remaining = expected_signal(expected, controller, group,
                                           message.simulation_time_ns,
                                           world.health.status == "active")
        require(head.aspect == phase, "signal phase does not match this World's sim time/Health")
        require(abs(head.remaining_seconds - remaining) < 0.0001, "signal countdown disagrees with sim time")
        state = (head.aspect, head.remaining_seconds)
        key = (controller, group)
        require(key not in groups or groups[key] == state, "heads within one controller/group disagree")
        groups[key] = state
    for controller in {key[0] for key in groups}:
        active_groups = {key for key, (aspect, _) in groups.items()
                         if key[0] == controller and aspect != pb.TRAFFIC_SIGNAL_RED}
        require(len(active_groups) <= 1 or active_groups.issubset(expected.pedestrian_only_groups),
                "one controller has simultaneous conflicting non-red groups")
    return groups


class TrafficController(Controller):
    def __init__(self, connection, play_id, expected_source, expected):
        super().__init__(connection, play_id, expected_source)
        self.session = "traffic-" + uuid.uuid4().hex
        self.expected = expected

    def envelope(self):
        message = super().envelope()
        message.source_id = "traffic-smoke"
        return message

    async def hello(self):
        server = pb.Envelope.FromString(await asyncio.wait_for(self.ws.recv(), 3))
        require(server.WhichOneof("payload") == "hello", "first frame must be Hello")
        require(server.source_id == self.expected_source, "port does not belong to our isolated host")
        require(server.schema_version == 2, "schema mismatch")
        require(server.map_package_checksum == self.expected.map_checksum, "child loaded the wrong collision map")
        require({"world-health.v1", "traffic-signals.v1"}.issubset(server.hello.capabilities),
                "traffic/Health capability missing")
        self.checksum = server.map_package_checksum
        request = self.envelope()
        request.hello.build = f"traffic-smoke-v{self.expected.format_version}"
        request.hello.schema = server.hello.schema
        request.hello.capabilities.extend([
            "world-state.v2", "control.v2", "simulation-reset.v1",
            "map-package-checksum.v1", "world-health.v1", "traffic-signals.v1",
        ])
        await self.ws.send(request.SerializeToString())

    async def receive_state(self):
        message = await super().receive_state()
        validate_world(message, self.expected)
        return message


async def wait_state(controller, predicate, timeout=3):
    async def receive():
        while True:
            message = await controller.receive_state()
            if predicate(message):
                return message
    return await asyncio.wait_for(receive(), timeout)


async def reset_to_all_red(controller):
    await controller.reset()
    message = await wait_state(controller, lambda state:
                               state.play_session_id == controller.play_id
                               and state.world_state.health.status == "awaiting_control")
    # The immediate t=0 publication may be superseded by one tick on a loaded
    # machine. Either way, the first observed new PIE state must restart at zero
    # within six 60Hz ticks, never continue the preceding full-cycle run.
    require(message.simulation_time_ns < 100_000_000, "new PIE did not reset simulation time")
    require(all(head.aspect == pb.TRAFFIC_SIGNAL_RED for head in message.world_state.traffic_signals),
            "new PIE must begin all-red")
    print(f"PASS new PIE reset: t={message.simulation_time_ns / SECOND_NS:.3f}s all-red")


async def heartbeat(controller):
    while True:
        # Reuses Health smoke's neutral gear, zero throttle, brake=1 command.
        await controller.control()
        await asyncio.sleep(0.05)


async def observe(controller, stop_when, timeout):
    sender = asyncio.create_task(heartbeat(controller))
    try:
        async def receive():
            while True:
                if sender.done():
                    sender.result()
                message = await controller.receive_state()
                require(message.play_session_id == controller.play_id, "wrong play identity")
                require(message.world_state.health.status in {"awaiting_control", "active"},
                        "20Hz safe heartbeat failed to maintain its control lease")
                if message.world_state.health.status == "active" and stop_when(message):
                    return message
        return await asyncio.wait_for(receive(), timeout)
    finally:
        sender.cancel()
        with suppress(asyncio.CancelledError):
            await sender


async def exercise(url, process, expected_source, expected):
    connection = await connect_child(url, process)
    play_id = "traffic-play-" + uuid.uuid4().hex
    try:
        controller = TrafficController(connection, play_id, expected_source, expected)
        await controller.hello()
        await wait_state(controller, lambda state: state.world_state.health.status == "awaiting_reset")
        await reset_to_all_red(controller)
        seen = {key: set() for key in expected.group_keys}
        frames = 0
        independent_overlap = False

        def complete_cycle(message):
            nonlocal frames, independent_overlap
            frames += 1
            for head in message.world_state.traffic_signals:
                seen[(head.controller_id, head.group_id)].add(head.aspect)
            permissive_controllers = {
                head.controller_id for head in message.world_state.traffic_signals
                if head.aspect != pb.TRAFFIC_SIGNAL_RED
            }
            independent_overlap |= len(permissive_controllers) >= 2
            return message.simulation_time_ns >= expected.observation_ns

        await observe(controller, complete_cycle, expected.observation_ns / SECOND_NS + 6)
        require(all(aspects == {pb.TRAFFIC_SIGNAL_RED, pb.TRAFFIC_SIGNAL_YELLOW,
                                pb.TRAFFIC_SIGNAL_GREEN} for aspects in seen.values()),
                "full cycle did not exercise each controller/group's red/yellow/green")
        if expected.format_version == 2 and len(expected.plans) > 1:
            require(independent_overlap,
                    "different controllers never demonstrated simultaneous independent permission")
        print(f"PASS {expected.observation_ns / SECOND_NS:.0f}-second authoritative v"
              f"{expected.format_version} cycle: {frames} atomic WorldStates; "
              f"{len(expected.heads)} heads/{len(expected.group_keys)} groups"
              + ("; independent-controller overlap observed"
                 if expected.format_version == 2 and len(expected.plans) > 1 else ""))
    finally:
        await connection.close()

    # A new PIE needs a fresh connection because the server binds play identity
    # to the connection generation and rejects in-place play_id changes.
    play_id = "traffic-play-" + uuid.uuid4().hex
    async with websockets.connect(url, open_timeout=2, close_timeout=1, max_queue=256) as connection:
        controller = TrafficController(connection, play_id, expected_source, expected)
        await controller.hello()
        await reset_to_all_red(controller)
        await observe(controller, lambda state: state.simulation_time_ns >= int(2.1 * SECOND_NS), 4)
        stopped = await wait_state(controller, lambda state: state.world_state.health.status == "safe_stop")
        require(stopped.world_state.health.last_command_age_ns > 250_000_000,
                "soft timeout fired before the command-age deadline")
        require(all(head.aspect == pb.TRAFFIC_SIGNAL_RED for head in stopped.world_state.traffic_signals),
                "stale control must not leave a traffic green")
        preserved_time = stopped.simulation_time_ns

        async def hard_close():
            try:
                while True:
                    await controller.receive_state()
            except ConnectionClosed as error:
                require(error.rcvd is not None and error.rcvd.code == 1008,
                        "hard timeout must close the child connection with 1008")
                require(error.rcvd.reason == "control lease hard timeout; reconnect required",
                        "unexpected hard-timeout policy reason")
                require(time.monotonic() - controller.last_control_sent_at >= 0.95,
                        "hard timeout fired before its 1000ms deadline")
        await asyncio.wait_for(hard_close(), 3)
        print("PASS control timeout: atomic SafeStop/all-red and hard-close 1008")

    # This is a reconnect within the second PIE, not a third simulation reset.
    async with websockets.connect(url, open_timeout=2, close_timeout=1, max_queue=256) as connection:
        controller = TrafficController(connection, play_id, expected_source, expected)
        await controller.hello()
        await controller.reset()
        state = await wait_state(controller, lambda message:
                                 message.play_session_id == play_id
                                 and message.world_state.health.status == "awaiting_control")
        require(state.simulation_time_ns >= preserved_time, "same PIE reconnect reset its signal timeline")
        await controller.control()
        await wait_state(controller, lambda message: message.world_state.health.status == "active")
        print("PASS same PIE reconnect: identity and timeline preserved; fresh control restored")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map-package", type=Path, default=ROOT / "map_packages/virtual_city_v1")
    parser.add_argument("--traffic-network", type=Path,
                        default=ROOT / "map_packages/virtual_city_v1/traffic_network.json")
    parser.add_argument("--host", type=Path, default=ROOT / "cpp/host/build/Release/simcore_publisher.exe")
    args = parser.parse_args()
    require(args.host.is_file(), f"build the host first: {args.host}")
    require((args.map_package / "manifest.cfg").is_file(), "map package manifest missing")
    expected = network_expectations(args.traffic_network.read_bytes())
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    require(port != 9000, "refusing the user's standard server port")
    log_dir = ROOT / "runtime_logs"
    log_dir.mkdir(exist_ok=True)
    stem = "traffic-smoke-" + datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    expected_source = "traffic-host-" + uuid.uuid4().hex
    command = [str(args.host.resolve()), "--map-package", str(args.map_package.resolve()),
               "--traffic-network", str(args.traffic_network.resolve()),
               "--vehicle-config", str(ROOT / "cpp/host/config/vehicle_sedan.cfg"),
               "--ws-port", str(port), "--command-timeout-ms", "250",
               "--hard-command-timeout-ms", "1000", "--source-id", expected_source]
    with (log_dir / (stem + ".stdout.log")).open("wb") as stdout, \
            (log_dir / (stem + ".stderr.log")).open("wb") as stderr, \
            child_host(command, cwd=ROOT, stdout=stdout, stderr=stderr) as process:
        print(f"Isolated server PID={process.pid} port={port}; logs={log_dir / stem}")
        overall_timeout = max(55, expected.observation_ns / SECOND_NS + 22)
        asyncio.run(asyncio.wait_for(exercise(
            f"ws://127.0.0.1:{port}", process, expected_source, expected), overall_timeout))
        print("PASS Traffic WebSocket smoke (isolated child only)")


if __name__ == "__main__":
    main()
