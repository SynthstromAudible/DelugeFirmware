/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include <cstdint>
#include <cstring>

constexpr uint8_t kMaxNumActiveNotes = 10;

namespace deluge::gui::ui::keyboard {

struct PressedPad : Cartesian {
	bool active;
};

struct NoteState {
	uint8_t note = 0;
	uint8_t velocity = 0;
	int16_t mpeValues[3] = {0};
	/// Generated notes will only create sound and not be used for interaction (e.g. setting root note)
	bool generatedNote = false;
};

// Always needs to be 0 currently for the math to math
constexpr uint8_t kLowestKeyboardNote = 0;
constexpr uint8_t kHighestKeyboardNote = kOctaveSize * 12;
struct NotesState {
	uint64_t states[3] = {0};
	NoteState notes[kMaxNumActiveNotes] = {0};
	uint8_t count = 0;

	void enableNote(uint8_t note, uint8_t velocity, bool generatedNote = false, int16_t* mpeValues = nullptr) {
		if (noteEnabled(note)) {
			return;
		}
		notes[count].note = note;
		notes[count].velocity = velocity;
		notes[count].generatedNote = generatedNote;
		if (mpeValues != nullptr) {
			memcpy(&notes[count].mpeValues, mpeValues, sizeof(notes[count].mpeValues));
		}

		states[(note / 64)] |= (1ull << (note % 64));
		count++;
	}

	bool noteEnabled(uint8_t note) {
		uint64_t expectedValue = (1ull << (note % 64));
		return (states[(note / 64)] & expectedValue) == expectedValue;
	}
};

}; // namespace deluge::gui::ui::keyboard
