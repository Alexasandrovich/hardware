"""RBOT — Robot Bus over Text (Pi master ↔ NUCLEO slave)."""

from rbot.client import LinkState, RobotClient, RobotHalted
from rbot.protocol import Frame, PROTO_VERSION, build_frame, parse_line
from rbot.logger import EventLogger, LogLevel

__all__ = [
    "Frame",
    "PROTO_VERSION",
    "build_frame",
    "parse_line",
    "RobotClient",
    "LinkState",
    "RobotHalted",
    "EventLogger",
    "LogLevel",
]
