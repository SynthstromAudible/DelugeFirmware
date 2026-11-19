# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_doc_hygiene.py: the history notes, line numbers and dangling paths it flags.

Gate doc-hygiene-selftest:
    the UI docs lint flags task, review and workstream breadcrumbs and rename narration in comments and Markdown,
    any line number in a citation, and a backticked path that names no one file (or a symbol beside it that the
    file does not hold), against a stand-in origin/main; reads Python docstrings and CMake, TOML and .inc comments;
    passes symbol citations, plain prose, string literals and a `#` inside a Python string; refuses a pinned main
    commit the clone lacks
"""

from __future__ import annotations

import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_doc_hygiene as hygiene
from check_doc_hygiene import findings_in, is_exempt

# The stand-in `origin/main` the dangling-path tests read: one file holding one definition.
MAIN_VIEW = "src/deluge/gui/views/view.cpp"
MAIN_VIEW_TEXT = "void View::setModLedStates() {\n}\n"
MAIN_VIEW_HEADER = "src/deluge/gui/views/view.h"


def kinds(text: str, path: str = "src/scenes/x.cpp") -> list[str]:
    return [f.kind for f in findings_in(path, text)]


class HistoryTest(unittest.TestCase):
    def test_flags_task_and_review_breadcrumbs(self):
        for note in (
            "// Added by Task 7 for the press arm.",
            "// fix round 1, Important 2.",
            "// Fix round 1: the release arm",
            "// FIX ROUND 2 found it",
            "// Minor 3. The cursor clamps here.",
            "// **Important 4.** The listing interprets note names.",
            "// Reported in F1 task 11's report.",
            "// the final review asked for this",
            "// E2 task 12 found the gap.",
            "// carried from WS4b into here",
            "// spec E §4.1's MustSelectRange",
            "// the seam for spec D §7",
            "// spec E3's carry",
            "// spec E-note-family §5.2",
            "// a permanent blit (shell spec §15.1, §15.3)",
            "// design spec §15.7",
            "// (task-3 ruling 2)",
            "/// Spec D's service table says",
            "// Spec E §4.1's one rule",
            "// the rule (spec §5.3)",
            "// migration §10.2 item 8",
            "// the brief mandates this",
            "// The brief's four facts",
            "// not this plan's job",
            "// A later plan wires it",
            "// the wiring plan's seam",
            "// what this task adds",
            "// as on this branch",
            "// Review round 1, I4",
            "// (Kate, 2026-10-25: match main)",
            "// measured 2026-10-25",
            "// batch 3 of the sweep",
            "// the batch-2 rewrite",
            "// the first cut of the walk",
        ):
            with self.subTest(note=note):
                self.assertEqual(kinds(note), ["history"])

    def test_flags_rename_and_former_narration(self):
        self.assertEqual(kinds("// The value used to be read here."), ["history"])
        self.assertEqual(kinds("// this used to be read here"), ["history"])
        self.assertEqual(kinds("// maybe used to be read here"), ["history"])
        self.assertEqual(
            kinds("// was renamed from gate_last_real_output"), ["history"]
        )

    def test_passes_plain_rationale(self):
        self.assertEqual(
            kinds("// The release of a fed press is fed too, whatever the claim."), []
        )
        self.assertEqual(kinds("// A task the scheduler runs."), [])
        self.assertEqual(kinds("// the lock is used to guard the list"), [])
        self.assertEqual(
            kinds("// the locks are used to guard it, as can be used to"), []
        )
        self.assertEqual(kinds("// the spec examples pin it"), [])
        self.assertEqual(kinds("// the branch this if takes"), [])
        self.assertEqual(kinds("// a plan of the song's arrangement"), [])
        self.assertEqual(kinds("// the briefest pause"), [])
        self.assertEqual(kinds("// a minor third above the root"), [])
        self.assertEqual(kinds("// Important: the clock runs first."), [])
        self.assertEqual(kinds("// a batch of events drains per tick"), [])
        self.assertEqual(kinds("// the first cutoff the filter reaches"), [])
        self.assertEqual(
            kinds(
                "// TODO(conflation-batch-4): an unknown answer is treated as absent"
            ),
            [],
        )

    def test_reads_comments_only(self):
        self.assertEqual(kinds('const char* s = "Task 7";\n'), [])


class ScriptCommentTest(unittest.TestCase):
    def test_reads_hash_comments_in_ui_scripts(self):
        self.assertEqual(
            kinds("# added by F3 task 4\n", "tests/ui-scripts/x.txt"), ["history"]
        )
        self.assertEqual(
            kinds("2500 snapshot s   # fix round 1\n", "tests/ui-scripts/x.txt"),
            ["history"],
        )

    def test_reads_only_the_comment_of_a_script_line(self):
        self.assertEqual(kinds("press task 7\n", "tests/ui-scripts/x.txt"), [])

    def test_a_data_file_is_not_read(self):
        self.assertEqual(kinds("# added by Task 7\n", "scripts/allowlist.txt"), [])
        self.assertEqual(
            kinds("# added by Task 7\n", "tests/harness/CMakeLists.txt"), ["history"]
        )

    def test_reads_c_comments_in_harness_sources(self):
        self.assertEqual(kinds("/* spec G §3 */\n", "tests/harness/x.c"), ["history"])

    def test_reads_python_comments(self):
        self.assertEqual(
            kinds("x = 1  # added by Task 7\n", "scripts/x.py"), ["history"]
        )

    def test_a_hash_inside_a_python_string_is_not_a_comment(self):
        self.assertEqual(kinds('NOTE = "# added by Task 7"\n', "scripts/x.py"), [])
        self.assertEqual(
            kinds('x = """Text.\n\n# Task 7 in a string\n"""\n', "scripts/x.py"), []
        )

    def test_reads_python_docstrings(self):
        self.assertEqual(kinds('"""Added by Task 7."""\n', "scripts/x.py"), ["history"])
        self.assertEqual(
            kinds(
                'def f():\n    """The walk.\n\n    `walk.cpp:12`\n    """\n',
                "scripts/x.py",
            ),
            ["line-number"],
        )

    def test_reads_cmake_toml_and_inc_comments(self):
        for text, path in (
            ("# added by Task 7\n", "scripts/cmake/x.cmake"),
            ("# added by Task 7\n", "tools/x/Cargo.toml"),
            ("// added by Task 7\n", "src/scenes/x.inc"),
        ):
            with self.subTest(path=path):
                self.assertEqual(kinds(text, path), ["history"])

    def test_the_parity_ledger_and_the_checker_are_exempt(self):
        self.assertTrue(is_exempt("tests/ui-scripts/parity/DIFFERENCES.md"))
        self.assertTrue(is_exempt("scripts/check_doc_hygiene.py"))
        self.assertFalse(is_exempt("tests/ui-scripts/parity/README.md"))


