#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Refuse a comparison that a C++ spec judges with to_be_true(), to_be_false(), to_be_truthy() or to_be_falsy().

`expect(a == b).to_be_true()` fails with "expected true, got false" and says nothing of a or b;
`expect(a).to_equal(b)` fails with both values. A site is an `expect(<argument>)` followed directly by
`.to_be_true(` or `.to_be_false(` (either may follow `.not_()`) whose argument holds, outside every bracket, a
comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`) or a `&&` or `||`; `.to_be_truthy(` judges as `.to_be_true(` and
`.to_be_falsy(` as `.to_be_false(`. Parentheses wrapping the whole argument and a `!` or
`not` applied to such parentheses are looked through, each negation (and `.not_()`) flipping the verdict, so
`expect(!(a == b)).to_be_true()` is the site `a == b` judged false. Comments and the insides of string and character
literals are not read, and a `<` that opens a template argument list (`static_cast<int*>(p)`, `std::get<1>(t)`) is
not a comparison. Each site is printed with the matcher that judges it instead (KINDS).

Modes:
  (default)  every site in the tracked C++ files under ROOTS (or under the paths given); --strict exits 1 on any.
  --staged   the sites the index adds over HEAD across every staged C++ file under ROOTS, for pre-commit: a site
             moved within a file or between files passes, a new one fails (exit 1). During a merge a site is new
             only beyond the copies either parent (HEAD or MERGE_HEAD) holds.
  --summary  site counts per directory, then per kind.

Gate spec-comparisons:
    --strict over every tracked spec file under ROOTS: no site anywhere, so a failed comparison in any spec names
    the values it compared rather than "expected true"; the pre-commit hook refuses only the sites a commit adds,
    and this gate catches any that reach the tree another way
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
    "text": "expect(s).to_contain(needle), or expect(s).not_().to_contain(needle)",
    "bytes": "to_equal_bytes(expect(std::span<const uint8_t>(a)), b)",
    "cstring": "expect(a).to_equal(b), which compares two C strings as text; std::string_view(a, n) for strncmp",
    "chain": "one expectation per comparison",
    "and": "one expectation per condition, guarded by an if where a later one dereferences",
    "or": "one expectation on the value the alternatives share, or to_satisfy with a named predicate",
}
START = re.compile(r"\bexpect\s*\(")
VERDICT = re.compile(
    r"\s*(\.\s*not_\s*\(\s*\)\s*)?\.\s*to_be_(true|false|truthy|falsy)\s*\("
)
JUDGED = {"true": "true", "truthy": "true", "false": "false", "falsy": "false"}
NEGATION = re.compile(r"!|\bnot\b")
ABS_CALL = re.compile(r"(?:std::)?f?abs[fl]?\s*\(")
FIND_CALL = re.compile(r"\.\s*r?find\s*\(")
RAW_STRING = re.compile(r'(?:u8|[uUL])?R"([^()\\\s]{0,16})\(')
IDENT = re.compile(r"[A-Za-z0-9_]")


@dataclass(frozen=True)
class Site:
    path: str
    line: int
    start: int  # offset of `expect`
    arg: tuple[
        int, int
    ]  # offsets of the comparison: the argument with wrapping parentheses and negations peeled
    end: int  # offset just past the `)` that closes the verdict's call
    verdict: str  # what the comparison is judged to be, "true" or "false", after every negation
    kind: str
    text: str  # the comparison with its whitespace collapsed
    written: str = (
        ""  # the expectation as written, whitespace collapsed and the message left out
    )
    shape: str = ""  # the comparison with comments and layout dropped: what identifies the site across commits

    def key(self) -> tuple[str, str]:
        return (self.verdict, self.shape or self.text)


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
    if {"nullptr", "NULL"} & {lhs, rhs}:
        return "null"
    if re.search(r"\bnpos\b", lhs + " " + rhs) and FIND_CALL.search(lhs + rhs):
        return "text"
    if re.search(r"\bmemcmp\s*\(", lhs + rhs):
        return "bytes"
    if re.search(r"\bstrn?cmp\s*\(", lhs + rhs):
        return "cstring"
    if op not in ("==", "!=") and (ABS_CALL.match(lhs) or ABS_CALL.match(rhs)):
        return "tolerance"
    return {"==": "eq", "!=": "ne"}.get(op, "order")


def shape(text: str, b: str) -> str:
    """text with comments dropped and whitespace removed, except one space between two identifier characters;
    whitespace inside a literal is kept. b is text blanked."""
    held = "\0"  # stands in for a space that stays (in a literal, between words) while the rest is squeezed
    out = []
    for c, bc in zip(text, b):
        if bc == " " and not c.isspace():
            out.append(" ")  # a comment
        elif bc == "_" and c.isspace():
            out.append(held)
        else:
            out.append(c)
    squeezed = re.sub(r"\s+", "", re.sub(r"(?<=\w)\s+(?=\w)", held, "".join(out)))
    return squeezed.replace(held, " ")


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


def _wrapped(b: str, lo: int, hi: int) -> bool:
    """Whether b[lo:hi] is one pair of parentheses and what they hold."""
    return lo < hi and b[lo] == "(" and close_paren(b, lo) == hi - 1


def _skip_space(b: str, i: int) -> int:
    while i < len(b) and b[i].isspace():
        i += 1
    return i


def peel(b: str, lo: int, hi: int) -> tuple[int, int, int]:
    """b[lo:hi] without the parentheses wrapping all of it and the negations applied to such parentheses, and how
    many negations were peeled. A negation of anything else (`!p`, `!a == b`) stays: it binds tighter than a
    comparison, so it does not negate the whole."""
    flips = 0
    while True:
        lo = _skip_space(b, lo)
        while hi > lo and b[hi - 1].isspace():
            hi -= 1
        if _wrapped(b, lo, hi):
            lo, hi = lo + 1, hi - 1
            continue
        neg = NEGATION.match(b, lo)
        if not neg:
            return lo, hi, flips
        operand = _skip_space(b, neg.end())
        inner = operand
        while m := NEGATION.match(b, inner):
            inner = _skip_space(b, m.end())
        if not _wrapped(b, inner, hi):
            return lo, hi, flips
        lo, flips = operand, flips + 1


def sites(path: str, text: str) -> list[Site]:
    b = blank(text)
    out: list[Site] = []
    for m in START.finditer(b):
        arg_end = close_paren(b, m.end() - 1)
        verdict = VERDICT.match(b, arg_end + 1)
        if not verdict:
            continue
        lo, hi, flips = peel(b, m.end(), arg_end)
        kind = kind_of(text[lo:hi], b[lo:hi])
        if not kind:
            continue
        flips += bool(verdict[1])
        judged = JUDGED[verdict[2]]
        if flips % 2:
            judged = {"true": "false", "false": "true"}[judged]
        call = " ".join(text[m.start() : arg_end + 1].split())
        call += "".join(text[arg_end + 1 : verdict.end()].split()) + ")"
        out.append(
            Site(
                path=path,
                line=text.count("\n", 0, m.start()) + 1,
                start=m.start(),
                arg=(lo, hi),
                end=close_paren(b, verdict.end() - 1) + 1,
                verdict=judged,
                kind=kind,
                text=" ".join(text[lo:hi].split()),
                written=call,
                shape=shape(text[lo:hi], b[lo:hi]),
            )
        )
    return out


def _keys(files: dict[str, str]) -> collections.Counter[tuple[str, str]]:
    return collections.Counter(
        s.key() for path, text in files.items() for s in sites(path, text)
    )


def added(
    old: dict[str, str], new: dict[str, str], theirs: dict[str, str] | None = None
) -> list[Site]:
    """The sites in `new` (path -> text) beyond those in `old`, matched by verdict and comparison shape. With
    `theirs` (a merged parent's files), a site is beyond only when it is beyond both parents' copies."""
    budget = _keys(old) | _keys(theirs or {})
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


def _merging() -> bool:
    return (
        subprocess.run(
            ["git", "rev-parse", "-q", "--verify", "MERGE_HEAD"],
            cwd=REPO,
            capture_output=True,
            check=False,
        ).returncode
        == 0
    )


def staged_sites() -> list[Site]:
    old: dict[str, str] = {}
    new: dict[str, str] = {}
    fields = _git(
        "diff", "--cached", "--name-status", "--no-renames", "-z", "--", *ROOTS
    ).split("\0")
    for status, path in zip(fields[0::2], fields[1::2]):
        if not path.endswith(SUFFIXES):
            continue
        if status != "A":
            old[path] = _show(f"HEAD:{path}")
        if status != "D":
            new[path] = _show(f":{path}")
    theirs = (
        {path: _show(f"MERGE_HEAD:{path}") for path in old.keys() | new.keys()}
        if _merging()
        else None
    )
    return added(old, new, theirs)


def tree_sites(paths: tuple[str, ...]) -> list[Site]:
    out: list[Site] = []
    for f in filter(None, _git("ls-files", "-z", "--", *paths).split("\0")):
        if f.endswith(SUFFIXES):
            out.extend(sites(f, (REPO / f).read_text(errors="ignore")))
    return out


def describe(s: Site) -> str:
    written = s.written or f"expect({s.text}).to_be_{s.verdict}()"
    return f"{s.path}:{s.line}: {s.kind}: {written} -- use {KINDS[s.kind]}"


def report(found: list[Site]) -> None:
    for s in found:
        print(describe(s))


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
