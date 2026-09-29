#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Boundary guardrail for the deluge synth core (synth untangling, Phase 2).

src/deluge/synth/ holds the decoupled per-voice modulation core — Envelope, LFO,
Patcher, ControlBlock, ParamPlane. It is a step below the application: it may use
util/, dsp-next/, sequence/, definitions*, and its sibling modulation/ headers,
but it must not reach up into the app layers — storage/, model/, processing/,
gui/, playback/, hid/, io/, the legacy dsp/ library, or the HAL/BSP (RZA1/, bsp/,
drivers/). memory/ is allowed only via the thin memory_allocator_interface.h seam.

This is a TRANSITIVE check: it follows the `#include "..."` graph from every
synth/ file (resolving against the same roots the build uses — src/deluge and
src) and fails if any reached header lives under a forbidden layer. That catches
reaches buried behind intermediate headers (e.g. util/functions.h -> gui/colour,
or param_set.h -> storage/), not just direct includes. A synth file may be put on
the shrink-only allowlist as a last resort.

Usage:
    scripts/check_synth_boundary.py            # check, exit 1 on violation
    scripts/check_synth_boundary.py --list     # print offenders (+ a chain) and exit 0
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

# The include roots the build hands the compiler (-I), longest first so a file under
# src/deluge resolves to its src/deluge-relative logical path before its src-relative one.
INCLUDE_ROOTS = (REPO_ROOT / "src" / "deluge", REPO_ROOT / "src")

# Upward / sideways reaches that break the synth boundary. (modulation/, util/, dsp-next/,
# sequence/ are NOT here — the synth core may use them.)
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

# Matches the quoted form only:  #include "model/foo.h"  (system <...> headers are stdlib/etl/argon).
_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def _is_forbidden(logical: str) -> bool:
    if logical.startswith(MEMORY_PREFIX):
        return logical not in MEMORY_ALLOWED
    return any(logical.startswith(p) for p in FORBIDDEN_INCLUDE_PREFIXES)


def _quoted_includes(path: Path) -> list[str]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    return [m.group(1) for line in text.splitlines() if (m := _INCLUDE_RE.match(line))]


def _resolve(inc: str) -> Path | None:
    for root in INCLUDE_ROOTS:
        candidate = root / inc
        if candidate.is_file():
            return candidate.resolve()
    return None


def _logical(path: Path) -> str | None:
    """The path as the boundary sees it (relative to the deepest matching include root)."""
    for root in INCLUDE_ROOTS:
        try:
            return path.relative_to(root.resolve()).as_posix()
        except ValueError:
            continue
    return None


def _reaches(synth_file: Path) -> dict[str, list[str]]:
    """Forbidden logical paths reachable from synth_file -> an example include chain."""
    found: dict[str, list[str]] = {}
    visited: set[Path] = {synth_file.resolve()}
    # queue of (file, chain-of-logical-includes-to-reach-it)
    queue: list[tuple[Path, list[str]]] = [(synth_file, [])]
    while queue:
        current, chain = queue.pop()
        for inc in _quoted_includes(current):
            resolved = _resolve(inc)
            if resolved is None:
                # Unresolved (system-absent / generated). Still flag if the *string* is forbidden
                # (e.g. a stray RZA1/ include with no host-side file).
                if _is_forbidden(inc) and inc not in found:
                    found[inc] = [*chain, inc]
                continue
            logical = _logical(resolved)
            if logical is None:
                continue  # outside the known roots (e.g. fatfs/ via its own -I) — not an app layer
            if _is_forbidden(logical):
                found.setdefault(logical, [*chain, logical])
                continue  # record the breach; don't descend into the app layer
            if resolved not in visited:
                visited.add(resolved)
                queue.append((resolved, [*chain, logical]))
    return found


def current_offenders() -> dict[str, dict[str, list[str]]]:
    offenders: dict[str, dict[str, list[str]]] = {}
    if not SYNTH_ROOT.exists():
        return offenders
    for path in sorted(SYNTH_ROOT.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        reaches = _reaches(path)
        if reaches:
            offenders[str(path.relative_to(REPO_ROOT))] = reaches
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
        "--list", action="store_true", help="print offenders (with a chain) and exit 0"
    )
    parser.add_argument(
        "--update",
        action="store_true",
        help="rewrite the allowlist to the current offender set",
    )
    args = parser.parse_args()

    offenders = current_offenders()

    if args.list:
        for synth_file in sorted(offenders):
            for target, chain in sorted(offenders[synth_file].items()):
                print(f"{synth_file}: {target}   (via {' -> '.join(chain)})")
        return 0

    if args.update:
        _write_allowlist(sorted(offenders))
        print(
            f"Wrote {len(offenders)} entr(y/ies) to {ALLOWLIST_PATH.relative_to(REPO_ROOT)}."
        )
        return 0

    allowed = load_allowlist()
    violations = {p: r for p, r in offenders.items() if p not in allowed}

    if violations:
        print(
            "ERROR: synth core files transitively reach outside the synth boundary:",
            file=sys.stderr,
        )
        for synth_file in sorted(violations):
            for target, chain in sorted(violations[synth_file].items()):
                print(
                    f"  {synth_file}  ->  {target}   (via {' -> '.join(chain)})",
                    file=sys.stderr,
                )
        print(
            "\nsrc/deluge/synth/ must not reach the app layers (storage/model/processing/\n"
            "gui/playback/hid/io), the legacy dsp/ library, or the HAL/BSP — directly OR\n"
            "transitively. Confine the offending include (forward-declare + move it to a\n"
            ".cpp, or split the header), or — only as a last resort — add the synth file to\n"
            "scripts/synth_boundary_allowlist.txt (which may only shrink).",
            file=sys.stderr,
        )
        return 1

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
    print(f"OK: {n} synth file(s) scanned; no synth boundary violations (transitive).")
    return 0


def _write_allowlist(paths: list[str]) -> None:
    lines = [
        "# Synth-boundary allowlist (synth untangling, Phase 2).",
        "#",
        "# Files under src/deluge/synth/ that still TRANSITIVELY reach a forbidden app/HAL",
        "# header. This list may only SHRINK — adding an entry should be rejected in review;",
        "# removing one is the unit of progress. Regenerate for comparison with:",
        "#   scripts/check_synth_boundary.py --list",
        "#",
        "# See docs/dev/synth_phase2_data_plane.md and scripts/check_synth_boundary.py.",
        "#",
        "# The list is EMPTY: src/deluge/synth reaches only util/, dsp-next/, modulation/,",
        "# sequence/, definitions*, and standard/etl headers — no app layer, directly or",
        "# transitively.",
    ]
    lines.extend(paths)
    ALLOWLIST_PATH.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
