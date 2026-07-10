#!/usr/bin/env python3
"""Управление роботом из браузера через RBOT (hold-to-drive)."""

from __future__ import annotations

import argparse
import atexit
import glob
import signal
import sys
import threading
import time

from flask import Flask, jsonify, request

from rbot.client import RobotClient, RobotHalted

DIRS = ("forward", "back", "left", "right", "stop")

app = Flask(__name__)
client: RobotClient | None = None
speed = 40
last_left = 0
last_right = 0
link_ok = False
desired_dir = "stop"
desired_lock = threading.Lock()
motion_stop = threading.Event()
motion_thread: threading.Thread | None = None
_shutdown_done = False


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        raise RuntimeError("Нет /dev/ttyACM* — подключи NUCLEO по USB")
    return ports[0]


def drive_from_dir(direction: str, pwm: int) -> tuple[int, int]:
    """Tank drive с зеркальными моторами: вперёд = L+ R-."""
    if direction == "forward":
        return pwm, -pwm
    if direction == "back":
        return -pwm, pwm
    if direction == "left":
        return pwm, pwm
    if direction == "right":
        return -pwm, -pwm
    return 0, 0


def motion_worker(hz: float, keepalive_s: float) -> None:
    """Один поток на UART: без очереди HTTP-запросов."""
    global last_left, last_right, link_ok

    interval = 1.0 / hz
    last_sent: tuple[int, int] | None = None
    last_tx = 0.0

    while not motion_stop.wait(interval):
        if client is None or client._halted:
            link_ok = False
            continue

        with desired_lock:
            direction = desired_dir
        left, right = drive_from_dir(direction, speed)
        now = time.monotonic()
        changed = last_sent != (left, right)
        stale = (now - last_tx) >= keepalive_s
        if not changed and not stale:
            continue

        try:
            ok = client.drive(left, right)
        except RobotHalted:
            link_ok = False
            break
        except Exception:
            ok = False

        if ok:
            last_sent = (left, right)
            last_tx = now
            last_left, last_right = left, right
            link_ok = True
            continue

        if client.reconnect():
            last_sent = None
            last_tx = 0.0
            link_ok = True
        else:
            link_ok = False


def start_motion(hz: float, keepalive_s: float) -> None:
    global motion_thread
    motion_stop.clear()
    motion_thread = threading.Thread(
        target=motion_worker,
        args=(hz, keepalive_s),
        daemon=True,
    )
    motion_thread.start()


def stop_motion() -> None:
    motion_stop.set()
    if motion_thread and motion_thread.is_alive():
        motion_thread.join(timeout=2.0)


def shutdown() -> None:
    global _shutdown_done
    if _shutdown_done:
        return
    _shutdown_done = True
    stop_motion()
    if client is not None:
        try:
            client.stop()
        except Exception:
            pass
        client.session_stop()


