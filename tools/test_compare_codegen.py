# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for compare_codegen.py: what normalisation hides and what it must not."""

from __future__ import annotations

import unittest

from compare_codegen import (
    canonical,
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
20001010:      \tpop\t{r4, pc}
20001014:      \t.word\t0x20251190

20002000 <bar>:
20002000:      \tbx\tlr
"""


class NormaliseTest(unittest.TestCase):
    def test_hides_addresses_but_keeps_immediates(self):
        self.assertEqual(normalise("\tbl\t0x20002000 <bar> @ imm = #0xff8"), "bl <bar>")
        self.assertEqual(
            normalise("\tldr\tr0, [pc, #0x8]           @ 0x20001014 <foo+0x14>"),
            "ldr r0, [pc]",
        )
        self.assertEqual(normalise("\tssat\tr0, #0x18, r0"), "ssat r0, #0x18, r0")
        self.assertEqual(normalise("\t.word\t0x20251190"), ".word ADDR")
        self.assertEqual(normalise("\t.word\t0x00000018"), ".word 0x00000018")
        # A literal pool's words print with their bytes, whatever the flags.
        self.assertEqual(normalise("e5 ef 23 20 .word 0x2023efe5"), ".word ADDR")
        self.assertEqual(
            normalise("\tldr.w\tr10, [pc, #0x560]       @ 0x200ef1f4 <f+0x568>"),
            "ldr.w r10, [pc]",
        )
        self.assertEqual(normalise("\tadr\tr1, #188 <cosf+0x75>"), "adr r1 <cosf>")

    def test_a_branch_into_a_function_keeps_only_its_symbol(self):
        self.assertEqual(normalise("\tb\t0x20001010 <foo+0x10>"), "b <foo>")


class AddressHalvesTest(unittest.TestCase):
    def test_masks_a_register_built_into_an_address_but_not_a_constant(self):
        body = [
            "movw r0, #0x65c0",
            "b <f>",
            "movwhs r0, #0x65c4",
            "movt r0, #0x2024",
            "movw r1, #0x1234",
            "movt r1, #0x0007",
        ]
        self.assertEqual(
            mask_address_halves(body),
            [
                "movw r0, ADDR",
                "b <f>",
                "movwhs r0, ADDR",
                "movt r0, ADDR",
                "movw r1, #0x1234",
                "movt r1, #0x0007",
            ],
        )


class PaddingTest(unittest.TestCase):
    def test_trailing_trap_fill_is_dropped(self):
        text = SAMPLE.replace(
            "20001014:      \t.word\t0x20251190\n",
            "20001014:      \t.word\t0x20251190\n"
            "20001018:      \tbmi\t0x20000fc4 <foo+0x18> @ imm = #-0x58\n",
        )
        self.assertEqual(compare(functions(SAMPLE), functions(text)), ([], [], []))


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
    def test_a_signature_change_with_the_same_code_is_identical(self):
        plain = {"_Z3fooj": "foo", "_Z3barj": "bar", "_Z3fooN4deluge3dsp2UQE": "foo"}
        rename = lambda name: plain.get(name, name)
        base = SAMPLE.replace("<foo>", "<_Z3fooj>").replace("<bar>", "<_Z3barj>")
        new = SAMPLE.replace("<foo>", "<_Z3fooN4deluge3dsp2UQE>").replace(
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
        changed = one.replace("sub", "rsb")
        self.assertEqual(
            compare(functions(one, rename), functions(changed, rename))[0], ["f"]
        )

    def test_same_code_at_other_addresses_is_identical(self):
        moved = (
            SAMPLE.replace("2000100", "2000300")
            .replace("0x20002000", "0x20004000")
            .replace("20002000", "20004000")
        )
        self.assertEqual(compare(functions(SAMPLE), functions(moved)), ([], [], []))

    def test_a_changed_immediate_is_a_change(self):
        changed = SAMPLE.replace("#0x18", "#0x17")
        self.assertEqual(compare(functions(SAMPLE), functions(changed))[0], ["foo"])

    def test_added_and_removed_functions_are_reported(self):
        without_bar = SAMPLE.split("\n\n")[0] + "\n"
        changed, added, removed = compare(functions(SAMPLE), functions(without_bar))
        self.assertEqual((changed, added, removed), ([], [], ["bar"]))


if __name__ == "__main__":
    unittest.main()
