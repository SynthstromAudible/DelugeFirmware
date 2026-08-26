#!/usr/bin/env python3
"""Build an AIFF test corpus from real WAV files, preserving loop points.

ffmpeg converts the audio but DROPS `smpl` loop metadata, so a plain conversion
exercises only COMM/SSND. This reads the source WAV's `smpl` chunk and injects
equivalent AIFF MARK + INST chunks, so the result also exercises the
marker-resolved loop path — the part with no other real-file coverage.

Usage: make_aiff_corpus.py <src-root> <out-dir> [max-files]
"""

import os
import struct
import subprocess
import sys

# How many files to take per (bit depth, channels, has-loop) shape. Looping
# sources are much rarer in the corpus, so they get the larger quota.
PER_SHAPE_PLAIN = 3
PER_SHAPE_WITH_LOOP = 6


def wav_chunks(data):
    """Yield (tag, payload) for each RIFF chunk. Tolerates truncation."""
    if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        return
    pos = 12
    while pos + 8 <= len(data):
        tag = data[pos : pos + 4]
        (size,) = struct.unpack_from("<I", data, pos + 4)
        payload = data[pos + 8 : pos + 8 + size]
        if len(payload) < size:
            return
        yield tag, payload
        pos += 8 + size + (size & 1)


def read_smpl(data):
    """-> (loop_start, loop_end, unity_note) or None if no usable loop."""
    for tag, payload in wav_chunks(data):
        if tag != b"smpl" or len(payload) < 36:
            continue
        unity = struct.unpack_from("<I", payload, 12)[0]
        num_loops = struct.unpack_from("<I", payload, 28)[0]
        if num_loops < 1 or len(payload) < 36 + 24:
            continue
        start = struct.unpack_from("<I", payload, 36 + 8)[0]
        end = struct.unpack_from("<I", payload, 36 + 12)[0]
        if end > start:
            return start, end, unity
    return None


def wav_depth_channels(data):
    for tag, payload in wav_chunks(data):
        if tag == b"fmt " and len(payload) >= 16:
            channels = struct.unpack_from("<H", payload, 2)[0]
            bits = struct.unpack_from("<H", payload, 14)[0]
            return bits, channels
    return None, None


def pstring(text):
    """AIFF pstring: length byte + chars, padded so the whole thing is even."""
    raw = bytes([len(text)]) + text.encode("ascii")
    return raw + (b"\x00" if len(raw) % 2 else b"")


def mark_chunk(begin_pos, end_pos):
    body = struct.pack(">H", 2)
    body += struct.pack(">hI", 1, begin_pos) + pstring("beg")
    body += struct.pack(">hI", 2, end_pos) + pstring("end")
    return b"MARK" + struct.pack(">I", len(body)) + body


def inst_chunk(base_note):
    body = struct.pack(
        ">bbbbbbh", max(0, min(127, base_note)), 0, 0, 127, 0, 127, 0
    )  # 8 bytes
    body += struct.pack(">hhh", 1, 1, 2)  # sustain loop: forward, markers 1..2
    body += struct.pack(">hhh", 0, 0, 0)  # release loop: none
    assert len(body) == 20, len(body)
    return b"INST" + struct.pack(">I", len(body)) + body


def inject(aiff, extra):
    """Append chunks to a FORM/AIFF file and fix up the FORM size."""
    out = aiff + extra
    struct.pack_into(">I", out := bytearray(out), 4, len(out) - 8)
    return bytes(out)


def main():
    src_root, out_dir = sys.argv[1], sys.argv[2]
    budget = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    os.makedirs(out_dir, exist_ok=True)

    made_with_loops = made_plain = 0
    seen_shapes = {}

    for dirpath, _, names in os.walk(src_root):
        for name in sorted(names):
            if made_with_loops + made_plain >= budget:
                break
            if not name.lower().endswith(".wav"):
                continue
            path = os.path.join(dirpath, name)
            try:
                with open(path, "rb") as fh:
                    head = fh.read(1 << 20)
            except OSError:
                continue

            bits, channels = wav_depth_channels(head)
            if bits is None:
                continue
            loop = read_smpl(head)
            # Spread the corpus across (depth, channels, has-loop) rather than
            # taking the first N files, which would all come from one folder and
            # share one shape. Looping files are rare, so they get a bigger quota.
            shape = (bits, channels, loop is not None)
            quota = PER_SHAPE_WITH_LOOP if loop else PER_SHAPE_PLAIN
            if seen_shapes.get(shape, 0) >= quota:
                continue
            seen_shapes[shape] = seen_shapes.get(shape, 0) + 1

            stem = f"{bits}bit_{channels}ch_{'loop' if loop else 'noloop'}_{made_with_loops + made_plain:03d}"
            dst = os.path.join(out_dir, stem + ".aiff")
            codec = {
                8: "pcm_s8",
                16: "pcm_s16be",
                24: "pcm_s24be",
                32: "pcm_s32be",
            }.get(bits)
            if codec is None:
                continue
            rc = subprocess.run(
                ["ffmpeg", "-v", "error", "-i", path, "-c:a", codec, dst, "-y"],
                capture_output=True,
                check=False,
            )
            if rc.returncode != 0 or not os.path.exists(dst):
                continue

            if loop:
                start, end, unity = loop
                with open(dst, "rb") as fh:
                    data = fh.read()
                data = inject(data, mark_chunk(start, end) + inst_chunk(unity))
                with open(dst, "wb") as fh:
                    fh.write(data)
                made_with_loops += 1
                print(f"loops  {stem}  start={start} end={end} note={unity}")
            else:
                made_plain += 1
                print(f"plain  {stem}")

    print(f"\n{made_with_loops} AIFF with MARK+INST loops, {made_plain} without")


if __name__ == "__main__":
    main()
