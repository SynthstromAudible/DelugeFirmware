"""Tests of the post-link placement gate over synthetic disassembly and symbol listings.

Gate placement-selftest:
    the placement gate reads llvm-objdump's functions with their direct branches and constants in order, follows
    lld's long-branch thunks to their targets, walks direct calls from its roots, cuts a boot function at a call or an
    instruction, reports SDRAM code reached and late-statics loads, reads a vtable's entries with or without the Thumb
    bit, strips Rust disambiguators before matching, fails a root that matches nothing, covers a known finding only for
    its rule and reports a known entry that covers nothing as stale
"""

import unittest

import check_placement as cp

OBJDUMP = """
deluge.elf:\tfile format elf32-littlearm

Disassembly of section .text:

20090000 <boot_root>:
20090000:      \tpush\t{r7, lr}
20090002:      \tbl\t0x20090010 <helper> @ imm = #0xc
20090006:      \tldr\tr0, [pc, #0x4]
20090008:      \tpop\t{r7, pc}
2009000a:      \tb\t0x20090008 <boot_root+0x8> @ imm = #-0x6
2009000c:      \t.word\t0x20100040

20090010 <helper>:
20090010:      \tmovw\tr1, #0x80
20090014:      \tmovt\tr1, #0x2010
20090018:      \tb.w\t0x20090030 <__Thumbv7ABSLongThunk_ui_fn> @ imm = #0x14

20090030 <__Thumbv7ABSLongThunk_ui_fn>:
20090030:      \tmovw\tr12, #0x100
20090034:      \tmovt\tr12, #0xc20
20090038:      \tbx\tr12

20090040 <hot_root>:
20090040:      \tbeq.w\t0x20090010 <helper> @ imm = #-0x34
20090044:      \tbx\tlr

Disassembly of section .sdram_init:

0c200100 <ui_fn>:
0c200100:      \tbx\tlr
"""


class ParseTests(unittest.TestCase):
    def setUp(self):
        self.fns = cp.parse_functions(OBJDUMP)

    def test_reads_every_function_with_its_address(self):
        self.assertEqual(self.fns["boot_root"].address, 0x20090000)
        self.assertEqual(self.fns["ui_fn"].address, 0x0C200100)

    def test_calls_are_direct_branches_to_other_functions_only(self):
        self.assertEqual(self.fns["boot_root"].calls, {"helper"})
        self.assertEqual(self.fns["hot_root"].calls, {"helper"})
        self.assertEqual(self.fns["helper"].calls, {"__Thumbv7ABSLongThunk_ui_fn"})

    def test_collects_literal_words_and_movw_movt_pairs(self):
        self.assertIn(0x20100040, self.fns["boot_root"].constants)
        self.assertIn(0x20100080, self.fns["helper"].constants)


class ReachTests(unittest.TestCase):
    def test_a_thunk_leads_to_its_target(self):
        fns = cp.parse_functions(OBJDUMP)
        graph = cp.call_graph(fns)
        self.assertEqual(graph["__Thumbv7ABSLongThunk_ui_fn"], {"ui_fn"})
        parents = cp.reach(graph, ["boot_root"])
        self.assertEqual(
            cp.path(parents, "ui_fn"),
            ["boot_root", "helper", "__Thumbv7ABSLongThunk_ui_fn", "ui_fn"],
        )

    def test_a_violation_names_the_sdram_function_reached(self):
        fns = cp.parse_functions(OBJDUMP)
        found = cp.sdram_reached(
            fns, cp.call_graph(fns), ["hot_root"], 0x0C000000, 0x10000000
        )
        self.assertEqual([v.function for v in found], ["ui_fn"])


