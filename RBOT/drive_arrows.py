#!/usr/bin/env python3
"""Управление роботом стрелками через RBOT (NUCLEO по USB)."""

from __future__ import annotations

import argparse
import curses
import glob
import sys
import threading
import time

import serial

from rbot.client import RobotClient, RobotHalted


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        raise RuntimeError("Нет /dev/ttyACM* — подключи NUCLEO по USB")
    return ports[0]


def ping_worker(client: RobotClient, stop: threading.Event, hz: float) -> None:
    interval = 1.0 / hz
    while not stop.wait(interval):
        try:
            client.ping()
        except Exception:
            break


def drive_from_key(key: int, speed: int) -> tuple[int, int] | None:
    """Tank drive с зеркальными моторами: вперёд = L+ R-."""
    if key == curses.KEY_UP:
        return speed, -speed
    if key == curses.KEY_DOWN:
        return -speed, speed
    if key == curses.KEY_LEFT:
        return speed, speed
    if key == curses.KEY_RIGHT:
        return -speed, -speed
    if key in (ord(" "), ord("s"), ord("S")):
        return 0, 0
    return None


def run_ui(stdscr, client: RobotClient, speed: int, ping_hz: float, release_s: float) -> None:
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.keypad(True)

    stop_ping = threading.Event()
    ping_thread = threading.Thread(
        target=ping_worker,
        args=(client, stop_ping, ping_hz),
        daemon=True,
    )
    ping_thread.start()

    held = (0, 0)
    sent = (0, 0)
    last_arrow_at = 0.0
    hold_started_at = 0.0
    repeat_grace_s = 0.45
    key_hits = 0
    status = "готов"

    try:
        while True:
            now = time.monotonic()
            idle = now - last_arrow_at if last_arrow_at > 0.0 else 999.0
            grace = repeat_grace_s if key_hits < 2 else release_s
            if last_arrow_at > 0.0 and idle < grace:
                target = held
            else:
                target = (0, 0)

            stdscr.erase()
            stdscr.addstr(0, 0, "RBOT — держи стрелку для движения")
            stdscr.addstr(1, 0, f"L={sent[0]:4d}  R={sent[1]:4d}  speed={speed}")
            stdscr.addstr(2, 0, f"status: {status}")
            stdscr.addstr(4, 0, "↑ вперёд  ↓ назад  ← влево  → вправо (удерживай)")
            stdscr.addstr(5, 0, "Пробел/S — стоп   Q — выход")
            stdscr.refresh()

            key = stdscr.getch()
            if key == ord("q") or key == ord("Q"):
                break

            if key != -1:
                cmd = drive_from_key(key, speed)
                if cmd is not None:
                    if key in (ord(" "), ord("s"), ord("S")):
                        held = (0, 0)
                        last_arrow_at = 0.0
                        hold_started_at = 0.0
                        key_hits = 0
                        target = (0, 0)
                    else:
                        if cmd != held:
                            key_hits = 1
                            hold_started_at = now
                        else:
                            key_hits += 1
                        held = cmd
                        last_arrow_at = now
                        target = cmd

            if target != sent:
                try:
                    ok = client.drive(target[0], target[1])
                    status = "OK" if ok else "NAK/timeout"
                    sent = target
                except serial.SerialException:
                    status = "LINK LOST — выйди (Q) и перезапусти"

            if target == (0, 0) and sent == (0, 0):
                key_hits = 0
                hold_started_at = 0.0

            time.sleep(0.02)
    finally:
        stop_ping.set()
        ping_thread.join(timeout=1.0)
        try:
            client.drive(0, 0)
            client.stop()
        except serial.SerialException:
            pass


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="RBOT arrow-key drive")
    parser.add_argument("--port", default=None, help="serial port, default /dev/ttyACM0")
    parser.add_argument("--speed", type=int, default=50, help="PWM percent (max 60)")
    parser.add_argument("--ping-hz", type=float, default=10.0, help="watchdog ping rate")
    parser.add_argument(
        "--release-ms",
        type=float,
        default=120.0,
        help="пауза без повтора клавиши = отпустил (мс)",
    )
    args = parser.parse_args(argv)

    port = find_port(args.port)
    client = RobotClient(port, pwm_max=60)

    print(f"Порт: {port}", file=sys.stderr)
    print("Сессия: HELLO → ARM → DRIVE 0 0 ...", file=sys.stderr)

    try:
        if not client.session_start():
            print("FAIL: не удалось подключиться (прошивка RBOT?)", file=sys.stderr)
            return 1
        print("OK: ARMED. Стрелки в терминале.", file=sys.stderr)
        curses.wrapper(
            lambda stdscr: run_ui(
                stdscr, client, args.speed, args.ping_hz, args.release_ms / 1000.0
            )
        )
        return 0
    except RobotHalted:
        print("HALTED: ESTOP или fault", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130
    finally:
        client.session_stop()


if __name__ == "__main__":
    raise SystemExit(main())
