import asyncio
from contextlib import asynccontextmanager, suppress

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from .config import settings
from .src.database import close_db, init_db
from .src.websocket_manager import WebSocketManager
from .src.zmq_subscriber import SubscriberStatus, zmq_subscriber_task


manager = WebSocketManager(settings.ws_send_timeout_seconds)


@asynccontextmanager
async def lifespan(app: FastAPI):
    subscriber_task: asyncio.Task | None = None
    database_started = False
    status = SubscriberStatus()
    app.state.subscriber_status = status
    app.state.subscriber_task = None

    try:
        if settings.db_enabled:
            await init_db()
            database_started = True
        subscriber_task = asyncio.create_task(
            zmq_subscriber_task(status, manager, settings),
            name="zmq-subscriber",
        )
        app.state.subscriber_task = subscriber_task
        yield
    finally:
        try:
            if subscriber_task is not None:
                subscriber_task.cancel()
                with suppress(asyncio.CancelledError):
                    await subscriber_task
        finally:
            app.state.subscriber_task = None
            if database_started:
                await close_db()


app = FastAPI(title="SimCore Relay Server", lifespan=lifespan)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()  # keep-alive (연결 유지 감시)
    except WebSocketDisconnect:
        pass
    finally:
        await manager.disconnect(websocket)


@app.get("/")
async def health():
    subscriber_task = getattr(app.state, "subscriber_task", None)
    status = getattr(app.state, "subscriber_status", SubscriberStatus())
    task_running = subscriber_task is not None and not subscriber_task.done()
    if task_running and status.socket_ready:
        service_status = "running"
    elif task_running:
        service_status = "starting"
    else:
        service_status = "degraded"
    return {
        "status": service_status,
        "ws_clients": manager.connection_count,
        "subscriber": status.snapshot(),
    }
