# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for render_song.py: which generated song a gate plays, the dry twin it is compared with, and the mod FX
hooks' summary it requires."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts" / "tasks"))

from render_song import _check_mod_fx_hooks, dry_twin, generated_song
from scenario_gate import Run


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


class _Gate:
    """What _check_mod_fx_hooks uses of a gate: its environment and its failures."""

    def __init__(self, env: dict[str, str]):
        self.env = env
        self.failures: list[str] = []

    def fail(self, message: str) -> None:
        self.failures.append(message)


def _failures(env: dict[str, str], *lines: str) -> list[str]:
    gate = _Gate(env)
    _check_mod_fx_hooks(gate, Run(0, "\n".join(lines), ""))
    return gate.failures


SWITCH = {"RENDER_MODFX_SWITCH": "600:chorus,1000:phaser"}
CLEAR = {"RENDER_MODFX_CLEAR": "1000,1800"}
GRAIN_CLEAR = {"RENDER_MODFX_CLEAR": "1300"}


class ModFxHooksTest(unittest.TestCase):
    def test_a_gate_without_hooks_needs_no_summary(self):
        self.assertEqual(_failures({}), [])

    def test_switches_applied_as_asked_pass(self):
        line = "RS-MODFX-SWITCH applied=2 asked=2 types=chorus,phaser ring=1 noring=1"
        self.assertEqual(_failures(SWITCH, line), [])

    def test_a_switch_hook_that_never_ran_fails(self):
        self.assertEqual(len(_failures(SWITCH)), 1)

    def test_a_switch_to_another_type_fails(self):
        line = "RS-MODFX-SWITCH applied=2 asked=2 types=chorus,flanger ring=2 noring=0"
        self.assertEqual(len(_failures(SWITCH, line)), 1)

    def test_a_ring_type_left_without_a_ring_fails(self):
        line = "RS-MODFX-SWITCH applied=2 asked=2 types=chorus,phaser ring=0 noring=2"
        self.assertEqual(len(_failures(SWITCH, line)), 1)

    def test_clears_reaching_both_paths_pass(self):
        line = "RS-MODFX-CLEAR applied=2 asked=2 ring=1 noring=1"
        self.assertEqual(_failures(CLEAR, line), [])

    def test_a_clear_hook_that_never_ran_fails(self):
        self.assertEqual(len(_failures(CLEAR)), 1)

    def test_clears_that_reach_one_path_fail(self):
        line = "RS-MODFX-CLEAR applied=2 asked=2 ring=2 noring=0"
        self.assertEqual(len(_failures(CLEAR, line)), 1)

    def test_a_clear_short_of_those_asked_fails(self):
        line = "RS-MODFX-CLEAR applied=1 asked=2 ring=1 noring=1"
        self.assertEqual(len(_failures(CLEAR, line)), 1)

    def test_clears_of_a_grain_pass(self):
        line = "RS-MODFX-CLEAR applied=1 asked=1 ring=0 noring=0 grain=1"
        self.assertEqual(_failures(GRAIN_CLEAR, line), [])

    def test_a_grain_clear_short_of_those_asked_fails(self):
        line = "RS-MODFX-CLEAR applied=0 asked=1 ring=0 noring=0 grain=0"
        self.assertEqual(len(_failures(GRAIN_CLEAR, line)), 1)

    def test_a_grain_gate_whose_clear_reached_a_comb_type_fails(self):
        line = "RS-MODFX-CLEAR applied=2 asked=2 ring=1 noring=0 grain=1"
        self.assertEqual(len(_failures(CLEAR, line)), 1)


if __name__ == "__main__":
    unittest.main()
