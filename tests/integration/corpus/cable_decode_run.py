"""The Deluge instrument's cable decoder against the app's reader, over every corpus song and every synth and kit preset
beside the corpus. The cable_decode driver checks each file itself (exit 0 agree, 1 does not parse, 3 a cable differs)
and prints one line per cable; those lines are fingerprinted per file in tests/golden/cable_decode/fixtures.json, so the
check keeps its meaning once the app reads cables through the decoder.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/integration/corpus/cable_decode_run.py  # check
    ./dbt test --update cable-decode                      # re-record every file's fingerprint

Gate cable-decode:
    the Deluge instrument decodes every song's and preset's patch cables as the app's reader does: the same cables,
    depths, polarities and amounts, in order, older formats included; each file's cables are fingerprinted, so the gate
    holds once the app reads through the decoder
"""

import argparse
import hashlib
import json
import os
import shlex
import sys
from pathlib import Path

import driver
import golden_update
from presets_run import find_presets

REPO = str(driver.REPO)
FIXTURES = os.path.join(driver.GOLDEN, "cable_decode", "fixtures.json")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=str(driver.default_binary()))
    ap.add_argument("--wrapper", default="")
    ap.add_argument("--corpus", default=driver.default_corpus() or "")
    ap.add_argument("--filter", default="")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--jobs", type=int, default=driver.default_jobs())
    args = ap.parse_args(argv)
    if not os.path.isdir(args.corpus):
        print(f"no corpus at {args.corpus!r} (set DELUGE_SONG_CORPUS)")
        return 77
    updating = golden_update.updating()
    pre = shlex.split(args.wrapper)
    root = os.path.dirname(os.path.abspath(args.corpus))
    jobs = [(song, "SONG") for song in driver.find_songs(args.corpus, args.filter)]
    jobs += [
        (path, kind) for key, path, kind in find_presets(root) if args.filter in key
    ]
    if not jobs:
        print("nothing to check")
        return 77
    results = driver.map_ordered(
        lambda job: driver.run(
            args.binary, "cable_decode", [job[0], job[1]], args.timeout, pre
        ),
        jobs,
        args.jobs,
    )
    fixtures = {}
    if os.path.exists(FIXTURES):
        with open(FIXTURES) as fh:
            fixtures = json.load(fh)
    recorded = {}
    ok = fail = skip = 0
    for (path, kind), r in zip(jobs, results):
        rel = os.path.relpath(path, root)
        if r.returncode == 1:
            if updating and rel in fixtures:
                # A fingerprinted file that stops parsing is a reader regression, not a file to step over.
                fail += 1
                print(f"FAIL(no parse) {kind} {rel}: it has a fingerprint")
                continue
            skip += 1
            print(f"skip     {rel}")
            continue
        if r.returncode != 0:
            fail += 1
            tail = "\n           ".join(r.stderr.strip().splitlines()[-3:])
            print(f"FAIL(rc={r.returncode}) {kind} {rel}\n           {tail}")
            continue
        digest = hashlib.sha256(r.stdout.encode()).hexdigest()
        recorded[rel] = digest
        if not updating and fixtures.get(rel) != digest:
            fail += 1
            print(f"CHANGED  {rel}" if rel in fixtures else f"ABSENT   {rel}")
            continue
        ok += 1
    if updating:
        if fail:
            print(f"FAIL: {fail} file(s) failed to decode; nothing recorded")
        elif not ok:
            fail += 1
            print("FAIL: nothing was recorded")
        else:
            # A fingerprinted file this machine lacks keeps its fingerprint.
            golden_update.write_json(
                Path(FIXTURES),
                {**fixtures, **recorded},
                os.path.relpath(FIXTURES, REPO),
                indent=1,
            )
    print("=" * 60)
    print(f"OK={ok} FAIL={fail} SKIP={skip}  (cable_decode, corpus={args.corpus})")
    if not ok and not fail and not updating:
        print("FAIL: nothing was checked")
        return 77
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
