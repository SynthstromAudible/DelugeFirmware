# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_fixed_point_raw.py: what it counts, what it exempts, and that its raw and view counts only shrink."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from check_fixed_point_raw import (
    EXEMPT,
    Counts,
    check,
    count,
    load_allowlist,
    scan,
    write_allowlist,
)


class CountTest(unittest.TestCase):
    def test_counts_each_raw_helper_call(self):
        self.assertEqual(count("x = multiply_32x32_rshift32(a, b);\n").raw, 1)
        self.assertEqual(
            count("y = multiply_accumulate_32x32_rshift32_rounded(s, a, b);\n").raw, 1
        )
        self.assertEqual(
            count("z = signed_saturate<24>(v) + add_saturate(a, b);\n").raw, 2
        )
        self.assertEqual(count("w = lshift_and_saturate<3>(v);\n").raw, 1)

    def test_counts_inline_asm_that_issues_a_dsp_instruction(self):
        self.assertEqual(
            count('asm("qadd %0, %1, %2" : "=r"(o) : "r"(a), "r"(b));\n').raw, 1
        )
        self.assertEqual(
            count(
                '__asm__ volatile("smmla %0, %1, %2, %3" : "=r"(o) : "r"(a), "r"(b), "r"(c));\n'
            ).raw,
            1,
        )
        self.assertEqual(
            count('asm("mov %0, %1\\n\\tssat %0, #16, %0" : "=r"(o) : "r"(a));\n').raw,
            1,
        )
        self.assertEqual(count('asm volatile("" ::: "memory");\n').raw, 0)
        self.assertEqual(count('asm("" : "+r"(v));\n').raw, 0)

    def test_counts_neon_arithmetic_and_argon_fixed_point_methods(self):
        self.assertEqual(count("r = vqdmulhq_s32(a, b);\n").raw, 1)
        self.assertEqual(count("r = x.MultiplyFixedQMax(y);\n").raw, 1)
        self.assertEqual(count("r = vmlal_s16(acc, a, b);\n").raw, 1)
        self.assertEqual(count("r = vshrq_n_s32(a, 3);\n").raw, 1)
        self.assertEqual(count("r = a.ShiftRightNarrow<16>();\n").raw, 1)
        self.assertEqual(count("r = vld1q_s32(p);\nvst1q_s32(p, r);\n").raw, 0)
        self.assertEqual(count("r = vreinterpretq_s32_u32(u);\n").raw, 0)
        self.assertEqual(
            count(
                "r = vaddl_s16(a, b);\nr = vrhaddq_u8(a, b);\nr = vqrshlq_s32(a, b);\n"
            ).raw,
            3,
        )
        self.assertEqual(
            count("r = vpaddl_u16(a);\nr = vqmovun_s32(a);\nr = vqabsq_s16(a);\n").raw,
            3,
        )
        self.assertEqual(
            count(
                "r = vmaxq_s32(a, b);\nr = vcgtq_s32(a, b);\nr = vabdq_s16(a, b);\n"
            ).raw,
            0,
        )

    def test_counts_the_typed_boundary_apart(self):
        counted = count(
            "auto q = Q<31>::from_raw(x);\nint32_t r = q.raw();\nint32_t s = p->raw();\n"
        )
        self.assertEqual((counted.raw, counted.boundary), (0, 3))
        self.assertEqual(count("auto s = reinterpret_bits<Q<31>>(u);\n").boundary, 1)

    def test_counts_the_seam_views_apart(self):
        counted = count(
            "auto t = typed_view(std::span{buf});\nauto r = deluge::dsp::raw_view(t);\n"
        )
        self.assertEqual(counted, Counts(0, 0, 2))
        self.assertEqual(
            count("auto n = my_typed_view(x);\nauto m = v.raw_view(x);\n").view, 0
        )
        self.assertEqual(count("// typed_view(x)\n").view, 0)

    def test_counts_a_view_with_template_arguments(self):
        code = "auto a = typed_view<Q<24>>(std::span<int32_t>{p, n});\nauto b = deluge::dsp::raw_view<24>(t);\n"
        self.assertEqual(count(code).view, 2)

    def test_ignores_comments_but_not_strings(self):
        self.assertEqual(count("// multiply_32x32_rshift32(a, b)\n").raw, 0)
        self.assertEqual(
            count("/* add_saturate(a, b) and\n   q.raw() */ int x;\n"), Counts(0, 0)
        )
        self.assertEqual(count("int x; // q31_mult(a, b)\n").raw, 0)
        self.assertEqual(
            count('const char* url = "http://x"; int y = q31_mult(a, b);\n').raw, 1
        )
        self.assertEqual(
            count("char c = '/'; int y = q31_mult(a, b); // q31_mult(c, d)\n").raw, 1
        )

    def test_ignores_a_name_that_only_contains_a_helper(self):
        self.assertEqual(count("my_add_saturate_table[3] = 0;\n").raw, 0)
        self.assertEqual(count("int add_saturate_count = 0;\n").raw, 0)


