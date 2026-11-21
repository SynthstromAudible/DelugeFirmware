#!/usr/bin/env python3
"""Tests for x86_parity: `python3 -m unittest test_x86_parity` (from tools/).

Gate x86-parity-tool:
    x86_parity turns a parity script into the host app's UI script, keys its card cache by the image's mtime, and
    lists every file two runs disagree on
"""

import contextlib
import io
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
import os, pathlib, sys, time
got = pathlib.Path(os.environ["UI_SNAPSHOT_DIR"])
got.mkdir(parents=True, exist_ok=True)
for name in {names!r}:
    (got / name).write_text({state!r})
print("UI-SCRIPT-RESULT failures=0" if {code} == 0 else "UI-SCRIPT-RESULT failures=1", flush=True)
time.sleep({sleep})
sys.exit({code})
"""


class MainTest(TempTest):
    """main() on fake binaries: each writes the snapshots @names holding @state, sleeps @sleep seconds and exits with
    @code."""

    def fake(
        self,
        state: str,
        code: int = 0,
        names: tuple[str, ...] = ("a.state",),
        sleep: float = 0,
    ) -> Path:
        path = self.tmp() / "fake"
        path.write_text(
            FAKE.format(
                python=sys.executable, state=state, code=code, names=names, sleep=sleep
            )
        )
        path.chmod(0o755)
        return path

    def run_main(
        self,
        ref: Path,
        test: Path,
        script: str,
        out: Path | None = None,
        scripts: list[Path] | None = None,
    ) -> tuple[int, str, Path]:
        out = out or self.tmp()
        named = scripts or [self.script("# card: tests/fixtures/card\n" + script)]
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
                *map(str, named),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        return r.returncode, r.stdout + r.stderr, out

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

    def test_a_reused_out_holds_only_this_runs_snapshots(self):
        out = self.tmp()
        two = self.fake("x", names=("a.state", "gone.state"))
        code, stdout, _ = self.run_main(two, two, "100 snapshot a\n", out=out)
        self.assertEqual(code, 0, stdout)
        one = self.fake("x")
        code, stdout, _ = self.run_main(one, one, "100 snapshot a\n", out=out)
        self.assertEqual(code, 0, stdout)
        for side in ("ref", "test"):
            got = out / side / "got" / "p99-x@card"
            self.assertEqual(sorted(p.name for p in got.iterdir()), ["a.state"])
            results = (out / side / "results.json").read_text()
            self.assertIn('"snapshots": 1', results)

    def test_a_script_named_twice_runs_once_and_is_reported_first(self):
        s = self.script("# card: tests/fixtures/card\n100 snapshot a\n")
        code, stdout, _ = self.run_main(
            self.fake("x"), self.fake("x"), "", scripts=[s, s.parent]
        )
        self.assertEqual(code, 0, stdout)
        self.assertTrue(
            stdout.startswith(f"X86-PARITY-NOTE {s.parent / s.name}"), stdout
        )
        self.assertIn("X86-PARITY legs=1 ref_failed=0 test_failed=0 differ=0", stdout)

    def test_two_scripts_with_one_name_are_refused_before_anything_runs(self):
        text = "# card: tests/fixtures/card\n100 snapshot a\n"
        first, second = self.script(text), self.script(text)
        code, stdout, out = self.run_main(
            self.fake("x"), self.fake("x"), "", scripts=[first, second]
        )
        self.assertEqual(code, 2, stdout)
        self.assertIn("share the leg name 'p99-x'", stdout)
        self.assertFalse((out / "ref").exists())


class TimeoutTest(TempTest):
    """A run that outlives its kill time, in-process so the kill time can be cut short."""

    def test_a_run_past_its_kill_time_is_a_failed_leg_and_the_run_exits_2(self):
        fake = self.tmp() / "fake"
        fake.write_text(
            FAKE.format(
                python=sys.executable, state="x", code=0, names=("a.state",), sleep=60
            )
        )
        fake.chmod(0o755)
        s = self.script("# card: tests/fixtures/card\n100 snapshot a\n")
        out = self.tmp()
        argv = [
            "x86_parity.py",
            "--ref",
            str(fake),
            "--test",
            str(fake),
            "--out",
            str(out),
        ]
        argv += ["--store", str(self.tmp()), str(s)]
        printed = io.StringIO()
        with (
            mock.patch.object(xp, "kill_after_s", return_value=1),
            mock.patch.object(sys, "argv", argv),
            contextlib.redirect_stdout(printed),
        ):
            code = xp.main()
        self.assertEqual(code, 2, printed.getvalue())
        self.assertIn(
            "p99-x@card ref=error: timeout test=error: timeout", printed.getvalue()
        )
        self.assertEqual(
            printed.getvalue().splitlines()[-1],
            "X86-PARITY legs=1 ref_failed=1 test_failed=1 differ=0",
        )
        log = (out / "ref" / "logs" / "p99-x@card.log").read_text()
        self.assertTrue(log.startswith("timeout\n"), log)
        self.assertIn("UI-SCRIPT-RESULT failures=0", log)

    def test_the_kill_time_is_past_the_apps_own_deadline(self):
        self.assertEqual(xp.kill_after_s(0), xp.deadline_s(0) + xp.TIMEOUT_GRACE_S)
        self.assertGreater(xp.TIMEOUT_GRACE_S, 0)


if __name__ == "__main__":
    unittest.main()
