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
#include "io/debug/log.h"
#include <stdlib.h>

namespace deluge::gui::ui::keyboard {

ChordList::ChordList()
    : chords{{"", {{0, NONE, NONE, NONE, NONE, NONE}}},  // TODO remove when root fixed on bottom of keyboard
             kMajor,
             kMinor,
			 kDim,
             kM7,
             k7,
             kMinor7,
			 kMinor7b5,
             kSus4,
             kSus2,
			 kAug,
             kM9,
             k9,
             kMinor9,
             kM11,
             k11,
             kMinor11,
             kM13,
             k13,
             kMinor13} {
}

Voicing ChordList::getChordVoicing(int32_t chordNo) {
	// Check if chord number is valid
	chordNo = validateChordNo(chordNo);

	int32_t voicingNo = voicingOffset[chordNo];
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

void ChordList::adjustChordRowOffset(int32_t offset) {
	if (offset > 0) {
		chordRowOffset = std::min<int32_t>(kOffScreenChords, chordRowOffset + offset);
	}
	else {
		chordRowOffset = std::max<int32_t>(0, chordRowOffset + offset);
	}
}

void ChordList::adjustVoicingOffset(int32_t chordNo, int32_t offset) {
	// Check if chord number is valid
	chordNo = validateChordNo(chordNo);

	if (offset > 0) {
		voicingOffset[chordNo] =
			std::min<int32_t>(kUniqueVoicings - 1, voicingOffset[chordNo] + offset);
	}
	else {
		voicingOffset[chordNo] =
		std::max<int32_t>(0, voicingOffset[chordNo] + offset);
	}
}

int32_t ChordList::validateChordNo(int32_t chordNo) {
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

} // namespace deluge::gui::ui::keyboard
