#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Refuse a comparison that a C++ spec judges with to_be_true() or to_be_false().

`expect(a == b).to_be_true()` fails with "expected true, got false" and says nothing of a or b;
`expect(a).to_equal(b)` fails with both values. A site is an `expect(<argument>)` followed directly by
`.to_be_true(` or `.to_be_false(` whose argument holds, outside every bracket, a comparison (`==`, `!=`, `<`, `>`,
`<=`, `>=`) or a `&&` or `||`. Comments and the insides of string and character literals are not read, and a `<`
that opens a template argument list (`static_cast<int*>(p)`, `std::get<1>(t)`) is not a comparison. Each site is
printed with the matcher that judges it instead (KINDS).

Modes:
  (default)  every site in the tracked C++ files under ROOTS (or under the paths given); --strict exits 1 on any.
  --staged   the sites the index adds over HEAD across every staged C++ file under ROOTS, for pre-commit: a site
             moved within a file or between files passes, a new one fails (exit 1).
  --summary  site counts per directory, then per kind.
"""

from __future__ import annotations

import argparse
import collections
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROOTS = ("tests", "sim/scenes_spec")
SUFFIXES = (".cpp", ".h", ".hpp", ".inc")
COMPARISONS = ("==", "!=", "<", ">", "<=", ">=")
KINDS = {
    "null": "expect(p).to_be_null(), or expect(p).not_().to_be_null()",
    "eq": "expect(a).to_equal(b)",
    "ne": "expect(a).not_().to_equal(b)",
    "order": "to_be_less_than/to_be_greater_than, or to_be_at_most/to_be_at_least(expect(a), b)",
    "tolerance": "expect(a).to_be_within(t).of(b)",
    "text": "to_contain_text(expect(s), needle), or to_contain_text(expect(s).not_(), needle)",
    "bytes": "to_equal_bytes(expect(std::span<const uint8_t>(a)), b)",
    "cstring": "expect(std::string_view(a)).to_equal(std::string_view(b))",
    "chain": "one expectation per comparison",
    "and": "one expectation per condition, guarded by an if where a later one dereferences",
    "or": "one expectation on the value the alternatives share, or to_satisfy with a named predicate",
}
START = re.compile(r"\bexpect\s*\(")
VERDICT = re.compile(r"\s*\.\s*to_be_(true|false)\s*\(")
RAW_STRING = re.compile(r'(?:u8|[uUL])?R"([^()\\\s]{0,16})\(')
IDENT = re.compile(r"[A-Za-z0-9_]")


@dataclass(frozen=True)
class Site:
    path: str
    line: int
    start: int  # offset of `expect`
    arg: tuple[int, int]  # offsets of the argument, inside expect's parentheses
    end: int  # offset just past the `)` that closes to_be_true( or to_be_false(
    verdict: str  # "true" or "false"
    kind: str
    text: str  # the argument with its whitespace collapsed

    def key(self) -> tuple[str, str]:
        return (self.verdict, self.text)


def _is_separator(text: str, i: int) -> bool:
    """Whether the `'` at text[i] is a digit separator (`1'000`) rather than a character literal's quote."""
    j = i
    while j > 0 and (IDENT.match(text[j - 1]) or text[j - 1] in ".'"):
        j -= 1
    return j < i and text[j].isdigit()


def blank(text: str) -> str:
    """text with comments blanked to spaces and literal contents to `_`; every offset and newline kept."""
    out = list(text)
    n = len(text)

    def fill(a: int, b: int, ch: str) -> None:
        for k in range(a, min(b, n)):
            if out[k] != "\n":
                out[k] = ch

    i = 0
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            fill(i, j, " ")
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            fill(i, j, " ")
            i = j
        elif (m := RAW_STRING.match(text, i)) and not (
            i > 0 and IDENT.match(text[i - 1])
        ):
            close = text.find(")" + m[1] + '"', m.end())
            close = n if close < 0 else close
            fill(m.end(), close, "_")
            i = close + len(m[1]) + 2
        elif text[i] == '"' or (text[i] == "'" and not _is_separator(text, i)):
            quote = text[i]
            j = i + 1
            while j < n and text[j] != quote and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            fill(i + 1, j, "_")
            i = j + 1
        else:
            i += 1
    return "".join(out)


def _template_end(b: str, i: int) -> int:
    """Offset of the `>` closing a template argument list opened by the `<` at b[i], or -1 when none closes it."""
    depth = nest = 0
    for j in range(i, len(b)):
        c = b[j]
        if c in "([{":
            nest += 1
        elif c in ")]}":
            if nest == 0:
                return -1
            nest -= 1
        elif nest == 0:
            if c == ";" or b.startswith(("&&", "||"), j):
                return -1
            if c == "<":
                depth += 1
            elif c == ">":
                depth -= 1
                if depth == 0:
                    return j
    return -1


