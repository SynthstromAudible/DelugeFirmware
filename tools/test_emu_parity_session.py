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

    def test_midi_steps_go_to_a_session_that_takes_midi(self):
        fake = FakeSession()
        sent = []
        fake.midi = sent.append
        script = "100 note 2 60 100\n200 cc 16 74 64\n300 snapshot after\n400 quit\n"
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "midi.txt"
            path.write_text(script)
            emu_parity.run_image(
                None,
                None,
                None,
                path,
                Path(tmp) / "out",
                emu_parity.button_table(),
                session=lambda _out: contextlib.nullcontext(fake),
            )
        self.assertEqual(sent, ["91 3c 64", "bf 4a 40"])


class MidiSteps(unittest.TestCase):
    def test_each_step_is_its_channel_message(self):
        cases = {
            ("note", "1 60 100"): "90 3c 64",
            ("noteoff", "16 60"): "8f 3c 00",
            ("cc", "3 74 127"): "b2 4a 7f",
            ("pc", "10 5"): "c9 05",
            ("bend", "1 8192"): "e0 00 40",
            ("bend", "1 16383"): "e0 7f 7f",
            ("midi", "f8"): "f8",
        }
        for (cmd, args), want in cases.items():
            self.assertEqual(
                emu_parity.midi_bytes(cmd, args.split()).hex(" "), want, cmd
            )

    def test_a_data_byte_outside_0_to_127_is_an_error(self):
        for cmd, args in (
            ("note", "1 128 100"),
            ("cc", "1 7 200"),
            ("pc", "1 -1"),
            ("bend", "1 16384"),
        ):
            with self.assertRaises(ValueError, msg=cmd):
                emu_parity.midi_bytes(cmd, args.split())

    def test_a_channel_outside_1_to_16_is_an_error(self):
        for channel in ("0", "17"):
            with self.assertRaises(ValueError):
                emu_parity.midi_bytes("note", [channel, "60", "100"])


def window(*oleds, flashes=()):
    """A snapshot window: one panel per OLED value, the last carrying @p flashes."""
    out = []
    for i, oled in enumerate(oleds):
        out.append(
            {
                "oled": [[oled]],
                "pads": [[[0, 0, 0]]],
                "leds": {"BACK": 0},
                "gold": [0, 0],
                "flashes": list(flashes) if i == len(oleds) - 1 else [],
                "pic": {},
            }
        )
    return out


class Verdict(unittest.TestCase):
    """classify(): MATCH, a PHASE-only difference, or a real DIFF."""

    def test_identical_windows_match(self):
        self.assertEqual(emu_parity.classify(window(1, 2), window(1, 2), None), "MATCH")

    def test_a_shifted_frame_before_the_same_last_panel_is_phase(self):
        self.assertEqual(
            emu_parity.classify(window(1, 2, 3), window(1, 5, 3), None), "PHASE"
        )

    def test_a_different_last_panel_is_a_diff(self):
        self.assertEqual(
            emu_parity.classify(window(1, 2, 3), window(1, 5, 4), None), "DIFF"
        )

    def test_a_different_led_in_the_last_panel_is_a_diff(self):
        test = window(1, 2)
        test[-1]["leds"] = {"BACK": 1}
        self.assertEqual(emu_parity.classify(window(1, 2), test, None), "DIFF")

    def test_pad_flashes_alone_with_equal_last_panels_are_phase(self):
        self.assertEqual(
            emu_parity.classify(
                window(1, flashes=[[0, 4, [0, 0, 255]]]), window(1), None
            ),
            "PHASE",
        )

    def test_a_reference_that_differs_from_itself_makes_it_phase(self):
        self.assertEqual(
            emu_parity.classify(window(1, 2), window(1, 7), window(1, 8)), "PHASE"
        )

    def test_a_reference_that_repeats_itself_leaves_it_a_diff(self):
        self.assertEqual(
            emu_parity.classify(window(1, 2), window(1, 7), window(1, 2)), "DIFF"
        )

    def test_a_test_image_that_differs_from_itself_makes_it_phase(self):
        self.assertEqual(
            emu_parity.classify(window(1, 2), window(1, 7), window(1, 2), window(1, 9)),
            "PHASE",
        )

    def test_both_images_repeating_themselves_leave_it_a_diff(self):
        self.assertEqual(
            emu_parity.classify(window(1, 2), window(1, 7), window(1, 2), window(1, 7)),
            "DIFF",
        )


if __name__ == "__main__":
    unittest.main()
