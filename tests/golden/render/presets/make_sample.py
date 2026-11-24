#!/usr/bin/env python3
"""Generate the deterministic source samples for the sample-playback golden cases.

Writes, under sample/, each a 44.1 kHz WAV (16-bit unless named otherwise) with an embedded `smpl` chunk pinning the root note to C4 (MIDI 60):

- _tone.wav (stereo) and _tone_mono.wav (mono): a seamlessly loopable, harmonically rich tone. Rich harmonics make the
  windowed-sinc anti-aliasing audible (so a kernel regression shows up in the hash); the perfect loop (every partial is
  an integer multiple of the buffer-length fundamental) lets loopMode=LOOP sustain the note for the whole capture
  without a seam click. L and R carry different harmonic mixes so the stereo interpolate() path exercises both channels
  distinctly.
- _tone_short.wav (stereo): the same mixes over 1,024 frames. Played an octave up and stretched, it outlasts the time
  stretcher's first hop but runs out before the next: shorter than one hop.
- _tone16.wav (stereo), _tone24.wav (stereo), _tone24_mono.wav, _tone32.wav (stereo) and _tone8_mono.wav: the tone's
  mixes at 16, 24, 32 and 8 bits (8-bit WAV is unsigned), its period repeated DEPTH_PERIODS times so each source spans
  several of the loader's 32 KB chunks and a 24-bit frame straddles their boundaries.
- _perc.wav (stereo): decaying noise bursts at irregular onsets, so the time stretcher's transient search (its perc
  cache) finds hits to place its hops on, where the tone gives it none.

Re-run only to regenerate the committed WAVs; the WAVs are the source of truth, and test_make_sample.py holds them to
this generator. The bytes depend on the platform's libm (math.sin and math.exp, rounded to 16 bits): the test's
byte-equality checks hold on the pr tier's Linux, and a libm whose last bits differ could move a sample by one.
"""

import math
import pathlib
import struct

HERE = pathlib.Path(__file__).parent
OUT = HERE / "sample"

SAMPLE_RATE = 44100
LENGTH = 4096  # one fundamental period == the whole buffer, so it loops seamlessly
# An octave up it outlasts the stretcher's first hop (at most 232 output frames, so 464 source frames) and runs out long
# before the next (at least 3,000 output frames later); test_make_sample.py derives both from the stretcher's constants.
SHORT_LENGTH = 1024
ROOT_NOTE = 60  # C4: played C4 -> native rate (phaseIncrement 2^24, sinc kernel 0)

# (harmonic, amplitude) per channel — distinct mixes so L != R.
HARMONICS_L = [(1, 0.50), (2, 0.25), (3, 0.16), (4, 0.10), (6, 0.07), (8, 0.05)]
HARMONICS_R = [(1, 0.48), (2, 0.20), (3, 0.18), (5, 0.11), (7, 0.06), (9, 0.04)]

# The depth sources repeat the tone's period this often: 40,960 frames, past seven 24-bit stereo chunks.
DEPTH_PERIODS = 10

PERC_LENGTH = 32768
# The frame each burst starts on: no two gaps alike, and each gap long enough for the burst before to decay away.
PERC_ONSETS = (512, 3301, 7559, 9874, 14612, 18217, 21390, 26903, 30011)
# Each burst's peak, as a fraction of full scale.
PERC_LEVELS = (0.90, 0.62, 0.81, 0.55, 0.74, 0.88, 0.50, 0.69, 0.79)
PERC_DECAY_FRAMES = 400  # the bursts' exponential time constant
PERC_SEED = 0x2545F491


def render_channel(harmonics, length=LENGTH, bits=16):
    out = []
    for n in range(length):
        acc = 0.0
        for k, amp in harmonics:
            acc += amp * math.sin(2.0 * math.pi * k * n / length)
        out.append(acc)
    peak = max(abs(s) for s in out)
    scale = 0.8 * ((1 << (bits - 1)) - 1) / peak
    return [round(s * scale) for s in out]


