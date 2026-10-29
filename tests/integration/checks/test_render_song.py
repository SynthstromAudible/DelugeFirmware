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

from render_song import (
    _check_mod_fx_hooks,
    _check_reverb_switch,
    dry_twin,
    generated_song,
    song_reverb_model,
)
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
# What an owner holds after a switch: (type, mod FX ring, granular ring), as the hook reports it.
CHORUS = ("chorus", 1, 0)
PHASER = ("phaser", 0, 0)
GRAIN = ("grainFX", 0, 1)


def _switch_lines(*held: tuple[str, int, int]) -> list[str]:
    """The hook's per-owner markers for @p held, then its summary, as the scenario prints them."""
    owners = [
        f"RS-MODFX-SWITCH block={600 * (i + 1)} type={t} ring={r} grain={g} owner=X"
        for i, (t, r, g) in enumerate(held)
    ]
    ring = sum(r for _, r, _ in held)
    grain = sum(g for _, _, g in held)
    types = ",".join(t for t, _, _ in held)
    summary = (
        f"RS-MODFX-SWITCH applied={len(held)} asked={len(held)} types={types} "
        f"ring={ring} noring={len(held) - ring - grain} grain={grain}"
    )
    return [*owners, summary]


CLEAR = {"RENDER_MODFX_CLEAR": "1000,1800"}
GRAIN_CLEAR = {"RENDER_MODFX_CLEAR": "1300"}


class ModFxHooksTest(unittest.TestCase):
    def test_a_gate_without_hooks_needs_no_summary(self):
        self.assertEqual(_failures({}), [])

    def test_switches_applied_as_asked_pass(self):
        self.assertEqual(_failures(SWITCH, *_switch_lines(CHORUS, PHASER)), [])

    def test_switches_naming_their_owner_pass(self):
        env = {"RENDER_MODFX_SWITCH": "500:chorus:RSG,1200:grainFX:RSG"}
        self.assertEqual(_failures(env, *_switch_lines(CHORUS, GRAIN)), [])

    def test_a_master_entering_and_leaving_the_grain_passes(self):
        env = {"RENDER_MODFX_SWITCH": "600:grainFX:master,1800:chorus:master"}
        self.assertEqual(_failures(env, *_switch_lines(GRAIN, CHORUS)), [])

    def test_a_switch_hook_that_never_ran_fails(self):
        self.assertEqual(len(_failures(SWITCH)), 1)

    def test_a_switch_to_another_type_fails(self):
        self.assertEqual(
            len(_failures(SWITCH, *_switch_lines(CHORUS, ("flanger", 1, 0)))), 1
        )

    def test_a_ring_type_left_without_a_ring_fails(self):
        self.assertEqual(
            len(_failures(SWITCH, *_switch_lines(("chorus", 0, 0), PHASER))), 1
        )

    def test_a_grain_left_without_its_ring_fails(self):
        env = {"RENDER_MODFX_SWITCH": "500:chorus:RSG,1200:grainFX:RSG"}
        self.assertEqual(
            len(_failures(env, *_switch_lines(CHORUS, ("grainFX", 0, 0)))), 1
        )

    def test_a_summary_that_does_not_add_up_the_owners_fails(self):
        owners = _switch_lines(CHORUS, PHASER)
        summary = "RS-MODFX-SWITCH applied=2 asked=2 types=chorus,phaser ring=2 noring=0 grain=0"
        self.assertEqual(len(_failures(SWITCH, owners[0], owners[1], summary)), 1)

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


REVERB_SWITCH = {"RENDER_REVERB_SWITCH": "600:model=2,800:hpf=0.2,1000:model=0"}


def _reverb_failures(
    env: dict[str, str], *lines: str, song_model: int | None = 1
) -> list[str]:
    gate = _Gate(env)
    _check_reverb_switch(gate, Run(0, "\n".join(lines), ""), song_model)
    return gate.failures


def _reverb_lines(*switches: tuple[int, str, int]) -> list[str]:
    """The hook's per-switch markers for @p switches (block, setting=value, model after), then its summary."""
    markers = [f"RS-REVERB-SWITCH block={b} set={s} model={m}" for b, s, m in switches]
    models = ",".join(str(m) for _, _, m in switches)
    count = len(switches)
    return [
        *markers,
        f"RS-REVERB-SWITCH applied={count} asked={count} models={models}",
    ]


