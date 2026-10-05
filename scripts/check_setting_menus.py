#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""No gesture sets a bound sound's instrument state except through its Edit: no file under src/deluge/gui may call
one of the setters a setting Edit runs or a reseed of the record, or write a source's repeat mode or oscillator type.
A gesture calls settingFor(), rangesFor()/recordRanges(), dxPatchFor() or recordControlsOf(), which apply the Edit
to the sound's slot or its controller mappings, so the model keeps holding the sound's state. A call on an audio clip
(which is no instrument slot; a receiver named `clip` only in the audio clip's own menus) or on a sample range's
holder is allowed by its receiver's name. The sample browser's preview sound, outside the song, is set up by the
runtime and calls no setter.

Usage:
    scripts/check_setting_menus.py               # check src/deluge/gui, exit 1 on a call
    scripts/check_setting_menus.py --root DIR    # check DIR
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

#: The setters a setting Edit runs.
SETTERS = (
    "setPolyphonic",
    "setVoicePriority",
    "setMaxVoiceCount",
    "setSynthMode",
    "setTranspose",
    "set_cents",
    "setSideChainSendLevel",
    "setOscType",
    "setCents",
    "setOscRetriggerPhase",
    "setOscillatorSync",
    "setRepeatMode",
    "setReversed",
    "setPitchAndSpeedIndependent",
    "setTimeStretchAmount",
    "setInterpolationMode",
    "setLfoWaveType",
    "setLfoSync",
    "setNumUnison",
    "setUnisonDetune",
    "setUnisonStereoSpread",
    "setModulatorTranspose",
    "setModulatorCents",
    "setModulatorRetriggerPhase",
    "setModulator1ToModulator0",
    "setOutputMidiChannel",
    "setOutputMidiNoteForDrum",
    # The record's reseeds: a gesture records what it changed through the Edits, which reseed and refresh the slot.
    "seedDocRanges",
    "seedDocSourceScalars",
    "seedDocDxPatch",
)
#: Receivers that are not a sound: an audio clip, and a sample range's holder.
ALLOWED_RECEIVERS = {"audioClip", "getCurrentAudioClip", "sampleHolder"}
#: A receiver allowed only in the audio clip's own menus, where `clip` is an AudioClip.
AUDIO_CLIP_RECEIVERS = {"clip"}
AUDIO_CLIP_MENUS = "audio_clip"

CALL = re.compile(r"(?:->|\.)\s*(" + "|".join(SETTERS) + r")\s*\(")
#: Direct writes of a source's fields the settings own.
RAW_WRITE = re.compile(r"(?:->|\.)\s*(repeatMode|oscType)\s*=(?!=)")
#: The receiver's last name before the call: `a->b.name`, `name()`, `name[i]`.
RECEIVER = re.compile(r"([A-Za-z_]\w*)\s*(?:\(\s*\))?\s*(?:\[[^\]]*\])?\s*$")
SUFFIXES = {".h", ".hpp", ".cpp"}


#: Files outside the root checked the same way: none, now that the root is the whole UI.
ALSO: tuple[Path, ...] = ()


def calls(root: Path, also: tuple[Path, ...] = ()) -> list[str]:
    """Every `path:line: receiver->setter` under root, or in @p also, that sets a sound's setting outside its Edit."""
    found = []
    for path in sorted(root.rglob("*")) + list(also):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            for match in [*CALL.finditer(line), *RAW_WRITE.finditer(line)]:
                receiver = RECEIVER.search(line[: match.start()])
                name = receiver.group(1) if receiver else "?"
                audio_clip_menu = (
                    path.is_relative_to(root)
                    and AUDIO_CLIP_MENUS in path.relative_to(root).parts
                )
                if name not in ALLOWED_RECEIVERS and not (
                    audio_clip_menu and name in AUDIO_CLIP_RECEIVERS
                ):
                    found.append(f"{path}:{number}: {name} {match.group(1)}")
    return found


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "src" / "deluge" / "gui")
    parser.add_argument("--also", type=Path, nargs="*", default=list(ALSO))
    args = parser.parse_args(argv)
    found = calls(args.root, tuple(args.also))
    for line in found:
        print(line)
    if found:
        print(
            f"FAIL: a gesture sets a sound's instrument state without its Edit ({len(found)} place(s))"
        )
        return 1
    print("OK: every gesture sets a sound's instrument state through its Edit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