class LineNumberTest(unittest.TestCase):
    def test_flags_a_file_line_citation(self):
        for note in (
            "// see `menu_scene.cpp:123`",
            "// (`shell/scene.h:68`)",
            "// `sound_editor.cpp:1078`",
            "// `gui/menu_item/submenu.cpp:21-37`",
            "// main's `View::setModLedStates` (`origin/main:src/deluge/gui/views/view.cpp:1548`)",
            "// the walk in check_layer_rules.py:40",
            "// the section (`sdram_sections.ld:40`)",
            "// `deluge.card:3` sets it",
        ):
            with self.subTest(note=note):
                self.assertIn("line-number", kinds(note))

    def test_flags_a_bare_line_citation(self):
        for note in (
            "// the popup arm (`:1517`)",
            "// the popup arm `:1517-1530`",
            "// and ::renderColumn (:1268).",
            "// where the x coordinate belongs (:1158, :1167)",
        ):
            with self.subTest(note=note):
                self.assertIn("line-number", kinds(note))

    def test_flags_each_line_number_on_a_line(self):
        self.assertEqual(kinds("// (`:1158`, `:1167`)"), ["line-number", "line-number"])

    def test_flags_a_line_number_in_a_script_comment(self):
        self.assertEqual(
            kinds(
                "1040 snapshot s   # `selectEncoderAction` (:268)\n",
                "tests/ui-scripts/x.txt",
            ),
            ["line-number"],
        )

    def test_passes_a_ratio_a_time_and_a_label(self):
        self.assertEqual(kinds("// a 3:2 ratio, the 10:30 mark, `Kind`: a label"), [])


