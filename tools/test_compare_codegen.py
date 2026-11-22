# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for compare_codegen.py: what masking hides and what it must not.

Gate compare-codegen-selftest:
    the machine-code comparison of two images names what an address points at instead of its value (a sized symbol, or
    ADDR in a loaded segment, for literal-pool words and the movw/movt pairs that reach each other along the function's
    branches; an in-function branch as the index it lands on) and masks pc-relative and adr offsets, lld's trap fill,
    ThinLTO suffixes and a signature's parameter types; it reports a changed constant (one outside every loaded segment,
    in a pool word or a movw/movt pair), a reference moved to another symbol, a retargeted branch and an added or
    removed function
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import unittest
from typing import ClassVar
from unittest import mock

import compare_codegen
from compare_codegen import (
    aliases,
    canonical,
    classifier,
    compare,
    demangler,
    disassemble,
    functions,
    mangled_renamer,
    mask_address_halves,
    namespace_renamer,
    normalise,
)


class NamespaceRenames(unittest.TestCase):
    def test_a_renamed_namespace_maps_anywhere_in_a_name(self):
        rename = namespace_renamer([("deluge::scenes", "deluge::ui::scenes")])
        self.assertEqual(
            rename("deluge::scenes::menu::Menu::render"),
            "deluge::ui::scenes::menu::Menu::render",
        )
        self.assertEqual(
            rename("std::vector<deluge::scenes::Node>::push_back"),
            "std::vector<deluge::ui::scenes::Node>::push_back",
        )
        self.assertEqual(
            rename("vtable for deluge::scenes::Scene"),
            "vtable for deluge::ui::scenes::Scene",
        )

    def test_a_longer_or_nested_name_is_left(self):
        rename = namespace_renamer([("deluge::scenes", "deluge::ui::scenes")])
        self.assertEqual(rename("deluge::scenes_x::f"), "deluge::scenes_x::f")
        self.assertEqual(rename("my::deluge::scenes::f"), "my::deluge::scenes::f")
        self.assertEqual(rename("deluge::ui::scenes::f"), "deluge::ui::scenes::f")

    def test_a_duplicate_old_name_takes_the_first_new_name(self):
        rename = namespace_renamer([("a::b", "x"), ("a::b", "y")])
        self.assertEqual(rename("a::b::f"), "x::f")

    def test_the_first_matching_pair_wins_and_a_result_is_not_renamed_again(self):
        rename = namespace_renamer(
            [("deluge::ui::oled", "deluge::oled"), ("deluge::ui", "deluge::view")]
        )
        self.assertEqual(rename("deluge::ui::oled::draw"), "deluge::oled::draw")
        self.assertEqual(rename("deluge::ui::Scene::f"), "deluge::view::Scene::f")
        self.assertEqual(rename("deluge::ui<int>"), "deluge::view<int>")

    def test_a_prefix_matches_only_on_a_component_boundary(self):
        rename = namespace_renamer([("deluge::ui", "deluge::view")])
        self.assertEqual(rename("deluge::uix::f"), "deluge::uix::f")
        self.assertEqual(rename("deluge::ui"), "deluge::view")
        self.assertEqual(rename("deluge::ui::f"), "deluge::view::f")

    def test_no_pairs_changes_nothing(self):
        self.assertEqual(namespace_renamer([])("deluge::ui::f"), "deluge::ui::f")


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


REAL_IMAGE = os.path.join(
    os.path.dirname(__file__), "..", "build", "RelWithDebInfo", "deluge.elf"
)

