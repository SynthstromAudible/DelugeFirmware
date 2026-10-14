#!/usr/bin/env python3
"""Differential runner for the record-playback driver (param_record_diff).

For each fixture that has a committed baseline under param_record_baselines/, run the driver and byte-compare
its stdout (minus '#'-prefixed comment lines, which carry the absolute path) against the baseline. The baseline
is captured from engine-native recording; a behaviour-preserving change (e.g. M2-D.3 forwarding the recording
node write to the recordLiveValue kernel) must reproduce it exactly. Any diff is a regression.

The driver is the param_record_diff scenario of golden_vt_render (driver.py); `./dbt test param-record-diff` runs
this.

Usage:
  DELUGE_SONG_CORPUS=/path/to/SONGS python3 tests/golden/param_record_run.py

Exit: 0 = all baselines reproduced; 1 = a regression; 77 = skipped (no corpus / no fixtures).
"""

import os
import sys

import driver

BASELINE_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "param_record_baselines"
)


def main(argv=None):
    return driver.baseline_diff(
        "param_record_diff", BASELINE_DIR, argv, description=__doc__
    )


if __name__ == "__main__":
    sys.exit(main())
