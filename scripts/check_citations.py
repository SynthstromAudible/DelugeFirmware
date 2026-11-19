#!/usr/bin/env python3
r"""Warn about line citations in the legacy tree that certainly point at nothing.

Legacy's own comments (`src/deluge`) cite other legacy code by line (`path.h:N-M symbol`). A line moves with
every edit above it, so these citations rot. The UI trees cite by file and symbol instead, and
`check_doc_hygiene.py` refuses a line number there; this script is the warning-only net for `src/deluge`.

WHAT IT REPORTS
---------------
For each citation `path.ext:N` or `path.ext:N-M`, and each bare `` `:N` `` that continues the citation to its left
on the same line:

  * the range starts past the file's end;
  * the range lies entirely inside the file's leading licence block;
  * the range is nothing but `#include` / `#pragma` / `namespace` / brace / blank lines;
  * a symbol is written beside the citation (`` `View::setModLedStates` (`view.cpp:N-M`) ``, or
    `path.ext:N-M symbol` inside the same backticks), the cited file defines it, and the range lies inside none
    of its definitions. A definition's extent is brace-matched from its definition line to its closing brace and
    the blank lines after it; a range inside any one definition (an overload, two classes' same-named method)
    passes.

WHAT IT CANNOT SEE
------------------
Whether the range still means what the sentence claims: a citation a few lines off that still lands inside its own
symbol passes. These shapes give no verdict, by design, because no sound one is possible:

  * a path that names no file, and a bare continuation with no citation to its left on its line;
  * a symbol that is not a plain identifier (an expression, a phrase, an operator), or is shorter than
    `MIN_SYMBOL_LEN`;
  * prose between the symbol and the citation: only `` `Sym` (`file:N`) `` claims to be `Sym`, while
    `` `Sym`'s first arm (`file:N`) `` names a block inside it;
  * a slash-separated pair of aliases before a two-range citation (`` `A` / `B` (`file:N` / `:M`) ``), which pairs
    names with ranges by position;
  * a symbol the cited file does not contain, or contains only as a mention (a declaration, a macro, a forward
    declaration, a call site), which has no body to brace-match.

An ambiguous path resolves to a candidate long enough to hold the cited line, then to one under a directory the
same file cited recently, then to the shallowest. Qualify the path where that guess is wrong.

  scripts/check_citations.py    # warn; always exits 0
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Where citations are read, and what they may point into: the legacy tree.
SCAN_ROOT = "src/deluge"
SCAN_SUFFIXES = {".h", ".hpp", ".cpp", ".txt", ".md"}
CITED_SUFFIXES = {".h", ".hpp", ".cpp", ".c"}

# `path.ext:N` or `path.ext:N-M`. The path may be any suffix of the real one; `resolve` turns it
# into a repo path.
CITATION_RE = re.compile(
    r"([A-Za-z0-9_][A-Za-z0-9_./+-]*\.(?:h|hpp|cpp|c)):(\d+)(?:-(\d+))?"
)
# The bare continuation, which inherits the path of the citation to its left on the same line.
CONTINUATION_RE = re.compile(r"`:(\d+)(?:-(\d+))?`")

# The symbol written next to a citation. To the LEFT, in its own backticks, with nothing but " ("
# between it and the citation (the citation claims to BE that symbol); the trailing backtick is the
# citation's own opening one. The left form may end the previous comment line instead.
LEFT_SYMBOL_RE = re.compile(r"`([^`]+)`\s*\(\s*`?$")
LEFT_SYMBOL_PREV_RE = re.compile(r"`([^`]+)`\s*$")
LEAD_IN_RE = re.compile(r"^\s*(?:///|//|\*|)\s*\(\s*`?$")
# A slash-separated pair of aliases immediately before the citation. A comment's own leading `//`
# also ends in a bare slash, but is not preceded by a backticked token.
SIBLING_ALIAS_RE = re.compile(r"`[^`]+`\s*/\s*$")
# Nothing but a comment's own lead-in before a candidate.
ONLY_LEAD_IN_RE = re.compile(r"^\s*(?:///|//|\*|)\s*$")
# To the RIGHT, inside the citation's own backticks.
RIGHT_SYMBOL_RE = re.compile(r"\s+([A-Za-z_][A-Za-z0-9_:]*)")
# A plain, possibly qualified identifier, optionally with an argument list.
SYMBOL_RE = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_]*::)*([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(.*\))?$"
)
# Below this length an identifier agrees with almost any range by accident.
MIN_SYMBOL_LEN = 4
# How far past a candidate definition's parameter list to look for the `{` that opens its body
# (past qualifiers, a trailing return type or an initializer list) before giving the occurrence up.
MAX_SIGNATURE_SCAN = 4000

BOILERPLATE_RE = re.compile(
    r"^\s*(?:"
    r"#\s*(?:include|pragma|endif|else)\b"
    r"|(?:inline\s+)?namespace\b"
    r"|using\s+namespace\b"
    r"|\}\s*;?\s*(?://.*)?"
    r"|\{\s*"
    r")?\s*$"
)


def cited_index(root: str) -> dict[str, list[str]]:
    """Every citable file under `root`, indexed by every suffix of its path."""
    index: dict[str, list[str]] = {}
    for p in sorted((REPO / root).rglob("*")):
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
        # A candidate too short to contain the cited line is almost never the one meant: a
        # four-line re-export header cannot be what a citation of its fortieth line names.
        long_enough = [h for h in hits if line_count(h) >= line]
        if long_enough:
            hits = long_enough
    if len(hits) == 1:
        return hits[0]
    # Still ambiguous: prefer a file under a directory this source file cited recently, then the
    # shallowest path, then give up in a stable way.
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


_FILE_TEXT: dict[str, str] = {}
_FILE_MASKED: dict[str, str] = {}


def file_text(rel: str) -> str:
    if rel not in _FILE_TEXT:
        _FILE_TEXT[rel] = (REPO / rel).read_text(encoding="utf-8", errors="replace")
    return _FILE_TEXT[rel]


_FILE_LINES: dict[str, list[str]] = {}


def lines_of(rel: str) -> list[str]:
    if rel not in _FILE_LINES:
        _FILE_LINES[rel] = file_text(rel).splitlines()
    return _FILE_LINES[rel]


def masked_text(rel: str) -> str:
    """`file_text(rel)` with every comment and string or char literal blanked to spaces, newlines kept.

    The brace and paren matching below trusts every `{`, `}`, `(`, `)` and `;` it sees, so one inside
    a comment or a literal must not reach it. Line numbers computed from an offset into this text are
    the original's.
    """
    if rel in _FILE_MASKED:
        return _FILE_MASKED[rel]
    text = file_text(rel)
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j == -1 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            j = min(j + 1, n)
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    masked = "".join(out)
    _FILE_MASKED[rel] = masked
    return masked


_FILE_EXTENTS: dict[str, dict[str, list[tuple[int, int]]]] = {}


_CLASS_KEYWORD_RE = re.compile(r"\b(?:class|struct)\s*$")
# `Symbol::method(` right after the match: an out-of-line member function, part of `Symbol`'s own
# definition, so its body counts as one of `Symbol`'s extents.
_QUALIFIED_METHOD_RE = re.compile(r"\s*::\s*[A-Za-z_]\w*\s*\(")


def _scan_for_body_open(masked: str, i: int, limit: int) -> int | None:
    """From offset `i`, the position of the `{` that opens a body, or None if a top-level `;` (a
    declaration) comes first or nothing recognisable turns up within `limit` characters.

    Tracks paren depth only, so a base clause's `Base(args)`, an initializer list, a trailing return
    type, `final`, `override` or template arguments pass through: none holds a top-level `{` or `;`.
    """
    depth = 0
    n = min(len(masked), limit)
    for j in range(i, n):
        c = masked[j]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif depth == 0 and c == "{":
            return j
        elif depth == 0 and c == ";":
            return None
    return None


def definition_extents(rel: str, symbol: str) -> list[tuple[int, int]]:
    """Brace-matched `[definition_line, body_end_line]` for every place `rel` DEFINES `symbol`.

    An occurrence is a definition when it is EITHER immediately followed by its own parameter list and
    then, past any qualifiers, a trailing return type or an initializer list, a `{` before any
    top-level `;` (a call's arguments cannot fool this: only what follows the matching `)` decides);
    OR immediately preceded by `class` or `struct`, with the same `{`-before-`;` test after the name;
    OR immediately followed by `::method(` (an out-of-line member of the class `symbol` names). A
    declaration, a macro, a forward declaration or a call site contributes nothing.

    The extent runs on through the blank lines after the closing brace, since a citation of a whole
    function routinely includes the gap before the next declaration. More than one entry means the
    name is defined more than once in this file.
    """
    per_file = _FILE_EXTENTS.setdefault(rel, {})
    if symbol in per_file:
        return per_file[symbol]
    text = file_text(rel)
    masked = masked_text(rel)
    n = len(masked)
    pattern = re.compile(r"\b" + re.escape(symbol) + r"\b")
    extents: list[tuple[int, int]] = []
    for m in pattern.finditer(masked):
        i = m.end()
        if _CLASS_KEYWORD_RE.search(masked[: m.start()]):
            # `class Foo final : public Bar { ... };` has no parameter list to walk first.
            pass
        else:
            qm = _QUALIFIED_METHOD_RE.match(masked, i)
            if qm is not None:
                i = qm.end() - 1  # at the method's own opening "(", walked below
            else:
                while i < n and masked[i].isspace():
                    i += 1
                if i >= n or masked[i] != "(":
                    continue  # a mention, not a definition
            # Walk the parameter list so a call's arguments (which may hold `(`, `)` or a `{}`
            # default) cannot be mistaken for what follows it.
            depth, i = 1, i + 1
            while i < n and depth > 0:
                if masked[i] == "(":
                    depth += 1
                elif masked[i] == ")":
                    depth -= 1
                i += 1
            if depth != 0:
                continue  # unbalanced to the end of the file
        body_open = _scan_for_body_open(masked, i, min(n, i + MAX_SIGNATURE_SCAN))
        if body_open is None:
            continue  # a declaration, or nothing recognisable within the scan window
        def_line = text.count("\n", 0, m.start()) + 1
        depth, j = 1, body_open + 1
        while j < n and depth > 0:
            if masked[j] == "{":
                depth += 1
            elif masked[j] == "}":
                depth -= 1
            j += 1
        if depth != 0:
            continue  # unbalanced to the end of the file
        body_end_line = text.count("\n", 0, j - 1) + 1
        file_lines = lines_of(rel)
        while body_end_line < len(file_lines) and not file_lines[body_end_line].strip():
            body_end_line += 1
        extents.append((def_line, body_end_line))
    per_file[symbol] = extents
    return extents


def adjacent_symbol(line: str, prev_line: str, start: int, end: int) -> str | None:
    """The identifier written next to the citation spanning [start, end), or None to skip.

    Returns the *last* segment of a qualified name (`View::setModLedStates` is checked as
    `setModLedStates`, which is what the definition spells). The module docstring lists the shapes
    that yield None.
    """
    candidate: str | None = None
    m = RIGHT_SYMBOL_RE.match(line[end:])
    if m is not None:
        candidate = m.group(1)
    else:
        before = line[:start]
        m = LEFT_SYMBOL_RE.search(before)
        if m is not None:
            lead = before[: m.start()]
            # The alias pair's slash is on this line before the candidate, or, when the candidate
            # starts its line, at the end of the previous one.
            if SIBLING_ALIAS_RE.search(lead) or (
                ONLY_LEAD_IN_RE.match(lead) and SIBLING_ALIAS_RE.search(prev_line)
            ):
                return None
            candidate = m.group(1)
        elif LEAD_IN_RE.match(before):
            m = LEFT_SYMBOL_PREV_RE.search(prev_line)
            if m is not None:
                if SIBLING_ALIAS_RE.search(prev_line[: m.start()]):
                    return None
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
        return None  # not in this file at all: nothing to compare
    if any(start <= n <= end for n in lines):
        # The range holds a line the symbol's own name is written on (a declaration line, a
        # one-line accessor, a split signature): the symbol is right there.
        return None
    extents = definition_extents(rel, symbol)
    if not extents:
        return None  # only mentions, no body to brace-match
    if any(d <= start and end <= b for d, b in extents):
        return None

    # Report against the extent the range is closest to, the definition a reader would suspect.
    def _gap(e: tuple[int, int]) -> int:
        d, b = e
        return (d - end) if end < d else (start - b) if start > b else 0

    d, b = min(extents, key=_gap)
    if end < d:
        return f"`{symbol}` is defined at :{d}-{b}, {d - end} line(s) above the cited range"
    if start > b:
        return f"`{symbol}` is defined at :{d}-{b}, {start - b} line(s) below the end of the cited range"
    return (
        f"`{symbol}` is defined at :{d}-{b}, which the cited range only partly overlaps"
    )


def scan() -> list[tuple[str, int, str, str, str]]:
    """Every certainly-wrong citation as `(citing file, line, token, resolved path, why)`."""
    index = cited_index(SCAN_ROOT)
    out = []
    for p in sorted((REPO / SCAN_ROOT).rglob("*")):
        if not p.is_file() or p.suffix not in SCAN_SUFFIXES:
            continue
        rel = str(p.relative_to(REPO))
        recent_dirs: list[str] = []
        prev_line = ""
        for lineno, line in enumerate(
            p.read_text(encoding="utf-8", errors="replace").splitlines(), 1
        ):
            spans: list[tuple[str, int, int, str, str | None]] = []
            # Where each full citation ends, so a bare continuation inherits the path of the citation
            # to its LEFT on the same line. A table row can name its file in the prose above and hold
            # an unrelated citation to the right of the continuation; inheriting from that one would
            # read the wrong file.
            anchors: list[tuple[int, str]] = []
            for m in CITATION_RE.finditer(line):
                token, s, e = (
                    m.group(1),
                    int(m.group(2)),
                    int(m.group(3) or m.group(2)),
                )
                resolved = resolve(token, index, recent_dirs, e)
                if resolved is None:
                    # No such file; a continuation of it has nothing to inherit either.
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
                # A continuation carries no symbol of its own: the backticked token to its left is
                # the citation it inherits from, not a name for these lines.
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
    argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    ).parse_args()
    hits = scan()
    for rel, lineno, token, resolved, why in hits:
        print(f"{rel}:{lineno}: citation `{token}` -> {resolved}: {why}")
    print(
        f"check_citations: {len(hits)} certainly-wrong citation(s) in {SCAN_ROOT} -- warning only, "
        "see the module docstring for what this predicate cannot see"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
