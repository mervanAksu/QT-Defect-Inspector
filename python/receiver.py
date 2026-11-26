#!/usr/bin/env python3
"""Defect Inspector receiver: reads JSON Lines results over TCP or UDP.

Only the Python standard library is used, so it runs anywhere Python 3.8+ is
installed (e.g. a Raspberry Pi next to the production line) without pip.

    python3 receiver.py                          # TCP, 127.0.0.1:5555
    python3 receiver.py --host 192.168.1.20 --only-defects
    python3 receiver.py --udp 5556               # listen for UDP datagrams
    python3 receiver.py --csv defects.csv        # also log every message to CSV
    python3 receiver.py --count 10               # exit after 10 messages

The output format matches the C++ client (src/receiver/MessageFormat.h).
"""

from __future__ import annotations

import argparse
import csv
import json
import socket
import sys
import time
from datetime import datetime, timezone
from typing import Callable, Iterable, List, Optional, TextIO

PROTOCOL_VERSION = 1
MAX_MESSAGE_BYTES = 64 * 1024
REQUIRED_NUMBERS = ("frame", "defect_count", "processing_ms", "latency_ms", "dropped_frames")


class ProtocolError(ValueError):
    """Raised for a line that is not a valid inspection message."""


def parse_message(raw: bytes) -> dict:
    """Decode and validate one message (the same rules as protocol::decode in C++)."""
    try:
        message = json.loads(raw)
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ProtocolError(f"invalid JSON: {exc}") from exc
    if not isinstance(message, dict):
        raise ProtocolError("message is not a JSON object")
    if message.get("type") != "inspection":
        raise ProtocolError("unknown message type")
    if message.get("version") != PROTOCOL_VERSION:
        raise ProtocolError(f"unsupported protocol version {message.get('version')!r}")
    for key in REQUIRED_NUMBERS:
        value = message.get(key)
        # bool is a subclass of int in Python; "true" must not count as a number.
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ProtocolError(f"missing or non-numeric field '{key}'")
    defects = message.get("defects", [])
    if not isinstance(defects, list) or len(defects) > message["defect_count"]:
        raise ProtocolError("invalid defects list")
    return message


def format_message(message: dict) -> str:
    """One readable line, e.g. '#17     2 defect(s)  largest 292 px² @ (483,295)  latency 2.1 ms'."""
    frame = f"#{message['frame']:<6}"
    latency = f"latency {message['latency_ms']:.1f} ms"
    count = message["defect_count"]
    if count == 0:
        return f"{frame} OK  {latency}"
    detail = f"{count} defect(s)"
    defects = message.get("defects") or []
    if defects:
        largest = max(defects, key=lambda d: d["area"])
        detail += f"  largest {largest['area']:.0f} px² @ ({largest['cx']:.0f},{largest['cy']:.0f})"
    return f"{frame} {detail}  {latency}"


class LineSplitter:
    """Splits a TCP byte stream into '\\n'-terminated lines (same idea as LineBuffer in C++).

    One recv() may return half a message or several messages; only complete lines
    are returned. Lines longer than max_length are dropped so a broken sender cannot
    make us buffer unlimited data.
    """

    def __init__(self, max_length: int = MAX_MESSAGE_BYTES) -> None:
        self._buffer = bytearray()
        self._max_length = max_length
        self._skipping = False
        self.discarded = 0

    def feed(self, data: bytes) -> List[bytes]:
        self._buffer.extend(data)
        lines: List[bytes] = []
        while True:
            newline = self._buffer.find(b"\n")
            if newline < 0:
                break
            line = bytes(self._buffer[:newline])
            del self._buffer[: newline + 1]
            if self._skipping:  # tail of an overlong line
                self._skipping = False
                continue
            if len(line) > self._max_length:
                self.discarded += 1
                continue
            lines.append(line.rstrip(b"\r"))
        if len(self._buffer) > self._max_length:
            if not self._skipping:
                self.discarded += 1
                self._skipping = True
            self._buffer.clear()
        return lines


