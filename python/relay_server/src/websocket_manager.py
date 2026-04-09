import asyncio
from fastapi import WebSocket


class WebSocketManager:
    def __init__(self):
        self._connections: set[WebSocket] = set()
        self._lock = asyncio.Lock()

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        async with self._lock:
            self._connections.add(websocket)
        print(f"[WS] Client connected. total={len(self._connections)}")

    async def disconnect(self, websocket: WebSocket) -> None:
        async with self._lock:
            self._connections.discard(websocket)
        print(f"[WS] Client disconnected. total={len(self._connections)}")

    async def broadcast(self, message: str) -> None:
        if not self._connections:
            return

        async with self._lock:
            targets = set(self._connections)

        dead: set[WebSocket] = set()
        await asyncio.gather(
            *[self._send(ws, message, dead) for ws in targets],
            return_exceptions=True,
        )

        if dead:
            async with self._lock:
                self._connections -= dead

    async def _send(self, websocket: WebSocket, message: str, dead: set) -> None:
        try:
            await websocket.send_text(message)
        except Exception:
            dead.add(websocket)

    @property
    def connection_count(self) -> int:
        return len(self._connections)


manager = WebSocketManager()
