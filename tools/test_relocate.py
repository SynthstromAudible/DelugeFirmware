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

    def relocate_rc(self, *args: str) -> int:
        return subprocess.run(
            ["python3", str(Path(relocate.__file__)), "--repo", str(self.root), *args],
            capture_output=True,
            check=False,
        ).returncode

    def relocate(self, *args: str) -> None:
        rc = self.relocate_rc(*args)
        assert rc == 0, f"relocate {args} exited {rc}"

    def dry(self, *args: str) -> str:
        return subprocess.run(
            [
                "python3",
                str(Path(relocate.__file__)),
                "--repo",
                str(self.root),
                *args,
                "--dry-run",
            ],
            capture_output=True,
            text=True,
            check=True,
        ).stdout

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


class LegacyRoot(unittest.TestCase):
    def test_move_out_of_the_legacy_root(self):
        repo = Repo(
            {
                "src/deluge/x/a.h": "#pragma once\n",
                "src/deluge/x/b.h": '#include "x/a.h"\n',
                "src/deluge/model/m.cpp": '#include "x/a.h"\n#include "model/m.h"\n',
                "src/deluge/model/m.h": "",
                "src/synth/s.cpp": '#include <x/a.h>\n#include "deluge/x/a.h"\n// see x/a.h\n',
                "src/deluge/x/CMakeLists.txt": "# x/ headers\n",
                "tests/unit/deluge/x/a_spec.cpp": '#include "x/a.h"\n',
                "docs/dev/d.md": "`src/deluge/x/a.h` is `x/a.h`; tests in `tests/unit/deluge/x`.\n",
            }
        )
        self.addCleanup(repo.close)
        relocate.apply(repo.root, moves=[("deluge/x", "y")], namespaces=[], targets=[])
        self.assertTrue((repo.root / "src/y/a.h").is_file())
        self.assertFalse((repo.root / "src/deluge/x").exists())
        self.assertTrue((repo.root / "tests/unit/y/a_spec.cpp").is_file())
        self.assertEqual(repo.read("src/y/b.h"), '#include "y/a.h"\n')
        self.assertEqual(
            repo.read("src/deluge/model/m.cpp"),
            '#include "y/a.h"\n#include "model/m.h"\n',
        )
        self.assertEqual(
            repo.read("src/synth/s.cpp"),
            '#include <y/a.h>\n#include "y/a.h"\n// see y/a.h\n',
        )
        self.assertEqual(repo.read("src/y/CMakeLists.txt"), "# y/ headers\n")
        self.assertEqual(repo.read("tests/unit/y/a_spec.cpp"), '#include "y/a.h"\n')
        self.assertEqual(
            repo.read("docs/dev/d.md"),
            "`src/y/a.h` is `y/a.h`; tests in `tests/unit/y`.\n",
        )

    def test_a_move_within_the_legacy_root_keeps_the_short_spelling(self):
        repo = Repo(
            {
                "src/deluge/x/a.h": "",
                "src/deluge/model/m.cpp": '#include "x/a.h"\n',
            }
        )
        self.addCleanup(repo.close)
        relocate.apply(
            repo.root, moves=[("deluge/x", "deluge/x_old")], namespaces=[], targets=[]
        )
        self.assertEqual(repo.read("src/deluge/model/m.cpp"), '#include "x_old/a.h"\n')

    def test_a_new_name_the_legacy_root_shadows_is_refused(self):
        repo = Repo({"src/deluge/x/a.h": "", "src/deluge/y/b.h": ""})
        self.addCleanup(repo.close)
        with self.assertRaises(relocate.Refused):
            relocate.apply(
                repo.root, moves=[("deluge/x", "y")], namespaces=[], targets=[]
            )
        self.assertTrue((repo.root / "src/deluge/x/a.h").is_file())
        self.assertFalse((repo.root / "src/y").exists())

    def test_an_old_name_src_also_holds_is_refused(self):
        repo = Repo(
            {
                "src/deluge/x/a.h": "",
                "src/x/b.h": "",
                "docs/d.md": "see `x/b.h`\n",
            }
        )
        self.addCleanup(repo.close)
        with self.assertRaises(relocate.Refused):
            relocate.apply(
                repo.root, moves=[("deluge/x", "y")], namespaces=[], targets=[]
            )
        self.assertTrue((repo.root / "src/deluge/x/a.h").is_file())
        self.assertFalse((repo.root / "src/y").exists())
        self.assertEqual(repo.read("docs/d.md"), "see `x/b.h`\n")


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


