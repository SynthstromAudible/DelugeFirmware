#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Ratchet on raw fixed-point arithmetic outside the format types (dsp-next/core/fixed_point.h).

Counts two things per C/C++ file under src/, comments excluded:
- raw sites: calls to q31_ops.h's raw helpers (multiply_32x32_rshift32, add_saturate, signed_saturate, ...), inline
  asm that issues a DSP instruction (smmul, qadd, ssat, vcvt, ...), NEON arithmetic intrinsics (vqdmulhq_s32,
  vmlal_s16, vshrq_n_s32, ...) and Argon's fixed-point methods (MultiplyFixedQMax, ShiftRightAccumulate, ...);
- boundary crossings: from_raw(), .raw() / ->raw() and reinterpret_bits<>, where typed and raw code meet.
fixed_point.h, fixed_point_neon.h and q31_ops.h, which define them, are exempt.

The raw counts are pinned in scripts/fixed_point_raw_allowlist.txt and may only SHRINK: a raw site in a file not on the
list, a count above its entry, and an entry with headroom left (a count below it) all fail; rewrite the list with
--write after moving code onto the typed operations. Boundary crossings are recorded but not gated: while interfaces
still take raw q31, moving a helper call onto the typed operations adds them. Bare int32_t audio arithmetic (`+`, `<<`
on samples) cannot be told apart from other integer code lexically, so reviews cover it, not this check.

Usage:
    scripts/check_fixed_point_raw.py            # check, exit 1 on growth or a stale entry
    scripts/check_fixed_point_raw.py --list     # print every file's counts, exit 0
    scripts/check_fixed_point_raw.py --write    # rewrite the allowlist; refuses to raise a raw count or add a raw file
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import NamedTuple

REPO_ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST_PATH = REPO_ROOT / "scripts" / "fixed_point_raw_allowlist.txt"
SCOPE = ("src",)
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inc"}
EXEMPT = frozenset(
    {
        "src/deluge/dsp-next/core/fixed_point.h",
        "src/deluge/dsp-next/core/fixed_point_neon.h",
        "src/deluge/dsp-next/core/high_word_asm.h",
        "src/deluge/dsp-next/core/q31_ops.h",
    }
)

RAW_HELPERS = re.compile(
    r"(?<![\w.])("
    r"multiply_32x32_rshift32(?:_rounded)?|multiply_(?:accumulate|subtract)_32x32_rshift32(?:_rounded)?|"
    r"q31_mult|q31t_rescale|signed_saturate(?:_operand_unknown)?|add_saturate|subtract_saturate|"
    r"lshift_and_saturate(?:_unknown)?|increase_magnitude_and_saturate|q31_from_float|q31_to_float"
    r")\s*[<(]"
)
# arm_neon.h's integer arithmetic: adds, subtracts, multiplies (the doubling, rounding, widening and accumulating
# forms), shifts and narrows. Loads, stores, lane moves and reinterprets are not arithmetic.
NEON_ARITHMETIC = re.compile(
    r"(?<![\w.])v(?:q?r?d?mulh|(?:qd)?(?:mlal|mlsl|mull)|q?add|q?sub|q?shl|q?r?shr|r?sra|q?movn|q?r?shrn|shll|mla|mls|mul)"
    r"q?(?:_n|_lane)?_[su](?:8|16|32|64)\s*\("
)
ARGON_FIXED_POINT = re.compile(
    r"\.\s*(?:MultiplyDoubleSaturateHigh|MultiplyRoundFixedQMax|MultiplyFixedQMax|MultiplyAddFixedQMax|"
    r"MultiplyDoubleSaturateLong|MultiplyDoubleAddSaturateLong|MultiplyAddLong|ShiftRightAccumulate|ShiftLeftLong|"
    r"ShiftRightNarrow)\s*[<(]"
)
TYPED_BOUNDARY = re.compile(
    r"\bfrom_raw\s*\(|(?:\.|->)\s*raw\s*\(\s*\)|\breinterpret_bits\s*<"
)
# An asm statement and its template: one or more adjacent string literals.
ASM_STATEMENT = re.compile(
    r'\b(?:asm|__asm__|__asm)\s*(?:volatile|__volatile__)?\s*\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)'
)
# The template's escapes (\n\t between instructions) would otherwise hide the next mnemonic's word boundary.
ESCAPE = re.compile(r"\\.")
DSP_INSTRUCTION = re.compile(
    r"\b(?:smmul|smmla|smmls|smull|smlal|umull|qadd|qsub|qdadd|qdsub|ssat|usat|smul|smla|smua|smus|vcvt)\w*"
)

HEADER = """\
# Raw fixed-point arithmetic per file (scripts/check_fixed_point_raw.py), outside dsp-next/core/fixed_point.h and
# q31_ops.h. `raw`: q31_ops.h helper calls, DSP-instruction inline asm, NEON arithmetic intrinsics and Argon's
# fixed-point methods. `boundary`: from_raw()/.raw()/
# reinterpret_bits<> crossings, recorded but not gated.
#
# The raw counts may only SHRINK: a count above its entry, a raw site in a file not on the list, or an entry with
# headroom left fails the check. After moving a file's arithmetic onto fixed_point.h's typed operations, rewrite with
#   scripts/check_fixed_point_raw.py --write
# which refuses to raise a raw count or add a file with raw sites.
#
# <path> raw=<n> boundary=<n>
"""


