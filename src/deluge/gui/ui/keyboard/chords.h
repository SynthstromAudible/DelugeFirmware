/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

constexpr int32_t kMaxChordKeyboardSize = 7;
constexpr int32_t kUniqueVoicings = 3;
constexpr int32_t kUniqueChords = 17;

namespace deluge::gui::ui::keyboard {

/// @brief A voicing is a set of offsets from the root note of a chord
struct Voicing {
	int32_t offsets[kMaxChordKeyboardSize];
};

/// @brief A chord is a name and a set of voicings
struct Chord {
	const char* name;
	Voicing voicings[kUniqueVoicings] = {0};
};

// Interval offsets for convenience
const int32_t NON = INT32_MAX;
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

// Chords
const Chord kMajor = {"M",
                      {{ROOT, MAJ3, P5, NON, NON, NON, NON},
                       {ROOT, OCT + MAJ3, P5, NON, NON, NON, NON},
                       {ROOT, OCT + MAJ3, P5, -12, NON, NON, NON}}};
const Chord kMinor = {"-",
                      {{ROOT, MIN3, P5, NON, NON, NON, NON},
                       {ROOT, OCT + MIN3, P5, NON, NON, NON, NON},
                       {ROOT, OCT + MIN3, P5, -12, NON, NON, NON}}};
const Chord kSus2 = {"SUS2",
                     {{ROOT, 2, P5, NON, NON, NON, NON},
                      {ROOT, 2 + OCT, P5, NON, NON, NON, NON},
                      {ROOT, 2 + OCT, P5, -12, NON, NON, NON}}};
const Chord kSus4 = {"SUS4",
                     {{ROOT, 5, P5, NON, NON, NON, NON},
                      {ROOT, 5 + OCT, P5, NON, NON, NON, NON},
                      {ROOT, 5 + OCT, P5, -12, NON, NON, NON}}};
const Chord k7 = {"7",
                  {{ROOT, MAJ3, P5, MIN7, NON, NON, NON},
                   {ROOT, MAJ3 + OCT, P5, MIN7, NON, NON, NON},
                   {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, NON, NON, NON}}};
const Chord kM7 = {"M7",
                   {{ROOT, MAJ3, P5, MAJ7, NON, NON, NON},
                    {ROOT, MAJ3 + OCT, P5, MAJ7, NON, NON, NON},
                    {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, NON, NON, NON}}};
const Chord kMinor7 = {"-7",
                       {{ROOT, MIN3, P5, MIN7, NON, NON, NON},
                        {ROOT, MIN3 + OCT, P5, MIN7, NON, NON, NON},
                        {ROOT, MIN3 + OCT, P5, MIN7 + OCT, NON, NON, NON}}};
const Chord k9 = {"9",
                  {{ROOT, MAJ3, P5, MIN7, MAJ9, NON, NON},
                   {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, NON, NON},
                   {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, NON, NON}}};
const Chord kM9 = {"M9",
                   {{ROOT, MAJ3, P5, MAJ7, MAJ9, NON, NON},
                    {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, NON, NON},
                    {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, NON, NON}}};
const Chord kMinor9 = {"-9",
                       {{ROOT, MIN3, P5, MIN7, MAJ9, NON, NON},
                        {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, NON, NON},
                        {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, NON, NON}}};
const Chord k11 = {"11",
                   {{ROOT, MAJ3, P5, MIN7, MAJ9, P11, NON},
                    {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, P11, NON},
                    {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, P11, NON}}};
const Chord kM11 = {"M11",
                    {{ROOT, MAJ3, P5, MAJ7, MAJ9, P11, NON},
                     {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, P11, NON},
                     {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, P11, NON}}};
const Chord kMinor11 = {"-11",
                        {{ROOT, MIN3, P5, MIN7, MAJ9, P11, NON},
                         {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, P11, NON},
                         {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, P11, NON}}};
// 11th are often omitted in 13th chords because they clash with the major 3rd
// if anything, the 11th is often played as a #11
const Chord k13 = {"13",
                   {{ROOT, MAJ3, P5, MIN7, MAJ9, MAJ13, NON},
                    {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, MAJ13, NON},
                    {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, MAJ13, NON}}};
const Chord kM13 = {"M13",
                    {{ROOT, MAJ3, P5, MAJ7, MAJ9, MAJ13, NON},
                     {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, MAJ13, NON},
                     {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, MAJ13, NON}}};
const Chord kMinor13 = {"-13",
                        {{ROOT, MIN3, P5, MIN7, MAJ9, P11, MAJ13},
                         {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, P11, MAJ13},
                         {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, P11, MAJ13}}};

/// @brief A collection of chords
class Chords {
public:
	Chords();

	/**
	 * @brief Get a voicing for a chord with a given index. If the index is past the unique voicings, the final voicing
	 * is returned.
	 *
	 * @param chordNo The index of the chord
	 * @return The voicing
	 */
	Voicing getVoicing(int32_t chordNo);

	Chord chords[kUniqueChords];
	int32_t voicingOffset[kUniqueChords] = {0};
};

} // namespace deluge::gui::ui::keyboard
