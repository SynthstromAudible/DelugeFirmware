#!/usr/bin/env python3
"""An upgraded file must load to exactly what its original loaded.

The upgrader (tools/deluge-upgrade) writes each song and preset as the firmware's JSON. The round-trip fixtures already
record what the firmware saves after loading each original: tests/golden/songs/fixtures.json and
tests/golden/presets/fixtures.json hash save(load(file)), and tests/golden/render/presets.json records each golden
preset's render. So the upgraded file's round trip must hash, and its render match, exactly what its original's did.

The song corpus and the SYNTHS and KITS folders beside it are kept upgraded (their fixtures keyed by the upgraded,
`.json`, names); DELUGE_LEGACY_CORPUS names the original corpus they were upgraded from, and when it is set each
original is upgraded again and must round-trip to its upgraded fixture's hash.

Cases, in two parts (--part; both by default):
  committed  the committed synthetic songs and the upgrader's legacy fixtures (tests/golden/upgrade/<group>/
             <name>.<kind>.XML, kind synth, kit or song; their hashes in tests/golden/upgrade/fixtures.json). They need
             nothing beyond the tree.
  corpus     each original song in DELUGE_LEGACY_CORPUS and each SYNTHS and KITS preset beside it, and the golden
             preset corpus upgraded and rendered (its factory presets upgraded from their originals in the backup's
             SYNTHS). Asked for on its own without that corpus, it fails at once.
--filter takes a substring of the cases to run. Under `./dbt test --update upgrade-differential` the committed part
re-records the legacy fixtures' hashes from their `<name>.oracle.xml` (the same content in the current form); a
fixture without an oracle keeps its hash, frozen, because the firmware reads only the current forms. To add a case,
commit the fixture and its oracle, then run the update. The corpus part records nothing.

    python3 tests/integration/corpus/upgrade_run.py [--part P] [--filter S] [--no-render]

A whole run (no --filter) also fails when a part ran fewer cases than it owns: the default `--part all` fails without
the legacy corpus (shortfall lines), as does `--part corpus`. A whole corpus run prints UPGRADE-ABSENT <n>, the fixtured
originals the corpus lacks (the fixtures were made from it), and fails when n is not 0. --no-render skips the golden
renders, so the run leaves their cases out of OK.

Prints UPGRADE OK=<n> FAIL=<n> REPORTED=<n>, and UPGRADE-FAIL <case>: <why> per failure; exits 1 on any failure.

Gate upgrade-differential:
    a file the upgrader (tools/deluge-upgrade) writes as JSON must load to exactly what its original loaded: each
    committed synthetic song and legacy fixture (tests/golden/upgrade, hashes in tests/golden/upgrade/fixtures.json),
    upgraded, must round-trip to its fixture hash

Gate upgrade-differential-corpus:
    the upgrader against a real card backup: each original corpus song and SYNTHS/KITS preset in DELUGE_LEGACY_CORPUS,
    upgraded, must round-trip to its fixture hash (the fixtures are keyed by the upgraded .json names:
    DELUGE_SONG_CORPUS names the upgraded corpus), and the golden preset corpus upgraded, its factory presets from their
    originals in the backup's SYNTHS, must render exactly as recorded (through RENDER_PRESET_CORPUS; sample cases
    upgraded against their own sample)
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import driver
import golden_update
import presets_run
import songs_run

ROOT = driver.REPO
TOOL = ROOT / "tools" / "deluge-upgrade"
GOLDEN = driver.GOLDEN / "render" / "presets"
# The legacy fixtures; each one's key is its path under the golden root ("upgrade/<group>/<name>.<kind>.XML").
LEGACY = driver.GOLDEN / "upgrade"
LEGACY_FIXTURES = LEGACY / "fixtures.json"
# The originals of the golden millisecond-zone cases, each beside its sample.
RENDER_ORIGINALS = LEGACY / "render"
# The fewest committed cases a run accepts. Shrinking them is a deliberate change to this number.
MIN_COMMITTED = 39
KINDS = {"synth": "SYNTH", "kit": "KIT", "song": "song"}


def legacy_cases():
    """(key, path, kind) for each legacy fixture; kind SYNTH, KIT or song."""
    out = []
    for path in sorted(LEGACY.rglob("*.XML")):
        kind = path.name.split(".")[-2]
        if kind in KINDS:
            out.append((str(path.relative_to(driver.GOLDEN)), path, KINDS[kind]))
    return out


def shortfalls(
    part, committed_ran, committed_owed, corpus_songs, corpus_presets, corpus_absent=0
):
    """Why a whole run (no --filter) ran fewer cases than its parts own; empty when it ran them all.

    committed_ran is the committed cases that were run, failures included, each once; committed_owed the committed
    cases the tree holds; corpus_songs and corpus_presets the corpus cases found; corpus_absent the fixtured corpus
    originals (songs and presets) the corpus lacks. A part that finds nothing to run must fail rather than pass on zero.
    """
    out = []
    if part in ("all", "committed"):
        if committed_owed == 0:
            out.append("the committed part owns no cases")
        if committed_ran < max(committed_owed, MIN_COMMITTED):
            out.append(
                f"committed part ran {committed_ran} of {max(committed_owed, MIN_COMMITTED)} cases"
            )
    if part in ("all", "corpus"):
        if corpus_songs == 0:
            out.append("corpus part found no songs")
        if corpus_presets == 0:
            out.append("corpus part found no presets")
        if corpus_absent:
            out.append(f"corpus part lacks {corpus_absent} fixtured originals")
    return out


def original(base: Path, key: str) -> Path:
    """The original a corpus fixture's upgraded (`.json`) key was made from."""
    stem = key.removesuffix(".json")
    for suffix in (".XML", ".xml", ""):
        path = base / (stem + suffix)
        if path.is_file():
            return path
    return base / key


