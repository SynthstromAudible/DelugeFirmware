#!/usr/bin/env python3
"""Writes the live-input race workload's card tree.

Usage: make_live_input_song.py <source song> <project dir>

The song is the two-synth fixture with both sounds' first oscillator on the left input a fifth up, so every note
pitch-shifts, and each clip's notes NOTE_TICKS long every NOTE_SPACING, so the live inputs go unread between notes and
are read again. The scenario plays from the arranger, so each sound's clip gets one long arrangement instance.
"""

import pathlib
import re
import sys

NOTE_TICKS = 24
NOTE_SPACING = 96
LOOP_TICKS = 384  # the fixture's clip length
ARRANGED_LOOPS = 100


def notes(length_ticks: int) -> str:
    """noteDataWithSplitProb: position (4 bytes), length (4), velocity, lift, probability, iterance (2), fill; one
    record per note."""
    records = (
        f"{pos:08X}{NOTE_TICKS:08X}5A4014000000"
        for pos in range(0, length_ticks, NOTE_SPACING)
    )
    return "0x" + "".join(records)


def instance(clip: int) -> str:
    """The sound's id (clip + 1), and its arrangement: clip number `clip` (from 0) from the start, for ARRANGED_LOOPS
    of its loops."""
    return f'{clip + 1}" clipInstances="0x{0:08X}{LOOP_TICKS * ARRANGED_LOOPS:08X}{clip:08X}"'


def main() -> None:
    source, project = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    song = source.read_text()
    song = re.sub(
        r'(<osc1\s+type=")[^"]*("\s+transpose=")[^"]*"', r'\1inLeft\g<2>7"', song
    )
    # The fixture's two sounds share id 1, its own test's subject; here they are 1 and 2
    clips = iter(range(2))
    song, sounds = re.subn(
        r'(<sound\s+id=")1"', lambda m: m.group(1) + instance(next(clips, 2)), song
    )
    if sounds != 2:
        sys.exit(f"expected the fixture's two sounds, found {sounds}")
    # The song's firmware version reads the split-probability form only
    song = re.sub(r'\s+noteDataWithLift="[^"]*"', "", song)
    song = re.sub(
        r'noteDataWithSplitProb="[^"]*"',
        f'noteDataWithSplitProb="{notes(LOOP_TICKS)}"',
        song,
    )
    out = project / "SONGS" / "LiveInput.XML"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(song)


if __name__ == "__main__":
    main()
