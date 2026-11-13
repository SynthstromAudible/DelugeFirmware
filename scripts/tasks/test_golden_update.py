#! /usr/bin/env python3
"""Unit tests for golden_update.py, golden_records.py and `./dbt test --update`.

Run directly with:
    python3 scripts/tasks/test_golden_update.py
"""

import json
import os
import sys
import tempfile
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import golden_records as gr
import golden_update as gu


def _env(log: Path) -> dict[str, str]:
    return {gu.SWITCH: "1", gu.LOG: str(log)}


class UpdatingTest(unittest.TestCase):
    def test_off_unless_exactly_one(self):
        self.assertFalse(gu.updating({}))
        self.assertFalse(gu.updating({gu.SWITCH: "0", gu.LOG: "x"}))
        self.assertFalse(gu.updating({gu.SWITCH: "", gu.LOG: "x"}))
        self.assertTrue(gu.updating({gu.SWITCH: "1", gu.LOG: "x"}))

    def test_the_switch_without_the_log_is_refused(self):
        with self.assertRaises(SystemExit) as cm:
            gu.updating({gu.SWITCH: "1"})
        self.assertEqual(cm.exception.code, 2)

    def test_without_drops_both(self):
        self.assertEqual(
            gu.without({gu.SWITCH: "1", gu.LOG: "x", "A": "b"}), {"A": "b"}
        )


class WriterTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.log = self.dir / "log"
        self.env = _env(self.log)

    def lines(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_a_new_file_is_reported_from_absent(self):
        self.assertTrue(gu.write_text(self.dir / "a.txt", "x\n", "a.txt", env=self.env))
        (line,) = self.lines()
        self.assertTrue(
            line.startswith("GOLDEN-UPDATED a.txt (absent) -> sha256="), line
        )
        self.assertEqual(gu.item_file(line), "a.txt")

    def test_unchanged_bytes_are_not_written_or_reported(self):
        path = self.dir / "a.txt"
        path.write_text("x\n")
        os.utime(path, (1, 1))
        self.assertFalse(gu.write_text(path, "x\n", "a.txt", env=self.env))
        self.assertEqual(path.stat().st_mtime, 1)
        self.assertEqual(self.lines(), [])

    def test_descriptions_are_one_word(self):
        for value in (
            None,
            "abc",
            "f" * 64,
            {"sha256": "f" * 64, "frames": 3, "rms_db": -1.5},
            [1, 2],
            7,
        ):
            self.assertNotIn(" ", gu.describe_value(value))
        self.assertNotIn(" ", gu.describe_bytes(b"a b"))

    def test_an_item_with_spaces_parses(self):
        gu.report("tests/x/Bedroom Beat.txt", "a", "b", env=self.env)
        self.assertEqual(gu.item_file(self.lines()[0]), "tests/x/Bedroom Beat.txt")

    def test_remove_reports_to_absent(self):
        path = self.dir / "a.pbm"
        path.write_text("P1")
        self.assertTrue(gu.remove(path, "a.pbm", env=self.env))
        self.assertFalse(path.exists())
        self.assertTrue(self.lines()[0].endswith(" -> (absent)"))
        self.assertFalse(gu.remove(path, "a.pbm", env=self.env))

    def test_write_json_formats_and_reports_each_key(self):
        path = self.dir / "f.json"
        path.write_text(
            json.dumps({"a": "1" * 64, "b": "2" * 64}, indent=2, sort_keys=True) + "\n"
        )
        n = gu.write_json(
            path,
            {"a": "1" * 64, "b": "3" * 64, "c": "4" * 64},
            "f.json",
            indent=2,
            env=self.env,
        )
        self.assertEqual(n, 2)
        self.assertEqual(
            path.read_text(),
            json.dumps(
                {"a": "1" * 64, "b": "3" * 64, "c": "4" * 64}, indent=2, sort_keys=True
            )
            + "\n",
        )
        self.assertEqual(
            self.lines(),
            [
                f"GOLDEN-UPDATED f.json:b {'2' * 16} -> {'3' * 16}",
                f"GOLDEN-UPDATED f.json:c (absent) -> {'4' * 16}",
            ],
        )

    def test_write_json_of_the_same_object_leaves_the_file(self):
        path = self.dir / "f.json"
        path.write_text(json.dumps({"a": "x"}, indent=1, sort_keys=True) + "\n")
        os.utime(path, (1, 1))
        self.assertEqual(
            gu.write_json(path, {"a": "x"}, "f.json", indent=1, env=self.env), 0
        )
        self.assertEqual(path.stat().st_mtime, 1)

    def test_parallel_key_updates_all_land(self):
        path = self.dir / "songs.json"
        threads = [
            threading.Thread(
                target=gu.update_json_key,
                args=(path, f"S{i}", {"sha256": str(i)}, "songs.json"),
                kwargs={"indent": 1, "env": self.env},
            )
            for i in range(16)
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(
            sorted(json.loads(path.read_text())), sorted(f"S{i}" for i in range(16))
        )
        self.assertEqual(len(self.lines()), 16)
        self.assertEqual(
            sorted(p.name for p in self.dir.iterdir()), ["log", "songs.json"]
        )


class RecordsTest(unittest.TestCase):
    HEADER = (
        "#pragma once\n"
        "inline constexpr int kA[] = {\n"
        "// clang-format off\n"
        "// GOLDEN-RECORDS BEGIN kA\n"
        "    1, // one\n"
        "// GOLDEN-RECORDS END kA\n"
        "// clang-format on\n"
        "};\n"
        "// GOLDEN-RECORDS BEGIN kB\n"
        "inline constexpr bool kB = false;\n"
        "// GOLDEN-RECORDS END kB\n"
    )

    def test_parse_keeps_indentation_and_order(self):
        got = gr.parse(
            [
                "GOLDEN-RECORD h.h kA     1, // one",
                "noise",
                "GOLDEN-RECORD h.h kA     2, // two",
            ]
        )
        self.assertEqual(got, {("h.h", "kA"): ["    1, // one", "    2, // two"]})

    def test_align_pads_code_before_trailing_comments(self):
        self.assertEqual(
            gr.align(
                ["    0x0ULL, // a", "    0x123456ULL, // b", "    // c", "    {1, 2},"]
            ),
            [
                "    0x0ULL,      // a",
                "    0x123456ULL, // b",
                "    // c",
                "    {1, 2},",
            ],
        )

    def test_rewrite_replaces_only_the_region(self):
        new, old = gr.rewrite(self.HEADER, "kA", ["    7, // seven"])
        self.assertEqual(old, ["    1, // one"])
        self.assertEqual(
            new, self.HEADER.replace("    1, // one\n", "    7, // seven\n")
        )

    def test_a_missing_marker_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "kC"):
            gr.rewrite(self.HEADER, "kC", ["x"])

    def test_apply_reports_a_changed_region_and_leaves_an_unchanged_one(self):
        root = Path(tempfile.mkdtemp())
        (root / "h.h").write_text(self.HEADER)
        lines = [
            "GOLDEN-RECORD h.h kA     1, // one",
            "GOLDEN-RECORD h.h kB inline constexpr bool kB = true;",
        ]
        updated = gr.apply(root, lines)
        self.assertEqual(len(updated), 1)
        self.assertTrue(
            updated[0].startswith("GOLDEN-UPDATED h.h:kB lines=1,sha256="), updated[0]
        )
        self.assertIn("inline constexpr bool kB = true;\n", (root / "h.h").read_text())
        self.assertEqual(gr.apply(root, lines), [])


if __name__ == "__main__":
    unittest.main()
