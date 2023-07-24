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

#include "gui/ui/keyboard/state_data.h"
#include "model/song/song.h"
#include "model/instrument/instrument.h"
#include "model/clip/instrument_clip.h"
#include "hid/button.h"
#include <string.h>
#include <array>

#define INVALID_NOTE -1
#define MAX_NUM_KEYBOARD_PAD_PRESSES 10
#define MAX_NUM_ACTIVE_NOTES 10

inline InstrumentClip* currentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

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

constexpr uint8_t kLowestKeyboardNote = 0;
constexpr uint8_t kHighestKeyboardNote = 12 * 12;
struct NotesState {
	uint64_t states[3] = {0};
	NoteState notes[MAX_NUM_ACTIVE_NOTES] = {0};
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

class KeyboardLayout {
public:
	KeyboardLayout() {}
	virtual ~KeyboardLayout() {}

	// Handle inputs
	virtual void evaluatePads(PressedPad presses[MAX_NUM_KEYBOARD_PAD_PRESSES]) = 0;
	virtual void handleVerticalEncoder(int offset) = 0; // Shift state not supplied since that function is already taken
	virtual void handleHorizontalEncoder(int offset, bool shiftEnabled) = 0; // returns weather the scroll had an effect
	virtual void recalculate() = 0; // This function is called on visibility change and if color offset changes

	// Handle output
	virtual void renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {}
	virtual void renderSidebarPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
		// Empty if not implemented
		for (int y = 0; y < kDisplayHeight; y++) {
			memset(image[y][kDisplayWidth], 0, kSideBarWidth * 3);
		}
	};

	// Properties
	virtual char* name() = 0;
	virtual bool supportsInstrument() { return false; }
	virtual bool supportsKit() { return false; }

	virtual NotesState* getNotesState() { return &currentNotesState; }

protected:
	inline int16_t getRootNote() { return currentSong->rootNote; }
	inline bool getScaleModeEnabled() { return currentClip()->inScaleMode; }
	inline uint8_t getScaleNoteCount() { return currentSong->numModeNotes; }
	inline uint8_t* getScaleNotes() { return currentSong->modeNotes; }
	inline uint8_t getDefaultVelocity() { return ((Instrument*)currentSong->currentClip->output)->defaultVelocity; }

	inline int getLowestClipNote() { return kLowestKeyboardNote; }
	inline int getHighestClipNote() {
		//@TODO: Fix for Kits
		return kHighestKeyboardNote;
	}

	inline void getNoteColour(uint8_t note, uint8_t rgb[]) { currentClip()->getMainColourFromY(note, 0, rgb); }

	inline KeyboardStateData* getState() { return &(currentClip()->keyboardState); }

protected:
	NotesState currentNotesState;
};

}; // namespace keyboard
