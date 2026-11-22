# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for compare_strings.py: what a rename may change in an image's strings, and what it must not.

Gate compare-strings-selftest:
    the string comparison of two images equates strings that differ only by the renamed paths and crates, applied in
    once each, and reports a changed message or a changed count
"""

from __future__ import annotations

import unittest

from compare_strings import diff_strings


class DiffStrings(unittest.TestCase):
    def test_equal_after_a_path_rename(self):
        old = ["/r/crates/deluge_alloc/src/lib.rs", "keep me"]
        new = ["/r/src/base/alloc/src/lib.rs", "keep me"]
        self.assertEqual(
            diff_strings(old, new, [("crates/deluge_alloc", "src/base/alloc")]),
            ([], []),
        )

    def test_a_changed_message_is_reported(self):
        self.assertEqual(
            diff_strings(["out of memory"], ["out of memry"], []),
            (["out of memory"], ["out of memry"]),
        )

    def test_counts_matter(self):
        self.assertEqual(
            diff_strings(["a path", "a path"], ["a path"], []), (["a path"], [])
        )

    def test_renames_apply_in_order_once(self):
        renames = [
            ("crates/deluge_sample_fill", "src/storage/sample/fill"),
            ("deluge_sample_fill", "deluge_storage_sample_fill"),
        ]
        self.assertEqual(
            diff_strings(
                ["crates/deluge_sample_fill/src/x.rs deluge_sample_fill::f"],
                ["src/storage/sample/fill/src/x.rs deluge_storage_sample_fill::f"],
                renames,
            ),
            ([], []),
        )


if __name__ == "__main__":
    unittest.main()
