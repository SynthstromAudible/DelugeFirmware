"""The Deluge instrument's codec checks. Runs one of the instrument_codec_* drivers over every corpus song and every
synth and kit preset beside the corpus; the drivers are self-checking (exit 0 = pass, 1 = load error, 3 = a sound
differs), so there is no baseline to diff.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/instrument_codec_run.py              # the encode check
"""

import argparse
import os
import shlex
import sys

import driver
from presets_run import find_presets


def sweep(scenario: str, argv=None, description: str = "") -> int:
    """Run @scenario over every corpus song (SONG) and every preset (SYNTH, KIT): 0 when all pass, 1 on a failure, 77
    with nothing to check. A file that does not load is skipped."""
    ap = argparse.ArgumentParser(description=description)
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
            args.binary, scenario, [job[0], job[1]], args.timeout, pre
        ),
        jobs,
        args.jobs,
    )
    ok = fail = skip = 0
    for (path, kind), r in zip(jobs, results):
        rel = os.path.relpath(path, root)
        if r.returncode == 0:
            ok += 1
        elif r.returncode == 1:
            skip += 1
            print(f"skip     {rel}")
        else:
            fail += 1
            tail = "\n           ".join(r.stderr.strip().splitlines()[-3:])
            print(f"FAIL(rc={r.returncode}) {kind} {rel}\n           {tail}")
    print("=" * 60)
    print(f"OK={ok} FAIL={fail} SKIP={skip}  ({scenario}, corpus={args.corpus})")
    return 1 if fail else 0


def main(argv=None):
    return sweep("instrument_codec_encode", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
