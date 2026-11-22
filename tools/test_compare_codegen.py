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
    number_twins,
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
# A device image's defined symbols, mangled, in address order (`llvm-nm -n --defined-only`, names only, every symbol
# listed, so names that two symbols share appear twice).
NAMES_FIXTURE = os.path.join(
    os.path.dirname(__file__), "testdata", "compare_codegen_names.txt"
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


# What may follow an own parameter list in a name that shares a key: cv and ref qualifiers, and a requires clause in
# which no `::` follows a bracket or a quote (that would be a local's name after its enclosing function's clause).
ORACLE_TAIL = re.compile(
    r"(?: const| volatile| ?&&?)*(?: requires (?:(?![)>'}]::).)*)?"
)
# A compiler clone's suffix.
ORACLE_CLONE = re.compile(r"(?: \(\.[\w.]+\))+$")


def names_a_local(base: str) -> bool:
    """Whether @p base holds a top-level `(…)` group other than `(anonymous namespace)`: an enclosing function."""
    base = re.sub(r"\boperator(?:\(\)|\[\]|[^\w\s(]+)", "operator", base)
    depth = 0
    for i, c in enumerate(base):
        if c in "(<{[":
            if (
                depth == 0
                and c == "("
                and not base.startswith("(anonymous namespace)", i)
            ):
                return True
            depth += 1
        elif c in ")>}]":
            depth -= 1
    return False


def is_own_signature_of(key: str, name: str) -> bool:
    """Whether @p name is the entity @p key names plus its own parameter list, the qualifiers after it and, unless the
    key names a local, a leading return type: what one key may hold several of."""
    veneer = re.match(r"__\w*?Thunk_", key)
    if veneer:
        if not name.startswith(veneer.group(0)):
            return False
        key, name = key[veneer.end() :], name[veneer.end() :]
    clone = ORACLE_CLONE.search(key)
    if clone:
        if not name.endswith(clone.group(0)):
            return False
        key, name = key[: clone.start()], name[: -len(clone.group(0))]
    returns = not names_a_local(key)
    at = name.find(key + "(")
    while at >= 0:
        if at == 0 or (returns and name[at - 1] == " "):
            rest = name[at + len(key) :]
            depth = 0
            for i, c in enumerate(rest):
                depth += {"(": 1, ")": -1}.get(c, 0)
                if depth == 0:
                    if rest[
                        : i + 1
                    ] != "(anonymous namespace)" and ORACLE_TAIL.fullmatch(
                        rest[i + 1 :]
                    ):
                        return True
                    break
        at = name.find(key + "(", at + 1)
    return False


def shared_keys(names: list[str], cxxfilt: str) -> dict[str, list[str]]:
    """The keys the tool gives one image whose symbols are @p names in address order (what number_twins() and then
    demangler() make of them) that several of its symbols share, each with those symbols."""
    nm = "".join(
        f"{4 * i:08x} 00000004 t {n}\n"
        for i, n in enumerate(names)
        if not n.startswith("$")
    )
    nm, _ = number_twins(nm, "")
    rename = demangler(cxxfilt, [nm], mangled=mangled_renamer([]))
    groups: dict[str, list[str]] = {}
    for line in nm.splitlines():
        name = line.split()[-1]
        groups.setdefault(rename(compare_codegen.LTO_SUFFIX.sub("", name)), []).append(
            f"{line.split()[0]} {name}"
        )
    return {k: v for k, v in groups.items() if len(v) > 1}


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
            "auto X::f() const::$_0::operator()<int>",
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

    def test_a_templates_lambdas_and_locals_keep_its_return_type(self):
        self.assertNotEqual(
            canonical("int r<int>()::'lambda'()::operator()() const"),
            canonical("long r<int>()::'lambda'()::operator()() const"),
        )
        self.assertNotEqual(canonical("int r<int>()::x"), canonical("long r<int>()::x"))
        self.assertEqual(
            canonical("int r<int>()::'lambda'()::operator()() const"),
            "int r<int>()::'lambda'()::operator()",
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

    def assert_no_two_symbols_share_a_key(self, mangled: list[str]):
        cxxfilt = shutil.which("llvm-cxxfilt")
        self.assertIsNotNone(cxxfilt, "the property test needs llvm-cxxfilt on PATH")
        self.assertEqual(shared_keys(mangled, cxxfilt), {})

    def test_no_two_symbols_share_a_key_in_the_fixture_image(self):
        with open(NAMES_FIXTURE) as f:
            self.assert_no_two_symbols_share_a_key(f.read().split())

    def test_no_two_symbols_share_a_key_in_the_live_image(self):
        if not (os.path.exists(REAL_IMAGE) and shutil.which("llvm-nm")):
            self.skipTest(
                "no build/RelWithDebInfo/deluge.elf or llvm-nm; "
                "the fixture test holds the property for a checked-in image"
            )
        listing = subprocess.run(
            ["llvm-nm", "-n", "--defined-only", REAL_IMAGE],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        self.assert_no_two_symbols_share_a_key(
            [
                line.split()[-1]
                for line in listing.splitlines()
                if len(line.split()) == 3
            ]
        )

    def test_the_oracle_rejects_what_must_not_share_a_key(self):
        self.assertFalse(
            is_own_signature_of("S<int>::g", "S<int>::g() requires C<T>::x")
        )
        self.assertFalse(
            is_own_signature_of(
                "S<int>::g",
                "S<int>::g() requires C<T>::'lambda0'()::operator()() const",
            )
        )
        self.assertFalse(
            is_own_signature_of(
                "r<int>()::'lambda'()::operator()",
                "long r<int>()::'lambda'()::operator()() const",
            )
        )
        self.assertFalse(
            is_own_signature_of("deluge::", "deluge::(anonymous namespace)::kTable")
        )
        self.assertTrue(
            is_own_signature_of(
                "std::expected<int, E>::~expected",
                "std::expected<int, E>::~expected() requires !is_trivially_destructible_v<T>",
            )
        )
        self.assertTrue(
            is_own_signature_of(
                "std::get<0u>", "int& std::get<0u>(std::array<int, 3u>&)"
            )
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

    def test_two_bodies_under_one_key_are_refused(self):
        # Overloads get a key each (demangler()); a rename that still gives two bodies one key would hide a call
        # moved between them, so it is an error rather than a set to compare.
        plain = {"_Z1fi": "f", "_Z1fj": "f"}
        rename = lambda name: plain.get(name, name)
        one = "10 <_Z1fi>:\n10:\tadd\tr0, r0, r1\n\n20 <_Z1fj>:\n20:\tsub\tr0, r0, r1\n"
        with self.assertRaises(ValueError):
            functions(one, rename)

    def test_a_body_listed_twice_at_its_address_is_read_once(self):
        twice = "10 <f>:\n10:\tbx\tlr\n\n10 <f>:\n10:\tbx\tlr\n"
        self.assertEqual(functions(twice), {"f": ("bx lr",)})


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


class MainTest(unittest.TestCase):
    def test_one_image_given_twice_compares_identical(self):
        def fake(tool, stdin=None):
            if tool[0] == "objdump":
                return SAMPLE
            if tool[0] == "cxxfilt":
                return stdin + "\n"
            return ""

        argv = [
            "compare_codegen.py",
            "a.elf",
            "a.elf",
            "--objdump",
            "objdump",
            "--cxxfilt",
            "cxxfilt",
        ]
        with (
            mock.patch.object(compare_codegen, "run", fake),
            mock.patch.object(compare_codegen.sys, "argv", argv),
            mock.patch("builtins.print") as printed,
        ):
            self.assertEqual(compare_codegen.main(), 0)
        self.assertIn(mock.call("changed: 0"), printed.call_args_list)


class MainHarness:
    """main() over fake tools: a disassembly and an `nm` listing per image path, DEMANGLED for llvm-cxxfilt and
    GNU_DEMANGLED for GNU c++filt."""

    GNU_DEMANGLED: ClassVar[dict[str, str]] = {}

    SEGMENTS = "LOAD 0x0 0x20000000 0x20000000 0x0 0x00400000 0x00400000 RW 0x4\n"
    DEMANGLED: ClassVar[dict[str, str]] = {
        "_ZL1k": "k",
        "_ZN12_GLOBAL__N_14flagEb": "(anonymous namespace)::flag(bool)",
    }

    def run_main(
        self, images: dict[str, tuple[str, str]], *args: str
    ) -> tuple[int, list]:
        """main() over fake images: path -> (disassembly, `nm -S -n` listing); @p args go on the command line."""

        def fake(tool, stdin=None):
            if tool[0] == "cxxfilt":
                return (
                    "\n".join(self.DEMANGLED.get(n, n) for n in stdin.splitlines())
                    + "\n"
                )
            if tool[0].endswith("c++filt"):
                return (
                    "\n".join(self.GNU_DEMANGLED.get(n, n) for n in stdin.splitlines())
                    + "\n"
                )
            disassembly, nm = images[tool[-1]]
            if tool[0] == "nm" and "--special-syms" not in tool:
                # llvm-nm lists mapping symbols (`$a`, `$t`, `$d`) only when asked to.
                nm = "".join(
                    line for line in nm.splitlines(keepends=True) if " $" not in line
                )
            return {"objdump": disassembly, "nm": nm, "readelf": self.SEGMENTS}[tool[0]]

        argv = ["compare_codegen.py", *images, "--objdump", "objdump", "--nm", "nm"]
        argv += ["--readelf", "readelf", "--cxxfilt", "cxxfilt", *args]
        with (
            mock.patch.object(compare_codegen, "run", fake),
            mock.patch.object(compare_codegen.sys, "argv", argv),
            mock.patch("builtins.print") as printed,
        ):
            return compare_codegen.main(), printed.call_args_list


class SameNamedSymbols(MainHarness, unittest.TestCase):
    """Two symbols of one image with one name (promoted statics whose names differ only in `.llvm.<hash>`, or static
    and anonymous-namespace symbols of two modules) are told apart by the symbol before them and their order."""

    def promoted(self, first: str, second: str, word: str) -> tuple[str, str]:
        nm = (
            "20001000 00000018 T foo\n"
            f"20251190 00000010 d _ZL1k.llvm.{first}\n"
            f"20251200 00000010 d _ZL1k.llvm.{second}\n"
        )
        return SAMPLE.replace("0x20251190", word), nm

    def test_a_pointer_retargeted_between_two_promoted_statics_is_a_change(self):
        status, printed = self.run_main(
            {
                "a.elf": self.promoted("111", "222", "0x20251190"),
                "b.elf": self.promoted("333", "444", "0x20251200"),
            }
        )
        self.assertEqual(status, 1)
        self.assertIn(mock.call("changed: 1"), printed)

    def test_the_same_layout_under_other_hashes_is_identical(self):
        status, printed = self.run_main(
            {
                "a.elf": self.anchored(
                    [("a_mod", 0x20251190, "111"), ("b_mod", 0x20251200, "222")],
                    "0x20251190",
                ),
                "b.elf": self.anchored(
                    [("a_mod", 0x20251190, "333"), ("b_mod", 0x20251200, "444")],
                    "0x20251190",
                ),
            }
        )
        self.assertEqual(status, 0)
        self.assertIn(mock.call("changed: 0"), printed)

    def test_copies_behind_one_anchor_never_pair(self):
        # Back to back behind `foo`: the first copy dropped, a third added after the second, `foo` moved to the second.
        base = (
            SAMPLE,
            (
                "20001000 00000018 T foo\n20251190 00000010 d _ZL1k.llvm.111\n20251200 00000010 d _ZL1k.llvm.222\n"
            ),
        )
        new = (
            SAMPLE.replace("0x20251190", "0x20251200"),
            (
                "20001000 00000018 T foo\n20251200 00000010 d _ZL1k.llvm.222\n20251300 00000010 d _ZL1k.llvm.333\n"
            ),
        )
        status, printed = self.run_main({"a.elf": base, "b.elf": new})
        self.assertEqual(status, 1)
        self.assertIn(mock.call("  foo"), printed)
        self.assertNotIn(mock.call("changed: 0"), printed)

    def arm_adr_image(self, imm: int) -> tuple[str, str]:
        # An ARM `adr` at 0x20001008: Align(0x20001010, 4) + imm. `$a` marks the function as ARM code.
        listing = (
            "20001000 <foo>:\n20001000:      \tpush\t{r4, lr}\n"
            f"20001008:      \tadr\tr0, #{imm} <_ZL1k.llvm.111>\n20001010:      \tpop\t{{r4, pc}}\n"
        )
        nm = (
            "20001000 00000000 t $a\n20001000 00000018 T foo\n20001020 00000010 d p_mod\n"
            "20001100 00000010 d _ZL1k.llvm.111\n20001120 00000010 d q_mod\n20001200 00000010 d _ZL1k.llvm.222\n"
        )
        return listing, nm

    def test_an_arm_adr_is_placed_by_the_arm_pc(self):
        # 0x20001010 + 0xf0 is the first copy; + 0x1f0 the second. As Thumb, 0xf0 would land 4 bytes short of it.
        status, printed = self.run_main(
            {"a.elf": self.arm_adr_image(0xF0), "b.elf": self.arm_adr_image(0x1F0)}
        )
        self.assertEqual(status, 1)
        self.assertIn(mock.call("  foo"), printed)
        status, _ = self.run_main(
            {"a.elf": self.arm_adr_image(0xF0), "b.elf": self.arm_adr_image(0xF0)}
        )
        self.assertEqual(status, 0)

    def test_an_arm_adr_that_lands_in_no_copy_is_an_error(self):
        # 0x20001010 + 0x100 is just past the first copy; as Thumb it would land inside it.
        with self.assertRaises(ValueError):
            self.run_main(
                {"a.elf": self.arm_adr_image(0x100), "b.elf": self.arm_adr_image(0x100)}
            )

    def test_a_call_retargeted_between_two_modules_statics_is_a_change(self):
        flag = "_ZN12_GLOBAL__N_14flagEb"
        listing = (
            f"10 <{flag}>:\n10:\tbx\tlr\n\n14 <{flag}>:\n14:\tmovs\tr0, #0x1\n\n"
            "20 <caller>:\n20:      \tbl\t0x{} <" + flag + "> @ imm = #0x0\n"
        )
        nm = f"00000010 00000004 t {flag}\n00000014 00000004 t {flag}\n00000020 00000004 T caller\n"
        status, printed = self.run_main(
            {"a.elf": (listing.format(10), nm), "b.elf": (listing.format(14), nm)}
        )
        self.assertEqual(status, 1)
        self.assertIn(mock.call("changed: 1"), printed)
        self.assertIn(mock.call("  caller"), printed)

    def anchored(
        self, copies: list[tuple[str, int, str]], word: str
    ) -> tuple[str, str]:
        """An image whose `foo` points at @p word, with each copy of `_ZL1k` (module anchor, address, hash) placed
        behind its module's own symbol."""
        nm = "20001000 00000018 T foo\n" + "".join(
            f"{a - 0x10:08x} 00000010 d {anchor}\n{a:08x} 00000010 d _ZL1k.llvm.{h}\n"
            for anchor, a, h in copies
        )
        return SAMPLE.replace("0x20251190", word), nm

    def test_a_pointer_moved_to_another_copy_when_one_is_dropped_and_one_added_is_a_change(
        self,
    ):
        status, printed = self.run_main(
            {
                "a.elf": self.anchored(
                    [("a_mod", 0x20251190, "111"), ("b_mod", 0x20251200, "222")],
                    "0x20251190",
                ),
                "b.elf": self.anchored(
                    [("b_mod", 0x20251200, "222"), ("c_mod", 0x20251220, "333")],
                    "0x20251200",
                ),
            }
        )
        self.assertEqual(status, 1)
        self.assertIn(mock.call("  foo"), printed)
        self.assertNotIn(mock.call("changed: 0"), printed)

    def test_the_same_copies_behind_the_same_anchors_compare_identical(self):
        copies = [("a_mod", 0x20251190, "111"), ("b_mod", 0x20251200, "222")]
        status, _ = self.run_main(
            {
                "a.elf": self.anchored(copies, "0x20251200"),
                "b.elf": self.anchored(copies, "0x20251200"),
            }
        )
        self.assertEqual(status, 0)

    def test_a_call_moved_to_another_function_copy_when_one_is_dropped_and_one_added_is_a_change(
        self,
    ):
        flag = "_ZN12_GLOBAL__N_14flagEb"

        def image(mods: list[tuple[str, int]], target: int) -> tuple[str, str]:
            listing = "".join(
                f"{a - 4:x} <{m}>:\n{a - 4:x}:\tnop\n\n{a:x} <{flag}>:\n{a:x}:\tbx\tlr\n\n"
                for m, a in mods
            )
            listing += (
                f"100 <caller>:\n100:      \tbl\t0x{target:x} <{flag}> @ imm = #0x0\n"
            )
            nm = "".join(
                f"{a - 4:08x} 00000004 t {m}\n{a:08x} 00000004 t {flag}\n"
                for m, a in mods
            )
            return listing, nm + "00000100 00000004 T caller\n"

        status, printed = self.run_main(
            {
                "a.elf": image([("p_mod", 0x14), ("q_mod", 0x24)], 0x14),
                "b.elf": image([("q_mod", 0x24), ("r_mod", 0x34)], 0x24),
            }
        )
        self.assertEqual(status, 1)
        self.assertIn(mock.call("  caller"), printed)
        self.assertNotIn(mock.call("changed: 0"), printed)

    def adr_image(self, target_hash_first: bool) -> tuple[str, str]:
        # A Thumb `adr` at 0x20001008: Align(0x2000100c, 4) + imm. objdump names its target without an address.
        imm, name = (
            (0xF4, "_ZL1k.llvm.111") if target_hash_first else (0x1F4, "_ZL1k.llvm.222")
        )
        listing = (
            "20001000 <foo>:\n20001000:      \tpush\t{r4, lr}\n"
            f"20001008:      \tadr\tr0, #{imm} <{name}>\n20001010:      \tpop\t{{r4, pc}}\n"
        )
        nm = (
            "20001000 00000018 T foo\n20001020 00000010 d p_mod\n20001100 00000010 d _ZL1k.llvm.111\n"
            "20001120 00000010 d q_mod\n20001200 00000010 d _ZL1k.llvm.222\n"
        )
        return listing, nm

    def test_an_adr_moved_to_another_copy_is_a_change(self):
        status, printed = self.run_main(
            {"a.elf": self.adr_image(True), "b.elf": self.adr_image(False)}
        )
        self.assertEqual(status, 1)
        self.assertIn(mock.call("  foo"), printed)

    def test_an_adr_that_lands_in_no_copy_is_an_error(self):
        listing, nm = self.adr_image(True)
        listing = listing.replace("#244", "#4")
        with self.assertRaises(ValueError):
            number_twins(nm, listing)


class RenamedMangledKeys(MainHarness, unittest.TestCase):
    """Under --rename, a name keyed by its mangled text has its substitution references reduced to `S_`, which can
    make two entities' mangled texts equal across images; their demangled text keeps them apart."""

    DEMANGLED: ClassVar[dict[str, str]] = {
        "_ZN6deluge3gfx1AINS0_1XEEC1Ev": "deluge::gfx::A<deluge::gfx::X>::A()",
        "_ZN6deluge3gfx1AINS0_1XEEC2Ev": "deluge::gfx::A<deluge::gfx::X>::A()",
        "_ZN6deluge3gfx1AINS_1XEEC1Ev": "deluge::gfx::A<deluge::X>::A()",
        "_ZN6deluge3gfx1AINS_1XEEC2Ev": "deluge::gfx::A<deluge::X>::A()",
        "_ZZN6deluge3gfx1fINS0_1XEEEvvE1x": "void deluge::gfx::f<deluge::gfx::X>()::x",
        "_ZZN6deluge3gfx1fINS0_1XEEEvvE1x_0": "void deluge::gfx::f<deluge::gfx::X>()::x",
        "_ZZN6deluge3gfx1fINS_1XEEEvvE1x": "void deluge::gfx::f<deluge::X>()::x",
        "_ZZN6deluge3gfx1fINS_1XEEEvvE1x_0": "void deluge::gfx::f<deluge::X>()::x",
    }
    RENAME = ("--rename", "deluge::gfx=deluge::ui::gfx")

    def constructors(self, x: str) -> tuple[str, str]:
        c1, c2 = (f"_ZN6deluge3gfx1AIN{x}1XEEC{v}Ev" for v in "12")
        listing = (
            f"10 <{c1}>:\n10:\tbx\tlr\n\n14 <{c2}>:\n14:\tbx\tlr\n\n"
            f"20 <caller>:\n20:      \tbl\t0x14 <{c2}> @ imm = #0x0\n"
        )
        nm = f"00000010 00000004 t {c1}\n00000014 00000004 t {c2}\n00000020 00000004 T caller\n"
        return listing, nm

    def test_a_call_moved_to_an_entity_whose_reduced_mangled_text_is_alike_is_a_change(
        self,
    ):
        status, printed = self.run_main(
            {"a.elf": self.constructors("S0_"), "b.elf": self.constructors("S_")},
            *self.RENAME,
        )
        self.assertEqual(status, 1)
        self.assertIn(mock.call("  caller"), printed)
        self.assertNotIn(mock.call("changed: 0"), printed)

    def test_a_pointer_moved_to_data_whose_reduced_mangled_text_is_alike_is_a_change(
        self,
    ):
        def image(x: str) -> tuple[str, str]:
            local = f"_ZZN6deluge3gfx1fIN{x}1XEEEvvE1x"
            nm = f"20001000 00000018 T foo\n20251190 00000010 d {local}\n20251200 00000010 d {local}_0\n"
            return SAMPLE, nm

        status, printed = self.run_main(
            {"a.elf": image("S0_"), "b.elf": image("S_")}, *self.RENAME
        )
        self.assertEqual(status, 1)
        self.assertIn(mock.call("  foo"), printed)
        self.assertNotIn(mock.call("changed: 0"), printed)


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

        base = "".join(f"10 <{n}>:\n" for n in (self.OLD, self.OTHER, self.PLAIN))
        moved = f"10 <{self.NEW}>:\n"
        with mock.patch.object(compare_codegen, "run", fake):
            return demangler(
                "cxxfilt",
                [base, moved],
                namespace_renamer([("deluge::gfx", "deluge::ui::gfx")]),
                images=[[base], [moved]],
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


def gnu_only(table):
    """A stand-in for `run`: names in @p table demangle only through GNU c++filt (`gnu-cxxfilt`)."""

    def fake(tool, stdin=None):
        known = table if tool[0] == "gnu-cxxfilt" else {}
        return "\n".join(known.get(n, n) for n in stdin.splitlines()) + "\n"

    return fake


class WrappedNames(unittest.TestCase):
    PAIR = (("deluge::gfx", "deluge::ui::gfx"),)
    TABLE: ClassVar[dict[str, str]] = {
        "_ZN6deluge3gfx1kE": "deluge::gfx::k",
        "_ZN6deluge2ui3gfx1kE": "deluge::ui::gfx::k",
        "_ZZN6deluge3gfx1fEvE5scene": "deluge::gfx::f()::scene",
        "_ZZN6deluge2ui3gfx1fEvE5scene": "deluge::ui::gfx::f()::scene",
    }

    def names(self, old, new):
        """A rename for two images, @p old's and @p new's (one symbol each)."""
        images = [[f"10 <{old}>:\n"], [f"10 <{new}>:\n"]]
        with mock.patch.object(compare_codegen, "run", fake_cxxfilt(self.TABLE)):
            return demangler(
                "cxxfilt",
                [text for image in images for text in image],
                namespace_renamer(list(self.PAIR)),
                mangled_renamer(list(self.PAIR)),
                images,
            )

    def test_a_data_name_follows_its_symbols_rename_and_keeps_its_wrapping(self):
        old = "__const._ZN6deluge3gfx1kE.sources"
        new = "__const._ZN6deluge2ui3gfx1kE.sources"
        rename = self.names(old, new)
        self.assertEqual(rename(old), rename(new))
        self.assertTrue(rename(old).startswith("__const."))
        self.assertTrue(rename(old).endswith(".sources"))

    def test_a_guard_destructor_name_follows_its_symbols_rename(self):
        old = "__dtor__ZZN6deluge3gfx1fEvE5scene"
        new = "__dtor__ZZN6deluge2ui3gfx1fEvE5scene"
        rename = self.names(old, new)
        self.assertEqual(rename(old), rename(new))
        self.assertTrue(rename(old).startswith("__dtor_"))

    def test_a_name_llvm_cxxfilt_rejects_follows_a_rename_through_gnu_cxxfilt(self):
        old = "_ZZZN6deluge3gui1fERKNS_6scenes1WEENK3$_0clIZNS2_1fES7_E3$_1EEDav"
        new = "_ZZZN6deluge3gui1fERKNS_2ui6scenes1WEENK3$_0clIZNS2_1fES8_E3$_1EEDav"
        lambda_ = "::$_0::operator()<deluge::gui::f({0} const&)::$_1>() const"
        texts = {
            old: "auto deluge::gui::f(deluge::scenes::W const&)"
            + lambda_.format("deluge::scenes::W"),
            new: "auto deluge::gui::f(deluge::ui::scenes::W const&)"
            + lambda_.format("deluge::ui::scenes::W"),
        }
        pair = [("deluge::scenes", "deluge::ui::scenes")]
        with mock.patch.object(compare_codegen, "run", gnu_only(texts)):
            rename = demangler(
                "cxxfilt",
                [f"10 <{old}>:\n10 <{new}>:\n"],
                namespace_renamer(pair),
                mangled_renamer(pair),
                [[f"10 <{old}>:\n"], [f"10 <{new}>:\n"]],
                "gnu-cxxfilt",
            )
        self.assertEqual(rename(old), rename(new))
        self.assertIn(
            " | auto deluge::gui::f(deluge::ui::scenes::W const&)", rename(old)
        )

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

    def keys(self, table):
        rename = self.names(table, ["".join(f"10 <{n}>:\n" for n in table)])
        return [rename(n) for n in table]

    def test_a_constrained_functions_locals_and_lambdas_stay_apart(self):
        table = {
            "_ZN1SIiE1gEvQ1CIT_E": "S<int>::g() requires C<T>",
            "_ZZN1SIiE1gEvQ1CIT_EE1x": "S<int>::g() requires C<T>::x",
            "_ZZN1SIiE1gEvQ1CIT_EENKUlvE_clEv": "S<int>::g() requires C<T>::'lambda'()::operator()() const",
            "_ZZN1SIiE1gEvQ1CIT_EENKUlvE0_clEv": "S<int>::g() requires C<T>::'lambda0'()::operator()() const",
        }
        self.assertEqual(len(set(self.keys(table))), 4)
        self.assertEqual(self.keys(table)[0], "S<int>::g")

    def test_constructor_and_destructor_variants_stay_apart(self):
        table = {
            "_ZN10FileReaderC1Ev": "FileReader::FileReader()",
            "_ZN10FileReaderC2Ev": "FileReader::FileReader()",
            "_ZN10FileReaderD0Ev": "FileReader::~FileReader()",
            "_ZN10FileReaderD1Ev": "FileReader::~FileReader()",
            "_ZN10FileReaderD2Ev": "FileReader::~FileReader()",
            "_ZZN1XD2EvE1x": "X::~X()::x",
            "_ZZN1XD1EvE1x": "X::~X()::x",
        }
        self.assertEqual(len(set(self.keys(table))), 7)

    def assert_retarget_is_a_change(self, d2, d0, text):
        table = {d2: text, d0: text}
        listing = f"10 <{d2}>:\n10:\tbx\tlr\n\n14 <{d0}>:\n14:\tbx\tlr\n"
        caller = "20 <caller>:\n20:      \tbl\t0x{} <{}> @ imm = #0x0\n"
        rename = self.names(
            table, [listing + caller.format(10, d2) + caller.format(14, d0)]
        )
        base = functions(listing + "\n" + caller.format(10, d2), rename)
        new = functions(listing + "\n" + caller.format(14, d0), rename)
        self.assertEqual(compare(base, new), (["caller"], [], []))

    def test_a_call_retargeted_from_the_base_to_the_deleting_destructor_is_a_change(
        self,
    ):
        self.assert_retarget_is_a_change(
            "_ZN10FileReaderD2Ev", "_ZN10FileReaderD0Ev", "FileReader::~FileReader()"
        )

    def test_a_class_whose_name_starts_with_E_keeps_its_variants(self):
        self.assert_retarget_is_a_change(
            "_ZN6deluge6EnvLfoD2Ev",
            "_ZN6deluge6EnvLfoD0Ev",
            "deluge::EnvLfo::~EnvLfo()",
        )
        self.assert_retarget_is_a_change(
            "_ZN6deluge2ui9EditLabelD2Ev",
            "_ZN6deluge2ui9EditLabelD0Ev",
            "deluge::ui::EditLabel::~EditLabel()",
        )

    def test_a_namespace_whose_name_starts_with_an_underscore_keeps_its_variants(self):
        text = "std::__cxx11::basic_stringbuf<char, std::char_traits<char>, std::allocator<char>>::~basic_stringbuf()"
        self.assert_retarget_is_a_change(
            "_ZNSt7__cxx1115basic_stringbufIcSt11char_traitsIcESaIcEED2Ev",
            "_ZNSt7__cxx1115basic_stringbufIcSt11char_traitsIcESaIcEED0Ev",
            text,
        )

    def test_a_float_templates_variants_stay_apart(self):
        self.assert_retarget_is_a_change(
            "_ZN6deluge3dsp5DelayIfLj16EE4TapsD2Ev",
            "_ZN6deluge3dsp5DelayIfLj16EE4TapsD0Ev",
            "deluge::dsp::Delay<float, 16u>::Taps::~Taps()",
        )

    def test_a_local_class_in_a_decltype_function_keeps_its_variants(self):
        self.assert_retarget_is_a_change(
            "_ZZ2f1IiEDTeqfL0p_LN1n4KindE7EET_EN5LocalD2Ev",
            "_ZZ2f1IiEDTeqfL0p_LN1n4KindE7EET_EN5LocalD0Ev",
            "decltype(fp == (n::Kind)7) f1<int>(int)::Local::~Local()",
        )

    def test_thunks_for_two_bases_stay_apart(self):
        self.assert_retarget_is_a_change(
            "_ZThn8_N1XD0Ev", "_ZThn16_N1XD0Ev", "non-virtual thunk to X::~X()"
        )
        table = {
            "_ZN4llvm15ScalarEvolution9ExitLimitC1Ev": "llvm::ScalarEvolution::ExitLimit::ExitLimit()",
            "_ZN4llvm15ScalarEvolution9ExitLimitC2Ev": "llvm::ScalarEvolution::ExitLimit::ExitLimit()",
            "_ZTv0_n12_N1XD1Ev": "virtual thunk to X::~X()",
            "_ZTv0_n16_N1XD1Ev": "virtual thunk to X::~X()",
        }
        self.assertEqual(len(set(self.keys(table))), 4)

    def test_discriminated_locals_stay_apart(self):
        table = {
            "_ZZ1hiE7options": "h(int)::options",
            "_ZZ1hiE7options_0": "h(int)::options",
            "_ZZ1giEN1X1mEv": "g(int)::X::m()",
            "_ZZ1giEN1X1mE_0v": "g(int)::X::m()",
            "_ZZ1fvE1x__12_": "f()::x",
        }
        self.assertEqual(len(set(self.keys(table))), 5)

    def test_a_symbol_alone_with_its_key_keeps_its_demangled_name(self):
        table = {
            "_ZN1WIL4Kind1EED2Ev": "W<(Kind)1>::~W()",
            "_ZZ1fvENKUlvE0_clEv": "f()::'lambda0'()::operator()() const",
            "_ZN6deluge12_GLOBAL__N_14kD2EE": "deluge::(anonymous namespace)::kD2E",
            "_ZNSt6vectorIiSaIiEEC2Ev": "std::vector<int, std::allocator<int>>::vector()",
        }
        self.assertEqual(
            self.keys(table),
            [
                "W<(Kind)1>::~W",
                "f()::'lambda0'()::operator()",
                "deluge::(anonymous namespace)::kD2E",
                "std::vector<int, std::allocator<int>>::vector",
            ],
        )

    def test_overloads_keep_their_mangled_names_and_follow_a_rename(self):
        # Overloads cannot share a key, so a signature change of one shows it removed and added: a false positive.
        old = {
            "_ZN6deluge3gfx1fEi": "deluge::gfx::f(int)",
            "_ZN6deluge3gfx1fEl": "deluge::gfx::f(long)",
        }
        new = {
            "_ZN6deluge2ui3gfx1fEi": "deluge::ui::gfx::f(int)",
            "_ZN6deluge2ui3gfx1fEl": "deluge::ui::gfx::f(long)",
        }
        listings = ["".join(f"10 <{n}>:\n" for n in names) for names in (old, new)]
        rename = self.names({**old, **new}, listings, [[listings[0]], [listings[1]]])
        self.assertEqual(
            [rename(n) for n in old],
            [
                "_ZN6deluge2ui3gfx1fEi | deluge::ui::gfx::f(int)",
                "_ZN6deluge2ui3gfx1fEl | deluge::ui::gfx::f(long)",
            ],
        )
        self.assertEqual([rename(n) for n in new], [rename(n) for n in old])

    def test_an_unmoved_name_beside_its_moved_twin_stays_apart(self):
        old, new = "_ZN6deluge3gfx1fEv", "_ZN6deluge2ui3gfx1fEv"
        table = {old: "deluge::gfx::f()", new: "deluge::ui::gfx::f()"}
        self.assertEqual(self.keys(table), [old, new])

    def test_a_split_globals_parts_and_a_clone_stay_apart(self):
        names = [
            "_ZN12_GLOBAL__N_14poolE.0",
            "_ZN12_GLOBAL__N_14poolE.1",
            "_Z1fv.constprop.0",
            "_Z1fv",
        ]
        plain = {
            "_ZN12_GLOBAL__N_14poolE": "(anonymous namespace)::pool",
            "_Z1fv": "f()",
        }
        rename = self.names(plain, ["".join(f"10 <{n}>:\n" for n in names)])
        self.assertEqual(
            [rename(n) for n in names],
            [
                "(anonymous namespace)::pool.0",
                "(anonymous namespace)::pool.1",
                "f.constprop.0",
                "f",
            ],
        )

    def test_rust_names_that_demangle_alike_keep_their_raw_names(self):
        core = "_RNvXNtNtCskEwzLGIm5x3_4core5slice3cmpShNtNtB6_3cmp9PartialEq2eq"
        a, b = (
            core + "Cs6NFbV2PXKjr_15deluge_bsp_rust",
            core + "Csj1bDsIbZyCz_14embedded_fatfs",
        )
        runtime = "_RNvNvNvNtNtCskEwzLGIm5x3_4core5slice5index16slice_index_fail{}8do_panic7runtime"
        c, d = runtime.format(""), runtime.format("s0_")
        alone = "_RNvCs6NFbV2PXKjr_15deluge_bsp_rust4main"
        text = "<[u8] as core::cmp::PartialEq>::eq"
        panic = "core::slice::index::slice_index_fail::do_panic::runtime"
        table = {a: text, b: text, c: panic, d: panic, alone: "deluge_bsp_rust::main"}
        self.assertEqual(self.keys(table), [a, b, c, d, "deluge_bsp_rust::main"])

    def test_a_name_only_gnu_cxxfilt_reads_is_keyed_by_both_texts(self):
        name = "_ZGTtNKSt13bad_exception4whatEv"
        text = "transaction clone for std::bad_exception::what() const"

        with mock.patch.object(compare_codegen, "run", gnu_only({name: text})):
            rename = demangler(
                "cxxfilt",
                [f"10 <{name}>:\n"],
                mangled=mangled_renamer([]),
                gnu_cxxfilt="gnu-cxxfilt",
            )
        self.assertEqual(rename(name), f"{name} | {text}")

    def test_a_name_no_demangler_reads_keeps_its_raw_name_under_a_rename(self):
        # A real fixture name; without its demangled text a reduced mangled key could pair two entities.
        old = (
            "_ZZZN6deluge3gui10shell_host12HostEditSink5applyERKNS_2ui6scenes4bind12WriteRequestEENK3$_0clINS2_5"
            "ApplyEEEDaT_ENKUlRS9_E_clESB_"
        )
        new = old.replace("3gui10shell_host", "2ui10shell_host")
        pair = [("deluge::gui::shell_host", "deluge::ui::shell_host")]
        with mock.patch.object(compare_codegen, "run", fake_cxxfilt({})):
            rename = demangler(
                "cxxfilt",
                [f"10 <{old}>:\n", f"10 <{new}>:\n"],
                namespace_renamer(pair),
                mangled_renamer(pair),
                [[f"10 <{old}>:\n"], [f"10 <{new}>:\n"]],
                gnu_cxxfilt="gnu-cxxfilt",
            )
        self.assertEqual((rename(old), rename(new)), (old, new))

    def test_collapsing_is_judged_per_image(self):
        # The same pair of spellings in two images, one name each, is a rename, not a collision.
        a = "_ZZZN6deluge3gfx1fEENK3$_0clIS3_EEDaT_"
        b = "_ZZZN6deluge2ui3gfx1fEENK3$_0clIS4_EEDaT_"
        texts = {
            a: "auto deluge::gfx::f()::$_0::operator()<int>(int) const",
            b: "auto deluge::ui::gfx::f()::$_0::operator()<int>(int) const",
        }
        with mock.patch.object(compare_codegen, "run", gnu_only(texts)):
            rename = demangler(
                "cxxfilt",
                [f"10 <{a}>:\n", f"10 <{b}>:\n"],
                namespace_renamer(self.PAIR),
                mangled_renamer(self.PAIR),
                [[f"10 <{a}>:\n"], [f"10 <{b}>:\n"]],
                "gnu-cxxfilt",
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
