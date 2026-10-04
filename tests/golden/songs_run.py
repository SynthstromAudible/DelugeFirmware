#!/usr/bin/env python3
"""Song serialization round-trip regression harness (project-model extraction, Phase 0).

For every song, run the `song_roundtrip` driver:

    gen1 = save(load(original))
    gen2 = save(load(gen1))

The driver asserts gen1 == gen2 (the canonical fixed point) and emits gen1. We
hash gen1 (SHA-256) and compare against songs_fixtures.json. A hash change means
the serialized project model changed — a refactor in the extraction must justify
and re-baseline it (--update). Mirrors the audio golden harness conventions.

The driver is the song_roundtrip scenario of golden_vt_render (driver.py), and `./dbt test song-roundtrip` runs
this check (tests/integration/checks/song_roundtrip.py).

CORPUS IS BRING-YOUR-OWN. Only the hashes (songs_fixtures.json) live in the repo;
the song .XML files stay out-of-repo (privacy/licensing). Point the runner at a
corpus via $DELUGE_SONG_CORPUS or --corpus (default: driver.default_corpus()).
When none of the fixtured songs are present locally, the check
SKIPs (exit 77) instead of failing — so CI without the corpus stays green.

Usage:
    ./dbt test -n song-roundtrip                      # build golden_vt_render
    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/songs_run.py                 # check fixtured songs (exit 1 on regression)
    python3 tests/golden/songs_run.py --update        # discover+re-baseline (walks the corpus)
    python3 tests/golden/songs_run.py --filter Bass   # subset (substring match)

ARM-NEON oracle (must agree with x86 on the *same* fixtures): the qemu-arm build of golden_vt_render.
    ./dbt test -n golden-synth-arm
    python3 tests/golden/songs_run.py --binary <its golden_vt_render> \
        --wrapper 'qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9'
"""

import argparse
import hashlib
import json
import os
import shlex
import sys
import tempfile

import driver

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FIXTURES = os.path.join(HERE, "songs_fixtures.json")
# Committed, privacy-clean synthetic songs that exercise rare structures the BYO corpus may lack
# (e.g. a sync-scale clip). Their fixture keys are prefixed "synthetic/" and resolve against HERE,
# not --corpus, so they are ALWAYS present -- giving real coverage even in CI with no corpus.
SYNTHETIC = os.path.join(HERE, "synthetic")
SYNTHETIC_PREFIX = "synthetic/"

# Driver exit codes.
EXIT_SETTLED, EXIT_LOADFAIL, EXIT_USAGE, EXIT_MISMATCH = 0, 1, 2, 3
SKIP_RC = 77  # autotools convention; matched by CTest SKIP_RETURN_CODE


def run_driver(cmd_prefix, binary, song, timeout, dump_stem=None):
    """Run the driver -> (returncode, gen1_sha256_or_None, summary_tail). With dump_stem, a settled gen1 is also kept
    at dump_stem plus the extension of the format it was saved in."""
    # The save is only hashed, so its name says nothing of the format songs are saved in.
    with tempfile.NamedTemporaryFile(suffix=".gen1", delete=False) as tf:
        out_path = tf.name
    try:
        r = driver.run(binary, "song_roundtrip", [song, out_path], timeout, cmd_prefix)
        tail = (r.stderr.strip().splitlines() or [""])[-1]
        digest = None
        if r.returncode == EXIT_SETTLED:
            with open(out_path, "rb") as fh:
                saved = fh.read()
            digest = hashlib.sha256(saved).hexdigest()
            if dump_stem:
                extension = ".json" if saved.lstrip().startswith(b"{") else ".XML"
                with open(dump_stem + extension, "wb") as fh:
                    fh.write(saved)
        return r.returncode, digest, tail
    finally:
        os.unlink(out_path)


def find_songs(corpus):
    out = []
    for root, _, files in os.walk(corpus):
        for n in files:
            if n.lower().endswith(".xml"):
                out.append(os.path.join(root, n))
    return sorted(out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=str(driver.default_binary()))
    ap.add_argument(
        "--wrapper", default="", help="run the binary under this (e.g. qemu-arm ...)"
    )
    ap.add_argument("--corpus", default=driver.default_corpus() or "")
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
    ap.add_argument(
        "--dump", default="", help="(check) keep each settled gen1 in this directory"
    )
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--jobs", type=int, default=driver.default_jobs())
    args = ap.parse_args(argv)

    if not os.path.exists(args.binary):
        sys.exit(f"driver not built: {args.binary}\n  ./dbt test -n song-roundtrip")
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
        paths = [song[1] if isinstance(song, tuple) else song for song in songs]
        results = driver.map_ordered(
            lambda path: run_driver(pre, args.binary, path, args.timeout),
            paths,
            args.jobs,
        )
        for song, (rc, digest, tail) in zip(songs, results):
            if isinstance(song, tuple):  # (prefix, path) -> synthetic, keyed from HERE
                rel = os.path.relpath(song[1], HERE)
            else:
                rel = os.path.relpath(song, args.corpus)
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
    present = []
    for rel, want in items:
        # Synthetic fixtures are committed under tests/golden/synthetic/ (resolved from HERE);
        # everything else is a BYO corpus song resolved against --corpus.
        if rel.startswith(SYNTHETIC_PREFIX):
            song = os.path.join(HERE, rel)
        else:
            song = os.path.join(args.corpus, rel)
        if os.path.isfile(song):
            present.append((rel, want, song))
        else:
            n_absent += 1
    if args.dump:
        os.makedirs(args.dump, exist_ok=True)

    def dump_stem(rel):
        if not args.dump:
            return None
        return os.path.join(args.dump, os.path.splitext(rel)[0].replace("/", "__"))

    results = driver.map_ordered(
        lambda item: run_driver(
            pre, args.binary, item[2], args.timeout, dump_stem(item[0])
        ),
        present,
        args.jobs,
    )
    for (rel, want, _), (rc, digest, tail) in zip(present, results):
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
