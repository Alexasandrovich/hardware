"""RBOT master client for Raspberry Pi."""

from __future__ import annotations

import threading
import time
from enum import Enum
from typing import Optional

import serial

from rbot.logger import EventLogger
from rbot.protocol import Frame, build_frame, parse_line


class LinkState(str, Enum):
    DOWN = "DOWN"
    UP = "UP"


class RobotHalted(Exception):
    """Critical fault — motion not allowed until reset."""


class RobotClient:
    RECONNECT_BACKOFF = (0.5, 1.0, 2.0, 5.0, 10.0)
    MAX_RETRIES = 20
    CMD_TIMEOUT_S = 0.15

    def __init__(
        self,
        port: str,
        baud: int = 115200,
        logger: Optional[EventLogger] = None,
        pwm_max: int = 60,
    ):
        self.port_name = port
        self.baud = baud
        self.log = logger or EventLogger()
        self.pwm_max = pwm_max
        self._seq = 0
        self._ser: Optional[serial.Serial] = None
        self.link = LinkState.DOWN
        self._state = "DISARMED"
        self._halted = False
        self._io_lock = threading.Lock()

    def _next_seq(self) -> int:
        self._seq = (self._seq + 1) % 10000
        return self._seq

    def open(self) -> None:
        if self._ser is not None and self._ser.is_open:
            self.link = LinkState.UP
            return
        self.close()
        self._ser = serial.Serial()
        self._ser.port = self.port_name
        self._ser.baudrate = self.baud
        self._ser.timeout = 0.05
        self._ser.rtscts = False
        self._ser.xonxoff = False
        self._ser.open()
        self._ser.dtr = True
        self._ser.rts = True
        time.sleep(3.0)
        self._ser.reset_input_buffer()
        self.link = LinkState.UP

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None
        self.link = LinkState.DOWN

    def _send_line(self, line: str) -> None:
        if not self._ser or not self._ser.is_open:
            raise serial.SerialException("port closed")
        self._ser.write(line.encode("ascii"))
        self._ser.flush()

    def _send(self, cmd: str, *args: str) -> int:
        seq = self._next_seq()
        self._send_line(build_frame(seq, cmd, *args))
        return seq

    def _read_response(self, expect_seq: int, timeout: float = CMD_TIMEOUT_S) -> Optional[Frame]:
        if not self._ser:
            return None
        deadline = time.monotonic() + timeout
        buf = ""
        while time.monotonic() < deadline:
            try:
                chunk = self._ser.read(256)
            except serial.SerialException:
                return None
            if chunk:
                buf += chunk.decode("ascii", errors="ignore")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    frame = parse_line(line.strip())
                    if not frame:
                        continue
                    if frame.cmd == "EVT":
                        self._handle_evt(frame)
                        continue
                    if frame.seq == expect_seq or frame.seq is None:
                        return frame
            else:
                time.sleep(0.005)
        return None

    def _exchange(self, cmd: str, *args: str, timeout: float = CMD_TIMEOUT_S) -> Optional[Frame]:
        with self._io_lock:
            if not self._ser or not self._ser.is_open:
                raise serial.SerialException("port closed")
            seq = self._send(cmd, *args)
            return self._read_response(seq, timeout=timeout)

    def _handle_evt(self, frame: Frame) -> None:
        code = frame.arg0
        if code == "WD_TRIGGER":
            self.log.warn("EVT_WD_TRIGGER", detail=frame.args[1] if len(frame.args) > 1 else "")
        elif code == "FAULT_ESTOP":
            self.log.crit("ESTOP_STM", detail=frame.args[1] if len(frame.args) > 1 else "")
            self._halted = True

    def _require_not_halted(self) -> None:
        if self._halted:
            raise RobotHalted("robot in HALTED state")

    def handshake(self) -> bool:
        resp = self._exchange("HELLO", "proto=1", timeout=1.0)
        if not resp or resp.cmd != "HELLO_ACK":
            return False
        if len(resp.args) >= 2:
            self._state = resp.args[1]
        self.log.info("LINK_UP", port=self.port_name, state=self._state)
        return True

    def arm(self) -> bool:
        self._require_not_halted()
        resp = self._exchange("ARM")
        if resp and resp.cmd == "ACK":
            self._state = "ARMED"
            self.log.info("ARMED")
            return True
        if resp and resp.cmd == "NAK":
            self.log.warn("NAK_ARM", reason=resp.args[1] if len(resp.args) > 1 else "")
        return False

    def disarm(self) -> bool:
        try:
            resp = self._exchange("DISARM", timeout=0.3)
        except serial.SerialException:
            return False
        if resp and resp.cmd == "ACK":
            self._state = "DISARMED"
            self.log.info("DISARMED")
            return True
        return False

    def ping(self) -> bool:
        self._require_not_halted()
        try:
            resp = self._exchange("PING", timeout=0.1)
        except serial.SerialException:
            self.link = LinkState.DOWN
            return False
        return resp is not None and resp.cmd == "PONG"

    def drive(self, left: int, right: int) -> bool:
        self._require_not_halted()
        left = max(-self.pwm_max, min(self.pwm_max, left))
        right = max(-self.pwm_max, min(self.pwm_max, right))
        try:
            resp = self._exchange("DRIVE", str(left), str(right))
        except serial.SerialException:
            self.link = LinkState.DOWN
            self.log.warn("LINK_LOST", cmd="DRIVE")
            return False
        if resp and resp.cmd == "ACK":
            return True
        if resp and resp.cmd == "NAK":
            reason = resp.args[1] if len(resp.args) > 1 else ""
            if reason == "NOT_ARMED":
                self.log.warn("NAK_NOT_ARMED")
            else:
                self.log.warn("NAK_DRIVE", reason=reason)
        else:
            self.log.warn("CMD_TIMEOUT", cmd="DRIVE")
        return False

    def stop(self) -> bool:
        try:
            resp = self._exchange("STOP", timeout=0.3)
        except serial.SerialException:
            return False
        return resp is not None and resp.cmd == "ACK"

    def estop(self, reason: str = "user") -> None:
        try:
            self._exchange("ESTOP", reason, timeout=0.3)
        except serial.SerialException:
            pass
        self._halted = True
        self.log.crit("ESTOP_USER", reason=reason)

    def reset_fault(self) -> bool:
        resp = self._exchange("RESET")
        if resp and resp.cmd == "ACK":
            self._halted = False
            self._state = "DISARMED"
            return True
        return False

    def connect_session(self) -> bool:
        """HELLO → ARM → DRIVE 0 0."""
        if not self.handshake():
            return False
        if not self.arm():
            return False
        return self.drive(0, 0)

    def reconnect(self) -> bool:
        self.log.warn("LINK_LOST")
        self.close()
        for attempt in range(self.MAX_RETRIES):
            delay = self.RECONNECT_BACKOFF[min(attempt, len(self.RECONNECT_BACKOFF) - 1)]
            self.log.warn("LINK_RETRY", attempt=attempt + 1)
            time.sleep(delay)
            try:
                self.open()
                if self.connect_session():
                    self.log.info("LINK_RESTORED", attempt=attempt + 1)
                    return True
            except serial.SerialException:
                continue
        self.log.crit("LINK_DEAD")
        self._halted = True
        return False

    def session_start(self) -> bool:
        self.log.info("SESSION_START")
        self.open()
        return self.connect_session()

    def session_stop(self) -> None:
        try:
            self.disarm()
        finally:
            self.close()
            self.log.info("SESSION_STOP")
