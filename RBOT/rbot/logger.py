"""Structured event logging for RBOT (Pi side)."""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Callable, Optional


class LogLevel(str, Enum):
    INFO = "INFO"
    WARN = "WARN"
    CRIT = "CRIT"


@dataclass
class LogEvent:
    level: LogLevel
    code: str
    message: str
    extra: dict[str, Any] = field(default_factory=dict)
    ts: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    def format(self) -> str:
        base = self.ts.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3]
        extra = " ".join(f"{k}={v}" for k, v in self.extra.items())
        if extra:
            return f"{base} [{self.level.value}] {self.code} {self.message} {extra}"
        return f"{base} [{self.level.value}] {self.code} {self.message}"


class EventLogger:
    def __init__(self, sink: Optional[Callable[[LogEvent], None]] = None):
        self._sink = sink or self._default_sink
        self.events: list[LogEvent] = []

    @staticmethod
    def _default_sink(ev: LogEvent) -> None:
        print(ev.format(), file=sys.stderr)

    def _emit(self, level: LogLevel, code: str, message: str, **extra: Any) -> None:
        ev = LogEvent(level, code, message, extra)
        self.events.append(ev)
        self._sink(ev)

    def info(self, code: str, message: str = "", **extra: Any) -> None:
        self._emit(LogLevel.INFO, code, message, **extra)

    def warn(self, code: str, message: str = "", **extra: Any) -> None:
        self._emit(LogLevel.WARN, code, message, **extra)

    def crit(self, code: str, message: str = "", **extra: Any) -> None:
        self._emit(LogLevel.CRIT, code, message, **extra)

    @property
    def halted(self) -> bool:
        return any(e.level == LogLevel.CRIT for e in self.events)
