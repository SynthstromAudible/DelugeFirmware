"""Checks, after the link, that the zero-initialised sections the boot clears hold only zero-initialised objects.

`.sdram_bss` and `.frunk_bss` (src/bsp/rust/linker/sdram_sections.x) take the objects placed with PLACE_SDRAM_BSS and
PLACE_INTERNAL_FRUNK, and the boot zeroes them (src/bsp/rust/src/boot_mem.rs). An object whose constructor the
compiler evaluates at build time gets a non-zero initial value in its object file instead of a constructor run at boot;
in a section the boot zeroes, that value is lost and the object starts as zeros. The linker script keeps these
sections' content (they are not NOLOAD), so this reads it, names the objects whose bytes are not zero (they belong in
PLACE_SDRAM_DATA, whose content the boot copies in), and then makes the sections NOBITS, as the image has them.
"""

import struct
import subprocess
import tempfile
from pathlib import Path

ZERO_SECTIONS = (".sdram_bss", ".frunk_bss")
SHT_NOBITS = 8


class NonZeroObjects(Exception):
    """A zero-initialised section holds objects with a non-zero initial value."""


def _sections(readelf: Path, elf: Path) -> dict[str, tuple[str, int, int]]:
    """Each section's type, address and size, by name."""
    out = subprocess.run(
        [str(readelf), "-SW", str(elf)], check=True, capture_output=True, text=True
    ).stdout
    found = {}
    for line in out.splitlines():
        line = line.replace("[ ", "[")
        fields = line.split()
        if len(fields) > 5 and fields[0].startswith("[") and fields[1] in ZERO_SECTIONS:
            found[fields[1]] = (fields[2], int(fields[3], 16), int(fields[5], 16))
    return found


def _symbols(nm: Path, elf: Path) -> list[tuple[int, int, str]]:
    """The ELF's sized symbols: (address, size, demangled name)."""
    out = subprocess.run(
        [str(nm), "-S", "-C", str(elf)], check=True, capture_output=True, text=True
    ).stdout
    symbols = []
    for line in out.splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) == 4:
            symbols.append((int(fields[0], 16), int(fields[1], 16), fields[3]))
    return symbols


def _make_nobits(elf: Path, names: list[str]) -> None:
    """Sets the ELF32 section type of each of @p names to SHT_NOBITS, in place (objcopy has no option for it)."""
    data = bytearray(elf.read_bytes())
    (shoff,) = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    (strtab_offset,) = struct.unpack_from(
        "<I", data, shoff + shstrndx * shentsize + 0x10
    )
    for i in range(shnum):
        header = shoff + i * shentsize
        (name_offset,) = struct.unpack_from("<I", data, header)
        end = data.index(0, strtab_offset + name_offset)
        if data[strtab_offset + name_offset : end].decode() in names:
            struct.pack_into("<I", data, header + 4, SHT_NOBITS)
    elf.write_bytes(bytes(data))


def check(elf: Path, readelf: Path, objcopy: Path, nm: Path) -> None:
    """Raises NonZeroObjects naming every object in a zero-initialised section that has non-zero bytes; else makes the
    sections NOBITS, in place."""
    problems = []
    to_clear = []
    for name, (kind, address, size) in _sections(readelf, elf).items():
        if kind == "NOBITS":
            continue
        to_clear.append(name)
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "section.bin"
            subprocess.run(
                [
                    str(objcopy),
                    "-O",
                    "binary",
                    f"--only-section={name}",
                    str(elf),
                    str(out),
                ],
                check=True,
            )
            content = out.read_bytes()
        if not any(content):
            continue
        named = set()
        for sym_address, sym_size, sym_name in _symbols(nm, elf):
            start = sym_address - address
            if 0 <= start < size and any(content[start : start + sym_size]):
                named.add(sym_name)
        problems.append(
            f"{name}: "
            + (", ".join(sorted(named)) if named else "(no symbol covers the bytes)")
        )
    if problems:
        raise NonZeroObjects(
            "objects with a non-zero initial value in a section the boot zeroes; place them with "
            "PLACE_SDRAM_DATA:\n  " + "\n  ".join(problems)
        )
    if to_clear:
        _make_nobits(elf, to_clear)