def noise(seed):
    """An endless xorshift32 stream of floats in [-1, 1), the same for the same seed on every platform."""
    state = seed
    while True:
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        yield state / 2147483648.0 - 1.0


def perc_channel(seed):
    """The bursts on one channel: each opens on a full-level frame, then decays as noise."""
    source = noise(seed)
    out = [0.0] * PERC_LENGTH
    for onset, level in zip(PERC_ONSETS, PERC_LEVELS):
        for t in range(PERC_LENGTH - onset):
            n = next(source)
            if t == 0:
                n = 1.0 if n >= 0 else -1.0
            out[onset + t] += level * math.exp(-t / PERC_DECAY_FRAMES) * n
    return [round(s * 32767) for s in out]


def smpl_chunk():
    # 9 uint32 the loader reads: manufacturer, product, samplePeriod, MIDIUnityNote,
    # MIDIPitchFraction, SMPTEFormat, SMPTEOffset, numSampleLoops(=0), samplerData.
    body = struct.pack("<9I", 0, 0, 0, ROOT_NOTE, 0, 0, 0, 0, 0)
    return b"smpl" + struct.pack("<I", len(body)) + body


def pcm_sample(value, bits):
    """One sample's bytes: little-endian two's complement, but unsigned at 8 bits as WAV stores it."""
    if bits == 8:
        return bytes([value + 128])
    return value.to_bytes(bits // 8, "little", signed=True)


def wav_bytes(channels, bits=16):
    num_channels = len(channels)
    frames = list(zip(*channels)) if num_channels > 1 else [(s,) for s in channels[0]]
    data = b"".join(pcm_sample(s, bits) for frame in frames for s in frame)

    block_align = num_channels * bits // 8
    fmt = struct.pack(
        "<HHIIHH",
        1,
        num_channels,
        SAMPLE_RATE,
        SAMPLE_RATE * block_align,
        block_align,
        bits,
    )
    fmt_chunk = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    smpl = smpl_chunk()
    data_chunk = b"data" + struct.pack("<I", len(data)) + data
    body = b"WAVE" + fmt_chunk + smpl + data_chunk
    return b"RIFF" + struct.pack("<I", len(body)) + body


def tone_wav():
    return wav_bytes([render_channel(HARMONICS_L), render_channel(HARMONICS_R)])


def tone_mono_wav():
    return wav_bytes([render_channel(HARMONICS_L)])


def short_tone_wav():
    return wav_bytes(
        [
            render_channel(HARMONICS_L, SHORT_LENGTH),
            render_channel(HARMONICS_R, SHORT_LENGTH),
        ]
    )


def depth_tone(harmonics, bits):
    return render_channel(harmonics, LENGTH, bits) * DEPTH_PERIODS


def tone16_wav():
    return wav_bytes([depth_tone(HARMONICS_L, 16), depth_tone(HARMONICS_R, 16)], 16)


def tone24_wav():
    return wav_bytes([depth_tone(HARMONICS_L, 24), depth_tone(HARMONICS_R, 24)], 24)


def tone24_mono_wav():
    return wav_bytes([depth_tone(HARMONICS_L, 24)], 24)


def tone32_wav():
    return wav_bytes([depth_tone(HARMONICS_L, 32), depth_tone(HARMONICS_R, 32)], 32)


def tone8_mono_wav():
    return wav_bytes([depth_tone(HARMONICS_L, 8)], 8)


def perc_wav():
    return wav_bytes([perc_channel(PERC_SEED), perc_channel(PERC_SEED ^ 0x5A5A5A5A)])


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, data in (
        ("_tone.wav", tone_wav()),
        ("_tone_mono.wav", tone_mono_wav()),
        ("_tone_short.wav", short_tone_wav()),
        ("_perc.wav", perc_wav()),
        ("_tone16.wav", tone16_wav()),
        ("_tone24.wav", tone24_wav()),
        ("_tone24_mono.wav", tone24_mono_wav()),
        ("_tone32.wav", tone32_wav()),
        ("_tone8_mono.wav", tone8_mono_wav()),
    ):
        (OUT / name).write_bytes(data)
        print(f"wrote {OUT / name} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
