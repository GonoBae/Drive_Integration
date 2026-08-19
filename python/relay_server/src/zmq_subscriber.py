import asyncio
import json
import zmq.asyncio

from config import settings
from src.database import insert_entity_states_batch
from src.websocket_manager import manager

# protoc로 생성된 파일 임포트 (scripts/generate_proto.sh 실행 후 사용 가능)
from generated.vehicle_pb2 import EntityStatePacket


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
            }
            for wheel in state.wheels
        ],
    }


async def zmq_subscriber_task() -> None:
    context = zmq.asyncio.Context()
    socket = context.socket(zmq.SUB)
    socket.connect(f"tcp://{settings.zmq_host}:{settings.zmq_port}")
    socket.setsockopt_string(zmq.SUBSCRIBE, "")

    print(f"[ZMQ] Subscriber connected → tcp://{settings.zmq_host}:{settings.zmq_port}")

    while True:
        try:
            data: bytes = await socket.recv()

            packet = EntityStatePacket()
            packet.ParseFromString(data)

            if not packet.entities:
                continue

            # WebSocket 브로드캐스트 (JSON 변환)
            payload = json.dumps([_state_to_dict(s) for s in packet.entities])
            await manager.broadcast(payload)

            # DB 배치 저장 (설정에 따라 선택적 저장)
            if settings.db_enabled:
                await insert_entity_states_batch(list(packet.entities))

        except asyncio.CancelledError:
            break
        except Exception as e:
            print(f"[ZMQ] Error: {e}")
            await asyncio.sleep(1)

    socket.close()
    context.term()
    print("[ZMQ] Subscriber stopped.")