# Shapes from a device image whose last `)` or space is not a signature.
NAME_FIXTURE = [
    "deluge::(anonymous namespace)::kTable",
    "deluge::(anonymous namespace)::kOther",
    "deluge::(anonymous namespace)::host()",
    "deluge::(anonymous namespace)::host()::ops",
    "guard variable for deluge::(anonymous namespace)::host()::ops",
    "deluge::gfx::f()::scene",
    "deluge::gfx::f()::list",
    "deluge::gfx::f(int)::scene",
    "BatteryLevelItem::render() const::$_0::operator()() const",
    "PatchCableStrengthItem::on_focus() const::$_0::operator()() const",
    "MarkerScene::pins() const::$_0::operator()() const",
    "MarkerScene::pins() const::$_1::operator()() const",
    "MarkerScene::pins() const::$_0::__invoke()",
    "ModKnobs::next_timer() const::$_0::__invoke()",
    "X::f(int) const::{lambda()#1}::operator()() const",
    "X::f(int) const::{lambda()#2}::operator()() const",
    "X::f(long) const::{lambda()#1}::operator()() const",
    "bar()::'lambda'(unsigned int)::operator()(unsigned int) const",
    "bar()::'lambda'(int)::operator()(int) const",
    "std::vector<(anonymous namespace)::A>::push_back(int)",
    "std::vector<(anonymous namespace)::B>::push_back(int)",
    "non-virtual thunk to X::f(int)",
    "X::f(int)",
    "X::f(long)",
    "vtable for X",
    "typeinfo for X",
    "(anonymous namespace)::pool::free(void*)",
    "(anonymous namespace)::pool::free(void*) (.constprop.0)",
    "X::operator bool() const::$_0::operator()() const",
    "X::operator int() const::$_0::operator()() const",
]


def is_own_signature_of(key: str, name: str) -> bool:
    """Whether @p name is @p key with its own parameter list (and what follows it) and perhaps a leading return type
    added: what one key may hold several of."""
    clone = re.search(r"(?: \(\.[\w.]+\))+$", key)
    if clone:
        if not name.endswith(clone.group(0)):
            return False
        key, name = key[: clone.start()], name[: -len(clone.group(0))]
    at = name.find(key + "(")
    while at >= 0:
        if at == 0 or name[at - 1] == " ":
            rest = name[at + len(key) :]
            depth = 0
            for i, c in enumerate(rest):
                depth += {"(": 1, ")": -1}.get(c, 0)
                if depth == 0:
                    if rest[
                        : i + 1
                    ] != "(anonymous namespace)" and OWN_QUALIFIERS.fullmatch(
                        rest[i + 1 :]
                    ):
                        return True
                    break
        at = name.find(key + "(", at + 1)
    return False


