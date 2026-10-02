#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""L0 guardrail for the dsp-next library (synth untangling, Phase 1).

src/deluge/dsp-next/ is the clean L0 DSP library: freestanding primitives that
must not reach upward into the application. This check fails if any dsp-next file
directly includes a forbidden header.

Unlike the platform-boundary check, there is **no allowlist** — dsp-next is
greenfield and clean by construction. A primitive that still needs an app header
is not ready to migrate; finish decoupling it before moving it here. (Primitives
that legitimately cannot be decoupled yet — the sample path — stay in the legacy
src/deluge/dsp/ until Phase 3; they do not come here.)

Forbidden: the application layers (storage/, model/, processing/, gui/,
playback/, modulation/, hid/, io/), the legacy dsp/ library, and the HAL/BSP
(RZA1/, bsp/, drivers/). memory/ is allowed only via the thin
memory_allocator_interface.h seam. util/, definitions*, argon, std, and
dsp-next's own headers are fine.

Usage:
    scripts/check_dsp_boundary.py            # check, exit 1 on violation
    scripts/check_dsp_boundary.py --list     # print offenders and exit 0
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DSP_NEXT_ROOT = REPO_ROOT / "src" / "deluge" / "dsp-next"

# Upward / sideways reaches that make a primitive non-L0.
FORBIDDEN_INCLUDE_PREFIXES = (
    "storage/",
    "model/",
    "processing/",
    "gui/",
    "playback/",
    "modulation/",
    "hid/",
    "io/",
    "dsp/",  # the legacy DSP library — dsp-next must be self-contained
    "RZA1/",
    "bsp/",
    "drivers/",
)

# memory/ is forbidden EXCEPT the thin allocator seam (the arm/thumb boundary
# header that already abstracts the allocator).
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
    if not DSP_NEXT_ROOT.exists():
        return offenders
    for path in sorted(DSP_NEXT_ROOT.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        inc = forbidden_includes(path)
        if inc:
            offenders[str(path.relative_to(REPO_ROOT))] = inc
    return offenders


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--list", action="store_true", help="print offenders and exit 0"
    )
    args = parser.parse_args()

    offenders = current_offenders()

    if args.list:
        for p in sorted(offenders):
            print(f"{p}: {', '.join(offenders[p])}")
        return 0

    if offenders:
        print(
            "ERROR: dsp-next files reach outside the L0 boundary:", file=sys.stderr
        )
        for p in sorted(offenders):
            print(f"  {p}  ->  {', '.join(offenders[p])}", file=sys.stderr)
        print(
            "\ndsp-next is the clean L0 DSP library — it must not include app layers\n"
            "(storage/model/processing/gui/playback/modulation/hid/io), the legacy\n"
            "dsp/ library, or the HAL/BSP. memory/ is allowed only via\n"
            "memory_allocator_interface.h. Finish decoupling the primitive before it\n"
            "moves here; the sample path stays in src/deluge/dsp/ until Phase 3.",
            file=sys.stderr,
        )
        return 1

    n = sum(
        1
        for p in DSP_NEXT_ROOT.rglob("*")
        if p.suffix in SOURCE_SUFFIXES and p.is_file()
    )
    print(f"OK: {n} dsp-next file(s) scanned; no L0 boundary violations.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
