"""Tests for RBOT protocol and mock integration."""

from __future__ import annotations

import time

from rbot.client import RobotClient
from rbot.fakeserial import make_linked_pair
from rbot.logger import EventLogger
from rbot.mock_stm32 import MockStm32
from rbot.protocol import build_frame, parse_line, xor_checksum


def _run_with_pair(fn):
    ser_client, ser_mock = make_linked_pair()
    mock = MockStm32()
    mock.start(ser_mock)
    log = EventLogger(sink=lambda e: None)
    client = RobotClient("fake", logger=log)
    client._ser = ser_client
    client.link = client.link.__class__.UP
    try:
        fn(client, mock)
    finally:
        client.session_stop()
        mock.stop()
        ser_client.close()
        ser_mock.close()


def test_checksum_known():
    body = "0001:HELLO:proto=1"
    frame = build_frame(1, "HELLO", "proto=1")
    assert frame.endswith("\n")
    assert xor_checksum(body)


def test_parse_rbot_frame():
    line = build_frame(42, "DRIVE", "-30", "40").strip()
    f = parse_line(line)
    assert f and f.seq == 42 and f.cmd == "DRIVE"


def test_rejects_plain_line():
    assert parse_line("L 30 R -30") is None
    assert parse_line("DRIVE 30 30") is None
    assert parse_line("ARM") is None


def test_mock_session():
    def body(client, mock):
        assert client.connect_session()
        assert client.ping()
        assert client.drive(20, 20)
        assert client.drive(0, 0)
        assert client.disarm()

    _run_with_pair(body)


def test_drive_not_armed():
    def body(client, mock):
        assert client.handshake()
        assert not client.drive(10, 10)

    _run_with_pair(body)


def test_watchdog_disarms():
    ser_client, ser_mock = make_linked_pair()
    mock = MockStm32(wd_ms=200)
    mock.start(ser_mock)
    log = EventLogger(sink=lambda e: None)
    client = RobotClient("fake", logger=log)
    client._ser = ser_client
    client.link = client.link.__class__.UP
    try:
        client.connect_session()
        client.drive(30, 30)
        time.sleep(0.35)
        assert mock.state.value == "DISARMED"
        assert mock.left == 0
    finally:
        client.session_stop()
        mock.stop()
        ser_client.close()
        ser_mock.close()
