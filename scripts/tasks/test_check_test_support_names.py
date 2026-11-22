#! /usr/bin/env python3
"""Unit tests for check_test_support_names.py: which support/ files shadow tests/support/.

Run directly with:
    python3 scripts/tasks/test_check_test_support_names.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from check_test_support_names import clashes

SHARED = ["tests/support/spec_main.h", "tests/support/mocks/mock_panic.cpp"]


class TestSupportNamesTest(unittest.TestCase):
    def test_a_per_directory_file_named_like_a_shared_one_clashes(self):
        self.assertEqual(
            clashes(SHARED + ["tests/unit/ui/gfx/support/spec_main.h"]),
            ["tests/unit/ui/gfx/support/spec_main.h shadows tests/support/spec_main.h"],
        )

    def test_a_nested_shared_path_clashes(self):
        self.assertEqual(
            clashes(
                SHARED + ["tests/integration/ui/scenes/support/mocks/mock_panic.cpp"]
            ),
            [
                "tests/integration/ui/scenes/support/mocks/mock_panic.cpp shadows tests/support/mocks/mock_panic.cpp"
            ],
        )

    def test_the_same_name_at_another_depth_does_not_clash(self):
        self.assertEqual(
            clashes(
                SHARED
                + [
                    "tests/unit/ui/widgets/support/mock_panic.cpp",
                    "tests/unit/ui/widgets/support/x/spec_main.h",
                ]
            ),
            [],
        )

    def test_a_file_outside_a_support_folder_does_not_clash(self):
        self.assertEqual(
            clashes(SHARED + ["tests/unit/ui/spec_main.h", "src/support/spec_main.h"]),
            [],
        )

    def test_the_shared_files_themselves_do_not_clash(self):
        self.assertEqual(clashes(SHARED), [])


if __name__ == "__main__":
    unittest.main()
