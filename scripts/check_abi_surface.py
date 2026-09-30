#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The libdeluge C ABI's surface: every declared function, where it is defined, and who calls it.

Declarations are read from ``include/libdeluge/*.h`` and ``src/OSLikeStuff/scheduler_api.h``. A function is
*defined* by a Rust ``fn`` in ``src/bsp/rust/src`` or ``crates/*/src``, or by a C/C++ body in ``src/``. Mocks in
``tests/`` are reported apart, because a mock of a function nobody calls keeps nothing alive.

Callers are counted by name, per area:
- ``app``: ``src/deluge`` and ``src/OSLikeStuff``;
- ``tests``: ``tests/``, excluding the mocks' own definitions;
- ``rust``: ``src/bsp/rust`` and ``crates``, excluding the definitions.

Comments are ignored.

Usage:
    scripts/check_abi_surface.py              # the table
    scripts/check_abi_surface.py --dead       # declared functions nothing calls
    scripts/check_abi_surface.py --check      # exit 1 if a declared function has no definition
    scripts/check_abi_surface.py --pins       # exit 1 if a Rust-defined declared function has no abi_pin!
    scripts/check_abi_surface.py NAME...      # the rows for these functions
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADERS = sorted((ROOT / "include" / "libdeluge").glob("*.h")) + [
    ROOT / "src" / "OSLikeStuff" / "scheduler_api.h"
]

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//[^\n]*")
# An optional noexcept spelling or `__attribute__((...))` (one level of nested parens allowed, e.g.
# `__attribute__((format(printf, 1, 2)))`), any number of either, in any order, trailing the parameter list.
_QUALIFIER = (
    r"(?:DELUGE_NOEXCEPT|noexcept|__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\))"
)
# A declaration: one to four type words or stars, a name, a parameter list, trailing qualifiers, a `;`.
DECLARATION = re.compile(
    r"(?m)^[ \t]*((?:[A-Za-z_]\w*[ \t\*]+){1,4})([A-Za-z_]\w*)\s*\(([^;{()]|\([^;{()]*\))*\)\s*(?:"
    + _QUALIFIER
    + r"\s*)*;"
)
NOT_A_RETURN_TYPE = {"typedef", "return", "if", "while", "sizeof", "define"}


def strip_comments(text: str) -> str:
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def declared() -> dict[str, Path]:
    names: dict[str, Path] = {}
    for header in HEADERS:
        for match in DECLARATION.finditer(strip_comments(header.read_text())):
            words = match.group(1).split()
            if words and words[0] not in NOT_A_RETURN_TYPE:
                names.setdefault(match.group(2), header)
    return names


def source_files(*roots: str, suffixes: tuple[str, ...]) -> list[Path]:
    files = []
    for root in roots:
        for path in (ROOT / root).rglob("*"):
            if (
                path.suffix in suffixes
                and "target" not in path.parts
                and path.is_file()
            ):
                files.append(path)
    return files


RUST = source_files(
    "src/bsp/rust/src", "src/bsp/rust/harness", "crates", suffixes=(".rs",)
)
CXX_APP = source_files(
    "src/deluge", "src/OSLikeStuff", suffixes=(".c", ".cpp", ".h", ".hpp")
)
CXX_TESTS = source_files("tests", suffixes=(".c", ".cpp", ".h", ".hpp", ".rs"))


def cxx_definition(name: str) -> re.Pattern[str]:
    """A C/C++ body for `name`: a type-like prefix at the start of a line, the name, a parameter list, a `{`."""
    return re.compile(
        r'(?m)^[ \t]*(?:extern\s+"C"\s+)?(?:[A-Za-z_][\w:<>]*[ \t\*&]+)+'
        + re.escape(name)
        + r"\s*\([^;{}]*\)\s*(?:noexcept|DELUGE_NOEXCEPT)?\s*\{"
    )


