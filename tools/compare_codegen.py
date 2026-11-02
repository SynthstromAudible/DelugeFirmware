#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Diffs two ELFs' machine code function by function, with everything the layout sets masked.

When any function changes size, the code and data after it move, and every address that names them changes with
them. This masks those so that what remains is a change in the instructions themselves:
- branch and call targets keep only their symbol;
- a literal-pool word in the RAM's address range becomes ADDR, and a pc-relative load or adr loses its offset;
- a `movw`/`movt` pair that builds an address (its `movt` half in the RAM's range) loses both immediates;
- the linker's trap fill between functions is dropped.
Every other operand, immediates included, compares as is. A constant built in a register the function also builds an
address in is masked with it.

Functions and call targets are named by their demangled name without its parameter list (or a template's return
type), so a function whose signature changes type but whose code doesn't, and every call to it, compare alike.
Overloads that share a name are compared as a set of bodies. ThinLTO's promoted-local suffixes (`.llvm.<hash>`) hash
the module's source, so they are dropped too.

A ThinLTO image's code is not fixed by its own source alone: a change in one module can move import and inlining
decisions elsewhere. A non-empty result is classified by reading each function's diff (`--diff NAME`), not taken as
a verdict.

Usage:
    tools/compare_codegen.py BASE.elf NEW.elf [--objdump llvm-objdump] [--show N] [--diff NAME ...]
"""

from __future__ import annotations

import argparse
import difflib
import re
import subprocess
import sys

HEADER = re.compile(r"^[0-9a-f]+ <(.+)>:$")
INSN = re.compile(r"^\s*[0-9a-f]+:\s+(.*)$")
ANNOTATION = re.compile(r"\s*@.*$")
RAW_BYTES = re.compile(r"^(?:[0-9a-f]{2} )+\s*")
TARGET = re.compile(r"0x[0-9a-f]+\s+(<[^>]+>)")
OFFSET = re.compile(r"<([^>+]+)\+0x[0-9a-f]+>")
SYMBOL = re.compile(r"<([^<>]+)>")
LTO_SUFFIX = re.compile(r"\.llvm\.\d+")
ADDRESS_WORD = re.compile(r"^\.word\s+0x([0-9a-f]{8})$")
PC_RELATIVE = re.compile(r"\[pc, #-?\w+\]")
ADR = re.compile(r"^(adr\S*\s+\w+), #-?\w+")
MOV_HALF = re.compile(r"^(mov[wt])(\S*) (\w+), #0x([0-9a-f]+)$")
# lld fills the gaps between functions with 0xd4: `bmi .-0x54` as Thumb, `ldrble sp, [r4], #1236` as ARM. The fill's
# length follows the layout, so a function's trailing fill is dropped.
PADDING = re.compile(r"^\s*(?:bmi\s.*@ imm = #-0x58|ldrble\s+sp, \[r4\], #1236)\s*$")
# SRAM, SDRAM and the uncached mirrors all sit at or above this; constants below it are kept as they are.
ADDRESS_FLOOR = 0x0C000000


def keep(name: str) -> str:
    return name


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


def normalise(line: str, rename=keep) -> str:
    """One disassembled instruction with its raw bytes, annotations, target addresses, in-function offsets, pc-relative
    offsets and address-range literal words removed, and each symbol it names passed through @p rename."""
    text = RAW_BYTES.sub("", ANNOTATION.sub("", line.strip()))
    text = TARGET.sub(r"\1", text)
    text = OFFSET.sub(r"<\1>", text)
    text = LTO_SUFFIX.sub("", text)
    text = SYMBOL.sub(lambda m: f"<{rename(m.group(1))}>", text)
    text = PC_RELATIVE.sub("[pc]", text)
    text = ADR.sub(r"\1", text)
    text = " ".join(text.split())
    word = ADDRESS_WORD.match(text)
    if word and int(word.group(1), 16) >= ADDRESS_FLOOR:
        return ".word ADDR"
    return text


def mask_address_halves(body: list[str]) -> list[str]:
    """@p body with the immediates of every `movw`/`movt` masked in a register that a `movt` loads with the high half
    of an address."""
    addressed = set()
    for insn in body:
        half = MOV_HALF.match(insn)
        if (
            half
            and half.group(1) == "movt"
            and int(half.group(4), 16) << 16 >= ADDRESS_FLOOR
        ):
            addressed.add(half.group(3))
    out = []
    for insn in body:
        half = MOV_HALF.match(insn)
        if half and half.group(3) in addressed:
            insn = f"{half.group(1)}{half.group(2)} {half.group(3)}, ADDR"
        out.append(insn)
    return out


def functions(disassembly: str, rename=keep) -> dict[str, list[tuple[str, ...]]]:
    """Each function's normalised instructions, keyed by its name passed through @p rename; the bodies of the
    functions that share a key, sorted."""
    found: dict[str, list[list[str]]] = {}
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
            current.append(insn.group(1))
        elif not line.strip():
            current = None
    result = {}
    for name, bodies in found.items():
        normalised = []
        for body in bodies:
            while body and PADDING.match(body[-1]):
                body.pop()
            normalised.append(
                tuple(mask_address_halves([normalise(insn, rename) for insn in body]))
            )
        result[name] = sorted(normalised)
    return result


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
    parser.add_argument(
        "--diff",
        nargs="*",
        default=[],
        help="print these changed functions' masked diffs",
    )
    args = parser.parse_args()
    base = disassemble(args.objdump, args.base)
    new = disassemble(args.objdump, args.new)
    rename = demangler([base, new])
    base_functions, new_functions = functions(base, rename), functions(new, rename)
    changed, added, removed = compare(base_functions, new_functions)
    for kind, names in (("changed", changed), ("added", added), ("removed", removed)):
        print(f"{kind}: {len(names)}")
        for name in names[: args.show]:
            print(f"  {name}")
    for name in args.diff:
        print(f"== {name}")
        for before, after in zip(
            base_functions.get(name, []), new_functions.get(name, [])
        ):
            for line in difflib.unified_diff(before, after, lineterm="", n=1):
                print(line)
    return 1 if changed or added or removed else 0


if __name__ == "__main__":
    sys.exit(main())
