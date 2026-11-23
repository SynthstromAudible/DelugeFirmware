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


if __name__ == "__main__":
    unittest.main()
