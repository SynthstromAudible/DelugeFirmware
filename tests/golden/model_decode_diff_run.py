#!/usr/bin/env python3
"""Model decode differential runner. Runs the model_decode_diff driver over the committed synthetic songs (each must
load) and, when there is one, the BYO song corpus: load, save, then require the model's decode of the saved song to
equal the records the engine loader leaves in the model. The driver is self-checking (exit 0 = equal, 1 = load error,
3 = a record differs).

The driver is the model_decode_diff scenario of golden_vt_render (driver.py); `./dbt test model-decode-diff` runs this.
"""

import argparse
import os
import sys

import driver

SYNTHETIC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "synthetic")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=str(driver.default_binary()))
    ap.add_argument("--wrapper", default="")
    ap.add_argument("--corpus", default=driver.default_corpus() or "")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--jobs", type=int, default=driver.default_jobs())
    args = ap.parse_args(argv)

    songs = [(song, True) for song in driver.find_songs(SYNTHETIC)]
    if os.path.isdir(args.corpus):
        songs += [(song, False) for song in driver.find_songs(args.corpus)]

    ok = fail = 0
    results = driver.run_all(
        args.binary,
        "model_decode_diff",
        [[song] for song, _ in songs],
        args.timeout,
        args.wrapper,
        args.jobs,
    )
    for (song, required), r in zip(songs, results):
        if r.returncode == 0:
            print(f"OK      {song}")
            ok += 1
        elif r.returncode == 1 and not required:
            print(f"skip(load) {song}")
        else:
            print(r.stderr, end="" if r.stderr.endswith("\n") else "\n")
            print(f"FAIL(rc={r.returncode}) {song}")
            fail += 1
    print("=" * 60)
    print(f"OK={ok} FAIL={fail}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
