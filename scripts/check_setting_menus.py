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

Two gestures write the runtime themselves and record what they wrote, checked file by file: a file that writes a mod
knob (a `modKnobs[...]` descriptor, or learnKnob()/unlearnKnobs() on what is not a menu item) also calls
recordControlsOf(), and a file that writes a sample holder's file or zone also calls recordRanges() or rangesFor().

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
    # The effects' setters: a gesture changes an effect through changeEffect (effectSettingFor in the menus).
    "setLpfMode",
    "setHpfMode",
    "setFilterRoute",
    "setClippingAmount",
    "setModFXType",
    "setDelayPingPong",
    "setDelayType",
    "setDelaySyncType",
    "setDelaySyncLevel",
    "setSidechainAttack",
    "setSidechainRelease",
    "setSidechainSyncType",
    "setSidechainSyncLevel",
    "setCompressorAttack",
    "setCompressorRelease",
    "setCompressorThreshold",
    "setCompressorRatio",
    "setCompressorSidechainHpf",
    "setCompressorBlend",
    "setStutterQuantized",
    "setStutterReversed",
    "setStutterPingPong",
    "setUseSongStutter",
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


#: The last name before a call or field, past any `()`, `[i]`, `*` or closing parenthesis: `(*item_)->x` names item_.
LAST_NAME = re.compile(r"([A-Za-z_]\w*)[\s()\[\]*\w]*$")
#: A mod knob written: its descriptor set, or a knob learned or unlearned.
KNOB_WRITE = re.compile(
    r"modKnobs\s*\[[^\]]*\]\s*\[[^\]]*\]\s*\.\s*paramDescriptor\s*\.\s*set\w*\s*\("
)
KNOB_LEARN = re.compile(r"(?:->|\.)\s*(learnKnob|unlearnKnobs)\s*\(")
#: Receivers of learnKnob() that are menu items, which hand the knob to the sound and record it themselves.
MENU_ITEM_RECEIVERS = {"current_item_", "getCurrentMenuItem"}
#: A sample holder's file or zone written; the holder is a receiver whose name ends in "holder", any case.
HOLDER_WRITE = re.compile(
    r"(?:->|\.)\s*(filePath|startPos|endPos|loopStartPos|loopEndPos)\s*=(?!=)"
)
#: What makes a file's writes recorded, by kind.
RECORDERS = {
    "a mod knob": ("recordControlsOf(",),
    "a sample holder's file or zone": ("recordRanges(", "rangesFor("),
}

#: Files outside the root checked the same way: none, now that the root is the whole UI.
ALSO: tuple[Path, ...] = ()

#: The host owners' code (a kit's, an audio track's and the master's effects and knobs): an effect setter or a view
#: member written here, bare or through a receiver, is a write that bypasses the owner's chain.
HOST_OWNER_DIRS = (
    REPO / "src" / "deluge" / "model" / "global_effectable",
    REPO / "src" / "deluge" / "model" / "mod_controllable",
    REPO / "src" / "deluge" / "model" / "instrument" / "kit.cpp",
    REPO / "src" / "deluge" / "processing" / "audio_output.cpp",
    REPO / "src" / "deluge" / "model" / "song" / "song.cpp",
)
EFFECT_SETTERS = tuple(
    s
    for s in SETTERS
    if s.startswith(
        (
            "setLpf",
            "setHpf",
            "setFilter",
            "setClipping",
            "setModFX",
            "setDelay",
            "setSidechain",
            "setCompressor",
            "setStutter",
            "setUseSongStutter",
        )
    )
) + ("assignModFXType",)
HOST_CALL = re.compile(r"(?<!\w)(" + "|".join(EFFECT_SETTERS) + r")\s*\(")
#: A host owner's settings or view set wholesale: only the Edits, the bridge and the functions HOST_ALLOWED names do it.
VIEW_WRITE = re.compile(r"(?<!\w)(setEffects|setChainState)\s*\(")
#: A function opening at the start of a line, `Class::name(` (a constructor's has no return type): the enclosing function
#: of the lines after it.
FUNCTION = re.compile(r"^(?:[A-Za-z_][\w:<>,*& ]*?\b)?(\w+)::(~?\w+)\s*\(")
#: A free function opening at the start of a line, `name(`.
FREE_FUNCTION = re.compile(
    r"^(?:\[\[\w+\]\]\s*)?(?!namespace\b|template\b|using\b|return\b)[A-Za-z_][\w:<>,*& ]*?\b(\w+)\s*\("
)
#: The functions that may write an owner's effects or view natively: a setter's own definition (named as the setter),
#: a copy of another owner (cloneFrom), a constructor, the load from the owner's element, and the one function that sets
#: a host chain's state (setChainState), which the Edits and the bridge call.
HOST_ALLOWED = {"cloneFrom", "readEffectsFromElement", "changeEffects", "setChainState"}


def host_writes(dirs: tuple[Path, ...] = HOST_OWNER_DIRS) -> list[str]:
    """Every `path:line: function writes <what>` in the host owners' code that writes an effect or the view natively."""
    found = []
    for root in dirs:
        for path in sorted(root.rglob("*")) if root.is_dir() else [root]:
            if path.suffix not in {".cpp"} or not path.is_file():
                continue
            function = ""
            owner = ""
            for number, line in enumerate(
                path.read_text(errors="replace").splitlines(), 1
            ):
                if opened := FUNCTION.match(line):
                    owner, function = opened.group(1), opened.group(2)
                    continue
                if free := FREE_FUNCTION.match(line):
                    owner, function = "", free.group(1)
                    continue
                if (
                    function in HOST_ALLOWED
                    or function == owner
                    or function in EFFECT_SETTERS
                ):
                    continue
                for match in [*HOST_CALL.finditer(line), *VIEW_WRITE.finditer(line)]:
                    found.append(
                        f"{path}:{number}: {owner}::{function} writes {match.group(1)}"
                    )
    return found


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


def unrecorded(root: Path) -> list[str]:
    """Every `<file>: writes <what> but never records it` under root: a knob or holder write without its recorder."""
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        text = path.read_text(errors="replace")
        audio_clip_menu = AUDIO_CLIP_MENUS in path.relative_to(root).parts
        writes = set()
        for line in text.splitlines():
            if KNOB_WRITE.search(line):
                writes.add("a mod knob")
            for match in KNOB_LEARN.finditer(line):
                receiver = LAST_NAME.search(line[: match.start()])
                if receiver is None or receiver.group(1) not in MENU_ITEM_RECEIVERS:
                    writes.add("a mod knob")
            for match in HOLDER_WRITE.finditer(line):
                before = line[: match.start()]
                receiver = LAST_NAME.search(before)
                if receiver is None or not receiver.group(1).lower().rstrip(
                    "_"
                ).endswith("holder"):
                    continue
                if (
                    "audioClip" in before
                    or "AudioClip" in before
                    or (audio_clip_menu and "clip" in before)
                ):
                    continue
                writes.add("a sample holder's file or zone")
        for what in sorted(writes):
            if not any(recorder in text for recorder in RECORDERS[what]):
                found.append(f"{path}: writes {what} but never records it")
    return found


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "src" / "deluge" / "gui")
    parser.add_argument("--also", type=Path, nargs="*", default=list(ALSO))
    args = parser.parse_args(argv)
    found = calls(args.root, tuple(args.also)) + unrecorded(args.root) + host_writes()
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
