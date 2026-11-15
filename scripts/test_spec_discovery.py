# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for the spec trees' discovery rules (tests/support/cmake/specs.cmake), against a fixture of test directories.

Each test configures the fixture as one tree would (clang, Ninja, CppSpec and argon stubbed so nothing is fetched)
and checks the ctest names the tree gets, or that the configure is refused and says why.

Gate spec-discovery:
    a test tree builds exactly the test directories that name it, and refuses to configure when a spec has no driver
    above it or a CMakeLists.txt names no tree or an unknown one: TREES is read after a newline or a tab, ONLY_<TREE>
    names a tree only as a whole word, and a comment names nothing, while a # inside a word starts no comment
"""

from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

ROOT = """cmake_minimum_required(VERSION 3.24)
project(SpecDiscoveryFixture CXX)
set(DELUGE_TEST_TREE ${FIXTURE_TREE})
include(${REPO}/tests/support/cmake/specs.cmake)
enable_testing()
deluge_spec_directories(${CMAKE_CURRENT_LIST_DIR}/${FIXTURE_BASE})
"""

# Stand-ins for the fetched dependencies: specs.cmake only needs their targets to exist.
STUBS = {
    "stubs/cppspec/CMakeLists.txt": "add_library(c++spec INTERFACE)\n",
    "stubs/argon/CMakeLists.txt": "add_library(argon INTERFACE)\n"
    "target_include_directories(argon INTERFACE ${CMAKE_CURRENT_LIST_DIR})\n",
}

# The directories every tree reads: what each tree builds from them is in TREES below.
GOOD = {
    # Specs in the directory and below it, for host and arm; a token merely containing ONLY_ names no tree.
    "unit/a/CMakeLists.txt": "# The TREES are host and arm.\n"
    "deluge_specs_driver(a_specs TREES host arm)\n"
    "set(FOO_ONLY_MONO 1)\n",
    "unit/a/one_spec.cpp": "",
    "unit/a/deep/two_spec.cpp": "",
    # A subdirectory with its own CMakeLists.txt builds its own specs, here on arm alone.
    "unit/a/c/CMakeLists.txt": "deluge_specs_driver(c_specs TREES arm)\n",
    "unit/a/c/four_spec.cpp": "",
    # ONLY_ARM moves one spec to the arm tree, which TREES does not name.
    "unit/b/CMakeLists.txt": "deluge_specs_driver(b_specs TREES host ONLY_ARM arm_only_spec.cpp)\n",
    "unit/b/three_spec.cpp": "",
    "unit/b/arm_only_spec.cpp": "",
    # A directory with no spec driver names its trees with deluge_test_directory.
    "unit/d/CMakeLists.txt": "deluge_test_directory(TREES ubsan)\nadd_test(NAME d_marker COMMAND true)\n",
    # TREES on the line after the driver's name.
    "unit/e/CMakeLists.txt": "deluge_specs_driver(e_specs\n  TREES host arm ONLY_ARM e_arm_spec.cpp)\n",
    "unit/e/five_spec.cpp": "",
    "unit/e/e_arm_spec.cpp": "",
    # TREES after a tab.
    "unit/f/CMakeLists.txt": "deluge_specs_driver(f_specs\tTREES\thost)\n",
    "unit/f/six_spec.cpp": "",
    # A # inside a quoted word starts no comment, so the ONLY_ARM after it is read.
    "unit/g/CMakeLists.txt": 'deluge_specs_driver(g_specs TREES host INCLUDES "x#y" ONLY_ARM g_arm_spec.cpp)\n',
    "unit/g/seven_spec.cpp": "",
    "unit/g/g_arm_spec.cpp": "",
    # A keyword in a comment names no tree.
    "unit/i/CMakeLists.txt": "# Not ONLY_ARM.\ndeluge_test_directory(TREES host)\nadd_test(NAME i_marker COMMAND true)\n",
}

TREES = {
    "host": [
        "five_spec",
        "i_marker",
        "one_spec",
        "seven_spec",
        "six_spec",
        "three_spec",
        "two_spec",
    ],
    "arm": [
        "arm_only_spec",
        "e_arm_spec",
        "five_spec",
        "four_spec",
        "g_arm_spec",
        "one_spec",
        "two_spec",
    ],
    "ubsan": ["d_marker"],
}

# Each base the host tree must refuse, with a fragment of the reason it gives.
BAD = {
    "bad-none": (
        {"x/CMakeLists.txt": "add_test(NAME x_marker COMMAND true)\n"},
        "names no tree",
    ),
    "bad-typo": (
        {
            "x/CMakeLists.txt": "deluge_specs_driver(x_specs TREES hots)\n",
            "x/x_spec.cpp": "",
        },
        "unknown tree 'hots'",
    ),
    # A spec with no CMakeLists.txt between it and the base.
    "bad-orphan": (
        {
            "x/CMakeLists.txt": "deluge_specs_driver(x_specs TREES host)\n",
            "x/x_spec.cpp": "",
            "y/z/orphan_spec.cpp": "",
        },
        "y/z/orphan_spec.cpp",
    ),
    # A spec whose nearest CMakeLists.txt declares no driver.
    "bad-nodriver": (
        {"x/CMakeLists.txt": "deluge_test_directory(TREES host)\n", "x/n_spec.cpp": ""},
        "x/n_spec.cpp",
    ),
    # A driver named only in a comment builds nothing.
    "bad-commentdriver": (
        {
            "x/CMakeLists.txt": "deluge_test_directory(TREES host) # deluge_specs_driver(k_specs TREES host)\n",
            "x/k_spec.cpp": "",
        },
        "x/k_spec.cpp",
    ),
}


def write_tree(root: Path, files: dict[str, str]) -> None:
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


class SpecDiscoveryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls._tmp.name)
        write_tree(cls.root / "fixture", {"CMakeLists.txt": ROOT, **GOOD})
        for base, (files, _) in BAD.items():
            write_tree(cls.root / "fixture" / base, files)
        write_tree(cls.root, STUBS)

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def configure(
        self, tree: str, base: str
    ) -> tuple[subprocess.CompletedProcess[str], Path]:
        """Configures the fixture's <base> as <tree> does, into its own build directory."""
        build = self.root / "build" / f"{tree}-{base}"
        result = subprocess.run(
            [
                "cmake",
                "-S",
                str(self.root / "fixture"),
                "-B",
                str(build),
                "-G",
                "Ninja",
                "-DCMAKE_CXX_COMPILER=clang++",
                f"-DREPO={REPO}",
                f"-DFIXTURE_TREE={tree}",
                f"-DFIXTURE_BASE={base}",
                f"-DFETCHCONTENT_SOURCE_DIR_CPPSPEC={self.root / 'stubs/cppspec'}",
                f"-DFETCHCONTENT_SOURCE_DIR_ARGON={self.root / 'stubs/argon'}",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        return result, build

    def test_each_tree_builds_the_directories_that_name_it(self) -> None:
        for tree, names in TREES.items():
            with self.subTest(tree=tree):
                result, build = self.configure(tree, "unit")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                listing = subprocess.run(
                    ["ctest", "--test-dir", str(build), "-N"],
                    capture_output=True,
                    text=True,
                    check=True,
                ).stdout
                self.assertEqual(
                    sorted(re.findall(r"Test +#\d+: (\S+)", listing)), names
                )

    def test_refuses_a_spec_or_a_directory_no_tree_builds(self) -> None:
        for base, (_, reason) in BAD.items():
            with self.subTest(base=base):
                result, _ = self.configure("host", base)
                self.assertNotEqual(result.returncode, 0, f"{base} configured")
                self.assertIn(reason, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
