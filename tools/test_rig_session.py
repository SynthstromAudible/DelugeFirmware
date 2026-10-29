#!/usr/bin/env python3
"""Tests for rig_session.py's DeviceSession over a fake RTT link: `python3 -m unittest test_rig_session`."""

import base64
import tempfile
import time
import unittest
from pathlib import Path

from rig_session import FULL_STATE, DeviceSession

LEDS = {0: "PLAY", 9 + 6: "LOAD"}


class FakeLink:
    """Hands out queued byte chunks, one per recv; times out like a socket when none are left."""

    def __init__(self, chunks=()):
        self.chunks = list(chunks)
        self.sent = b""

    def feed(self, text: str):
        self.chunks.append(text.encode())

    def recv(self, _n):
        if not self.chunks:
            time.sleep(0.01)
            raise TimeoutError
        return self.chunks.pop(0)

    def sendall(self, data):
        self.sent += data


def attached(link: FakeLink, origin_us=1_000_000) -> DeviceSession:
    """A session whose resend came back as a ready line at `origin_us`."""
    link.feed(f"@T 0 {origin_us - 10}\n@R 1 {origin_us}\n")
    return DeviceSession(link, led_names=LEDS)


class Decoding(unittest.TestCase):
    def test_decodes_split_lines(self):
        link = FakeLink()
        s = attached(link)
        link.feed("@L 2 1000100 0 ")
        link.feed("1\nINFO  [app] a log line\n@L 3 10")
        link.feed("00100 15 1\n@T 4 1000200\n")
        s.run_until(200_000)
        self.assertEqual(s.read_leds(), {"PLAY": True, "LOAD": True})
        self.assertIn("INFO  [app] a log line", s.log)

    def test_state_follows_changes(self):
        link = FakeLink()
        s = attached(link)
        cols = base64.b64encode(
            bytes(3 * 8) + bytes([10, 11, 12]) + bytes(3 * 7)
        ).decode()
        link.feed(f"@C 2 1000100 1 {cols}\n@G 3 1000100 1 1 2 3 4\n")
        link.feed("@F 4 1000100 13 3\n@M 5 1000100 5 20\n@T 6 1000200\n")
        s.run_until(200_000)
        pads = s.read_pads(flashes=False)
        self.assertEqual((len(pads), len(pads[0])), (18, 8))
        # Pair 1's ninth entry is column 3, row 0.
        self.assertEqual(pads[3][0], (10, 11, 12))
        self.assertEqual(s.read_gold_knob(), [[0, 0, 0, 0], [1, 2, 3, 4]])
        # Pad index 13 = y + x * 8: x 1, y 5.
        self.assertEqual(s.read_pad_flashes(), [(1, 5, 3)])
        self.assertEqual(s.read_pad_flashes(), [])
        self.assertEqual(
            s.read_pic_settings(), {"refresh_time": 20, "dimmer_interval": 5}
        )

    def test_oled_decodes_like_delugemu(self):
        link = FakeLink()
        s = attached(link)
        frame = bytearray(768)
        frame[0] = 0b0000_0001  # page 0, column 0, row 0
        frame[128 + 5] = 0b1000_0000  # page 1, column 5, row 15
        link.feed(
            f"@O 2 1000100 128 48 {base64.b64encode(frame).decode()}\n@T 3 1000200\n"
        )
        s.run_until(200_000)
        rows = s.read_oled()
        self.assertEqual((len(rows), len(rows[0])), (48, 128))
        self.assertTrue(rows[0][0])
        self.assertTrue(rows[15][5])
        self.assertEqual(sum(map(sum, rows)), 2)


class Attaching(unittest.TestCase):
    def test_attach_waits_for_the_app_and_ignores_the_boot_ready_line(self):
        # The input task says ready as the executor starts; the app's start-up then holds the executor.
        link = FakeLink(
            [
                b"@R 0 48000\n@T 1 50000\n",
                b"5.226: deluge.cpp:719: going into main loop\n",
                b"@T 2 5300000\n@R 3 5300100\n",
            ]
        )
        s = DeviceSession(link, led_names=LEDS, ready_log="going into main loop")
        self.assertEqual(s.vclock_ns(), 0)
        self.assertEqual(link.sent, b"S\n")
        link.feed("@T 4 5400100\n")
        s.run_until(100_000_000)

    def test_a_link_dropped_during_boot_is_reopened_without_its_partial_line(self):
        # The J-Link server drops RTT clients until the control block is found; a line cut off by the
        # drop must not be glued to the next connection's bytes.
        first = FakeLink([b"@T 0 40000\n@O 1 40000 128 48 abab", b""])
        second = FakeLink(
            [
                b"5.2: deluge.cpp:719: going into main loop\n@T 7 5300000\n",
                b"@R 8 5300100\n",
            ]
        )
        s = DeviceSession(
            first,
            led_names=LEDS,
            ready_log="going into main loop",
            reconnect=lambda: second,
        )
        self.assertIs(s.link, second)
        self.assertEqual(second.sent, b"S\n")

    def test_a_link_dropped_after_attaching_is_an_error(self):
        link = FakeLink()
        s = attached(link)
        link.chunks.append(b"")
        with self.assertRaises(ConnectionError):
            s.run_until(100_000_000)


