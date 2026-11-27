"""Tests of the placement generator: layers.toml's [[placement]] table to the linker fragment's marked blocks."""

import tempfile
import unittest
from pathlib import Path

import placement

TOML = """
[[placement]]
path = "src/ui"
memory = "sdram"
reason = "the UI tier, at menu and input rate"
min_text = 153600
min_data = 65536
min_bss = 8192
"""

FRAGMENT = """head
        /* BEGIN placement text */
        stale
        /* END placement text */
mid
        /* BEGIN placement data */
        /* END placement data */
        /* BEGIN placement bss */
        /* END placement bss */
    /* BEGIN placement asserts */
    /* END placement asserts */
tail
"""


def load(text):
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "layers.toml"
        p.write_text(text)
        return placement.load(p)


class NameTests(unittest.TestCase):
    def test_path_is_namespace(self):
        self.assertEqual(
            placement.namespace("src/ui/views/waveform"), "deluge::ui::views::waveform"
        )

    def test_mangled_namespace_is_length_prefixed(self):
        self.assertEqual(placement.mangled("deluge::ui"), "6deluge2ui")
        self.assertEqual(placement.mangled("deluge::ui::scenes"), "6deluge2ui6scenes")


class LoadTests(unittest.TestCase):
    def test_reads_an_entry(self):
        (p,) = load(TOML)
        self.assertEqual(
            (p.path, p.memory, p.min_text, p.min_data, p.min_bss),
            ("src/ui", "sdram", 153600, 65536, 8192),
        )

    def test_refuses_a_legacy_path(self):
        with self.assertRaises(placement.PlacementError):
            load(
                '[[placement]]\npath = "src/deluge/gui"\nmemory = "sdram"\nreason = "x"\n'
            )

    def test_refuses_an_unknown_memory(self):
        with self.assertRaises(placement.PlacementError):
            load('[[placement]]\npath = "src/ui"\nmemory = "flash"\nreason = "x"\n')

    def test_refuses_an_entry_without_a_reason(self):
        with self.assertRaises(placement.PlacementError):
            load('[[placement]]\npath = "src/ui"\nmemory = "sdram"\n')


class BlockTests(unittest.TestCase):
    def setUp(self):
        self.blocks = placement.blocks(load(TOML))

    def test_text_is_bounded_by_its_own_symbols(self):
        self.assertIn("__placement_ui_text_start = .;", self.blocks["text"])
        self.assertIn("*(.text.*6deluge2ui*)", self.blocks["text"])
        self.assertIn("__placement_ui_text_end = .;", self.blocks["text"])

    def test_data_and_bss_follow_the_namespace_within_their_own_symbols(self):
        self.assertIn("__placement_ui_data_start = .;", self.blocks["data"])
        self.assertIn("*(.rodata.*6deluge2ui* .data.*6deluge2ui*)", self.blocks["data"])
        self.assertIn("__placement_ui_bss_end = .;", self.blocks["bss"])
        self.assertIn("*(.bss.*6deluge2ui*)", self.blocks["bss"])

    def test_each_minimum_becomes_an_assert(self):
        for kind, size in (("text", 153600), ("data", 65536), ("bss", 8192)):
            self.assertIn(
                f"__placement_ui_{kind}_end - __placement_ui_{kind}_start > {size}",
                self.blocks["asserts"],
            )

    def test_no_minimum_no_assert(self):
        (p,) = load('[[placement]]\npath = "src/ui"\nmemory = "sdram"\nreason = "x"\n')
        self.assertEqual(placement.blocks([p])["asserts"], "")


class RenderTests(unittest.TestCase):
    def test_replaces_only_between_markers_and_keeps_indentation(self):
        out = placement.render(FRAGMENT, placement.blocks(load(TOML)))
        self.assertNotIn("stale", out)
        self.assertTrue(out.startswith("head\n") and out.endswith("tail\n"))
        self.assertIn("        *(.text.*6deluge2ui*)", out)

    def test_a_missing_marker_is_an_error(self):
        with self.assertRaises(placement.PlacementError):
            placement.render("no markers\n", placement.blocks(load(TOML)))


class FreshnessTests(unittest.TestCase):
    def test_the_tree_is_current(self):
        root = Path(__file__).resolve().parents[2]
        fragment = root / "src/platform/device/linker/sdram_sections.x"
        current = fragment.read_text()
        self.assertEqual(
            placement.render(
                current, placement.blocks(placement.load(root / "scripts/layers.toml"))
            ),
            current,
            "sdram_sections.x's placement blocks are stale: run python3 scripts/tasks/placement.py --write",
        )


if __name__ == "__main__":
    unittest.main()
