#!/usr/bin/env python3
"""Tests that emu_parity's script runner drives whatever session it is given:
`python3 -m unittest test_emu_parity_session`.

Gate device-parity:
    emu_parity's script runner drives whatever session it is given (delugemu or the rig) through the same steps and
    timings, and device_parity builds delugemu's starting flash from the rig's settings sector, refusing a short or
    all-zero read
"""

import contextlib
import io
import json
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

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

    def test_a_random_colour_pad_reads_lit_or_unlit(self):
        fake = FakeSession()
        fake.read_pads = lambda flashes=True: [
            [(fake.t % 97 + 1, 0, 0)] * 8 for _ in range(18)
        ]
        # The pads change at every sample, so the panel never settles: boot at virtual time 0.
        with (
            tempfile.TemporaryDirectory() as tmp,
            mock.patch.object(emu_parity, "boot", return_value=0),
        ):
            path = Path(tmp) / "random.txt"
            path.write_text("100 random_colour 0 0\n200 snapshot s\n1200 quit\n")
            snaps = emu_parity.run_image(
                None,
                None,
                None,
                path,
                Path(tmp) / "out",
                emu_parity.button_table(),
                session=lambda _out: contextlib.nullcontext(fake),
            )
        self.assertGreater(len({json.dumps(p["pads"][1][0]) for p in snaps["s"]}), 1)
        self.assertEqual(
            {json.dumps(p["pads"][0][0]) for p in snaps["s"]}, {"[1, 1, 1]"}
        )

    def test_a_run_without_din_out_carries_no_din_midi(self):
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
                din_out=False,
            )
        self.assertFalse(any("midi" in p for panels in snaps.values() for p in panels))

    def test_din_midi_is_what_the_image_sent_since_the_previous_window(self):
        fake = FakeSession()
        with (
            tempfile.TemporaryDirectory() as tmp,
            mock.patch.object(
                emu_parity.DinListener,
                "take",
                side_effect=[b"\xf8", b"\xb0\x07\x64", b""],
            ),
        ):
            path = Path(tmp) / "din.txt"
            path.write_text("100 snapshot first\n300 snapshot second\n500 quit\n")
            snaps = emu_parity.run_image(
                None,
                None,
                None,
                path,
                Path(tmp) / "out",
                emu_parity.button_table(),
                session=lambda _out: contextlib.nullcontext(fake),
            )
        # Boot's f8 belongs to no window.
        self.assertEqual({p["midi"] for p in snaps["first"]}, {"b0 07 64"})
        self.assertEqual({p["midi"] for p in snaps["second"]}, {""})

    def _run_script(self, text):
        fake = FakeSession()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.txt"
            path.write_text(text)
            return emu_parity.run_image(
                None,
                None,
                None,
                path,
                Path(tmp) / "out",
                emu_parity.button_table(),
                session=lambda _out: contextlib.nullcontext(fake),
            )

    def test_an_unknown_command_is_refused(self):
        with self.assertRaisesRegex(ValueError, "unknown command 'random_color'"):
            self._run_script("100 random_color 0 0\n200 snapshot s\n")

    def test_a_repeated_snapshot_name_is_refused(self):
        with self.assertRaisesRegex(ValueError, "snapshot 's' twice"):
            self._run_script("100 snapshot s\n200 snapshot s\n")

    def test_every_parity_script_parses(self):
        scripts = [
            path
            for d in ("parity", "parity-card", "parity-songs", "device")
            for path in sorted(
                (emu_parity.REPO / "tests/ui-scripts" / d).rglob("*.txt")
            )
        ]
        self.assertTrue(scripts)
        for path in scripts:
            with self.subTest(path.name):
                emu_parity.check_steps(path, emu_parity.parse_script(path))