OWN_QUALIFIERS = re.compile(r"(?: (?:const|volatile|noexcept)| ?&&?)*(?: requires .*)?")


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

    def test_an_anonymous_namespace_is_never_a_parameter_list(self):
        self.assertEqual(
            canonical("deluge::(anonymous namespace)::kTable"),
            "deluge::(anonymous namespace)::kTable",
        )
        self.assertEqual(
            canonical("(anonymous namespace)::emergency_pool"),
            "(anonymous namespace)::emergency_pool",
        )
        self.assertEqual(
            canonical("deluge::(anonymous namespace)::host()"),
            "deluge::(anonymous namespace)::host",
        )
        self.assertEqual(
            canonical(
                "std::vector<(anonymous namespace)::Node>::push_back((anonymous namespace)::Node&&)"
            ),
            "std::vector<(anonymous namespace)::Node>::push_back",
        )
        self.assertEqual(
            canonical("Holder<(anonymous namespace)::Tag>::instance"),
            "Holder<(anonymous namespace)::Tag>::instance",
        )

    def test_a_lambda_keeps_its_enclosing_functions_signature(self):
        self.assertEqual(
            canonical("BatteryLevelItem::render() const::$_0::operator()() const"),
            "BatteryLevelItem::render() const::$_0::operator()",
        )
        self.assertEqual(
            canonical("ModKnobs::next_timer(int) const::$_0::__invoke(int)"),
            "ModKnobs::next_timer(int) const::$_0::__invoke",
        )
        self.assertEqual(
            canonical("X::f(int) const::{lambda()#1}::operator()() const"),
            "X::f(int) const::{lambda()#1}::operator()",
        )
        self.assertEqual(
            canonical("X::f() &&::$_2::operator()(int) const"),
            "X::f() &&::$_2::operator()",
        )
        self.assertEqual(
            canonical("auto X::f() const::$_0::operator()<int>(int) const"),
            "X::f() const::$_0::operator()<int>",
        )

    def test_a_local_keeps_its_enclosing_functions_signature(self):
        self.assertEqual(
            canonical("deluge::dsp::fixed::saw_bandlimited_table()::table"),
            "deluge::dsp::fixed::saw_bandlimited_table()::table",
        )
        self.assertEqual(
            canonical(
                "AudioEngine::render(int const*, unsigned int) const::lastWarnTime"
            ),
            "AudioEngine::render(int const*, unsigned int) const::lastWarnTime",
        )
        self.assertEqual(
            canonical("guard variable for ClearSong::getOptions()::options"),
            "guard variable for ClearSong::getOptions()::options",
        )
        self.assertEqual(
            canonical("X::operator bool() const::$_0::operator()() const"),
            "X::operator bool() const::$_0::operator()",
        )

    def test_a_return_type_is_stripped_only_at_top_level(self):
        self.assertEqual(
            canonical(
                "std::string std::operator+<char>(char const*, std::string const&)"
            ),
            "std::operator+<char>",
        )
        self.assertEqual(canonical("char const* name<int>()"), "name<int>")
        self.assertEqual(canonical("int const f<int>()"), "f<int>")
        self.assertEqual(
            canonical("X::operator unsigned int() const"), "X::operator unsigned int"
        )
        self.assertEqual(canonical("operator delete[](void*)"), "operator delete[]")
        self.assertEqual(
            canonical("bool std::operator<<char>(std::string const&, char const*)"),
            "std::operator<<char>",
        )
        self.assertEqual(
            canonical(
                "std::ostream& std::operator<<<std::char_traits<char>>(std::ostream&, char)"
            ),
            "std::operator<<<std::char_traits<char>>",
        )
        self.assertEqual(canonical("X::operator->() const"), "X::operator->")

    def test_special_names_keep_what_they_name(self):
        self.assertEqual(
            canonical("non-virtual thunk to X::f(int)"), "non-virtual thunk to X::f"
        )
        self.assertEqual(canonical("vtable for X"), "vtable for X")
        self.assertEqual(canonical("vtable for f()::Local"), "vtable for f()::Local")

    def test_a_trailing_requires_clause_and_a_clone_suffix(self):
        self.assertEqual(
            canonical(
                "std::expected<int, E>::~expected() requires !is_trivially_destructible_v<T> || "
                "!is_trivially_destructible_v<T0>"
            ),
            "std::expected<int, E>::~expected",
        )
        self.assertEqual(
            canonical(
                "auto S::operator()<int>(int const&) const requires requires { {fp < fp0} -> B; }"
            ),
            "S::operator()<int>",
        )
        self.assertEqual(
            canonical("(anonymous namespace)::pool::free(void*) (.constprop.0)"),
            "(anonymous namespace)::pool::free (.constprop.0)",
        )

    def test_a_signature_change_keeps_one_key(self):
        self.assertEqual(canonical("int f<int>(int)"), canonical("void f<int>(long)"))
        self.assertEqual(canonical("X::g() const"), canonical("X::g()"))
        self.assertEqual(canonical("X::g() const &"), canonical("X::g() &&"))

    def test_only_overloads_share_a_key(self):
        overloads = [
            "deluge::project::encode(deluge::project::Groove const&, int)",
            "deluge::project::encode(deluge::project::ScaleSettings const&)",
        ]
        self.assertEqual(len({canonical(n) for n in overloads}), 1)
        self.assert_only_overloads_collide(NAME_FIXTURE)

    def assert_only_overloads_collide(self, names):
        keys: dict[str, set[str]] = {}
        for name in names:
            keys.setdefault(canonical(name), set()).add(name)
        for key, group in keys.items():
            if len(group) > 1:
                for name in group:
                    self.assertTrue(
                        is_own_signature_of(key, name), (key, sorted(group))
                    )

    @unittest.skipUnless(
        os.path.exists(REAL_IMAGE) and shutil.which("llvm-nm"), "no device image"
    )
    def test_only_overloads_share_a_key_in_a_real_image(self):
        listing = subprocess.run(
            ["llvm-nm", "-C", "--defined-only", REAL_IMAGE],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        self.assert_only_overloads_collide(
            {line[11:] for line in listing.splitlines() if len(line) > 11}
        )


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


SDRAM_NM = """\
20001000 00000010 T foo
2f000000 00000000 D __sdram_text_start
2f000400 00000000 D __sdram_text_end
"""

TWO_SECTIONS = """\
Disassembly of section .text:

20001000 <foo>:
20001000:      \tbx\tlr

Disassembly of section .sdram_init:

2f000000 <gui>:
2f000000:      \tmov.w\tr2, #0x18
2f000004:      \tbx\tlr
"""


class DisassembleTest(unittest.TestCase):
    def test_the_sdram_range_is_disassembled_as_data_and_appended(self):
        calls = []

        def fake(tool, stdin=None):
            calls.append(tool)
            return f"out{len(calls)}\n"

        with mock.patch.object(compare_codegen, "run", fake):
            text = disassemble("objdump", "a.elf", SDRAM_NM)
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0], ["objdump", "-d", "--no-show-raw-insn", "a.elf"])
        self.assertEqual(
            calls[1],
            [
                "objdump",
                "-D",
                "--no-show-raw-insn",
                "-j",
                ".sdram_init",
                "--start-address=0x2f000000",
                "--stop-address=0x2f000400",
                "a.elf",
            ],
        )
        self.assertEqual(text, "out1\nout2\n")

    def test_an_image_without_the_symbols_is_disassembled_once(self):
        calls = []

        def fake(tool, stdin=None):
            calls.append(tool)
            return "only\n"

        nm = "20001000 00000010 T foo\n2f000000 00000000 D __sdram_text_start\n"
        with mock.patch.object(compare_codegen, "run", fake):
            text = disassemble("objdump", "a.elf", nm)
        self.assertEqual(calls, [["objdump", "-d", "--no-show-raw-insn", "a.elf"]])
        self.assertEqual(text, "only\n")

    def test_a_change_in_the_sdram_code_is_a_change(self):
        changed = TWO_SECTIONS.replace("#0x18", "#0x17")
        self.assertEqual(
            compare(functions(TWO_SECTIONS), functions(changed)), (["gui"], [], [])
        )


