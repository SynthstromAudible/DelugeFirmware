#!/usr/bin/env python3
"""Tests for x86_parity: `python3 -m unittest test_x86_parity` (from tools/).

Gate x86-parity-tool:
    x86_parity turns a parity script into the host app's UI script, keys its card cache by the image's mtime, and
    lists every file two runs disagree on
"""

import os
import tempfile
import unittest
from pathlib import Path

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


if __name__ == "__main__":
    unittest.main()
