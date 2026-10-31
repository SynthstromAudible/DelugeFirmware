#!/usr/bin/env python3
"""The audible window's equivalence harness (docs/dev/audible_window_harness.md).

audible_window.py render --out DIR [--filter a,b]   render the goldens and the song set with parameter dumps
audible_window.py songs                              print the song set picked from the corpus
"""

import argparse
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

import render
import songs


def _corpus() -> Path:
    return Path(
        os.environ.get(
            "DELUGE_SONG_CORPUS",
            ROOT / ".superpowers" / "estate" / "corpus-upgraded" / "SONGS",
        )
    )


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
    args = ap.parse_args(argv)

    if args.command == "songs":
        print("\n".join(songs.pick(_corpus())))
        return 0
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
