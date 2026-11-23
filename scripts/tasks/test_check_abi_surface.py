# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Unit tests for check_abi_surface.py: which headers make up the C ABI surface it checks, include/libdeluge/ and each
crate's include/deluge_*.h, and no vendored header.

Run directly with:
    python3 scripts/tasks/test_check_abi_surface.py
"""

from __future__ import annotations

import importlib.util
import re
import shutil
import tempfile
import unittest
from pathlib import Path
from types import ModuleType

CHECKER = Path(__file__).resolve().parent.parent / "check_abi_surface.py"


def load_in(root: Path) -> ModuleType:
    """check_abi_surface imported from a copy at @p root/scripts, so its ROOT is @p root, a tree whose Cargo workspace
    has no members."""
    (root / "Cargo.toml").write_text("[workspace]\nmembers = []\n")
    (root / "scripts").mkdir()
    copy = root / "scripts" / CHECKER.name
    shutil.copy(CHECKER, copy)
    spec = importlib.util.spec_from_file_location("check_abi_surface_under_test", copy)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Headers(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name).resolve()

    def tearDown(self):
        self.tmp.cleanup()

    def plant(self, rel: str) -> Path:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("/* declarations */\n")
        return path

    def headers(self) -> list[Path]:
        return load_in(self.root).HEADERS

    def test_the_platform_headers_are_read(self):
        platform = self.plant("include/libdeluge/clock.h")
        self.assertIn(platform, self.headers())

    def test_each_crate_header_is_read(self):
        alloc = self.plant("src/base/alloc/include/deluge_base_alloc.h")
        reader = self.plant(
            "src/storage/sample/reader/include/deluge_storage_sample_reader.h"
        )
        headers = self.headers()
        self.assertIn(alloc, headers)
        self.assertIn(reader, headers)

    def test_a_vendored_header_is_not_read(self):
        vendored = self.plant(
            "src/storage/sample/convert/third_party/argon/include/deluge_vendored.h"
        )
        self.assertNotIn(vendored, self.headers())

    def test_an_unchecked_crate_header_is_not_read(self):
        resource = self.plant("src/storage/resource/include/deluge_storage_resource.h")
        self.assertNotIn(resource, self.headers())

    def test_a_crate_include_file_not_named_deluge_is_not_read(self):
        other = self.plant("src/storage/stream/include/stream_detail.h")
        self.assertNotIn(other, self.headers())


REPO = CHECKER.parent.parent
CMAKE_LIST = "cmake/deluge_crate_includes.cmake"
BINDGEN = "src/platform/device/build/bindgen.rs"
WRAPPER = "src/platform/device/wrapper.h"
UNBOUND_CRATE_DIR = "src/storage/resource/include"


def crate_include_dirs_on_disk(root: Path) -> set[str]:
    """The include dirs, relative to @p root, of every non-vendored `src/**/include/deluge_*.h`."""
    return {
        header.parent.relative_to(root).as_posix()
        for header in (root / "src").rglob("include/deluge_*.h")
        if "third_party" not in header.parts
    }


def crate_header_names_on_disk(root: Path) -> set[str]:
    return {
        header.name
        for header in (root / "src").rglob("include/deluge_*.h")
        if "third_party" not in header.parts
    }


def crate_list_problems(root: Path) -> list[str]:
    """What disagrees among the crate-header lists, as messages naming each missing or extra entry: the CMake list,
    bindgen's `CRATE_INCLUDE_DIRS` plus the resource dir, wrapper.h's includes, and the headers on disk."""
    disk = crate_include_dirs_on_disk(root)
    cmake = set(
        re.findall(
            r"\$\{DELUGE_REPO_ROOT\}/(\S+)",
            (root / CMAKE_LIST).read_text(),
        )
    )
    rust = (root / BINDGEN).read_text()
    block = re.search(r"CRATE_INCLUDE_DIRS: &\[&str\] = &\[(.*?)\];", rust, re.DOTALL)
    bindgen = set(re.findall(r'"([^"]+)"', block.group(1))) if block else set()
    wrapper = set(
        re.findall(
            r'^#include "(deluge_[^"]+\.h)"', (root / WRAPPER).read_text(), re.MULTILINE
        )
    )
    wrapper_expected = crate_header_names_on_disk(root) - {"deluge_storage_resource.h"}

    problems = []

    def compare(name: str, got: set[str], want: set[str], against: str):
        for entry in sorted(want - got):
            problems.append(f"{name} is missing {entry} ({against})")
        for entry in sorted(got - want):
            problems.append(f"{name} has {entry}, which is not in {against}")

    compare(CMAKE_LIST, cmake, disk, "the src/**/include/deluge_*.h dirs")
    compare(
        "CRATE_INCLUDE_DIRS + the resource dir",
        bindgen | {UNBOUND_CRATE_DIR},
        disk,
        "the src/**/include/deluge_*.h dirs",
    )
    compare(
        WRAPPER,
        wrapper,
        wrapper_expected,
        "the crate headers except deluge_storage_resource.h",
    )
    return problems


class CrateHeaderLists(unittest.TestCase):
    """The three hand-kept lists of crate headers (CMake include dirs, bindgen's include dirs, wrapper.h) agree with
    the headers on disk, so adding a crate header cannot be forgotten in one of them."""

    def test_the_lists_agree_with_the_headers_on_disk(self):
        self.assertEqual([], crate_list_problems(REPO))

    def copy_with(self, rel: str, old: str, new: str) -> Path:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        for name in (CMAKE_LIST, BINDGEN, WRAPPER):
            dest = root / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(REPO / name, dest)
        for header in (REPO / "src").rglob("include/deluge_*.h"):
            dest = root / header.relative_to(REPO)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.touch()
        target = root / rel
        text = target.read_text()
        self.assertIn(old, text)
        target.write_text(text.replace(old, new, 1))
        return root

    def test_a_dir_missing_from_the_cmake_list_is_named(self):
        root = self.copy_with(
            CMAKE_LIST, "${DELUGE_REPO_ROOT}/src/base/alloc/include\n", ""
        )
        self.assertEqual(
            [
                (
                    f"{CMAKE_LIST} is missing src/base/alloc/include "
                    "(the src/**/include/deluge_*.h dirs)"
                )
            ],
            crate_list_problems(root),
        )

    def test_a_dir_missing_from_bindgen_is_named(self):
        root = self.copy_with(BINDGEN, '    "src/storage/stream/include",\n', "")
        self.assertEqual(
            [
                (
                    "CRATE_INCLUDE_DIRS + the resource dir is missing src/storage/stream/include "
                    "(the src/**/include/deluge_*.h dirs)"
                )
            ],
            crate_list_problems(root),
        )

    def test_a_header_missing_from_the_wrapper_is_named(self):
        root = self.copy_with(WRAPPER, '#include "deluge_storage_stream.h"\n', "")
        self.assertEqual(
            [
                (
                    f"{WRAPPER} is missing deluge_storage_stream.h "
                    "(the crate headers except deluge_storage_resource.h)"
                )
            ],
            crate_list_problems(root),
        )

    def test_an_extra_wrapper_include_is_named(self):
        root = self.copy_with(
            WRAPPER,
            '#include "deluge_storage_stream.h"\n',
            '#include "deluge_storage_stream.h"\n#include "deluge_storage_resource.h"\n',
        )
        self.assertEqual(
            [
                (
                    f"{WRAPPER} has deluge_storage_resource.h, which is not in "
                    "the crate headers except deluge_storage_resource.h"
                )
            ],
            crate_list_problems(root),
        )


if __name__ == "__main__":
    unittest.main()