class Timing(unittest.TestCase):
    def test_run_until_follows_heartbeats_from_the_attach(self):
        link = FakeLink()
        s = attached(link, origin_us=5_000_000)
        self.assertEqual(s.vclock_ns(), 0)
        link.feed("@T 2 5050000\n@T 3 5100000\n")
        s.run_until(100_000_000)
        self.assertEqual(s.vclock_ns(), 100_000_000)

    def test_dead_link_times_out(self):
        s = attached(FakeLink())
        start = time.monotonic()
        with self.assertRaises(TimeoutError):
            s.run_until(1_000_000_000, timeout=0.5)
        self.assertLess(time.monotonic() - start, 1.5)


class Failures(unittest.TestCase):
    def test_dropped_lines_raise(self):
        link = FakeLink()
        s = attached(link)
        link.feed("@T 2 1000100\n@T 5 1000200\n")
        with self.assertRaisesRegex(RuntimeError, "lost"):
            s.run_until(200_000)

    def test_flash_overflow_raises(self):
        link = FakeLink()
        s = attached(link)
        link.feed("@D 2 1000100 4\n@T 3 1000200\n")
        with self.assertRaisesRegex(RuntimeError, "flash"):
            s.run_until(200_000)

    def test_error_line_raises(self):
        link = FakeLink()
        s = attached(link)
        link.feed("@E 2 1000100 P 3\n")
        with self.assertRaisesRegex(RuntimeError, "P 3"):
            s.run_until(200_000)


class Midi(unittest.TestCase):
    def test_midi_runs_amidi_on_the_probe_host(self):
        s = attached(FakeLink())
        calls = []

        class Done:
            returncode = 0
            stderr = ""

        s.midi_port = "hw:1,0,0"
        s._host = "pi@probe"
        s._ssh = lambda host, script, timeout=60: calls.append((host, script)) or Done()
        s.midi("90 3c 64")
        self.assertEqual(calls, [("pi@probe", "amidi -p hw:1,0,0 -S '90 3c 64'")])

    def test_midi_takes_only_hex_bytes(self):
        s = attached(FakeLink())
        s.midi_port = "hw:1,0,0"
        s._ssh = lambda *a, **k: self.fail("ran a command")
        for bad in ("90 3c; reboot", "90 3cz", "", "9"):
            with self.assertRaises(ValueError):
                s.midi(bad)

    def test_midi_without_a_port_is_an_error(self):
        s = attached(FakeLink())
        with self.assertRaisesRegex(RuntimeError, "port"):
            s.midi("90 3c 64")


class Gestures(unittest.TestCase):
    def test_gestures_become_command_lines(self):
        link = FakeLink()
        s = attached(link)
        s.button(6, 1, True)
        s.pad_event(3, 5, False)
        s.encoder(4, -2)
        self.assertEqual(link.sent, b"S\nB 6 1 1\nP 3 5 0\nE 4 -8\n")


class ReviewFindings(unittest.TestCase):
    def test_lines_lost_before_the_resend_do_not_fail_the_attach(self):
        # Boot-time output the probe never read is not panel state the session relies on.
        resend = "".join(f"@L {8 + i} 1000000 {i} 0\n" for i in range(FULL_STATE))
        link = FakeLink(
            [
                b"@T 0 40000\n@T 7 50000\n",
                (resend + f"@R {8 + FULL_STATE} 1000000\n").encode(),
            ]
        )
        s = DeviceSession(link, led_names=LEDS)
        self.assertEqual(s.vclock_ns(), 0)

    def test_lines_lost_inside_the_resend_fail_the_attach(self):
        resend = "".join(
            f"@L {8 + i} 1000000 {i} 0\n" for i in range(FULL_STATE) if i != 30
        )
        link = FakeLink(
            [b"@T 0 40000\n", (resend + f"@R {8 + FULL_STATE} 1000000\n").encode()]
        )
        with self.assertRaisesRegex(RuntimeError, "resend"):
            DeviceSession(link, led_names=LEDS)

    def test_a_device_reset_is_reported_as_such(self):
        link = FakeLink()
        s = attached(link)
        link.feed("@T 0 30000\n")
        with self.assertRaisesRegex(RuntimeError, "restarted"):
            s.run_until(100_000_000)

    def test_settle_reads_a_refusal_of_the_last_command(self):
        link = FakeLink()
        s = attached(link)
        s.pad_event(3, 5, True)
        link.feed("@E 2 1000100 P 3 5 1\n@T 3 1100000\n")
        with self.assertRaisesRegex(RuntimeError, "refused"):
            s.settle()

    def test_the_midi_port_is_quoted_for_the_remote_shell(self):
        s = attached(FakeLink())
        calls = []

        class Done:
            returncode = 0
            stderr = ""

        s.midi_port = "Deluge MIDI 1"
        s._ssh = lambda host, script, timeout=60: calls.append(script) or Done()
        s.midi("f8")
        self.assertEqual(calls, ["amidi -p 'Deluge MIDI 1' -S 'f8'"])

    def test_a_screenshot_before_any_frame_is_blank(self):
        link = FakeLink()
        s = attached(link)
        link.feed("@O 2 1000100 0 0\n@T 3 1000200\n")
        s.run_until(200_000)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "shot.png"
            s.screenshot(str(path))
            self.assertTrue(path.read_bytes().startswith(b"\x89PNG"))


if __name__ == "__main__":
    unittest.main()
