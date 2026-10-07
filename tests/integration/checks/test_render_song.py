# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for render_song.py: which generated song a gate plays, and the dry twin it is compared with."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts" / "tasks"))

from render_song import dry_twin, generated_song


class GeneratedSongTest(unittest.TestCase):
    def test_the_gate_s_own_song_wins_over_a_shell_export(self):
        with mock.patch.dict(os.environ, {"RENDER_GENERATED": "RS_KIT_FX"}):
            self.assertEqual(
                generated_song({"RENDER_GENERATED": "RS_MASTER_FX"}), "RS_MASTER_FX"
            )

    def test_a_gate_without_one_plays_the_default_song(self):
        with mock.patch.dict(os.environ, {"RENDER_GENERATED": "RS_KIT_FX"}):
            self.assertEqual(generated_song({}), "RS_STREAM")


class DryTwinTest(unittest.TestCase):
    SONGS = frozenset(
        {
            "RS_STREAM",
            "RS_KIT_FX",
            "RS_KIT_DRY",
            "RS_KIT_FILTERS",
            "RS_KIT_FILTERS_DRY",
            "RS_LONELY",
        }
    )

    def test_an_fx_song_is_compared_with_its_dry_song(self):
        self.assertEqual(dry_twin("RS_KIT_FX", self.SONGS), "RS_KIT_DRY")

    def test_any_other_song_is_compared_with_its_dry_suffix(self):
        self.assertEqual(dry_twin("RS_KIT_FILTERS", self.SONGS), "RS_KIT_FILTERS_DRY")

    def test_the_default_song_has_no_twin(self):
        self.assertIsNone(dry_twin("RS_STREAM", self.SONGS))

    def test_a_song_without_a_twin_is_an_error_not_a_skip(self):
        with self.assertRaises(LookupError):
            dry_twin("RS_LONELY", self.SONGS)


if __name__ == "__main__":
    unittest.main()