HTML = """<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>Robot Drive</title>
  <style>
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body {
      margin: 0; min-height: 100dvh; display: flex; flex-direction: column;
      align-items: center; justify-content: center; gap: 1rem;
      font-family: system-ui, sans-serif; background: #111; color: #eee;
      touch-action: none; user-select: none;
    }
    h1 { margin: 0; font-size: 1.1rem; font-weight: 600; opacity: 0.8; }
    #status { font-size: 0.85rem; opacity: 0.7; min-height: 1.2em; }
    .pad {
      display: grid; grid-template-columns: repeat(3, 5.5rem);
      grid-template-rows: repeat(3, 5.5rem); gap: 0.5rem;
    }
    .btn {
      border: none; border-radius: 1rem; background: #2a2a2a; color: #fff;
      font-size: 1.6rem; cursor: pointer; box-shadow: 0 2px 0 #000;
    }
    .btn:active, .btn.active { background: #3d7a3d; transform: translateY(1px); }
    .btn.stop { background: #5a2a2a; font-size: 0.9rem; }
    .btn.stop:active, .btn.stop.active { background: #8a3030; }
    .btn.estop { background: #7a2020; font-size: 0.75rem; grid-column: 1 / -1; }
    .empty { visibility: hidden; pointer-events: none; }
    .hint { font-size: 0.75rem; opacity: 0.5; max-width: 18rem; text-align: center; }
  </style>
</head>
<body>
  <h1>Robot Drive</h1>
  <div id="status">…</div>
  <div class="pad">
    <div class="empty"></div>
    <button class="btn" data-dir="forward">↑</button>
    <div class="empty"></div>
    <button class="btn" data-dir="left">←</button>
    <button class="btn stop" data-dir="stop">STOP</button>
    <button class="btn" data-dir="right">→</button>
    <div class="empty"></div>
    <button class="btn" data-dir="back">↓</button>
    <div class="empty"></div>
    <button class="btn estop" id="estop">E-STOP</button>
  </div>
  <p class="hint">Удерживай кнопку направления. Отпусти — стоп.</p>
  <script>
    const statusEl = document.getElementById('status');
    let activeDir = null;

    async function api(path, body) {
      const opts = body
        ? { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body) }
        : {};
      const r = await fetch(path, opts);
      return r.json();
    }

    async function refreshStatus() {
      try {
        const s = await api('/api/status');
        statusEl.textContent = s.ok
          ? `ARMED · L${s.left} R${s.right}`
          : `Ошибка: ${s.error || 'нет связи'}`;
      } catch (e) {
        statusEl.textContent = 'Нет связи с Pi';
      }
    }

    async function sendDrive(dir) {
      try {
        const s = await api('/api/drive', { dir });
        if (!s.ok) statusEl.textContent = 'Нет связи с STM32';
      } catch (e) {
        statusEl.textContent = 'Ошибка drive';
      }
    }

    function setActive(btn) {
      document.querySelectorAll('.btn[data-dir]').forEach(b => b.classList.remove('active'));
      if (btn) btn.classList.add('active');
    }

    function startDrive(dir, btn) {
      if (activeDir === dir) return;
      activeDir = dir;
      setActive(btn);
      sendDrive(dir);
    }

    function stopDrive() {
      activeDir = null;
      setActive(null);
      sendDrive('stop');
    }

    document.querySelectorAll('.btn[data-dir]').forEach(btn => {
      const dir = btn.dataset.dir;
      const down = (e) => { e.preventDefault(); startDrive(dir, btn); };
      const up = (e) => { e.preventDefault(); if (activeDir === dir) stopDrive(); };
      btn.addEventListener('pointerdown', down);
      btn.addEventListener('pointerup', up);
      btn.addEventListener('pointerleave', up);
      btn.addEventListener('pointercancel', up);
    });

    document.getElementById('estop').addEventListener('click', async () => {
      stopDrive();
      await api('/api/estop', { reason: 'web' });
      statusEl.textContent = 'E-STOP';
    });

    setInterval(refreshStatus, 1000);
    refreshStatus();
  </script>
</body>
</html>
"""


@app.get("/")
def index():
    return HTML


@app.get("/api/status")
def api_status():
    if client is None:
        return jsonify(ok=False, error="client not ready")
    armed = client.link.value == "UP" and client._state == "ARMED" and not client._halted
    return jsonify(
        ok=armed and link_ok,
        link=client.link.value,
        state=client._state,
        halted=client._halted,
        left=last_left,
        right=last_right,
        error=None if link_ok else "stm32",
    )


@app.post("/api/drive")
def api_drive():
    global desired_dir
    if client is None:
        return jsonify(ok=False, error="no client"), 503
    data = request.get_json(silent=True) or {}
    direction = data.get("dir", "stop")
    if direction not in DIRS:
        return jsonify(ok=False, error="bad dir"), 400
    with desired_lock:
        desired_dir = direction
    left, right = drive_from_dir(direction, speed)
    return jsonify(ok=link_ok, left=left, right=right)


@app.post("/api/estop")
def api_estop():
    global desired_dir
    if client is None:
        return jsonify(ok=False), 503
    with desired_lock:
        desired_dir = "stop"
    data = request.get_json(silent=True) or {}
    client.estop(data.get("reason", "web"))
    return jsonify(ok=True)


def main() -> int:
    global client, speed, link_ok

    parser = argparse.ArgumentParser(description="RBOT web drive")
    parser.add_argument("--port", help="Serial port (default: first /dev/ttyACM*)")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--speed", type=int, default=40, help="PWM 0..60")
    parser.add_argument("--drive-hz", type=float, default=10.0, help="частота DRIVE на UART")
    parser.add_argument(
        "--keepalive-ms",
        type=float,
        default=350.0,
        help="повтор DRIVE для watchdog STM32 (мс)",
    )
    args = parser.parse_args()

    speed = max(0, min(60, args.speed))
    port = find_port(args.port)

    client = RobotClient(port)
    if not client.session_start():
        print("RBOT session failed — проверь NUCLEO и порт", file=sys.stderr)
        client.session_stop()
        return 1

    link_ok = True
    start_motion(args.drive_hz, args.keepalive_ms / 1000.0)
    atexit.register(shutdown)

    def on_signal(_signum, _frame):
        shutdown()
        sys.exit(0)

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    print(f"Web drive: http://{args.host}:{args.http_port}/  (serial {port})")
    try:
        app.run(host=args.host, port=args.http_port, threaded=True, use_reloader=False)
    finally:
        shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