class DinOutput(unittest.TestCase):
    """delugemu sends each DIN byte as a datagram of its own, more than a socket's receive queue holds."""

    def test_a_long_gap_of_dense_output_loses_nothing(self):
        with (
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as rx,
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx,
        ):
            rx.bind(("127.0.0.1", 0))
            rx.setblocking(False)
            # Far more datagrams than the receive queue holds, in bursts it does.
            sent = bytes(i % 128 for i in range(5000))
            with emu_parity.DinListener(rx) as din:
                for i, b in enumerate(sent):
                    tx.sendto(bytes([b]), rx.getsockname())
                    if i % 50 == 49:
                        time.sleep(0.002)
                self.assertEqual(din.take(), sent)
                self.assertEqual(din.take(), b"")


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

    def test_a_pad_flash_on_one_image_only_is_a_diff_even_with_equal_last_panels(self):
        self.assertEqual(
            emu_parity.classify(
                window(1, flashes=[[0, 4, [0, 0, 255]]]), window(1), None
            ),
            "DIFF",
        )

    def test_the_same_flashes_at_other_moments_are_phase(self):
        ref = window(1, 2, flashes=[[0, 4, [0, 0, 255]]])
        test = window(1, 2)
        test[0]["flashes"] = [[0, 4, [0, 0, 255]]]
        test[0]["oled"] = [[5]]
        self.assertEqual(emu_parity.classify(ref, test, None), "PHASE")

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

    def test_different_din_midi_out_is_a_diff(self):
        ref, test = window(1), window(1)
        ref[0]["midi"], test[0]["midi"] = "b0 07 64", ""
        self.assertEqual(emu_parity.classify(ref, test, None), "DIFF")
        self.assertIn("DIN MIDI out", "; ".join(emu_parity.differences(ref, test)))

    def test_the_same_din_midi_out_matches(self):
        ref, test = window(1), window(1)
        ref[0]["midi"] = test[0]["midi"] = "b0 07 64"
        self.assertEqual(emu_parity.classify(ref, test, None), "MATCH")


BLANK = "." * 128
NAME = {10: "#" * 20 + "." * 108}


def screen(rows: dict[int, str]) -> list[list[bool]]:
    """A 48-row OLED as delugemu reads it, blank but for `rows` (index -> '#'/'.' text)."""
    return [[c == "#" for c in rows.get(i, BLANK)] for i in range(48)]


def text_rows(text: str, first_row: int = 37) -> dict[int, str]:
    """{row: text} for `text` drawn centred in the 7 px font with its top at `first_row`."""
    line = emu_parity.render_centred(text, emu_parity.font_columns())
    return {first_row + i: row for i, row in enumerate(line)}


def screens(*oleds, pads=0):
    return [
        {
            "oled": o,
            "pads": [[[pads, 0, 0]]],
            "leds": {"BACK": 0},
            "gold": [0, 0],
            "flashes": [],
            "pic": {},
        }
        for o in oleds
    ]


