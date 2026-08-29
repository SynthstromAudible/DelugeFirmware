#!/usr/bin/env python3
"""Diff (or re-record) a UI test harness snapshot tree (spec G §3).

A snapshot tree holds only two kinds of file, written by `tests/harness/snapshot.{h,cpp}`:
`<name>.oled<N>.pbm` (a P1 PBM per OLED surface) and `<name>.state` (sorted text). Every other
file in the tree is ignored.

  python3 scripts/ui_snapshot_diff.py <got_dir> <golden_dir>          # compare
  python3 scripts/ui_snapshot_diff.py <got_dir> <golden_dir> --list   # list files compared
  GFX_GOLDEN_UPDATE=1 python3 scripts/ui_snapshot_diff.py <got_dir> <golden_dir>  # re-record

Comparison is byte-identical, not parsed-content: a `.pbm`'s per-row XOR summary is printed on
mismatch purely to help a human read the diff, and never affects the pass/fail verdict.

Exit codes: 0 match (or re-recorded), 1 mismatch, 2 usage error or a missing directory.
"""

import argparse
import difflib
import os
import shutil
import sys
from pathlib import Path

SNAPSHOT_SUFFIXES = (".pbm", ".state")


def snapshot_files(root: Path) -> dict[str, Path]:
    """Every `.pbm`/`.state` file under `root`, keyed by its path relative to `root`."""
    if not root.is_dir():
        return {}
    return {
        str(p.relative_to(root)): p
        for p in sorted(root.rglob("*"))
        if p.is_file() and p.suffix in SNAPSHOT_SUFFIXES
    }


def parse_p1_pbm(data: bytes) -> tuple[int, int, list[list[int]]]:
    """Parse a P1 (plain-text) PBM into `(width, height, rows)`, `rows[y][x]` a 0/1 int.

    Raises `ValueError` (with a message meant for a human) if `data` is not a well-formed P1
    PBM -- including if it looks like a P4 (binary) PBM.
    """
    # Strip '#' comments (to end of line) before tokenizing, per the PBM format.
    text = "\n".join(
        line.split("#", 1)[0]
        for line in data.decode("ascii", errors="replace").splitlines()
    )
    tokens = text.split()
    if not tokens or tokens[0] != "P1":
        magic = tokens[0] if tokens else "<empty>"
        raise ValueError(f"not a P1 PBM (magic {magic!r})")
    if len(tokens) < 3:
        raise ValueError("truncated PBM header")
    try:
        width, height = int(tokens[1]), int(tokens[2])
    except ValueError as exc:
        raise ValueError(f"bad PBM dimensions {tokens[1]!r} {tokens[2]!r}") from exc
    bits = tokens[3:]
    if len(bits) != width * height:
        raise ValueError(f"expected {width * height} pixel values, found {len(bits)}")
    rows = []
    for y in range(height):
        row = bits[y * width : (y + 1) * width]
        if any(b not in ("0", "1") for b in row):
            raise ValueError(f"row {y}: non-binary pixel value")
        rows.append([int(b) for b in row])
    return width, height, rows


def pbm_row_diff_summary(golden: bytes, got: bytes) -> list[str]:
    """Per-row XOR summary lines for a mismatched `.pbm` pair, for human eyes only."""
    try:
        gw, gh, grows = parse_p1_pbm(golden)
        ww, wh, wrows = parse_p1_pbm(got)
    except ValueError as exc:
        return [f"    (could not parse PBM for row summary: {exc})"]
    if (gw, gh) != (ww, wh):
        return [
            f"    (size differs: golden {gw}x{gh} vs got {ww}x{wh}, no row summary)"
        ]
    lines = []
    for y in range(gh):
        diff = sum(1 for a, b in zip(grows[y], wrows[y]) if a != b)
        if diff:
            lines.append(f"    row {y}: {diff} pixels differ")
    return lines


def state_diff_lines(golden: bytes, got: bytes, name: str) -> list[str]:
    golden_text = golden.decode("utf-8", errors="replace").splitlines(keepends=True)
    got_text = got.decode("utf-8", errors="replace").splitlines(keepends=True)
    diff = difflib.unified_diff(
        golden_text, got_text, fromfile=f"golden/{name}", tofile=f"got/{name}"
    )
    return [line.rstrip("\n") for line in diff]


def do_update(got_dir: Path, golden_dir: Path, got_files: dict[str, Path]) -> None:
    golden_dir.mkdir(parents=True, exist_ok=True)
    stale = [
        p
        for p in sorted(golden_dir.rglob("*"))
        if p.is_file() and p.suffix in SNAPSHOT_SUFFIXES
    ]
    for p in stale:
        p.unlink()
        print(f"removed {p.relative_to(golden_dir)}")
    for name in sorted(got_files):
        dest = golden_dir / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(got_files[name], dest)
        print(f"wrote {name}")


def do_compare(got_files: dict[str, Path], golden_files: dict[str, Path]) -> bool:
    """Print the diff and return True iff everything matched."""
    ok = True
    missing = sorted(set(golden_files) - set(got_files))
    extra = sorted(set(got_files) - set(golden_files))
    common = sorted(set(got_files) & set(golden_files))

    for name in common:
        golden_bytes = golden_files[name].read_bytes()
        got_bytes = got_files[name].read_bytes()
        if golden_bytes == got_bytes:
            continue
        ok = False
        print(f"MISMATCH: {name}")
        if name.endswith(".state"):
            for line in state_diff_lines(golden_bytes, got_bytes, name):
                print(f"  {line}")
        elif name.endswith(".pbm"):
            for line in pbm_row_diff_summary(golden_bytes, got_bytes):
                print(line)

    for name in missing:
        ok = False
        print(f"missing in got: {name}")
    for name in extra:
        ok = False
        print(f"extra in got: {name}")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("got_dir", type=Path)
    ap.add_argument("golden_dir", type=Path)
    ap.add_argument(
        "--list",
        action="store_true",
        help="print the files that would be compared and exit",
    )
    args = ap.parse_args()

    update = os.environ.get("GFX_GOLDEN_UPDATE") == "1"

    if not args.got_dir.is_dir():
        print(f"error: got directory does not exist: {args.got_dir}", file=sys.stderr)
        return 2
    if not update and not args.golden_dir.is_dir():
        print(
            f"error: golden directory does not exist: {args.golden_dir}",
            file=sys.stderr,
        )
        return 2

    got_files = snapshot_files(args.got_dir)

    if args.list:
        golden_files = snapshot_files(args.golden_dir)
        for name in sorted(set(got_files) | set(golden_files)):
            print(name)
        return 0

    if update:
        do_update(args.got_dir, args.golden_dir, got_files)
        return 0

    golden_files = snapshot_files(args.golden_dir)
    return 0 if do_compare(got_files, golden_files) else 1


if __name__ == "__main__":
    sys.exit(main())
