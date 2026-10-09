"""Function name to address range lookup, over `nm -C -S` output from the bench image."""

import unittest
from pathlib import Path

import symbols

NM = (Path(__file__).parent / "testdata" / "nm_sample.txt").read_text()


class ParseNm(unittest.TestCase):
    def test_reads_sized_text_symbols(self):
        syms = symbols.parse_nm(NM)
        self.assertEqual(len(syms), 8)
        block = syms[0]
        self.assertEqual(
            block.name, "deluge::bench::CalibrationLoop::block(unsigned int)"
        )
        self.assertEqual((block.start, block.end), (0x200F3598, 0x200F35B8))

    def test_skips_data_unsized_and_malformed_lines(self):
        text = (
            "602ef000 00000030 B _SEGGER_RTT\n"
            "200f0000 00000000 t empty()\n"
            "nonsense\n"
            "200f1001 00000010 T thumb_entry()\n"
        )
        syms = symbols.parse_nm(text)
        self.assertEqual([s.name for s in syms], ["thumb_entry()"])

    def test_clears_the_thumb_bit(self):
        (sym,) = symbols.parse_nm("200f1001 00000010 T thumb_entry()\n")
        self.assertEqual((sym.start, sym.end), (0x200F1000, 0x200F1010))

    def test_bare_name_keeps_template_arguments(self):
        sym = symbols.Symbol("ns::f<int (*)(int)>::g(char)", 0, 4)
        self.assertEqual(sym.bare, "ns::f<int (*)(int)>::g")


class Lookup(unittest.TestCase):
    def setUp(self):
        self.syms = symbols.parse_nm(NM)

    def test_finds_by_bare_name(self):
        sym = symbols.lookup(self.syms, "deluge::bench::MonoVecGainCase::block")
        self.assertEqual((sym.start, sym.end), (0x200F4D6C, 0x200F4E44))

    def test_finds_by_full_signature(self):
        sym = symbols.lookup(
            self.syms, "deluge::bench::MonoVecGainCase::setup(unsigned int)"
        )
        self.assertEqual(sym.start, 0x200F4D22)

    def test_missing_name_says_so(self):
        with self.assertRaisesRegex(LookupError, "no function"):
            symbols.lookup(self.syms, "deluge::bench::Nope::block")

    def test_ambiguous_name_lists_candidates(self):
        twice = symbols.parse_nm(
            "200f0000 00000010 t ns::f(int)\n200f0010 00000010 t ns::f(char)\n"
        )
        with self.assertRaisesRegex(
            LookupError, "ambiguous.*ns::f\\(int\\).*ns::f\\(char\\)"
        ):
            symbols.lookup(twice, "ns::f")
        self.assertEqual(symbols.lookup(twice, "ns::f(char)").start, 0x200F0010)

    def test_the_same_symbol_listed_twice_is_one(self):
        dup = symbols.parse_nm(
            "200f0000 00000010 t ns::f(int)\n200f0000 00000010 T ns::f(int)\n"
        )
        self.assertEqual(symbols.lookup(dup, "ns::f").start, 0x200F0000)

    def test_at_finds_the_containing_function(self):
        sym = symbols.at(self.syms, 0x200F35AC)
        self.assertEqual(sym.bare, "deluge::bench::CalibrationLoop::block")
        self.assertIsNone(symbols.at(self.syms, 0x10))


if __name__ == "__main__":
    unittest.main()