class RepoMoves(unittest.TestCase):
    def repo(self, files):
        r = Repo(files)
        self.addCleanup(r.close)
        return r

    def test_a_member_crate_moves_and_its_workspace_and_dependents_follow(self):
        r = self.repo(
            {
                "crates/Cargo.toml": '[workspace]\nmembers = ["a", "b"]\n',
                "crates/a/Cargo.toml": '[package]\nname = "a"\n[lib]\nname = "a"\n',
                "crates/a/src/lib.rs": "pub fn f() {}\n",
                "crates/b/Cargo.toml": '[package]\nname = "b"\n[dependencies]\na = { path = "../a" }\n',
                "crates/b/src/lib.rs": "use a::f;\n",
                "bsp/Cargo.toml": '[package]\nname = "bsp"\n[dependencies]\na = { path = "../crates/a" }\n',
                "CMakeLists.txt": "set(X ${CMAKE_SOURCE_DIR}/crates/a/include)\n",
            }
        )
        r.relocate(
            "--repo-move",
            "crates/a",
            "src/base/alloc",
            "--crate",
            "a=deluge_base_alloc",
        )
        self.assertIn(
            'members = ["../src/base/alloc", "b"]', r.read("crates/Cargo.toml")
        )
        a = r.read("src/base/alloc/Cargo.toml")
        self.assertIn('name = "deluge_base_alloc"', a)
        self.assertIn('workspace = "../../../crates"', a)
        self.assertIn(
            'deluge_base_alloc = { path = "../../src/base/alloc" }',
            r.read("crates/b/Cargo.toml"),
        )
        self.assertIn(
            'deluge_base_alloc = { path = "../src/base/alloc" }',
            r.read("bsp/Cargo.toml"),
        )
        self.assertEqual(r.read("crates/b/src/lib.rs"), "use deluge_base_alloc::f;\n")
        self.assertIn(
            "${CMAKE_SOURCE_DIR}/src/base/alloc/include", r.read("CMakeLists.txt")
        )

    def test_a_moved_manifest_recomputes_its_outgoing_paths(self):
        r = self.repo(
            {
                "src/bsp/rust/Cargo.toml": '[package]\nname = "deluge-bsp-rust"\n[dependencies]\nx = { path = "../../../crates/x" }\n',
                "src/bsp/rust/src/lib.rs": "",
                "crates/x/Cargo.toml": '[package]\nname = "x"\n',
                "crates/x/src/lib.rs": "",
            }
        )
        r.relocate("--repo-move", "src/bsp/rust", "src/platform/device")
        self.assertIn(
            'x = { path = "../../../crates/x" }',
            r.read("src/platform/device/Cargo.toml"),
        )

    def test_a_manifest_moving_deeper_recomputes_its_outgoing_paths(self):
        r = self.repo(
            {
                "bsp/Cargo.toml": '[package]\nname = "bsp"\n[dependencies]\nx = { path = "../crates/x" }\n',
                "crates/x/Cargo.toml": '[package]\nname = "x"\n',
            }
        )
        r.relocate("--repo-move", "bsp", "src/platform/device")
        self.assertIn(
            'x = { path = "../../../crates/x" }',
            r.read("src/platform/device/Cargo.toml"),
        )

    def test_a_path_token_is_rewritten_only_on_a_path_boundary(self):
        r = self.repo(
            {
                "sim/CMakeLists.txt": "",
                "a.cmake": "-S sim\n${R}/sim/compat\nsimulate\nsim_panel\n",
            }
        )
        r.relocate("--repo-move", "sim", "src/platform/host")
        self.assertEqual(
            r.read("a.cmake"),
            "-S src/platform/host\n${R}/src/platform/host/compat\nsimulate\nsim_panel\n",
        )

    def test_a_crate_rename_leaves_longer_identifiers(self):
        r = self.repo(
            {
                "crates/deluge_rust/Cargo.toml": '[package]\nname = "deluge_rust"\n',
                "crates/deluge_rust/src/lib.rs": "",
                "t/CMakeLists.txt": "deluge_rust_rs libdeluge_rust.a -p deluge_rust\n",
            }
        )
        r.relocate(
            "--repo-move",
            "crates/deluge_rust",
            "tests/trees/host/rust",
            "--crate",
            "deluge_rust=deluge_test_rust",
        )
        self.assertEqual(
            r.read("t/CMakeLists.txt"),
            "deluge_rust_rs libdeluge_test_rust.a -p deluge_test_rust\n",
        )

    def test_refuses_a_target_inside_the_source(self):
        r = self.repo({"crates/a/Cargo.toml": '[package]\nname = "a"\n'})
        self.assertEqual(r.relocate_rc("--repo-move", "crates", "crates/x"), 2)

    def test_refuses_an_existing_target(self):
        r = self.repo({"a/x": "", "b/y": ""})
        self.assertEqual(r.relocate_rc("--repo-move", "a", "b"), 2)

    def test_refuses_an_untracked_source(self):
        r = self.repo({"a/x": ""})
        self.assertEqual(r.relocate_rc("--repo-move", "nope", "c"), 2)

    def test_refuses_to_combine_with_the_library_move(self):
        r = self.repo({"a/x": ""})
        self.assertEqual(r.relocate_rc("--repo-move", "a", "c", "--move", "x", "y"), 2)

    def test_refuses_a_new_path_that_is_not_normal_before_writing_anything(self):
        for new in ("../c", "c/../d", "./c", "/c", "c//d"):
            with self.subTest(new=new):
                r = self.repo({"a/x": "", "d.md": "see a/x\n"})
                self.assertEqual(r.relocate_rc("--repo-move", "a", new), 2)
                self.assertEqual(r.read("d.md"), "see a/x\n")
                self.assertTrue((r.root / "a/x").is_file())

    def test_refuses_a_member_glob_that_matches_the_moved_crate(self):
        r = self.repo(
            {
                "w/Cargo.toml": '[workspace]\nmembers = ["m/*"]\n',
                "w/m/a/Cargo.toml": '[package]\nname = "a"\n',
            }
        )
        self.assertEqual(r.relocate_rc("--repo-move", "w/m/a", "z/a"), 2)
        self.assertTrue((r.root / "w/m/a/Cargo.toml").is_file())

    def test_a_dependency_alias_keeps_its_key_and_gets_the_new_package_name(self):
        r = self.repo(
            {
                "crates/alloc/Cargo.toml": '[package]\nname = "deluge_alloc"\n',
                "bsp/Cargo.toml": (
                    '[package]\nname = "bsp"\n'
                    '[dependencies]\nfs_alloc = { package = "deluge_alloc", path = "../crates/alloc" }\n'
                    "[target.'cfg(target_os = \"none\")'.dependencies]\n"
                    'deluge_alloc = { path = "../crates/alloc", optional = true }\n'
                    '[dependencies.other]\npath = "../crates/alloc"\n'
                    "[profile.release.package.deluge_alloc]\nopt-level = 3\n"
                    '[features]\nasan = ["deluge_alloc/asan", "dep:deluge_alloc"]\n'
                ),
            }
        )
        r.relocate(
            "--repo-move",
            "crates/alloc",
            "src/base/alloc",
            "--crate",
            "deluge_alloc=deluge_base_alloc",
        )
        bsp = r.read("bsp/Cargo.toml")
        self.assertIn(
            'fs_alloc = { package = "deluge_base_alloc", path = "../src/base/alloc" }',
            bsp,
        )
        self.assertIn(
            'deluge_base_alloc = { path = "../src/base/alloc", optional = true }', bsp
        )
        self.assertIn('[dependencies.other]\npath = "../src/base/alloc"\n', bsp)
        self.assertIn("[profile.release.package.deluge_base_alloc]", bsp)
        self.assertIn('["deluge_base_alloc/asan", "dep:deluge_base_alloc"]', bsp)
        self.assertIn('name = "deluge_base_alloc"', r.read("src/base/alloc/Cargo.toml"))

    def test_a_dependencies_block_is_renamed_unless_it_aliases(self):
        r = self.repo(
            {
                "c/a/Cargo.toml": '[package]\nname = "a"\n',
                "m/Cargo.toml": (
                    '[package]\nname = "m"\n[dependencies.a]\npath = "../c/a"\nfeatures = ["x"]\n'
                    '[dev-dependencies.k]\npackage = "a"\npath = "../c/a"\n'
                ),
            }
        )
        r.relocate("--repo-move", "c/a", "d/z", "--crate", "a=zed")
        m = r.read("m/Cargo.toml")
        self.assertIn('[dependencies.zed]\npath = "../d/z"', m)
        self.assertIn('[dev-dependencies.k]\npackage = "zed"\npath = "../d/z"', m)

    def test_a_moved_member_inside_a_multiline_members_array_and_exclude_follow(self):
        r = self.repo(
            {
                "w/Cargo.toml": '[workspace]\nmembers = [\n    "a", # one\n    "b",\n]\nexclude = ["a/fuzz"]\n',
                "w/a/Cargo.toml": '[package]\nname = "a"\n',
                "w/a/fuzz/Cargo.toml": '[package]\nname = "f"\n[dependencies.a]\npath = ".."\n',
                "w/b/Cargo.toml": '[package]\nname = "b"\n',
            }
        )
        r.relocate("--repo-move", "w/a", "z/a")
        w = r.read("w/Cargo.toml")
        self.assertIn('    "../z/a", # one\n    "b",', w)
        self.assertIn('exclude = ["../z/a/fuzz"]', w)
        self.assertIn('workspace = "../../w"', r.read("z/a/Cargo.toml"))
        self.assertIn('path = ".."', r.read("z/a/fuzz/Cargo.toml"))
        self.assertNotIn("workspace", r.read("z/a/fuzz/Cargo.toml"))

    def test_a_cargo_path_is_not_also_rewritten_as_a_token(self):
        r = self.repo(
            {
                "crates/x/Cargo.toml": '[package]\nname = "x"\n',
                "src/bsp/Cargo.toml": '[package]\nname = "b"\n[dependencies]\nx = { path = "../../crates/x" }\n# see crates/x\n',
            }
        )
        r.relocate("--repo-move", "crates/x", "src/base/x")
        self.assertEqual(
            r.read("src/bsp/Cargo.toml"),
            '[package]\nname = "b"\n[dependencies]\nx = { path = "../base/x" }\n# see src/base/x\n',
        )

    def test_the_north_star_and_lock_files_are_never_rewritten(self):
        r = self.repo(
            {
                "sim/a": "",
                "docs/dev/code_layout_north_star.md": "| `sim` |\n",
                "Cargo.lock": 'name = "sim"\n# sim\n',
            }
        )
        r.relocate("--repo-move", "sim", "src/platform/host")
        self.assertEqual(r.read("docs/dev/code_layout_north_star.md"), "| `sim` |\n")
        self.assertEqual(r.read("Cargo.lock"), 'name = "sim"\n# sim\n')

    def test_rust_identifiers_follow_a_rename_only_in_crate_position(self):
        r = self.repo(
            {
                "c/a/Cargo.toml": '[package]\nname = "a_crate"\n',
                "m/x.rs": "extern crate a_crate;\nuse a_crate::f;\nuse ::a_crate::g;\nlet a_crate_x = a_crate::h(); let a_crate = 1;\n",
            }
        )
        r.relocate("--repo-move", "c/a", "d/z", "--crate", "a_crate=zed")
        self.assertEqual(
            r.read("m/x.rs"),
            "extern crate zed;\nuse zed::f;\nuse ::zed::g;\nlet a_crate_x = zed::h(); let a_crate = 1;\n",
        )

    def test_the_tools_own_tests_and_fixtures_are_never_rewritten(self):
        files = {
            "sim/a": "",
            "tools/test_relocate.py": 'r.relocate("--repo-move", "sim", "x")\n',
            "tools/test_compare_strings.py": "sim/a\n",
            "tools/test_compare_codegen.py": "sim/a\n",
            "tools/testdata/t.txt": "sim/a\n",
            "tools/other.py": "sim/a\n",
        }
        r = self.repo(files)
        r.relocate("--repo-move", "sim", "src/platform/host")
        for rel in list(files)[1:5]:
            self.assertEqual(r.read(rel), files[rel], rel)
        self.assertEqual(r.read("tools/other.py"), "src/platform/host/a\n")

    def test_a_path_is_rewritten_where_it_starts_one(self):
        r = self.repo(
            {
                "crates/deluge_alloc/Cargo.toml": "",
                "d.md": (
                    "`crates/deluge_alloc/src/slab.rs` and `crates/deluge_alloc`\n"
                    "see tests/unit/crates/deluge_alloc/x.cpp and tests/unit/crates/deluge_alloc\n"
                    "(crates/deluge_alloc) a=crates/deluge_alloc, [crates/deluge_alloc] k:crates/deluge_alloc\n"
                    "./crates/deluge_alloc ../../crates/deluge_alloc ${R}/crates/deluge_alloc $R/crates/deluge_alloc\n"
                ),
            }
        )
        r.relocate("--repo-move", "crates/deluge_alloc", "src/base/alloc")
        new = "src/base/alloc"
        self.assertEqual(
            r.read("d.md"),
            f"`{new}/src/slab.rs` and `{new}`\n"
            "see tests/unit/crates/deluge_alloc/x.cpp and tests/unit/crates/deluge_alloc\n"
            f"({new}) a={new}, [{new}] k:{new}\n"
            f"./{new} ../../{new} ${{R}}/{new} $R/{new}\n",
        )

    def test_a_backticked_directory_with_a_slash_is_rewritten(self):
        r = self.repo({"sim/a": "", "d.md": "the `sim/` tree and `sim/compat`\n"})
        r.relocate("--repo-move", "sim", "src/platform/host")
        self.assertEqual(
            r.read("d.md"),
            "the `src/platform/host/` tree and `src/platform/host/compat`\n",
        )

    def test_a_path_starting_a_regex_alternative_is_rewritten_but_a_bare_one_is_listed(
        self,
    ):
        r = self.repo(
            {
                "sim/a": "",
                "c.yaml": "files: ^(src/|sim/|tests/)\nfiles: ^(tests/|sim/scenes_spec/)\nfiles: ^(src|sim|tests)/\n",
            }
        )
        out = r.dry("--repo-move", "sim", "src/platform/host")
        r.relocate("--repo-move", "sim", "src/platform/host")
        self.assertEqual(
            r.read("c.yaml"),
            "files: ^(src/|src/platform/host/|tests/)\n"
            "files: ^(tests/|src/platform/host/scenes_spec/)\n"
            "files: ^(src|sim|tests)/\n",
        )
        listing = out.split("bare mentions, not rewritten")[1]
        self.assertIn("c.yaml:3:", listing)
        self.assertNotIn("c.yaml:1:", listing)

    def test_a_bare_single_component_is_left_and_listed(self):
        r = self.repo(
            {
                "sim/a": "",
                "d.md": 'run ./dbt sim\nprog="sim"\nfeatures = ["sim"]\nthe host sim here\nsim/compat\n',
                "c.cmake": "-S sim\nadd_subdirectory(sim)\nadd_subdirectory(${R}/sim ${B}/sim)\n",
            }
        )
        out = r.dry("--repo-move", "sim", "src/platform/host")
        r.relocate("--repo-move", "sim", "src/platform/host")
        self.assertEqual(
            r.read("d.md"),
            'run ./dbt sim\nprog="sim"\nfeatures = ["sim"]\nthe host sim here\nsrc/platform/host/compat\n',
        )
        self.assertEqual(
            r.read("c.cmake"),
            "-S src/platform/host\nadd_subdirectory(src/platform/host)\n"
            "add_subdirectory(${R}/src/platform/host ${B}/src/platform/host)\n",
        )
        listing = out.split("bare mentions, not rewritten")[1]
        for line in ("d.md:1:", "d.md:2:", "d.md:3:", "d.md:4:"):
            self.assertIn(line, listing)
        self.assertNotIn("d.md:5:", listing)

    def test_a_backticked_crate_name_is_left_and_listed_not_rewritten(self):
        r = self.repo(
            {
                "c/a/Cargo.toml": '[package]\nname = "deluge_alloc"\n',
                "README.md": "the `deluge_alloc`/`deluge_free` pair; deluge_alloc heap; libdeluge_alloc.a -p deluge_alloc\n",
                "x.cpp": "// behind the `deluge_alloc` C ABI\nvoid* p = deluge_alloc(h);\n",
            }
        )
        out = r.dry("--repo-move", "c/a", "d/z", "--crate", "deluge_alloc=zed")
        r.relocate("--repo-move", "c/a", "d/z", "--crate", "deluge_alloc=zed")
        self.assertEqual(
            r.read("README.md"),
            "the `deluge_alloc`/`deluge_free` pair; deluge_alloc heap; libzed.a -p zed\n",
        )
        self.assertEqual(
            r.read("x.cpp"),
            "// behind the `deluge_alloc` C ABI\nvoid* p = deluge_alloc(h);\n",
        )
        listing = out.split("crate-name mentions, not rewritten")[1]
        self.assertIn("README.md:1:", listing)
        self.assertIn("x.cpp:1:", listing)
        self.assertNotIn("x.cpp:2:", listing)

    def test_a_hyphenated_crate_also_renames_its_underscore_spelling_in_rust_and_archives(
        self,
    ):
        r = self.repo(
            {
                "b/Cargo.toml": '[package]\nname = "deluge-bsp-rust"\n[lib]\nname = "deluge_bsp_rust"\n',
                "b/src/lib.rs": "",
                "m/Cargo.toml": '[package]\nname = "m"\n[dependencies]\ndeluge-bsp-rust = { path = "../b" }\n',
                "m/src/lib.rs": "use deluge_bsp_rust::f;\n",
                "CMakeLists.txt": "libdeluge_bsp_rust.a -p deluge-bsp-rust\n",
            }
        )
        r.relocate(
            "--repo-move", "b", "p/dev", "--crate", "deluge-bsp-rust=deluge-plat-dev"
        )
        self.assertIn('name = "deluge-plat-dev"', r.read("p/dev/Cargo.toml"))
        self.assertIn('name = "deluge_plat_dev"', r.read("p/dev/Cargo.toml"))
        self.assertIn('deluge-plat-dev = { path = "../p/dev" }', r.read("m/Cargo.toml"))
        self.assertEqual(r.read("m/src/lib.rs"), "use deluge_plat_dev::f;\n")
        self.assertEqual(
            r.read("CMakeLists.txt"), "libdeluge_plat_dev.a -p deluge-plat-dev\n"
        )

    def test_dotted_dependency_keys_are_renamed(self):
        r = self.repo(
            {
                "c/al/Cargo.toml": '[package]\nname = "al"\n',
                "m/Cargo.toml": '[package]\nname = "m"\n[dependencies]\nal.path = "../c/al"\nal.optional = true\n',
            }
        )
        r.relocate("--repo-move", "c/al", "d/z", "--crate", "al=zed")
        self.assertEqual(
            r.read("m/Cargo.toml"),
            '[package]\nname = "m"\n[dependencies]\nzed.path = "../d/z"\nzed.optional = true\n',
        )

    def test_dry_run_lists_the_move_and_changes_nothing(self):
        r = self.repo({"sim/a": "", "d.md": "see sim/a\n"})
        out = subprocess.run(
            [
                "python3",
                str(Path(relocate.__file__)),
                "--repo",
                str(r.root),
                "--repo-move",
                "sim",
                "x/sim",
                "--dry-run",
            ],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        self.assertIn("git mv sim x/sim", out)
        self.assertIn("d.md: -see sim/a / +see x/sim/a", out)
        self.assertEqual(r.read("d.md"), "see sim/a\n")
        self.assertTrue((r.root / "sim/a").is_file())


if __name__ == "__main__":
    unittest.main()
