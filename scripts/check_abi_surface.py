#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The libdeluge C ABI's surface: every declared function, where it is defined, and who calls it.

Declarations are read from ``include/libdeluge/*.h``. A function is *defined* by a Rust ``fn`` in
``src/bsp/rust/src`` or ``crates/*/src``, or by a C/C++ body in ``src/``. Mocks in ``tests/`` are reported
apart, because a mock of a function nobody calls keeps nothing alive.

``--pins`` requires an ``abi_pin!`` for every Rust definition and every Rust ``extern "C"`` import of a declared
function, each by a pin that reaches it (see ``pinned``). A crate's ``#[cfg(test)]`` stubs are exempt.

Callers are counted by name, per area:
- ``app``: ``src/deluge`` and ``src/OSLikeStuff``;
- ``tests``: ``tests/``, excluding the mocks' own definitions;
- ``rust``: ``src/bsp/rust`` and ``crates``, excluding the definitions.

Comments are ignored.

Usage:
    scripts/check_abi_surface.py              # the table
    scripts/check_abi_surface.py --dead       # declared functions nothing calls
    scripts/check_abi_surface.py --check      # exit 1 if a declared function has no definition
    scripts/check_abi_surface.py --pins       # exit 1 if a declared function Rust defines or imports has no abi_pin!
    scripts/check_abi_surface.py NAME...      # the rows for these functions
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADERS = sorted((ROOT / "include" / "libdeluge").glob("*.h"))

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


ABI_PIN = re.compile(r"abi_pin!\s*\(\s*(?:in\s+([\w:]+)\s*:)?([^)]*)\)", re.DOTALL)
# The rename arm, `abi_pin!(sys_name => local_name)`: the two sides are the bindgen name and the item that
# defines it, which differ only when the C name isn't a legal Rust identifier (a keyword). `local_name` then
# spells the C name as a raw identifier (`r#yield`), so stripping the `r#` recovers it.
ABI_PIN_RENAME = re.compile(r"^(\w+)\s*=>\s*(?:r#)?(\w+)$")
BSP_SRC = ROOT / "src" / "bsp" / "rust" / "src"
HARNESS = ROOT / "src" / "bsp" / "rust" / "harness"
CRATES = ROOT / "crates"
# `extern "C" { ... }` (optionally `unsafe`): an import block, as opposed to `extern "C" fn`, a definition.
EXTERN_BLOCK = re.compile(r'\bextern\s+"C"\s*\{')
EXTERN_FN = re.compile(r"\bfn\s+([A-Za-z_]\w*)\s*\(")

# Where a Rust item lives, for matching it to the pin that checks it: the file's repo-relative path for
# `src/bsp/rust/src` and `src/bsp/rust/harness`, whose pins sit beside what they pin, or the crate's directory name for `crates/<dir>/src`,
# which `abi_pins.rs` pins through the `in path:` form.
Scope = str


def crate_names() -> dict[str, Scope]:
    """Every name `src/bsp/rust/src` can spell an in-tree crate by, mapped to that crate's scope: the crate's own
    name, and any `alias = { package = "crate" ... }` rename in `src/bsp/rust/Cargo.toml`."""
    names: dict[str, Scope] = {}
    for manifest in CRATES.glob("*/Cargo.toml"):
        package = re.search(r'(?m)^name\s*=\s*"([^"]+)"', manifest.read_text())
        if package:
            names[package.group(1).replace("-", "_")] = manifest.parent.name
    cargo = (ROOT / "src" / "bsp" / "rust" / "Cargo.toml").read_text()
    for alias, package in re.findall(
        r'(?m)^([\w-]+)\s*=\s*\{[^}\n]*package\s*=\s*"([^"]+)"', cargo
    ):
        crate = package.replace("-", "_")
        if crate in names:
            names[alias.replace("-", "_")] = names[crate]
    return names


def scope_of(path: Path) -> Scope | None:
    """The scope a pin must name to check an item in `path`, or None for a file no pin reaches."""
    if path.is_relative_to(BSP_SRC) or path.is_relative_to(HARNESS):
        return path.relative_to(ROOT).as_posix()
    if path.is_relative_to(CRATES):
        parts = path.relative_to(CRATES).parts
        if len(parts) > 2 and parts[1] == "src":
            return parts[0]
    return None


