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
// Configured via SETTINGS > MIDI > MACRO and persisted in SETTINGS/MIDIMacro.XML on the SD card.
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
	bool enabled = true;          // muted when false, but keeps cc/from/to
};

struct Macro {
	LearnedMIDI leader;
	bool enabled = false; // master enable, off by default; turn a macro on manually to use it
	MacroFollowerSlot followers[kNumFollowerSlots];
};

extern Macro macros[kNumMacros];

// Whether the MIDI macro feature is turned on in Community Features. Gates CC broadcast and the
// clip-view capture gestures.
bool isEnabled();

// Folder for per-macro preset files, a sibling of MIDIMacro.XML under SETTINGS.
constexpr char const* kPresetsFolder = "SETTINGS/MIDI_MACRO_PRESETS";

// Which macro a preset browser (load/save) is acting on. Set by the menu item before opening the
// browser UI, read by the UI's performLoad()/performSave().
extern int32_t presetMacroIndex;

// If the incoming CC matches any macro's learned leader, writes its value to that macro's follower
// CCs on the active MIDI clip. Returns whether it matched (and so consumed the message).
bool tryMacro(MIDICable& cable, int32_t channelOrZone, int32_t ccNumber, int32_t value);

// Flags the config as needing a save; writeToFile() is a no-op while unchanged.
void markDirty();

void writeToFile();
void readFromFile();

// Save/load a single macro as its own preset file, reusing the main file's <midiMacro><macro/>
// structure. writeMacroPreset() writes the body between the browser's createXMLFile() and
// closeFileAfterWriting(); loadMacroPreset() opens, reads one macro into macros[macroIndex], and
// marks the main config dirty so the loaded state also persists to MIDIMacro.XML.
void writeMacroPreset(Serializer& writer, int32_t macroIndex);
Error loadMacroPreset(FilePointer* fp, int32_t macroIndex);

// Captures the current live value of each configured follower CC on the active MIDI clip into that
// follower's from (toMax=false, capture A) or to (toMax=true, capture B). The leader CC then morphs
// the followers between the two via the sendToFollowers() interpolation.
void capture(int32_t macroIndex, bool toMax);
}; // namespace MIDIMacro