def rust_definition(name: str) -> re.Pattern[str]:
    """A Rust definition of `name`: `fn name` followed by its signature and a `{` (no `;` in between) -- not a bare
    `fn name(...);` inside an `unsafe extern "C" { ... }` import block, which merely declares an external symbol --
    or any item exported as `name` (`yield` is a Rust keyword, so it is exported by name)."""
    return re.compile(
        r"\bfn\s+"
        + re.escape(name)
        + r"\b[^;{]*\{|export_name\s*=\s*\""
        + re.escape(name)
        + r"\""
    )


ABI_PIN = re.compile(r"abi_pin!\s*\(\s*(?:in\s+[\w:]+\s*:)?([^)]*)\)", re.DOTALL)
# The rename arm, `abi_pin!(sys_name => local_name)`: the two sides are the bindgen name and the item that
# defines it, which differ only when the C name isn't a legal Rust identifier (a keyword). `local_name` then
# spells the C name as a raw identifier (`r#yield`), so stripping the `r#` recovers it.
ABI_PIN_RENAME = re.compile(r"^(\w+)\s*=>\s*(?:r#)?(\w+)$")


def pinned() -> set[str]:
    """Every C name pinned by an `abi_pin!` invocation in `src/bsp/rust/src` (this includes `abi_pins.rs`,
    which pins the crates' exports and the BSP's own imports of them)."""
    names: set[str] = set()
    for path in RUST:
        if "src/bsp/rust/src" not in path.as_posix():
            continue
        for match in ABI_PIN.finditer(strip_comments(path.read_text(errors="replace"))):
            body = match.group(1)
            rename = ABI_PIN_RENAME.match(body.strip())
            if rename:
                names.add(rename.group(2))
                continue
            names.update(n.strip() for n in body.split(",") if n.strip())
    return names


def scan(name: str) -> dict[str, int]:
    word = re.compile(r"\b" + re.escape(name) + r"\b")
    row = {"rust_def": 0, "cxx_def": 0, "mock": 0, "app": 0, "tests": 0, "rust": 0}
    areas = (
        [(p, "rust") for p in RUST]
        + [(p, "app") for p in CXX_APP]
        + [(p, "tests") for p in CXX_TESTS]
    )
    for path, area in areas:
        if path in HEADERS:
            continue
        text = path.read_text(errors="replace")
        if name not in text:
            continue
        text = strip_comments(text)
        definition = (
            rust_definition(name) if path.suffix == ".rs" else cxx_definition(name)
        )
        defs = len(definition.findall(text))
        uses = len(word.findall(text)) - defs
        if area == "tests":
            row["mock"] += defs
        else:
            row["rust_def" if path.suffix == ".rs" else "cxx_def"] += defs
        row[area] += uses
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("names", nargs="*")
    parser.add_argument("--dead", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--pins", action="store_true")
    args = parser.parse_args()

    decls = declared()
    names = args.names or sorted(decls)
    undefined = []
    print(f"{'function':48} {'header':22} def mock  app tests rust")
    for name in names:
        row = scan(name)
        defined = row["rust_def"] + row["cxx_def"]
        callers = row["app"] + row["tests"] + row["rust"]
        if not defined:
            undefined.append(name)
        if args.dead and callers:
            continue
        if args.check and defined:
            continue
        header = decls[name].name if name in decls else "-"
        print(
            f"{name:48} {header:22} {defined:3} {row['mock']:4} {row['app']:4} {row['tests']:5} {row['rust']:4}"
        )
    unpinned = []
    if args.pins:
        pins = pinned()
        unpinned = [n for n in names if scan(n)["rust_def"] and n not in pins]
        for name in unpinned:
            print(f"unpinned: {name}")
    if (args.check and undefined) or unpinned:
        if undefined and args.check:
            print(f"{len(undefined)} declared function(s) with no definition")
        if unpinned:
            print(f"{len(unpinned)} Rust-defined function(s) with no abi_pin!")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
