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

constexpr int32_t kMaxChordKeyboardSize = 7;
constexpr int32_t kUniqueVoicings = 4;
constexpr int32_t kUniqueChords = 20;
constexpr int32_t kOffScreenChords = kUniqueChords - kDisplayHeight;

namespace deluge::gui::ui::keyboard {

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
const Chord kMajor = {"M",
                      NoteSet({ROOT, MAJ3, P5}),
                      {{ROOT, MAJ3, P5, NONE, NONE, NONE, NONE},
                       {ROOT, OCT + MAJ3, P5, NONE, NONE, NONE, NONE},
                       {ROOT, OCT + MAJ3, P5, -OCT, NONE, NONE, NONE}}};
const Chord kMinor = {"-",
                      NoteSet({ROOT, MIN3, P5}),
                      {{ROOT, MIN3, P5, NONE, NONE, NONE, NONE},
                       {ROOT, OCT + MIN3, P5, NONE, NONE, NONE, NONE},
                       {ROOT, OCT + MIN3, P5, -OCT, NONE, NONE, NONE}}};
const Chord kDim = {"DIM",
                    NoteSet({ROOT, MIN3, DIM5}),
                    {{ROOT, MIN3, DIM5, NONE, NONE, NONE, NONE},
                     {ROOT, OCT + MIN3, DIM5, NONE, NONE, NONE, NONE},
                     {ROOT, OCT + MIN3, DIM5, -OCT, NONE, NONE, NONE}}};
const Chord kAug = {"AUG",
                    NoteSet({ROOT, MIN3, AUG5}),
                    {{ROOT, MIN3, AUG5, NONE, NONE, NONE, NONE},
                     {ROOT, OCT + MIN3, AUG5, NONE, NONE, NONE, NONE},
                     {ROOT, OCT + MIN3, AUG5, -OCT, NONE, NONE, NONE}}};
const Chord kSus2 = {"SUS2",
                     NoteSet({ROOT, MAJ2, P5}),
                     {{ROOT, MAJ2, P5, NONE, NONE, NONE, NONE},
                      {ROOT, MAJ2 + OCT, P5, NONE, NONE, NONE, NONE},
                      {ROOT, MAJ2 + OCT, P5, -OCT, NONE, NONE, NONE}}};
const Chord kSus4 = {"SUS4",
                     NoteSet({ROOT, P4, P5}),
                     {{ROOT, P4, P5, NONE, NONE, NONE, NONE},
                      {ROOT, P4 + OCT, P5, NONE, NONE, NONE, NONE},
                      {ROOT, P4 + OCT, P5, -OCT, NONE, NONE, NONE}}};
const Chord k7 = {"7",
                  NoteSet({ROOT, MAJ3, P5, MIN7}),
                  {{ROOT, MAJ3, P5, MIN7, NONE, NONE, NONE},
                   {ROOT, MAJ3 + OCT, P5, MIN7, NONE, NONE, NONE},
                   {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, NONE, NONE, NONE}}};
const Chord kM7 = {"M7",
                   NoteSet({ROOT, MAJ3, P5, MAJ7}),
                   {{ROOT, MAJ3, P5, MAJ7, NONE, NONE, NONE},
                    {ROOT, MAJ3 + OCT, P5, MAJ7, NONE, NONE, NONE},
                    {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, NONE, NONE, NONE}}};
const Chord kMinor7 = {"-7",
                       NoteSet({ROOT, MIN3, P5, MIN7}),
                       {{ROOT, MIN3, P5, MIN7, NONE, NONE, NONE},
                        {ROOT, MIN3 + OCT, P5, MIN7, NONE, NONE, NONE},
                        {ROOT, MIN3 + OCT, P5, MIN7 + OCT, NONE, NONE, NONE}}};
const Chord kMinor7b5 = {"-7flat5",
                         NoteSet({ROOT, MIN3, DIM5, MIN7}),
                         {{ROOT, MIN3, DIM5, MIN7, NONE, NONE, NONE},
                          {ROOT, MIN3 + OCT, DIM5, MIN7, NONE, NONE, NONE},
                          {ROOT, MIN3 + OCT, DIM5, MIN7 + OCT, NONE, NONE, NONE}}};
const Chord k9 = {"9",
                  NoteSet({ROOT, MAJ3, P5, MIN7, MAJ2}),
                  {{ROOT, MAJ3, P5, MIN7, MAJ9, NONE, NONE},
                   {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, NONE, NONE},
                   {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, NONE, NONE}}};
const Chord kM9 = {"M9",
                   NoteSet({ROOT, MAJ3, P5, MAJ7, MAJ2}),
                   {{ROOT, MAJ3, P5, MAJ7, MAJ9, NONE, NONE},
                    {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, NONE, NONE},
                    {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, NONE, NONE}}};
const Chord kMinor9 = {"-9",
                       NoteSet({ROOT, MIN3, P5, MIN7, MAJ2}),
                       {{ROOT, MIN3, P5, MIN7, MAJ9, NONE, NONE},
                        {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, NONE, NONE},
                        {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, NONE, NONE}}};
const Chord k11 = {"11",
                   NoteSet({ROOT, MAJ3, P5, MIN7, MAJ2, P4}),
                   {{ROOT, MAJ3, P5, MIN7, MAJ9, P11, NONE},
                    {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, P11, NONE},
                    {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, P11, NONE}}};
const Chord kM11 = {"M11",
                    NoteSet({ROOT, MAJ3, P5, MAJ7, MAJ2, P4}),
                    {{ROOT, MAJ3, P5, MAJ7, MAJ9, P11, NONE},
                     {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, P11, NONE},
                     {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, P11, NONE}}};
const Chord kMinor11 = {"-11",
                        NoteSet({ROOT, MIN3, P5, MIN7, MAJ2, P4}),
                        {{ROOT, MIN3, P5, MIN7, MAJ9, P11, NONE},
                         {{ROOT, P4, MIN7, MIN3 + OCT, P5 + OCT, NONE, NONE}, "SO WHAT"},
                         {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, P11, NONE},
                         {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, P11, NONE}}};
// 11th are often omitted in 13th and M13th chords because they clash with the major 3rd
// if anything, the 11th is often played as a #11
const Chord k13 = {"13",
                   NoteSet({ROOT, MAJ3, P5, MIN7, MAJ2, MAJ6}),
                   {{ROOT, MAJ3, P5, MIN7, MAJ9, MAJ13, NONE},
                    {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, MAJ13, NONE},
                    {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, MAJ13, NONE}}};
const Chord kM13 = {"M13",
                    NoteSet({ROOT, MAJ3, P5, MAJ7, MAJ2, MAJ6}),
                    {{ROOT, MAJ3, P5, MAJ7, MAJ9, MAJ13, NONE},
                     {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, MAJ13, NONE},
                     {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, MAJ13, NONE}}};
const Chord kMinor13 = {"-13",
                        NoteSet({ROOT, MIN3, P5, MIN7, MAJ2, P4, MAJ6}),
                        {{ROOT, MIN3, P5, MIN7, MAJ9, P11, MAJ13},
                         {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, P11, MAJ13},
                         {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, P11, MAJ13}}};

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
