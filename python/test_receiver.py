"""Unit and socket tests for receiver.py (standard library unittest).

Run:  python3 -m unittest -v test_receiver      (from the python/ folder)
"""

import io
import json
import socket
import threading
import unittest

import receiver as rx


def make_message(frame=1, defects=0, **overrides):
    message = {
        "type": "inspection", "version": 1, "frame": frame, "sent_at": "2026-11-26T15:29:16.055Z",
        "defect_count": defects, "processing_ms": 1.8, "latency_ms": 2.06, "dropped_frames": 0,
        "image": {"width": 640, "height": 480},
        "defects": [{"x": 100 * i, "y": 10, "width": 20, "height": 20,
                     "cx": 100 * i + 10, "cy": 20, "area": 100 * (i + 1)} for i in range(defects)],
    }
    message.update(overrides)
    return message


def encode(message):
    return json.dumps(message, separators=(",", ":")).encode() + b"\n"


class ParseMessageTest(unittest.TestCase):
    def test_valid_message(self):
        message = rx.parse_message(encode(make_message(frame=7, defects=2)))
        self.assertEqual(message["frame"], 7)
        self.assertEqual(len(message["defects"]), 2)

    def test_rejects_invalid_messages(self):
        cases = {
            "not json": b"hello",
            "array": b"[1, 2]",
            "wrong type": encode(make_message(type="other")),
            "future version": encode(make_message(version=2)),
            "missing frame": encode({k: v for k, v in make_message().items() if k != "frame"}),
            "frame is text": encode(make_message(frame="1")),
            "bool is not a number": encode(make_message(latency_ms=True)),
            "more defects than count": encode(make_message(defects=1, defect_count=0)),
            "not utf-8": b"\xff\xfe",
        }
        for name, raw in cases.items():
            with self.subTest(name):
                with self.assertRaises(rx.ProtocolError):
                    rx.parse_message(raw)


class FormatMessageTest(unittest.TestCase):
    # Same expected strings as formatMessageMatchesPythonClient in tests/test_receiver.cpp.
    def test_clean_frame(self):
        self.assertEqual(rx.format_message(make_message(frame=18)), "#18     OK  latency 2.1 ms")

    def test_defective_frame_shows_largest_defect(self):
        self.assertEqual(rx.format_message(make_message(frame=17, defects=2)),
                         "#17     2 defect(s)  largest 200 px² @ (110,20)  latency 2.1 ms")


class LineSplitterTest(unittest.TestCase):
    def test_multiple_and_partial_lines(self):
        splitter = rx.LineSplitter()
        self.assertEqual(splitter.feed(b"one\ntwo\nthr"), [b"one", b"two"])
        self.assertEqual(splitter.feed(b"ee\n"), [b"three"])

    def test_byte_by_byte(self):
        splitter = rx.LineSplitter()
        data = encode(make_message(frame=5))
        lines = []
        for i in range(len(data)):
            lines += splitter.feed(data[i:i + 1])
        self.assertEqual(len(lines), 1)
        self.assertEqual(rx.parse_message(lines[0])["frame"], 5)

    def test_strips_carriage_return(self):
        self.assertEqual(rx.LineSplitter().feed(b"windows\r\n"), [b"windows"])

    def test_discards_overlong_lines(self):
        splitter = rx.LineSplitter(max_length=10)
        self.assertEqual(splitter.feed(b"a" * 20), [])
        self.assertEqual(splitter.discarded, 1)
        self.assertEqual(splitter.feed(b"aaa\nok\n"), [b"ok"])
        self.assertEqual(splitter.feed(b"0123456789AB\nfine\n"), [b"fine"])
        self.assertEqual(splitter.discarded, 2)


