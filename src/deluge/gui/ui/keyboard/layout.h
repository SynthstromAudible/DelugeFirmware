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
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/keyboard/notes_state.h"
#include "gui/ui/keyboard/state_data.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/note/note_row.h"
#include "model/scale/note_set.h"
#include "model/song/song.h"

constexpr uint8_t kMaxNumKeyboardPadPresses = 10;

namespace deluge::gui::ui::keyboard {

enum class RequiredScaleMode : uint8_t {
	Undefined = 0,
	Disabled = 1,
	Enabled = 2,
};

typedef uint8_t NoteHighlightIntensity[kHighestKeyboardNote];

class KeyboardLayout {
public:
	KeyboardLayout() = default;
	virtual ~KeyboardLayout() {}

	/// Handle input pad presses
	virtual void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) = 0;

	/// Shift state not supplied since that function is already taken
	virtual void handleVerticalEncoder(int32_t offset) = 0;

	/// Will be called with offset 0 to recalculate bounds on clip changes
	virtual void handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
	                                     PressedPad presses[kMaxNumKeyboardPadPresses],
	                                     bool encoderPressed = false) = 0;

	/// This function is called on visibility change and if colour offset changes
	virtual void precalculate() = 0;

	/// Handle output
	virtual void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {}

	virtual void renderSidebarPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
		// Clean sidebar if function is not overwritten
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			image[y][kDisplayWidth] = colours::black;
			image[y][kDisplayWidth + 1] = colours::black;
		}
	};

	// Properties

	virtual l10n::String name() = 0;
	/// This currently includes Synth, MIDI and CV
	virtual bool supportsInstrument() { return false; }
	virtual bool supportsKit() { return false; }
	virtual RequiredScaleMode requiredScaleMode() { return RequiredScaleMode::Undefined; }

	virtual NotesState& getNotesState() { return currentNotesState; }

	virtual void checkNewInstrument(Instrument* newInstrument) {}

protected:
	inline bool isKit() { return getCurrentOutputType() == OutputType::KIT; }
	/// Song root note can be in any octave, layouts get the normalized one
	inline int16_t getRootNote() { return (currentSong->key.rootNote % kOctaveSize); }
	inline bool getScaleModeEnabled() { return getCurrentInstrumentClip()->inScaleMode; }
	inline uint8_t getScaleNoteCount() { return currentSong->key.modeNotes.count(); }

	inline NoteSet& getScaleNotes() { return currentSong->key.modeNotes; }

	inline uint8_t getDefaultVelocity() { return getCurrentInstrument()->defaultVelocity; }

	inline int32_t getLowestClipNote() { return kLowestKeyboardNote; }
	inline int32_t getHighestClipNote() {
		if (isKit()) {
			return getCurrentInstrumentClip()->noteRows.getNumElements() - 1;
		}

		return kHighestKeyboardNote;
	}

	inline RGB getNoteColour(uint8_t note) {
		int8_t colourOffset = 0;

		// Get colour offset for kit rows
		if (getCurrentOutputType() == OutputType::KIT) {
			if (note >= 0 && note < getCurrentInstrumentClip()->noteRows.getNumElements()) {
				NoteRow* noteRow = getCurrentInstrumentClip()->noteRows.getElement(note);
				if (noteRow) {
					colourOffset = noteRow->getColourOffset(getCurrentInstrumentClip());
				}
			}
		}

		return getCurrentInstrumentClip()->getMainColourFromY(note, colourOffset);
	}

	inline NoteHighlightIntensity& getHighlightedNotes() { return keyboardScreen.highlightedNotes; }
	inline NoteHighlightIntensity& getNornsNotes() { return keyboardScreen.nornsNotes; }

	inline KeyboardState& getState() { return getCurrentInstrumentClip()->keyboardState; }

public:
	uint8_t velocity = 64;

protected:
	NotesState currentNotesState;
};

}; // namespace deluge::gui::ui::keyboard
