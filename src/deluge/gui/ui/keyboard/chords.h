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
#include "model/scale/note_set.h"
#include <array>
// #include <vector>

constexpr int8_t kMaxChordKeyboardSize = 7;
constexpr int8_t kUniqueVoicings = 4;
constexpr int8_t kUniqueChords = 33;
constexpr int8_t kOffScreenChords = kUniqueChords - kDisplayHeight;

namespace deluge::gui::ui::keyboard {

enum class ChordQuality {
	MAJOR,
	MINOR,
	DIMINISHED,
	AUGMENTED,
	DOMINANT,
	OTHER,
	CHORD_QUALITY_MAX,
};

// Check and return the quality of a chord, assuming the notes are defined from the root, even if it is a rootless chord
ChordQuality getChordQuality(NoteSet& notes);

// Name a pitch class (0-11) with flats or sharps, e.g. preferFlats -> "Db", else "C#".
const char* noteNameInKey(uint8_t pitchClass, bool preferFlats);

// True if the key spells with flats (F/Bb/Eb/Ab/Db major + their relative minors), from the scale.
bool keyPrefersFlats(uint8_t keyRootPc, NoteSet scale);

// Try to name a chord from absolute note codes (e.g. "Db M7"), matching against the chord table and
// trying each note as the root so inversions still resolve. `preferFlats` picks the accidental style
// to match the key. Returns true and fills `out` on a match.
bool nameChordFromNotes(const uint8_t* notes, int32_t count, char* out, bool preferFlats);

// A suggested next chord: its root pitch-class (0-11) and the ChordList index of its diatonic quality.
struct ChordSuggestion {
	uint8_t rootNote; // pitch class 0-11 of a suggested next root; the library lights every diatonic
	int8_t chordNo;   // chord type at this root. chordNo is the diatonic 7th (kept for reference/scoring).
};

// Describe a held chord both ways for the live inspector: fills `absOut` with the absolute, key-spelled
// name (e.g. "Db M7") and `romanOut` with the Roman numeral in the key (e.g. "bVIM7", empty if the root
// isn't a diatonic degree of a 7-note scale). Returns true if the chord was recognised.
bool describeChordInKey(const uint8_t* notes, int32_t count, uint8_t keyRootPc, NoteSet scale, char* absOut,
                        char* romanOut);

// The "brain": given the key root, the scale (NoteSet of intervals from root, 7-note scales only),
// and the pitch class of the chord you're currently on, rank the diatonic chords you could go to next
// by a deep-house-weighted functional pull + voice-leading smoothness (ported from chord_suggest.py).
// Fills `out` with up to `maxOut` suggestions, best first. Returns the count (0 if not a 7-note scale
// or the current root isn't in the scale).
int suggestNextChords(uint8_t keyRoot, NoteSet scaleNotes, uint8_t scaleCount, uint8_t currentRootPc,
                      ChordSuggestion* out, int32_t maxOut);

// Interval offsets for convenience
const int8_t NONE = INT8_MAX;
const int8_t ROOT = 0;
const int8_t MIN2 = 1;
const int8_t MAJ2 = 2;
const int8_t MIN3 = 3;
const int8_t MAJ3 = 4;
const int8_t P4 = 5;
const int8_t AUG4 = 6;
const int8_t DIM5 = 6;
const int8_t P5 = 7;
const int8_t AUG5 = 8;
const int8_t MIN6 = 8;
const int8_t MAJ6 = 9;
const int8_t DIM7 = 9;
const int8_t MIN7 = 10;
const int8_t DOM7 = 10;
const int8_t MAJ7 = 11;
const int8_t OCT = kOctaveSize;
const int8_t MIN9 = MIN2 + OCT;
const int8_t MAJ9 = MAJ2 + OCT;
const int8_t MIN10 = MIN3 + OCT;
const int8_t MAJ10 = MAJ3 + OCT;
const int8_t P11 = P4 + OCT;
const int8_t AUG11 = AUG4 + OCT;
const int8_t DIM12 = DIM5 + OCT;
const int8_t P12 = P5 + OCT;
const int8_t MIN13 = MIN6 + OCT;
const int8_t MAJ13 = MAJ6 + OCT;
const int8_t MIN14 = MIN7 + OCT;
const int8_t MAJ14 = MAJ7 + OCT;

/// @brief A voicing is a set of offsets from the root note of a chord
struct Voicing {
	int8_t offsets[kMaxChordKeyboardSize];
	const char* supplementalName = "";
};

/// @brief A chord is a name and a set of voicings
struct Chord {
	const char* name;
	NoteSet intervalSet;
	Voicing voicings[kUniqueVoicings] = {0};
};

// ChordList
extern const Chord kEmptyChord;
extern const Chord kMajor;
extern const Chord kMinor;
extern const Chord k6;
extern const Chord k2;
extern const Chord k69;
extern const Chord kSus2;
extern const Chord kSus4;
extern const Chord k7;
extern const Chord k7Sus4;
extern const Chord k7Sus2;
extern const Chord kM7;
extern const Chord kMinor7;
extern const Chord kMinor2;
extern const Chord kMinor4;
extern const Chord kDim;
extern const Chord kFullDim;
extern const Chord kAug;
extern const Chord kMinor6;
extern const Chord kMinorMaj7;
extern const Chord kMinor7b5;
extern const Chord kMinor9b5;
extern const Chord kMinor7b5b9;
extern const Chord k9;
extern const Chord kM9;
extern const Chord kMinor9;
extern const Chord k11;
extern const Chord kM11;
extern const Chord kMinor11;
extern const Chord k13;
extern const Chord kM13;
extern const Chord kM13Sharp11;
extern const Chord kMinor13;

extern const std::array<const Chord, 10> majorChords;

extern const std::array<const Chord, 10> minorChords;

extern const std::array<const Chord, 10> dominantChords;

extern const std::array<const Chord, 10> diminishedChords;

extern const std::array<const Chord, 10> augmentedChords;

extern const std::array<const Chord, 10> otherChords;

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
	Voicing getChordVoicing(int8_t chordNo);

	void adjustChordRowOffset(int8_t offset);
	void adjustVoicingOffset(int8_t chordNo, int8_t offset);

	Chord chords[kUniqueChords];
	int8_t voicingOffset[kUniqueChords] = {0};
	uint8_t chordRowOffset = 0;

private:
	int8_t validateChordNo(int8_t chordNo);
};

/// @brief A fully-specified chord instance: a root note plus the chosen voicing.
///
/// This is the value object that flows through the harmonic-composition system. Chord selection
/// produces it; audition (keyboard) and commit (clip write) both consume it. It carries intent
/// (root + interval offsets + velocity), never resolved absolute-note data, so it stays small and
/// stable across views. See docs/HARMONIC_COMPOSITION_ARCHITECTURE.md.
struct ChordSelection {
	int32_t rootNote = 0; // absolute MIDI base note (e.g. 60 = C4)
	Voicing voicing{};    // chosen voicing (interval offsets from the root)
	int8_t chordNo = -1;  // index into ChordList, for naming / future re-voicing; -1 = none
	uint8_t velocity = 64;
};

/// @brief Resolve a ChordSelection into absolute MIDI note numbers.
///
/// Pure: depends only on the selection. Shared by audition and commit so the chord heard is exactly
/// the chord written. Writes up to @p maxNotes notes into @p notesOut and returns how many.
uint8_t resolveChordNotes(const ChordSelection& selection, int16_t* notesOut, uint8_t maxNotes);

} // namespace deluge::gui::ui::keyboard
