# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for render_song.py: which generated song a gate plays."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts" / "tasks"))

from render_song import generated_song


class GeneratedSongTest(unittest.TestCase):
    def test_the_gate_s_own_song_wins_over_a_shell_export(self):
        with mock.patch.dict(os.environ, {"RENDER_GENERATED": "RS_KIT_FX"}):
            self.assertEqual(
                generated_song({"RENDER_GENERATED": "RS_MASTER_FX"}), "RS_MASTER_FX"
            )

    def test_a_gate_without_one_plays_the_default_song(self):
        with mock.patch.dict(os.environ, {"RENDER_GENERATED": "RS_KIT_FX"}):
            self.assertEqual(generated_song({}), "RS_STREAM")


if __name__ == "__main__":
    unittest.main()
