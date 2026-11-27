"""Unit tests for test_selection.py: globbing, the changed files, selection and the registry's area checks."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import ClassVar

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


def git(root, *args):
    subprocess.run(["git", *args], cwd=root, check=True, capture_output=True)


class ChangedFilesTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        git(self.root, "init", "-q", "-b", "main")
        git(self.root, "config", "user.email", "t@example.com")
        git(self.root, "config", "user.name", "t")
        for name in ("a.txt", "b.txt", "old.txt"):
            (self.root / name).write_text(name)
        git(self.root, "add", ".")
        git(self.root, "commit", "-q", "-m", "base")
        git(self.root, "branch", "base")

    def test_committed_staged_unstaged_untracked_and_both_sides_of_a_rename(self):
        (self.root / "a.txt").write_text("changed")
        git(self.root, "commit", "-q", "-am", "a")
        git(self.root, "mv", "old.txt", "new.txt")
        (self.root / "b.txt").write_text("unstaged")
        (self.root / "fresh.txt").write_text("untracked")
        self.assertEqual(
            ts.changed_files(self.root, "base"),
            ["a.txt", "b.txt", "fresh.txt", "new.txt", "old.txt"],
        )

    def test_the_merge_base_not_the_ref_itself(self):
        git(self.root, "checkout", "-q", "base")
        (self.root / "b.txt").write_text("base moved on")
        git(self.root, "commit", "-q", "-am", "elsewhere")
        git(self.root, "checkout", "-q", "main")
        self.assertEqual(ts.changed_files(self.root, "base"), [])

    def test_an_unknown_ref_is_an_error(self):
        with self.assertRaises(ValueError):
            ts.changed_files(self.root, "no-such-ref")


class Gate:
    """A stand-in Harness with only what selection reads."""

    def __init__(
        self,
        name,
        areas,
        tier="pr",
        path=".",
        run=(),
        build=(),
        check="",
        scenario=None,
        corpus_runner="",
        baseline=(),
    ):
        self.name, self.areas, self.tier, self.path = name, list(areas), tier, path
        self.run, self.build, self.check, self.scenario = (
            list(run),
            list(build),
            check,
            scenario,
        )
        self.corpus_runner, self.baseline = corpus_runner, list(baseline)


class SinceTest(unittest.TestCase):
    AREAS: ClassVar[ts.Areas] = ts.Areas(
        areas={"ui": ["src/ui/**"], "codec": ["src/codec/**"]},
        no_gate=["docs/**"],
        every_gate=["CMakeLists.txt"],
    )
    GATES: ClassVar[list[Gate]] = [
        Gate("menu", ["ui"]),
        Gate("codec", ["codec"], baseline=["tests/golden/codec"]),
        Gate("app", ["all"]),
        Gate("sweep", ["codec"], run=["python3", "tests/sweep/run.py"]),
    ]

    def names(self, changed):
        return [h.name for h in ts.since(self.GATES, self.AREAS, changed).selected]

    def test_an_area_selects_its_gates_and_all_gates(self):
        self.assertEqual(self.names(["src/ui/x.cpp"]), ["menu", "app"])

    def test_a_gates_own_file_selects_it(self):
        self.assertEqual(self.names(["tests/golden/codec/a.json"]), ["codec", "app"])
        self.assertEqual(self.names(["tests/sweep/run.py"]), ["app", "sweep"])

    def test_no_gate_files_select_nothing(self):
        result = ts.since(self.GATES, self.AREAS, ["docs/a.md"])
        self.assertEqual(result.selected, [])

    def test_an_every_gate_file_and_an_unmapped_file_select_everything_and_say_why(
        self,
    ):
        for changed, message in (
            (["CMakeLists.txt"], "--since: CMakeLists.txt reaches every gate"),
            (
                ["src/elsewhere/z.cpp"],
                "--since: src/elsewhere/z.cpp is in no area: running the whole selection",
            ),
        ):
            result = ts.since(self.GATES, self.AREAS, changed)
            self.assertEqual(
                [h.name for h in result.selected], ["menu", "codec", "app", "sweep"]
            )
            self.assertEqual(result.messages, [message])

    def test_each_gate_says_what_reached_it(self):
        result = ts.since(self.GATES, self.AREAS, ["src/codec/c.cpp"])
        self.assertEqual(
            result.reasons,
            {
                "codec": "area codec: src/codec/c.cpp",
                "app": "all: src/codec/c.cpp",
                "sweep": "area codec: src/codec/c.cpp",
            },
        )


if __name__ == "__main__":
    unittest.main()
