#!/usr/bin/env python3
"""Enforce the src/gfx include rules (src/gfx/README.md).

gfx may include: its own headers ("gfx/..." or a relative path that resolves inside
src/gfx) and standard headers (<...>). gfx/oled and gfx/grid may not include each other.

Angle-bracket includes are NOT waved through. The firmware's include path carries src/,
include/ and lib/, so `#include <etl/string.h>` or `#include <util/containers.h>` names a
project header just as surely as the quoted spelling does; only headers that resolve
outside those roots (and carry none of the known project prefixes) count as standard.

Probe procedure -- the check is only worth its ctest if it actually fails on a violation.
Introduce each of these into src/gfx, run `python3 scripts/check_gfx_layer.py`, confirm it
exits 1 naming that line, then revert it (the real tree must exit 0):

  1. `#include "model/song/song.h"` in src/gfx/oled/canvas.cpp
     -- an app header by its quoted project-relative path.
  2. `#include "gfx/grid/rgb.h"` in src/gfx/oled/canvas.cpp
     -- a gfx/-prefixed include crossing the oled/grid divide.
  3. `#include "../grid/rgb.h"` in src/gfx/oled/canvas.cpp
     -- the same crossing spelled as a relative path.
  4. `#include <etl/string.h>` in src/gfx/oled/canvas.cpp
     -- a project header (lib/etl) borrowed in the angle-bracket spelling.
"""

import argparse
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]')
SIDES = {"oled", "grid"}

REPO_ROOT = Path(__file__).resolve().parents[1]

# Directories on the firmware's include path. A <...> target that resolves under one of
# these is a project header, not a standard one.
PROJECT_INCLUDE_ROOTS = ("src", "include", "lib")

# Project header prefixes rejected regardless of whether the file is on disk right now:
# vendored libraries reached through their own include/ dir (so not found under the roots
# above), plus the app subtrees gfx must never reach into. No standard header starts with
# any of these.
FORBIDDEN_ANGLE_PREFIXES = (
    "etl/",
    "argon/",
    "libdeluge/",
    "gui/",
    "hid/",
    "model/",
    "storage/",
    "util/",
    "io/",
    "playback/",
    "processing/",
    "definitions",
)


def is_project_header(target: str) -> bool:
    """True if a <target> names something in this repo rather than a standard header."""
    if target.startswith(FORBIDDEN_ANGLE_PREFIXES):
        return True
    return any((REPO_ROOT / root / target).exists() for root in PROJECT_INCLUDE_ROOTS)


def check(root: Path) -> int:
    bad = 0
    root_resolved = root.resolve()
    for path in sorted(root.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".c", ".cpp"}:
            continue
        parts = path.relative_to(root).parts
        # A file directly under src/gfx/ (not inside oled/ or grid/) has no side, so
        # there's nothing for it to cross -- skip the oled/grid rule for it.
        side = parts[0] if parts and parts[0] in SIDES else None
        for lineno, line in enumerate(path.read_text().splitlines(), 1):
            m = INCLUDE.match(line)
            if not m:
                continue
            kind, target = m.groups()
            if kind == "<":
                # Standard headers only. A project header included with angle brackets
                # (the include path carries src/, include/ and lib/) is just as forbidden
                # as the quoted spelling, so it must be rejected here rather than skipped.
                if is_project_header(target):
                    print(f"{path}:{lineno}: forbidden include <{target}>")
                    bad += 1
                continue
            if target.startswith("gfx/"):
                ok = True
                if side is not None:
                    other = "grid" if side == "oled" else "oled"
                    if target.startswith(f"gfx/{other}/"):
                        ok = False
            else:
                resolved = (path.parent / target).resolve()
                # Require the target to actually exist under root: a lexical relative
                # path with no ".." always nests under path.parent (and so under root)
                # whether or not the file is really there, so without this an invented
                # multi-segment path (e.g. "model/song/song.h") would silently pass.
                ok = root_resolved in resolved.parents and resolved.exists()
                if ok and side is not None:
                    # A relative include can also cross oled/grid (e.g. "../grid/rgb.h"
                    # from oled/): derive the *target's* side the same way and compare.
                    target_parts = resolved.relative_to(root_resolved).parts
                    target_side = (
                        target_parts[0]
                        if target_parts and target_parts[0] in SIDES
                        else None
                    )
                    if target_side is not None and target_side != side:
                        ok = False
            if not ok:
                print(f'{path}:{lineno}: forbidden include "{target}"')
                bad += 1
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="src/gfx")
    args = ap.parse_args()
    n = check(Path(args.root))
    if n:
        print(f"{n} forbidden include(s) in {args.root}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