def pinned() -> dict[str, set[Scope]]:
    """Every C name pinned by an `abi_pin!` in `src/bsp/rust/src` or `src/bsp/rust/harness`, with the scope each pin checks. A plain or
    rename pin checks the item in scope at the invocation: the definition or import in its own file. The
    `in path:` form checks the named crate's item, a definition or an import. A name counts as pinned only for the
    scope its pin reaches, so neither stands in for the other: a crate's import of a function the BSP defines is
    pinned apart from the BSP's definition, and a pin of the import alone leaves the definition unchecked and
    reported. Likewise a pin beside the host definition does not cover the device one in another file."""
    crates = crate_names()
    names: dict[str, set[Scope]] = {}
    for path in RUST:
        if not (path.is_relative_to(BSP_SRC) or path.is_relative_to(HARNESS)):
            continue
        for match in ABI_PIN.finditer(strip_comments(path.read_text(errors="replace"))):
            module, body = match.group(1), match.group(2)
            scope = path.relative_to(ROOT).as_posix()
            if module:
                segments = module.split("::")
                if segments[0] == "crate" and len(segments) > 1:
                    scope = (BSP_SRC / f"{segments[1]}.rs").relative_to(ROOT).as_posix()
                elif segments[0] not in ("self", "super"):
                    scope = crates.get(segments[0], segments[0])
            rename = ABI_PIN_RENAME.match(body.strip())
            if rename:
                names.setdefault(rename.group(2), set()).add(scope)
                continue
            for n in body.split(","):
                if n.strip():
                    names.setdefault(n.strip(), set()).add(scope)
    return names


CFG_TEST = re.compile(r"#\[cfg\(test\)\]")


def strip_cfg_test(text: str) -> str:
    """`text` without its `#[cfg(test)]` items: a crate's unit-test stubs of a header function are linked only into
    that crate's own test binary, where there is no `sys` to pin them against."""
    out, pos = [], 0
    for attr in CFG_TEST.finditer(text):
        if attr.start() < pos:
            continue
        end = attr.end()
        while end < len(text) and text[end] not in "{;":
            end += 1
        if end < len(text) and text[end] == "{":
            depth = 1
            end += 1
            while depth and end < len(text):
                depth += {"{": 1, "}": -1}.get(text[end], 0)
                end += 1
        else:
            end += 1
        out.append(text[pos : attr.start()])
        pos = end
    out.append(text[pos:])
    return "".join(out)


def rust_items(decls: dict[str, Path]) -> dict[str, set[tuple[Scope, str]]]:
    """Every header-declared function a pinnable Rust scope defines or imports: name -> {(scope, "def"|"import")}.
    An import is a `fn` inside an `extern "C" { ... }` block; a definition is what `rust_definition` matches."""
    items: dict[str, set[tuple[Scope, str]]] = {}
    for path in RUST:
        scope = scope_of(path)
        if scope is None:
            continue
        text = strip_cfg_test(strip_comments(path.read_text(errors="replace")))
        for block in EXTERN_BLOCK.finditer(text):
            depth, end = 1, block.end()
            while depth and end < len(text):
                depth += {"{": 1, "}": -1}.get(text[end], 0)
                end += 1
            for fn in EXTERN_FN.finditer(text, block.end(), end):
                if fn.group(1) in decls:
                    items.setdefault(fn.group(1), set()).add((scope, "import"))
        for name in decls:
            if name in text and rust_definition(name).search(text):
                items.setdefault(name, set()).add((scope, "def"))
    return items


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
        items = rust_items(decls)
        for name in names:
            for scope, kind in sorted(items.get(name, ())):
                if scope not in pins.get(name, ()):
                    where = scope if "/" in scope else f"crates/{scope}"
                    unpinned.append(name)
                    print(f"unpinned: {name} ({kind} in {where})")
    if (args.check and undefined) or unpinned:
        if undefined and args.check:
            print(f"{len(undefined)} declared function(s) with no definition")
        if unpinned:
            print(f"{len(unpinned)} Rust definition(s) or import(s) with no abi_pin!")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
