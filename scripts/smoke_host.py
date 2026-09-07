"""Shared child-process lifecycle for isolated WebSocket smoke checks."""

import asyncio
from contextlib import contextmanager
import os
import subprocess
import time

import websockets


@contextmanager
def child_host(command, *, cwd, stdout, stderr):
    """Stop only the process created here, including failed or interrupted runs."""
    process = subprocess.Popen(
        command, cwd=cwd, stdout=stdout, stderr=stderr,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    try:
        yield process
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


async def connect_child(url, process, *, close_timeout=1):
    """Retry startup transport failures; callers verify the nonce before sending."""
    deadline = time.monotonic() + 10
    while True:
        if process.poll() is not None:
            raise AssertionError("isolated host exited during startup; inspect its logs")
        try:
            return await websockets.connect(url, open_timeout=1,
                                            close_timeout=close_timeout, max_queue=256)
        except (OSError, TimeoutError):
            if time.monotonic() >= deadline:
                raise
            await asyncio.sleep(0.1)
