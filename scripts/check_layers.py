#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The include-layer checker: every include in the tree against scripts/layers.toml.

The table holds four kinds of entry, and this script checks each:

- a library ([[library]]) may include only the C++ standard library, the system headers it lists, its own headers
  and the dependencies it declares. A file belongs to the deepest library whose root holds it. A quoted include is
  read as the compiler finds it: beside the including file first, then as written. Any #include that is not a plain
  <name> or "name" is a violation, so a macro or a comment cannot slip a header past the check, and so is any path
  with "..";
- a rule ([[rule]]) counts the includes matching its pattern in the files its globs cover. With transitive = true it
  follows every quoted include through the include roots and counts each forbidden header the file reaches. A count
  may not rise above scripts/layers_baseline.txt; a rule with no baseline row is held at zero;
- the tree ([tree] and [[directory]]): no directory under src/ that the table does not list, no file at the src/
  root it does not list, and no file under src/ at a path the legacy tree also has, because the legacy root precedes
  src/ on the include path and one of the two would be shadowed;
- the legacy tree's source-file count, which may only fall.

Usage:
    scripts/check_layers.py            # check, exit 1 on any violation
    scripts/check_layers.py --list     # print every count as file<TAB>rule<TAB>count and exit 0
    scripts/check_layers.py --update   # rewrite the baseline; refuses to raise a shrink-only count
