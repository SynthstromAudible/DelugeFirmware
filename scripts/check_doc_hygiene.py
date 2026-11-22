#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Flag what the UI trees' docs may not hold: history and process notes, line numbers, and paths that name no file.

A comment says what the code is and why; how it got there is git's. A citation names a file and a symbol, never a
line, because a line moves with every edit above it. Three kinds of finding:

- ``history``: a task, review or workstream breadcrumb (``Task 7``, ``Fix round 1``, ``Minor 2``, ``Important 3``,
  ``F1 task 11``, ``WS4b``, ``spec D §7``, ``spec E3``, ``shell spec §15.1``, ``task-3 ruling``, ``the brief``,
  ``this plan's``, ``batch 3`` (not a hyphen-joined tag such as ``conflation-batch-4``), ``first cut`` -- the plans
  and specs live outside the tree), a date, or rename/former narration (``used to``, ``was renamed``).
- ``line-number``: a line number in a citation -- ``path.cpp:N`` (also after ``origin/main:``) or a bare ``:N`` or
  ``:N-M``. Cite the file and the symbol instead.
- ``dangling-path``: a backticked path that names no tracked file, or more than one (write enough of the path to
  name one); with an ``origin/main:`` prefix, a path that is not a file of ``origin/main``. A brace path
  (``dir/x.{h,cpp}``, ``dir/{a,b}.cpp``) names each of its expansions, and each must be such a file. A backticked
  symbol written beside the path -- ``Sym`` (``path``), ``Sym`` in ``path``, or ``path``'s ``Sym`` -- must occur in
  that file (in one of a brace path's files).

A symbol is read beside a path in the forms the citations are written in: ``Sym`` (``path``), (``Sym`` in ``path``) and
``path`` ``Sym``; a name shorter than four characters, or one of a ``A`` / ``B`` pair of aliases, is not checked.

Comments only: ``//`` and ``/* */`` text in C/C++ (``.inc`` too), ``#`` comments in UI scripts, CMake and TOML
files, ``#`` comments and docstrings in Python (Python is tokenized, so a ``#`` inside any other string is not a
comment), all of a Markdown file. Any other ``.txt`` file is data and is not read. An ``origin/main:`` path is read
at the ``main`` commit ``scripts/main_citation_ref`` pins, never at a live ``origin/main``. ``--strict`` exits 1 on
any finding.
"""

from __future__ import annotations

import argparse
import ast
import io
import re
import subprocess
import sys
import tokenize
from dataclasses import dataclass
from functools import cache
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

TREES = (
    "src/ui/shell",
    "src/ui",
    "sim/scenes_spec",
    "tests/unit/ui/shell",
    "tests/unit/ui",
    "tests/unit/deluge/gui",
    "tests/unit/harness",
    "tests/ui-scripts",
    "tests/harness",
    "tests/integration",
    "tools",
    "scripts",
)
# A record by design: the parity ledger lists each accepted difference with its date and the decision taken. The
# checker's own docstring names the breadcrumbs it refuses.
EXEMPT = ("tests/ui-scripts/parity/DIFFERENCES.md", "scripts/check_doc_hygiene.py")
# The guest glue: our code that lives inside the legacy tree, as path prefixes.
GLUE = (
    "src/deluge/gui/ui/shell_",
    "src/deluge/gui/ui/menu_view_of",
    "src/deluge/gui/ui/menu_entry",
    "src/deluge/gui/ui/browser_host",
    "src/deluge/util/menu_entry",
    "src/deluge/util/sound_menu_open",
    "src/deluge/gui/ui/root_knobs",
    "src/deluge/gui/ui/notify_drain",
    "src/deluge/util/note_editor_open",
    "src/deluge/util/sample_preview_open",
    "src/deluge/util/shell_slice_out",
)
SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".inc", ".md", ".txt", ".py", ".cmake", ".toml"}
# Files whose comments start at `#`: the UI scripts, CMake and TOML files, and Python. A `.txt` file elsewhere is data.
HASH_COMMENTS = {".txt", ".py", ".cmake", ".toml"}
UI_SCRIPTS = "tests/ui-scripts/"
# What `origin/main:` names in a citation of a file this tree does not have.
MAIN = "origin/main"
# The `main` commit such a citation is read at: pinned, so that a later `main` cannot turn an unrelated commit red. Each
# merge of `main` into `next` moves it (`docs/dev/roadmap.md`, "Merge train").
MAIN_REF_FILE = REPO / "scripts" / "main_citation_ref"

HISTORY = re.compile(
    r"\b[Tt]ask \d+[a-z]?\b"
    r"|(?i:\bfix round\b)"
    r"|\b(?:Minor|Important) \d+\b"
    r"|\b[A-Z]\d+ task\b"
    r"|\bfinal review\b"
    r"|\bWS\d+[a-z]?(?:-\d+)?\b"
    r"|\b[Ss]pec [A-H](?:\d+)?(?:-[a-z-]+)?\b"
    r"|\b(?:shell|design|migration) spec\b"
    r"|\b[Ss]pec [A-H]?\s?§|\bmigration §"
    r"|\btask-\d+\b|\b[Rr]uling\b"
    r"|\b[Tt]he brief\b|\bbrief's\b"
    r"|\b(?:[Tt]his|[Aa] later|[Tt]he wiring) plan\b|\bplan's\b"
    r"|\b[Tt]his task\b|\bon this branch\b|\bthis branch stack\b"
    r"|\b[Rr]eview round\b|\(Kate,|\b20\d\d-\d\d-\d\d\b"
    r"|(?<!\bis )(?<!\bare )(?<!\bbe )(?<!\bbeen )\bused to\b"
    r"|\bwas renamed\b|\brenamed from\b|\bthe old name\b"
    r"|(?<![-\w])batch[ -]?\d+\b|\bfirst cut\b"
)
# The file types a line citation names: every type a citation in these trees was written against.
LINE_EXTENSIONS = (
    r"(?:h|hpp|cpp|c|inc|py|rs|S|ld|txt|md|toml|cmake|yml|yaml|json|card|cfg)"
)
# `path.ext:N`, or a bare `:N` / `:N-M` at the start or after a space, a bracket, a comma or a backtick.
LINE_NUMBER = re.compile(
    rf"[\w./+-]+\.{LINE_EXTENSIONS}:\d+(?:-\d+)?" r"|(?:^|(?<=[\s(,`])):\d+(?:-\d+)?\b"
)
# The file types a backticked path is checked for.
EXTENSIONS = r"(?:h|hpp|c|cpp|inc|py|rs|txt|md|toml)"
# A whole backticked path, optionally `origin/main:`-prefixed.
PATH = re.compile(rf"`((?:{MAIN}:)?[A-Za-z0-9_][A-Za-z0-9_./+-]*\.{EXTENSIONS})`")
# A backticked brace path, optionally `origin/main:`-prefixed: `dir/x.{h,cpp}` or `dir/{a,b}.cpp`. A path with two
# brace groups, or one with an expansion that is not a citable path, is not checked.
BRACE_PATH = re.compile(
    rf"`((?:{MAIN}:)?)([A-Za-z0-9_./+-]*)\{{([A-Za-z0-9_./+-]+(?:,[A-Za-z0-9_./+-]+)+)\}}([A-Za-z0-9_./+-]*)`"
)
# One expansion of a brace path, held to the shape `PATH` reads.
EXPANDED = re.compile(rf"[A-Za-z0-9_][A-Za-z0-9_./+-]*\.{EXTENSIONS}")
# A symbol as written: possibly qualified, possibly with an argument list. Its last name is what the file must hold.
SYMBOL = re.compile(r"^(?:[A-Za-z_]\w*::)*~?([A-Za-z_]\w*)(?:\s*\(.*\))?$")
# Below this length a name occurs in almost any file by accident, so it is not checked.
MIN_SYMBOL_LEN = 4
# `Sym` (`path`): the symbol ends the text before the path.
SYMBOL_BEFORE = re.compile(r"`([^`]+)`\s*\(\s*$")
# `A` / `B` (`path`): a pair of aliases, which no one path need hold both of.
ALIASES = re.compile(r"`[^`]+`\s*/\s*$")
# `path` `Sym`: the symbol right after the path.
SYMBOL_AFTER = re.compile(r"^\s*`([^`]+)`")
# (`Sym` in `path`)
SYMBOL_IN = re.compile(r"\(\s*`([^`]+)`\s+in\s+$")


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    kind: str
    text: str


def _git(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=REPO, capture_output=True, text=True, check=True
    ).stdout


@cache
def _head_by_suffix() -> dict[str, tuple[str, ...]]:
    """Every tracked file, indexed by every path suffix of itself (`walk.cpp`, `menu/walk.cpp`, ...)."""
    index: dict[str, list[str]] = {}
    for f in _git("ls-files").split("\n"):
        if not f:
            continue
        parts = f.split("/")
        for i in range(len(parts)):
            index.setdefault("/".join(parts[i:]), []).append(f)
    return {k: tuple(v) for k, v in index.items()}


@cache
def _main_ref() -> str:
    """The pinned `main` commit; exits 2 when the file names none or this clone lacks it."""
    try:
        ref = MAIN_REF_FILE.read_text().strip()
    except OSError:
        ref = ""
    found = subprocess.run(
        ["git", "cat-file", "-e", f"{ref}^{{commit}}"],
        cwd=REPO,
        capture_output=True,
        check=False,
    )
    if not ref or found.returncode != 0:
        print(
            f"check_doc_hygiene: cannot read the main commit {ref or '(none)'} that "
            f"{MAIN_REF_FILE.name} pins (git fetch origin {ref or 'main'}); "
            "an `origin/main:` citation needs it",
            file=sys.stderr,
        )
        sys.exit(2)
    return ref


@cache
def _main_files() -> frozenset[str]:
    """Every file of the pinned `main` commit."""
    return frozenset(
        f for f in _git("ls-tree", "-r", "--name-only", _main_ref()).split("\n") if f
    )


@cache
def _text(path: str, on_main: bool) -> str:
    if on_main:
        return _git("show", f"{_main_ref()}:{path}")
    return (REPO / path).read_text(errors="ignore")


def _resolve(cited: str) -> tuple[str, bool] | str:
    """The one file the path @p cited names and whether it is read on `main`, or why it names none."""
    on_main = cited.startswith(f"{MAIN}:")
    if on_main:
        path = cited.removeprefix(f"{MAIN}:")
        if path not in _main_files():
            return f"`{cited}`: no such file in {MAIN}"
        return path, True
    candidates = _head_by_suffix().get(cited, ())
    if not candidates:
        return f"`{cited}`: no tracked file"
    if len(candidates) > 1:
        return f"`{cited}`: {len(candidates)} tracked files end in it"
    return candidates[0], False


def _dangling(written: str, cited: list[str], symbol: str | None) -> list[str]:
    """Why the backticked path @p written, naming the files @p cited, (and the @p symbol beside it) dangles."""
    why = []
    files = []
    for c in cited:
        r = _resolve(c)
        if isinstance(r, str):
            why.append(r)
        else:
            files.append(r)
    if (
        symbol is not None
        and files
        and not any(
            re.search(rf"\b{re.escape(symbol)}\b", _text(path, on_main))
            for path, on_main in files
        )
    ):
        why.append(f"`{written}`: `{symbol}` does not occur in it")
    return why


def _expansions(m: re.Match[str]) -> list[str]:
    """Each path a `BRACE_PATH` match names, or none when an expansion is not a citable path."""
    pin, head, alternatives, tail = m.groups()
    paths = [f"{head}{a}{tail}" for a in alternatives.split(",")]
    if not all(EXPANDED.fullmatch(p) for p in paths):
        return []
    return [f"{pin}{p}" for p in paths]


def _name(written: str) -> str | None:
    """The name a written symbol asks the file to hold, or None when it is too short or not a symbol."""
    m = SYMBOL.match(written.strip())
    return m.group(1) if m is not None and len(m.group(1)) >= MIN_SYMBOL_LEN else None


def _symbol(before: str, after: str, previous: str) -> str | None:
    """The symbol written beside a path, given the comment text @p before and @p after it and the @p previous line's."""
    m = SYMBOL_BEFORE.search(before)
    if m is not None:
        lead = before[: m.start()]
        alias = ALIASES.search(lead) or (
            not lead.strip(" /*#") and ALIASES.search(previous)
        )
        name = None if alias else _name(m.group(1))
        if name is not None:
            return name
    m = SYMBOL_AFTER.match(after) or SYMBOL_IN.search(before)
    return _name(m.group(1)) if m is not None else None


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