class PrefixTests(unittest.TestCase):
    def test_only_calls_and_constants_before_the_until_call_count(self):
        text = """
20090100 <start>:
20090100:      \tbl\t0x20090010 <helper> @ imm = #0x0
20090104:      \tbl\t0x20090200 <init_sdram_memory> @ imm = #0x0
20090108:      \tbl\t0x20090040 <hot_root> @ imm = #0x0
2009010c:      \t.word\t0x20100040
"""
        fn = cp.parse_functions(text)["start"]
        names = {
            "helper": "helper",
            "init_sdram_memory": "init_sdram_memory",
            "hot_root": "hot_root",
        }
        calls, constants = cp.prefix(fn, names, until="init_sdram_memory")
        self.assertEqual(calls, {"helper"})
        self.assertEqual(constants, set())

    def test_an_instruction_can_mark_the_cut(self):
        text = """
20090100 <start>:
20090100:      \tbl\t0x20090010 <helper> @ imm = #0x0
20090104:      \tmcr\tp15, #0x0, r0, c7, c5, #0x6
20090108:      \tbl\t0x20090040 <hot_root> @ imm = #0x0
"""
        fn = cp.parse_functions(text)["start"]
        calls, _ = cp.prefix(fn, {}, until_insn=r"mcr\s+p15, #0x0, r\d+, c7, c5, #0x6")
        self.assertEqual(calls, {"helper"})

    def test_an_until_that_matches_no_call_is_an_error(self):
        fn = cp.parse_functions(OBJDUMP)["boot_root"]
        with self.assertRaises(cp.ConfigError):
            cp.prefix(fn, {"helper": "helper"}, until="^nothing$")

    def test_a_prefix_needs_exactly_one_cut(self):
        fn = cp.parse_functions(OBJDUMP)["boot_root"]
        with self.assertRaises(cp.ConfigError):
            cp.prefix(fn, {})


class LateLoadTests(unittest.TestCase):
    def test_reports_constants_inside_the_late_range(self):
        fns = cp.parse_functions(OBJDUMP)
        self.assertEqual(
            cp.late_loads(fns["boot_root"], 0x20100000, 0x20100050), [0x20100040]
        )
        self.assertEqual(cp.late_loads(fns["helper"], 0x20100000, 0x20100050), [])


class VtableTests(unittest.TestCase):
    def test_reports_entries_in_sdram_text_with_or_without_the_thumb_bit(self):
        words = {"Foo": [0x20090001, 0x0C200101], "Bar": [0x20090041]}
        self.assertEqual(
            cp.vtable_hits(words, 0x0C200000, 0x0C300000), {"Foo": [0x0C200101]}
        )


class RootTests(unittest.TestCase):
    def test_a_root_pattern_matching_nothing_is_an_error(self):
        with self.assertRaises(cp.ConfigError):
            cp.match_roots(["^nothing_here$"], {"boot_root": "boot_root"})

    def test_rust_disambiguators_are_stripped_before_matching(self):
        names = {"_RNvMs_x": "deluge_bsp[1a2b3c4d]::Board::new"}
        self.assertEqual(
            cp.match_roots([r"^deluge_bsp::Board::new$"], names), ["_RNvMs_x"]
        )


class HotReportTests(unittest.TestCase):
    def test_groups_namespaced_code_by_library_and_the_rest_by_first_component(self):
        sizes = {"a": 100, "b": 40, "c": 7}
        names = {
            "a": "deluge::dsp::f(int)",
            "b": "deluge::dsp::g()",
            "c": "Sound::render()",
        }
        self.assertEqual(
            cp.hot_report(["a", "b", "c"], sizes, names),
            [("deluge::dsp", 140), ("Sound", 7)],
        )


class KnownTests(unittest.TestCase):
    def test_a_known_finding_is_covered_and_an_unused_entry_is_stale(self):
        findings = [
            cp.Violation("hot", "_Z1av", ["r", "_Z1av"]),
            cp.Violation("hot", "_Z1bv", ["r", "_Z1bv"]),
        ]
        known = [
            {"rule": "hot", "function": r"^a\(\)$", "reason": "x"},
            {"rule": "hot", "function": r"^gone\(\)$", "reason": "y"},
        ]
        remaining, covered, stale = cp.apply_known(
            findings, known, {"_Z1av": "a()", "_Z1bv": "b()"}
        )
        self.assertEqual([v.function for v in remaining], ["_Z1bv"])
        self.assertEqual([v.function for v in covered], ["_Z1av"])
        self.assertEqual([k["function"] for k in stale], [r"^gone\(\)$"])

    def test_a_known_entry_covers_only_its_rule(self):
        findings = [cp.Violation("vtable", "a()", ["x"])]
        remaining, _, stale = cp.apply_known(
            findings, [{"rule": "hot", "function": "a", "reason": "x"}], {}
        )
        self.assertEqual(len(remaining), 1)
        self.assertEqual(len(stale), 1)


if __name__ == "__main__":
    unittest.main()
