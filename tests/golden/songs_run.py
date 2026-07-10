#!/usr/bin/env python3
"""Song serialization round-trip regression harness (project-model extraction, Phase 0).

For every song, run the `song_roundtrip` driver:

    gen1 = save(load(original))
    gen2 = save(load(gen1))

The driver asserts gen1 == gen2 (the canonical fixed point) and emits gen1. We
hash gen1 (SHA-256) and compare against songs_fixtures.json. A hash change means
the serialized project model changed — a refactor in the extraction must justify
and re-baseline it (--update). Mirrors the audio golden harness conventions.

CORPUS IS BRING-YOUR-OWN. Only the hashes (songs_fixtures.json) live in the repo;
the song .XML files stay out-of-repo (privacy/licensing). Point the runner at a
corpus via $DELUGE_SONG_CORPUS or --corpus (default: tests/golden/songs/, which
is gitignored). When none of the fixtured songs are present locally, the check
SKIPs (exit 77) instead of failing — so CI without the corpus stays green.

Usage:
    cmake -B build-sim -S sim -G Ninja && cmake --build build-sim --target song_roundtrip
    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/songs_run.py                 # check fixtured songs (exit 1 on regression)
    python3 tests/golden/songs_run.py --update        # discover+re-baseline (walks the corpus)
    python3 tests/golden/songs_run.py --filter Bass   # subset (substring match)

ARM-NEON oracle (must agree with x86 on the *same* fixtures):
    cmake -B build-sim-arm -S sim -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/sim/arm-linux-toolchain.cmake
    cmake --build build-sim-arm --target song_roundtrip
    python3 tests/golden/songs_run.py --binary build-sim-arm/song_roundtrip \
        --wrapper 'qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9'
"""

import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CORPUS = os.path.join(HERE, "songs")
FIXTURES = os.path.join(HERE, "songs_fixtures.json")
# Committed, privacy-clean synthetic songs that exercise rare structures the BYO corpus may lack
# (e.g. a sync-scale clip). Their fixture keys are prefixed "synthetic/" and resolve against HERE,
# not --corpus, so they are ALWAYS present -- giving real coverage even in CI with no corpus.
SYNTHETIC = os.path.join(HERE, "synthetic")
SYNTHETIC_PREFIX = "synthetic/"

# Driver exit codes.
EXIT_SETTLED, EXIT_LOADFAIL, EXIT_USAGE, EXIT_MISMATCH = 0, 1, 2, 3
SKIP_RC = 77  # autotools convention; matched by CTest SKIP_RETURN_CODE


def run_driver(cmd_prefix, binary, song, timeout):
    """Run the driver -> (returncode, gen1_sha256_or_None, summary_tail)."""
    with tempfile.NamedTemporaryFile(suffix=".XML", delete=False) as tf:
        out_path = tf.name
    try:
        r = subprocess.run(
            cmd_prefix + [binary, song, out_path], capture_output=True, timeout=timeout
        )
        tail = (r.stderr.decode(errors="replace").strip().splitlines() or [""])[-1]
        digest = None
        if r.returncode == EXIT_SETTLED:
            digest = hashlib.sha256(open(out_path, "rb").read()).hexdigest()
        return r.returncode, digest, tail
    except subprocess.TimeoutExpired:
        return -1, None, "TIMEOUT"
    finally:
        os.unlink(out_path)


def find_songs(corpus):
    out = []
    for root, _, files in os.walk(corpus):
        for n in files:
            if n.lower().endswith(".xml"):
                out.append(os.path.join(root, n))
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--binary", default=os.path.join(REPO, "build-sim", "song_roundtrip")
    )
    ap.add_argument(
        "--wrapper", default="", help="run the binary under this (e.g. qemu-arm ...)"
    )
    ap.add_argument(
        "--corpus", default=os.environ.get("DELUGE_SONG_CORPUS", DEFAULT_CORPUS)
    )
    ap.add_argument(
        "--filter", default="", help="only songs whose path contains this substring"
    )
    ap.add_argument(
        "--exclude",
        default="problem_songs",
        help="(--update) skip corpus paths containing this substring; '' to disable",
    )
    ap.add_argument(
        "--update", action="store_true", help="discover+re-baseline (walks the corpus)"
    )
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        sys.exit(
            f"driver not built: {args.binary}\n  cmake --build build-sim --target song_roundtrip"
        )
    pre = shlex.split(args.wrapper)

    fixtures = {}
    if os.path.exists(FIXTURES):
        with open(FIXTURES) as fh:
            fixtures = json.load(fh)

    # ---- discover / re-baseline: walk the corpus, hash settled songs ----
    if args.update:
        if not os.path.isdir(args.corpus):
            sys.exit(
                f"--update needs a corpus dir; none at {args.corpus} (set $DELUGE_SONG_CORPUS)"
            )
        songs = [
            s
            for s in find_songs(args.corpus)
            if args.filter in s and (not args.exclude or args.exclude not in s)
        ]
        # Always also re-baseline the committed synthetic fixtures, keyed "synthetic/..." and
        # resolved from HERE, so --update (which rewrites the whole file) never drops them.
        songs += [(SYNTHETIC_PREFIX, s) for s in find_songs(SYNTHETIC)]
        out = {}
        n_settled = n_skip = 0
        for song in songs:
            if isinstance(song, tuple):  # (prefix, path) -> synthetic, keyed from HERE
                rel = os.path.relpath(song[1], HERE)
                song = song[1]
            else:
                rel = os.path.relpath(song, args.corpus)
            rc, digest, tail = run_driver(pre, args.binary, song, args.timeout)
            if rc == EXIT_SETTLED:
                out[rel] = digest
                n_settled += 1
                print(f"SETTLED  {rel}  {digest[:16]}")
            else:
                n_skip += 1
                print(f"skip     {rel}  ({tail})")
        with open(FIXTURES, "w") as fh:
            json.dump(dict(sorted(out.items())), fh, indent=2)
            fh.write("\n")
        print("=" * 60)
        print(f"wrote {FIXTURES}: {n_settled} fixtures ({n_skip} skipped)")
        return 0

    # ---- check: iterate the committed contract; verify each present song ----
    if not fixtures:
        print("no fixtures committed; nothing to check")
        return 0
    items = [(rel, h) for rel, h in sorted(fixtures.items()) if args.filter in rel]
    n_ok = n_regress = n_absent = 0
    for rel, want in items:
        # Synthetic fixtures are committed under tests/golden/synthetic/ (resolved from HERE);
        # everything else is a BYO corpus song resolved against --corpus.
        if rel.startswith(SYNTHETIC_PREFIX):
            song = os.path.join(HERE, rel)
        else:
            song = os.path.join(args.corpus, rel)
        if not os.path.isfile(song):
            n_absent += 1
            continue
        rc, digest, tail = run_driver(pre, args.binary, song, args.timeout)
        if rc == EXIT_SETTLED and digest == want:
            n_ok += 1
        else:
            n_regress += 1
            now = digest[:16] if digest else f"rc={rc} ({tail})"
            print(f"REGRESS  {rel}\n           was {want[:16]}  now {now}")

    checked = n_ok + n_regress
    print("=" * 60)
    print(
        f"OK={n_ok} REGRESS={n_regress} ABSENT={n_absent}  "
        f"(corpus={args.corpus}, {len(items)} fixtures)"
    )
    if checked == 0:
        print(
            "no fixtured songs found locally — set $DELUGE_SONG_CORPUS to your songs dir. SKIPPING."
        )
        return SKIP_RC
    return 1 if n_regress else 0


if __name__ == "__main__":
    sys.exit(main())
