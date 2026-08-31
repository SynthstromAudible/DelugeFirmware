#!/usr/bin/env python3
"""Warn about legacy line citations that certainly point at nothing.

The UI-redesign layers (`src/ui`, `src/scenes`, and their specs) document nearly every decision
with a citation into the legacy tree -- `gui/menu_item/gate/selection.h:43-54 selectButtonPress`.
Those citations **rot**, and the rot is not hypothetical: E2 task 11b's own two-line edit to
`gui/ui/menus.cpp` shifted every line below `:1647` by two and broke about fifty citations from
five earlier tasks that had been exactly correct when written. That is a regression a script
catches for free and a convention does not.

WHAT THIS CHECKS, AND WHAT IT CANNOT
------------------------------------
It checks the one thing about a citation that is both mechanical and sound: whether the **whole**
cited range is something no claim could possibly refer to. Three cases, all from E2 task 13's
audit, which is where the predicate and its measurements come from:

  * the range starts past the file's end;
  * the range lies entirely inside the file's leading GPL header block;
  * the range is nothing but `#include` / `#pragma` / `namespace` / brace / blank lines.

It does **not** check that the cited line *means* what the comment claims -- that is not
mechanizable -- and it is blind to the commonest drift of all: a citation four lines low still
lands on plausible code. Task 9 found 176 of exactly that shape by reading, and no predicate would
have. So a clean run here is not a clean bill of health, which is the whole reason this is
**warning-only**: it always exits 0, it is a net under one specific regression, and the convention
that a citation names its SYMBOL as well as its line (`selection.h:43-54 selectButtonPress`) stays
on top of it as the thing that actually survives a line shift.

  scripts/check_citations.py             # warn (always exit 0)
  scripts/check_citations.py --strict    # exit 1 on any unexcepted hit, for a local sweep
  scripts/check_citations.py --list-all  # ignore the exception list

Known false positives live in `scripts/citation_exceptions.txt`, one per line, in the
`<citing file>:<cited token>` form the warnings print.
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXCEPTIONS = REPO / "scripts" / "citation_exceptions.txt"

# Where citations are written. These are the layers of the UI redesign, which is where the
# legacy-citation convention is practised; the rest of the repo does not cite this way.
SCAN_ROOTS = ("src/scenes", "src/ui", "sim/scenes_spec", "tests/spec")
SCAN_SUFFIXES = {".h", ".hpp", ".cpp"}

# What a citation may point INTO. Only the legacy tree rots relative to these comments; a
# citation into `src/ui` or `src/scenes` is a citation into a file the same commit can fix.
CITED_ROOT = "src/deluge"
CITED_SUFFIXES = {".h", ".hpp", ".cpp", ".c"}

# `path.ext:N` or `path.ext:N-M`. The path may be any suffix of the real one (`gate/selection.h`),
# which is how these are written; resolution below turns it into a repo path.
CITATION_RE = re.compile(
    r"([A-Za-z0-9_][A-Za-z0-9_./+-]*\.(?:h|hpp|cpp|c)):(\d+)(?:-(\d+))?"
)
# The bare continuation form -- `` `:55` `` -- which inherits the path from the previous citation
# on the same line. Task 11b reported 19 citations its own sweep could not match for exactly this
# reason, so they are captured here rather than silently skipped.
CONTINUATION_RE = re.compile(r"`:(\d+)(?:-(\d+))?`")

BOILERPLATE_RE = re.compile(
    r"^\s*(?:"
    r"#\s*(?:include|pragma|endif|else)\b"
    r"|(?:inline\s+)?namespace\b"
    r"|using\s+namespace\b"
    r"|\}\s*;?\s*(?://.*)?"
    r"|\{\s*"
    r")?\s*$"
)


def cited_index() -> dict[str, list[str]]:
    """Every legacy path, indexed by every suffix of itself."""
    index: dict[str, list[str]] = {}
    root = REPO / CITED_ROOT
    for p in sorted(root.rglob("*")):
        if not p.is_file() or p.suffix not in CITED_SUFFIXES:
            continue
        rel = str(p.relative_to(REPO))
        parts = p.relative_to(REPO).parts
        for i in range(len(parts)):
            index.setdefault("/".join(parts[i:]), []).append(rel)
    return index


_LINE_COUNTS: dict[str, int] = {}


def line_count(rel: str) -> int:
    if rel not in _LINE_COUNTS:
        _LINE_COUNTS[rel] = len(
            (REPO / rel).read_text(encoding="utf-8", errors="replace").splitlines()
        )
    return _LINE_COUNTS[rel]


def resolve(
    token: str, index: dict[str, list[str]], recent_dirs: list[str], line: int = 0
) -> str | None:
    """Turn a written citation path into a repo path, breaking ties the way a reader would."""
    hits = index.get(token)
    if not hits:
        return None
    if len(hits) > 1 and line > 0:
        # A candidate too short to contain the cited line is almost never the one meant --
        # `colour.h:43` cannot be `gui/colour/colour.h`, which is a four-line re-export, so it is
        # `gui/menu_item/colour.h`. Only narrows when it leaves something.
        long_enough = [h for h in hits if line_count(h) >= line]
        if long_enough:
            hits = long_enough
    if len(hits) == 1:
        return hits[0]
    # Ambiguous (`mode.h` matches five files). Prefer a file under a directory this same source
    # file has already cited unambiguously, then the shallowest path (`sidechain/shape.h` means
    # `menu_item/sidechain/shape.h`, not `menu_item/reverb/sidechain/shape.h`), then give up in a
    # stable way. This is a heuristic and it is wrong sometimes -- `integer.cpp` in `bindings.cpp`
    # means `patched_param/integer.cpp` and this picks `menu_item/integer.cpp`; those seven live
    # in the exception list, named as resolver artefacts rather than as bad citations.
    for d in reversed(recent_dirs[-4:]):
        for h in hits:
            if h.startswith(d):
                return h
    return min(hits, key=lambda h: (h.count("/"), h))


def header_block_end(lines: list[str]) -> int:
    """1-based line number of the last line of the file's leading `/* ... */` licence block, or 0."""
    for i, line in enumerate(lines):
        s = line.strip()
        if not s:
            continue
        if not s.startswith("/*"):
            return 0
        for j in range(i, len(lines)):
            if "*/" in lines[j]:
                return j + 1
        return len(lines)
    return 0


