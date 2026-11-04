#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Flag what the UI trees' docs may not hold: history and process notes, and line citations to our own code.

A comment says what the code is and why; how it got there is git's. Two kinds of finding:

- ``history``: a task, review or workstream breadcrumb (``Task 7``, ``fix round 1``, ``F1 task 11``, ``WS4b``,
  ``spec D §7``, ``spec E3``, ``shell spec §15.1`` -- the plan specs live outside the tree) or rename/former narration (``used to``, ``was renamed``).
- ``own-citation``: a ``file:N`` citation whose file is ours -- the port's trees and the guest glue -- which a later
  edit moves out from under it. Cite the symbol instead. Citations into legacy code are ``check_citations.py``'s.

Comments only: ``//`` and ``/* */`` text in C/C++, all of a Markdown file. ``--strict`` exits 1 on any finding.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from functools import cache
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

TREES = (
    "src/scenes",
    "src/shell",
    "src/ui",
    "sim/scenes_spec",
    "tests/unit/spec/shell",
    "tests/unit/spec/ui",
    "tests/unit/spec/gui",
)
# The guest glue: our code that lives inside the legacy tree.
GLUE = (
    "src/deluge/gui/ui/shell_",
    "src/deluge/gui/ui/menu_view_of",
    "src/deluge/util/shell_menu_entry",
)
OURS = ("src/scenes/", "src/shell/", "src/ui/", "src/gfx/", "sim/", "tests/") + GLUE
SUFFIXES = {".h", ".hpp", ".cpp", ".md"}

HISTORY = re.compile(
    r"\b[Tt]ask \d+[a-z]?\b"
    r"|\bfix round\b"
    r"|\b[A-Z]\d+ task\b"
    r"|\bfinal review\b"
    r"|\bWS\d+[a-z]?(?:-\d+)?\b"
    r"|\bspec [A-H](?:\d+)?(?:-[a-z-]+)?\b"
    r"|\b(?:shell|design|migration) spec\b"
    r"|(?<!is )(?<!are )(?<!be )(?<!been )\bused to\b"
    r"|\bwas renamed\b|\brenamed from\b|\bthe old name\b"
)
CITATION = re.compile(
    r"`((?:[A-Za-z0-9_.-]+/)*([A-Za-z0-9_]+\.(?:cpp|h|hpp)))(?::\d+(?:-\d+)?)`"
)


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    kind: str
    text: str


@cache
def _by_basename() -> dict[str, tuple[str, ...]]:
    files = subprocess.run(
        ["git", "ls-files"], cwd=REPO, capture_output=True, text=True, check=True
    ).stdout.split()
    index: dict[str, list[str]] = {}
    for f in files:
        index.setdefault(f.rsplit("/", 1)[-1], []).append(f)
    return {k: tuple(v) for k, v in index.items()}


def _is_ours(cited: str, base: str) -> bool:
    candidates = [c for c in _by_basename().get(base, ()) if c.endswith(cited)]
    return bool(candidates) and all(c.startswith(OURS) for c in candidates)


def _comment_text(line: str, in_block: bool) -> tuple[str, bool]:
    """The comment part of one C/C++ line, and whether a block comment is still open after it."""
    out = []
    i = 0
    quote = None
    while i < len(line):
        if in_block:
            end = line.find("*/", i)
            if end < 0:
                out.append(line[i:])
                return " ".join(out), True
            out.append(line[i:end])
            i = end + 2
            in_block = False
            continue
        ch = line[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif line.startswith("//", i):
            out.append(line[i + 2 :])
            break
        elif line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        i += 1
    return " ".join(out), in_block


def findings_in(path: str, text: str) -> list[Finding]:
    out: list[Finding] = []
    markdown = path.endswith(".md")
    in_block = False
    for n, line in enumerate(text.splitlines(), 1):
        if markdown:
            comment = line
        else:
            comment, in_block = _comment_text(line, in_block)
        if not comment.strip():
            continue
        if HISTORY.search(comment):
            out.append(Finding(path, n, "history", line.strip()))
        for m in CITATION.finditer(comment):
            if _is_ours(m.group(1), m.group(2)):
                out.append(Finding(path, n, "own-citation", m.group(0)))
    return out


def scan(paths: tuple[str, ...]) -> list[Finding]:
    files = subprocess.run(
        ["git", "ls-files", "--", *paths],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    out: list[Finding] = []
    for f in files:
        if Path(f).suffix not in SUFFIXES:
            continue
        out.extend(findings_in(f, (REPO / f).read_text(errors="ignore")))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "paths",
        nargs="*",
        help="files or directories (default: the UI trees and the guest glue)",
    )
    ap.add_argument("--strict", action="store_true", help="exit 1 on any finding")
    ap.add_argument(
        "--summary",
        action="store_true",
        help="counts per directory instead of each finding",
    )
    a = ap.parse_args()
    paths = tuple(a.paths) or TREES + tuple(f"{g}*" for g in GLUE)
    found = scan(paths)
    if a.summary:
        counts: dict[tuple[str, str], int] = {}
        for f in found:
            key = (
                next((t for t in TREES if f.path.startswith(t)), "guest glue"),
                f.kind,
            )
            counts[key] = counts.get(key, 0) + 1
        for (d, kind), n in sorted(counts.items()):
            print(f"{n:6d} {kind:13s} {d}")
    else:
        for f in found:
            print(f"{f.path}:{f.line}: {f.kind}: {f.text}")
    print(f"check_doc_hygiene: {len(found)} finding(s)")
    return 1 if (a.strict and found) else 0


if __name__ == "__main__":
    sys.exit(main())
