"""Function names to address ranges, from the ELF's symbol table as `nm -C -S` prints it."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
NM_CANDIDATES = (
    "llvm-nm",
    str(REPO / "toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-nm"),
)


@dataclass(frozen=True)
class Symbol:
    """A defined function: its demangled name and its [start, end) addresses, Thumb bit cleared."""

    name: str
    start: int
    end: int

    @property
    def bare(self) -> str:
        """The name without its parameter list: `ns::Class::fn(int)` is `ns::Class::fn`."""
        depth = 0
        for i, ch in enumerate(self.name):
            if ch == "<":
                depth += 1
            elif ch == ">":
                depth -= 1
            elif ch == "(" and depth == 0 and i > 0:
                return self.name[:i]
        return self.name


def parse_nm(text: str) -> list[Symbol]:
    """The sized text symbols in `nm -C -S --defined-only` output."""
    out = []
    for line in text.splitlines():
        parts = line.split(None, 3)
        if len(parts) != 4 or parts[2] not in "tTwW":
            continue
        try:
            start = int(parts[0], 16) & ~1
            size = int(parts[1], 16)
        except ValueError:
            continue
        if size:
            out.append(Symbol(parts[3], start, start + size))
    return out


def lookup(symbols: list[Symbol], name: str) -> Symbol:
    """The one function called `name`, matched with or without its parameter list.

    Raises LookupError naming the candidates when there are none or several (overloads, or a symbol
    the linker kept twice at different addresses).
    """
    exact = [s for s in symbols if s.name == name]
    hits = exact or [s for s in symbols if s.bare == name]
    distinct = {(s.start, s.end): s for s in hits}
    if len(distinct) == 1:
        return next(iter(distinct.values()))
    if not distinct:
        near = sorted({s.name for s in symbols if name.split("::")[-1] in s.name})[:10]
        hint = "; similar: " + ", ".join(near) if near else ""
        raise LookupError(f"no function {name!r} in the ELF{hint}")
    names = ", ".join(f"{s.name} @0x{s.start:08X}" for s in distinct.values())
    raise LookupError(f"{name!r} is ambiguous: {names}; give the full signature")


def at(symbols: list[Symbol], address: int) -> Symbol | None:
    """The function containing `address`."""
    for s in symbols:
        if s.start <= address < s.end:
            return s
    return None


def read_symbols(elf: str | Path) -> list[Symbol]:
    """Run nm over the ELF."""
    last: Exception | None = None
    for nm in NM_CANDIDATES:
        try:
            proc = subprocess.run(
                [nm, "-C", "-S", "--defined-only", str(elf)],
                capture_output=True,
                text=True,
                check=True,
            )
        except (OSError, subprocess.CalledProcessError) as e:
            last = e
            continue
        return parse_nm(proc.stdout)
    raise RuntimeError(f"no nm could read {elf}: {last}")
