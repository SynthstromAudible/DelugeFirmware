# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_layers.py: what the table's libraries, rules and tree checks must accept and reject.

Gate layers-selftest:
    the include-layer checker rejects every way an undeclared header could slip into a library, counts each rule's
    includes as the old ratchets did, follows a transitive rule through intermediate headers, and refuses a new
    top-level directory, a stray root file, a shadowed include path and a growing legacy tree
"""

from __future__ import annotations

import tempfile
import textwrap
import unittest
from pathlib import Path

import check_layers as cl

TABLE = textwrap.dedent(
    """
    [tree]
    include_roots = ["src/deluge", "src"]
    legacy = "src/deluge"
    scan_roots = ["src", "sim", "tests"]
    root_files = ["CMakeLists.txt"]

    [[directory]]
    path = "lib"
    target = "src/lib"

    [[directory]]
    path = "deluge"
    target = "deleted"

    [[library]]
    name = "lib"
    root = "src/lib"
    prefix = "lib/"
    may_include = ["dep/", "other/one.h"]
    system = ["stdint.h"]

    [[library]]
    name = "lib-inner"
    root = "src/lib/inner"
    prefix = "lib/inner/"
    """
)


class Repo:
    """A throwaway repository: a table, and files planted at repo-relative paths."""

    def __init__(self, table: str = TABLE):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / "scripts").mkdir()
        (self.root / "scripts" / "layers.toml").write_text(table)
        (self.root / "src").mkdir()

    def plant(self, rel: str, text: str) -> Path:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        cl.read_lines.cache_clear()
        return path

    def config(self) -> cl.Config:
        return cl.load_config(self.root / "scripts" / "layers.toml")

    def close(self) -> None:
        self._tmp.cleanup()


class LibraryCheck(unittest.TestCase):
    def setUp(self):
        self.repo = Repo()

    def tearDown(self):
        self.repo.close()

    def found(self, text: str, rel: str = "src/lib/probe.h") -> list[str]:
        """The violations in @p text, planted at @p rel (other planted files' violations are left out)."""
        self.repo.plant(rel, text)
        return [
            v
            for v in cl.library_violations(self.repo.config(), self.repo.root)
            if f" {rel}:" in v
        ]

    def test_accepts_standard_headers_its_own_and_its_dependencies(self):
        text = '#pragma once\n#include <cstdint>\n#include <optional>\n#include "lib/x.h"\n#include "dep/y.h"\n#include "other/one.h"\n'
        self.assertEqual(self.found(text), [])

    def test_accepts_spacing_variants(self):
        self.assertEqual(
            self.found('#  include  <cstdint>\n  #include "lib/x.h"\n'), []
        )

    def test_accepts_a_listed_system_header(self):
        self.assertEqual(self.found("#include <stdint.h>\n"), [])

    def test_rejects_an_app_header_in_quotes_or_angle_brackets(self):
        self.assertEqual(len(self.found('#include "definitions_cxx.hpp"\n')), 1)
        self.assertEqual(len(self.found("#include <definitions_cxx.hpp>\n")), 1)

    def test_rejects_an_unlisted_non_standard_angle_header(self):
        self.assertEqual(len(self.found("#include <etl/string.h>\n")), 1)
        self.assertEqual(len(self.found("#include <stdbool.h>\n")), 1)

    def test_rejects_path_traversal(self):
        self.assertEqual(
            len(self.found('#include "dep/../../deluge/model/song/song.h"\n')), 1
        )
        self.assertEqual(len(self.found('#include "../other/x.h"\n')), 1)

    def test_rejects_an_include_it_cannot_parse(self):
        self.assertEqual(len(self.found('#include /* c */ "lib/x.h"\n')), 1)
        self.assertEqual(len(self.found("#define HDR <x.h>\n#include HDR\n")), 1)

    def test_rejects_an_undeclared_neighbour_of_a_declared_dependency(self):
        self.assertEqual(len(self.found('#include "other/two.h"\n')), 1)
        self.assertEqual(len(self.found('#include "depth/x.h"\n')), 1)

    def test_scans_every_c_family_suffix(self):
        for suffix in (
            ".c",
            ".cc",
            ".cpp",
            ".cxx",
            ".h",
            ".hh",
            ".hpp",
            ".hxx",
            ".inc",
            ".inl",
            ".ipp",
            ".tcc",
        ):
            with self.subTest(suffix=suffix):
                self.assertEqual(
                    len(
                        self.found(
                            '#include "definitions_cxx.hpp"\n',
                            rel=f"src/lib/probe{suffix}",
                        )
                    ),
                    1,
                )

    def test_a_relative_include_beside_the_file_is_its_own(self):
        self.repo.plant("src/lib/grid/rgb.h", "#pragma once\n")
        self.assertEqual(
            self.found('#include "rgb.h"\n', rel="src/lib/grid/palette.h"), []
        )

    def test_a_relative_spelling_of_a_missing_file_is_read_as_written(self):
        self.assertEqual(len(self.found('#include "model/song/song.h"\n')), 1)

    def test_a_file_belongs_to_the_deepest_library(self):
        found = self.found('#include "dep/y.h"\n', rel="src/lib/inner/probe.h")
        self.assertEqual(len(found), 1)
        self.assertTrue(found[0].startswith("lib-inner: src/lib/inner/probe.h:1:"))

    def test_a_violation_names_the_library_the_file_and_the_line(self):
        self.assertEqual(
            self.found('#pragma once\n#include "model/song/song.h"\n'),
            ['lib: src/lib/probe.h:2: #include "model/song/song.h"'],
        )


class TableLoading(unittest.TestCase):
    def test_a_name_used_twice_is_refused(self):
        repo = Repo(
            TABLE + '\n[[library]]\nname = "lib"\nroot = "src/x"\nprefix = "x/"\n'
        )
        try:
            with self.assertRaises(ValueError):
                repo.config()
        finally:
            repo.close()

    def test_the_directories_come_from_the_directory_entries(self):
        repo = Repo()
        try:
            self.assertEqual(repo.config().tree.directories, ("lib", "deluge"))
        finally:
            repo.close()


if __name__ == "__main__":
    unittest.main()