def top_level_ops(b: str) -> list[tuple[str, int]]:
    """The comparison and logical operators outside every bracket of a blanked expression, with their offsets."""
    ops: list[tuple[str, int]] = []
    depth = 0
    i = 0
    while i < len(b):
        c = b[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif depth == 0:
            if b.startswith("<=>", i):
                ops.append(("<=>", i))
                i += 3
                continue
            two = b[i : i + 2]
            if two in ("<<", ">>", "->"):
                i += 2
                continue
            if two in ("==", "!=", "<=", ">=", "&&", "||"):
                ops.append((two, i))
                i += 2
                continue
            if c == "<" and i > 0 and (IDENT.match(b[i - 1]) or b[i - 1] in ">:"):
                end = _template_end(b, i)
                if end >= 0:
                    i = end + 1
                    continue
            if c in "<>":
                ops.append((c, i))
        i += 1
    return ops


def kind_of(arg: str, b: str) -> str:
    """The kind of site an expect argument makes (a KINDS key), or "" when it is not one."""
    ops = top_level_ops(b)
    names = [op for op, _ in ops]
    if "||" in names:
        return "or"
    if "&&" in names:
        return "and"
    comparisons = [(op, at) for op, at in ops if op in COMPARISONS]
    if not comparisons:
        return ""
    if len(comparisons) > 1:
        return "chain"
    op, at = comparisons[0]
    lhs, rhs = arg[:at].strip(), arg[at + len(op) :].strip()
    if "nullptr" in (lhs, rhs):
        return "null"
    if re.search(r"\bnpos\b", lhs + " " + rhs) and ".find(" in lhs + rhs:
        return "text"
    if re.search(r"\bmemcmp\s*\(", lhs + rhs):
        return "bytes"
    if re.search(r"\bstrn?cmp\s*\(", lhs + rhs):
        return "cstring"
    if op not in ("==", "!=") and re.match(r"(?:std::)?f?abs\s*\(", lhs):
        return "tolerance"
    return {"==": "eq", "!=": "ne"}.get(op, "order")


def close_paren(b: str, open_at: int) -> int:
    """Offset of the `)` matching the `(` at b[open_at], or len(b)."""
    depth = 0
    for j in range(open_at, len(b)):
        if b[j] == "(":
            depth += 1
        elif b[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return len(b)


def sites(path: str, text: str) -> list[Site]:
    b = blank(text)
    out: list[Site] = []
    for m in START.finditer(b):
        arg_end = close_paren(b, m.end() - 1)
        verdict = VERDICT.match(b, arg_end + 1)
        if not verdict:
            continue
        kind = kind_of(text[m.end() : arg_end], b[m.end() : arg_end])
        if not kind:
            continue
        out.append(
            Site(
                path=path,
                line=text.count("\n", 0, m.start()) + 1,
                start=m.start(),
                arg=(m.end(), arg_end),
                end=close_paren(b, verdict.end() - 1) + 1,
                verdict=verdict[1],
                kind=kind,
                text=" ".join(text[m.end() : arg_end].split()),
            )
        )
    return out


def added(old: dict[str, str], new: dict[str, str]) -> list[Site]:
    """The sites in `new` (path -> text) beyond those in `old`, matched by verdict and argument text."""
    budget = collections.Counter(
        s.key() for path, text in old.items() for s in sites(path, text)
    )
    out = []
    for path, text in sorted(new.items()):
        for s in sites(path, text):
            if budget[s.key()] > 0:
                budget[s.key()] -= 1
            else:
                out.append(s)
    return out


def _git(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=REPO, capture_output=True, text=True, check=True
    ).stdout


def _show(spec: str) -> str:
    done = subprocess.run(
        ["git", "show", spec], cwd=REPO, capture_output=True, text=True, check=False
    )
    return done.stdout if done.returncode == 0 else ""


def staged_sites() -> list[Site]:
    old: dict[str, str] = {}
    new: dict[str, str] = {}
    for line in _git(
        "diff", "--cached", "--name-status", "--no-renames", "--", *ROOTS
    ).splitlines():
        status, path = line.split("\t", 1)
        if not path.endswith(SUFFIXES):
            continue
        if status != "A":
            old[path] = _show(f"HEAD:{path}")
        if status != "D":
            new[path] = _show(f":{path}")
    return added(old, new)


def tree_sites(paths: tuple[str, ...]) -> list[Site]:
    out: list[Site] = []
    for f in _git("ls-files", "--", *paths).split():
        if f.endswith(SUFFIXES):
            out.extend(sites(f, (REPO / f).read_text(errors="ignore")))
    return out


def report(found: list[Site]) -> None:
    for s in found:
        print(
            f"{s.path}:{s.line}: {s.kind}: expect({s.text}).to_be_{s.verdict}() -- use {KINDS[s.kind]}"
        )


def summary(found: list[Site]) -> None:
    by_dir = collections.Counter(str(Path(s.path).parent) for s in found)
    by_kind = collections.Counter(f"{s.kind} ({s.verdict})" for s in found)
    for d, n in sorted(by_dir.items()):
        print(f"{n:6d} {d}")
    for k, n in by_kind.most_common():
        print(f"{n:6d} {k}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("paths", nargs="*", help="files or directories (default: ROOTS)")
    ap.add_argument("--strict", action="store_true", help="exit 1 on any site")
    ap.add_argument(
        "--staged",
        action="store_true",
        help="only the sites the index adds over HEAD; exit 1 on any",
    )
    ap.add_argument(
        "--summary",
        action="store_true",
        help="counts per directory and kind instead of each site",
    )
    a = ap.parse_args()
    found = staged_sites() if a.staged else tree_sites(tuple(a.paths) or ROOTS)
    (summary if a.summary else report)(found)
    print(
        f"check_spec_comparisons: {len(found)} site(s)"
        + (" added by the index" if a.staged else "")
    )
    return 1 if found and (a.strict or a.staged) else 0


if __name__ == "__main__":
    sys.exit(main())
