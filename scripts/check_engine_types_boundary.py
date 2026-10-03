#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Boundary check for src/engine-api/types/, the data-only types of the host–engine contract.

A file there may include only C++ standard-library headers (<name>, from the set below) and other types-layer headers
("engine-api/types/...", no ".."). Any #include line that is not in one of those two plain forms is a violation, so a
macro, a comment or path traversal cannot slip an app header past the check. There is no allowlist: the layer is
clean by construction, and a type that still needs another header is not ready to move here.

Usage:
    scripts/check_engine_types_boundary.py              # check src/engine-api/types, exit 1 on a violation
    scripts/check_engine_types_boundary.py --root DIR   # check DIR instead (test_check_engine_types_boundary.py does)
"""

from __future__ import annotations

import argparse
import re
import sys
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
OWN_PREFIX = "engine-api/types/"

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


def allowed(line: str) -> bool:
    match = INCLUDE.match(line)
    if match is None:
        return False
    standard, quoted = match.groups()
    if standard is not None:
        return standard in STANDARD_HEADERS
    return quoted.startswith(OWN_PREFIX) and ".." not in Path(quoted).parts


def violations(root: Path) -> list[str]:
    found = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in SUFFIXES:
            continue
        for number, line in enumerate(path.read_text().splitlines(), start=1):
            if DIRECTIVE.match(line) and not allowed(line):
                found.append(
                    f"{path.relative_to(root.parent)}:{number}: {line.strip()}"
                )
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--root", type=Path, default=REPO / "src" / "engine-api" / "types"
    )
    args = parser.parse_args()
    found = violations(args.root)
    for line in found:
        print(line)
    if found:
        print(f"FAIL: {len(found)} include(s) outside the engine-api types boundary")
        return 1
    print("OK: engine-api types include only the standard library and themselves")
    return 0


if __name__ == "__main__":
    sys.exit(main())