class DanglingPathTest(unittest.TestCase):
    def setUp(self):
        head_text = hygiene._text
        for patch in (
            mock.patch.object(
                hygiene, "_main_files", lambda: frozenset({MAIN_VIEW, MAIN_VIEW_HEADER})
            ),
            mock.patch.object(
                hygiene,
                "_text",
                lambda path, on_main: (
                    MAIN_VIEW_TEXT if on_main else head_text(path, False)
                ),
            ),
        ):
            patch.start()
            self.addCleanup(patch.stop)

    def test_passes_a_path_that_names_one_file(self):
        self.assertEqual(kinds("// `check_doc_hygiene.py` reads comments"), [])
        self.assertEqual(kinds("// `scripts/check_doc_hygiene.py`"), [])

    def test_passes_a_symbol_the_file_holds(self):
        for note in (
            "// `findings_in` (`check_doc_hygiene.py`)",
            "// (`findings_in()` in `check_doc_hygiene.py`)",
            "// `check_doc_hygiene.py` `findings_in`",
            "// main's `View::setModLedStates` (`origin/main:src/deluge/gui/views/view.cpp`)",
        ):
            with self.subTest(note=note):
                self.assertEqual(kinds(note), [])

    def test_flags_a_path_that_names_no_file(self):
        self.assertEqual(
            kinds("// `menu/no_such_file.cpp` holds it"), ["dangling-path"]
        )

    def test_flags_an_ambiguous_path(self):
        self.assertEqual(kinds("// see `CMakeLists.txt`"), ["dangling-path"])

    def test_flags_a_main_path_that_main_does_not_hold(self):
        self.assertEqual(
            kinds("// `origin/main:src/scenes/menu/menu_scene.cpp`"), ["dangling-path"]
        )
        self.assertEqual(
            kinds("// `origin/main:gui/views/view.cpp`, a path main has only in full"),
            ["dangling-path"],
        )

    def test_flags_a_symbol_the_file_does_not_hold(self):
        for note in (
            "// `no_such_symbol` (`check_doc_hygiene.py`)",
            "// (`no_such_symbol` in `check_doc_hygiene.py`)",
            "// main's `View::noSuchMethod` (`origin/main:src/deluge/gui/views/view.cpp`)",
        ):
            with self.subTest(note=note):
                self.assertEqual(kinds(note), ["dangling-path"])

    def test_passes_a_symbol_with_prose_between(self):
        self.assertEqual(
            kinds("// `no_such_symbol` is read by `check_doc_hygiene.py`"), []
        )

    def test_passes_a_brace_path_whose_every_file_exists(self):
        for note in (
            "// `scripts/{check_doc_hygiene,check_citations}.py`",
            "// `findings_in` (`scripts/{check_doc_hygiene,check_citations}.py`)",
            "// `origin/main:src/deluge/gui/views/view.{h,cpp}`",
            "// main's `View::setModLedStates` (`origin/main:src/deluge/gui/views/view.{h,cpp}`)",
        ):
            with self.subTest(note=note):
                self.assertEqual(kinds(note), [])

    def test_flags_each_missing_file_of_a_brace_path(self):
        for note, n in (
            ("// `scripts/check_doc_hygiene.{py,h}`", 1),
            ("// `scripts/{check_doc_hygiene,no_such_file}.py`", 1),
            ("// `scripts/{no_such_file,no_such_other}.py`", 2),
            ("// `origin/main:src/deluge/gui/views/view.{h,inc}`", 1),
            (
                "// `no_such_symbol` (`scripts/{check_doc_hygiene,check_citations}.py`)",
                1,
            ),
        ):
            with self.subTest(note=note):
                self.assertEqual(kinds(note), ["dangling-path"] * n)

    def test_passes_a_short_name_and_a_pair_of_aliases(self):
        for text in (
            "// `abc` (`check_doc_hygiene.py`)",
            "// `findings_in` / `no_such_symbol` (`check_doc_hygiene.py`)",
            "// `findings_in` /\n// `no_such_symbol` (`check_doc_hygiene.py`)",
        ):
            with self.subTest(text=text):
                self.assertEqual(kinds(text), [])


class MainRefTest(unittest.TestCase):
    def setUp(self):
        hygiene._main_ref.cache_clear()
        self.addCleanup(hygiene._main_ref.cache_clear)

    def pin(self, text: str) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        ref = Path(folder.name) / "main_citation_ref"
        ref.write_text(text)
        patch = mock.patch.object(hygiene, "MAIN_REF_FILE", ref)
        patch.start()
        self.addCleanup(patch.stop)

    def test_reads_the_pinned_commit(self):
        head = hygiene._git("rev-parse", "HEAD").strip()
        self.pin(head + "\n")
        self.assertEqual(hygiene._main_ref(), head)

    def test_a_commit_the_clone_lacks_exits_2(self):
        self.pin("0" * 40 + "\n")
        with (
            self.assertRaises(SystemExit) as stop,
            contextlib.redirect_stderr(io.StringIO()),
        ):
            hygiene._main_ref()
        self.assertEqual(stop.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