def _docstring_rows(text: str) -> set[tuple[int, int]]:
    """The first and last line of every module, class and function docstring in Python source."""
    rows = set()
    for node in ast.walk(ast.parse(text)):
        if isinstance(
            node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)
        ):
            first = node.body[0] if node.body else None
            if (
                isinstance(first, ast.Expr)
                and isinstance(first.value, ast.Constant)
                and isinstance(first.value.value, str)
            ):
                rows.add((first.lineno, first.end_lineno))
    return rows


def _python_comments(text: str) -> dict[int, str] | None:
    """Each line's comment text in Python source, its `#` comment and any docstring on it, by line number, or None
    when the source does not parse."""
    lines = text.splitlines()
    out: dict[int, str] = {}
    try:
        docs = _docstring_rows(text)
        for tok in tokenize.generate_tokens(io.StringIO(text).readline):
            if tok.type == tokenize.COMMENT:
                out[tok.start[0]] = out.get(tok.start[0], "") + tok.string[1:]
            elif tok.type == tokenize.STRING and (tok.start[0], tok.end[0]) in docs:
                for row in range(tok.start[0], tok.end[0] + 1):
                    line = lines[row - 1]
                    start = tok.start[1] if row == tok.start[0] else 0
                    end = tok.end[1] if row == tok.end[0] else len(line)
                    out[row] = out.get(row, "") + line[start:end]
    except (tokenize.TokenError, SyntaxError):
        return None
    return out


