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

#include "model/song/song.h"
#include "model/clip/instrument_clip.h"

#include "hid/button.h"
#include <string.h>
#include <array>

#define INVALID_NOTE -1
#define MAX_NUM_KEYBOARD_PAD_PRESSES 10
#define MAX_NUM_ACTIVE_NOTES 10

namespace keyboard {

struct PressedPad {
	uint8_t x;
	uint8_t y;
	bool active;
};

struct NoteState {
	uint8_t note = 0;
	uint8_t velocity = 0;
	int16_t mpeValues[3] = {0};
	bool generatedNote = false;
};

struct NotesState {
	uint64_t states[2] = {0};
	NoteState notes[MAX_NUM_ACTIVE_NOTES] = {0};
	uint8_t count = 0;

	void enableNote(uint8_t note, uint8_t velocity) { //@TODO: Add MPE values
		if (noteEnabled(note)) {
			return;
		}
		notes[count].note = note;
		notes[count].velocity = velocity;
		states[(note >= 64 ? 1 : 0)] |= (1ull << (note >= 64 ? (note - 64) : note));
		count++;
	}

	bool noteEnabled(uint8_t note) {
		uint64_t expectedValue = (1ull << (note >= 64 ? (note - 64) : note));
		return (states[(note >= 64 ? 1 : 0)] & expectedValue) == expectedValue;
	}
};

class KeyboardLayout {
public:
	KeyboardLayout() {}
	virtual ~KeyboardLayout() {}

	// Handle inputs
	virtual NotesState evaluatePads(PressedPad presses[MAX_NUM_KEYBOARD_PAD_PRESSES]) = 0;
	virtual void handleVerticalEncoder(
	    int offset) = 0; // returns weather the scroll had an effect // Shift state not supplied since that function is already taken
	virtual void handleHorizontalEncoder(int offset, bool shiftEnabled) = 0; // returns weather the scroll had an effect

	// Handle output
	virtual void renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {}
	virtual void renderSidebarPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
		// Empty if not implemented
		for (int y = 0; y < kDisplayHeight; y++) {
			memset(image[y][kDisplayWidth], 0, kSideBarWidth * 3);
		}
	};

	virtual void stopAllNotes() = 0;

	// Properties
	virtual bool supportsInstrument() { return false; }
	virtual bool supportsKit() { return false; }

	//@TODO: This also needs velocity
	//virtual std::array<int, MAX_NUM_KEYBOARD_PAD_PRESSES> getActiveNotes() = 0;

	//@TODO: rootNote(), scale(), saving, restoring

protected:
	inline int getRootNote() { return currentSong->rootNote; }

	int getLowestClipNote() {
		//@TODO:
		return 0;
	}

	int getHighestClipNote() {
		return 127; // @TODO:
	}

	// Helpers
	bool notesFull() {
		return false; //@TODO: Check for max activeNotes
	}

	// void enableNote(int note) {
	// 	//@TODO: Add to activeNotes
	// }

	void disableNote(int note) {
		//@TODO: Remove from activeNotes
	}

	// inline uint8_t[kDisplayHeight * KEYBOARD_ROW_INTERVAL_MAX + kDisplayWidth][3] colours() {
	// 	return noteColours;
	// }

	Instrument* getActiveInstrument() { return (Instrument*)currentSong->currentClip->output; }

protected:
	//NoteList activeNotes; //@TODO: Add velocity to active notes
};

}; // namespace keyboard