class VeneerNames(unittest.TestCase):
    OLD = "__Thumbv7ABSLongThunk__ZN6deluge3gfx1fEv"
    NEW = "__Thumbv7ABSLongThunk__ZN6deluge2ui3gfx1fEv"
    OTHER = "__Thumbv7ABSLongThunk__ZN6deluge3gfx1gEv"
    PLAIN = "_ZN6deluge3gfx1fEv"

    def names(self):
        table = {
            "_ZN6deluge3gfx1fEv": "deluge::gfx::f()",
            "_ZN6deluge2ui3gfx1fEv": "deluge::ui::gfx::f()",
            "_ZN6deluge3gfx1gEv": "deluge::gfx::g()",
        }

        def fake(tool, stdin=None):
            return "\n".join(table[n] for n in stdin.splitlines()) + "\n"

        listing = "".join(
            f"10 <{n}>:\n" for n in (self.OLD, self.NEW, self.OTHER, self.PLAIN)
        )
        with mock.patch.object(compare_codegen, "run", fake):
            return demangler(
                "cxxfilt",
                [listing],
                namespace_renamer([("deluge::gfx", "deluge::ui::gfx")]),
            )

    def test_a_veneer_follows_its_targets_rename(self):
        rename = self.names()
        self.assertEqual(rename(self.OLD), rename(self.NEW))
        self.assertTrue(rename(self.OLD).startswith("__Thumbv7ABSLongThunk_"))

    def test_veneers_to_different_targets_stay_distinct(self):
        rename = self.names()
        self.assertNotEqual(rename(self.OLD), rename(self.OTHER))

    def test_a_plain_symbol_is_not_given_a_veneer_prefix(self):
        self.assertEqual(self.names()(self.PLAIN), "deluge::ui::gfx::f")


