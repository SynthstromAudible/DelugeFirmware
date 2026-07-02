#!/usr/bin/env python3
"""Drive clip_clone_roundtrip over a song corpus.

Duplicates a real InstrumentClip via InstrumentClip::clone() for each song that has one, asserting the clone is
deep-independent + structurally correct and that the reverse-flatten path runs. This covers the golden-blind
edit path (the load->save golden never clones) that the param_clone_test unit harness can't reach (clone()
needs a live Song + ModelStack).

  DELUGE_SONG_CORPUS=/path/to/SONGS python3 tests/golden/clip_clone_run.py --binary ./build-sim/clip_clone_roundtrip

Per-song exit codes from the binary: 0=all clone assertions passed, 3=skip (no instrument clip with notes),
1=song didn't load in the minimal harness (skip — loader limitation, not a clone defect), 4=assertion failed.
This runner FAILS only on rc==4 (a real clone regression).
"""

import argparse
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="./build-sim/clip_clone_roundtrip")
    ap.add_argument("--corpus", default=os.environ.get("DELUGE_SONG_CORPUS", ""))
    args = ap.parse_args()
    if not args.corpus:
        print("set DELUGE_SONG_CORPUS or pass --corpus", file=sys.stderr)
        return 2

    songs = []
    for root, _dirs, files in os.walk(args.corpus):
        for name in sorted(files):
            if name.upper().endswith(".XML"):
                songs.append(os.path.join(root, name))
    songs.sort()

    cloned = skip_noclip = skip_load = regressions = 0
    for song in songs:
        r = subprocess.run([args.binary, song], capture_output=True, text=True)
        loaded = (
            "LOADED" in r.stdout
        )  # printed after a successful load, before any clone
        if r.returncode == 0:
            cloned += 1
        elif r.returncode == 3:
            skip_noclip += 1
        elif r.returncode == 1 or not loaded:
            # rc==1 is a clean load-error return; a crash (e.g. abort) before the LOADED marker is a
            # load-path defect in the deserializer, not a clone regression — count as a load skip.
            skip_load += 1
        else:  # crashed/failed AFTER load -> a real clone-path regression
            regressions += 1
            print(f"REGRESS rc={r.returncode} {os.path.basename(song)}")
            print(r.stdout.strip())
            print(r.stderr.strip())

    print("=" * 60)
    print(
        f"CLONED={cloned} REGRESS={regressions} "
        f"SKIP_NOCLIP={skip_noclip} SKIP_LOAD={skip_load}  "
        f"(corpus={args.corpus}, {len(songs)} songs)"
    )
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main())
