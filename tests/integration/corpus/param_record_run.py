#!/usr/bin/env python3
"""Differential runner for the record-playback driver (param_record_diff).

For each song in SONGS, run the driver and byte-compare its stdout ('#' comment lines, which carry the absolute path,
removed) with its baseline in tests/golden/param_record/: the recorded lanes, their undo and redo, and what a reload
reads back. Any difference is a regression. `./dbt test param-record-diff` runs this; `./dbt test --update
param-record-diff` records the baselines from the run.

Usage:
  DELUGE_SONG_CORPUS=/path/to/SONGS python3 tests/integration/corpus/param_record_run.py

Exit: 0 = all baselines reproduced; 1 = a regression; 77 = skipped (no corpus).

Gate param-record-diff:
    the engine's live automation recording must reproduce its committed baselines (tests/golden/param_record):
    a scripted run of values recorded through setCurrentValueInResponseToUserInput and processCurrentPos into a
    song-global param (undone, redone, saved, reloaded, its latch drifting back) and into clip lanes (synth, kit row,
    MIDI CC, MPE, reversed, latch; each take undone and redone) must print the same nodes byte for byte
"""

import os
import sys

import driver

BASELINE_DIR = os.path.join(driver.GOLDEN, "param_record")
# The corpus songs whose record-playback output is baselined, each as <name>.txt in BASELINE_DIR.
SONGS = [
    "241217 Midi To Deluge Test 9",
    "Bedroom Beat",
    "Glitchy_2",
    "Kitchen Clave",
    "Sampling",
]


def main(argv=None):
    return driver.baseline_diff(
        "param_record_diff",
        BASELINE_DIR,
        SONGS,
        argv,
        description=__doc__,
        require_exit_0=True,
    )


if __name__ == "__main__":
    sys.exit(main())
