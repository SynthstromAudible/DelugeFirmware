#!/usr/bin/env python3
"""Writes the synthetic wavetables the load_policy pins and the band builder's spec load."""

import math
import pathlib
import struct

OUT = pathlib.Path(__file__).resolve().parent


def cycle(n: int, c: int, cycles: int) -> list[float]:
    """Cycle c of a morph from a sine to a band-limited saw, harmonics up to a quarter of n."""
    t = c / max(cycles - 1, 1)
    top = max(1, n // 4)
    return [
        math.sin(2 * math.pi * i / n) * (1 - t)
        + t
        * sum(math.sin(2 * math.pi * h * i / n) / h for h in range(1, top + 1))
        * 0.5
        for i in range(n)
    ]


def write(name, cycles, n, bits=16, fmt=1, clm=None, junk=0):
    xs = [x for c in range(cycles) for x in cycle(n, c, cycles)]
    if fmt == 3:
        data = b"".join(struct.pack("<f", 0.9 * x) for x in xs)
        bits = 32
    else:
        full = (1 << (bits - 1)) - 1
        q = [max(-full - 1, min(full, round(0.9 * x * full))) for x in xs]
        if bits == 8:
            data = bytes((v + 128) & 0xFF for v in q)
        elif bits == 16:
            data = b"".join(struct.pack("<h", v) for v in q)
        elif bits == 24:
            data = b"".join(struct.pack("<i", v)[:3] for v in q)
        else:
            data = b"".join(struct.pack("<i", v) for v in q)
    block = bits // 8
    body = struct.pack("<HHIIHH", fmt, 1, 44100, 44100 * block, block, bits)
    chunks = b"fmt " + struct.pack("<I", len(body)) + body
    if junk:
        chunks += b"JUNK" + struct.pack("<I", junk) + bytes(junk)
    if clm is not None:
        tag = f"<!>{clm:04d} 00000000 wavetable".encode()
        tag += b"\0" * (len(tag) % 2)
        chunks += b"clm " + struct.pack("<I", len(tag)) + tag
    data_at = 12 + len(chunks) + 8  # byte offset of the audio data in the file
    chunks += b"data" + struct.pack("<I", len(data)) + data + b"\0" * (len(data) % 2)
    (OUT / name).write_bytes(
        b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks
    )
    return data_at


write("WTP_ONE.WAV", 1, 2048)  # one 2048 cycle: numCycles 1
write(
    "WTP_NPOT.WAV", 1, 600
)  # one 600-sample cycle: the DFT path, band 0 re-rendered at 1024
# 64 cycles of 256 whose audio data is not 4-byte aligned (RIFF chunks are word-aligned, so 2 mod 4 is as far off as
# it can be): the loader's misaligned 32-bit reads.
assert write("WTP_MANY.WAV", 64, 256, clm=256, junk=4) % 4 == 2
write("WTP_BIG.WAV", 2, 8192, clm=8192)  # the largest cycle: m = 13
write(
    "WTP_S32.WAV", 1, 32
)  # m = 5: 32 points, the smallest forward with a radix-4 first stage
write("WTP_S16.WAV", 1, 16)  # m = 4: 16 points, the smallest forward transform
write("WTP_S8.WAV", 1, 8)  # m = 3: one band; the zero-band case
write("WTP_P24.WAV", 8, 2048, bits=24)  # 24-bit samples straddling cluster boundaries
write("WTP_P32.WAV", 2, 2048, bits=32)
write("WTP_U8.WAV", 4, 2048, bits=8)
write("WTP_F32.WAV", 4, 2048, fmt=3)
