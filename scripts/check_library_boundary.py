#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Boundary check for the standalone libraries: each may include only what it declares.

A file in a library may include C++ standard-library headers (<name>, from the set below), its own headers (its
prefix, no "..") and the dependencies its LIBRARIES entry declares. Any #include line that is not in one of those plain
forms is a violation, so a macro, a comment or path traversal cannot slip an undeclared header past the check. There
is no allowlist: each library is clean by construction, and code that still needs another header is not ready to move
into it.

Usage:
    scripts/check_library_boundary.py                              # check every library, exit 1 on a violation
    scripts/check_library_boundary.py --library NAME               # check one
    scripts/check_library_boundary.py --library NAME --root DIR    # check DIR under NAME's rules
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DIRECTIVE = re.compile(r"^\s*#\s*include\b")
INCLUDE = re.compile(r'^\s*#\s*include\s*(?:<([^<>"]+)>|"([^<>"]+)")\s*(?://.*)?$')
SUFFIXES = {
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ipp",
    ".tcc",
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
}
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
    """A library under the check: where it lives, the prefix of its own headers, and what else it may include.

    `may_include` entries ending in "/" admit every header under that directory; others admit that one header.
    """

    root: Path
    own_prefix: str
    may_include: tuple[str, ...] = ()


LIBRARIES: dict[str, Library] = {
    "engine-types": Library(REPO / "src" / "engine-api" / "types", "engine-api/types/"),
    "serialization": Library(
        REPO / "src" / "serialization", "serialization/", ("foundation/panic.h",)
    ),
    "music-theory": Library(
        REPO / "src" / "music-theory", "music-theory/", ("foundation/panic.h",)
    ),
    "model": Library(
        REPO / "src" / "deluge-project",
        "deluge-project/",
        ("engine-api/types/", "serialization/", "music-theory/", "foundation/panic.h"),
    ),
}


def allowed(library: Library, line: str) -> bool:
    match = INCLUDE.match(line)
    if match is None:
        return False
    standard, quoted = match.groups()
    if standard is not None:
        return standard in STANDARD_HEADERS
    if ".." in Path(quoted).parts:
        return False
    if quoted.startswith(library.own_prefix):
        return True
    return any(
        quoted.startswith(dep) if dep.endswith("/") else quoted == dep
        for dep in library.may_include
    )


def violations(library: Library, root: Path | None = None) -> list[str]:
    root = library.root if root is None else root
    found = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in SUFFIXES:
            continue
        for number, line in enumerate(path.read_text().splitlines(), start=1):
            if DIRECTIVE.match(line) and not allowed(library, line):
                found.append(
                    f"{path.relative_to(root.parent)}:{number}: {line.strip()}"
                )
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--library", choices=sorted(LIBRARIES))
    parser.add_argument("--root", type=Path)
    args = parser.parse_args()
    if args.root is not None and args.library is None:
        parser.error("--root needs --library")
    names = [args.library] if args.library else sorted(LIBRARIES)
    total = 0
    for name in names:
        found = violations(LIBRARIES[name], args.root)
        for line in found:
            print(f"{name}: {line}")
        total += len(found)
    if total:
        print(f"FAIL: {total} include(s) outside a library boundary")
        return 1
    print("OK: every library includes only what it may")
    return 0


if __name__ == "__main__":
    sys.exit(main())