class Counts(NamedTuple):
    raw: int
    boundary: int


def strip_comments(text: str) -> str:
    """The text with // and /* */ comments blanked (newlines kept), string and character literals left alone."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            end = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, end))
            i = end
        else:
            out.append(c)
            i += 1
    return "".join(out)


def count(text: str) -> Counts:
    """One file's raw sites and boundary crossings, comments excluded."""
    code = strip_comments(text)
    raw = len(RAW_HELPERS.findall(code))
    raw += len(NEON_ARITHMETIC.findall(code)) + len(ARGON_FIXED_POINT.findall(code))
    raw += sum(
        1
        for m in ASM_STATEMENT.finditer(code)
        if DSP_INSTRUCTION.search(ESCAPE.sub(" ", m.group(1)))
    )
    return Counts(raw, len(TYPED_BOUNDARY.findall(code)))


def scan(root: Path = REPO_ROOT) -> dict[str, Counts]:
    """Every in-scope file's counts, keyed by its path relative to @p root; files with neither are left out."""
    counts: dict[str, Counts] = {}
    for scope in SCOPE:
        for path in sorted((root / scope).rglob("*")):
            if not path.is_file() or path.suffix not in SUFFIXES:
                continue
            rel = path.relative_to(root).as_posix()
            if rel in EXEMPT:
                continue
            counted = count(path.read_text(errors="replace"))
            if counted.raw or counted.boundary:
                counts[rel] = counted
    return counts


def load_allowlist(path: Path = ALLOWLIST_PATH) -> dict[str, Counts]:
    """The recorded counts."""
    if not path.exists():
        return {}
    entries: dict[str, Counts] = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, *fields = line.split()
        values = dict(field.split("=", 1) for field in fields)
        entries[name] = Counts(
            int(values.get("raw", 0)), int(values.get("boundary", 0))
        )
    return entries


def check(
    counts: dict[str, Counts], allowlist: dict[str, Counts]
) -> tuple[list[str], list[str]]:
    """The violations, and which of them are stale entries (headroom a --write would take back)."""
    errors, stale = [], []
    for name in sorted(set(counts) | set(allowlist)):
        raw = counts.get(name, Counts(0, 0)).raw
        pinned = allowlist.get(name, Counts(0, 0)).raw
        if raw > pinned:
            where = (
                f"allowlist pins {pinned}"
                if name in allowlist
                else "the file is not on the allowlist"
            )
            errors.append(f"{name}: {raw} raw fixed-point site(s), {where}")
        elif raw < pinned:
            errors.append(
                f"{name}: {raw} raw fixed-point site(s), allowlist still pins {pinned}: run --write"
            )
            stale.append(name)
    return errors, stale


def write_allowlist(
    path: Path,
    counts: dict[str, Counts],
    previous: dict[str, Counts],
    allow_growth: bool,
) -> None:
    """Writes @p counts as the allowlist; refuses a raw count above @p previous unless @p allow_growth."""
    if not allow_growth:
        grown = [
            n for n, c in counts.items() if c.raw > previous.get(n, Counts(0, 0)).raw
        ]
        if grown:
            raise ValueError(
                "would raise the raw count of: " + ", ".join(sorted(grown))
            )
    body = "".join(
        f"{name} raw={c.raw} boundary={c.boundary}\n"
        for name, c in sorted(counts.items())
    )
    path.write_text(HEADER + body)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--list", action="store_true", help="print every file's counts")
    parser.add_argument(
        "--write",
        action="store_true",
        help="rewrite the allowlist from the current counts",
    )
    parser.add_argument(
        "--allow-growth",
        action="store_true",
        help="with --write: allow a new file or a higher raw count",
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
        raw = sum(c.raw for c in counts.values())
        boundary = sum(c.boundary for c in counts.values())
        print(
            f"Wrote {len(counts)} file(s): {raw} raw site(s), {boundary} boundary crossing(s)."
        )
        return 0

    if args.list:
        for name, c in sorted(counts.items()):
            print(f"{c.raw:5d} {c.boundary:5d}  {name}")
        print(
            f"{sum(c.raw for c in counts.values())} raw site(s), {sum(c.boundary for c in counts.values())} "
            f"boundary crossing(s), in {len(counts)} file(s)"
        )
        return 0

    errors, stale = check(counts, allowlist)
    for error in errors:
        print(error, file=sys.stderr)
    if len(errors) > len(stale):
        print(
            "Raw fixed-point arithmetic grew. Write it with dsp-next/core/fixed_point.h's typed operations, or, if a raw "
            "site is unavoidable, justify it in review and rewrite the list with --write --allow-growth.",
            file=sys.stderr,
        )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
