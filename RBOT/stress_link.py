#!/usr/bin/env python3
"""Стресс-тест связи Pi↔NUCLEO без браузера."""

from __future__ import annotations

import argparse
import glob
import sys
import threading
import time

from rbot.client import RobotClient


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        raise RuntimeError("Нет /dev/ttyACM*")
    return ports[0]


def ping_worker(client: RobotClient, stop: threading.Event, hz: float) -> None:
    interval = 1.0 / hz
    while not stop.wait(interval):
        client.ping()


def main() -> int:
    parser = argparse.ArgumentParser(description="RBOT UART stress test")
    parser.add_argument("--port", default=None)
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--drive-hz", type=float, default=10.0)
    parser.add_argument("--ping-hz", type=float, default=5.0)
    parser.add_argument("--speed", type=int, default=40)
    args = parser.parse_args()

    port = find_port(args.port)
    client = RobotClient(port)
    if not client.session_start():
        print("session_start FAILED", file=sys.stderr)
        return 1

    stop = threading.Event()
    ping_thread = threading.Thread(
        target=ping_worker, args=(client, stop, args.ping_hz), daemon=True
    )
    ping_thread.start()

    patterns = [
        (args.speed, -args.speed),
        (-args.speed, args.speed),
        (args.speed, args.speed),
        (-args.speed, -args.speed),
        (0, 0),
    ]
    interval = 1.0 / args.drive_hz
    deadline = time.monotonic() + args.seconds
    sent = ok = fail = 0

    print(f"stress {args.seconds}s on {port} (drive {args.drive_hz}Hz + ping {args.ping_hz}Hz)")
    try:
        i = 0
        while time.monotonic() < deadline:
            left, right = patterns[i % len(patterns)]
            sent += 1
            if client.drive(left, right):
                ok += 1
            else:
                fail += 1
                print(f"FAIL #{fail} at t={args.seconds - (deadline - time.monotonic()):.1f}s L={left} R={right}")
                if client.reconnect():
                    print("  reconnect OK")
                else:
                    print("  reconnect FAILED — stop")
                    return 2
            i += 1
            time.sleep(interval)
    finally:
        stop.set()
        ping_thread.join(timeout=1.0)
        client.session_stop()

    print(f"done: sent={sent} ok={ok} fail={fail}")
    return 0 if fail == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
