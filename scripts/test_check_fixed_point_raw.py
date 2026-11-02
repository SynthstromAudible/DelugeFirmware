# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_fixed_point_raw.py: what it counts, what it exempts, and that its allowlist only shrinks."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from check_fixed_point_raw import (
    EXEMPT,
    check,
    count_raw,
    load_allowlist,
    scan,
    write_allowlist,
)


class CountTest(unittest.TestCase):
    def test_counts_each_raw_helper_call(self):
        self.assertEqual(count_raw("x = multiply_32x32_rshift32(a, b);\n"), 1)
        self.assertEqual(
            count_raw("y = multiply_accumulate_32x32_rshift32_rounded(s, a, b);\n"), 1
        )
        self.assertEqual(
            count_raw("z = signed_saturate<24>(v) + add_saturate(a, b);\n"), 2
        )
        self.assertEqual(count_raw("w = lshift_and_saturate<3>(v);\n"), 1)

    def test_counts_dsp_instructions_in_inline_asm(self):
        self.assertEqual(
            count_raw('asm("qadd %0, %1, %2" : "=r"(o) : "r"(a), "r"(b));\n'), 1
        )
        self.assertEqual(
            count_raw(
                'asm volatile ("smmla %0, %1, %2, %3" : "=r"(o) : "r"(a), "r"(b), "r"(c));\n'
            ),
            1,
        )
        self.assertEqual(count_raw('asm volatile("" ::: "memory");\n'), 0)

    def test_counts_the_typed_boundary(self):
        self.assertEqual(count_raw("auto q = Q<31>::from_raw(x);\n"), 1)
        self.assertEqual(count_raw("int32_t r = q.raw();\n"), 1)
        self.assertEqual(count_raw("auto s = reinterpret_bits<Q<31>>(u);\n"), 1)

    def test_ignores_comments(self):
        self.assertEqual(count_raw("// multiply_32x32_rshift32(a, b)\n"), 0)
        self.assertEqual(
            count_raw("/* add_saturate(a, b) and\n   q.raw() */ int x;\n"), 0
        )
        self.assertEqual(count_raw("int x; // q31_mult(a, b)\n"), 0)

    def test_ignores_a_name_that_only_contains_a_helper(self):
        self.assertEqual(count_raw("my_add_saturate_table[3] = 0;\n"), 0)
        self.assertEqual(count_raw("int add_saturate_count = 0;\n"), 0)


class ScanTest(unittest.TestCase):
    def test_scans_c_family_files_and_exempts_the_two_headers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src/a").mkdir(parents=True)
            (root / "src/a/k.cpp").write_text(
                "x = q31_mult(a, b);\ny = q31_mult(c, d);\n"
            )
            (root / "src/a/clean.h").write_text("int x;\n")
            (root / "src/a/notes.md").write_text("q31_mult(a, b)\n")
            for exempt in EXEMPT:
                (root / exempt).parent.mkdir(parents=True, exist_ok=True)
                (root / exempt).write_text("q31_mult(a, b);\n")
            self.assertEqual(scan(root), {"src/a/k.cpp": 2})


class RatchetTest(unittest.TestCase):
    def test_a_file_not_on_the_list_fails(self):
        errors, _ = check({"src/new.cpp": 1}, {})
        self.assertEqual(len(errors), 1)

    def test_a_count_that_grew_fails(self):
        errors, _ = check({"src/k.cpp": 3}, {"src/k.cpp": 2})
        self.assertEqual(len(errors), 1)

    def test_a_count_that_fell_passes_and_is_reported_shrinkable(self):
        errors, shrinkable = check({"src/k.cpp": 1}, {"src/k.cpp": 2})
        self.assertEqual(errors, [])
        self.assertEqual(shrinkable, ["src/k.cpp"])

    def test_a_file_that_is_gone_or_clean_is_shrinkable(self):
        errors, shrinkable = check({}, {"src/k.cpp": 2})
        self.assertEqual(errors, [])
        self.assertEqual(shrinkable, ["src/k.cpp"])

    def test_writing_refuses_growth_unless_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "allowlist.txt"
            write_allowlist(path, {"src/k.cpp": 2}, previous={}, allow_growth=True)
            self.assertEqual(load_allowlist(path), {"src/k.cpp": 2})
            with self.assertRaises(ValueError):
                write_allowlist(
                    path,
                    {"src/k.cpp": 3},
                    previous=load_allowlist(path),
                    allow_growth=False,
                )
            with self.assertRaises(ValueError):
                write_allowlist(
                    path,
                    {"src/k.cpp": 2, "src/n.cpp": 1},
                    previous=load_allowlist(path),
                    allow_growth=False,
                )
            write_allowlist(
                path,
                {"src/k.cpp": 1},
                previous=load_allowlist(path),
                allow_growth=False,
            )
            self.assertEqual(load_allowlist(path), {"src/k.cpp": 1})


if __name__ == "__main__":
    unittest.main()
