/*
 * Copyright (c) 2014-2024 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/chords.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include <array>
#include <stdlib.h>

namespace deluge::gui::ui::keyboard {

ChordQuality getChordQuality(NoteSet& notes) {
	if (notes.count() < 3) {
		return ChordQuality::OTHER;
	}
	else if (notes.has(MAJ3) && notes.has(P5)) {
		if (notes.has(MIN7)) {
			return ChordQuality::DOMINANT;
		}
		else {
			return ChordQuality::MAJOR;
		}
	}
	else if (notes.has(MIN3) && notes.has(P5)) {
		return ChordQuality::MINOR;
	}
	else if (notes.has(MIN3) && notes.has(DIM5)) {
		return ChordQuality::DIMINISHED;
	}
	else if (notes.has(MAJ3) && notes.has(AUG5)) {
		return ChordQuality::AUGMENTED;
	}
	else {
		return ChordQuality::OTHER;
	}
}

ChordList::ChordList()
    : chords{
          kEmptyChord, kMajor,  kMinor,  k6,      k2,   k69,      kSus2,    kSus4,   k7,         k7Sus4,      k7Sus2,
          kM7,         kMinor7, kMinor2, kMinor4, kDim, kFullDim, kAug,     kMinor6, kMinorMaj7, kMinor7b5,   kMinor9b5,
          kMinor7b5b9, k9,      kM9,     kMinor9, k11,  kM11,     kMinor11, k13,     kM13,       kM13Sharp11, kMinor13,
      } {
}

Voicing ChordList::getChordVoicing(int8_t chordNo) {
	// Check if chord number is valid
	chordNo = validateChordNo(chordNo);

	int8_t voicingNo = voicingOffset[chordNo];
	bool valid;
	if (voicingNo <= 0) {
		return chords[chordNo].voicings[0];
	}
	// Check if voicing is valid
	// voicings after the first should default to 0
	// So if the voicing is all 0, we should return the voicing before it
	else if (voicingNo > 0) {
		for (int voicingN = voicingNo; voicingN >= 0; voicingN--) {
			Voicing voicing = chords[chordNo].voicings[voicingN];

			bool valid = false;
			for (int j = 0; j < kMaxChordKeyboardSize; j++) {
				if (voicing.offsets[j] != 0) {
					valid = true;
				}
			}
			if (valid) {
				return voicing;
			}
		}
		if (!valid) {
			D_PRINTLN("Voicing is invalid, returning default voicing");
			return chords[0].voicings[0];
		}
	}
	return chords[chordNo].voicings[0];
}

void ChordList::adjustChordRowOffset(int8_t offset) {
	if (offset > 0) {
		chordRowOffset = std::min<int8_t>(kOffScreenChords, chordRowOffset + offset);
	}
	else {
		chordRowOffset = std::max<int8_t>(0, chordRowOffset + offset);
	}
}

void ChordList::adjustVoicingOffset(int8_t chordNo, int8_t offset) {
	// Check if chord number is valid
	chordNo = validateChordNo(chordNo);

	if (offset > 0) {
		voicingOffset[chordNo] = std::min<int8_t>(kUniqueVoicings - 1, voicingOffset[chordNo] + offset);
	}
	else {
		voicingOffset[chordNo] = std::max<int8_t>(0, voicingOffset[chordNo] + offset);
	}
}

int8_t ChordList::validateChordNo(int8_t chordNo) {
	if (chordNo < 0) {
		D_PRINTLN("Chord number is negative, returning chord 0");
		chordNo = 0;
	}
	else if (chordNo >= kUniqueChords) {
		D_PRINTLN("Chord number is too high, returning last chord");
		chordNo = kUniqueChords - 1;
	}
	return chordNo;
}
// ChordList
PLACE_SDRAM_DATA const Chord kEmptyChord = {"", NoteSet({ROOT}), {{0, NONE, NONE, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMajor = {"M",
                                       NoteSet({ROOT, MAJ3, P5}),
                                       {{ROOT, MAJ3, P5, NONE, NONE, NONE, NONE},
                                        {ROOT, OCT + MAJ3, P5, NONE, NONE, NONE, NONE},
                                        {ROOT, OCT + MAJ3, P5, -OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor = {"-",
                                       NoteSet({ROOT, MIN3, P5}),
                                       {{ROOT, MIN3, P5, NONE, NONE, NONE, NONE},
                                        {ROOT, OCT + MIN3, P5, NONE, NONE, NONE, NONE},
                                        {ROOT, OCT + MIN3, P5, -OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kDim = {"DIM",
                                     NoteSet({ROOT, MIN3, DIM5}),
                                     {{ROOT, MIN3, DIM5, NONE, NONE, NONE, NONE},
                                      {ROOT, OCT + MIN3, DIM5, NONE, NONE, NONE, NONE},
                                      {ROOT, OCT + MIN3, DIM5, -OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kFullDim = {
    "FULLDIM", NoteSet({ROOT, MIN3, DIM5, DIM7}), {{ROOT, MIN3, DIM5, DIM7, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kAug = {"AUG",
                                     NoteSet({ROOT, MIN3, AUG5}),
                                     {{ROOT, MIN3, AUG5, NONE, NONE, NONE, NONE},
                                      {ROOT, OCT + MIN3, AUG5, NONE, NONE, NONE, NONE},
                                      {ROOT, OCT + MIN3, AUG5, -OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kSus2 = {"SUS2",
                                      NoteSet({ROOT, MAJ2, P5}),
                                      {{ROOT, MAJ2, P5, NONE, NONE, NONE, NONE},
                                       {ROOT, MAJ2 + OCT, P5, NONE, NONE, NONE, NONE},
                                       {ROOT, MAJ2 + OCT, P5, -OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kSus4 = {"SUS4",
                                      NoteSet({ROOT, P4, P5}),
                                      {{ROOT, P4, P5, NONE, NONE, NONE, NONE},
                                       {ROOT, P4 + OCT, P5, NONE, NONE, NONE, NONE},
                                       {ROOT, P4 + OCT, P5, -OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord k7 = {"7",
                                   NoteSet({ROOT, MAJ3, P5, MIN7}),
                                   {{ROOT, MAJ3, P5, MIN7, NONE, NONE, NONE},
                                    {ROOT, MAJ3 + OCT, P5, MIN7, NONE, NONE, NONE},
                                    {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord k7Sus4 = {"7SUS4",
                                       NoteSet({ROOT, P4, P5, MIN7}),
                                       {{ROOT, P4, P5, MIN7, NONE, NONE, NONE},
                                        {ROOT, P4 + OCT, P5, MIN7, NONE, NONE, NONE},
                                        {ROOT, P4 + OCT, P5, MIN7 + OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord k7Sus2 = {"7SUS2",
                                       NoteSet({ROOT, MAJ2, P5, MIN7}),
                                       {{ROOT, MAJ2, P5, MIN7, NONE, NONE, NONE},
                                        {ROOT, MAJ2 + OCT, P5, MIN7, NONE, NONE, NONE},
                                        {ROOT, MAJ2 + OCT, P5, MIN7 + OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kM7 = {"M7",
                                    NoteSet({ROOT, MAJ3, P5, MAJ7}),
                                    {{ROOT, MAJ3, P5, MAJ7, NONE, NONE, NONE},
                                     {ROOT, MAJ3 + OCT, P5, MAJ7, NONE, NONE, NONE},
                                     {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor7 = {"-7",
                                        NoteSet({ROOT, MIN3, P5, MIN7}),
                                        {{ROOT, MIN3, P5, MIN7, NONE, NONE, NONE},
                                         {ROOT, MIN3 + OCT, P5, MIN7, NONE, NONE, NONE},
                                         {ROOT, MIN3 + OCT, P5, MIN7 + OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor2 = {"-2",
                                        NoteSet({ROOT, MIN3, P5, MAJ2}),
                                        {{ROOT, MIN3, P5, MAJ2, NONE, NONE, NONE},
                                         {ROOT, MIN3 + OCT, P5, MAJ2, NONE, NONE, NONE},
                                         {ROOT, MIN3 + OCT, P5 + OCT, MAJ2, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor4 = {"-4",
                                        NoteSet({ROOT, MIN3, P5, P4}),
                                        {{ROOT, MIN3, P5, P4, NONE, NONE, NONE},
                                         {ROOT, MIN3 + OCT, P5, P4, NONE, NONE, NONE},
                                         {ROOT, MIN3 + OCT, P5 + OCT, P4, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinorMaj7 = {"-M7",
                                           NoteSet({ROOT, MIN3, P5, MAJ7}),
                                           {{ROOT, MIN3, P5, MAJ7, NONE, NONE, NONE},
                                            {ROOT, MIN3 + OCT, P5, MAJ7, NONE, NONE, NONE},
                                            {ROOT, MIN3 + OCT, P5, MAJ7 + OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor7b5 = {"-7" FLAT_CHAR_STR "5",
                                          NoteSet({ROOT, MIN3, DIM5, MIN7}),
                                          {{ROOT, MIN3, DIM5, MIN7, NONE, NONE, NONE},
                                           {ROOT, MIN3 + OCT, DIM5, MIN7, NONE, NONE, NONE},
                                           {ROOT, MIN3 + OCT, DIM5, MIN7 + OCT, NONE, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor9b5 = {
    "-9" FLAT_CHAR_STR "5", NoteSet({ROOT, MIN3, DIM5, MIN7, MAJ2}), {{ROOT, MIN3, DIM5, MIN7, MAJ9, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor7b5b9 = {"-7" FLAT_CHAR_STR "5" FLAT_CHAR_STR "9",
                                            NoteSet({ROOT, MIN3, DIM5, MIN7, MIN2}),
                                            {{ROOT, MIN3, DIM5, MIN7, MIN9, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord k9 = {"9",
                                   NoteSet({ROOT, MAJ3, P5, MIN7, MAJ2}),
                                   {{ROOT, MAJ3, P5, MIN7, MAJ9, NONE, NONE},
                                    {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, NONE, NONE},
                                    {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kM9 = {"M9",
                                    NoteSet({ROOT, MAJ3, P5, MAJ7, MAJ2}),
                                    {{ROOT, MAJ3, P5, MAJ7, MAJ9, NONE, NONE},
                                     {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, NONE, NONE},
                                     {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor9 = {"-9",
                                        NoteSet({ROOT, MIN3, P5, MIN7, MAJ2}),
                                        {{ROOT, MIN3, P5, MIN7, MAJ9, NONE, NONE},
                                         {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, NONE, NONE},
                                         {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, NONE, NONE}}};
PLACE_SDRAM_DATA const Chord k11 = {"11",
                                    NoteSet({ROOT, MAJ3, P5, MIN7, MAJ2, P4}),
                                    {{ROOT, MAJ3, P5, MIN7, MAJ9, P11, NONE},
                                     {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, P11, NONE},
                                     {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, P11, NONE}}};
PLACE_SDRAM_DATA const Chord kM11 = {"M11",
                                     NoteSet({ROOT, MAJ3, P5, MAJ7, MAJ2, P4}),
                                     {{ROOT, MAJ3, P5, MAJ7, MAJ9, P11, NONE},
                                      {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, P11, NONE},
                                      {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, P11, NONE}}};
PLACE_SDRAM_DATA const Chord kMinor11 = {"-11",
                                         NoteSet({ROOT, MIN3, P5, MIN7, MAJ2, P4}),
                                         {{ROOT, MIN3, P5, MIN7, MAJ9, P11, NONE},
                                          {{ROOT, P4, MIN7, MIN3 + OCT, P5 + OCT, NONE, NONE}, "SO WHAT"},
                                          {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, P11, NONE},
                                          {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, P11, NONE}}};
// 11th are often omitted in 13th and M13th chords because they clash with the major 3rd
// if anything, the 11th is often played as a #11
PLACE_SDRAM_DATA const Chord k13 = {"13",
                                    NoteSet({ROOT, MAJ3, P5, MIN7, MAJ2, MAJ6}),
                                    {{ROOT, MAJ3, P5, MIN7, MAJ9, MAJ13, NONE},
                                     {ROOT, MAJ3 + OCT, P5, MIN7, MAJ9, MAJ13, NONE},
                                     {ROOT, MAJ3 + OCT, P5, MIN7 + OCT, MAJ9, MAJ13, NONE}}};
PLACE_SDRAM_DATA const Chord kM13 = {"M13",
                                     NoteSet({ROOT, MAJ3, P5, MAJ7, MAJ2, MAJ6}),
                                     {{ROOT, MAJ3, P5, MAJ7, MAJ9, MAJ13, NONE},
                                      {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, MAJ13, NONE},
                                      {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, MAJ13, NONE}}};
PLACE_SDRAM_DATA const Chord kM13Sharp11 = {"M13#11",
                                            NoteSet({ROOT, MAJ3, P5, MAJ7, MAJ2, MAJ6, AUG4}),
                                            {{ROOT, MAJ3, P5, MAJ7, MAJ9, MAJ13, AUG11},
                                             {ROOT, MAJ3 + OCT, P5, MAJ7, MAJ9, MAJ13, AUG11},
                                             {ROOT, MAJ3 + OCT, P5, MAJ7 + OCT, MAJ9, MAJ13, AUG11}}};
PLACE_SDRAM_DATA const Chord kMinor13 = {"-13",
                                         NoteSet({ROOT, MIN3, P5, MIN7, MAJ2, P4, MAJ6}),
                                         {{ROOT, MIN3, P5, MIN7, MAJ9, P11, MAJ13},
                                          {ROOT, MIN3 + OCT, P5, MIN7, MAJ9, P11, MAJ13},
                                          {ROOT, MIN3 + OCT, P5, MIN7 + OCT, MAJ9, P11, MAJ13}}};
PLACE_SDRAM_DATA const Chord k6 = {"6",
                                   NoteSet({ROOT, MAJ3, P5, MAJ6}),
                                   {
                                       {ROOT, MAJ3, P5, MAJ6, NONE, NONE, NONE},
                                   }};
PLACE_SDRAM_DATA const Chord k2 = {"2",
                                   NoteSet({ROOT, MAJ3, P5, MAJ2}),
                                   {
                                       {{ROOT, MAJ3 - OCT, P5, MAJ2, NONE, NONE, NONE}, "Open Mu"},
                                       {{ROOT, MAJ3, P5, MAJ2, NONE, NONE, NONE}, "Mu"},
                                   }};
PLACE_SDRAM_DATA const Chord k69 = {"69",
                                    NoteSet({ROOT, MAJ3, P5, MAJ6, MAJ2}),
                                    {
                                        {ROOT, MAJ3, P5, MAJ6, MAJ9, NONE, NONE},
                                    }};
PLACE_SDRAM_DATA const Chord kMinor6 = {"-6",
                                        NoteSet({ROOT, MIN3, P5, MAJ6}),
                                        {
                                            {ROOT, MIN3, P5, MAJ6, NONE, NONE, NONE},
                                        }};

PLACE_SDRAM_DATA const std::array<Chord, 10> majorChords = {kMajor, kM7,  k6,    k2,    k69,
                                                            kM9,    kM13, kSus4, kSus2, kM13Sharp11};

PLACE_SDRAM_DATA const std::array<Chord, 10> minorChords = {
    kMinor, kMinor7, kMinor4, kMinor11, kMinor6, kMinor2, kEmptyChord, kEmptyChord, kEmptyChord, kEmptyChord,
};

PLACE_SDRAM_DATA const std::array<Chord, 10> dominantChords = {
    kMajor, k7, k69, k9, k7Sus4, k7Sus2, k11, k13, kEmptyChord, kEmptyChord,
};

PLACE_SDRAM_DATA const std::array<Chord, 10> diminishedChords = {
    kDim,        kMinor7b5,   kMinor7b5b9, kEmptyChord, kEmptyChord,
    kEmptyChord, kEmptyChord, kEmptyChord, kEmptyChord, kEmptyChord,
};

PLACE_SDRAM_DATA const std::array<Chord, 10> augmentedChords = {
    kAug,        kEmptyChord, kEmptyChord, kEmptyChord, kEmptyChord,
    kEmptyChord, kEmptyChord, kEmptyChord, kEmptyChord, kEmptyChord,
};

PLACE_SDRAM_DATA const std::array<Chord, 10> otherChords = {
    kSus2,       kSus4,       kEmptyChord, kEmptyChord, kEmptyChord,
    kEmptyChord, kEmptyChord, kEmptyChord, kEmptyChord, kEmptyChord,
};

} // namespace deluge::gui::ui::keyboard