"""

from __future__ import annotations

import functools
import re
from dataclasses import dataclass
from pathlib import Path

import tomllib

REPO = Path(__file__).resolve().parent.parent
SUFFIXES = frozenset(
    {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".hxx",
        ".inc",
        ".inl",
        ".ipp",
        ".tcc",
        ".s",
        ".S",
    }
)
DIRECTIVE = re.compile(r"^\s*#\s*include\b")
PLAIN_INCLUDE = re.compile(
    r'^\s*#\s*include\s*(?:<([^<>"]+)>|"([^<>"]+)")\s*(?://.*)?$'
)
ANY_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[>"]')
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')

# The C++23 standard library headers, including the C compatibility headers (<cstdint>, not <stdint.h>).
STANDARD_HEADERS = frozenset(
    [
        "algorithm",
        "any",
        "array",
        "atomic",
        "barrier",
        "bit",
        "bitset",
        "charconv",
        "chrono",
        "codecvt",
        "compare",
        "complex",
        "concepts",
        "condition_variable",
        "coroutine",
        "deque",
        "exception",
        "execution",
        "expected",
        "filesystem",
        "flat_map",
        "flat_set",
        "format",
        "forward_list",
        "fstream",
        "functional",
        "future",
        "generator",
        "initializer_list",
        "iomanip",
        "ios",
        "iosfwd",
        "iostream",
        "istream",
        "iterator",
        "latch",
        "limits",
        "list",
        "locale",
        "map",
        "mdspan",
        "memory",
        "memory_resource",
        "mutex",
        "new",
        "numbers",
        "numeric",
        "optional",
        "ostream",
        "print",
        "queue",
        "random",
        "ranges",
        "ratio",
        "regex",
        "scoped_allocator",
        "semaphore",
        "set",
        "shared_mutex",
        "source_location",
        "span",
        "spanstream",
        "sstream",
        "stack",
        "stacktrace",
        "stdexcept",
        "stdfloat",
        "stop_token",
        "streambuf",
        "string",
        "string_view",
        "strstream",
        "syncstream",
        "system_error",
        "thread",
        "tuple",
        "type_traits",
        "typeindex",
        "typeinfo",
        "unordered_map",
        "unordered_set",
        "utility",
        "valarray",
        "variant",
        "vector",
        "version",
        "cassert",
        "cctype",
        "cerrno",
        "cfenv",
        "cfloat",
        "cinttypes",
        "climits",
        "clocale",
        "cmath",
        "csetjmp",
        "csignal",
        "cstdarg",
        "cstddef",
        "cstdint",
        "cstdio",
        "cstdlib",
        "cstring",
        "ctime",
        "cuchar",
        "cwchar",
        "cwctype",
    ]
)


@dataclass(frozen=True)
class Library:
    """A declared-dependency library: where it lives, its own include prefix, and what else it may include.

    A may_include entry ending in "/" admits every header under it; any other entry admits that one header. system
    lists the non-C++ headers (C headers, intrinsics) it may include in angle brackets.
    """

    name: str
    root: str
    prefix: str
    may_include: tuple[str, ...] = ()
    system: tuple[str, ...] = ()


@dataclass(frozen=True)
class Rule:
    """A forbidden include pattern, counted per file over the files its globs cover."""

    name: str
    files: tuple[str, ...]
    pattern: str
    exclude: tuple[str, ...] = ()
    allow: tuple[str, ...] = ()
    transitive: bool = False
    shrink_only: bool = False
    reason: str = ""


@dataclass(frozen=True)
class Tree:
    include_roots: tuple[str, ...]
    legacy: str
    scan_roots: tuple[str, ...]
    directories: tuple[str, ...]
    root_files: tuple[str, ...]


@dataclass(frozen=True)
class Config:
    tree: Tree
    libraries: tuple[Library, ...]
    rules: tuple[Rule, ...]


def load_config(path: Path) -> Config:
    raw = tomllib.loads(path.read_text())
    t = raw.get("tree", {})
    tree = Tree(
        include_roots=tuple(t.get("include_roots", ("src/deluge", "src"))),
        legacy=t.get("legacy", "src/deluge"),
        scan_roots=tuple(t.get("scan_roots", ("src", "sim", "tests"))),
        directories=tuple(d["path"] for d in raw.get("directory", [])),
        root_files=tuple(t.get("root_files", ())),
    )
    libraries = tuple(
        Library(
            name=entry["name"],
            root=entry["root"],
            prefix=entry["prefix"],
            may_include=tuple(entry.get("may_include", ())),
            system=tuple(entry.get("system", ())),
        )
        for entry in raw.get("library", [])
    )
    rules = tuple(
        Rule(
            name=entry["name"],
            files=tuple(entry["files"]),
            pattern=entry["pattern"],
            exclude=tuple(entry.get("exclude", ())),
            allow=tuple(entry.get("allow", ())),
            transitive=entry.get("transitive", False),
            shrink_only=entry.get("shrink_only", False),
            reason=entry.get("reason", ""),
        )
        for entry in raw.get("rule", [])
    )
    names = [entry.name for entry in libraries] + [entry.name for entry in rules]
    repeated = sorted({name for name in names if names.count(name) > 1})
    if repeated:
        raise ValueError(f"{path}: names used more than once: {', '.join(repeated)}")
    for rule in rules:
        re.compile(rule.pattern)
    return Config(tree, libraries, rules)


def source_files(repo: Path, root: str) -> list[Path]:
    base = repo / root
    if not base.is_dir():
        return []
    return sorted(p for p in base.rglob("*") if p.is_file() and p.suffix in SUFFIXES)


@functools.cache
def read_lines(path: Path) -> tuple[str, ...]:
    return tuple(path.read_text(encoding="utf-8", errors="replace").splitlines())


def relative(repo: Path, path: Path) -> str:
    return path.relative_to(repo).as_posix()


def owner(config: Config, rel_path: str) -> Library | None:
    """The deepest library whose root holds @p rel_path."""
    best = None
    for library in config.libraries:
        if rel_path.startswith(library.root.rstrip("/") + "/") and (
            best is None or len(library.root) > len(best.root)
        ):
            best = library
    return best


def logical_of(repo: Path, config: Config, resolved: Path) -> str | None:
    """@p resolved relative to the first include root that holds it, or None outside them all."""
    for root in config.tree.include_roots:
        base = (repo / root).resolve()
        if resolved.is_relative_to(base):
            return resolved.relative_to(base).as_posix()
    return None


def logical_path(repo: Path, config: Config, including: Path, quoted: str) -> str:
    """A quoted include as the compiler finds it: the file beside @p including if there is one, else as written."""
    beside = including.parent / quoted
    if beside.is_file():
        logical = logical_of(repo, config, beside.resolve())
        if logical is not None:
            return logical
    return quoted


def library_allows(library: Library, logical: str) -> bool:
    if logical.startswith(library.prefix):
        return True
    return any(
        logical.startswith(dep) if dep.endswith("/") else logical == dep
        for dep in library.may_include
    )


def library_violations(config: Config, repo: Path) -> list[str]:
    found = []
    for library in config.libraries:
        for path in source_files(repo, library.root):
            rel_path = relative(repo, path)
            if owner(config, rel_path) is not library:
                continue
            for number, line in enumerate(read_lines(path), start=1):
                if not DIRECTIVE.match(line):
                    continue
                match = PLAIN_INCLUDE.match(line)
                ok = False
                if match is not None:
                    angle, quoted = match.groups()
                    if angle is not None:
                        ok = angle in STANDARD_HEADERS or angle in library.system
                    elif ".." not in Path(quoted).parts:
                        ok = library_allows(
                            library, logical_path(repo, config, path, quoted)
                        )
                if not ok:
                    found.append(f"{library.name}: {rel_path}:{number}: {line.strip()}")
    return found
