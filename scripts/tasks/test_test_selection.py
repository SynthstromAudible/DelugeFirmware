"""Unit tests for test_selection.py: globbing, the changed files, selection and the registry's area checks."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import test_selection as ts


class GlobTest(unittest.TestCase):
    def test_a_double_star_crosses_directories_and_a_single_star_does_not(self):
        self.assertTrue(ts.matches("src/ui/**", "src/ui/scenes/menu/node.h"))
        self.assertTrue(
            ts.matches("src/**/codec*", "src/instruments/classic/codec.cpp")
        )
        self.assertTrue(ts.matches("src/**/codec*", "src/codec.h"))
        self.assertFalse(
            ts.matches("src/*/codec*", "src/instruments/classic/codec.cpp")
        )
        self.assertTrue(ts.matches("*.md", "README.md"))
        self.assertFalse(ts.matches("*.md", "docs/a.md"))
        self.assertTrue(ts.matches("**/*.md", "docs/a.md"))
        self.assertTrue(
            ts.matches(
                "tests/golden/ui/menu_oracle/tables_?.tsv",
                "tests/golden/ui/menu_oracle/tables_2.tsv",
            )
        )


class AreasTest(unittest.TestCase):
    def areas(self):
        return ts.Areas(
            areas={"ui": ["src/ui/**"], "codec": ["src/codec/**"]},
            no_gate=["docs/**"],
            every_gate=["CMakeLists.txt"],
        )

    def entry(self, name, areas):
        return ts.Entry(name=name, areas=areas, own=[])

    def test_an_unknown_or_unused_area_and_a_dead_glob_are_problems(self):
        problems = ts.consistency_problems(
            [self.entry("a", ["ui"]), self.entry("b", ["nope"])],
            self.areas(),
            ["src/ui/x.cpp", "docs/a.md", "CMakeLists.txt"],
        )
        self.assertIn("b: unknown area 'nope'", problems)
        self.assertIn("area 'codec' is used by no entry", problems)
        self.assertIn(
            "area 'codec': glob 'src/codec/**' matches no tracked file", problems
        )

    def test_all_needs_no_table(self):
        self.assertEqual(
            ts.consistency_problems(
                [
                    self.entry("a", ["all"]),
                    self.entry("b", ["ui"]),
                    self.entry("c", ["codec"]),
                ],
                self.areas(),
                ["src/ui/x.cpp", "src/codec/y.cpp", "docs/a.md", "CMakeLists.txt"],
            ),
            [],
        )


if __name__ == "__main__":
    unittest.main()
