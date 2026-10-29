#!/usr/bin/env python3
"""Tests that emu_parity's script runner drives whatever session it is given:
`python3 -m unittest test_emu_parity_session`."""

import contextlib
import tempfile
import unittest
from pathlib import Path

import emu_parity

P01 = emu_parity.REPO / "tests/ui-scripts/parity/p01-views.txt"


class FakeSession:
    """A panel that is lit and still from the start, on a clock that jumps to each stop."""

    def __init__(self):
        self.t = 0
        self.calls = []
        self.q = self

    def execute(self, _cmd):
        return None

    def hmp(self, _cmd):
        return ""

    def vclock_ns(self):
        return self.t

    def run_until(self, ns, timeout=120.0):
        self.t = max(self.t, ns)

    def button(self, x, y, down):
        self.calls.append(("button", x, y, down, self.t))

    def pad_event(self, x, y, down):
        self.calls.append(("pad", x, y, down, self.t))

    def encoder(self, index, detents):
        self.calls.append(("encoder", index, detents, self.t))

    def read_oled(self):
        return [[True] * 128] + [[False] * 128] * 47

    def read_pads(self, flashes=True):
        return [[(1, 2, 3)] * 8 for _ in range(18)]

    def read_leds(self):
        return {"PLAY": False}

    def read_gold_knob(self):
        return [[0] * 4, [0] * 4]

    def read_pad_flashes(self):
        return []

    def read_pic_settings(self):
        return {"refresh_time": 23, "dimmer_interval": 0}

    def screenshot(self, path):
        Path(path).write_bytes(b"")

    def assert_no_aborts(self):
        return None


class SessionSeam(unittest.TestCase):
    def test_run_image_uses_the_given_session(self):
        fake = FakeSession()
        with tempfile.TemporaryDirectory() as tmp:
            snaps = emu_parity.run_image(
                None,
                None,
                None,
                P01,
                Path(tmp),
                emu_parity.button_table(),
                session=lambda _out: contextlib.nullcontext(fake),
            )
        session_xy = emu_parity.button_table()["Session"]
        presses = [c for c in fake.calls if c[0] == "button"]
        # The script's first gesture is Session down at 300 ms after boot, then up at 320.
        self.assertEqual(presses[0][1:4], (*session_xy, True))
        self.assertEqual(presses[1][1:4], (*session_xy, False))
        self.assertEqual(presses[1][4] - presses[0][4], 20_000_000)
        self.assertEqual(list(snaps)[:3], ["boot", "song", "song-again"])


if __name__ == "__main__":
    unittest.main()
