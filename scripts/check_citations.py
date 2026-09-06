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

There is a **second** predicate, added by E4 task 3 and just as mechanical. The convention is that
a citation names its SYMBOL as well as its line -- `` `View::setModLedStates` (`view.cpp:1548-1679`) ``
-- and that is exactly the thing a line shift breaks while leaving the citation resolvable. So:
when a citation has a symbol written next to it, and that symbol **does occur in the cited file**,
it must occur **inside the cited range**. If it occurs only outside, the range has drifted off it.

That "does occur in the cited file" clause is what keeps the check sound rather than merely strict.
A citation is very often written as `port symbol` (`legacy file:lines`) --
`` `ModKnobs::calculate_knob_pos_for_mod_encoder_turn` … (`view.cpp:949-970`) `` -- where the symbol
names *this* layer and the citation names legacy. Those pair a name with a file that has never
contained it, so there is nothing to compare and they are skipped, not failed.

**Shapes this predicate deliberately skips or narrows.** This list is complete: a shape that is not
here is judged. A shape it cannot judge soundly must produce no verdict, and every exemption below
is one of those, not a convenience.

  * a citation with no backticked token adjacent to it (`` (`:1517`) `` on its own) -- there is no
    symbol to check. This is the biggest hole and it is structural: the bare continuation form
    carries no name, so nothing here can see drift in one. Most of E4 task 3's own drift was of
    exactly this shape;
  * a token that is not a plain identifier: an expression (`` `*getModKnobMode() == whichButton` ``),
    a phrase, an operator. Digging an identifier out of an expression is guesswork;
  * prose between the symbol and the citation. `` `renderOLED`'s polarity switcher (`:125-157`) ``
    does not claim to BE `renderOLED`; only `` `Sym` (`file:a-b`) `` does. Requiring the gap to be
    exactly "` (" took a first run from 493 hits to 255, nearly all of the difference being correct
    interior citations;
  * an identifier of fewer than `MIN_SYMBOL_LEN` characters -- `on`, `kind`, `set` occur everywhere
    and would agree with any range by luck;
  * an identifier that appears **nowhere** in the cited file (the port-symbol case above, and a
    symbol legacy has since deleted -- which is real rot, but rot the "past EOF" and boilerplate
    predicates are the right net for);
  * a citation into anything but `src/deluge`, as before.

**And two narrowings, which are not skips but do bound what it can see. Both are here because the
"deliberately skips" list above omitted them once, and a list that omits its own largest holes is
the false comfort this docstring exists to prevent.**

  * **Direction.** It judges only a range that ends ABOVE its symbol, never one that starts below
    it. A range starting below is `` `readCurrentValue` (`attack.h:34-36`) `` citing a function's
    *body* -- legitimate, and line-for-line indistinguishable from a citation that drifted
    downwards. A range wholly above its symbol is not ambiguous: nothing is ever the body of a
    function that begins after it. **Measured, not assumed**: a direction-agnostic variant run over
    the tree gave 35 upward and 47 downward; every one of the 35 was real drift, every one of the
    47 was a body citation, and the restriction missed none of the 37 defects the strict run had
    already found. Drift *downwards* past a symbol is therefore invisible here.
  * **`DRIFT_WINDOW`.** A symbol further than that many lines below the range is read as an
    unrelated later occurrence of a common name rather than as this range's symbol, and skipped. A
    citation that has slid further than the window is invisible too.

It still does **not** check that the cited line *means* what the comment claims -- that is not
mechanizable -- and a citation four lines low that still lands inside its own symbol passes. Task 9
found 176 of that shape by reading, and no predicate would have. So a clean run is not a clean bill
of health. It is a net under two specific regressions, and since E4 task 3 fix round 2 it is a
**blocking** one in CI (`--strict`), on the argument that a warning nobody fails on is what let
three consecutive rounds of drift ship: the cost of a false positive is one named line in the skip
list above, and the cost of a false negative has been measured three times.

  scripts/check_citations.py             # warn (always exit 0), for a quick local look
  scripts/check_citations.py --strict    # exit 1 on any unexcepted hit -- what CI runs
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

# The symbol written next to a citation, in either of the two house forms:
#   `View::setModLedStates` (`view.cpp:1548-1679`)   -- symbol to the LEFT, its own backticks
#   `gate/selection.h:43-54 selectButtonPress`       -- symbol to the RIGHT, same backticks
# The left form is the common one and is allowed to sit on the *previous* line, because a long
# symbol and its citation are routinely split across two comment lines.
# The left form, and *only* when the citation claims to BE that symbol: the symbol's closing
# backtick, then nothing but " (" before the citation. `` `renderOLED`'s polarity switcher
# (`patch_cable_strength.cpp:125-157`) `` does not claim to be `renderOLED` -- it names a block
# inside it -- and matching it would flag every interior citation in the tree. The trailing
# `` ` `` is the citation's own opening backtick.
LEFT_SYMBOL_RE = re.compile(r"`([^`]+)`\s*\(\s*`?$")
# The same, split across two comment lines: the symbol ends the line above and the citation opens
# this one after nothing but the comment lead-in.
LEFT_SYMBOL_PREV_RE = re.compile(r"`([^`]+)`\s*$")
LEAD_IN_RE = re.compile(r"^\s*(?:///|//|\*|)\s*\(\s*`?$")
RIGHT_SYMBOL_RE = re.compile(r"\s+([A-Za-z_][A-Za-z0-9_:]*)")
# A plain, possibly-qualified identifier, optionally with an empty or filled argument list. Anything
# else -- an expression, a phrase, an operator -- is not a symbol this can check.
SYMBOL_RE = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_]*::)*([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(.*\))?$"
)
# Below this length an identifier agrees with almost any range by accident.
MIN_SYMBOL_LEN = 4
# How far a named symbol may sit from its cited range before this is read as an interior citation
# rather than as drift. A line-shift edit moves a citation by a handful of lines; an interior range
# naming its enclosing function is usually tens of lines below the signature.
DRIFT_WINDOW = 6

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


