#!/usr/bin/env python3
"""Preset serialization round-trip regression harness.

For every synth and kit preset, run the `preset_roundtrip` driver:

    gen1 = save(load(original))
    gen2 = save(load(gen1))

The driver asserts gen1 == gen2 (the canonical fixed point) and emits gen1. We hash gen1 (SHA-256) and compare against
tests/golden/presets/fixtures.json. A hash change means what a preset saves as changed: justify it and re-record it
(`./dbt test --update preset-roundtrip`).

The presets are the SYNTHS and KITS folders beside the song corpus ($DELUGE_SONG_CORPUS or --corpus; see
driver.default_corpus()). They stay out of the repo; only the hashes live here. With none of the fixtured presets
present the runner exits 77, which fails the preset-roundtrip corpus gate (corpus_gate.py).

Usage:
    ./dbt test -n preset-roundtrip                              # build golden_vt_render
    python3 tests/integration/corpus/presets_run.py             # check (exit 1 on a regression)
    ./dbt test --update preset-roundtrip                        # re-record (builds first)
    python3 tests/integration/corpus/presets_run.py --dump DIR  # also keep each settled gen1 in DIR

Gate preset-roundtrip:
    what a preset saves as must not change unexplained: each fixtured synth and kit preset
    (tests/golden/presets/fixtures.json, from the SYNTHS and KITS folders beside DELUGE_SONG_CORPUS) is loaded, saved as
    a preset is saved, reloaded and saved again; the two saves must be identical and the first must hash to its fixture
    (./dbt test --update preset-roundtrip re-records, with evidence)
"""

import argparse
import hashlib
import json
import os
import shlex
import sys
import tempfile
from pathlib import Path

import driver
import golden_update

REPO = str(driver.REPO)
FIXTURES = os.path.join(driver.GOLDEN, "presets", "fixtures.json")
FOLDERS = (("SYNTHS", "SYNTH"), ("KITS", "KIT"))

EXIT_SETTLED = 0
SKIP_RC = 77


def run_driver(cmd_prefix, binary, preset, kind, timeout, dump_path=None):
    """Run the driver -> (returncode, gen1_sha256_or_None, summary_tail). With dump_path, a settled gen1 is kept."""
    with tempfile.NamedTemporaryFile(suffix=".gen1", delete=False) as tf:
        out_path = tf.name
    try:
        r = driver.run(
            binary, "preset_roundtrip", [preset, kind, out_path], timeout, cmd_prefix
        )
        tail = (r.stderr.strip().splitlines() or [""])[-1]
        digest = None
        if r.returncode == EXIT_SETTLED:
            with open(out_path, "rb") as fh:
                saved = fh.read()
            digest = hashlib.sha256(saved).hexdigest()
            if dump_path:
                with open(dump_path, "wb") as fh:
                    fh.write(saved)
        return r.returncode, digest, tail
    finally:
        os.unlink(out_path)


def find_presets(root):
    """(key, path, kind) for every preset under root's SYNTHS and KITS folders, sorted by key."""
    out = []
    for folder, kind in FOLDERS:
        base = os.path.join(root, folder)
        for dirpath, _, files in os.walk(base):
            for n in files:
                if n.lower().endswith((".xml", ".json")):
                    path = os.path.join(dirpath, n)
                    key = folder + "/" + os.path.relpath(path, base)
                    out.append((key, path, kind))
    return sorted(out)


def record(args, pre, presets, fixtures: dict[str, str]) -> int:
    """Re-record tests/golden/presets/fixtures.json from this run: every preset here that settles gets its hash; a
    fixtured preset this machine lacks keeps its hash; a fixtured preset that no longer settles records nothing and
    fails."""
    if not presets:
        print(
            "FAIL: no presets beside the song corpus to record (set DELUGE_SONG_CORPUS)"
        )
        return 1
    results = driver.map_ordered(
        lambda p: run_driver(pre, args.binary, p[1], p[2], args.timeout),
        presets,
        args.jobs,
    )
    out = dict(fixtures)
    settled = failed = 0
    for (key, _, _), (rc, digest, tail) in zip(presets, results):
        if rc == EXIT_SETTLED:
            out[key] = digest
            settled += 1
        elif key in fixtures:
            failed += 1
            print(
                f"FAIL     {key}  rc={rc} ({tail}): a fixtured preset no longer settles"
            )
        else:
            print(f"skip     {key}  (rc={rc}: {tail})")
    print("=" * 60)
    print(f"SETTLED={settled} FAIL={failed}  (presets beside {args.corpus})")
    if failed:
        return 1
    golden_update.write_json(
        Path(FIXTURES), out, os.path.relpath(FIXTURES, REPO), indent=2
    )
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=str(driver.default_binary()))
    ap.add_argument(
        "--wrapper", default="", help="run the binary under this (e.g. qemu-arm ...)"
    )
    ap.add_argument(
        "--corpus",
        default=driver.default_corpus() or "",
        help="the song corpus; presets are its siblings",
    )
    ap.add_argument(
        "--filter", default="", help="only presets whose key contains this substring"
    )
    ap.add_argument(
        "--dump", default="", help="keep each settled gen1 in this directory"
    )
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--jobs", type=int, default=driver.default_jobs())
    args = ap.parse_args(argv)

    if not os.path.exists(args.binary):
        sys.exit(f"driver not built: {args.binary}\n  ./dbt test -n preset-roundtrip")
    pre = shlex.split(args.wrapper)
    root = os.path.dirname(os.path.abspath(args.corpus)) if args.corpus else ""
    presets = [p for p in find_presets(root) if args.filter in p[0]] if root else []
    if args.dump:
        os.makedirs(args.dump, exist_ok=True)

    def dump_path(key):
        if not args.dump:
            return None
        return os.path.join(
            args.dump, os.path.splitext(key)[0].replace("/", "__") + ".XML"
        )

    fixtures = {}
    if os.path.exists(FIXTURES):
        with open(FIXTURES) as fh:
            fixtures = json.load(fh)

    if golden_update.updating():
        return record(args, pre, presets, fixtures)

    if not fixtures:
        print("no fixtures committed; nothing to check")
        return 0
    by_key = {p[0]: p for p in presets}
    present = [
        (key, want, by_key[key])
        for key, want in sorted(fixtures.items())
        if key in by_key
    ]
    n_absent = sum(1 for key in fixtures if key not in by_key and args.filter in key)
    results = driver.map_ordered(
        lambda item: run_driver(
            pre, args.binary, item[2][1], item[2][2], args.timeout, dump_path(item[0])
        ),
        present,
        args.jobs,
    )
    n_ok = n_regress = 0
    for (key, want, _), (rc, digest, tail) in zip(present, results):
        if rc == EXIT_SETTLED and digest == want:
            n_ok += 1
        else:
            n_regress += 1
            now = digest[:16] if digest else f"rc={rc} ({tail})"
            print(f"REGRESS  {key}\n           was {want[:16]}  now {now}")
    print("=" * 60)
    print(
        f"OK={n_ok} REGRESS={n_regress} ABSENT={n_absent}  (presets beside {args.corpus})"
    )
    if n_ok + n_regress == 0:
        print("no fixtured presets found locally; SKIPPING.")
        return SKIP_RC
    return 1 if n_regress else 0


if __name__ == "__main__":
    sys.exit(main())
