"""CLI: mock demo and real NUCLEO session."""

from __future__ import annotations

import argparse
import sys
import time

import serial

from rbot.client import RobotClient
from rbot.fakeserial import make_linked_pair
from rbot.logger import EventLogger
from rbot.mock_stm32 import MockStm32


def run_mock_demo(drive: tuple[int, int], seconds: float) -> int:
    ser_client, ser_mock = make_linked_pair()

    motors: list[tuple[int, int]] = []

    def on_motor(l: int, r: int) -> None:
        motors.append((l, r))
        print(f"  [mock motor] L={l} R={r}", file=sys.stderr)

    mock = MockStm32(on_motor=on_motor)
    mock.start(ser_mock)

    log = EventLogger()
    client = RobotClient(ser_client.port, logger=log)
    client._ser = ser_client
    client.link = client.link.__class__.UP

    try:
        print("=== RBOT mock session (in-memory) ===", file=sys.stderr)
        if not client.connect_session():
            print("FAIL: connect_session", file=sys.stderr)
            return 1
        print("OK: HELLO + ARM + DRIVE 0 0", file=sys.stderr)

        left, right = drive
        if not client.drive(left, right):
            print(f"WARN: drive {left} {right}", file=sys.stderr)
        else:
            print(f"OK: drive {left} {right} for {seconds}s", file=sys.stderr)
        time.sleep(seconds)

        client.stop()
        client.disarm()
        print(f"OK: motors log entries={len(motors)}", file=sys.stderr)
        return 0
    finally:
        client.session_stop()
        mock.stop()
        ser_client.close()
        ser_mock.close()


def run_port_demo(port: str, drive: tuple[int, int], seconds: float) -> int:
    log = EventLogger()
    client = RobotClient(port, logger=log)
    try:
        print(f"=== RBOT on {port} ===", file=sys.stderr)
        if not client.session_start():
            print("FAIL: session_start — check NUCLEO USB and firmware", file=sys.stderr)
            return 1
        left, right = drive
        client.drive(left, right)
        time.sleep(seconds)
        client.stop()
        return 0
    finally:
        client.session_stop()


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="RBOT demo client")
    p.add_argument("command", nargs="?", default="demo", choices=["demo"])
    p.add_argument("--mock", action="store_true", help="pseudo-TTY + MockStm32")
    p.add_argument("--port", default="/dev/ttyACM0", help="serial port for real NUCLEO")
    p.add_argument("--drive", nargs=2, type=int, default=[0, 0], metavar=("L", "R"))
    p.add_argument("--seconds", type=float, default=1.0)
    args = p.parse_args(argv)

    drive = (args.drive[0], args.drive[1])
    if args.mock:
        return run_mock_demo(drive, args.seconds)
    return run_port_demo(args.port, drive, args.seconds)


if __name__ == "__main__":
    raise SystemExit(main())