def certainly_wrong(path: Path, start: int, end: int) -> str | None:
    """Why the whole cited range can refer to no claim, or None if it might be fine."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if start < 1 or start > len(lines):
        return "past EOF"
    end = min(max(end, start), len(lines))
    if end <= header_block_end(lines):
        return "inside the licence header"
    if all(BOILERPLATE_RE.match(lines[n - 1]) for n in range(start, end + 1)):
        return "only include/namespace/brace/blank lines"
    return None


def load_exceptions() -> set[str]:
    if not EXCEPTIONS.exists():
        return set()
    return {
        line.split("#", 1)[0].strip()
        for line in EXCEPTIONS.read_text().splitlines()
        if line.split("#", 1)[0].strip()
    }


def scan() -> list[tuple[str, int, str, str, str]]:
    """Every certainly-wrong citation as `(citing file, line, token, resolved path, why)`."""
    index = cited_index()
    out = []
    for root in SCAN_ROOTS:
        for p in sorted((REPO / root).rglob("*")):
            if not p.is_file() or p.suffix not in SCAN_SUFFIXES:
                continue
            rel = str(p.relative_to(REPO))
            recent_dirs: list[str] = []
            for lineno, line in enumerate(
                p.read_text(encoding="utf-8", errors="replace").splitlines(), 1
            ):
                spans: list[tuple[str, int, int, str]] = []
                # Where each full citation ends, so a bare `` `:N` `` continuation can inherit the
                # path of the citation that precedes it **on the same line and to its left**.
                # Deliberately not "the last citation anywhere on the line": the table style used
                # throughout `menu/nodes/*.h` writes `| `fooMenu` (`:806`) | `Foo` (`foo.h:21`) |`,
                # where the bare token means `menus.cpp` (named in the prose above the table) and
                # the only citation on the line sits to its RIGHT. Inheriting rightwards turned
                # every such row into a false "past EOF". Unresolvable continuations are skipped --
                # under-reporting is the right failure for a warning-only net.
                anchors: list[tuple[int, str]] = []
                for m in CITATION_RE.finditer(line):
                    token, s, e = (
                        m.group(1),
                        int(m.group(2)),
                        int(m.group(3) or m.group(2)),
                    )
                    resolved = resolve(token, index, recent_dirs, e)
                    if resolved is None:
                        continue
                    recent_dirs.append(str(Path(resolved).parent) + "/")
                    anchors.append((m.end(), resolved))
                    spans.append((resolved, s, e, f"{token}:{m.group(2)}"))
                for m in CONTINUATION_RE.finditer(line):
                    prior = [path for end, path in anchors if end <= m.start()]
                    if not prior:
                        continue
                    s, e = int(m.group(1)), int(m.group(2) or m.group(1))
                    spans.append((prior[-1], s, e, f":{m.group(1)}"))
                for resolved, s, e, token in spans:
                    why = certainly_wrong(REPO / resolved, s, e)
                    if why is not None:
                        out.append((rel, lineno, token, resolved, why))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--strict", action="store_true", help="exit 1 on any unexcepted hit"
    )
    ap.add_argument(
        "--list-all", action="store_true", help="ignore scripts/citation_exceptions.txt"
    )
    a = ap.parse_args()

    excepted = set() if a.list_all else load_exceptions()
    hits = scan()
    shown = [h for h in hits if f"{h[0]}:{h[2]}" not in excepted]
    for rel, lineno, token, resolved, why in shown:
        print(f"{rel}:{lineno}: citation `{token}` -> {resolved}: {why}")
    skipped = len(hits) - len(shown)
    print(
        f"check_citations: {len(shown)} certainly-wrong citation(s)"
        + (f", {skipped} known exception(s) suppressed" if skipped else "")
        + (
            ""
            if a.strict
            else " -- warning only, see the module docstring for what this predicate cannot see"
        )
    )
    return 1 if (a.strict and shown) else 0


if __name__ == "__main__":
    sys.exit(main())