def fake_cxxfilt(table):
    """A stand-in for `run([cxxfilt], stdin)`: names in @p table demangle, any other stays as it is."""

    def fake(tool, stdin=None):
        return "\n".join(table.get(n, n) for n in stdin.splitlines()) + "\n"

    return fake


class WrappedNames(unittest.TestCase):
    PAIR = (("deluge::gfx", "deluge::ui::gfx"),)
    TABLE: ClassVar[dict[str, str]] = {
        "_ZN6deluge3gfx1kE": "deluge::gfx::k",
        "_ZN6deluge2ui3gfx1kE": "deluge::ui::gfx::k",
        "_ZZN6deluge3gfx1fEvE5scene": "deluge::gfx::f()::scene",
        "_ZZN6deluge2ui3gfx1fEvE5scene": "deluge::ui::gfx::f()::scene",
    }

    def names(self, listing):
        with mock.patch.object(compare_codegen, "run", fake_cxxfilt(self.TABLE)):
            return demangler(
                "cxxfilt",
                [listing],
                namespace_renamer(list(self.PAIR)),
                mangled_renamer(list(self.PAIR)),
            )

    def test_a_data_name_follows_its_symbols_rename_and_keeps_its_wrapping(self):
        old = "__const._ZN6deluge3gfx1kE.sources"
        new = "__const._ZN6deluge2ui3gfx1kE.sources"
        rename = self.names(f"10 <{old}>:\n10 <{new}>:\n")
        self.assertEqual(rename(old), rename(new))
        self.assertTrue(rename(old).startswith("__const."))
        self.assertTrue(rename(old).endswith(".sources"))

    def test_a_guard_destructor_name_follows_its_symbols_rename(self):
        old = "__dtor__ZZN6deluge3gfx1fEvE5scene"
        new = "__dtor__ZZN6deluge2ui3gfx1fEvE5scene"
        rename = self.names(f"10 <{old}>:\n10 <{new}>:\n")
        self.assertEqual(rename(old), rename(new))
        self.assertTrue(rename(old).startswith("__dtor_"))

    def test_a_name_the_demangler_rejects_is_renamed_as_mangled_text(self):
        old = "_ZZZN6deluge3gui1fERKNS_6scenes1WEENK3$_0clIZNS2_1fES7_E3$_1EEDav"
        new = "_ZZZN6deluge3gui1fERKNS_2ui6scenes1WEENK3$_0clIZNS2_1fES8_E3$_1EEDav"
        pair = [("deluge::scenes", "deluge::ui::scenes")]
        with mock.patch.object(compare_codegen, "run", fake_cxxfilt({})):
            rename = demangler(
                "cxxfilt",
                [f"10 <{old}>:\n10 <{new}>:\n"],
                namespace_renamer(pair),
                mangled_renamer(pair),
                [[f"10 <{old}>:\n"], [f"10 <{new}>:\n"]],
            )
        self.assertEqual(rename(old), rename(new))

    def test_a_component_run_is_renamed_to_the_new_spelling(self):
        rename = mangled_renamer(
            [("deluge::instrument", "deluge::instruments::classic")]
        )
        self.assertEqual(
            rename("_ZN6deluge10instrument1fEv"),
            rename("_ZN6deluge11instruments7classic1fEv"),
        )
        self.assertNotEqual(
            rename("_ZN6deluge10instrument1fEv"),
            rename("_ZN6deluge10instrumenx1fEv"),
        )

    def test_two_different_lambdas_stay_distinct(self):
        pair = [("deluge::scenes", "deluge::ui::scenes")]
        a = "_ZZZN6deluge3gui1fERKNS_6scenes1WEENK3$_0clIZNS2_1fES7_E3$_1EEDav"
        b = a.replace("$_1", "$_2")
        with mock.patch.object(compare_codegen, "run", fake_cxxfilt({})):
            rename = demangler(
                "cxxfilt",
                [f"10 <{a}>:\n10 <{b}>:\n"],
                namespace_renamer(pair),
                mangled_renamer(pair),
            )
        self.assertNotEqual(rename(a), rename(b))


