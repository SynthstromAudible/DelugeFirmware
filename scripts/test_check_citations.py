# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_citations.py's scan scope: a ``--paths`` entry the scan roots do not reach is an error."""

from __future__ import annotations

import unittest

from check_citations import unscanned_paths


class ScanScopeTest(unittest.TestCase):
    def test_the_ui_trees_and_their_unit_specs_are_scanned(self):
        for path in (
            "src/scenes",
            "src/shell",
            "src/ui",
            "sim/scenes_spec",
            "tests/unit/spec/shell",
            "tests/unit/spec/ui",
            "tests/unit/spec/gui",
            "src/deluge/gui/ui/shell_",
            "src/deluge/util/shell_menu_entry",
        ):
            with self.subTest(path=path):
                self.assertEqual(unscanned_paths((path,)), [])

    def test_a_path_outside_every_scan_root_is_reported(self):
        self.assertEqual(unscanned_paths(("nowhere/at_all",)), ["nowhere/at_all"])


if __name__ == "__main__":
    unittest.main()
