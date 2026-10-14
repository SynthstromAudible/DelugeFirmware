#!/usr/bin/env python3
"""Drive clip_clone_roundtrip over a song corpus.

Duplicates a real InstrumentClip via InstrumentClip::clone() for each song that has one, asserting the clone is
deep-independent + structurally correct and that the reverse-flatten path runs. This covers the golden-blind
edit path (the load->save golden never clones) that the param_clone unit driver can't reach (clone()
needs a live Song + ModelStack).

The driver is the clip_clone_roundtrip scenario of golden_vt_render (driver.py); `./dbt test clip-clone-roundtrip`
runs this.

  DELUGE_SONG_CORPUS=/path/to/SONGS python3 tests/golden/clip_clone_run.py

Per-song exit codes from the driver: 0=all clone assertions passed, 3=skip (no instrument clip with notes),
1=song didn't load (skip — loader limitation, not a clone defect), 4=assertion failed. A run that ends the
harness before printing LOADED is a load-path defect, not a clone one, and is counted as a load skip.
This runner FAILS only on rc==4 or an end AFTER the load (a real clone regression).
"""

import argparse
import os
import sys

import driver


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=str(driver.default_binary()))
    ap.add_argument("--wrapper", default="")
    ap.add_argument("--corpus", default=driver.default_corpus() or "")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--jobs", type=int, default=driver.default_jobs())
    args = ap.parse_args(argv)
    if not os.path.isdir(args.corpus):
        print("SKIP: no song corpus (DELUGE_SONG_CORPUS)")
        return 77

    songs = driver.find_songs(args.corpus)
    cloned = skip_noclip = skip_load = regressions = 0
    # Direct, so a LOADED printed before a crash is still seen.
    results = driver.run_all(
        args.binary,
        "clip_clone_roundtrip",
        [[song] for song in songs],
        args.timeout,
        args.wrapper,
        args.jobs,
        direct=True,
    )
    for song, r in zip(songs, results):
        loaded = (
            "LOADED" in r.stdout
        )  # printed after a successful load, before any clone
        if r.returncode == 0:
            cloned += 1
        elif r.returncode == 3:
            skip_noclip += 1
        elif r.returncode == 1 or not loaded:
            skip_load += 1
        else:  # failed or ended AFTER load -> a real clone-path regression
            regressions += 1
            print(f"REGRESS rc={r.returncode} {os.path.basename(song)}")
            print(r.stdout.strip())
            print(r.stderr.strip()[-2000:])

    print("=" * 60)
    print(
        f"CLONED={cloned} REGRESS={regressions} "
        f"SKIP_NOCLIP={skip_noclip} SKIP_LOAD={skip_load}  "
        f"(corpus={args.corpus}, {len(songs)} songs)"
    )
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main())