class FoldedAliases(unittest.TestCase):
    NM = """\
20001000 00000008 T alpha
20001000 00000008 T beta
20002000 00000008 T gamma
"""

    @staticmethod
    def image(first: str, gamma: str = "bx\tlr") -> str:
        return (
            f"20001000 <{first}>:\n20001000:      \tbx\tlr\n\n"
            f"20002000 <gamma>:\n20002000:      \t{gamma}\n"
        )

    def test_two_images_that_print_different_aliases_compare_identical(self):
        table = aliases(self.NM)
        one = functions(self.image("alpha"), alias=table)
        two = functions(self.image("beta"), alias=table)
        self.assertEqual(sorted(one), ["alpha", "beta", "gamma"])
        self.assertEqual(compare(one, two), ([], [], []))

    def test_without_the_table_the_printed_alias_decides(self):
        self.assertEqual(
            compare(functions(self.image("alpha")), functions(self.image("beta")))[1:],
            (["beta"], ["alpha"]),
        )

    def test_genuinely_different_functions_stay_distinct(self):
        table = aliases(self.NM)
        one = functions(self.image("alpha"), alias=table)
        two = functions(self.image("beta", "nop"), alias=table)
        self.assertEqual(compare(one, two), (["gamma"], [], []))

    def test_a_call_to_a_folded_body_names_it_by_the_same_alias(self):
        table = aliases(self.NM)
        caller = (
            "20003000 <caller>:\n20003000:      \tbl\t0x20001000 <{}> @ imm = #0x0\n"
        )
        one = functions(caller.format("alpha"), alias=table)
        two = functions(caller.format("beta"), alias=table)
        self.assertEqual(compare(one, two), ([], [], []))
        self.assertIn("bl <alpha>", one["caller"][0])

    def test_a_function_that_changes_folding_group_is_a_change(self):
        # z is neither group's least name, so only its own entry shows the body it now has.
        nm = (
            "20001000 00000004 T a_false\n20001000 00000004 T b_false\n"
            "20001000 00000004 T z\n"
            "20002000 00000004 T a_true\n20002000 00000004 T c_true\n"
        )
        base = (
            "20001000 <a_false>:\n20001000:      \tmovs\tr0, #0x0\n\n"
            "20002000 <a_true>:\n20002000:      \tmovs\tr0, #0x1\n"
        )
        new_nm = (
            "20001000 00000004 T a_false\n20001000 00000004 T b_false\n"
            "20002000 00000004 T a_true\n20002000 00000004 T c_true\n"
            "20002000 00000004 T z\n"
        )
        base_f = functions(base, alias=aliases(nm))
        new_f = functions(base, alias=aliases(new_nm))
        self.assertEqual(compare(base_f, new_f), (["z"], [], []))

    def test_symbols_of_unknown_type_fold_too(self):
        # llvm-nm prints `?` for the symbols of a section it cannot classify, such as the packed SDRAM code.
        nm = "0c210fb0 00000008 ? alpha\n0c210fb0 00000008 ? beta\n"
        self.assertEqual(aliases(nm), {0x0C210FB0: ("alpha", "beta")})

    def test_aliases_are_named_through_the_rename(self):
        table = aliases(self.NM, lambda n: {"alpha": "zeta"}.get(n, n))
        self.assertEqual(table, {0x20001000: ("beta", "zeta")})


