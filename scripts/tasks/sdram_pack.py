#! /usr/bin/env python3
"""Compresses the image's SDRAM content into its load copy, after the link.

`.sdram_init` and `.sdram_exidx`, which follows it (src/platform/device/linker/sdram_sections.x), are linked at their SDRAM
addresses only, so the ELF carries their bytes but the image does not. They are packed as one span, the gap between them
zero-filled. `.sdram_init_pack`, in the image's SRAM tail, is reserved zero-filled at link time; this
module writes the deflated content into it, and the boot (src/platform/device/src/sdram_pack.rs) inflates it back.

The pack is `[u32 LE raw length][u32 LE zlib length][zlib stream][zero padding]`, padded to the reserved size.
"""

import struct
import subprocess
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

HEADER = struct.Struct("<II")


class PackDoesNotFit(Exception):
    """The deflated content is larger than the pack the linker reserved."""


def pack(raw: bytes, reserve: int) -> bytes:
    """Deflates `raw` into a pack of exactly `reserve` bytes.

    Raises PackDoesNotFit when the header and stream need more than `reserve`.
    """
    stream = zlib.compress(raw, 9)
    body = HEADER.pack(len(raw), len(stream)) + stream
    if len(body) > reserve:
        raise PackDoesNotFit(
            f"sdram_pack: {len(raw)} B of SDRAM content deflate to {len(body)} B, past the {reserve} B the linker "
            "reserved (.sdram_init_pack in sdram_sections.x)"
        )
    return body + bytes(reserve - len(body))


def unpack(packed: bytes) -> bytes:
    """Inflates a pack back to its raw content; the check `pack_elf` runs on what it wrote."""
    raw_len, stream_len = HEADER.unpack_from(packed)
    raw = zlib.decompress(packed[HEADER.size : HEADER.size + stream_len])
    if len(raw) != raw_len:
        raise ValueError(
            f"sdram_pack: the pack holds {len(raw)} B, its header says {raw_len} B"
        )
    return raw


@dataclass
class PackResult:
    raw: int
    packed: int
    reserve: int


# The SDRAM content, in address order: the span from the first's start to the last's end is what the boot inflates.
SDRAM_SECTIONS = (".sdram_init", ".sdram_exidx")


def _section(objcopy: Path, elf: Path, names: tuple[str, ...], out: Path) -> bytes:
    """The bytes from the lowest of `names`' starts to the highest of their ends, gaps zero-filled."""
    subprocess.run(
        [
            str(objcopy),
            "-O",
            "binary",
            *(f"--only-section={n}" for n in names),
            str(elf),
            str(out),
        ],
        check=True,
    )
    return out.read_bytes()


def pack_elf(elf: Path, objcopy: Path) -> PackResult:
    """Packs `elf`'s SDRAM content (`SDRAM_SECTIONS`) into its `.sdram_init_pack`, in place, and makes it no-load.

    No-load, so that neither the image (objcopy -O binary) nor a debugger's load writes SDRAM, which is not up until
    the boot has started it. The written pack is read back and inflated as a check.
    """
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        raw = _section(objcopy, elf, SDRAM_SECTIONS, tmp_dir / "raw.bin")
        reserve = len(
            _section(objcopy, elf, (".sdram_init_pack",), tmp_dir / "reserve.bin")
        )
        packed = pack(raw, reserve)
        (tmp_dir / "pack.bin").write_bytes(packed)
        subprocess.run(
            [
                str(objcopy),
                f"--update-section=.sdram_init_pack={tmp_dir / 'pack.bin'}",
                *(f"--set-section-flags={n}=noload" for n in SDRAM_SECTIONS),
                str(elf),
            ],
            check=True,
        )
        if (
            unpack(_section(objcopy, elf, (".sdram_init_pack",), tmp_dir / "check.bin"))
            != raw
        ):
            raise ValueError(
                "sdram_pack: the pack written into the ELF does not inflate to the SDRAM content"
            )
    _, stream_len = HEADER.unpack_from(packed)
    return PackResult(raw=len(raw), packed=HEADER.size + stream_len, reserve=reserve)
