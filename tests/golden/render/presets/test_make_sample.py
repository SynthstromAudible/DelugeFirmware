# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for make_sample.py: the synthetic sources the sample cases play are what the cases assume, and stable.

Gate make-sample-selftest:
    the sample cases' synthetic sources are what their cases assume: the percussive source is 32,768 frames of
    44.1 kHz 16-bit stereo whose bursts start exactly at its listed onsets, the short tone played an octave up
    outlasts the time stretcher's first hop and not the next, each source comes out byte-identical from two runs, and
    the committed WAVs are the generator's output
"""

from __future__ import annotations

import struct
import unittest
from pathlib import Path

import make_sample

SAMPLE_DIR = Path(__file__).resolve().parent / "sample"

# A burst's onset is the first frame of at least ONSET_LEVEL after QUIET_FRAMES frames below it.
ONSET_LEVEL = 4096
QUIET_FRAMES = 256


def parse_wav(data: bytes) -> tuple[dict, list[tuple[int, ...]]]:
    """The fmt fields and the frames of a PCM WAV."""
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    chunks = {}
    pos = 12
    while pos + 8 <= len(data):
        tag = data[pos : pos + 4]
        (size,) = struct.unpack_from("<I", data, pos + 4)
        chunks[tag] = data[pos + 8 : pos + 8 + size]
        pos += 8 + size + (size & 1)
    _, channels, rate, _, _, bits = struct.unpack("<HHIIHH", chunks[b"fmt "])
    samples = struct.unpack(f"<{len(chunks[b'data']) // 2}h", chunks[b"data"])
    frames = [
        tuple(samples[i : i + channels]) for i in range(0, len(samples), channels)
    ]
    root = struct.unpack("<9I", chunks[b"smpl"])[3]
    return {"channels": channels, "rate": rate, "bits": bits, "root": root}, frames


def onsets(frames: list[tuple[int, ...]]) -> list[int]:
    """Where a burst starts: a frame at ONSET_LEVEL or louder on either channel, after QUIET_FRAMES quiet ones."""
    found = []
    quiet = QUIET_FRAMES
    for i, frame in enumerate(frames):
        if max(abs(s) for s in frame) >= ONSET_LEVEL:
            if quiet >= QUIET_FRAMES:
                found.append(i)
            quiet = 0
        else:
            quiet += 1
    return found


class PercussiveSourceTest(unittest.TestCase):
    def setUp(self):
        self.fmt, self.frames = parse_wav(make_sample.perc_wav())

    def test_it_is_the_tone_format_and_length(self):
        tone, _ = parse_wav((SAMPLE_DIR / "_tone.wav").read_bytes())
        self.assertEqual(self.fmt, tone)
        self.assertEqual(self.fmt["rate"], 44100)
        self.assertEqual(self.fmt["bits"], 16)
        self.assertEqual(self.fmt["channels"], 2)
        self.assertEqual(len(self.frames), 32768)

    def test_its_bursts_start_at_the_listed_onsets(self):
        self.assertEqual(onsets(self.frames), list(make_sample.PERC_ONSETS))

    def test_its_onsets_are_irregular(self):
        gaps = {
            b - a for a, b in zip(make_sample.PERC_ONSETS, make_sample.PERC_ONSETS[1:])
        }
        self.assertEqual(len(gaps), len(make_sample.PERC_ONSETS) - 1)

    def test_its_channels_differ(self):
        self.assertNotEqual([f[0] for f in self.frames], [f[1] for f in self.frames])

    def test_two_runs_give_the_same_bytes(self):
        self.assertEqual(make_sample.perc_wav(), make_sample.perc_wav())

    def test_the_committed_file_is_the_generators_output(self):
        self.assertEqual(
            (SAMPLE_DIR / "_perc.wav").read_bytes(), make_sample.perc_wav()
        )


class ShortToneTest(unittest.TestCase):
    def test_an_octave_up_it_outlasts_the_first_hop_and_not_the_next(self):
        fmt, frames = parse_wav(make_sample.short_tone_wav())
        self.assertEqual(fmt["channels"], 2)
        self.assertEqual(len(frames), 1024)
        # The first hop ends 200 output frames into the note, give or take 32 at random; an octave up the play-head
        # reads two source frames to each output frame.
        self.assertGreater(len(frames), (200 + 32) * 2)
        # An octave up the shortest hop after it is 3,000 output frames.
        self.assertLess(len(frames), 3000)

    def test_two_runs_give_the_same_bytes(self):
        self.assertEqual(make_sample.short_tone_wav(), make_sample.short_tone_wav())

    def test_the_committed_file_is_the_generators_output(self):
        self.assertEqual(
            (SAMPLE_DIR / "_tone_short.wav").read_bytes(), make_sample.short_tone_wav()
        )


class ToneTest(unittest.TestCase):
    def test_the_committed_tones_are_the_generators_output(self):
        self.assertEqual(
            (SAMPLE_DIR / "_tone.wav").read_bytes(), make_sample.tone_wav()
        )
        self.assertEqual(
            (SAMPLE_DIR / "_tone_mono.wav").read_bytes(), make_sample.tone_mono_wav()
        )


if __name__ == "__main__":
    unittest.main()