class ScanTest(unittest.TestCase):
    def test_scans_c_family_files_and_exempts_the_core_headers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src/a").mkdir(parents=True)
            (root / "src/a/k.cpp").write_text(
                "x = q31_mult(a, b);\ny = q31_mult(c, d);\nz = q.raw();\n"
            )
            (root / "src/a/clean.h").write_text("int x;\n")
            (root / "src/a/notes.md").write_text("q31_mult(a, b)\n")
            for exempt in EXEMPT:
                (root / exempt).parent.mkdir(parents=True, exist_ok=True)
                (root / exempt).write_text("q31_mult(a, b);\n")
            self.assertEqual(scan(root), {"src/a/k.cpp": Counts(2, 1)})

    def test_the_asm_header_is_not_counted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            asm = root / "src/deluge/dsp-next/core/high_word_asm.h"
            asm.parent.mkdir(parents=True)
            asm.write_text('asm("smmul %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));\n')
            self.assertEqual(scan(root), {})


class RatchetTest(unittest.TestCase):
    def test_views_in_a_file_not_on_the_list_fail(self):
        errors, _ = check({"src/new.cpp": Counts(0, 0, 1)}, {})
        self.assertEqual(len(errors), 1)

    def test_a_view_count_that_grew_fails_and_one_that_fell_is_stale(self):
        errors, _ = check(
            {"src/k.cpp": Counts(1, 0, 3)}, {"src/k.cpp": Counts(1, 0, 2)}
        )
        self.assertEqual(len(errors), 1)
        errors, stale = check(
            {"src/k.cpp": Counts(1, 0, 1)}, {"src/k.cpp": Counts(1, 0, 2)}
        )
        self.assertEqual(len(errors), 1)
        self.assertEqual(stale, ["src/k.cpp"])

    def test_raw_sites_in_a_file_not_on_the_list_fail(self):
        errors, _ = check({"src/new.cpp": Counts(1, 0)}, {})
        self.assertEqual(len(errors), 1)

    def test_a_raw_count_that_grew_fails(self):
        errors, _ = check({"src/k.cpp": Counts(3, 0)}, {"src/k.cpp": Counts(2, 0)})
        self.assertEqual(len(errors), 1)

    def test_a_raw_count_that_fell_fails_until_written(self):
        errors, stale = check({"src/k.cpp": Counts(1, 0)}, {"src/k.cpp": Counts(2, 0)})
        self.assertEqual(len(errors), 1)
        self.assertEqual(stale, ["src/k.cpp"])

    def test_a_file_whose_raw_sites_are_gone_is_stale(self):
        errors, stale = check({}, {"src/k.cpp": Counts(2, 0)})
        self.assertEqual(len(errors), 1)
        self.assertEqual(stale, ["src/k.cpp"])

    def test_boundary_crossings_that_grew_or_appeared_fail(self):
        errors, _ = check({"src/k.cpp": Counts(2, 9)}, {"src/k.cpp": Counts(2, 1)})
        self.assertEqual(len(errors), 1)
        errors, _ = check({"src/n.cpp": Counts(0, 4)}, {})
        self.assertEqual(len(errors), 1)

    def test_boundary_crossings_that_fell_are_stale(self):
        errors, stale = check({"src/k.cpp": Counts(2, 1)}, {"src/k.cpp": Counts(2, 3)})
        self.assertEqual(len(errors), 1)
        self.assertEqual(stale, ["src/k.cpp"])

    def test_writing_refuses_raw_growth_unless_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "allowlist.txt"
            write_allowlist(
                path, {"src/k.cpp": Counts(2, 1)}, previous={}, allow_growth=True
            )
            self.assertEqual(load_allowlist(path), {"src/k.cpp": Counts(2, 1)})
            with self.assertRaises(ValueError):
                write_allowlist(
                    path,
                    {"src/k.cpp": Counts(3, 1)},
                    previous=load_allowlist(path),
                    allow_growth=False,
                )
            with self.assertRaises(ValueError):
                write_allowlist(
                    path,
                    {"src/k.cpp": Counts(2, 1), "src/n.cpp": Counts(1, 0)},
                    previous=load_allowlist(path),
                    allow_growth=False,
                )
            with self.assertRaises(ValueError):
                write_allowlist(
                    path,
                    {"src/k.cpp": Counts(1, 5)},
                    previous=load_allowlist(path),
                    allow_growth=False,
                )
            write_allowlist(
                path,
                {"src/k.cpp": Counts(1, 1)},
                previous=load_allowlist(path),
                allow_growth=False,
            )
            self.assertEqual(load_allowlist(path), {"src/k.cpp": Counts(1, 1)})

    def test_writing_records_views_and_refuses_their_growth(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "allowlist.txt"
            write_allowlist(
                path, {"src/k.cpp": Counts(0, 1, 2)}, previous={}, allow_growth=True
            )
            self.assertIn("src/k.cpp raw=0 view=2 boundary=1", path.read_text())
            self.assertEqual(load_allowlist(path), {"src/k.cpp": Counts(0, 1, 2)})
            with self.assertRaises(ValueError):
                write_allowlist(
                    path,
                    {"src/k.cpp": Counts(0, 1, 3)},
                    previous=load_allowlist(path),
                    allow_growth=False,
                )


if __name__ == "__main__":
    unittest.main()
