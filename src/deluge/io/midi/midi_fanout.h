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

// Groups of one learned incoming source CC fanned out to up to kNumDestSlots destination CCs
// on the active MIDI clip. Dest values are written into the clip's own CC params, so they reach
// the clip's output and record into each CC's automation lane. The source CC is consumed; to
// forward/record it too, include it as one of its group's dests.
// Configured via SETTINGS > MIDI > FAN-OUT and persisted in SETTINGS/MIDIFanOut.XML on the SD card.
namespace MIDIFanOut {

constexpr int32_t kNumGroups = 4;
constexpr int32_t kNumDestSlots = 8;
constexpr uint8_t kDestCCNone = 255;
constexpr uint8_t kMaxDestCC = 127;
constexpr uint8_t kMaxValue = 127;  // from/to and CC output range
constexpr uint8_t kDefaultFrom = 0; // output at source = 0
constexpr uint8_t kDefaultTo = 127; // output at source = 127 (defaults give pass-through)

// One destination CC slot: the source value 0..127 is scaled linearly onto [from, to]:
//   out = clamp(from + (to - from) * input / 127, 0, 127)
// Set from > to to invert the response; from == to sends a constant.
struct FanOutDestSlot {
	uint8_t cc = kDestCCNone;    // kDestCCNone = OFF, else 0..kMaxDestCC
	uint8_t from = kDefaultFrom; // 0..kMaxValue, output when source is at 0
	uint8_t to = kDefaultTo;     // 0..kMaxValue, output when source is at 127 (from>to inverts)
	bool enabled = true;         // muted when false, but keeps cc/from/to
};

struct FanOutGroup {
	LearnedMIDI source;
	bool enabled = false; // master enable, off by default; turn a group on manually to use it
	FanOutDestSlot dests[kNumDestSlots];
};

extern FanOutGroup groups[kNumGroups];

// Folder for per-group snapshot files, a sibling of MIDIFanOut.XML under SETTINGS.
constexpr char const* kSnapshotsFolder = "SETTINGS/SNAPSHOTS";

// Which group a template browser (load/save) is acting on. Set by the menu item before opening the
// browser UI, read by the UI's performLoad()/performSave().
extern int32_t templateGroupIndex;

// If the incoming CC matches any group's learned source, writes its value to that group's
// destination CCs on the active MIDI clip. Returns whether it matched (and so consumed the message).
bool tryFanOut(MIDICable& cable, int32_t channelOrZone, int32_t ccNumber, int32_t value);

// Flags the config as needing a save; writeToFile() is a no-op while unchanged.
void markDirty();

void writeToFile();
void readFromFile();

// Save/load a single group as its own template file, reusing the main file's <midiFanOut><group/>
// structure. writeGroupTemplate() writes the body between the browser's createXMLFile() and
// closeFileAfterWriting(); loadGroupTemplate() opens, reads one group into groups[groupIndex], and
// marks the main config dirty so the loaded state also persists to MIDIFanOut.XML.
void writeGroupTemplate(Serializer& writer, int32_t groupIndex);
Error loadGroupTemplate(FilePointer* fp, int32_t groupIndex);

// Captures the current live value of each configured destination CC on the active MIDI clip into
// that dest's from (toMax=false, the "min" snapshot) or to (toMax=true, the "max" snapshot). The
// source CC then morphs between the two via the existing sendToDests() interpolation.
void captureSnapshot(int32_t groupIndex, bool toMax);
}; // namespace MIDIFanOut
