#!/usr/bin/env python3
"""Differential runner for the AudioClip descriptor edit/round-trip driver (audio_clip_edit_diff).

For each song in SONGS, run the driver and byte-compare its stdout ('#' comment lines, which carry the absolute path,
removed) with its baseline in tests/golden/audio_clip_edit/: each audio clip's descriptor after load, edit and round
trip. Any difference is a regression, and so is a run that does not exit 0 (the driver also checks the document and
the engine agree, to stderr). `./dbt test audio-clip-edit-diff` runs this; `./dbt test --update
audio-clip-edit-diff` records the baselines from the run.

Usage:
  DELUGE_SONG_CORPUS=/path/to/SONGS python3 tests/integration/corpus/audio_clip_edit_run.py

Exit: 0 = all baselines reproduced; 1 = a regression; 77 = skipped (no corpus).

Gate audio-clip-edit-diff:
    an AudioClip's descriptor edits, their undo, and a save and reload must reproduce the committed baseline
    (tests/golden/audio_clip_edit) byte for byte, with each clip's DocAudioClip matching the engine's cache
    after every step
"""

import os
import sys

import driver

BASELINE_DIR = os.path.join(driver.GOLDEN, "audio_clip_edit")
# The corpus songs whose descriptor output is baselined, each as <name>.txt in BASELINE_DIR.
SONGS = ["Sampling"]


def main(argv=None):
    return driver.baseline_diff(
        "audio_clip_edit_diff",
        BASELINE_DIR,
        SONGS,
        argv,
        description=__doc__,
        require_exit_0=True,
    )


if __name__ == "__main__":
    sys.exit(main())
