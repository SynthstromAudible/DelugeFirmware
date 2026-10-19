"""A stdlib reader for the 32-bit little-endian ARM ELF the firmware links: its entry point, loadable
segments and raw (mangled) symbols. It runs where the probe is, which has no toolchain."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

PT_LOAD = 1
PF_X = 1
SHT_SYMTAB = 2


@dataclass(frozen=True)
class Segment:
    """A PT_LOAD segment's file bytes, at the address a debugger's `load` writes them: its physical address."""

    paddr: int
    vaddr: int
    flags: int
    data: bytes

    @property
    def executable(self) -> bool:
        return bool(self.flags & PF_X)


class Elf:
    """One ELF file, read whole."""

    def __init__(self, path: str | Path) -> None:
        self.raw = Path(path).read_bytes()
        if self.raw[:4] != b"\x7fELF" or self.raw[4] != 1 or self.raw[5] != 1:
            raise ValueError(f"{path}: not a 32-bit little-endian ELF")
        (
            _type,
            _machine,
            _version,
            self.entry,
            self._phoff,
            self._shoff,
            _flags,
            _ehsize,
            self._phentsize,
            self._phnum,
            self._shentsize,
            self._shnum,
            self._shstrndx,
        ) = struct.unpack_from("<HHIIIIIHHHHHH", self.raw, 16)

    def segments(self) -> list[Segment]:
        """The PT_LOAD segments with file contents (bss-only segments carry nothing to load)."""
        out = []
        for i in range(self._phnum):
            p_type, off, vaddr, paddr, filesz, _memsz, flags, _align = (
                struct.unpack_from(
                    "<IIIIIIII", self.raw, self._phoff + i * self._phentsize
                )
            )
            if p_type == PT_LOAD and filesz:
                out.append(Segment(paddr, vaddr, flags, self.raw[off : off + filesz]))
        return out

    def _sections(self) -> list[tuple[int, ...]]:
        return [
            struct.unpack_from(
                "<IIIIIIIIII", self.raw, self._shoff + i * self._shentsize
            )
            for i in range(self._shnum)
        ]

    def section(self, name: str) -> tuple[int, int] | None:
        """The address range [start, end) of the section called `name`, or None."""
        sections = self._sections()
        names_off = sections[self._shstrndx][4]
        want = name.encode() + b"\0"
        for sh in sections:
            start = names_off + sh[0]
            if self.raw[start : start + len(want)] == want:
                return sh[3], sh[3] + sh[5]
        return None

    def symbol(self, name: str) -> int | None:
        """The value of the first symbol named exactly `name` (mangled), or None."""
        sections = self._sections()
        for sh in sections:
            if sh[1] != SHT_SYMTAB:
                continue
            _name, _t, _f, _a, off, size, link, _i, _al, entsize = sh
            strtab = sections[link]
            str_off = strtab[4]
            want = name.encode()
            for k in range(size // entsize):
                st_name, st_value = struct.unpack_from(
                    "<II", self.raw, off + k * entsize
                )
                start = str_off + st_name
                if self.raw[start : start + len(want) + 1] == want + b"\0":
                    return st_value
        return None
