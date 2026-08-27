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


def check(root: Path) -> int:
    bad = 0
    for path in sorted(root.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".c", ".cpp"}:
            continue
        side = "oled" if "oled" in path.relative_to(root).parts else "grid"
        for lineno, line in enumerate(path.read_text().splitlines(), 1):
            m = INCLUDE.match(line)
            if not m:
                continue
            kind, target = m.groups()
            if kind == "<":
                continue  # standard headers only; nothing else in the tree is <...>-included from gfx
            if target.startswith("gfx/"):
                ok = True
                other = "grid" if side == "oled" else "oled"
                if target.startswith(f"gfx/{other}/"):
                    ok = False
            else:
                resolved = (path.parent / target).resolve()
                # Require the target to actually exist under root: a lexical relative
                # path with no ".." always nests under path.parent (and so under root)
                # whether or not the file is really there, so without this an invented
                # multi-segment path (e.g. "model/song/song.h") would silently pass.
                ok = root.resolve() in resolved.parents and resolved.exists()
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
