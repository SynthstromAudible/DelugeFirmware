#!/usr/bin/env python3
# Copyright © 2025 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Phase 0 guardrail for the libdeluge separation.

The portable Deluge application (``src/deluge/``) must talk to hardware only
through the ``<libdeluge/...>`` boundary, never by including a HAL header
directly. This check fails if any application file includes a forbidden HAL
header unless that file is on the allowlist.

The allowlist (``scripts/platform_boundary_allowlist.txt``) enumerates the files
that still violate the rule today. It may only *shrink*: removing an entry as a
subsystem is moved behind the boundary is the unit of progress; adding one is a
regression and should be rejected in review.

Usage:
    scripts/check_platform_boundary.py            # check, exit 1 on violation
    scripts/check_platform_boundary.py --list     # print current offenders
    scripts/check_platform_boundary.py --update   # rewrite the allowlist to
                                                  # match the current tree
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# The application tree that must respect the boundary.
APP_ROOT = REPO_ROOT / "src" / "deluge"

# Shared application-config headers that live outside src/deluge but are included
# all over the app, so they must stay HAL-free too (otherwise the HAL leaks into
# the app transitively). NOTE: this is a *direct*-include check — it cannot see a
# HAL header pulled in transitively through some other include. The definitive
# guarantee that the app has no transitive HAL dependency is building it with no
# HAL on the include path (the host-sim / standalone-libdeluge target).
EXTRA_APP_FILES = (
    REPO_ROOT / "src" / "definitions.h",
    REPO_ROOT / "src" / "definitions_cxx.hpp",
)

# Forbidden include path prefixes (HAL / SoC vendor code). Extend this as new
# HALs or BSP-implementation directories appear (e.g. "hal/", "bsp/").
FORBIDDEN_INCLUDE_PREFIXES = ("RZA1/",)

ALLOWLIST_PATH = REPO_ROOT / "scripts" / "platform_boundary_allowlist.txt"

SOURCE_SUFFIXES = {".c", ".cpp", ".cc", ".h", ".hpp", ".hh", ".S"}

# Matches:  #include "RZA1/foo.h"   or   #include <RZA1/foo.h>
_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[>"]')


def forbidden_includes(path: Path) -> list[str]:
    """Return the forbidden headers a file includes (empty if none)."""
    found: list[str] = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return found
    for line in text.splitlines():
        m = _INCLUDE_RE.match(line)
        if not m:
            continue
        inc = m.group(1)
        if any(inc.startswith(p) for p in FORBIDDEN_INCLUDE_PREFIXES):
            found.append(inc)
    return found


def current_offenders() -> dict[str, list[str]]:
    """Map of {repo-relative path string: forbidden includes} across the app tree."""
    offenders: dict[str, list[str]] = {}
    candidates = list(APP_ROOT.rglob("*")) + list(EXTRA_APP_FILES)
    for path in sorted(candidates):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        inc = forbidden_includes(path)
        if inc:
            offenders[str(path.relative_to(REPO_ROOT))] = inc
    return offenders


def load_allowlist() -> set[str]:
    allow: set[str] = set()
    if not ALLOWLIST_PATH.exists():
        return allow
    for line in ALLOWLIST_PATH.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            allow.add(line)
    return allow


def write_allowlist(paths: list[str]) -> None:
    header = (
        "# Platform-boundary allowlist (Phase 0 of the libdeluge separation).\n"
        "#\n"
        "# Files under src/deluge/ that still include a HAL header (RZA1/...) directly,\n"
        "# bypassing the libdeluge boundary. This list may only SHRINK. Adding a new\n"
        "# entry should be rejected in review; removing one is the unit of progress.\n"
        "#\n"
        "# See docs/dev/libdeluge_separation_plan.md (Phase 0) and\n"
        "#     scripts/check_platform_boundary.py\n"
        "# Regenerate the *current* offender set (for comparison) with:\n"
        "#   grep -rl 'RZA1/' src/deluge | sort\n"
        "\n"
    )
    ALLOWLIST_PATH.write_text(header + "\n".join(paths) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--list", action="store_true", help="print current offenders and exit"
    )
    parser.add_argument(
        "--update",
        action="store_true",
        help="rewrite the allowlist to the current tree (use only to ratchet down)",
    )
    args = parser.parse_args()

    offenders = current_offenders()
    offender_paths = sorted(offenders)

    if args.list:
        for p in offender_paths:
            print(f"{p}: {', '.join(offenders[p])}")
        return 0

    if args.update:
        write_allowlist(offender_paths)
        print(
            f"Wrote {len(offender_paths)} entries to {ALLOWLIST_PATH.relative_to(REPO_ROOT)}"
        )
        return 0

    allow = load_allowlist()
    current = set(offender_paths)

    # New violations: offending files not on the allowlist.
    new_violations = sorted(current - allow)
    # Stale entries: allowlisted files that no longer offend (progress!).
    stale = sorted(allow - current)

    status = 0

    if new_violations:
        status = 1
        print(
            "ERROR: files bypass the libdeluge boundary and are not allowlisted:",
            file=sys.stderr,
        )
        for p in new_violations:
            print(f"  {p}  ->  {', '.join(offenders[p])}", file=sys.stderr)
        print(
            "\nThe application must include <libdeluge/...>, not a HAL header.\n"
            "If this is unavoidable for now, that is a regression — discuss in review.",
            file=sys.stderr,
        )

    if stale:
        # Not fatal: progress should be celebrated, but the allowlist must be
        # pruned so it keeps ratcheting. Make it loud.
        entry_word = "entry" if len(stale) == 1 else "entries"
        print(
            f"\nNOTE: {len(stale)} allowlist {entry_word} no longer bypass the "
            "boundary. Prune them (or run --update):",
        )
        for p in stale:
            print(f"  {p}")

    if status == 0 and not stale:
        print(
            f"OK: {len(current)} allowlisted boundary exception(s); no new violations."
        )

    return status


if __name__ == "__main__":
    raise SystemExit(main())
