#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Diffs the printable strings two device images load (.rodata, .data and the SDRAM content inflated from
.sdram_init_pack), after renaming paths and crates in the first image's strings. A string is a run of printable ASCII of
at least six characters, or of at least three when a NUL byte ends it (a C string).

Data objects are no strings, however their bytes print: an SRAM address ends in `2X 20`, so a vtable, a type-info
object, a table of pointers or a code literal pool reads as text, with a NUL after it as a C string, and its pointers
change whenever what they point at moves. So every 4-byte-aligned word (aligned by its load address) that lies inside a
sized symbol other than a string (local labels such as `.str.N` and `.Lanon.…`, which hold string literals, and
type-info names `_ZTS…` are strings) and names a loaded symbol (its start, with or without the Thumb bit, or a place
inside a vtable or construction vtable) is masked before the runs are read. A type-info name after its object's vtable
pointer is then read alone, while text in a named char array that only reads as an address (` ` is 0x20202020) is still
read.

A Rust string has no NUL after it, so when a pointer follows it (a `&str`'s data pointer, in a panic location) the
pointer's printable low bytes join the run, and they change whenever the text they point at moves. So a run no NUL ends,
that ends inside or at the end of a 4-byte-aligned word (aligned by the section's load address; the inflated SDRAM
content's is `.sdram_init`'s) holding an address inside a LOAD segment or the SDRAM content where a string of at least
eight characters starts (the byte before it is no text), ends where that word starts; cut short, it needs six
characters. A word the run reads on past is text, and a C string is never cut. Bytes that only read as an address into
code, zeroed memory, the middle of a string or a lone printable byte (a Rust format string's encoded arguments, a jump
table) are text.

Equal multisets after the renames show that every string found this way is unchanged. They do not show:
- that the bytes of a word cut or masked as a pointer are unchanged (that word is not compared);
- anything about a run shorter than six characters that no NUL ends, including one a pointer cut made that short;
- anything about tables and other non-text data, which bytes_check covers.

The renames are trusted: each is a substring replacement on the first image's strings, so a changed string that
contains renamed text compares equal to the same string with the new text. That is why the renames are exact moved
paths and crate names. Each string is rewritten in one pass: at every position the first matching rename wins, and
its output is not renamed again.

Usage: tools/compare_strings.py OLD.elf NEW.elf [--rename OLD=NEW ...] [--objcopy llvm-objcopy]
                                [--readelf PATH] [--nm PATH]
"""

from __future__ import annotations

import argparse
import bisect
import collections
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

# The fewest characters of the string a pointer word must point at the start of (a `&str` after a Rust string points
# at a file path, a type name or a message).
STRING_START = 8
PRINTABLE = re.compile(rb"[\x20-\x7e]{3,}(?=\x00)|[\x20-\x7e]{6,}")
SECTIONS = (".rodata", ".data")


def printable(byte: int) -> bool:
    return 0x20 <= byte <= 0x7E


def section(objcopy: str, elf: str, name: str) -> bytes:
    """The raw bytes of section @p name in @p elf."""
    with tempfile.NamedTemporaryFile() as out:
        subprocess.run(
            [objcopy, "-O", "binary", f"--only-section={name}", elf, out.name],
            check=True,
        )
        return Path(out.name).read_bytes()


def sdram(objcopy: str, elf: str) -> bytes:
    """The SDRAM content the boot copies out of .sdram_init_pack: two little-endian words (inflated size, deflated
    size), then the deflated bytes."""
    pack = section(objcopy, elf, ".sdram_init_pack")
    if len(pack) < 8:
        return b""
    raw, deflated = struct.unpack("<II", pack[:8])
    data = zlib.decompress(pack[8 : 8 + deflated])
    if len(data) != raw:
        raise ValueError(
            f"{elf}: .sdram_init_pack inflates to {len(data)} B, its header says {raw}"
        )
    return data


def layout(readelf: str, elf: str) -> tuple[dict[str, int], list[tuple[int, int]]]:
    """Where @p elf loads what strings() reads (section name -> address; `.sdram_init` is where the inflated SDRAM
    content is linked), and the address ranges it loads: its LOAD segments and the inflated content's range."""
    out = subprocess.run(
        [readelf, "-lSW", elf], check=True, capture_output=True, text=True
    ).stdout
    at, ranges = {}, []
    for line in out.splitlines():
        fields = line.split()
        if fields[:1] == ["LOAD"] and int(fields[5], 16):
            ranges.append((int(fields[2], 16), int(fields[2], 16) + int(fields[5], 16)))
        header = re.search(
            r"\]\s+(\S+)\s+\S+\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)", line
        )
        if header:
            at[header.group(1)] = int(header.group(2), 16)
    return at, ranges


def data_objects(nm: str, elf: str) -> list[tuple[int, int]]:
    """The address ranges of @p elf's sized symbols that are no string: every one but local labels (`.str.N`,
    `.Lanon.…`, which hold string literals), type-info names (`_ZTS…`) and mapping symbols."""
    out = subprocess.run(
        [nm, "-S", "-n", "--defined-only", elf],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    found = []
    for line in out.splitlines():
        fields = line.split()
        if (
            len(fields) == 4
            and int(fields[1], 16)
            and not fields[3].startswith((".", "_ZTS", "$"))
        ):
            start = int(fields[0], 16) & ~1
            found.append((start, start + int(fields[1], 16)))
    return found


def pointer_targets(nm: str, elf: str) -> set[int]:
    """Where @p elf's pointers point: every defined symbol's start, that start with the Thumb bit set, and each
    4-byte-aligned place inside a vtable or construction vtable (`_ZTV…`, `_ZTC…`), where objects and VTTs point at the
    sub-tables a class with bases has."""
    out = subprocess.run(
        [nm, "-S", "-n", "--defined-only", elf],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    found = set()
    for line in out.splitlines():
        fields = line.split()
        if (
            len(fields) >= 3
            and re.fullmatch(r"[0-9a-f]+", fields[0])
            and not fields[-1].startswith("$")
        ):
            start = int(fields[0], 16) & ~1
            found |= {start, start | 1}
            if fields[-1].startswith(("_ZTV", "_ZTC")) and len(fields) == 4:
                found |= set(range(start, start + int(fields[1], 16) + 1, 4))
    return found


def unpointered(
    blob: bytes, base: int, targets: set[int], objects: list[tuple[int, int]]
) -> bytes:
    """@p blob, loaded at @p base, with each 4-byte-aligned word inside one of @p objects (data_objects()) that holds
    one of @p targets (pointer_targets()) made non-text: a vtable, a type-info object, a table of pointers or a code
    literal pool is no string, however its pointers' bytes print (an SRAM address ends in `2X 20`). A word that only
    reads as an address in the image (`    ` in a `static const char` table is 0x20202020) is text. A word that points
    into its own object (a list head) is masked too, and one into another of @p objects (`&table[i]`) unless each of
    its bytes prints or is a NUL, when it may be such text (a char table's `\0\0  ` is 0x20200000)."""
    out = bytearray(blob)
    ordered = sorted(objects)
    starts = [start for start, _ in ordered]

    def inside_an_object(value: int) -> bool:
        i = bisect.bisect_right(starts, value) - 1
        return i >= 0 and value < ordered[i][1]

    for start, end in objects:
        lo, hi = max(start, base), min(end, base + len(blob))
        for at in range(lo + (-lo % 4), hi - 3, 4):
            word = blob[at - base : at - base + 4]
            value = struct.unpack("<I", word)[0]
            text = all(b == 0 or printable(b) for b in word)
            if (
                value in targets
                or start <= value < end
                or (inside_an_object(value) and not text)
            ):
                out[at - base : at - base + 4] = b"\x01" * 4
    return bytes(out)


def pointer_at(
    blob: bytes, offset: int, ranges: list[tuple[int, int]], text_at
) -> bool:
    """Whether the little-endian word of @p blob at @p offset holds an address inside @p ranges at which @p text_at
    (address -> bool) finds text."""
    if offset < 0 or offset + 4 > len(blob):
        return False
    value = struct.unpack_from("<I", blob, offset)[0]
    return any(start <= value < end for start, end in ranges) and text_at(value)


def runs(
    blob: bytes,
    base: int | None,
    ranges: list[tuple[int, int]],
    text_at=lambda address: False,
) -> list[str]:
    """The printable runs (PRINTABLE) of @p blob, loaded at @p base. A run no NUL ends that ends inside, or at the end
    of, a 4-byte-aligned word holding an address inside @p ranges at which @p text_at finds a string start ends where
    that word starts instead (again, while the new end does): those bytes are a pointer's low bytes, such as a `&str`'s
    after the Rust string before it, not the string's. A word the run reads on past is text, and a C string, which its
    NUL ends, is never cut."""
    found = []
    for m in PRINTABLE.finditer(blob):
        start, end = m.span()
        if base is not None and blob[end : end + 1] != b"\x00":
            while True:
                word = end - 1 - (base + end - 1) % 4
                if word < start or not pointer_at(blob, word, ranges, text_at):
                    break
                end = word
        # A run cut short has a pointer after it, not a NUL, so it needs the six characters of a bare run.
        if end == m.end() or end - start >= 6:
            found.append(blob[start:end].decode("ascii"))
    return found


def strings(
    objcopy: str, elf: str, readelf: str = "llvm-readelf", nm: str = "llvm-nm"
) -> list[str]:
    """Every printable run in the loaded data of @p elf (runs()), each loaded where layout() says, with the pointers in
    its data objects masked (unpointered())."""
    at, ranges = layout(readelf, elf)
    objects = data_objects(nm, elf)
    content = sdram(objcopy, elf)
    if content and ".sdram_init" in at:
        ranges = [*ranges, (at[".sdram_init"], at[".sdram_init"] + len(content))]
    # A symbol at no loaded address (`__fini_array_start` at 0, `IRQ_MODE`, a stack size) is no pointer's target.
    targets = {
        t for t in pointer_targets(nm, elf) if any(lo <= t < hi for lo, hi in ranges)
    }
    blobs = [(section(objcopy, elf, s), at.get(s)) for s in SECTIONS] + [
        (content, at.get(".sdram_init"))
    ]

    def text_at(address: int) -> bool:
        """Whether a string of STRING_START or more characters starts at @p address."""
        for blob, base in blobs:
            if base is not None and base <= address < base + len(blob):
                at = address - base
                return (
                    (at == 0 or not printable(blob[at - 1]))
                    and all(printable(b) for b in blob[at : at + STRING_START])
                    and at + STRING_START <= len(blob)
                )
        return False

    return [
        text
        for blob, base in blobs
        for text in runs(
            unpointered(blob, base, targets, objects) if base is not None else blob,
            base,
            ranges,
            text_at,
        )
    ]


def diff_strings(
    old: list[str], new: list[str], renames: list[tuple[str, str]]
) -> tuple[list[str], list[str]]:
    """The strings only @p old has (after applying @p renames in order to each) and the strings only @p new has,
    counting repeats."""

    news = {}
    for a, b in renames:
        news.setdefault(a, b)
    pattern = (
        re.compile("|".join(re.escape(a) for a, _ in renames)) if renames else None
    )

    def renamed(s: str) -> str:
        return pattern.sub(lambda m: news[m.group()], s) if pattern else s

    before = collections.Counter(renamed(s) for s in old)
    after = collections.Counter(new)
    return sorted((before - after).elements()), sorted((after - before).elements())


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("old")
    p.add_argument("new")
    p.add_argument(
        "--rename",
        action="append",
        default=[],
        help="OLD=NEW, applied to the first image's strings in order",
    )
    p.add_argument(
        "--objcopy",
        default="llvm-objcopy",
    )
    p.add_argument("--readelf", default="llvm-readelf")
    p.add_argument("--nm", default="llvm-nm")
    a = p.parse_args()
    renames = []
    for spec in a.rename:
        old, sep, new = spec.partition("=")
        if not sep or not old:
            p.error(f"--rename wants OLD=NEW, got {spec!r}")
        renames.append((old, new))
    gone, came = diff_strings(
        strings(a.objcopy, a.old, a.readelf, a.nm),
        strings(a.objcopy, a.new, a.readelf, a.nm),
        renames,
    )
    for s in gone:
        print(f"- {s}")
    for s in came:
        print(f"+ {s}")
    print(f"differ: {len(gone) + len(came)}")
    return 1 if gone or came else 0


if __name__ == "__main__":
    sys.exit(main())
