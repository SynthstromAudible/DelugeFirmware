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
#include "gui/ui/keyboard/notes_state.h"
#include "gui/ui/keyboard/state_data.h"
#include "hid/button.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

constexpr uint8_t kMaxNumKeyboardPadPresses = 10;

namespace deluge::gui::ui::keyboard {

inline InstrumentClip* currentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

inline Instrument* currentInstrument() {
	return (Instrument*)currentSong->currentClip->output;
}

struct PressedPad : Cartesian {
	bool active;
};

enum class RequiredScaleMode : uint8_t {
	Undefined = 0,
	Disabled = 1,
	Enabled = 2,
};

constexpr uint8_t kModesArraySize = kOctaveSize;
typedef uint8_t ModesArray[kModesArraySize];

class KeyboardLayout {
public:
	KeyboardLayout() = default;
	virtual ~KeyboardLayout() {}

	/// Handle input pad presses
	virtual void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) = 0;

	/// Shift state not supplied since that function is already taken
	virtual void handleVerticalEncoder(int32_t offset) = 0;

	/// Will be called with offset 0 to recalculate bounds on clip changes
	virtual void handleHorizontalEncoder(int32_t offset, bool shiftEnabled) = 0;

	/// This function is called on visibility change and if color offset changes
	virtual void precalculate() = 0;

	/// Handle output
	virtual void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {}

	virtual void renderSidebarPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
		// Clean sidebar if function is not overwritten
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			image[y][kDisplayWidth] = colors::black;
			image[y][kDisplayWidth + 1] = colors::black;
		}
	};

	// Properties

	virtual char const* name() = 0;
	/// This currently includes Synth, MIDI and CV
	virtual bool supportsInstrument() { return false; }
	virtual bool supportsKit() { return false; }
	virtual RequiredScaleMode requiredScaleMode() { return RequiredScaleMode::Undefined; }

	virtual NotesState& getNotesState() { return currentNotesState; }

protected:
	inline bool isKit() { return currentInstrument()->type == InstrumentType::KIT; }
	/// Song root note can be in any octave, layouts get the normalized one
	inline int16_t getRootNote() { return (currentSong->rootNote % kOctaveSize); }
	inline bool getScaleModeEnabled() { return currentClip()->inScaleMode; }
	inline uint8_t getScaleNoteCount() { return currentSong->numModeNotes; }

	inline ModesArray& getScaleNotes() { return currentSong->modeNotes; }

	inline uint8_t getDefaultVelocity() { return currentInstrument()->defaultVelocity; }

	inline int32_t getLowestClipNote() { return kLowestKeyboardNote; }
	inline int32_t getHighestClipNote() {
		if (isKit()) {
			return currentClip()->noteRows.getNumElements() - 1;
		}

		return kHighestKeyboardNote;
	}

	inline RGB getNoteColor(uint8_t note) {
		int8_t colorOffset = 0;

		// Get color offset for kit rows
		if (currentInstrument()->type == InstrumentType::KIT) {
			if (note >= 0 && note < currentClip()->noteRows.getNumElements()) {
				NoteRow* noteRow = currentClip()->noteRows.getElement(note);
				if (noteRow) {
					colorOffset = noteRow->getColorOffset(currentClip());
				}
			}
		}

		return currentClip()->getMainColorFromY(note, colorOffset);
	}

	inline KeyboardState& getState() { return currentClip()->keyboardState; }

protected:
	NotesState currentNotesState;
};

}; // namespace deluge::gui::ui::keyboard
