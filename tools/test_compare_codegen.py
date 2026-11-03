# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for compare_codegen.py: what masking hides and what it must not."""

from __future__ import annotations

import unittest

from compare_codegen import (
    canonical,
    classifier,
    compare,
    functions,
    mask_address_halves,
    normalise,
)

SAMPLE = """\
20001000 <foo>:
20001000:      \tpush\t{r4, lr}
20001004:      \tbl\t0x20002000 <bar> @ imm = #0xff8
20001008:      \tldr\tr0, [pc, #0x8]           @ 0x20001014 <foo+0x14>
2000100c:      \tssat\tr0, #0x18, r0
2000100e:      \tbeq\t0x20001004 <foo+0x4> @ imm = #-0xc
20001010:      \tpop\t{r4, pc}
20001014:      \t.word\t0x20251190

20002000 <bar>:
20002000:      \tbx\tlr
"""


def moved(text: str) -> str:
    return (
        text.replace("2000100", "2000300")
        .replace("20002000", "20004000")
        .replace("0x20002000", "0x20004000")
    )


class NormaliseTest(unittest.TestCase):
    def test_hides_addresses_but_keeps_immediates(self):
        self.assertEqual(normalise("\tbl\t0x20002000 <bar> @ imm = #0xff8"), "bl <bar>")
        self.assertEqual(
            normalise("\tldr\tr0, [pc, #0x8]           @ 0x20001014 <foo+0x14>"),
            "ldr r0, [pc]",
        )
        self.assertEqual(normalise("\tssat\tr0, #0x18, r0"), "ssat r0, #0x18, r0")
        self.assertEqual(normalise("\t.word\t0x20251190"), ".word ADDR")
        self.assertEqual(normalise("e5 ef 23 20 .word 0x2023efe5"), ".word ADDR")
        self.assertEqual(normalise("\tadr\tr1, #188 <cosf+0x75>"), "adr r1 <cosf>")

    def test_constants_outside_the_loaded_segments_compare_as_they_are(self):
        for word in ("0x00000018", "0x5a82799a", "0xfffffffe", "0x3f800000"):
            self.assertEqual(normalise(f"\t.word\t{word}"), f".word {word}")

    def test_a_branch_inside_the_function_becomes_an_instruction_index(self):
        index = {0x20001004: 1}.get
        self.assertEqual(
            normalise("\tbeq\t0x20001004 <foo+0x4> @ imm = #-0xc", local=index),
            "beq <.1>",
        )


class AddressHalvesTest(unittest.TestCase):
    def test_masks_the_halves_that_build_an_address_on_every_arm(self):
        body = [
            "movw r0, #0x65c0",
            "b <.3>",
            "movwhs r0, #0x65c4",
            "movt r0, #0x2024",
            "movw r1, #0x1234",
            "movt r1, #0x0007",
        ]
        self.assertEqual(
            mask_address_halves(body),
            [
                "movw r0, lo(ADDR)",
                "b <.3>",
                "movwhs r0, lo(ADDR)",
                "movt r0, hi(ADDR)",
                "movw r1, #0x1234",
                "movt r1, #0x0007",
            ],
        )

    def test_a_constant_in_a_register_that_later_holds_an_address_is_kept(self):
        body = [
            "movw r2, #0x5",
            "mul r2, r2, r3",
            "movw r2, #0x1000",
            "movt r2, #0x2024",
        ]
        self.assertEqual(mask_address_halves(body)[0], "movw r2, #0x5")

    def test_a_constant_built_by_movw_and_movt_is_compared(self):
        sqrt_half = ["movw r2, #0x799a", "movt r2, #0x5a82"]
        half = ["movw r2, #0x8000", "movt r2, #0x4000"]
        self.assertNotEqual(mask_address_halves(sqrt_half), mask_address_halves(half))


