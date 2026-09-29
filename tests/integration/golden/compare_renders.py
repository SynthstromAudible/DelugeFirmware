#!/usr/bin/env python3
"""Compare two sets of golden-master renders case by case, for re-baselining with evidence.

A changed hash cannot say whether the sound survived. This compares what the two renders sound like: their level,
their envelope over time, their spectrum, and (where the sound has no randomness) their waveform.

    python3 tests/integration/golden/compare_renders.py OLD NEW [--report FILE]

OLD and NEW are directories of renders named by case (`factory/000 Rich Saw Bass.wav`, or `.pcm`): either 16-bit
stereo WAVs, as the C-host harness kept them, or raw interleaved little-endian i32, as `RENDER_PRESET_KEEP` keeps them.
Both are brought to the same 16-bit scale (the C host wrote the top 16 bits of each sample).

The renders are aligned on the first note's onset before comparing, because the two harnesses start a note at
different points in their block. Per case it reports:
    onset     the new onset's shift from the old one, in frames
    rms, peak each render's RMS level (dB below full scale) and peak, and the difference in dB
    env       correlation of the two RMS envelopes (512-frame windows): 1 when the sound rises and falls the same way
    centroid  the difference in spectral centroid, in percent: how much brighter or darker the new render is
    wave      the best correlation of the waveforms within 256 frames of the aligned position: near 1 only when the
              waveform itself matches, so low for any sound with random phases or noise

--skip N leaves the first N frames after the aligned onset out of every measure but the onset: the C-host harness's
32-frame blocks swell the first cycle of a note (up to 1.8x the steady level), which the Embassy host's 128-frame
blocks do not.

Needs numpy.
"""

import argparse
import math
import sys
import wave
from pathlib import Path

import numpy as np

WINDOW = 512
LAG = 256


def load(path: Path) -> np.ndarray:
    """A render as a float array of shape (frames, 2), at 16-bit scale."""
    if path.suffix.lower() == ".wav":
        with wave.open(str(path)) as w:
            if w.getsampwidth() != 2 or w.getnchannels() != 2:
                raise ValueError(f"{path}: expected 16-bit stereo")
            data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
        return data.reshape(-1, 2).astype(np.float64)
    data = np.frombuffer(path.read_bytes(), dtype="<i4")
    return (data.reshape(-1, 2) >> 16).astype(np.float64)


def onset(x: np.ndarray) -> int:
    """The first frame louder than 1% of the render's peak."""
    mono = np.abs(x).max(axis=1)
    peak = mono.max()
    if peak == 0:
        return 0
    return int(np.argmax(mono > peak * 0.01))


def db(value: float) -> float:
    return 20 * math.log10(value / 32768) if value > 0 else -math.inf


def envelope(mono: np.ndarray) -> np.ndarray:
    n = len(mono) // WINDOW
    return np.sqrt((mono[: n * WINDOW].reshape(n, WINDOW) ** 2).mean(axis=1))


def centroid(mono: np.ndarray) -> float:
    # Weighted by power, so the dither floor under a quiet stretch does not decide it.
    spectrum = np.abs(np.fft.rfft(mono)) ** 2
    total = spectrum.sum()
    return (
        float((spectrum * np.arange(len(spectrum))).sum() / total) if total > 0 else 0.0
    )


def correlation(a: np.ndarray, b: np.ndarray) -> float:
    if a.std() == 0 or b.std() == 0:
        return 1.0 if a.std() == b.std() else 0.0
    return float(np.corrcoef(a, b)[0, 1])


def best_wave_correlation(a: np.ndarray, b: np.ndarray) -> float:
    """The highest normalized correlation of `a` and `b` within LAG frames either way."""
    n = len(a)
    size = 1 << (2 * n - 1).bit_length()
    cross = np.fft.irfft(np.fft.rfft(a, size) * np.conj(np.fft.rfft(b, size)), size)
    lags = np.concatenate([cross[: LAG + 1], cross[-LAG:]])
    norm = math.sqrt(float((a * a).sum()) * float((b * b).sum()))
    return float(lags.max() / norm) if norm > 0 else 0.0


def compare(old: np.ndarray, new: np.ndarray, skip: int = 0) -> dict:
    shift = onset(new) - onset(old)
    if shift > 0:
        new = new[shift:]
    elif shift < 0:
        old = old[-shift:]
    n = min(len(old), len(new))
    old, new = old[:n], new[:n]
    if skip:
        start = onset(old) + skip
        old, new = old[start:], new[start:]
    old_mono, new_mono = old.sum(axis=1), new.sum(axis=1)
    old_rms = math.sqrt(float((old**2).mean()))
    new_rms = math.sqrt(float((new**2).mean()))
    old_centroid, new_centroid = centroid(old_mono), centroid(new_mono)
    return {
        "onset": shift,
        "old_rms": db(old_rms),
        "new_rms": db(new_rms),
        "rms_diff": db(new_rms) - db(old_rms),
        "old_peak": float(np.abs(old).max()),
        "new_peak": float(np.abs(new).max()),
        "peak_diff": db(float(np.abs(new).max())) - db(float(np.abs(old).max())),
        "env": correlation(envelope(old_mono), envelope(new_mono)),
        "centroid": 100 * (new_centroid / old_centroid - 1)
        if old_centroid > 0
        else 0.0,
        "wave": best_wave_correlation(old_mono, new_mono),
    }


def renders(root: Path) -> dict[str, Path]:
    return {
        str(p.relative_to(root).with_suffix("")): p
        for p in root.rglob("*")
        if p.suffix.lower() in (".wav", ".pcm")
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument(
        "--report", type=Path, help="also write the table here, as Markdown"
    )
    parser.add_argument(
        "--skip", type=int, default=0, help="frames after the onset to leave out"
    )
    args = parser.parse_args()

    old, new = renders(args.old), renders(args.new)
    missing = sorted(set(old) ^ set(new))
    for name in missing:
        print(f"only in {'OLD' if name in old else 'NEW'}: {name}", file=sys.stderr)

    rows = []
    for name in sorted(set(old) & set(new)):
        rows.append((name, compare(load(old[name]), load(new[name]), args.skip)))
    rows.sort(key=lambda row: (-abs(row[1]["rms_diff"]), row[1]["env"]))

    header = "| case | onset | old rms | new rms | Δrms dB | Δpeak dB | env | centroid % | wave |"
    lines = [header, "|" + "---|" * 9]
    for name, r in rows:
        lines.append(
            f"| {name} | {r['onset']} | {r['old_rms']:.2f} | {r['new_rms']:.2f} | {r['rms_diff']:+.2f} "
            f"| {r['peak_diff']:+.2f} | {r['env']:.3f} | {r['centroid']:+.1f} | {r['wave']:.3f} |"
        )
    table = "\n".join(lines)
    print(table)
    if args.report:
        args.report.write_text(table + "\n")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
