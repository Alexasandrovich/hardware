"""Mock NUCLEO — same RBOT state machine as stm32 firmware."""

from __future__ import annotations

import threading
import time
from enum import Enum
from typing import Callable, Optional

import serial

from rbot.protocol import (
    FW_VERSION,
    PROTO_VERSION,
    Frame,
    build_frame,
    build_response,
    clamp_speed,
    parse_line,
    xor_checksum,
)


class StmState(str, Enum):
    DISARMED = "DISARMED"
    ARMED = "ARMED"
    FAULT = "FAULT"


class MockStm32:
    """RBOT slave simulator for pseudo-TTY or any serial port."""

    def __init__(
        self,
        pwm_max: int = 60,
        wd_ms: int = 500,
        on_motor: Optional[Callable[[int, int], None]] = None,
    ):
        self.pwm_max = pwm_max
        self.wd_ms = wd_ms
        self.state = StmState.DISARMED
        self.left = 0
        self.right = 0
        self.last_cmd_ms = time.monotonic()
        self.err_code = 0
        self._on_motor = on_motor or (lambda l, r: None)
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self, port: serial.Serial) -> None:
        self._port = port
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)

    def _write(self, text: str) -> None:
        self._port.write(text.encode("ascii"))
        self._port.flush()

    def _touch_watchdog(self) -> None:
        self.last_cmd_ms = time.monotonic()

    def _stop_motors(self) -> None:
        self.left = 0
        self.right = 0
        self._on_motor(0, 0)

    def _apply_drive(self, left: int, right: int) -> None:
        self.left = clamp_speed(left, self.pwm_max)
        self.right = clamp_speed(right, self.pwm_max)
        self._on_motor(self.left, self.right)

    def _reply_ack(self, seq: int, cmd: str) -> None:
        self._write(build_response(seq, "ACK", cmd))

    def _reply_nak(self, seq: int, cmd: str, reason: str) -> None:
        self._write(build_response(seq, "NAK", cmd, reason))

    def _reply_evt(self, code: str, detail: str) -> None:
        body = f"0000:EVT:{code}:{detail}"
        cs = xor_checksum(body)
        self._write(f"@{body}*{cs}\n")

    def _handle(self, frame: Frame) -> None:
        seq = frame.seq
        cmd = frame.cmd

        if cmd == "INVALID":
            if frame.seq >= 0:
                self._reply_nak(frame.seq, "FRAME", "BAD_CS")
            return

        if cmd == "UNKNOWN":
            self._reply_nak(seq, "UNKNOWN", "UNKNOWN_CMD")
            return

        if cmd == "HELLO":
            self.state = StmState.DISARMED
            self._stop_motors()
            self.err_code = 0
            self._touch_watchdog()
            self._write(
                build_response(
                    seq,
                    "HELLO_ACK",
                    PROTO_VERSION,
                    self.state.value,
                    FW_VERSION,
                )
            )
            return

        if cmd == "PING":
            self._touch_watchdog()
            self._write(build_response(seq, "PONG", f"{seq:04d}"))
            return

        if cmd == "ARM":
            if self.state == StmState.FAULT:
                self._reply_nak(seq, cmd, "FAULT")
                return
            self.state = StmState.ARMED
            self._touch_watchdog()
            self._reply_ack(seq, cmd)
            return

        if cmd == "DISARM":
            self.state = StmState.DISARMED
            self._stop_motors()
            self._touch_watchdog()
            self._reply_ack(seq, cmd)
            return

        if cmd == "STOP":
            self._stop_motors()
            self._touch_watchdog()
            self._reply_ack(seq, cmd)
            return

        if cmd == "ESTOP":
            self.state = StmState.FAULT
            self.err_code = 2
            self._stop_motors()
            reason = frame.arg0 or "unknown"
            self._reply_ack(seq, cmd)
            self._reply_evt("FAULT_ESTOP", reason)
            return

        if cmd == "RESET":
            if self.state != StmState.FAULT:
                self._reply_nak(seq, cmd, "BAD_ARGS")
                return
            self.state = StmState.DISARMED
            self.err_code = 0
            self._stop_motors()
            self._reply_ack(seq, cmd)
            return

        if cmd == "GET_STATUS":
            self._touch_watchdog()
            status = (
                f"{self.state.value}:L:{self.left}:R:{self.right}:"
                f"WD:{self.wd_ms}:ERR:{self.err_code}"
            )
            self._write(build_response(seq, "STATUS", status))
            return

        if cmd == "SET_CFG":
            if len(frame.args) < 2:
                self._reply_nak(seq, cmd, "BAD_ARGS")
                return
            key, val = frame.args[0], frame.args[1]
            if key == "pwm_max":
                self.pwm_max = max(0, min(100, int(val)))
            elif key == "wd_ms":
                self.wd_ms = max(100, min(2000, int(val)))
            else:
                self._reply_nak(seq, cmd, "BAD_ARGS")
                return
            self._reply_ack(seq, cmd)
            return

        if cmd == "DRIVE":
            if len(frame.args) < 2:
                self._reply_nak(seq, cmd, "BAD_ARGS")
                return
            if self.state == StmState.FAULT:
                self._reply_nak(seq, cmd, "FAULT")
                return
            if self.state != StmState.ARMED:
                self._reply_nak(seq, cmd, "NOT_ARMED")
                return
            try:
                left, right = int(frame.args[0]), int(frame.args[1])
            except ValueError:
                self._reply_nak(seq, cmd, "BAD_ARGS")
                return
            self._apply_drive(left, right)
            self._touch_watchdog()
            self._reply_ack(seq, cmd)
            return

        self._reply_nak(seq, cmd, "UNKNOWN_CMD")

    def _watchdog_tick(self) -> None:
        if self.state != StmState.ARMED:
            return
        elapsed_ms = (time.monotonic() - self.last_cmd_ms) * 1000
        if elapsed_ms > self.wd_ms:
            self._stop_motors()
            self.state = StmState.DISARMED
            self.err_code = 1
            self._reply_evt("WD_TRIGGER", str(int(elapsed_ms)))

    def _run(self) -> None:
        buf = ""
        while not self._stop.is_set():
            self._watchdog_tick()
            try:
                chunk = self._port.read(256)
            except serial.SerialException:
                break
            if not chunk:
                time.sleep(0.01)
                continue
            buf += chunk.decode("ascii", errors="ignore")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                frame = parse_line(line)
                if frame:
                    self._handle(frame)