class WrappedNameKeys(unittest.TestCase):
    PAIR: ClassVar[list[tuple[str, str]]] = [("deluge::gfx", "deluge::ui::gfx")]

    def names(self, table, listings, images=None):
        with mock.patch.object(compare_codegen, "run", fake_cxxfilt(table)):
            return demangler(
                "cxxfilt",
                listings,
                namespace_renamer(self.PAIR),
                mangled_renamer(self.PAIR),
                *([images] if images else []),
            )

    def test_two_guard_destructors_for_different_locals_stay_distinct(self):
        a = "__dtor__ZZN6deluge3gfx1fEvE5scene"
        b = "__dtor__ZZN6deluge3gfx1fEvE4list"
        table = {
            a[7:]: "deluge::gfx::f()::scene",
            b[7:]: "deluge::gfx::f()::list",
        }
        rename = self.names(table, [f"10 <{a}>:\n10 <{b}>:\n"])
        self.assertNotEqual(rename(a), rename(b))
        self.assertIn("(", rename(a))

    def test_data_in_an_anonymous_namespace_keeps_its_name(self):
        a = "__const._ZN6deluge3gfx12_GLOBAL__N_11aE.x"
        b = "__const._ZN6deluge3gfx12_GLOBAL__N_11bE.x"
        table = {
            a[8:-2]: "deluge::gfx::(anonymous namespace)::a",
            b[8:-2]: "deluge::gfx::(anonymous namespace)::b",
        }
        rename = self.names(table, [f"10 <{a}>:\n10 <{b}>:\n"])
        self.assertNotEqual(rename(a), rename(b))

    def test_names_that_differ_only_in_a_back_reference_stay_distinct(self):
        a = "_ZZZN3foo1fEENK3$_0clIS3_EEDaT_"
        b = "_ZZZN3foo1fEENK3$_0clIS4_EEDaT_"
        listing = f"10 <{a}>:\n10 <{b}>:\n"
        rename = self.names({}, [listing])
        self.assertNotEqual(rename(a), rename(b))
        caller = "20 <caller>:\n20:      \tbl\t0x10 <{}> @ imm = #0x0\n"
        base = functions(caller.format(a), rename)
        new = functions(caller.format(b), rename)
        self.assertEqual(compare(base, new), (["caller"], [], []))

    def test_a_call_retargeted_between_lambdas_of_const_members_is_a_change(self):
        a = "_ZZNK16BatteryLevelItem6renderEvENK3$_0clEv"
        b = "_ZZNK8ModKnobs10next_timerEvENK3$_0clEv"
        table = {
            a: "BatteryLevelItem::render() const::$_0::operator()() const",
            b: "ModKnobs::next_timer() const::$_0::operator()() const",
        }
        listing = f"10 <{a}>:\n10:\tbx\tlr\n\n14 <{b}>:\n14:\tbx\tlr\n"
        caller = "20 <caller>:\n20:      \tbl\t0x{} <{}> @ imm = #0x0\n"
        rename = self.names(
            table, [listing + caller.format(10, a) + caller.format(14, b)]
        )
        self.assertNotEqual(rename(a), rename(b))
        base = functions(listing + "\n" + caller.format(10, a), rename)
        new = functions(listing + "\n" + caller.format(14, b), rename)
        self.assertEqual(compare(base, new), (["caller"], [], []))

    def test_a_pointer_retargeted_between_anonymous_namespace_data_is_a_change(self):
        a = "_ZN6deluge12_GLOBAL__N_16kTableE"
        b = "_ZN6deluge12_GLOBAL__N_16kOtherE"
        table = {
            a: "deluge::(anonymous namespace)::kTable",
            b: "deluge::(anonymous namespace)::kOther",
        }
        nm = f"20251190 00000010 d {a}\n20251200 00000010 d {b}\n"
        rename = self.names(table, [nm])
        classify = classifier(
            "LOAD 0x0 0x20000000 0x20000000 0x0 0x00400000 0x00400000 RW 0x4\n",
            nm,
            rename,
        )
        self.assertNotEqual(classify(0x20251190), classify(0x20251200))
        new = SAMPLE.replace("0x20251190", "0x20251200")
        self.assertEqual(
            compare(
                functions(SAMPLE, classify=classify), functions(new, classify=classify)
            ),
            (["foo"], [], []),
        )

    def test_collapsing_is_judged_per_image(self):
        # The same pair of spellings in two images, one name each, is a rename, not a collision.
        a = "_ZZZN6deluge3gfx1fEENK3$_0clIS3_EEDaT_"
        b = "_ZZZN6deluge2ui3gfx1fEENK3$_0clIS4_EEDaT_"
        rename = self.names(
            {}, [f"10 <{a}>:\n", f"10 <{b}>:\n"], [[f"10 <{a}>:\n"], [f"10 <{b}>:\n"]]
        )
        self.assertEqual(rename(a), rename(b))


