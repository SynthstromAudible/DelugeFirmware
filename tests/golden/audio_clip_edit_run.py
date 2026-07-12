#!/usr/bin/env python3
"""Differential runner for the AudioClip descriptor edit/round-trip harness (audio_clip_edit_diff).

For each fixture with a committed baseline under audio_clip_edit_baselines/, run the harness and byte-compare
its stdout (minus '#'-prefixed comment lines, which carry the absolute path) against the baseline. The baseline
is captured from HEAD before the owner-4 inversion; the behaviour-preserving flip (make DocAudioClip
authoritative for load/save + derive the engine cache) must reproduce it exactly. Any diff is a regression.

Usage:
  DELUGE_SONG_CORPUS=/path/to/SONGS python3 tests/golden/audio_clip_edit_run.py --binary build-sim/audio_clip_edit_diff
  # optional: --wrapper 'qemu-arm -L /usr/arm-linux-gnueabihf' to run the ARM binary

Exit: 0 = all baselines reproduced; 1 = a regression; 77 = skipped (no corpus / no fixtures).
"""

import argparse
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASELINE_DIR = os.path.join(HERE, "audio_clip_edit_baselines")


def strip_comments(text):
    return "\n".join(
        line for line in text.splitlines() if not line.startswith("#")
    ).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--wrapper", default="")
    args = ap.parse_args()

    corpus = os.environ.get("DELUGE_SONG_CORPUS")
    if not corpus or not os.path.isdir(corpus):
        print("SKIP: DELUGE_SONG_CORPUS not set to a directory")
        return 77
    if not os.path.isdir(BASELINE_DIR):
        print("SKIP: no baselines directory")
        return 77

    baselines = sorted(f[:-4] for f in os.listdir(BASELINE_DIR) if f.endswith(".txt"))
    if not baselines:
        print("SKIP: no baselines")
        return 77

    wrapper = shlex.split(args.wrapper)
    ok = regress = absent = 0
    for name in baselines:
        song = os.path.join(corpus, name + ".XML")
        if not os.path.isfile(song):
            print(f"ABSENT  {name} (fixture not in corpus)")
            absent += 1
            continue
        with open(os.path.join(BASELINE_DIR, name + ".txt")) as fh:
            expected = fh.read().strip()
        got = subprocess.run(
            wrapper + [args.binary, song], capture_output=True, text=True
        )
        actual = strip_comments(got.stdout)
        if actual == expected and got.returncode == 0:
            print(f"OK      {name}")
            ok += 1
        else:
            print(f"REGRESS {name} (rc={got.returncode})")
            print(f"  expected: {expected!r}")
            print(f"  actual:   {actual!r}")
            if got.stderr.strip():
                print(f"  stderr:   {got.stderr.strip()!r}")
            regress += 1

    print("=" * 60)
    print(
        f"OK={ok} REGRESS={regress} ABSENT={absent}  (corpus={corpus}, {len(baselines)} baselines)"
    )
    return 1 if regress else 0


if __name__ == "__main__":
    sys.exit(main())
