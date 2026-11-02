#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Ratchet on raw fixed-point arithmetic outside the format types (dsp-next/core/fixed_point.h).

Counts, per C/C++ file under src/, the places that do Q31 arithmetic on bare integers instead of through
fixed_point.h's typed operations: calls to q31_ops.h's raw helpers (multiply_32x32_rshift32, add_saturate,
signed_saturate, ...), inline asm that issues a DSP instruction (smmul, qadd, ssat, vcvt, ...), and the typed
boundary itself (from_raw(), .raw(), reinterpret_bits<>). fixed_point.h and q31_ops.h, which define them, are exempt.

The counts are pinned in scripts/fixed_point_raw_allowlist.txt, which may only SHRINK: a file not on it, or a count
above its entry, fails. Moving code onto the typed operations is the unit of progress; rewrite the list with --write
afterwards. Bare int32_t audio arithmetic (`+`, `<<` on samples) cannot be told apart from other integer code
lexically, so reviews cover it, not this check.

Usage:
    scripts/check_fixed_point_raw.py            # check, exit 1 on growth
    scripts/check_fixed_point_raw.py --list     # print every file's count and what can shrink, exit 0
    scripts/check_fixed_point_raw.py --write    # rewrite the allowlist; refuses to raise a count or add a file
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST_PATH = REPO_ROOT / "scripts" / "fixed_point_raw_allowlist.txt"
SCOPE = ("src",)
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inc"}
EXEMPT = frozenset(
    {"src/deluge/dsp-next/core/fixed_point.h", "src/deluge/dsp-next/core/q31_ops.h"}
)

RAW_HELPERS = re.compile(
    r"(?<![\w.])("
    r"multiply_32x32_rshift32(?:_rounded)?|multiply_(?:accumulate|subtract)_32x32_rshift32(?:_rounded)?|"
    r"q31_mult|q31t_rescale|signed_saturate(?:_operand_unknown)?|add_saturate|subtract_saturate|"
    r"lshift_and_saturate(?:_unknown)?|increase_magnitude_and_saturate|q31_from_float|q31_to_float"
    r")\s*[<(]"
)
TYPED_BOUNDARY = re.compile(r"\bfrom_raw\s*\(|\.raw\s*\(\s*\)|\breinterpret_bits\s*<")
DSP_ASM = re.compile(
    r"\basm\s*(?:volatile\s*)?\(\s*\"\s*"
    r"(?:smmul|smmla|smmls|smull|smlal|umull|qadd|qsub|qdadd|qdsub|ssat|usat|smul|smla|smua|smus|vcvt)"
)
COMMENTS = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)

HEADER = """\
# Raw fixed-point arithmetic per file: q31_ops.h helper calls, DSP-instruction inline asm and from_raw()/.raw()/
# reinterpret_bits<> outside dsp-next/core/fixed_point.h and q31_ops.h (scripts/check_fixed_point_raw.py).
#
# This list may only SHRINK: a count above its entry, or a file not on it, fails the check. Moving a file's
# arithmetic onto fixed_point.h's typed operations is the unit of progress; afterwards rewrite the list with
#   scripts/check_fixed_point_raw.py --write
# which refuses to raise a count or add a file.
#
# <path> <count>
"""


def count_raw(text: str) -> int:
    """Raw fixed-point sites in one file's text, comments excluded."""
    code = COMMENTS.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    return sum(len(p.findall(code)) for p in (RAW_HELPERS, TYPED_BOUNDARY, DSP_ASM))


def scan(root: Path = REPO_ROOT) -> dict[str, int]:
    """Every in-scope file's count, keyed by its path relative to @p root; files with none are left out."""
    counts: dict[str, int] = {}
    for scope in SCOPE:
        for path in sorted((root / scope).rglob("*")):
            if not path.is_file() or path.suffix not in SUFFIXES:
                continue
            rel = path.relative_to(root).as_posix()
            if rel in EXEMPT:
                continue
            n = count_raw(path.read_text(errors="replace"))
            if n:
                counts[rel] = n
    return counts


def load_allowlist(path: Path = ALLOWLIST_PATH) -> dict[str, int]:
    """The pinned counts."""
    if not path.exists():
        return {}
    entries: dict[str, int] = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            name, count = line.rsplit(maxsplit=1)
            entries[name] = int(count)
    return entries


def check(
    counts: dict[str, int], allowlist: dict[str, int]
) -> tuple[list[str], list[str]]:
    """The violations (a new file, or a count above its entry) and the files whose entry can shrink."""
    errors = []
    for name, n in sorted(counts.items()):
        pinned = allowlist.get(name)
        if pinned is None:
            errors.append(
                f"{name}: {n} raw fixed-point site(s), and the file is not on the allowlist"
            )
        elif n > pinned:
            errors.append(
                f"{name}: {n} raw fixed-point site(s), allowlist pins {pinned}"
            )
    shrinkable = sorted(
        name for name, pinned in allowlist.items() if counts.get(name, 0) < pinned
    )
    return errors, shrinkable


def write_allowlist(
    path: Path, counts: dict[str, int], previous: dict[str, int], allow_growth: bool
) -> None:
    """Writes @p counts as the allowlist; refuses a count above @p previous or a new file unless @p allow_growth."""
    if not allow_growth:
        grown = [n for n, c in counts.items() if c > previous.get(n, 0)]
        if grown:
            raise ValueError("would grow the allowlist: " + ", ".join(sorted(grown)))
    body = "".join(f"{name} {n}\n" for name, n in sorted(counts.items()))
    path.write_text(HEADER + body)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="print every file's count and what can shrink",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="rewrite the allowlist from the current counts",
    )
    parser.add_argument(
        "--allow-growth",
        action="store_true",
        help="with --write: allow new files and higher counts",
    )
    args = parser.parse_args()

    counts = scan()
    allowlist = load_allowlist()
    if args.write:
        try:
            write_allowlist(ALLOWLIST_PATH, counts, allowlist, args.allow_growth)
        except ValueError as error:
            print(f"check_fixed_point_raw: {error}", file=sys.stderr)
            return 1
        print(
            f"Wrote {len(counts)} file(s), {sum(counts.values())} site(s), to {ALLOWLIST_PATH.relative_to(REPO_ROOT)}."
        )
        return 0

    errors, shrinkable = check(counts, allowlist)
    if args.list:
        for name, n in sorted(counts.items()):
            print(f"{n:5d}  {name}")
        print(f"{sum(counts.values())} site(s) in {len(counts)} file(s)")
        for name in shrinkable:
            print(f"can shrink: {name} ({allowlist[name]} -> {counts.get(name, 0)})")
        return 0
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(
            "Raw fixed-point arithmetic grew. Write it with dsp-next/core/fixed_point.h's typed operations, or, if a raw "
            "site is unavoidable, justify it in review and rewrite the list with --write --allow-growth.",
            file=sys.stderr,
        )
        return 1
    if shrinkable:
        print(
            f"check_fixed_point_raw: {len(shrinkable)} allowlist entr(y/ies) can shrink; run --write."
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