class ReceiverTest(unittest.TestCase):
    def test_counts_prints_and_writes_csv(self):
        out, err, csv_file = io.StringIO(), io.StringIO(), io.StringIO()
        receiver = rx.Receiver(out=out, err=err, only_defects=True, csv_file=csv_file)

        receiver.handle(encode(make_message(frame=1)))
        receiver.handle(encode(make_message(frame=2, defects=2)))
        receiver.handle(b"garbage")
        receiver.handle(b"   ")  # empty lines are ignored, not invalid

        self.assertEqual((receiver.received, receiver.defective, receiver.invalid), (2, 1, 1))
        self.assertEqual(out.getvalue().count("\n"), 1)  # only the defective frame printed
        self.assertIn("invalid message", err.getvalue())
        rows = csv_file.getvalue().strip().splitlines()
        self.assertEqual(rows[0].split(","), list(rx.Receiver.CSV_COLUMNS))
        self.assertEqual(rows[2].split(",")[1:4], ["2", "2", "200"])

    def test_handle_signals_when_count_reached(self):
        receiver = rx.Receiver(out=io.StringIO(), exit_after=2)
        self.assertFalse(receiver.handle(encode(make_message(frame=1))))
        self.assertTrue(receiver.handle(encode(make_message(frame=2))))


class FakeServer:
    """TCP server that serves one 'session' (a list of byte chunks) per connection."""

    def __init__(self, sessions):
        self.sock = socket.create_server(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, args=(sessions,), daemon=True)
        self.thread.start()

    def _serve(self, sessions):
        with self.sock:
            for chunks in sessions:
                conn, _ = self.sock.accept()
                with conn:
                    for chunk in chunks:
                        conn.sendall(chunk)


class TcpTest(unittest.TestCase):
    def test_reads_messages_split_across_chunks(self):
        data = b"".join(encode(make_message(frame=i, defects=i % 2)) for i in range(3))
        chunks = [data[:10], data[10:150], data[150:]]  # boundaries in the middle of messages
        server = FakeServer([chunks])
        out = io.StringIO()
        receiver = rx.Receiver(out=out, err=io.StringIO(), exit_after=3)

        rx.run_tcp("127.0.0.1", server.port, receiver, reconnect=False)

        self.assertEqual(receiver.received, 3)
        self.assertEqual(receiver.defective, 1)
        self.assertTrue(out.getvalue().startswith("#0 "))

    def test_reconnects_with_exponential_backoff(self):
        # First connection: one message, then the server closes it.
        # Second connection: two more messages.
        server = FakeServer([[encode(make_message(frame=0))],
                             [encode(make_message(frame=1)), encode(make_message(frame=2))]])
        delays = []
        receiver = rx.Receiver(out=io.StringIO(), err=io.StringIO(), exit_after=3)

        rx.run_tcp("127.0.0.1", server.port, receiver, initial_delay=0.01,
                   sleep=lambda s: delays.append(s))

        self.assertEqual(receiver.received, 3)
        self.assertEqual(delays, [0.01])  # one reconnect, with the initial delay

    def test_backoff_doubles_up_to_the_maximum_while_server_is_down(self):
        probe = socket.create_server(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()  # nobody listens on this port now
        delays = []

        def fake_sleep(seconds):
            delays.append(seconds)
            if len(delays) == 5:
                raise KeyboardInterrupt  # stop the endless retry loop

        receiver = rx.Receiver(out=io.StringIO(), err=io.StringIO())
        with self.assertRaises(KeyboardInterrupt):
            rx.run_tcp("127.0.0.1", port, receiver, initial_delay=1, max_delay=4, sleep=fake_sleep)
        self.assertEqual(delays, [1, 2, 4, 4, 4])


class UdpTest(unittest.TestCase):
    def test_each_datagram_is_one_message(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as rx_sock, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx_sock:
            rx_sock.bind(("127.0.0.1", 0))
            rx_sock.settimeout(3)
            receiver = rx.Receiver(out=io.StringIO(), err=io.StringIO(), exit_after=2)
            for frame in (10, 11):
                tx_sock.sendto(encode(make_message(frame=frame, defects=1)), rx_sock.getsockname())

            rx.run_udp(rx_sock, receiver)

            self.assertEqual((receiver.received, receiver.defective), (2, 2))


if __name__ == "__main__":
    unittest.main()