def round_trip(binary, path, kind, timeout):
    """(rc, digest, tail) of save(load(path)) for a song or a SYNTH or KIT preset."""
    if kind == "song":
        return songs_run.run_driver([], binary, str(path), timeout)
    return presets_run.run_driver([], binary, str(path), kind, timeout)


def build_tool() -> Path:
    """Build the upgrader and return the binary that build wrote: under CARGO_TARGET_DIR when the runner sets it."""
    subprocess.run(["cargo", "build", "--release", "--quiet"], cwd=TOOL, check=True)
    target = Path(os.environ.get("CARGO_TARGET_DIR", TOOL / "target"))
    return target / "release" / "deluge-upgrade"


def upgrade(tool: Path, source: Path, out: Path, card: Path | None, report: Path):
    """Upgrade one file or directory into out; (ok, stderr)."""
    cmd = [str(tool), str(source), "--out", str(out), "--report", str(report)]
    if card is not None:
        cmd += ["--card", str(card)]
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    return r.returncode == 0, r.stderr


def reported(report: Path) -> list:
    return json.loads(report.read_text()) if report.exists() else []


def oracle_of(path: Path) -> Path:
    """The oracle beside a legacy fixture: the same content in the current form."""
    return path.with_name(path.name.replace(".XML", ".oracle.xml"))


def plan_record(cases, legacy: dict, flt: str):
    """Split the legacy fixtures a recording covers into (record, kept, missing). `cases` is (key, has_oracle) per
    fixture. A fixture with an oracle is recorded from it. One without keeps its committed hash, which is frozen: the
    firmware reads only the current forms, so an older form's hash cannot be re-derived from its original. One
    without an oracle and without a committed hash is a new case, which cannot be recorded and is `missing`."""
    record, kept, missing = [], [], []
    for key, has_oracle in cases:
        if flt not in key:
            continue
        if has_oracle:
            record.append(key)
        elif key in legacy:
            kept.append(key)
        else:
            missing.append(key)
    return record, kept, missing