class SectionLine(unittest.TestCase):
    """D77: main draws an output name without its section line until a later redraw; ours draws it at once."""

    def test_the_font_renders_section_1_as_the_firmware_draws_it(self):
        line = emu_parity.render_centred("SECTION 1", emu_parity.font_columns())
        self.assertEqual(line[0][37:42], ".###.")
        self.assertEqual(line[6][85:90], ".###.")

    def test_a_section_line_only_ours_draws_is_d77(self):
        ref = screens(screen(NAME))
        test = screens(screen({**NAME, **text_rows("SECTION 1")}))
        self.assertEqual(emu_parity.classify(ref, test, None), "D77")

    def test_a_section_line_whose_top_rows_a_popup_covers_is_d77(self):
        box = {i: "#" * 128 for i in range(37, 40)}
        drawn = {i: row for i, row in text_rows("SECTION 1").items() if i >= 40}
        ref = screens(screen({**NAME, **box}))
        test = screens(screen({**NAME, **box, **drawn}))
        self.assertEqual(emu_parity.classify(ref, test, None), "D77")

    def test_what_is_not_d77(self):
        drawn = {i: row for i, row in text_rows("SECTION 1").items() if i >= 40}
        cases = {
            "part of the line, nothing covering the rest": (
                screen(NAME),
                screen({**NAME, **drawn}),
            ),
            "other text in the line's rows": (
                screen(NAME),
                screen({**NAME, **text_rows("SECTOR 1")}),
            ),
            "the line off its rows": (
                screen(NAME),
                screen({**NAME, **text_rows("SECTION 1", first_row=38)}),
            ),
            "no name above the line": (screen({}), screen(text_rows("SECTION 1"))),
            "main draws something there": (
                screen({**NAME, 40: "#" + "." * 127}),
                screen({**NAME, 40: "##" + "." * 126}),
            ),
            "outside the line": (screen(NAME), screen({**NAME, 20: "#" + "." * 127})),
        }
        for what, (ref, test) in cases.items():
            with self.subTest(what):
                self.assertEqual(
                    emu_parity.classify(screens(ref), screens(test), None), "DIFF"
                )

    def test_a_window_whose_other_panel_differs_elsewhere_is_not_d77(self):
        ref = screens(screen(NAME), screen({**NAME, 2: "#" + "." * 127}))
        test = screens(
            screen({**NAME, **text_rows("SECTION 1")}),
            screen({**NAME, 2: ".#" + "." * 126}),
        )
        self.assertEqual(emu_parity.classify(ref, test, None), "DIFF")

    def test_a_window_missing_a_frame_main_shows_is_not_d77(self):
        ref = screens(screen(NAME), screen({**NAME, 2: "#" + "." * 127}))
        for what, test in (
            ("main's frames less one", screens(screen(NAME))),
            (
                "with the line, less one",
                screens(screen({**NAME, **text_rows("SECTION 1")})),
            ),
        ):
            with self.subTest(what):
                self.assertEqual(emu_parity.classify(ref, test, None), "DIFF")

    def test_a_section_line_with_a_frame_caught_at_another_moment_is_d77(self):
        # Without the line, ours ends the window as main does: PHASE. With it, D77.
        ref = screens(screen({2: "#" + "." * 127}), screen(NAME))
        test = screens(screen({**NAME, **text_rows("SECTION 1")}))
        self.assertEqual(emu_parity.classify(ref, test, None), "D77")

    def test_a_difference_beyond_the_oled_is_not_d77(self):
        ref = screens(screen(NAME))
        test = screens(screen({**NAME, **text_rows("SECTION 1")}), pads=9)
        self.assertEqual(emu_parity.classify(ref, test, None), "DIFF")


class Legs(unittest.TestCase):
    def _script(self, d, name, *cards):
        path = Path(d) / name
        path.write_text(
            "".join(f"# card: {c}\n" for c in cards) + "100 snapshot boot\n"
        )
        return path

    def test_scripts_run_on_each_header_card_grouped_in_first_seen_order(self):
        with tempfile.TemporaryDirectory() as d:
            a = self._script(d, "p01-a.txt", "content/bare.img", "tests/fixtures/card")
            b = self._script(d, "p02-b.txt", "tests/fixtures/card")
            got = emu_parity.legs([a, b], None, None)
        self.assertEqual(
            [(label, s) for label, _, s in got],
            [("content-bare", [a]), ("card", [a, b])],
        )
        self.assertEqual(got[1][1], emu_parity.REPO / "tests/fixtures/card")

    def test_an_explicit_card_runs_every_script_on_it(self):
        with tempfile.TemporaryDirectory() as d:
            a = self._script(d, "p01-a.txt", "content/bare.img")
            b = self._script(d, "p02-b.txt")
            got = emu_parity.legs([a, b], None, Path("/x/sweep.img"))
        self.assertEqual(got, [("sweep", Path("/x/sweep.img"), [a, b])])

    def test_a_script_without_a_header_is_an_error(self):
        with (
            tempfile.TemporaryDirectory() as d,
            self.assertRaisesRegex(ValueError, "no `# card:` header"),
        ):
            emu_parity.legs([self._script(d, "p01-a.txt")], None, None)

    def test_a_directory_is_its_scripts_in_name_order(self):
        with tempfile.TemporaryDirectory() as d:
            b, a = self._script(d, "p02-b.txt"), self._script(d, "p01-a.txt")
            (Path(d) / "README.md").write_text("")
            self.assertEqual(emu_parity.expand([Path(d)]), [a, b])

    def test_one_script_on_two_cards_runs_on_two_monitor_sockets(self):
        a = emu_parity.qmp_socket(Path("/runs/card-a/p01-views/ref"))
        b = emu_parity.qmp_socket(Path("/runs/card-b/p01-views/ref"))
        self.assertNotEqual(a, b)
        self.assertEqual(a, emu_parity.qmp_socket(Path("/runs/card-a/p01-views/ref")))
        # A Unix socket's path is at most 107 bytes.
        self.assertLess(len(emu_parity.qmp_socket(Path("/" + "x" * 300))), 108)

    def test_two_cards_with_one_label_are_refused(self):
        with (
            tempfile.TemporaryDirectory() as d,
            self.assertRaisesRegex(ValueError, "card: one label for"),
        ):
            a = self._script(d, "p01-a.txt", "tests/fixtures/card")
            b = self._script(d, "p02-b.txt", f"{d}/card")
            emu_parity.legs([a, b], None, None)

    def test_an_image_is_its_own_sd(self):
        self.assertEqual(
            emu_parity.sd_image(Path("/x/a.img"), Path("/unused"), Path("/unused")),
            (Path("/x/a.img"), False),
        )


