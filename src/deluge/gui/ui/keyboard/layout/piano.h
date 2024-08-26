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

#include "gui/ui/keyboard/layout/column_controls.h"
#include "model/scale/note_set.h"

namespace deluge::gui::ui::keyboard::layout {

constexpr int32_t kMinPianoRowInterval = 7;
constexpr int32_t kMaxPianoRowInterval = 7;

class KeyboardLayoutPiano : public ColumnControlsKeyboard {
public:
	KeyboardLayoutPiano() {}
	~KeyboardLayoutPiano() override {}

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	char const* name() override { return "Piano"; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }
	RequiredScaleMode requiredScaleMode() override { return RequiredScaleMode::Enabled; }

private:
	void offsetPads(int32_t offset, bool shiftEnabled);
	inline uint16_t noteFromCoords(int32_t x, int32_t y) {
		if (x == 0 || x == 15) {
			return 255;
		}
		else {
			if (y == 1 || y == 3 || y == 5 || y == 7) {
				if (x == 2 || x == 3 || x == 5 || x == 6 || x == 7 || x == 9 || x == 10 || x == 12 || x == 13 || x == 14) {
					return (noteFromPadIndex(padIndexFromCoords(x - 1, y - 1)) + 1);
				}
				else {
					return 255;
				}
			}
			else {
				return noteFromPadIndex(padIndexFromCoords(x, y));
			}
		}
	}

	inline uint16_t padIndexFromCoords(int32_t x, int32_t y) {
		x--;
		if (y > 0) {
			y--;
		}
		return getState().piano.scrollOffset + x + y * getState().piano.rowInterval;
	}

	inline uint16_t noteFromPadIndex(uint16_t padIndex) {
		NoteSet& scaleNotes = getScaleNotes();
		uint8_t scaleNoteCount = getScaleNoteCount();

		uint8_t octave = padIndex / scaleNoteCount;
		uint8_t octaveNoteIndex = padIndex % scaleNoteCount;
		return octave * kOctaveSize + getRootNote() + scaleNotes[octaveNoteIndex];
	}

	inline uint16_t padIndexFromNote(uint16_t note) {
		NoteSet& scaleNotes = getScaleNotes();
		uint8_t scaleNoteCount = getScaleNoteCount();
		int16_t rootNote = getRootNote();

		uint8_t padScaleOffset = 0;
		for (uint8_t idx = 0; idx < scaleNoteCount; ++idx) {
			if (scaleNotes[idx] == (((note + kOctaveSize) - rootNote) % kOctaveSize)) {
				padScaleOffset = idx;
				break;
			}
		}
		int32_t octave = (((note + kOctaveSize) - rootNote) / kOctaveSize);
		// Make sure we don't go into negative because our root note is lower than C-2
		return std::max<int32_t>(octave * scaleNoteCount + padScaleOffset, std::numeric_limits<uint16_t>::min());
	}

	// inline uint16_t padIndexFromNote(uint16_t note) {
	// 	uint8_t scaleNoteCount = getScaleNoteCount();

	// 	padIndex = octave * scaleNoteCount
	// 	C1 = 7

	// 	return note;
	// 	//note % kOctaveSize

	// 	// uint8_t scaleNoteCount = getScaleNoteCount();
	// 	// uint8_t octave = padIndex / scaleNoteCount;
	// 	// uint8_t octaveNoteIndex = padIndex % scaleNoteCount;
	// 	// return octave * kOctaveSize + getRootNote() + getScaleNotes()[octaveNoteIndex];
	// }

	RGB noteColours[kDisplayHeight * kMaxPianoRowInterval + kDisplayWidth];
};

}; // namespace deluge::gui::ui::keyboard::layout
