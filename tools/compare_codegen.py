#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Diffs two ELFs' machine code function by function, with addresses normalised away.

A change that should only move code (types renamed, an include repointed) leaves every function's instruction stream
the same; this says which functions differ, appeared or vanished. Branch and call targets keep their symbol, literal
pool words that look like addresses become ADDR, and every other operand, immediates included, is compared as is.

Functions and call targets are named by their demangled name without its parameter list (or a template's return
type), so a function whose signature changes type but whose code doesn't, and every call to it, compare alike.
Overloads that share a name are compared as a set of bodies. ThinLTO's promoted-local suffixes (`.llvm.<hash>`) hash
the module's source, so they are dropped too.

Usage:
    tools/compare_codegen.py BASE.elf NEW.elf [--objdump llvm-objdump] [--show N]
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys

HEADER = re.compile(r"^[0-9a-f]+ <(.+)>:$")
INSN = re.compile(r"^\s*[0-9a-f]+:\s+(.*)$")
ANNOTATION = re.compile(r"\s*@.*$")
TARGET = re.compile(r"0x[0-9a-f]+\s+(<[^>]+>)")
OFFSET = re.compile(r"<([^>+]+)\+0x[0-9a-f]+>")
SYMBOL = re.compile(r"<([^<>]+)>")
LTO_SUFFIX = re.compile(r"\.llvm\.\d+")
ADDRESS_WORD = re.compile(r"^\.word\s+0x([0-9a-f]{8})$")
# SRAM, SDRAM and the uncached mirrors all sit at or above this; constants below it are kept as they are.
ADDRESS_FLOOR = 0x0C000000


def canonical(demangled: str) -> str:
    """A demangled name without its final parameter list, the qualifiers after it, or a leading return type."""
    name = demangled.strip()
    close = name.rfind(")")
    if close > 0 and not name.endswith(">"):
        depth = 0
        for i in range(close, -1, -1):
            depth += {")": 1, "(": -1}.get(name[i], 0)
            if depth == 0:
                name = name[:i]
                break
    depth = 0
    for i in range(len(name) - 1, -1, -1):
        depth += {">": 1, ")": 1, "<": -1, "(": -1}.get(name[i], 0)
        if depth == 0 and name[i] == " " and not name[:i].endswith("operator"):
            return name[i + 1 :]
    return name


def normalise(line: str, rename=lambda name: name) -> str:
    """One disassembled instruction with its address annotations, target addresses and in-function offsets removed,
    and each symbol it names passed through @p rename."""
    text = ANNOTATION.sub("", line.strip())
    text = TARGET.sub(r"\1", text)
    text = OFFSET.sub(r"<\1>", text)
    text = LTO_SUFFIX.sub("", text)
    text = SYMBOL.sub(lambda m: f"<{rename(m.group(1))}>", text)
    text = " ".join(text.split())
    word = ADDRESS_WORD.match(text)
    if word and int(word.group(1), 16) >= ADDRESS_FLOOR:
        return ".word ADDR"
    return text


def functions(
    disassembly: str, rename=lambda name: name
) -> dict[str, list[tuple[str, ...]]]:
    """Each function's normalised instructions, keyed by its name passed through @p rename; the bodies of the
    functions that share a key, sorted."""
    found: dict[str, list[tuple[str, ...]]] = {}
    current: list[str] | None = None
    for line in disassembly.splitlines():
        header = HEADER.match(line)
        if header:
            current = []
            found.setdefault(rename(LTO_SUFFIX.sub("", header.group(1))), []).append(
                current
            )
            continue
        insn = INSN.match(line)
        if insn and current is not None:
            current.append(normalise(insn.group(1), rename))
        elif not line.strip():
            current = None
    return {
        name: sorted(tuple(body) for body in bodies) for name, bodies in found.items()
    }


def compare(
    base: dict[str, list], new: dict[str, list]
) -> tuple[list[str], list[str], list[str]]:
    """The changed, added and removed function names."""
    changed = sorted(n for n in base.keys() & new.keys() if base[n] != new[n])
    return changed, sorted(new.keys() - base.keys()), sorted(base.keys() - new.keys())


def demangler(disassemblies: list[str]):
    """A rename that maps every mangled symbol in @p disassemblies to its canonical() demangled name."""
    names = sorted(
        {
            LTO_SUFFIX.sub("", n)
            for text in disassemblies
            for n in re.findall(r"<([^<>+\s]+)", text)
        }
    )
    demangled = subprocess.run(
        ["llvm-cxxfilt"],
        input="\n".join(names),
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    table = {mangled: canonical(plain) for mangled, plain in zip(names, demangled)}
    return lambda name: table.get(name, name)


def disassemble(objdump: str, elf: str) -> str:
    return subprocess.run(
        [objdump, "-d", "--no-show-raw-insn", elf],
        check=True,
        capture_output=True,
        text=True,
    ).stdout


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("base")
    parser.add_argument("new")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument(
        "--show", type=int, default=20, help="list at most this many names per kind"
    )
    args = parser.parse_args()
    base = disassemble(args.objdump, args.base)
    new = disassemble(args.objdump, args.new)
    rename = demangler([base, new])
    changed, added, removed = compare(functions(base, rename), functions(new, rename))
    for kind, names in (("changed", changed), ("added", added), ("removed", removed)):
        print(f"{kind}: {len(names)}")
        for name in names[: args.show]:
            print(f"  {name}")
    return 1 if changed or added or removed else 0


if __name__ == "__main__":
    sys.exit(main())
