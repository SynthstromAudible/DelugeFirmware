/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "io/midi/learned_midi.h"
#include <cstdint>

class MIDICable;
class Action;
class Clip;
class MIDIInstrument;
class ModelStackWithTimelineCounter;

// A macro: one learned incoming source CC broadcast to up to kNumTargetSlots target CCs on the
// active MIDI clip. Target values are written into the clip's own CC params, so they reach the
// clip's output and record into each CC's automation lane. The source CC is consumed; to
// forward/record it too, include it as one of its macro's targets.
// Config is per-track: the four macros and the per-instrument enable gate live on each MIDIInstrument
// and serialize with it (song + preset). Configured in the MIDI clip's menu.
namespace MIDIMacro {

constexpr int32_t kNumMacros = 4;
constexpr int32_t kNumTargetSlots = 8;
constexpr uint8_t kTargetCCNone = 255;
constexpr uint8_t kMaxTargetCC = 127;
constexpr uint8_t kMaxValue = 127;  // from/to and CC output range
constexpr uint8_t kDefaultFrom = 0; // output at source = 0
constexpr uint8_t kDefaultTo = 127; // output at source = 127 (defaults give pass-through)

// Each macro also appears as an automatable "lane" in MIDI automation view, stored as a pseudo-CC
// MIDIParam at these IDs. They sit above the 0..127 CC byte range so no real target CC (max 127)
// can collide, and they never emit MIDI (suppressed in MIDIParamCollection::sendMIDI).
constexpr int32_t kMacroParamIDBase = 128;
constexpr int32_t kNumMacroParams = kNumMacros;
inline bool isMacroParamID(int32_t id) {
	return id >= kMacroParamIDBase && id < kMacroParamIDBase + kNumMacroParams;
}
inline int32_t macroIndexFromParamID(int32_t id) {
	return id - kMacroParamIDBase;
}
inline int32_t paramIDForMacro(int32_t idx) {
	return kMacroParamIDBase + idx;
}

// One target CC slot: the source value 0..127 is scaled linearly onto [from, to]:
//   out = clamp(from + (to - from) * input / 127, 0, 127)
// Set from > to to invert the response; from == to sends a constant. from/to are the A/B captures.
struct MacroTargetSlot {
	uint8_t cc = kTargetCCNone;  // kTargetCCNone = OFF, else 0..kMaxTargetCC
	uint8_t from = kDefaultFrom; // 0..kMaxValue, output when source is at 0 (capture A)
	uint8_t to = kDefaultTo;     // 0..kMaxValue, output when source is at 127 (capture B; from>to inverts)
	bool send = true;            // the "Send" toggle: muted when false, but keeps cc/from/to
};

struct Macro {
	LearnedMIDI source;     // external-CC source (when sourceKnob < 0)
	int8_t sourceKnob = -1; // instead of a CC, one of the 2 physical gold knobs (0 or 1) can be the source; -1 = none
	uint8_t sourceKnobPos = 0; // live 0..kMaxValue position a gold-knob source accumulates into (not serialized)
	bool active = false;       // off by default; the macro fires only when active (and the feature is enabled)
	MacroTargetSlot targets[kNumTargetSlots];
};

// Whether the MIDI macro feature is turned on in Community Features. Gates menu visibility and, with
// each instrument's per-track enable, whether macros fire. This is the global off-switch.
bool isEnabled();

// Returns the index of a macro (other than macros[exceptMacro], and other than the given slot) whose
// target already targets this CC, or -1 if none; if non-null, *slotOut receives that target's
// slot. Duplicate destination CCs are allowed but flagged.
int32_t findTargetCCOwner(const Macro* macros, uint8_t cc, int32_t exceptMacro, int32_t exceptSlot,
                          int32_t* slotOut = nullptr);

// The OWNER of a destination CC is the first target (scanning macros, then slots) that targets it.
// A later target on the same CC is SHADOWED: it keeps the CC in its config (and the UI warns), but
// it is inert - it neither bakes automation nor sends live - so exactly one target ever drives a CC.
// Returns the macro of the earlier-scan target that would shadow macros[macroIndex].targets[slot]
// if it targeted `cc`, or -1 if that position would be the owner.
int32_t findShadowingOwner(const Macro* macros, uint8_t cc, int32_t macroIndex, int32_t slot);
inline bool isTargetShadowed(const Macro* macros, int32_t macroIndex, int32_t slot) {
	uint8_t cc = macros[macroIndex].targets[slot].cc;
	return cc != kTargetCCNone && findShadowingOwner(macros, cc, macroIndex, slot) >= 0;
}

// Shows a "<dest> used by Macro <ownerMacro+1>" popup (dest = device CC name or "CC n") - shared by
// the target editor, preset load and the Active toggle so the wording is identical everywhere.
// `persistent` keeps it up until cancelPopup() (used while a quick-edit button is held).
void showCCConflictPopup(uint8_t cc, int32_t ownerMacro, bool persistent = false);

// If macroIndex is inactive because a target CC is owned by another active macro, flashes an
// "Inactive / <dest> used by / Macro N" status and returns true; otherwise returns false. Shown when a
// macro lane is selected in automation view so you can see why it won't fire live.
bool showMacroInactivePopup(int32_t macroIndex);

// Returns the index of an *active* macro (other than macroIndex) in this instrument's macros that owns
// one of macroIndex's target CCs, or -1 if none; if non-null, *conflictCC receives the shared CC. A
// macro may not be made active while such a conflict exists (two active macros would fight over the
// shared CC); duplicate CCs are otherwise allowed to sit dormant.
int32_t findActiveConflict(const Macro* macros, int32_t macroIndex, uint8_t* conflictCC = nullptr);
inline bool macroHasActiveConflict(const Macro* macros, int32_t macroIndex) {
	return findActiveConflict(macros, macroIndex) >= 0;
}

// True if any of the four macros is configured (a learned source, a target CC set, or active).
// Gates serialization so an all-default instrument writes nothing.
bool anyMacroConfigured(Macro* macros);

// Folder for per-macro preset files under SETTINGS. Presets are portable target configs, shared.
constexpr char const* kPresetsFolder = "SETTINGS/MIDI_MACRO_PRESETS";

// Which macro a preset browser (load/save) is acting on. Set by the menu item before opening the
// browser UI, read by the UI's performLoad()/performSave().
extern int32_t presetMacroIndex;

// If the incoming CC matches any active macro's learned source on the active MIDI clip's instrument
// (and that instrument has macros enabled), writes its value to that macro's target CCs. Returns
// whether it matched (and so consumed the message).
bool tryMacro(MIDICable& cable, int32_t channelOrZone, int32_t ccNumber, int32_t value);

// Turning physical gold knob whichKnob (0 or 1): if any active macro on the active MIDI clip has that
// knob as its source, accumulate its live position by offset and drive its targets. Returns whether
// it matched (so the caller suppresses the knob's normal action - a knob source is dedicated).
bool tryKnobMacro(int32_t whichKnob, int32_t offset);

// The dedicated macro row: in the regular MIDI clip view with the LAST param-select button active,
// gold knob 1 drives Macro 1 and gold knob 2 drives Macro 2 directly (no learning needed). Only fires
// for an Active macro, so the row keeps its normal knob behavior when macros aren't in use. Returns
// whether it drove the macro (the caller then suppresses the knob's normal action).
bool tryModeKnobMacro(int32_t whichKnob, int32_t offset);

// Bakes macro macroIndex's automation-lane curve (pseudo-CC paramIDForMacro(idx)) into its target CC
// automation lanes on the given clip, scaled per target. Call after a user edits/clears the macro
// lane or changes a target's config. No-op if the macro lane's param was never created on this clip
// (nothing was ever baked, so the targets are left alone). If action != null, target clears are
// snapshotted into it so one BACK restores the macro lane and its targets together.
void reFanMacro(Clip* clip, int32_t macroIndex, Action* action);

// Re-bakes a single target slot (cheaper than reFanMacro when only its from/to/send changed).
void reFanTarget(Clip* clip, int32_t macroIndex, int32_t slot, Action* action);

// Repoints a target at a new destination CC: clears the bake left on the old CC (so no ghost lane
// keeps playing) unless another target still targets it, then bakes the macro curve into the new CC.
// Both writes are snapshotted into one undo action, so BACK restores any automation this replaced.
void changeTargetCC(Clip* clip, int32_t macroIndex, int32_t slot, uint8_t newCC);

// Serializes/deserializes an instrument's macros (and its enable gate) as a <midiMacros> block within
// the instrument's own XML. Reused for both the song file and standalone instrument presets.
void writeMacrosToFile(Serializer& writer, Macro* macros, bool enabled);
void readMacrosFromFile(Deserializer& reader, Macro* macros, bool& enabled);

// Save/load a preset: a macro's targets only (not its source or active state), so a preset is a
// portable target configuration applicable to any macro. writeMacroPreset() writes the body between
// the browser's createXMLFile() and closeFileAfterWriting(); loadMacroPreset() replaces
// macros[macroIndex]'s targets (keeping its source), clears baked lanes left on replaced CCs and
// re-bakes the macro lane into the new set on `clip`. If the loaded targets conflict with an active
// macro, the loaded macro is deactivated rather than losing CCs.
void writeMacroPreset(Serializer& writer, Macro* macros, int32_t macroIndex);
Error loadMacroPreset(FilePointer* fp, Clip* clip, Macro* macros, int32_t macroIndex);

// Captures the current live value of each configured target CC on the active MIDI clip into that
// target's from (toMax=false, capture A) or to (toMax=true, capture B). The source CC then morphs
// the targets between the two via the sendToTargets() interpolation.
void capture(int32_t macroIndex, bool toMax);
}; // namespace MIDIMacro
