/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
 * Copyright © 2024 Madeline Scyphers
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

#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "model/scale/note_set.h"
#include <array>

constexpr int32_t kMaxChordKeyboardSize = 7;
constexpr int32_t kUniqueVoicings = 4;
constexpr int32_t kUniqueChords = 20;
constexpr int32_t kOffScreenChords = kUniqueChords - kDisplayHeight;

namespace deluge::gui::ui::keyboard {

enum ChordQuality : int8_t {
	MAJOR = 0,
	MINOR,
	DIMINISHED,
	AUGMENTED,
	DOMINANT,
	OTHER,
	CHORD_QUALITY_MAX,
};

const std::array<RGB, CHORD_QUALITY_MAX> qualityColours{
    {colours::blue, colours::purple, colours::green, colours::kelly::very_light_blue, colours::cyan, colours::yellow}};

// Check and return the quality of a chord, assuming the notes are defined from the root, even if it is a rootless chord
ChordQuality getChordQuality(NoteSet& notes);

/// @brief A voicing is a set of offsets from the root note of a chord

// Interval offsets for convenience
const int32_t NONE = INT32_MAX;
const int32_t ROOT = 0;
const int32_t MIN2 = 1;
const int32_t MAJ2 = 2;
const int32_t MIN3 = 3;
const int32_t MAJ3 = 4;
const int32_t P4 = 5;
const int32_t AUG4 = 6;
const int32_t DIM5 = 6;
const int32_t P5 = 7;
const int32_t AUG5 = 8;
const int32_t MIN6 = 8;
const int32_t MAJ6 = 9;
const int32_t MIN7 = 10;
const int32_t DOM7 = 10;
const int32_t MAJ7 = 11;
const int32_t OCT = kOctaveSize;
const int32_t MIN9 = MIN2 + OCT;
const int32_t MAJ9 = MAJ2 + OCT;
const int32_t MIN10 = MIN3 + OCT;
const int32_t MAJ10 = MAJ3 + OCT;
const int32_t P11 = P4 + OCT;
const int32_t AUG11 = AUG4 + OCT;
const int32_t DIM12 = DIM5 + OCT;
const int32_t P12 = P5 + OCT;
const int32_t MIN13 = MIN6 + OCT;
const int32_t MAJ13 = MAJ6 + OCT;
const int32_t MIN14 = MIN7 + OCT;
const int32_t MAJ14 = MAJ7 + OCT;

struct Voicing {
	int32_t offsets[kMaxChordKeyboardSize];
	const char* supplementalName = "";
};

/// @brief A chord is a name and a set of voicings
struct Chord {
	const char* name;
	NoteSet intervalSet;
	Voicing voicings[kUniqueVoicings] = {0};
};

// ChordList
extern const Chord kMajor;
extern const Chord kMinor;
extern const Chord kDim;
extern const Chord kAug;
extern const Chord kSus2;
extern const Chord kSus4;
extern const Chord k7;
extern const Chord kM7;
extern const Chord kMinor7;
extern const Chord kMinor7b5;
extern const Chord k9;
extern const Chord kM9;
extern const Chord kMinor9;
extern const Chord k11;
extern const Chord kM11;
extern const Chord kMinor11;
extern const Chord k13;
extern const Chord kM13;
extern const Chord kMinor13;

/// @brief A collection of chords
class ChordList {
public:
	ChordList();

	/**
	 * @brief Get a voicing for a chord with a given index. If the voicingOffset for that Chord is out of bounds,
	 * return the max or min voicing depending on the direction.
	 *
	 * @param chordNo The index of the chord
	 * @return The voicing
	 */
	Voicing getChordVoicing(int32_t chordNo);

	void adjustChordRowOffset(int32_t offset);
	void adjustVoicingOffset(int32_t chordNo, int32_t offset);

	Chord chords[kUniqueChords];
	int32_t voicingOffset[kUniqueChords] = {0};
	uint32_t chordRowOffset = 0;

private:
	int32_t validateChordNo(int32_t chordNo);
};

} // namespace deluge::gui::ui::keyboard
