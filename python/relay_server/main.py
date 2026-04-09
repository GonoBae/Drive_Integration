import asyncio
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from config import settings
from src.database import init_db, close_db
from src.websocket_manager import manager
from src.zmq_subscriber import zmq_subscriber_task


@asynccontextmanager
async def lifespan(_app: FastAPI):
    if settings.db_enabled:
        await init_db()
    task = asyncio.create_task(zmq_subscriber_task())
    yield
    task.cancel()
    if settings.db_enabled:
        await close_db()


app = FastAPI(title="SimCore Relay Server", lifespan=lifespan)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()  # keep-alive (연결 유지 감시)
    except WebSocketDisconnect:
        await manager.disconnect(websocket)


@app.get("/")
def health():
    return {
        "status": "running",
        "ws_clients": manager.connection_count,
    }
