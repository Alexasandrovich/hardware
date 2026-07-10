"""In-memory serial pair for tests and --mock (no PTY required)."""

from __future__ import annotations

import queue
import threading
import time
from typing import Optional


class FakeSerial:
    """Minimal pyserial-compatible linked port."""

    def __init__(self, peer_tx: queue.Queue[bytes], peer_rx: queue.Queue[bytes], name: str = "fake"):
        self._tx = peer_tx
        self._rx = peer_rx
        self.port = name
        self.timeout = 0.05
        self._open = True

    @property
    def is_open(self) -> bool:
        return self._open

    def read(self, size: int = 1) -> bytes:
        if not self._open:
            return b""
        deadline = time.monotonic() + (self.timeout or 0)
        out = bytearray()
        while len(out) < size and time.monotonic() < deadline:
            try:
                chunk = self._rx.get(timeout=max(0, deadline - time.monotonic()))
                out.extend(chunk)
            except queue.Empty:
                break
        return bytes(out)

    def write(self, data: bytes) -> int:
        if not self._open:
            raise OSError("port closed")
        self._tx.put(bytes(data))
        return len(data)

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self._open = False


def make_linked_pair() -> tuple[FakeSerial, FakeSerial]:
    q_ab: queue.Queue[bytes] = queue.Queue()
    q_ba: queue.Queue[bytes] = queue.Queue()
    a = FakeSerial(q_ab, q_ba, "fakeA")
    b = FakeSerial(q_ba, q_ab, "fakeB")
    return a, b
