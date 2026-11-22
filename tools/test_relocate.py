# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for relocate.py: what a move must rewrite, and what it must leave alone.

Gate relocate-selftest:
    a library move respells exactly the includes the compiler resolves into the moved directory, rewrites its path
    tokens, namespaces and target names in every tracked text file, moves its unit tests with it, and refuses a move
    the legacy include root would make ambiguous
"""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

import relocate


class Repo:
    def __init__(self, files: dict[str, str]):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        for rel, text in files.items():
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)

    def read(self, rel: str) -> str:
        return (self.root / rel).read_text()

    def close(self) -> None:
        self._tmp.cleanup()


FILES = {
    "src/deluge/model/song.cpp": '#include "scenes/menu/menu.h"\n#include "model/song.h"\n',
    "src/deluge/model/song.h": "#pragma once\n",
    "src/scenes/menu/menu.h": "#pragma once\nnamespace deluge::scenes::menu {\nstruct Menu {};\n}\n",
    "src/scenes/menu/menu.cpp": '#include "menu.h"\n#include "scenes/menu/menu.h"\n// see `scenes/menu/menu.h` and src/scenes/menu/menu.h\n',
    "src/scenes/CMakeLists.txt": "add_library(deluge_scenes STATIC menu/menu.cpp)\n",
    "src/shell/shell.cpp": (
        "#include <scenes/menu/menu.h>\n"
        "deluge::scenes::menu::Menu m;\n"
        "::deluge::scenes::menu::Menu n;\n"
        "int deluge_scenes_count;\n"
    ),
    "src/CMakeLists.txt": "add_subdirectory(scenes)\nadd_subdirectory(scenes/bind)\n",
    "sim/CMakeLists.txt": "add_subdirectory(${REPO_ROOT}/src/scenes ${CMAKE_BINARY_DIR}/scenes)\ntarget_link_libraries(x deluge_scenes deluge_scenes_bind)\n",
    "scripts/layers.toml": 'root = "src/scenes/bind"\nprefix = "scenes/bind/"\npattern = \'^(?!scenes/|shell/)\'\n',
    "tests/unit/scenes/menu_spec.cpp": '#include "scenes/menu/menu.h"\n',
    "docs/dev/code_layout_north_star.md": "| `src/scenes` | `ui/scenes/` |\n",
    "docs/dev/other.md": "The scenes live in `src/scenes/`; a reviewer reads `scenes/menu/menu.h`. Not `myscenes/x` nor `foo/scenes/x`.\n",
}


class Moves(unittest.TestCase):
    def setUp(self):
        self.repo = Repo(FILES)
        relocate.apply(
            self.repo.root,
            moves=[("scenes", "ui/scenes")],
            namespaces=[("deluge::scenes", "deluge::ui::scenes")],
            targets=[("deluge_scenes", "deluge_ui_scenes")],
        )

    def tearDown(self):
        self.repo.close()

    def test_the_directory_and_its_unit_tests_move(self):
        self.assertTrue((self.repo.root / "src/ui/scenes/menu/menu.h").is_file())
        self.assertFalse((self.repo.root / "src/scenes").exists())
        self.assertTrue(
            (self.repo.root / "tests/unit/ui/scenes/menu_spec.cpp").is_file()
        )

    def test_an_include_resolving_into_the_move_is_respelled_in_either_bracket(self):
        self.assertIn(
            '#include "ui/scenes/menu/menu.h"',
            self.repo.read("src/deluge/model/song.cpp"),
        )
        self.assertIn(
            "#include <ui/scenes/menu/menu.h>", self.repo.read("src/shell/shell.cpp")
        )
        self.assertIn(
            '#include "ui/scenes/menu/menu.h"',
            self.repo.read("tests/unit/ui/scenes/menu_spec.cpp"),
        )

    def test_an_include_resolving_elsewhere_is_left(self):
        self.assertIn(
            '#include "model/song.h"', self.repo.read("src/deluge/model/song.cpp")
        )

    def test_a_relative_include_inside_the_move_is_left(self):
        self.assertIn(
            '#include "menu.h"', self.repo.read("src/ui/scenes/menu/menu.cpp")
        )

    def test_path_tokens_are_rewritten_in_comments_and_text_files(self):
        self.assertIn(
            "see `ui/scenes/menu/menu.h` and src/ui/scenes/menu/menu.h",
            self.repo.read("src/ui/scenes/menu/menu.cpp"),
        )
        self.assertEqual(
            self.repo.read("src/CMakeLists.txt"),
            "add_subdirectory(scenes)\nadd_subdirectory(ui/scenes/bind)\n",
        )
        self.assertIn(
            "${REPO_ROOT}/src/ui/scenes ${CMAKE_BINARY_DIR}/scenes",
            self.repo.read("sim/CMakeLists.txt"),
        )
        self.assertEqual(
            self.repo.read("scripts/layers.toml"),
            'root = "src/ui/scenes/bind"\nprefix = "ui/scenes/bind/"\npattern = \'^(?!ui/scenes/|shell/)\'\n',
        )
        self.assertEqual(
            self.repo.read("docs/dev/other.md"),
            "The scenes live in `src/ui/scenes/`; a reviewer reads `ui/scenes/menu/menu.h`. Not `myscenes/x` nor `foo/scenes/x`.\n",
        )

    def test_the_north_star_is_never_rewritten(self):
        self.assertEqual(
            self.repo.read("docs/dev/code_layout_north_star.md"),
            "| `src/scenes` | `ui/scenes/` |\n",
        )

    def test_namespaces_are_renamed_where_qualified(self):
        self.assertIn(
            "namespace deluge::ui::scenes::menu {",
            self.repo.read("src/ui/scenes/menu/menu.h"),
        )
        shell = self.repo.read("src/shell/shell.cpp")
        self.assertIn("deluge::ui::scenes::menu::Menu m;", shell)
        self.assertIn("::deluge::ui::scenes::menu::Menu n;", shell)

    def test_targets_are_renamed_as_whole_words(self):
        sim = self.repo.read("sim/CMakeLists.txt")
        self.assertIn("deluge_ui_scenes deluge_scenes_bind", sim)
        self.assertIn(
            "add_library(deluge_ui_scenes STATIC",
            self.repo.read("src/ui/scenes/CMakeLists.txt"),
        )
        self.assertIn("int deluge_scenes_count;", self.repo.read("src/shell/shell.cpp"))


class Spellings(unittest.TestCase):
    def moved(self, files, moves=(("scenes", "ui/scenes"),), targets=()):
        repo = Repo({"src/scenes/a.h": "", **files})
        self.addCleanup(repo.close)
        relocate.apply(
            repo.root, moves=list(moves), namespaces=[], targets=list(targets)
        )
        return repo

    def test_a_string_literal_script_or_shell_word_is_left(self):
        repo = self.moved(
            {
                "src/x.cpp": 'const char* a = "scenes/x"; // scenes/x here\n',
                "scripts/tool.py": 'p = gcc / "scenes/x"\n',
                "scripts/run.sh": "ls scenes/x\n",
            }
        )
        self.assertEqual(
            repo.read("src/x.cpp"), 'const char* a = "scenes/x"; // ui/scenes/x here\n'
        )
        self.assertEqual(repo.read("scripts/tool.py"), 'p = gcc / "scenes/x"\n')
        self.assertEqual(repo.read("scripts/run.sh"), "ls scenes/x\n")

    def test_layer_data_cmake_and_comments_are_rewritten(self):
        repo = self.moved(
            {
                "scripts/layers.toml": 'prefix = "scenes/"\n',
                "scripts/layers_baseline.txt": "scenes/a.h\n",
                "src/CMakeLists.txt": "add_subdirectory(scenes/bind)\n",
                "src/y.h": "/* the scenes/x header\n * and scenes/y */\nint a;\n",
            }
        )
        self.assertEqual(repo.read("scripts/layers.toml"), 'prefix = "ui/scenes/"\n')
        self.assertEqual(repo.read("scripts/layers_baseline.txt"), "ui/scenes/a.h\n")
        self.assertEqual(
            repo.read("src/CMakeLists.txt"), "add_subdirectory(ui/scenes/bind)\n"
        )
        self.assertEqual(
            repo.read("src/y.h"),
            "/* the ui/scenes/x header\n * and ui/scenes/y */\nint a;\n",
        )

    def test_variable_and_relative_prefixes_are_rewritten(self):
        repo = self.moved(
            {
                "a.sh": 'x="$REPO/src/scenes" y=${ROOT}/src/scenes z=../../src/scenes/q w=not/src/scenes\n',
            }
        )
        self.assertEqual(
            repo.read("a.sh"),
            'x="$REPO/src/ui/scenes" y=${ROOT}/src/ui/scenes z=../../src/ui/scenes/q w=not/src/scenes\n',
        )

    def test_a_sentence_final_path_is_rewritten(self):
        repo = self.moved({"d.md": "see src/scenes. And src/scenes.h stays.\n"})
        self.assertEqual(
            repo.read("d.md"), "see src/ui/scenes. And src/scenes.h stays.\n"
        )

    def test_chained_moves_are_refused_and_change_nothing(self):
        repo = Repo({"src/a/x.h": "", "src/b/y.h": "", "d.md": "a/x.h and src/b\n"})
        try:
            code = relocate.main(
                ["--repo", str(repo.root), "--move", "a", "b/a", "--move", "b", "c"]
            )
            self.assertEqual(code, 2)
            self.assertTrue((repo.root / "src/a/x.h").is_file())
            self.assertTrue((repo.root / "src/b/y.h").is_file())
            self.assertEqual(repo.read("d.md"), "a/x.h and src/b\n")
        finally:
            repo.close()

    def test_siblings_with_a_shared_prefix_are_not_chained(self):
        repo = Repo({"src/ui/core/x.h": "", "src/ui/coreX/y.h": ""})
        try:
            relocate.apply(
                repo.root,
                moves=[("ui/core", "ui/w/core"), ("ui/coreX", "ui/w/coreX")],
                namespaces=[],
                targets=[],
            )
            self.assertTrue((repo.root / "src/ui/w/coreX/y.h").is_file())
        finally:
            repo.close()

    def test_the_planned_multi_move_is_accepted(self):
        repo = Repo({f"src/ui/{n}/x.h": "" for n in ("core", "oled", "grid")})
        try:
            code = relocate.main(
                ["--repo", str(repo.root)]
                + [
                    a
                    for n in ("core", "oled", "grid")
                    for a in ("--move", f"ui/{n}", f"ui/widgets/{n}")
                ]
            )
            self.assertEqual(code, 0)
            self.assertTrue((repo.root / "src/ui/widgets/grid/x.h").is_file())
        finally:
            repo.close()

    def test_raw_strings_with_encoding_prefixes_are_code(self):
        repo = Repo(
            {
                "src/scenes/a.h": "",
                "src/x.cpp": 'auto a = u8R"(scenes/x)"; auto b = LR"(scenes/y)"; // scenes/z\n'
                'auto c = u8R"(")// scenes/w)";\n',
            }
        )
        try:
            relocate.apply(
                repo.root, moves=[("scenes", "ui/scenes")], namespaces=[], targets=[]
            )
            self.assertEqual(
                repo.read("src/x.cpp"),
                'auto a = u8R"(scenes/x)"; auto b = LR"(scenes/y)"; // ui/scenes/z\n'
                'auto c = u8R"(")// scenes/w)";\n',
            )
        finally:
            repo.close()

    def test_a_line_comment_continued_by_a_backslash_is_one_comment(self):
        repo = Repo(
            {
                "src/scenes/a.h": "",
                "src/x.cpp": "// a \\\nscenes/x\nscenes/y\n",
            }
        )
        try:
            relocate.apply(
                repo.root, moves=[("scenes", "ui/scenes")], namespaces=[], targets=[]
            )
            self.assertEqual(repo.read("src/x.cpp"), "// a \\\nui/scenes/x\nscenes/y\n")
        finally:
            repo.close()

    def test_a_target_is_not_renamed_inside_a_hyphenated_name(self):
        repo = self.moved(
            {"src/CMakeLists.txt": "t sample-view sample-view-tests\n"},
            targets=[("sample-view", "X")],
        )
        self.assertEqual(repo.read("src/CMakeLists.txt"), "t X sample-view-tests\n")

    def test_the_website_is_never_rewritten(self):
        repo = self.moved({"website/a.md": "src/scenes\n"})
        self.assertEqual(repo.read("website/a.md"), "src/scenes\n")

    def test_dry_run_prints_each_rewritten_non_include_line(self):
        repo = Repo(
            {
                "src/scenes/a.h": "",
                "d.md": "see src/scenes\n",
                "x.cpp": '#include "scenes/a.h"\n',
            }
        )
        try:
            out = subprocess.run(
                [
                    "python3",
                    str(Path(relocate.__file__)),
                    "--repo",
                    str(repo.root),
                    "--move",
                    "scenes",
                    "ui/scenes",
                    "--dry-run",
                ],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
            self.assertIn("d.md: -see src/scenes / +see src/ui/scenes", out)
            self.assertNotIn("x.cpp:", out)
        finally:
            repo.close()


class Refusals(unittest.TestCase):
    def test_a_move_the_legacy_root_shadows_is_refused(self):
        repo = Repo(
            {
                "src/deluge/storage/a.h": "",
                "src/storage/b.h": "",
                "src/x.cpp": '#include "storage/b.h"\n',
            }
        )
        try:
            with self.assertRaises(relocate.Refused):
                relocate.apply(
                    repo.root,
                    moves=[("storage", "base/storage")],
                    namespaces=[],
                    targets=[],
                )
            self.assertTrue((repo.root / "src/storage/b.h").is_file())
            self.assertEqual(repo.read("src/x.cpp"), '#include "storage/b.h"\n')
        finally:
            repo.close()

    def test_a_move_onto_an_existing_path_is_refused(self):
        repo = Repo({"src/a/x.h": "", "src/b/a/y.h": ""})
        try:
            with self.assertRaises(relocate.Refused):
                relocate.apply(
                    repo.root, moves=[("a", "b/a")], namespaces=[], targets=[]
                )
        finally:
            repo.close()


class MoreRefusals(unittest.TestCase):
    def test_a_move_onto_an_existing_unit_test_directory_is_refused(self):
        repo = Repo({"src/a/x.h": "", "tests/unit/b/y.cpp": ""})
        try:
            with self.assertRaises(relocate.Refused):
                relocate.apply(repo.root, moves=[("a", "b")], namespaces=[], targets=[])
        finally:
            repo.close()


class DryRun(unittest.TestCase):
    def test_plan_changes_nothing_on_disk(self):
        repo = Repo(FILES)
        try:
            changes = relocate.plan(repo.root, [("scenes", "ui/scenes")], [], [])
            self.assertIn("src/deluge/model/song.cpp", changes)
            self.assertEqual(
                repo.read("src/deluge/model/song.cpp"),
                FILES["src/deluge/model/song.cpp"],
            )
        finally:
            repo.close()


if __name__ == "__main__":
    unittest.main()