def is_exempt(path: str) -> bool:
    return path in EXEMPT


def findings_in(path: str, text: str) -> list[Finding]:
    out: list[Finding] = []
    markdown = path.endswith(".md")
    suffix = Path(path).suffix
    if suffix == ".txt" and not (
        path.startswith(UI_SCRIPTS) or Path(path).name == "CMakeLists.txt"
    ):
        return out
    python = _python_comments(text) if suffix == ".py" else None
    hashed = suffix in HASH_COMMENTS
    in_block = False
    previous = ""
    for n, line in enumerate(text.splitlines(), 1):
        if markdown:
            comment = line
        elif python is not None:
            comment = python.get(n, "")
        elif hashed:
            comment = line.partition("#")[2]
        else:
            comment, in_block = _comment_text(line, in_block)
        if not comment.strip():
            previous = ""
            continue
        if HISTORY.search(comment):
            out.append(Finding(path, n, "history", line.strip()))
        for m in LINE_NUMBER.finditer(comment):
            out.append(Finding(path, n, "line-number", m.group(0)))
        cited = [(m, [m.group(1)]) for m in PATH.finditer(comment)]
        cited += [(m, _expansions(m)) for m in BRACE_PATH.finditer(comment)]
        for m, files in cited:
            if not files:
                continue
            symbol = _symbol(comment[: m.start()], comment[m.end() :], previous)
            for why in _dangling(m.group(0).strip("`"), files, symbol):
                out.append(Finding(path, n, "dangling-path", why))
        previous = comment
    return out


def tracked(paths: tuple[str, ...]) -> list[str]:
    """The tracked files under @p paths (directories, files or path prefixes)."""
    return _git("ls-files", "--", *paths).split()


def scan(paths: tuple[str, ...]) -> list[Finding]:
    out: list[Finding] = []
    for f in tracked(paths):
        if Path(f).suffix not in SUFFIXES or is_exempt(f):
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
    # A scope entry that matches nothing gates nothing; a moved tree or a renamed glue file must move it too.
    empty = [p for p in paths if not tracked((p,))]
    if empty:
        print(
            f"check_doc_hygiene: no tracked file under {', '.join(empty)}",
            file=sys.stderr,
        )
        return 2
    found = scan(paths)
    if a.summary:
        counts: dict[tuple[str, str], int] = {}
        for f in found:
            key = (
                next((t for t in TREES if f.path.startswith(f"{t}/")), "guest glue"),
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
