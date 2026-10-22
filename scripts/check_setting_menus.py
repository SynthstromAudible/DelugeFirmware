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

Nor may a file under src/deluge/gui or src/scenes write a song setting outside SetSetting: the song's reverb, swing
interval and disabled scales, a track's name and MIDI or CV device fields, a kit lane's name, a note row's direction,
an audio clip's descriptor. A gesture calls songSettingFor(), trackSettingFor(), laneSettingFor(),
noteRowSettingFor() or audioClipSettingFor() (model/edits/setting_edit.h), which write the record and project it.
Nor may it write a MIDI binding (a mute command, an input, a learned knob, a section's launch command) past the song's
mapping table: a gesture calls the mapping Edits (model/edits/learn_mapping.h), which write the table and project it.

The check reads a write by its text, so it does not follow an alias: `*target.thing = ...` through a pointer taken
earlier, or `learnedThing->clear()` on a `LearnedMIDI*` a menu holds, is not found. They are legitimate today: the
menus that hold such a pointer (a mute command or an input they edit as the learn gesture) hand it back to the mapping
Edits, which write the table and project it, and the check cannot tell that pointer from one that writes. A gesture that
wrote through an alias would go unreported; the mapping tests, which read the table after each gesture, are what catch
one.

Nor may a file under src/scenes write a param on its AutoParam: a param menu writes through SetParamValue and deletes
its automation through DeleteParamAutomation (model/edits/). The gold knob's turn and automation delete and a patch
cable's strength still do, allowed line by line until 2c-2 gives them their Edits.

Two gestures write the runtime themselves and record what they wrote, checked file by file: a file that writes a mod
knob (a `modKnobs[...]` descriptor, or learnKnob()/unlearnKnobs() on what is not a menu item) also calls
recordControlsOf(), and a file that writes a sample holder's file or zone also calls recordRanges() or rangesFor().

Usage:
    scripts/check_setting_menus.py                    # check src/deluge/gui and src/scenes, exit 1 on a call
    scripts/check_setting_menus.py --root DIR [DIR]   # check those
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
#: Receivers that are not a sound: a sample range's holder. (An audio clip's settings are song state, below.)
ALLOWED_RECEIVERS = {"sampleHolder"}
#: A receiver allowed only in the audio clip's own menus: none, an audio clip's settings being song state.
AUDIO_CLIP_RECEIVERS: set[str] = set()
AUDIO_CLIP_MENUS = "audio_clip"

CALL = re.compile(r"(?:->|\.)\s*(" + "|".join(SETTERS) + r")\s*\(")
#: Direct writes of a source's fields the settings own.
RAW_WRITE = re.compile(
    r"(?:->|\.)\s*(repeatMode|oscType|polyphonic|maxVoiceCount|outputMidiChannel|outputMidiNoteForDrum)\s*=(?!=)"
)
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

#: A song setting written outside SetSetting: the record's own fields, the engine copies a projection makes, and the
#: setters a projection calls.
SONG_STATE_WRITES = (
    re.compile(r"project\s*\.\s*reverb\s*\.\s*\w+\s*=(?!=)"),
    re.compile(
        r"project\s*\.\s*scale\s*\.\s*disabled_preset_scales\s*(?:\[[^\]]*\]\s*=(?!=)|\.\s*(?:set|reset|flip)\s*\(|=(?!=))"
    ),
    re.compile(r"(?<!\w)changeSwingInterval\s*\("),
    re.compile(r"AudioEngine::reverb\s*\.\s*set\w+\s*\("),
    re.compile(r"(?<!\w)reverbPan\s*=(?!=)"),
    re.compile(r"(?<!\w)reverbSidechain(?:Volume|Shape)\s*=(?!=)"),
    re.compile(
        r"(?<!\w)reverbSidechainSettings\s*\.\s*(?:attack|release|sync_level|sync_type)\s*=(?!=)"
    ),
    re.compile(
        r"(?:->|\.)\s*(?:setName|setChannel|setChannelSuffix|setDeviceDefinitionFileName|setCollapseAftertouch"
        r"|setCollapseMPE|setOutputMPEY|setCV2Mode|setSequenceDirectionMode|setSampleStartPos|setSampleEndPos"
        r"|setSampleTranspose|setSampleCents|setOverdubsShouldCloneOutput|setAttack)\s*\("
    ),
    re.compile(
        r"(?:->|\.)\s*(?:outputMPEY|collapseAftertouch|collapseMPE|deviceDefinitionFileName|channelSuffix)\s*=(?!=)"
    ),
    re.compile(r"(?:->|\.)\s*deviceDefinitionFileName\s*\.\s*clear\s*\("),
    #: An output's, instrument's or drum's name written raw.
    re.compile(
        r"(?i:output|instrument|drum)\w*\s*(?:\(\s*\))?\s*(?:->|\.)\s*name\s*=(?!=)"
    ),
    re.compile(
        r"(?i:clip)\w*\s*(?:\(\s*\))?\s*(?:->|\.)\s*sampleHolder\s*\.\s*(?:startPos|endPos)\s*=(?!=)"
    ),
    #: A clip's name, MIDI program and bend ranges; the sync-scaling clip.
    re.compile(r"(?i:clip|target)\w*\s*(?:\(\s*\))?\s*(?:->|\.)\s*name\s*=(?!=)"),
    re.compile(r"(?:->|\.)\s*(?:midiBank|midiSub|midiPGM)\s*=(?!=)"),
    re.compile(r"bendRanges\s*\[[^\]]*\]\s*=(?!=)"),
    re.compile(r"(?<!\w)setInputTickScaleClip\s*\("),
    #: An audio track's input.
    re.compile(r"(?:->|\.)\s*(?:setOutputRecordingFrom|clearRecordingFrom)\s*\("),
    re.compile(r"(?:->|\.)\s*inputChannel\s*=(?!=)"),
    re.compile(
        r"(?:->|\.)\s*mode\s*=\s*(?:AudioOutputMode|static_cast<AudioOutputMode>)"
    ),
    #: A patch cable's polarity, written past its record (PatchCableSet::setPolarity writes both).
    re.compile(r"(?:->|\.)\s*polarity\s*=(?!=)"),
    #: A MIDI device's labels and hide flag.
    re.compile(r"(?:->|\.)\s*hideUnlabeledCC\s*=(?!=)"),
    re.compile(r"(?:->|\.)\s*setNameForCC\s*\("),
    #: An arp's settings: a clip's, a kit row's, or the one an editor holds, through its field or its preset.
    re.compile(
        r"(?:\barpSettings|\bcurrentArpSettings|\bctx\s*\.\s*arp|getArpSettings\s*\(\s*\))\s*(?:->|\.)\s*\w+\s*=(?!=)"
    ),
    re.compile(
        r"(?:->|\.)\s*(?:updateSettingsFromCurrentPreset|updatePresetFromCurrentSettings)\s*\("
    ),
    #: A MIDI binding written past the song's mapping table (model/edits/learn_mapping.h): a clip's or kit row's mute
    #: command, a track's or kit row's input, an owner's learned knobs, a section's launch command.
    re.compile(
        r"(?:->|\.)\s*(?:muteMIDICommand|midiInput)\s*(?:=(?!=)|\.\s*(?:\w+\s*=(?!=)|clear\s*\())"
    ),
    re.compile(
        r"(?<!current_item_\))(?<!getCurrentMenuItem\(\))(?:->|\.)\s*(?:learnKnob|unlearnKnobs)\s*\("
    ),
    re.compile(
        r"(?<!\w)midi_knobs\s*(?:=(?!=)|\.\s*(?:push_back|emplace_back|insert|erase|clear|resize|pop_back|assign|swap)\s*\(|\[[^\]]*\]\s*(?:\.\s*\w+\s*)?=(?!=))"
    ),
    re.compile(
        r"(?<!\w)std::(?:ranges::)?(?:erase_if|erase|sort|stable_sort|remove_if|remove|unique|reverse|rotate|shuffle"
        r"|partition|stable_partition|fill|swap|move)\s*\([^;]*\bmidi_knobs\b"
    ),
    re.compile(r"(?<!\w)launch_midi_command\b"),
)
#: Song-state writes under the roots that are allowed, by file (relative to src/) and the text the write's line holds,
#: each with its reason. An empty text allows the whole file.
SONG_STATE_ALLOWED: dict[str, dict[str, str]] = {
    "deluge/gui/views/view.cpp": {
        "setChannel(-1)": "the channel search parks the instrument off every channel while it looks for a free one",
        "setChannel(oldChannel)": "and puts it back before the in-place re-channel, which is a SetSetting",
    },
    "deluge/gui/menu_item/menu_item_with_cc_learning.cpp": {
        "currentModControllable->learnKnob(": "a mod knob's learn (channel 16 and up) sets the owner's mod knobs, "
        "which recordControlsOf records; a MIDI knob's goes through learnKnobFor",
    },
    "scenes/menu/nodes/sound_fx.cpp": {
        "reverbSidechainSettings.sync_type = syncValueToSyncType(value)": "the reverb sidechain's sync type is the "
        "engine's alone, with no row to write; its level is the song's record, written through SetSetting",
    },
    "deluge/gui/menu_item/sidechain/sync.h": {
        "reverbSidechainSettings.sync_type = syncValueToSyncType(current_value)": "the legacy menu's own write of "
        "the same row-less sync type",
    },
    "deluge/gui/ui/browser/sample_claim.cpp": {
        "": "a claim records the sample it loaded onto an audio clip as a load does (syncSampleRecordFromEngine)",
    },
}
DEFAULT_ROOTS = (REPO / "src" / "deluge" / "gui", REPO / "src" / "scenes")


def code_lines(text: str):
    """Yield `(number, code)` for each line of @p text with its comments removed: `//` to the end of the line and
    `/* ... */`, which may span lines."""
    in_block = False
    for number, line in enumerate(text.splitlines(), 1):
        code = []
        i = 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    break
                in_block = False
                i = end + 2
                continue
            line_comment = line.find("//", i)
            block_comment = line.find("/*", i)
            if line_comment >= 0 and (
                block_comment < 0 or line_comment < block_comment
            ):
                code.append(line[i:line_comment])
                break
            if block_comment >= 0:
                code.append(line[i:block_comment])
                in_block = True
                i = block_comment + 2
                continue
            code.append(line[i:])
            break
        yield number, "".join(code)


def song_state_writes(
    roots: tuple[Path, ...] = DEFAULT_ROOTS, allowed=None
) -> list[str]:
    """Every `path:line: <write>` under @p roots that writes a song setting outside SetSetting, less the files
    @p allowed names (paths relative to src/, or to the root for a root outside it)."""
    allowed = SONG_STATE_ALLOWED if allowed is None else allowed
    found = []
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in SUFFIXES or not path.is_file():
                continue
            base = REPO / "src"
            key = str(
                path.relative_to(base)
                if path.is_relative_to(base)
                else path.relative_to(root)
            )
            allowances = allowed.get(key, {})
            if "" in allowances:
                continue
            for number, code in code_lines(path.read_text(errors="replace")):
                if any(text in code for text in allowances):
                    continue
                for pattern in SONG_STATE_WRITES:
                    if match := pattern.search(code):
                        found.append(
                            f"{path}:{number}: song setting written outside SetSetting: {match.group(0).strip()}"
                        )
                        break
    return found


#: Files outside the roots checked the same way: none, the roots being the whole UI.
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
        # A file whose song-state writes are all allowed (a load path) sets an audio clip's settings the same way.
        base = REPO / "src"
        if path.is_relative_to(base) and "" in SONG_STATE_ALLOWED.get(
            str(path.relative_to(base)), {}
        ):
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


SCENES_ROOT = REPO / "src" / "scenes"
# An engine follow-up the Edit that changed the setting owns: a scenes gesture leaves it to the Edit (SetEngineSetting
# and SetSourceRanges retune a sound's voices). Legacy menus under src/deluge/gui still make the call themselves.
SCENES_FOLLOW_UP = re.compile(r"\b(recalculateAllVoicePhaseIncrements)\s*\(")
# The reverb's recompute after a setting it copies (a sidechain's attack, release, sync level) is the setting's Edit's
# too: SetEngineSetting and SetEffectSetting raise it, and the song reverb's own projection does. A scene file that
# writes settings through their Edits and raises it itself is doing the Edit's job. A param write's raise (an unpatched
# param that updates the reverb) has no Edit to do it, and a file with no setting write is not judged.
SCENES_REVERB_RAISE = re.compile(r"\bmustUpdateReverbParamsBeforeNextRender\s*=")
SCENES_SETTING_WRITE = re.compile(r"\b(effectSettingFor|songSettingFor)\s*\(")


def scene_follow_ups(root: Path) -> list[str]:
    """Every `path:line: runs <follow-up>` or reverb raise under root: an engine follow-up a scene runs itself instead
    of its Edit."""
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        for number, line in code_lines(path.read_text(errors="replace")):
            for match in SCENES_FOLLOW_UP.finditer(line):
                found.append(f"{path}:{number}: runs {match.group(1)}")
        lines = list(code_lines(path.read_text(errors="replace")))
        if any(SCENES_SETTING_WRITE.search(line) for _, line in lines):
            for number, line in lines:
                if SCENES_REVERB_RAISE.search(line):
                    found.append(
                        f"{path}:{number}: raises mustUpdateReverbParamsBeforeNextRender beside a setting's Edit"
                    )
    return found


#: A scene marking its instrument edited: SetSetting marks it for the settings that count as an edit.
SCENES_EDITED_MARK = re.compile(r"\beditedByUser\s*=(?!=)")
#: The legacy setting helpers a converted node no longer calls: its target writes its row through the menu's sink. The
#: path is the call's first string argument, after the receiver a clip's, track's or lane's helper takes.
SCENES_LEGACY_HELPER = re.compile(
    r"\b(clipSettingFor|clipValueSettingFor|clipArpSettingFor|laneArpSettingFor|arpSettingFor|laneSettingFor"
    r"|noteRowSettingFor|audioClipSettingFor|trackSettingFor|songSettingFor|songSettingFromMenu)\s*\(\s*(?:[^,;\"()]*,\s*)?(?:\"([^\"]*)\")?"
)
#: The scene files whose nodes write their rows by target, each with the paths a hook still writes through a legacy
#: helper: the sidechain's volume (its menu scale is not the row's) and sync level (written beside the engine's sync
#: type), the swing interval (a song-root hook), and an audio clip's interpolation (a sample hook not yet a target).
SCENES_CONVERTED: dict[str, frozenset[str]] = {
    "sound_arp.cpp": frozenset(),
    "sound_randomizer.cpp": frozenset(),
    "sound_bend.cpp": frozenset(),
    "sound_fx.cpp": frozenset(
        {"reverb/compressor/volume", "reverb/compressor/syncLevel"}
    ),
    "roots.cpp": frozenset({"swingInterval"}),
    "sound_voice.cpp": frozenset(),
    "sound_osc.cpp": frozenset({"linearInterpolation"}),
}


def converted_scene_writes(root: Path) -> list[str]:
    """Every `path:line:` under root where a scene marks its instrument edited, or a converted node file writes a row
    through a legacy setting helper its target replaced."""
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        lines = list(code_lines(path.read_text(errors="replace")))
        for number, line in lines:
            if SCENES_EDITED_MARK.search(line):
                found.append(
                    f"{path}:{number}: marks its instrument edited outside SetSetting"
                )
        if path.name not in SCENES_CONVERTED:
            continue
        # A call's path may be on the line after its helper's name: match across the file's code.
        numbers = [number for number, _ in lines]
        code = "\n".join(line for _, line in lines)
        for match in SCENES_LEGACY_HELPER.finditer(code):
            if match.group(2) not in SCENES_CONVERTED[path.name]:
                number = numbers[code.count("\n", 0, match.start())]
                found.append(
                    f"{path}:{number}: a converted node writes through {match.group(1)}"
                )
    return found


#: A param written on its AutoParam: a scene's param menu writes its param through SetParamValue (and deletes its
#: automation through DeleteParamAutomation), which log the change and raise what follows it.
SCENES_PARAM_WRITE = re.compile(
    r"(?:->|\.)\s*(setCurrentValueInResponseToUserInput|setValuePossiblyForRegion|deleteAutomation)\s*\("
)
#: The param writes 2c-2 still owes an Edit, by file (relative to src/scenes) and the text the write's line holds, each
#: with its reason.
SCENES_PARAM_ALLOWED: dict[str, dict[str, str]] = {
    "menu/context.cpp": {
        "setValuePossiblyForRegion(new_value, param, target.mod_pos, target.mod_length)": "a gold knob's turn, "
        "written for the region the view holds; 2c-2 owes it its Edit",
        "deleteAutomation(action, param)": "shift+select on a gold knob deletes its automation; 2c-2 owes it "
        "DeleteParamAutomation",
    },
    "menu/bindings.cpp": {
        "setCurrentValueInResponseToUserInput(final_value, with_param)": "a patch cable's strength, which may "
        "create the cable; 2c-2 owes it its Edit",
    },
}


def scene_param_writes(root: Path, allowed=None) -> list[str]:
    """Every `path:line: writes a param on its AutoParam` under @p root, less the lines @p allowed names (paths
    relative to @p root)."""
    allowed = SCENES_PARAM_ALLOWED if allowed is None else allowed
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        allowances = allowed.get(str(path.relative_to(root)), {})
        for number, code in code_lines(path.read_text(errors="replace")):
            if any(text in code for text in allowances):
                continue
            if match := SCENES_PARAM_WRITE.search(code):
                found.append(
                    f"{path}:{number}: writes a param on its AutoParam ({match.group(1)}) outside its Edit"
                )
    return found


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, nargs="+", default=list(DEFAULT_ROOTS))
    parser.add_argument("--also", type=Path, nargs="*", default=list(ALSO))
    args = parser.parse_args(argv)
    found = []
    for root in args.root:
        found += calls(root, tuple(args.also)) + unrecorded(root)
        if root.resolve().is_relative_to(SCENES_ROOT):
            found += scene_follow_ups(root) + converted_scene_writes(root)
            if root.resolve() == SCENES_ROOT:
                found += scene_param_writes(root)
    found += host_writes() + song_state_writes(tuple(args.root))
    for line in found:
        print(line)
    if found:
        print(
            f"FAIL: a gesture sets a sound's instrument or song state without its Edit ({len(found)} place(s))"
        )
        return 1
    print("OK: every gesture sets a sound's instrument and song state through its Edit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
