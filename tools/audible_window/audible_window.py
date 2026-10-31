#!/usr/bin/env python3
"""The audible window's equivalence harness (docs/dev/audible_window_harness.md).

audible_window.py render --out DIR [--filter a,b]   render the goldens and the song set with parameter dumps
audible_window.py songs                              print the song set picked from the corpus
audible_window.py compare REF NEW --report FILE      report what moved between two render sets (and FILE.json)
audible_window.py identity [REF]                     the reference compared with itself must show nothing moved

The reference snapshot lives in .superpowers/estate/audible-window/reference/<commit>/; identity takes REF, else
$AUDIBLE_WINDOW_REFERENCE, else the newest snapshot there.
"""

import argparse
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

import compare
import render
import report
import songs

SNAPSHOTS = ROOT / ".superpowers" / "estate" / "audible-window" / "reference"


def _corpus() -> Path:
    return Path(
        os.environ.get(
            "DELUGE_SONG_CORPUS",
            ROOT / ".superpowers" / "estate" / "corpus-upgraded" / "SONGS",
        )
    )


def _reference(given: Path | None) -> Path | None:
    if given is not None:
        return given
    if os.environ.get("AUDIBLE_WINDOW_REFERENCE"):
        return Path(os.environ["AUDIBLE_WINDOW_REFERENCE"])
    snapshots = sorted(
        SNAPSHOTS.glob("*/manifest.json"), key=lambda p: p.stat().st_mtime
    )
    return snapshots[-1].parent if snapshots else None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="command", required=True)
    r = sub.add_parser("render", help="render the comparison set")
    r.add_argument("--out", required=True, type=Path)
    r.add_argument(
        "--filter",
        default="",
        help="comma-separated case-name substrings (default: every case)",
    )
    sub.add_parser("songs", help="print the song set")
    c = sub.add_parser("compare", help="report what moved between two render sets")
    c.add_argument("ref", type=Path)
    c.add_argument("new", type=Path)
    c.add_argument("--report", required=True, type=Path)
    i = sub.add_parser("identity", help="the reference compared with itself")
    i.add_argument("ref", type=Path, nargs="?")
    args = ap.parse_args(argv)

    if args.command == "songs":
        print("\n".join(songs.pick(_corpus())))
        return 0
    if args.command == "compare":
        results = compare.compare_sets(args.ref, args.new)
        report.write(
            results,
            str(args.ref),
            str(args.new),
            args.report,
            args.report.with_suffix(".json"),
        )
        changed = sum(not r.identical for r in results)
        print(
            f"{len(results)} renders, {changed} changed or missing; report in {args.report}"
        )
        return 0
    if args.command == "identity":
        ref = _reference(args.ref)
        if ref is None:
            print(f"no reference snapshot under {SNAPSHOTS}", file=sys.stderr)
            return 1
        results = compare.compare_sets(ref, ref)
        bad = [r.name for r in results if not r.identical]
        print(f"identity of {ref}: {len(results)} renders, {len(bad)} not identical")
        for name in bad:
            print(f"  not identical: {name}")
        return 1 if bad or not results else 0
    manifest = render.render_all(
        ROOT, args.out.resolve(), [n for n in args.filter.split(",") if n], _corpus()
    )
    print(
        f"rendered {len(manifest['cases'])} cases and {len(manifest['songs'])} songs into {args.out}"
    )
    if manifest["failed"]:
        print("failed: " + ", ".join(manifest["failed"]), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
