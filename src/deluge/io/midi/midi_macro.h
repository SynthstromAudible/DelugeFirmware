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
	LearnedMIDI leader;
	bool active = false; // off by default; the macro fires only when active (and the feature is enabled)
	MacroFollowerSlot followers[kNumFollowerSlots];
};

// Whether the MIDI macro feature is turned on in Community Features. Gates menu visibility and, with
// each instrument's per-track enable, whether macros fire. This is the global off-switch.
bool isEnabled();

// True if any follower other than macros[exceptMacro].followers[exceptSlot] already targets this CC
// within this instrument's macros. Used by the follower CC editor to skip CCs already taken.
bool isFollowerCCUsed(const Macro* macros, uint8_t cc, int32_t exceptMacro, int32_t exceptSlot);

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

// Serializes/deserializes an instrument's macros (and its enable gate) as a <midiMacros> block within
// the instrument's own XML. Reused for both the song file and standalone instrument presets.
void writeMacrosToFile(Serializer& writer, Macro* macros, bool enabled);
void readMacrosFromFile(Deserializer& reader, Macro* macros, bool& enabled);

// Save/load a preset: a macro's followers only (not its leader or active state), so a preset is a
// portable follower configuration applicable to any macro. writeMacroPreset() writes the body between
// the browser's createXMLFile() and closeFileAfterWriting(); loadMacroPreset() replaces
// macros[macroIndex]'s followers (keeping its leader). If the loaded followers conflict with an active
// macro, the loaded macro is deactivated rather than losing CCs.
void writeMacroPreset(Serializer& writer, Macro* macros, int32_t macroIndex);
Error loadMacroPreset(FilePointer* fp, Macro* macros, int32_t macroIndex);

// Captures the current live value of each configured follower CC on the active MIDI clip into that
// follower's from (toMax=false, capture A) or to (toMax=true, capture B). The leader CC then morphs
// the followers between the two via the sendToFollowers() interpolation.
void capture(int32_t macroIndex, bool toMax);
}; // namespace MIDIMacro
