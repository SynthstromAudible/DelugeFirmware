#!/usr/bin/env python3
"""Enforce the src/gfx include rules (src/gfx/README.md).

gfx may include: its own headers ("gfx/..." or a relative path that resolves inside
src/gfx) and standard headers (<...>). gfx/oled and gfx/grid may not include each other.
"""

import argparse
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]')
SIDES = {"oled", "grid"}


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
                continue  # standard headers only; nothing else in the tree is <...>-included from gfx
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
