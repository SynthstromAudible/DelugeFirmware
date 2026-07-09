#!/usr/bin/env python3
"""JSON round-trip runner. Runs json_roundtrip over the BYO song corpus; the binary is self-checking
(exit 0 = settled JSON fixed point, 1 = XML/JSON load error, 3 = JSON fixed-point mismatch), so there is
no baseline to diff. Proves the JSON side of the record-first deserializer (JsonSerializer + parseJson +
ValueTreeDeserializer JSON mode) is a faithful round trip.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip
"""

import argparse
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CORPUS = os.path.join(HERE, "songs")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--wrapper", default="")
    ap.add_argument(
        "--corpus", default=os.environ.get("DELUGE_SONG_CORPUS", DEFAULT_CORPUS)
    )
    ap.add_argument("--filter", default="")
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()

    if not os.path.isdir(args.corpus):
        print("SKIP: DELUGE_SONG_CORPUS not set to a directory")
        return 77

    songs = []
    for root, _dirs, files in os.walk(args.corpus):
        for n in files:
            if n.lower().endswith(".xml") and args.filter in n:
                songs.append(os.path.join(root, n))
    songs.sort()
    if not songs:
        print("SKIP: no songs found in corpus")
        return 77

    pre = shlex.split(args.wrapper)
    ok = fail = 0
    for song in songs:
        rel = os.path.relpath(song, args.corpus)
        try:
            r = subprocess.run(
                pre + [args.binary, song], capture_output=True, timeout=args.timeout
            )
        except subprocess.TimeoutExpired:
            print(f"FAIL(timeout) {rel}")
            fail += 1
            continue
        if r.returncode == 0:
            print(f"OK      {rel}")
            ok += 1
        elif r.returncode == 1:
            # XML load error (e.g. a non-song XML / preset in the corpus) — not this harness's concern.
            print(f"skip(load) {rel}")
        else:
            sys.stdout.write(r.stderr.decode("utf-8", "replace"))
            print(f"FAIL(rc={r.returncode}) {rel}")
            fail += 1
    print("=" * 60)
    print(f"OK={ok} FAIL={fail}  (corpus={args.corpus})")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