def record_legacy(binary: str, legacy: dict, args) -> int:
    """Re-record tests/golden/upgrade/fixtures.json: the save(load(oracle)) hash of each legacy fixture that has an
    oracle. A fixture without one keeps its frozen hash; a new fixture needs its oracle (`<name>.oracle.xml`) beside it.
    A round trip that fails records nothing and fails."""
    paths = {key: (path, kind) for key, path, kind in legacy_cases()}
    record, kept, missing = plan_record(
        [(key, oracle_of(path).exists()) for key, (path, _) in paths.items()],
        legacy,
        args.filter,
    )
    for key in missing:
        print(
            f"UPGRADE-FAIL {key}: no committed hash and no {oracle_of(paths[key][0]).name} to record one from"
        )
    if missing:
        return 1
    for key in kept:
        print(f"UPGRADE-KEPT {key} (frozen: no oracle)")
    recorded = dict(legacy)
    failed = 0
    for key in record:
        path, kind = paths[key]
        rc, digest, tail = round_trip(binary, oracle_of(path), kind, args.timeout)
        if digest is None:
            failed += 1
            print(f"UPGRADE-FAIL {key}: rc={rc} {tail}: nothing recorded")
            continue
        recorded[key] = {"kind": kind, "sha256": digest}
    if failed:
        return 1
    golden_update.write_json(
        LEGACY_FIXTURES, recorded, str(LEGACY_FIXTURES.relative_to(ROOT)), indent=2
    )
    print(f"UPGRADE-RECORDED {len(record)} from oracles, {len(kept)} frozen")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="")
    ap.add_argument(
        "--part",
        choices=("all", "committed", "corpus"),
        default="all",
        help="committed: the legacy fixtures and the synthetic songs; corpus: DELUGE_LEGACY_CORPUS's songs and the "
        "presets beside it, and the golden renders",
    )
    ap.add_argument(
        "--no-render", action="store_true", help="skip the golden preset renders"
    )
    ap.add_argument(
        "--render-filter",
        default="",
        help="render only the golden cases holding this (RENDER_PRESET_FILTER)",
    )
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args(argv)

    binary = str(driver.default_binary())
    legacy = json.loads(LEGACY_FIXTURES.read_text()) if LEGACY_FIXTURES.exists() else {}
    if golden_update.updating():
        if args.part == "corpus":
            print(
                "UPGRADE-FAIL the corpus part records nothing: its fixtures are the round-trip gates' own"
            )
            return 1
        return record_legacy(binary, legacy, args)
    corpus = os.environ.get("DELUGE_LEGACY_CORPUS") or ""
    committed = args.part in ("all", "committed")
    from_corpus = args.part in ("all", "corpus")
    if args.part == "corpus" and not (
        corpus and Path(corpus).is_dir() and (Path(corpus).parent / "SYNTHS").is_dir()
    ):
        print(
            "UPGRADE-FAIL legacy corpus: DELUGE_LEGACY_CORPUS must name a card backup's original SONGS, "
            "with its SYNTHS beside it"
        )
        print("UPGRADE OK=0 FAIL=1 REPORTED=0")
        return 1
    tool = build_tool()
    songs = json.loads(Path(songs_run.FIXTURES).read_text())
    presets = json.loads(Path(presets_run.FIXTURES).read_text())

    ok = fail = n_reported = 0
    work = Path(tempfile.mkdtemp(prefix="upgrade_run."))
    try:
        # Songs and corpus presets: the upgraded file's round trip hashes as its original's did.
        cases = []
        legacy_found = corpus_songs = corpus_presets = absent = committed_ran = 0
        for key, path, kind in legacy_cases() if committed else []:
            legacy_found += 1
            if args.filter not in key:
                continue
            committed_ran += 1
            if key not in legacy:
                fail += 1
                print(
                    f"UPGRADE-FAIL {key}: no recorded hash (./dbt test --update upgrade-differential records it)"
                )
                continue
            kind_ = "song" if kind == "song" else ("preset-" + kind)
            cases.append((kind_, key, path, legacy[key]["sha256"], None))
        for key in sorted(songs):
            if args.filter not in key:
                continue
            if key.startswith(songs_run.SYNTHETIC_PREFIX):
                if not committed:
                    continue
                source = Path(songs_run.DATA) / key
            elif corpus and from_corpus:
                source = original(Path(corpus), key)
            else:
                continue
            synthetic = key.startswith(songs_run.SYNTHETIC_PREFIX)
            committed_ran += synthetic
            if not source.is_file():
                if synthetic:
                    fail += 1
                    print(f"UPGRADE-FAIL {key}: the committed song is missing")
                else:
                    absent += 1
            else:
                corpus_songs += not synthetic
                # The round trip loads no samples: upgraded without a card, a zone in milliseconds stays (reported)
                # and saves as the original's does there.
                cases.append(("song", key, source, songs[key], None))
        if corpus and from_corpus:
            beside = Path(corpus).parent
            for key in sorted(presets):
                source = original(beside, key)
                if args.filter not in key:
                    continue
                if source.is_file():
                    corpus_presets += 1
                    cases.append(("preset", key, source, presets[key], None))
                else:
                    absent += 1

        if not args.filter:
            synthetic = sum(k.startswith(songs_run.SYNTHETIC_PREFIX) for k in songs)
            owed = max(legacy_found, len(legacy)) + synthetic
            if from_corpus and corpus:
                print(f"UPGRADE-ABSENT {absent}")
            for why in shortfalls(
                args.part, committed_ran, owed, corpus_songs, corpus_presets, absent
            ):
                fail += 1
                print(f"UPGRADE-FAIL shortfall: {why}")

        def one(case):
            kind, key, source, want, card = case
            out = work / "files" / key
            report = work / "reports" / (key + ".json")
            out.parent.mkdir(parents=True, exist_ok=True)
            report.parent.mkdir(parents=True, exist_ok=True)
            converted, err = upgrade(tool, source, out.parent, card, report)
            upgraded = out.parent / (source.stem + ".json")
            if not converted or not upgraded.is_file():
                return (
                    key,
                    "the upgrader failed: " + err.strip()[-200:],
                    reported(report),
                )
            if kind == "song":
                type_ = "song"
            elif kind.startswith("preset-"):
                type_ = kind.removeprefix("preset-")
            else:
                type_ = dict(presets_run.FOLDERS)[key.split("/")[0]]
            rc, digest, tail = round_trip(binary, upgraded, type_, args.timeout)
            if digest != want:
                return (
                    key,
                    f"rc={rc} round trip {(digest or '-')[:16]} != {want[:16]} ({tail})",
                    reported(report),
                )
            return key, None, reported(report)

        for key, why, findings in driver.map_ordered(one, cases, driver.default_jobs()):
            n_reported += len(findings)
            for f in findings:
                print(f"REPORTED {key}: {f['conversion']}: {f['detail']}")
            if why is None:
                ok += 1
            else:
                fail += 1
                print(f"UPGRADE-FAIL {key}: {why}")

        # Golden presets: the upgraded corpus renders exactly as recorded.
        if (
            from_corpus
            and not args.no_render
            and (not args.filter or "golden" in args.filter)
        ):
            upgraded = work / "golden"
            reissued = 0
            shutil.copytree(
                GOLDEN, upgraded, ignore=shutil.ignore_patterns("*.xml", "*.XML")
            )
            report = work / "golden-report.json"
            converted, err = upgrade(tool, GOLDEN, upgraded, None, report)
            # A case with a sample is staged with it as SAMPLES/WT.WAV: it is upgraded against such a card.
            for preset in sorted(GOLDEN.rglob("*.xml")):
                wav = preset.with_suffix(".wav")
                if not wav.exists() or not converted:
                    continue
                # A card per case by its path: two folders may hold cases of the same name
                card = work / "card" / preset.relative_to(GOLDEN).with_suffix("")
                (card / "SAMPLES").mkdir(parents=True)
                shutil.copy(wav, card / "SAMPLES" / "WT.WAV")
                target = upgraded / preset.parent.relative_to(GOLDEN)
                converted, err = upgrade(tool, preset, target, card, report)
            # The re-issued cases are upgraded again from their originals, over the copies: the millisecond zones
            # against their own sample, the factory presets from the legacy corpus with it as the card.
            for preset in sorted(RENDER_ORIGINALS.glob("*.xml")):
                if not converted:
                    break
                card = work / "card" / ("ms-" + preset.stem)
                (card / "SAMPLES").mkdir(parents=True)
                shutil.copy(preset.with_suffix(".wav"), card / "SAMPLES" / "WT.WAV")
                converted, err = upgrade(
                    tool, preset, upgraded / "upgrade", card, report
                )
                reissued += 1
            factory = sorted((GOLDEN / "factory").glob("*.json"))
            backup = Path(corpus).parent if corpus else None
            if backup is None or not (backup / "SYNTHS").is_dir():
                fail += 1
                print(
                    "UPGRADE-FAIL golden: the factory presets' originals need DELUGE_LEGACY_CORPUS "
                    "(its ../SYNTHS)"
                )
            else:
                for preset in factory:
                    if not converted:
                        break
                    source = backup / "SYNTHS" / (preset.stem + ".XML")
                    if not source.is_file():
                        fail += 1
                        print(
                            f"UPGRADE-FAIL golden: no original for factory/{preset.name}"
                        )
                        continue
                    converted, err = upgrade(
                        tool, source, upgraded / "factory", backup, report
                    )
                    reissued += 1
            print(
                f"UPGRADE-REISSUED {reissued} golden case(s) upgraded from their originals"
            )
            if not converted:
                fail += 1
                print(f"UPGRADE-FAIL golden: the upgrader failed: {err.strip()[-300:]}")
            else:
                env = dict(os.environ, RENDER_PRESET_CORPUS=str(upgraded))
                if args.render_filter:
                    env["RENDER_PRESET_FILTER"] = args.render_filter
                r = subprocess.run(
                    [str(ROOT / "dbt"), "test", "golden-synth"],
                    cwd=ROOT,
                    env=env,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                for line in r.stdout.splitlines() + r.stderr.splitlines():
                    if (
                        "RP-PCM" in line
                        or "RP-CASE" in line
                        or "RP-SILENT" in line
                        or "RP-CASES" in line
                    ):
                        print(f"UPGRADE-RENDER {line.strip()}")
                if r.returncode == 0:
                    ok += 1
                else:
                    fail += 1
                    print(
                        "UPGRADE-FAIL golden: an upgraded golden preset renders differently"
                    )
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print(f"UPGRADE OK={ok} FAIL={fail} REPORTED={n_reported}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
