# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_doc_hygiene.py: the history notes and the line citations to our own code it flags."""

from __future__ import annotations

import unittest

from check_doc_hygiene import findings_in


def kinds(text: str) -> list[str]:
    return [f.kind for f in findings_in("src/scenes/x.cpp", text)]


class HistoryTest(unittest.TestCase):
    def test_flags_task_and_review_breadcrumbs(self):
        for note in (
            "// Added by Task 7 for the press arm.",
            "// fix round 1, Important 2.",
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
        ):
            with self.subTest(note=note):
                self.assertEqual(kinds(note), ["history"])

    def test_flags_rename_and_former_narration(self):
        self.assertEqual(kinds("// The value used to be read here."), ["history"])
        self.assertEqual(
            kinds("// was renamed from gate_last_real_output"), ["history"]
        )

    def test_passes_plain_rationale(self):
        self.assertEqual(
            kinds("// The release of a fed press is fed too, whatever the claim."), []
        )
        self.assertEqual(kinds("// A task the scheduler runs."), [])
        self.assertEqual(kinds("// the spec examples pin it"), [])

    def test_reads_comments_only(self):
        self.assertEqual(kinds('const char* s = "Task 7";\n'), [])


class CitationTest(unittest.TestCase):
    def test_flags_a_line_citation_to_our_own_code(self):
        self.assertEqual(kinds("// see `menu_scene.cpp:123`"), ["own-citation"])
        self.assertEqual(kinds("// (`shell/scene.h:68`)"), ["own-citation"])

    def test_passes_a_legacy_citation(self):
        self.assertEqual(kinds("// `sound_editor.cpp:1078`"), [])
        self.assertEqual(kinds("// `gui/menu_item/submenu.cpp:21-37`"), [])

    def test_passes_a_symbol_citation(self):
        self.assertEqual(kinds("// `MenuWalk::ascend` (`menu/walk.cpp`)"), [])


if __name__ == "__main__":
    unittest.main()