class ClassifierTest(unittest.TestCase):
    SEGMENTS = "  LOAD 0x0 0x20000000 0x20000000 0x1000 0x1000 RW 0x10000\n"
    SYMBOLS = "20000100 00000010 d foo\n20000200 00000008 d bar\n"

    def test_names_the_symbol_an_address_falls_in(self):
        classify = classifier(self.SEGMENTS, self.SYMBOLS)
        self.assertEqual(classify(0x20000104), "ADDR<foo>")
        self.assertEqual(classify(0x20000204), "ADDR<bar>")
        self.assertEqual(classify(0x20000300), "ADDR")
        self.assertIsNone(classify(0x5A82799A))

    def test_a_reference_moved_to_another_symbol_is_a_change(self):
        classify = classifier(self.SEGMENTS, self.SYMBOLS)
        before = "10 <f>:\n10:\t.word\t0x20000104\n"
        after = "10 <f>:\n10:\t.word\t0x20000204\n"
        self.assertEqual(
            compare(
                functions(before, classify=classify),
                functions(after, classify=classify),
            )[0],
            ["f"],
        )


class CanonicalTest(unittest.TestCase):
    def test_drops_parameters_qualifiers_and_a_template_return_type(self):
        self.assertEqual(
            canonical("deluge::synth::SourceSlot::advance_osc_phase(unsigned int)"),
            "deluge::synth::SourceSlot::advance_osc_phase",
        )
        self.assertEqual(canonical("unsigned int foo<int>(int)"), "foo<int>")
        self.assertEqual(
            canonical("bar()::'lambda'(unsigned int)::operator()(unsigned int) const"),
            "bar()::'lambda'(unsigned int)::operator()",
        )
        self.assertEqual(canonical("operator new(unsigned int)"), "operator new")
        self.assertEqual(canonical("kTable"), "kTable")


class CompareTest(unittest.TestCase):
    def test_same_code_at_other_addresses_is_identical(self):
        self.assertEqual(
            compare(functions(SAMPLE), functions(moved(SAMPLE))), ([], [], [])
        )

    def test_a_changed_immediate_is_a_change(self):
        changed = SAMPLE.replace("#0x18", "#0x17")
        self.assertEqual(compare(functions(SAMPLE), functions(changed))[0], ["foo"])

    def test_a_branch_retargeted_inside_the_function_is_a_change(self):
        changed = SAMPLE.replace("0x20001004 <foo+0x4>", "0x2000100c <foo+0xc>")
        self.assertEqual(compare(functions(SAMPLE), functions(changed))[0], ["foo"])

    def test_trailing_trap_fill_is_dropped(self):
        filled = SAMPLE.replace(
            "20001014:      \t.word\t0x20251190\n",
            "20001014:      \t.word\t0x20251190\n20001018:      \tbmi\t0x20000fc4 <foo+0x18> @ imm = #-0x58\n",
        )
        self.assertEqual(compare(functions(SAMPLE), functions(filled)), ([], [], []))

    def test_added_and_removed_functions_are_reported(self):
        without_bar = SAMPLE.split("\n\n")[0] + "\n"
        self.assertEqual(
            compare(functions(SAMPLE), functions(without_bar)), ([], [], ["bar"])
        )

    def test_a_signature_change_with_the_same_code_is_identical(self):
        plain = {"_Z3fooj": "foo", "_Z3barj": "bar", "_Z3fooN4deluge3dsp2UQE": "foo"}
        rename = lambda name: plain.get(name, name)
        base = SAMPLE.replace("<foo", "<_Z3fooj").replace("<bar>", "<_Z3barj>")
        new = SAMPLE.replace("<foo", "<_Z3fooN4deluge3dsp2UQE").replace(
            "<bar>", "<_Z3barj>"
        )
        self.assertEqual(
            compare(functions(base, rename), functions(new, rename)), ([], [], [])
        )

    def test_overloads_compare_as_a_set_of_bodies(self):
        plain = {"_Z1fi": "f", "_Z1fj": "f"}
        rename = lambda name: plain.get(name, name)
        one = "10 <_Z1fi>:\n10:\tadd\tr0, r0, r1\n\n20 <_Z1fj>:\n20:\tsub\tr0, r0, r1\n"
        swapped = (
            one.replace("add", "SWAP").replace("sub", "add").replace("SWAP", "sub")
        )
        self.assertEqual(
            compare(functions(one, rename), functions(swapped, rename)), ([], [], [])
        )
        self.assertEqual(
            compare(
                functions(one, rename), functions(one.replace("sub", "rsb"), rename)
            )[0],
            ["f"],
        )


if __name__ == "__main__":
    unittest.main()
