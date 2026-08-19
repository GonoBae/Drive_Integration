import asyncio
import logging

from fastapi import WebSocket


logger = logging.getLogger(__name__)


class WebSocketManager:
    def __init__(self, send_timeout_seconds: float = 0.1):
        self._connections: set[WebSocket] = set()
        self._lock = asyncio.Lock()
        self._send_timeout_seconds = send_timeout_seconds

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        async with self._lock:
            self._connections.add(websocket)
        logger.info("WebSocket client connected; total=%d", len(self._connections))

    async def disconnect(self, websocket: WebSocket) -> None:
        async with self._lock:
            self._connections.discard(websocket)
        logger.info("WebSocket client disconnected; total=%d", len(self._connections))

    async def broadcast(self, message: str) -> int:
        if not self._connections:
            return 0

        async with self._lock:
            targets = tuple(self._connections)

        results = await asyncio.gather(*(self._send(ws, message) for ws in targets))
        dead = {websocket for websocket, sent in zip(targets, results) if not sent}

        if dead:
            async with self._lock:
                self._connections -= dead
            await asyncio.gather(*(self._close(websocket) for websocket in dead))
        return len(dead)

    async def _send(self, websocket: WebSocket, message: str) -> bool:
        try:
            await asyncio.wait_for(
                websocket.send_text(message),
                timeout=self._send_timeout_seconds,
            )
            return True
        except asyncio.TimeoutError:
            logger.warning(
                "WebSocket send timed out after %.3fs",
                self._send_timeout_seconds,
            )
        except Exception as exc:
            logger.warning("WebSocket send failed: %s", exc)
        return False

    async def _close(self, websocket: WebSocket) -> None:
        try:
            await asyncio.wait_for(
                websocket.close(code=1013, reason="relay send failed"),
                timeout=self._send_timeout_seconds,
            )
        except asyncio.TimeoutError:
            logger.warning(
                "WebSocket close timed out after %.3fs",
                self._send_timeout_seconds,
            )
        except Exception as exc:
            logger.debug("WebSocket close failed: %s", exc)

    @property
    def connection_count(self) -> int:
        return len(self._connections)
