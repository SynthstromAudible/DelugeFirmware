#!/usr/bin/env python3
"""Differential runner for the AudioClip descriptor edit/round-trip driver (audio_clip_edit_diff).

For each fixture with a committed baseline under audio_clip_edit_baselines/, run the driver and byte-compare
its stdout (minus '#'-prefixed comment lines, which carry the absolute path) against the baseline. The baseline
is captured from HEAD before the owner-4 inversion; the behaviour-preserving flip (make DocAudioClip
authoritative for load/save + derive the engine cache) must reproduce it exactly. Any diff is a regression, and
so is a run that does not exit 0 (the driver also asserts the Doc/engine invariant, to stderr).

The driver is the audio_clip_edit_diff scenario of golden_vt_render (driver.py); `./dbt test audio-clip-edit-diff`
runs this.

Usage:
  DELUGE_SONG_CORPUS=/path/to/SONGS python3 tests/golden/audio_clip_edit_run.py

Exit: 0 = all baselines reproduced; 1 = a regression; 77 = skipped (no corpus / no fixtures).
"""

import os
import sys

import driver

BASELINE_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "audio_clip_edit_baselines"
)


def main(argv=None):
    return driver.baseline_diff(
        "audio_clip_edit_diff",
        BASELINE_DIR,
        argv,
        description=__doc__,
        require_exit_0=True,
    )


if __name__ == "__main__":
    sys.exit(main())
