#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Boundary guardrail for the deluge synth core (synth untangling, Phase 2).

src/deluge/synth/ holds the decoupled per-voice modulation core — Envelope, LFO,
Patcher, ControlBlock, ParamPlane. It is a step below the application: it may use
util/, dsp-next/, definitions*, and its sibling modulation/ headers, but it must
not reach up into the app layers. This check fails if a synth/ file directly
includes a forbidden header (unless that file is on the shrink-only allowlist).

Unlike dsp-next (which forbids modulation/ too), the synth core legitimately uses
modulation/ — the ParamManager-world types (PatchedParamSet, PatchCableSet) and
the param tables it patches over still live there. Forbidden are the app layers
above it: storage/, model/, processing/, gui/, playback/, hid/, io/, the legacy
dsp/ library, and the HAL/BSP (RZA1/, bsp/, drivers/). memory/ is allowed only
via the thin memory_allocator_interface.h seam.

NOTE: this is a *direct*-include check (like the dsp/platform ones) — it cannot
see a forbidden header pulled in transitively through, e.g., util/functions.h or
modulation/params/param_set.h. Cutting those transitive reaches is later work.

Usage:
    scripts/check_synth_boundary.py            # check, exit 1 on violation
    scripts/check_synth_boundary.py --list     # print offenders and exit 0
    scripts/check_synth_boundary.py --update   # rewrite the allowlist to match
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SYNTH_ROOT = REPO_ROOT / "src" / "deluge" / "synth"
ALLOWLIST_PATH = REPO_ROOT / "scripts" / "synth_boundary_allowlist.txt"

# Upward / sideways reaches that break the synth boundary. (modulation/ is NOT
# here — the synth core may use its sibling modulation/ headers.)
FORBIDDEN_INCLUDE_PREFIXES = (
    "storage/",
    "model/",
    "processing/",
    "gui/",
    "playback/",
    "hid/",
    "io/",
    "dsp/",  # the legacy DSP library
    "RZA1/",
    "bsp/",
    "drivers/",
)

# memory/ is forbidden EXCEPT the thin allocator seam.
MEMORY_PREFIX = "memory/"
MEMORY_ALLOWED = {"memory/memory_allocator_interface.h"}

SOURCE_SUFFIXES = {".c", ".cpp", ".cc", ".h", ".hpp", ".hh", ".s", ".S"}

# Matches:  #include "model/foo.h"   or   #include <model/foo.h>
_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[>"]')


def _is_forbidden(inc: str) -> bool:
    if inc.startswith(MEMORY_PREFIX):
        return inc not in MEMORY_ALLOWED
    return any(inc.startswith(p) for p in FORBIDDEN_INCLUDE_PREFIXES)


def forbidden_includes(path: Path) -> list[str]:
    found: list[str] = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return found
    for line in text.splitlines():
        m = _INCLUDE_RE.match(line)
        if m and _is_forbidden(m.group(1)):
            found.append(m.group(1))
    return found


def current_offenders() -> dict[str, list[str]]:
    offenders: dict[str, list[str]] = {}
    if not SYNTH_ROOT.exists():
        return offenders
    for path in sorted(SYNTH_ROOT.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        inc = forbidden_includes(path)
        if inc:
            offenders[str(path.relative_to(REPO_ROOT))] = inc
    return offenders


def load_allowlist() -> set[str]:
    allowed: set[str] = set()
    if not ALLOWLIST_PATH.exists():
        return allowed
    for line in ALLOWLIST_PATH.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            allowed.add(line)
    return allowed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--list", action="store_true", help="print offenders and exit 0"
    )
    parser.add_argument(
        "--update",
        action="store_true",
        help="rewrite the allowlist to the current offender set",
    )
    args = parser.parse_args()

    offenders = current_offenders()

    if args.list:
        for p in sorted(offenders):
            print(f"{p}: {', '.join(offenders[p])}")
        return 0

    if args.update:
        _write_allowlist(sorted(offenders))
        print(
            f"Wrote {len(offenders)} entr(y/ies) to {ALLOWLIST_PATH.relative_to(REPO_ROOT)}."
        )
        return 0

    allowed = load_allowlist()
    violations = {p: inc for p, inc in offenders.items() if p not in allowed}

    if violations:
        print(
            "ERROR: synth core files reach outside the synth boundary:", file=sys.stderr
        )
        for p in sorted(violations):
            print(f"  {p}  ->  {', '.join(violations[p])}", file=sys.stderr)
        print(
            "\nsrc/deluge/synth/ must not include the app layers (storage/model/\n"
            "processing/gui/playback/hid/io), the legacy dsp/ library, or the HAL/BSP.\n"
            "It MAY use util/, dsp-next/, and its sibling modulation/ headers. Finish\n"
            "decoupling the offending include, or — only as a last resort — add the file\n"
            "to scripts/synth_boundary_allowlist.txt (which may only shrink).",
            file=sys.stderr,
        )
        return 1

    # Anything on the allowlist that is now clean should be removed (keep it honest).
    stale = sorted(allowed - set(offenders))
    if stale:
        print(
            "ERROR: stale synth-boundary allowlist entries (now clean — remove them):",
            file=sys.stderr,
        )
        for p in stale:
            print(f"  {p}", file=sys.stderr)
        return 1

    n = sum(
        1 for p in SYNTH_ROOT.rglob("*") if p.suffix in SOURCE_SUFFIXES and p.is_file()
    )
    print(f"OK: {n} synth file(s) scanned; no synth boundary violations.")
    return 0


def _write_allowlist(paths: list[str]) -> None:
    lines = [
        "# Synth-boundary allowlist (synth untangling, Phase 2).",
        "#",
        "# Files under src/deluge/synth/ that still directly include a forbidden app/HAL",
        "# header. This list may only SHRINK — adding an entry should be rejected in",
        "# review; removing one is the unit of progress. Regenerate for comparison with:",
        "#   scripts/check_synth_boundary.py --list",
        "#",
        "# See docs/dev/synth_phase2_data_plane.md and scripts/check_synth_boundary.py.",
        "#",
        "# The list is EMPTY: src/deluge/synth includes only util/, dsp-next/, modulation/,",
        "# definitions*, and standard/etl headers.",
    ]
    lines.extend(paths)
    ALLOWLIST_PATH.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
