import asyncio
import json
import logging
import time
from dataclasses import asdict, dataclass
from typing import Awaitable, Callable

from google.protobuf.message import DecodeError
import zmq.asyncio

from ..config import Settings, settings
from ..generated.vehicle_pb2 import EntityStatePacket
from .database import insert_entity_states_batch
from .websocket_manager import WebSocketManager


logger = logging.getLogger(__name__)
DatabaseWriter = Callable[[list], Awaitable[None]]


@dataclass
class SubscriberStatus:
    running: bool = False
    socket_ready: bool = False
    received_messages: int = 0
    decoded_messages: int = 0
    decode_errors: int = 0
    transport_errors: int = 0
    websocket_errors: int = 0
    database_errors: int = 0
    last_message_unix_seconds: float | None = None
    last_error: str | None = None
    db_retry_after_monotonic: float = 0.0

    def snapshot(self) -> dict:
        result = asdict(self)
        result.pop("db_retry_after_monotonic")
        if self.last_message_unix_seconds is None:
            result["last_message_age_ms"] = None
        else:
            result["last_message_age_ms"] = max(
                0.0,
                (time.time() - self.last_message_unix_seconds) * 1000.0,
            )
        return result


def _state_to_dict(state) -> dict:
    return {
        "entity_id": state.entity_id,
        "timestamp": state.timestamp,
        "lat":       state.lat,
        "lon":       state.lon,
        "alt":       state.alt,
        "heading":   state.heading,
        "pitch":     state.pitch,
        "roll":      state.roll,
        "speed":     state.speed,
        "accel":     state.accel,
        "fuel":      state.fuel,
        "rpm":       state.rpm,
        "east":      state.east,
        "north":     state.north,
        "yaw_rate":  state.yaw_rate,
        "steering_angle": state.steering_angle,
        "gear":      state.gear,
        "position_enu": {
            "x": state.position_enu.x,
            "y": state.position_enu.y,
            "z": state.position_enu.z,
        },
        "linear_velocity_body": {
            "x": state.linear_velocity_body.x,
            "y": state.linear_velocity_body.y,
            "z": state.linear_velocity_body.z,
        },
        "angular_velocity_body": {
            "x": state.angular_velocity_body.x,
            "y": state.angular_velocity_body.y,
            "z": state.angular_velocity_body.z,
        },
        "wheels": [
            {
                "wheel_index": wheel.wheel_index,
                "in_contact": wheel.in_contact,
                "steering_angle": wheel.steering_angle,
                "angular_speed": wheel.angular_speed,
                "normal_load": wheel.normal_load,
                "longitudinal_slip": wheel.longitudinal_slip,
                "slip_angle": wheel.slip_angle,
                "longitudinal_force": wheel.longitudinal_force,
                "lateral_force": wheel.lateral_force,
                "contact_point_enu": {
                    "x": wheel.contact_point_enu.x,
                    "y": wheel.contact_point_enu.y,
                    "z": wheel.contact_point_enu.z,
                },
                "contact_normal_enu": {
                    "x": wheel.contact_normal_enu.x,
                    "y": wheel.contact_normal_enu.y,
                    "z": wheel.contact_normal_enu.z,
                },
            }
            for wheel in state.wheels
        ],
    }


def _decode_packet(data: bytes, status: SubscriberStatus) -> EntityStatePacket | None:
    packet = EntityStatePacket()
    try:
        packet.ParseFromString(data)
    except DecodeError as exc:
        status.decode_errors += 1
        status.last_error = f"protobuf decode: {exc}"
        logger.warning("Discarding malformed ZMQ protobuf frame: %s", exc)
        return None
    status.decoded_messages += 1
    return packet


async def _deliver_packet(
    packet: EntityStatePacket,
    status: SubscriberStatus,
    websocket_manager: WebSocketManager,
    relay_settings: Settings,
    database_writer: DatabaseWriter,
) -> None:
    payload = json.dumps([_state_to_dict(state) for state in packet.entities])

    try:
        status.websocket_errors += await websocket_manager.broadcast(payload)
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        status.websocket_errors += 1
        status.last_error = f"websocket broadcast: {exc}"
        logger.exception("WebSocket broadcast failed")

    if not relay_settings.db_enabled:
        return

    now = time.monotonic()
    if now < status.db_retry_after_monotonic:
        return

    try:
        await asyncio.wait_for(
            database_writer(list(packet.entities)),
            timeout=relay_settings.db_write_timeout_seconds,
        )
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        status.database_errors += 1
        status.last_error = f"database write: {exc}"
        status.db_retry_after_monotonic = now + relay_settings.db_error_backoff_seconds
        logger.exception(
            "Database write failed; retrying after %.3fs",
            relay_settings.db_error_backoff_seconds,
        )


async def zmq_subscriber_task(
    status: SubscriberStatus,
    websocket_manager: WebSocketManager,
    relay_settings: Settings = settings,
    database_writer: DatabaseWriter = insert_entity_states_batch,
) -> None:
    context: zmq.asyncio.Context | None = None
    socket: zmq.asyncio.Socket | None = None
    endpoint = f"tcp://{relay_settings.zmq_host}:{relay_settings.zmq_port}"

    status.running = True
    try:
        context = zmq.asyncio.Context()
        socket = context.socket(zmq.SUB)
        socket.setsockopt(zmq.RCVHWM, relay_settings.zmq_receive_hwm)
        socket.setsockopt(zmq.CONFLATE, int(relay_settings.zmq_conflate))
        socket.setsockopt(zmq.LINGER, relay_settings.zmq_linger_ms)
        socket.setsockopt_string(zmq.SUBSCRIBE, "")
        socket.connect(endpoint)
        status.socket_ready = True

        logger.info("ZMQ subscriber connected to %s", endpoint)

        while True:
            try:
                data: bytes = await socket.recv()
            except asyncio.CancelledError:
                raise
            except zmq.ZMQError as exc:
                status.transport_errors += 1
                status.last_error = f"ZMQ receive: {exc}"
                logger.exception("ZMQ receive failed")
                await asyncio.sleep(relay_settings.zmq_error_backoff_seconds)
                continue

            status.received_messages += 1
            status.last_message_unix_seconds = time.time()
            packet = _decode_packet(data, status)
            if packet is None or not packet.entities:
                continue

            await _deliver_packet(
                packet,
                status,
                websocket_manager,
                relay_settings,
                database_writer,
            )
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        status.transport_errors += 1
        status.last_error = f"subscriber stopped: {exc}"
        logger.exception("ZMQ subscriber stopped unexpectedly")
    finally:
        status.socket_ready = False
        status.running = False
        if socket is not None:
            socket.close(linger=relay_settings.zmq_linger_ms)
        if context is not None:
            context.term()
        logger.info("ZMQ subscriber stopped")
