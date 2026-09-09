#!/usr/bin/env python3
"""Generate `tests/fixtures/card*/SAMPLES/TONE.WAV` — the one sample the level-3 A/B loads.

**Why this exists rather than a file checked in from somewhere.** `tests/ui-scripts/app/menu.txt`'s
file-selector arm drives a real `SampleBrowser`/`BrowserScene` over a real `DELUGE_SD_ROOT`, so the
card has to hold an audio file the firmware can actually parse and load. A sample copied from a
sound library — or from another repository — is a licensing question nobody can answer years later,
and a binary whose provenance is "it was there" cannot be re-derived if it is ever corrupted. So the
bytes are *computed*, by this script, from a formula stated in full below, and the script is the
provenance.

**The formula.** 16-bit signed little-endian PCM, mono, 44100 Hz, 4410 frames (0.1 s), a 440 Hz sine
at 0.5 full scale:

    s[n] = round_half_away_from_zero(16383.5 * sin(2 * pi * 440 * n / 44100))     n = 0 .. 4409

`round_half_away_from_zero` is spelled out rather than left to `round()`, whose banker's rounding
would make the output depend on nothing visible in this text. The header is a plain 44-byte
canonical RIFF/WAVE (`RIFF` / `WAVE` / `fmt ` 16 / `data`) with no padding and no extra chunks, so
the whole file is 8864 bytes and every one of them is determined by the four numbers above.

Re-running this must produce the committed files byte for byte; `--check` asserts exactly that and
is what a reviewer runs instead of trusting this docstring.

Usage:
    make_fixture_sample.py            # write both fixtures
    make_fixture_sample.py --check    # verify the committed bytes match the formula
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

RATE = 44100
FRAMES = 4410
FREQ = 440
AMPLITUDE = 16383.5

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGETS = (
    REPO_ROOT / "tests/fixtures/card/SAMPLES/TONE.WAV",
    REPO_ROOT / "tests/fixtures/card-shell-menu/SAMPLES/TONE.WAV",
)


def _round_half_away_from_zero(x: float) -> int:
    """`round()` is banker's rounding; this is the schoolbook one, so the docstring's formula and
    the bytes on disk cannot disagree at a .5 that never appears in the text."""
    return math.floor(x + 0.5) if x >= 0 else math.ceil(x - 0.5)


def wav_bytes() -> bytes:
    samples = [
        _round_half_away_from_zero(
            AMPLITUDE * math.sin(2.0 * math.pi * FREQ * n / RATE)
        )
        for n in range(FRAMES)
    ]
    data = b"".join(struct.pack("<h", s) for s in samples)
    fmt = struct.pack("<HHIIHH", 1, 1, RATE, RATE * 2, 2, 16)
    body = (
        b"WAVE"
        + b"fmt "
        + struct.pack("<I", len(fmt))
        + fmt
        + b"data"
        + struct.pack("<I", len(data))
        + data
    )
    return b"RIFF" + struct.pack("<I", len(body)) + body


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--check", action="store_true", help="verify rather than write")
    args = ap.parse_args()

    want = wav_bytes()
    rc = 0
    for path in TARGETS:
        if args.check:
            got = path.read_bytes() if path.is_file() else None
            if got != want:
                print(
                    f"make_fixture_sample: {path} does not match the formula",
                    file=sys.stderr,
                )
                rc = 1
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(want)
        print(f"wrote {path} ({len(want)} bytes)")
    if args.check and rc == 0:
        print(f"make_fixture_sample: both fixtures match ({len(want)} bytes)")
    return rc


if __name__ == "__main__":
    sys.exit(main())
