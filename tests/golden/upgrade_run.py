#!/usr/bin/env python3
"""An upgraded file must load to exactly what its original loaded.

The upgrader (tools/deluge-upgrade) writes each song and preset as the firmware's JSON. The round-trip fixtures already
record what the firmware saves after loading each original: tests/golden/songs_fixtures.json and
tests/golden/presets_fixtures.json hash save(load(file)), and tests/integration/golden/presets.json records each golden
preset's render. So the upgraded file's round trip must hash, and its render match, exactly what its original's did.

Cases: the committed synthetic songs and the golden preset corpus always; the song corpus and the SYNTHS and KITS
folders beside it when DELUGE_SONG_CORPUS names one. --filter takes a substring of the cases to run.

    python3 tests/golden/upgrade_run.py [--filter S] [--no-render]

Prints UPGRADE OK=<n> FAIL=<n> REPORTED=<n>, and UPGRADE-FAIL <case>: <why> per failure; exits 1 on any failure.
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
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

import driver
import presets_run
import songs_run

TOOL = ROOT / "tools" / "deluge-upgrade"
GOLDEN = ROOT / "tests" / "integration" / "golden" / "presets"


def build_tool() -> Path:
    subprocess.run(["cargo", "build", "--release", "--quiet"], cwd=TOOL, check=True)
    return TOOL / "target" / "release" / "deluge-upgrade"


def upgrade(tool: Path, source: Path, out: Path, card: Path | None, report: Path):
    """Upgrade one file or directory into out; (ok, stderr)."""
    cmd = [str(tool), str(source), "--out", str(out), "--report", str(report)]
    if card is not None:
        cmd += ["--card", str(card)]
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    return r.returncode == 0, r.stderr


def reported(report: Path) -> list:
    return json.loads(report.read_text()) if report.exists() else []


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="")
    ap.add_argument(
        "--no-render", action="store_true", help="skip the golden preset renders"
    )
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args(argv)

    tool = build_tool()
    binary = str(driver.default_binary())
    corpus = os.environ.get("DELUGE_SONG_CORPUS") or ""
    songs = json.loads((HERE / "songs_fixtures.json").read_text())
    presets = json.loads((HERE / "presets_fixtures.json").read_text())

    ok = fail = n_reported = 0
    work = Path(tempfile.mkdtemp(prefix="upgrade_run."))
    try:
        # Songs and corpus presets: the upgraded file's round trip hashes as its original's did.
        cases = []
        for key in sorted(songs):
            if args.filter not in key:
                continue
            if key.startswith(songs_run.SYNTHETIC_PREFIX):
                source = HERE / key
            elif corpus:
                source = Path(corpus) / key
            else:
                continue
            if source.is_file():
                card = (
                    None
                    if key.startswith(songs_run.SYNTHETIC_PREFIX)
                    else Path(corpus).parent
                )
                cases.append(("song", key, source, songs[key], card))
        if corpus:
            beside = Path(corpus).parent
            for key in sorted(presets):
                source = beside / key
                if args.filter in key and source.is_file():
                    cases.append(("preset", key, source, presets[key], beside))

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
                rc, digest, tail = songs_run.run_driver(
                    [], binary, str(upgraded), args.timeout
                )
            else:
                folder = key.split("/")[0]
                type_ = dict(presets_run.FOLDERS)[folder]
                rc, digest, tail = presets_run.run_driver(
                    [], binary, str(upgraded), type_, args.timeout
                )
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
        if not args.no_render and (not args.filter or "golden" in args.filter):
            upgraded = work / "golden"
            shutil.copytree(
                GOLDEN, upgraded, ignore=shutil.ignore_patterns("*.xml", "*.XML")
            )
            report = work / "golden-report.json"
            converted, err = upgrade(tool, GOLDEN, upgraded, None, report)
            if not converted:
                fail += 1
                print(f"UPGRADE-FAIL golden: the upgrader failed: {err.strip()[-300:]}")
            else:
                env = dict(os.environ, RENDER_PRESET_CORPUS=str(upgraded))
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