KNOB_FIRST = {"RENDER_REVERB_SWITCH": "600:room=0.3,800:model=2"}


class ReverbSwitchTest(unittest.TestCase):
    def test_reverb_switch_applied(self):
        lines = [
            "RS-REVERB-SWITCH block=600 set=model=2 model=2",
            "RS-REVERB-SWITCH block=800 set=hpf=0.2 model=2",
            "RS-REVERB-SWITCH block=1000 set=model=0 model=0",
            "RS-REVERB-SWITCH applied=3 asked=3 models=2,2,0",
        ]
        self.assertEqual(_reverb_failures(REVERB_SWITCH, *lines), [])

    def test_reverb_switch_missing(self):
        lines = [
            "RS-REVERB-SWITCH block=600 set=model=2 model=2",
            "RS-REVERB-SWITCH block=800 set=hpf=0.2 model=2",
            "RS-REVERB-SWITCH applied=2 asked=3 models=2,2",
        ]
        failures = _reverb_failures(REVERB_SWITCH, *lines)
        self.assertEqual(len(failures), 1)
        self.assertTrue(failures[0].startswith("RS-REVERB-SWITCH —"))

    def test_a_switch_leaving_another_model_fails(self):
        lines = _reverb_lines(
            (600, "model=2", 2), (800, "hpf=0.2", 2), (1000, "model=0", 1)
        )
        failures = _reverb_failures(REVERB_SWITCH, *lines)
        self.assertEqual(len(failures), 1)
        self.assertTrue(failures[0].startswith("RS-REVERB-SWITCH —"))

    def test_a_knob_that_changed_the_model_fails(self):
        lines = _reverb_lines(
            (600, "model=2", 2), (800, "hpf=0.2", 0), (1000, "model=0", 0)
        )
        self.assertEqual(len(_reverb_failures(REVERB_SWITCH, *lines)), 1)

    def test_an_unknown_setting_fails(self):
        env = {"RENDER_REVERB_SWITCH": "600:model=2,800:size=0.2"}
        lines = _reverb_lines((600, "model=2", 2))
        failures = _reverb_failures(env, *lines)
        self.assertEqual(len(failures), 1)
        self.assertIn("unknown setting size", failures[0])

    def test_a_list_starting_with_a_knob_leaves_the_song_s_model(self):
        lines = _reverb_lines((600, "room=0.3", 1), (800, "model=2", 2))
        self.assertEqual(_reverb_failures(KNOB_FIRST, *lines, song_model=1), [])

    def test_a_first_knob_leaving_another_model_than_the_song_s_fails(self):
        lines = _reverb_lines((600, "room=0.3", 0), (800, "model=2", 2))
        self.assertEqual(len(_reverb_failures(KNOB_FIRST, *lines, song_model=1)), 1)

    def test_a_first_knob_on_a_song_saving_no_model_fails(self):
        lines = _reverb_lines((600, "room=0.3", 0), (800, "model=2", 2))
        self.assertEqual(len(_reverb_failures(KNOB_FIRST, *lines, song_model=None)), 1)

    def test_a_malformed_item_fails_instead_of_raising(self):
        for switches in (
            "600",
            "600:model",
            "x:model=2",
            "600:room=wide",
            "600:model=3",
        ):
            with self.subTest(switches=switches):
                env = {"RENDER_REVERB_SWITCH": switches}
                failures = _reverb_failures(env, *_reverb_lines((600, "model=2", 2)))
                self.assertEqual(len(failures), 1)
                self.assertTrue(failures[0].startswith("RS-REVERB-SWITCH —"))


class SongReverbModelTest(unittest.TestCase):
    def test_the_model_the_song_saves(self):
        xml = '<song>\n\t<reverb model="2" roomSize="0">\n\t</reverb>\n</song>'
        self.assertEqual(song_reverb_model(xml), 2)

    def test_a_song_saving_no_model(self):
        self.assertIsNone(song_reverb_model('<song>\n\t<reverb roomSize="0">\n</song>'))


if __name__ == "__main__":
    unittest.main()