class Main(unittest.TestCase):
    """main(): usage errors exit 4, and the SD images it builds go, whatever the run does."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.d = Path(tmp.name)
        for name in ("ref.elf", "test.elf"):
            (self.d / name).write_bytes(b"")
        self.script = self.d / "p01-a.txt"
        self.script.write_text("100 snapshot boot\n")
        self.out = self.d / "out"

    def main(self, *extra, ref="ref.elf", test="test.elf", script=None):
        argv = [
            "emu_parity.py",
            "--ref",
            str(self.d / ref),
            "--test",
            str(self.d / test),
            "--delugemu",
            str(self.d),
            "--out",
            str(self.out),
            *extra,
            str(script or self.script),
        ]
        err = io.StringIO()
        with (
            mock.patch("sys.argv", argv),
            contextlib.redirect_stderr(err),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            return emu_parity.main(), err.getvalue()

    def made(self, label):
        """An sd_image that builds the image and its card tree, as mksd.sh and card-manifest do."""

        def build(card, at, delugemu):
            at.parent.mkdir(parents=True, exist_ok=True)
            at.write_bytes(b"img")
            at.with_suffix(".tree").mkdir()
            (at.with_suffix(".tree") / "SONGS").write_text("")
            return at, True

        return build

    def legs(self, *labels):
        return mock.patch.object(
            emu_parity,
            "legs",
            return_value=[(label, self.d / label, [self.script]) for label in labels],
        )

    def test_a_missing_image_under_test_is_a_usage_error(self):
        code, err = self.main(test="absent.elf")
        self.assertEqual(code, 4)
        self.assertIn("no image under test", err)

    def test_a_missing_script_is_a_usage_error(self):
        code, err = self.main(script=self.d / "p99-absent.txt")
        self.assertEqual(code, 4)
        self.assertIn("no such script", err)

    def test_a_card_that_does_not_image_is_a_usage_error_and_leaves_no_image(self):
        failing = self.made("a")

        def sd_image(card, at, delugemu):
            if card.name == "b":
                raise subprocess.CalledProcessError(1, ["mksd.sh"])
            return failing(card, at, delugemu)

        with self.legs("a", "b"), mock.patch.object(emu_parity, "sd_image", sd_image):
            code, err = self.main()
        self.assertEqual(code, 4)
        self.assertIn("b", err)
        self.assertEqual(list((self.out / "cards").iterdir()), [])

    def test_the_built_images_go_when_a_run_is_interrupted(self):
        with (
            self.legs("a"),
            mock.patch.object(emu_parity, "sd_image", self.made("a")),
            mock.patch.object(emu_parity, "run_image", side_effect=KeyboardInterrupt),
            self.assertRaises(KeyboardInterrupt),
        ):
            self.main()
        self.assertEqual(list((self.out / "cards").iterdir()), [])

    def test_the_built_images_and_trees_go_after_a_run(self):
        with (
            self.legs("a"),
            mock.patch.object(emu_parity, "sd_image", self.made("a")),
            mock.patch.object(emu_parity, "run_image", return_value={}),
        ):
            code, _ = self.main()
        self.assertEqual(code, 0)
        self.assertEqual(list((self.out / "cards").iterdir()), [])


if __name__ == "__main__":
    unittest.main()
