#!/usr/bin/env python3
"""Generate the deterministic source samples for the sample-playback golden cases.

Writes corpus/sample/_tone.wav (stereo) and _tone_mono.wav (mono): a seamlessly
loopable, harmonically rich tone with an embedded `smpl` chunk pinning the root
note to C4 (MIDI 60). Rich harmonics make the windowed-sinc anti-aliasing audible
(so a kernel regression shows up in the hash); the perfect loop (every partial is
an integer multiple of the buffer-length fundamental) lets loopMode=LOOP sustain
the note for the whole capture without a seam click. L and R carry different
harmonic mixes so the stereo interpolate() path exercises both channels distinctly.

Re-run only to regenerate the committed WAVs; the WAVs are the source of truth.
"""

import math
import pathlib
import struct

HERE = pathlib.Path(__file__).parent
OUT = HERE / "corpus" / "sample"

SAMPLE_RATE = 44100
LENGTH = 4096  # one fundamental period == the whole buffer, so it loops seamlessly
ROOT_NOTE = 60  # C4: played C4 -> native rate (phaseIncrement 2^24, sinc kernel 0)

# (harmonic, amplitude) per channel — distinct mixes so L != R.
HARMONICS_L = [(1, 0.50), (2, 0.25), (3, 0.16), (4, 0.10), (6, 0.07), (8, 0.05)]
HARMONICS_R = [(1, 0.48), (2, 0.20), (3, 0.18), (5, 0.11), (7, 0.06), (9, 0.04)]


def render_channel(harmonics):
    out = []
    for n in range(LENGTH):
        acc = 0.0
        for k, amp in harmonics:
            acc += amp * math.sin(2.0 * math.pi * k * n / LENGTH)
        out.append(acc)
    peak = max(abs(s) for s in out)
    scale = 0.8 * 32767 / peak
    return [int(round(s * scale)) for s in out]


def smpl_chunk():
    # 9 uint32 the loader reads: manufacturer, product, samplePeriod, MIDIUnityNote,
    # MIDIPitchFraction, SMPTEFormat, SMPTEOffset, numSampleLoops(=0), samplerData.
    body = struct.pack("<9I", 0, 0, 0, ROOT_NOTE, 0, 0, 0, 0, 0)
    return b"smpl" + struct.pack("<I", len(body)) + body


def write_wav(path, channels):
    num_channels = len(channels)
    frames = list(zip(*channels)) if num_channels > 1 else [(s,) for s in channels[0]]
    data = b"".join(struct.pack("<" + "h" * num_channels, *frame) for frame in frames)

    block_align = num_channels * 2
    fmt = struct.pack(
        "<HHIIHH",
        1,
        num_channels,
        SAMPLE_RATE,
        SAMPLE_RATE * block_align,
        block_align,
        16,
    )
    fmt_chunk = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    smpl = smpl_chunk()
    data_chunk = b"data" + struct.pack("<I", len(data)) + data
    body = b"WAVE" + fmt_chunk + smpl + data_chunk
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
    print(f"wrote {path} ({num_channels}ch, {LENGTH} frames, {len(data)} data bytes)")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    left = render_channel(HARMONICS_L)
    right = render_channel(HARMONICS_R)
    write_wav(OUT / "_tone.wav", [left, right])
    write_wav(OUT / "_tone_mono.wav", [left])


if __name__ == "__main__":
    main()
