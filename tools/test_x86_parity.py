#!/usr/bin/env python3
"""Tests for x86_parity: `python3 -m unittest test_x86_parity` (from tools/).

Gate x86-parity-tool:
    x86_parity turns a parity script into the host app's UI script, keys its card cache by the image's mtime, and
    lists every file two runs disagree on
"""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import x86_parity as xp
from emu_parity import midi_bytes


class TempTest(unittest.TestCase):
    def tmp(self) -> Path:
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        return Path(d.name)

    def script(self, text: str) -> Path:
        path = self.tmp() / "p99-x.txt"
        path.write_text(text)
        return path

    def host_lines(self, text: str) -> list[str]:
        return xp.translate(self.script(text))[0].splitlines()


class TranslateTest(TempTest):
    def test_times_shift_by_the_boot_and_a_quit_follows_the_last_step(self):
        self.assertEqual(
            self.host_lines("100 snapshot boot\n300 button Play down\n"),
            ["5100 snapshot boot", "5300 button Play down", "5400 quit"],
        )

    def test_a_load_expands_to_the_presses_emu_parity_makes(self):
        lines = self.host_lines("100 load AB\n")
        x_a, y_a = xp.KEYS["A"]
        x_b, y_b = xp.KEYS["B"]
        self.assertEqual(
            lines[:-1],
            [
                "5100 button Load down",
                "5120 button Load up",
                f"6600 pad {x_a} {y_a} down",
                f"6640 pad {x_a} {y_a} up",
                f"6750 pad {x_b} {y_b} down",
                f"6790 pad {x_b} {y_b} up",
                "7700 button SelectEnc down",
                "7720 button SelectEnc up",
            ],
        )
        times = [int(line.split()[0]) for line in lines]
        self.assertEqual(times, sorted(times))

    def test_a_long_turn_is_split_into_turns_the_runner_takes(self):
        self.assertEqual(
            self.host_lines("100 encoder Select +40\n")[:2],
            ["5100 encoder Select +31", "5110 encoder Select +9"],
        )

    def test_random_colour_is_dropped_and_its_pad_masked(self):
        text, masked, _ = xp.translate(
            self.script("100 random_colour 3 4\n200 snapshot a\n")
        )
        self.assertNotIn("random_colour", text)
        self.assertIn((3, 4), masked)

    def test_a_midi_step_becomes_its_bytes_in_hex(self):
        self.assertEqual(midi_bytes("cc", ["1", "74", "64"]), bytes([0xB0, 0x4A, 0x40]))
        self.assertEqual(self.host_lines("100 cc 1 74 64\n")[0], "5100 midi b04a40")

    def test_the_steps_emu_parity_passes_over_are_dropped(self):
        lines = self.host_lines(
            "100 model ClipContent 3\n200 notice Error 248\n300 released\n400 snapshot a\n"
        )
        self.assertEqual(lines, ["5400 snapshot a", "5500 quit"])

    def test_an_unknown_command_or_a_repeated_snapshot_is_refused_with_the_script_name(
        self,
    ):
        with self.assertRaisesRegex(
            ValueError, "p99-x.txt: unknown command 'frobnicate'"
        ):
            xp.translate(self.script("100 frobnicate\n"))
        with self.assertRaisesRegex(ValueError, "snapshot 'a' twice"):
            xp.translate(self.script("100 snapshot a\n200 snapshot a\n"))

    def test_a_load_name_the_pad_keyboard_cannot_type_is_refused(self):
        with self.assertRaisesRegex(ValueError, "cannot type '_'"):
            xp.translate(self.script("100 load A_B\n"))

    def test_the_deadline_grows_with_the_script_and_has_a_floor(self):
        self.assertEqual(
            xp.deadline_s(0),
            xp.DEADLINE_FLOOR_S + xp.DEADLINE_FACTOR * xp.BUDGET_MARGIN_MS // 1000,
        )
        self.assertGreater(xp.deadline_s(1_000_000), 2 * 1000)


class CardCacheTest(TempTest):
    def test_the_cache_directory_changes_with_the_image_mtime(self):
        image = self.tmp() / "bare.img"
        image.write_bytes(b"x")
        cache = self.tmp()
        os.utime(image, ns=(1_000_000_000, 1_000_000_000))
        first = xp.cache_dir_for(image, cache)
        os.utime(image, ns=(2_000_000_000, 2_000_000_000))
        second = xp.cache_dir_for(image, cache)
        self.assertNotEqual(first, second)
        self.assertEqual(first.parent, cache)
        self.assertTrue(first.name.startswith("bare@"))

    def test_a_card_manifest_is_built_here_not_left_to_the_binary(self):
        built = self.tmp()
        with mock.patch.object(
            xp.ui_script_cards, "tree", return_value=built / "x"
        ) as tree:
            got = xp.card_tree(
                "tests/fixtures/card-midi-definition.card", self.tmp(), built
            )
        tree.assert_called_once_with(
            xp.REPO / "tests/fixtures/card-midi-definition.card",
            built / "card-midi-definition",
        )
        self.assertEqual(got, built / "x")

    def test_a_directory_card_is_used_as_it_stands(self):
        self.assertEqual(
            xp.card_tree("tests/fixtures/card", self.tmp(), self.tmp()),
            xp.REPO / "tests/fixtures/card",
        )