class Receiver:
    """Handles decoded messages: printing, CSV logging and statistics."""

    CSV_COLUMNS = ("received_at", "frame", "defect_count", "largest_area", "latency_ms",
                   "processing_ms", "dropped_frames")

    def __init__(self, out: TextIO = sys.stdout, err: TextIO = sys.stderr,
                 only_defects: bool = False, csv_file: Optional[TextIO] = None,
                 exit_after: int = 0) -> None:
        self.out = out
        self.err = err
        self.only_defects = only_defects
        self.exit_after = exit_after
        self.received = 0
        self.defective = 0
        self.invalid = 0
        self._csv = None
        if csv_file is not None:
            self._csv = csv.writer(csv_file)
            self._csv.writerow(self.CSV_COLUMNS)
            self._csv_file = csv_file

    def handle(self, raw: bytes) -> bool:
        """Processes one line/datagram. Returns True when --count is reached."""
        if not raw.strip():
            return False
        try:
            message = parse_message(raw)
        except ProtocolError as exc:
            self.invalid += 1
            print(f"invalid message: {exc}", file=self.err, flush=True)
            return False

        self.received += 1
        if message["defect_count"] > 0:
            self.defective += 1
        if not self.only_defects or message["defect_count"] > 0:
            print(format_message(message), file=self.out, flush=True)
        if self._csv is not None:
            areas = [d["area"] for d in message.get("defects") or []]
            self._csv.writerow((
                datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
                message["frame"], message["defect_count"], max(areas, default=0),
                message["latency_ms"], message["processing_ms"], message["dropped_frames"],
            ))
            self._csv_file.flush()
        return self.exit_after > 0 and self.received >= self.exit_after

    def summary(self) -> str:
        return (f"received {self.received} message(s), {self.defective} with defects, "
                f"{self.invalid} invalid")


def run_tcp(host: str, port: int, receiver: Receiver, reconnect: bool = True,
            initial_delay: float = 1.0, max_delay: float = 30.0,
            sleep: Callable[[float], None] = time.sleep) -> None:
    """Connects and reads until --count is reached; reconnects with exponential backoff."""
    delay = initial_delay
    while True:
        try:
            with socket.create_connection((host, port), timeout=5) as sock:
                sock.settimeout(None)  # connected: block on recv() without a timeout
                print("connected", file=receiver.err, flush=True)
                delay = initial_delay  # success: reset the backoff
                splitter = LineSplitter()
                while True:
                    data = sock.recv(4096)
                    if not data:  # server closed the connection
                        break
                    before = splitter.discarded
                    for line in splitter.feed(data):
                        if receiver.handle(line):
                            return
                    receiver.invalid += splitter.discarded - before
                print("disconnected", file=receiver.err, flush=True)
        except OSError as exc:  # refused, unreachable, reset, ...
            print(f"error: {exc}", file=receiver.err, flush=True)
        if not reconnect:
            return
        print(f"reconnecting in {delay:g} s", file=receiver.err, flush=True)
        sleep(delay)
        delay = min(delay * 2, max_delay)


def run_udp(sock: socket.socket, receiver: Receiver) -> None:
    """Reads datagrams from a bound UDP socket; each datagram is one message."""
    while True:
        data, _sender = sock.recvfrom(65535)
        if receiver.handle(data):
            return


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1", help="TCP server address")
    parser.add_argument("--port", type=int, default=5555, help="TCP server port")
    parser.add_argument("--udp", type=int, metavar="PORT", help="listen for UDP datagrams instead")
    parser.add_argument("--only-defects", action="store_true", help="print only frames with defects")
    parser.add_argument("--csv", metavar="FILE", help="append every message to a CSV file")
    parser.add_argument("--count", type=int, default=0, metavar="N", help="exit after N messages")
    args = parser.parse_args(list(argv) if argv is not None else None)

    csv_file = open(args.csv, "w", newline="", encoding="utf-8") if args.csv else None
    receiver = Receiver(only_defects=args.only_defects, csv_file=csv_file, exit_after=args.count)
    try:
        if args.udp is not None:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind(("0.0.0.0", args.udp))
                print(f"listening for UDP on port {args.udp}", file=sys.stderr, flush=True)
                run_udp(sock, receiver)
        else:
            print(f"connecting to {args.host}:{args.port}", file=sys.stderr, flush=True)
            run_tcp(args.host, args.port, receiver)
    except KeyboardInterrupt:  # Ctrl+C: stop quietly and print the summary
        pass
    finally:
        if csv_file is not None:
            csv_file.close()
        print(receiver.summary(), file=sys.stderr, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
