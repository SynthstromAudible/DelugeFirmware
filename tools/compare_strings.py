#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Diffs the printable strings two device images load (.rodata, .data and the SDRAM content inflated from
.sdram_init_pack), after renaming paths and crates in the first image's strings. A string is a run of printable ASCII
of at least six characters, or of at least three when a NUL byte ends it (a C string). Equal multisets after the
renames prove that no such string changed; they say nothing about tables and other non-text data, which bytes_check
covers, nor about strings that sit next to other bytes without a NUL after them and are shorter than six characters.

The renames are trusted: each is a substring replacement on the first image's strings, so a changed string that
contains renamed text compares equal to the same string with the new text. That is why the renames are exact moved
paths and crate names. Each string is rewritten in one pass: at every position the first matching rename wins, and
its output is not renamed again.

Usage: tools/compare_strings.py OLD.elf NEW.elf [--rename OLD=NEW ...] [--objcopy PATH]
"""

from __future__ import annotations

import argparse
import collections
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

PRINTABLE = re.compile(rb"[\x20-\x7e]{3,}(?=\x00)|[\x20-\x7e]{6,}")
SECTIONS = (".rodata", ".data")
DEFAULT_OBJCOPY = (
    Path(__file__).resolve().parent.parent
    / "toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-objcopy"
)


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


def strings(objcopy: str, elf: str) -> list[str]:
    """Every printable run of at least six characters in the loaded data of @p elf."""
    blobs = [section(objcopy, elf, s) for s in SECTIONS] + [sdram(objcopy, elf)]
    return [
        m.group().decode("ascii") for blob in blobs for m in PRINTABLE.finditer(blob)
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
        default=str(DEFAULT_OBJCOPY),
    )
    a = p.parse_args()
    renames = []
    for spec in a.rename:
        old, sep, new = spec.partition("=")
        if not sep or not old:
            p.error(f"--rename wants OLD=NEW, got {spec!r}")
        renames.append((old, new))
    gone, came = diff_strings(
        strings(a.objcopy, a.old), strings(a.objcopy, a.new), renames
    )
    for s in gone:
        print(f"- {s}")
    for s in came:
        print(f"+ {s}")
    print(f"differ: {len(gone) + len(came)}")
    return 1 if gone or came else 0


if __name__ == "__main__":
    sys.exit(main())