class MangledRenames(unittest.TestCase):
    def test_a_component_inside_a_longer_identifier_is_left(self):
        rename = mangled_renamer(
            [("deluge::instrument", "deluge::instruments::classic")]
        )
        # `ab6deluge` is one nine-character component, followed by `instrument`.
        self.assertEqual(
            rename("_ZN9ab6deluge10instrument1fEv"), "_ZN9ab6deluge10instrument1fEv"
        )

    def test_a_namespace_nested_in_another_is_left(self):
        rename = mangled_renamer(
            [("deluge::instrument", "deluge::instruments::classic")]
        )
        self.assertEqual(
            rename("_ZN3abc6deluge10instrument1fEv"), "_ZN3abc6deluge10instrument1fEv"
        )

    def test_an_operator_of_a_renamed_namespace_is_renamed(self):
        rename = mangled_renamer([("deluge::gfx", "deluge::ui::gfx")])
        self.assertEqual(
            rename("_ZN6deluge3gfxplERKS0_"), rename("_ZN6deluge2ui3gfxplERKS0_")
        )

    def test_a_name_without_a_renamed_namespace_keeps_its_back_references(self):
        rename = mangled_renamer([("deluge::scenes", "deluge::ui::scenes")])
        a = "_ZZZN3foo1fEENK3$_0clIS3_EEDaT_"
        b = "_ZZZN3foo1fEENK3$_0clIS4_EEDaT_"
        self.assertNotEqual(rename(a), rename(b))

    def test_a_name_with_a_renamed_namespace_in_either_spelling_collapses(self):
        rename = mangled_renamer([("deluge::scenes", "deluge::ui::scenes")])
        a = "_ZN6deluge6scenes1fENS_6scenes1XES3_"
        b = "_ZN6deluge2ui6scenes1fENS_2ui6scenes1XES4_"
        self.assertEqual(rename(a), rename(b))

    def test_a_ref_qualified_member_is_renamed_behind_its_substitution(self):
        rename = mangled_renamer([("deluge::scenes", "deluge::ui::scenes")])
        self.assertEqual(
            rename("_ZNKR6deluge3barENS_6scenes1XE"),
            rename("_ZNKR6deluge3barENS_2ui6scenes1XE"),
        )

    def test_a_substitution_is_renamed_only_where_it_stands_for_the_namespaces_head(
        self,
    ):
        rename = mangled_renamer([("deluge::scenes", "deluge::ui::scenes")])
        # S_ is `foo` here, so `S_6scenes` is foo::scenes.
        self.assertEqual(
            rename("_ZN3foo3barENS_6scenes1XE"), "_ZN3foo3barENS_6scenes1XE"
        )
        self.assertEqual(
            rename("_ZN6deluge3barENS_6scenes1XE"),
            rename("_ZN6deluge3barENS_2ui6scenes1XE"),
        )


if __name__ == "__main__":
    unittest.main()