_FILE_SYMBOL_LINES: dict[str, dict[str, list[int]]] = {}


def symbol_lines(rel: str, symbol: str) -> list[int]:
    """1-based lines of `rel` on which `symbol` appears as a whole word."""
    per_file = _FILE_SYMBOL_LINES.setdefault(rel, {})
    if symbol not in per_file:
        pattern = re.compile(r"\b" + re.escape(symbol) + r"\b")
        per_file[symbol] = [
            n
            for n, line in enumerate(
                (REPO / rel).read_text(encoding="utf-8", errors="replace").splitlines(),
                1,
            )
            if pattern.search(line)
        ]
    return per_file[symbol]


def adjacent_symbol(line: str, prev_line: str, start: int, end: int) -> str | None:
    """The identifier written next to the citation spanning [start, end), or None to skip.

    Returns the *last* segment of a qualified name -- `View::setModLedStates` is checked as
    `setModLedStates`, because that is what appears at the definition. See the module docstring for
    the shapes that deliberately yield None.
    """
    candidate: str | None = None
    # Right form: `path:43-54 selectButtonPress`, the symbol trailing inside the same backticks.
    m = RIGHT_SYMBOL_RE.match(line[end:])
    if m is not None:
        candidate = m.group(1)
    else:
        before = line[:start]
        m = LEFT_SYMBOL_RE.search(before)
        if m is not None:
            candidate = m.group(1)
        elif LEAD_IN_RE.match(before):
            m = LEFT_SYMBOL_PREV_RE.search(prev_line)
            if m is not None:
                candidate = m.group(1)
    if candidate is None:
        return None
    m = SYMBOL_RE.match(candidate.strip())
    if m is None:
        return None
    name = m.group(1)
    return name if len(name) >= MIN_SYMBOL_LEN else None


def symbol_drifted(rel: str, symbol: str, start: int, end: int) -> str | None:
    """Why `symbol` says the range has drifted, or None if it does not say so."""
    lines = symbol_lines(rel, symbol)
    if not lines:
        # The symbol is not in this file at all: a port symbol paired with a legacy citation, or a
        # symbol legacy has deleted. Nothing to compare -- see the docstring.
        return None
    if any(start <= n <= end for n in lines):
        return None
    # Only a range that ends ABOVE the symbol is judged. A range that starts below it is the
    # commonest legitimate shape in this tree -- `` `readCurrentValue` (`attack.h:34-36`) `` cites
    # the *body* of a function whose signature is at :33 -- and is indistinguishable, line by line,
    # from a citation that drifted downwards. A range wholly above its symbol is not: nothing is
    # ever "the body of" a function that starts after it, so the range has slid up off its
    # signature. That is the direction an insertion above the symbol produces, which is exactly the
    # regression `menus.cpp`'s two-line edit caused.
    later = [n for n in lines if n > end]
    if not later:
        return None
    nearest = min(later)
    if nearest - end > DRIFT_WINDOW:
        # Far below: an unrelated later occurrence of a common name, not this range's symbol.
        return None
    return f"`{symbol}` is at :{nearest}, {nearest - end} line(s) below the end of the cited range"


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
            prev_line = ""
            for lineno, line in enumerate(
                p.read_text(encoding="utf-8", errors="replace").splitlines(), 1
            ):
                spans: list[tuple[str, int, int, str, str | None]] = []
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
                    spans.append(
                        (
                            resolved,
                            s,
                            e,
                            f"{token}:{m.group(2)}",
                            adjacent_symbol(line, prev_line, m.start(), m.end()),
                        )
                    )
                for m in CONTINUATION_RE.finditer(line):
                    prior = [path for end, path in anchors if end <= m.start()]
                    if not prior:
                        continue
                    s, e = int(m.group(1)), int(m.group(2) or m.group(1))
                    # A bare continuation carries no symbol of its own: the backticked token to its
                    # left is the citation it inherits from, not a name for these lines.
                    spans.append((prior[-1], s, e, f":{m.group(1)}", None))
                for resolved, s, e, token, symbol in spans:
                    why = certainly_wrong(REPO / resolved, s, e)
                    if why is None and symbol is not None:
                        why = symbol_drifted(resolved, symbol, s, e)
                    if why is not None:
                        out.append((rel, lineno, token, resolved, why))
                prev_line = line
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
