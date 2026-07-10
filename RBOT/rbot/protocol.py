"""RBOT/1 frame parse and build — shared by Pi client and mock STM32."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

PROTO_VERSION = "RBOT/1"
FW_VERSION = "FW1.0.0"
MAX_LINE = 127


@dataclass(frozen=True)
class Frame:
    seq: int
    cmd: str
    args: tuple[str, ...]
    raw: str

    @property
    def arg0(self) -> str:
        return self.args[0] if self.args else ""


def xor_checksum(body: str) -> str:
    cs = 0
    for b in body.encode("ascii"):
        cs ^= b
    return f"{cs:02X}"


def build_frame(seq: int, cmd: str, *args: str) -> str:
    parts = [f"{seq % 10000:04d}", cmd.upper()]
    parts.extend(str(a) for a in args)
    body = ":".join(parts)
    cs = xor_checksum(body)
    return f"@{body}*{cs}\n"


def parse_line(line: str) -> Optional[Frame]:
    """Parse one RBOT frame. Non-@ lines are rejected (return None)."""
    line = line.strip()
    if not line or len(line) > MAX_LINE or not line.startswith("@"):
        return None
    if "*" not in line:
        return None

    star = line.rindex("*")
    body = line[1:star]
    cs_given = line[star + 1 :].strip()
    if len(cs_given) != 2 or xor_checksum(body).upper() != cs_given.upper():
        return Frame(-1, "INVALID", ("BAD_CS",), line)

    parts = body.split(":")
    if len(parts) < 2:
        return None
    try:
        seq = int(parts[0])
    except ValueError:
        return None
    cmd = parts[1].upper()
    args = tuple(parts[2:])
    return Frame(seq, cmd, args, line)


def build_response(seq: int, cmd: str, *args: str) -> str:
    return build_frame(seq, cmd, *args)


def clamp_speed(value: int, pwm_max: int = 100) -> int:
    limit = min(100, max(0, pwm_max))
    return max(-limit, min(limit, value))
