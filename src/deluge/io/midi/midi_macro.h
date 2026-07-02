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

// A macro: one learned incoming leader CC broadcast to up to kNumFollowerSlots follower CCs on the
// active MIDI clip. Follower values are written into the clip's own CC params, so they reach the
// clip's output and record into each CC's automation lane. The leader CC is consumed; to
// forward/record it too, include it as one of its macro's followers.
// Config is per-track: the four macros and the per-instrument enable gate live on each MIDIInstrument
// and serialize with it (song + preset). Configured in the MIDI clip's menu.
namespace MIDIMacro {

constexpr int32_t kNumMacros = 4;
constexpr int32_t kNumFollowerSlots = 8;
constexpr uint8_t kFollowerCCNone = 255;
constexpr uint8_t kMaxFollowerCC = 127;
constexpr uint8_t kMaxValue = 127;  // from/to and CC output range
constexpr uint8_t kDefaultFrom = 0; // output at leader = 0
constexpr uint8_t kDefaultTo = 127; // output at leader = 127 (defaults give pass-through)

// Each macro also appears as an automatable "lane" in MIDI automation view, stored as a pseudo-CC
// MIDIParam at these IDs. They sit above the 0..127 CC byte range so no real follower CC (max 127)
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

// One follower CC slot: the leader value 0..127 is scaled linearly onto [from, to]:
//   out = clamp(from + (to - from) * input / 127, 0, 127)
// Set from > to to invert the response; from == to sends a constant. from/to are the A/B captures.
struct MacroFollowerSlot {
	uint8_t cc = kFollowerCCNone; // kFollowerCCNone = OFF, else 0..kMaxFollowerCC
	uint8_t from = kDefaultFrom;  // 0..kMaxValue, output when leader is at 0 (capture A)
	uint8_t to = kDefaultTo;      // 0..kMaxValue, output when leader is at 127 (capture B; from>to inverts)
	bool send = true;             // the "Send" toggle: muted when false, but keeps cc/from/to
};

struct Macro {
	LearnedMIDI leader;     // external-CC leader (when leaderKnob < 0)
	int8_t leaderKnob = -1; // instead of a CC, one of the 2 physical gold knobs (0 or 1) can be the leader; -1 = none
	uint8_t leaderKnobPos = 0; // live 0..kMaxValue position a gold-knob leader accumulates into (not serialized)
	bool active = false;       // off by default; the macro fires only when active (and the feature is enabled)
	MacroFollowerSlot followers[kNumFollowerSlots];
};

// Whether the MIDI macro feature is turned on in Community Features. Gates menu visibility and, with
// each instrument's per-track enable, whether macros fire. This is the global off-switch.
bool isEnabled();

// Returns the index of a macro (other than macros[exceptMacro], and other than the given slot) whose
// follower already targets this CC, or -1 if none; if non-null, *slotOut receives that follower's
// slot. Duplicate destination CCs are allowed but flagged.
int32_t findFollowerCCOwner(const Macro* macros, uint8_t cc, int32_t exceptMacro, int32_t exceptSlot,
                            int32_t* slotOut = nullptr);

// The OWNER of a destination CC is the first follower (scanning macros, then slots) that targets it.
// A later follower on the same CC is SHADOWED: it keeps the CC in its config (and the UI warns), but
// it is inert - it neither bakes automation nor sends live - so exactly one follower ever drives a CC.
// Returns the macro of the earlier-scan follower that would shadow macros[macroIndex].followers[slot]
// if it targeted `cc`, or -1 if that position would be the owner.
int32_t findShadowingOwner(const Macro* macros, uint8_t cc, int32_t macroIndex, int32_t slot);
inline bool isFollowerShadowed(const Macro* macros, int32_t macroIndex, int32_t slot) {
	uint8_t cc = macros[macroIndex].followers[slot].cc;
	return cc != kFollowerCCNone && findShadowingOwner(macros, cc, macroIndex, slot) >= 0;
}

// Shows a "<dest> used by Macro <ownerMacro+1>" popup (dest = device CC name or "CC n") - shared by
// the follower editor, preset load and the Active toggle so the wording is identical everywhere.
// `persistent` keeps it up until cancelPopup() (used while a quick-edit button is held).
void showCCConflictPopup(uint8_t cc, int32_t ownerMacro, bool persistent = false);

// If macroIndex is inactive because a follower CC is owned by another active macro, flashes an
// "Inactive / <dest> used by / Macro N" status and returns true; otherwise returns false. Shown when a
// macro lane is selected in automation view so you can see why it won't fire live.
bool showMacroInactivePopup(int32_t macroIndex);

// Returns the index of an *active* macro (other than macroIndex) in this instrument's macros that owns
// one of macroIndex's follower CCs, or -1 if none; if non-null, *conflictCC receives the shared CC. A
// macro may not be made active while such a conflict exists (two active macros would fight over the
// shared CC); duplicate CCs are otherwise allowed to sit dormant.
int32_t findActiveConflict(const Macro* macros, int32_t macroIndex, uint8_t* conflictCC = nullptr);
inline bool macroHasActiveConflict(const Macro* macros, int32_t macroIndex) {
	return findActiveConflict(macros, macroIndex) >= 0;
}

// True if any of the four macros is configured (a learned leader, a follower CC set, or active).
// Gates serialization so an all-default instrument writes nothing.
bool anyMacroConfigured(Macro* macros);

// Folder for per-macro preset files under SETTINGS. Presets are portable follower configs, shared.
constexpr char const* kPresetsFolder = "SETTINGS/MIDI_MACRO_PRESETS";

// Which macro a preset browser (load/save) is acting on. Set by the menu item before opening the
// browser UI, read by the UI's performLoad()/performSave().
extern int32_t presetMacroIndex;

// If the incoming CC matches any active macro's learned leader on the active MIDI clip's instrument
// (and that instrument has macros enabled), writes its value to that macro's follower CCs. Returns
// whether it matched (and so consumed the message).
bool tryMacro(MIDICable& cable, int32_t channelOrZone, int32_t ccNumber, int32_t value);

// Turning physical gold knob whichKnob (0 or 1): if any active macro on the active MIDI clip has that
// knob as its leader, accumulate its live position by offset and drive its followers. Returns whether
// it matched (so the caller suppresses the knob's normal action - a knob leader is dedicated).
bool tryKnobMacro(int32_t whichKnob, int32_t offset);

// Bakes macro macroIndex's automation-lane curve (pseudo-CC paramIDForMacro(idx)) into its follower CC
// automation lanes on the given clip, scaled per follower. Call after a user edits/clears the macro
// lane or changes a follower's config. No-op if the macro lane's param was never created on this clip
// (nothing was ever baked, so the followers are left alone). If action != null, follower clears are
// snapshotted into it so one BACK restores the macro lane and its followers together.
void reFanMacro(Clip* clip, int32_t macroIndex, Action* action);

// Re-bakes a single follower slot (cheaper than reFanMacro when only its from/to/send changed).
void reFanFollower(Clip* clip, int32_t macroIndex, int32_t slot, Action* action);

// Repoints a follower at a new destination CC: clears the bake left on the old CC (so no ghost lane
// keeps playing) unless another follower still targets it, then bakes the macro curve into the new CC.
// Both writes are snapshotted into one undo action, so BACK restores any automation this replaced.
void changeFollowerCC(Clip* clip, int32_t macroIndex, int32_t slot, uint8_t newCC);

// Serializes/deserializes an instrument's macros (and its enable gate) as a <midiMacros> block within
// the instrument's own XML. Reused for both the song file and standalone instrument presets.
void writeMacrosToFile(Serializer& writer, Macro* macros, bool enabled);
void readMacrosFromFile(Deserializer& reader, Macro* macros, bool& enabled);

// Save/load a preset: a macro's followers only (not its leader or active state), so a preset is a
// portable follower configuration applicable to any macro. writeMacroPreset() writes the body between
// the browser's createXMLFile() and closeFileAfterWriting(); loadMacroPreset() replaces
// macros[macroIndex]'s followers (keeping its leader), clears baked lanes left on replaced CCs and
// re-bakes the macro lane into the new set on `clip`. If the loaded followers conflict with an active
// macro, the loaded macro is deactivated rather than losing CCs.
void writeMacroPreset(Serializer& writer, Macro* macros, int32_t macroIndex);
Error loadMacroPreset(FilePointer* fp, Clip* clip, Macro* macros, int32_t macroIndex);

// Captures the current live value of each configured follower CC on the active MIDI clip into that
// follower's from (toMax=false, capture A) or to (toMax=true, capture B). The leader CC then morphs
// the followers between the two via the sendToFollowers() interpolation.
void capture(int32_t macroIndex, bool toMax);
}; // namespace MIDIMacro