class CompareTest(TempTest):
    def tree(self, files: dict[str, bytes]) -> Path:
        root = self.tmp()
        for name, data in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(data)
        return root

    def test_equal_trees_compare_empty(self):
        files = {"leg/a.state": b"1", "leg/a.oled0.pbm": b"2"}
        self.assertEqual(xp.compare(self.tree(files), self.tree(files)), [])

    def test_a_file_on_one_side_only_and_a_differing_file_are_listed(self):
        a = self.tree(
            {"leg/a.state": b"1", "leg/b.state": b"same", "leg/only-a.midi": b""}
        )
        b = self.tree({"leg/a.state": b"2", "leg/b.state": b"same"})
        self.assertEqual(xp.compare(a, b), ["leg/only-a.midi", "leg/a.state"])


FAKE = """#!{python}
import os, pathlib, sys
got = pathlib.Path(os.environ["UI_SNAPSHOT_DIR"])
got.mkdir(parents=True, exist_ok=True)
(got / "a.state").write_text({state!r})
print("UI-SCRIPT-RESULT failures=0" if {code} == 0 else "UI-SCRIPT-RESULT failures=1")
sys.exit({code})
"""


class MainTest(TempTest):
    """main() on fake binaries: each writes one snapshot holding @state and exits with @code."""

    def fake(self, state: str, code: int = 0) -> Path:
        path = self.tmp() / "fake"
        path.write_text(FAKE.format(python=sys.executable, state=state, code=code))
        path.chmod(0o755)
        return path

    def run_main(self, ref: Path, test: Path, script: str) -> tuple[int, str, Path]:
        out = self.tmp()
        s = self.script("# card: tests/fixtures/card\n" + script)
        r = subprocess.run(
            [
                sys.executable,
                str(Path(xp.__file__)),
                "--ref",
                str(ref),
                "--test",
                str(test),
                "--out",
                str(out),
                "--store",
                str(self.tmp()),
                str(s),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        return r.returncode, r.stdout, out

    def test_two_equal_runs_exit_0(self):
        code, stdout, out = self.run_main(
            self.fake("x"), self.fake("x"), "100 snapshot a\n"
        )
        self.assertEqual(code, 0, stdout)
        self.assertEqual(
            stdout.splitlines()[-1],
            "X86-PARITY legs=1 ref_failed=0 test_failed=0 differ=0",
        )
        self.assertEqual((out / "diff.txt").read_text(), "")

    def test_a_difference_exits_1_and_is_listed(self):
        code, stdout, out = self.run_main(
            self.fake("x"), self.fake("y"), "100 snapshot a\n"
        )
        self.assertEqual(code, 1, stdout)
        self.assertEqual(
            stdout.splitlines()[-1],
            "X86-PARITY legs=1 ref_failed=0 test_failed=0 differ=1",
        )
        self.assertEqual((out / "diff.txt").read_text(), "p99-x@card/a.state\n")

    def test_a_failed_leg_exits_2(self):
        code, stdout, _ = self.run_main(
            self.fake("x"), self.fake("x", code=1), "100 snapshot a\n"
        )
        self.assertEqual(code, 2, stdout)
        self.assertIn("p99-x@card ref=ok test=exit 1", stdout)
        self.assertEqual(
            stdout.splitlines()[-1],
            "X86-PARITY legs=1 ref_failed=0 test_failed=1 differ=0",
        )

    def test_a_script_that_cannot_be_translated_fails_its_legs_and_is_named_first(self):
        code, stdout, _ = self.run_main(
            self.fake("x"), self.fake("x"), "100 load A_B\n"
        )
        self.assertEqual(code, 2, stdout)
        self.assertTrue(
            stdout.startswith("X86-PARITY-ERROR p99-x.txt: ValueError"), stdout
        )
        self.assertEqual(
            stdout.splitlines()[-1],
            "X86-PARITY legs=1 ref_failed=1 test_failed=1 differ=0",
        )


if __name__ == "__main__":
    unittest.main()
