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

#include "gui/ui/keyboard/layout.h"

namespace deluge::gui::ui::keyboard::layout {

constexpr int32_t kMinInKeyRowInterval = 1;
constexpr int32_t kMaxInKeyRowInterval = 16;

class KeyboardLayoutInKey : public KeyboardLayout {
public:
	KeyboardLayoutInKey() {}
	virtual ~KeyboardLayoutInKey() {}

	virtual void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]);
	virtual void handleVerticalEncoder(int32_t offset);
	virtual void handleHorizontalEncoder(int32_t offset, bool shiftEnabled);
	virtual void precalculate();

	virtual void renderPads(Colour image[][kDisplayWidth + kSideBarWidth]);

	virtual char const* name() { return "In-Key"; }
	virtual bool supportsInstrument() { return true; }
	virtual bool supportsKit() { return false; }
	virtual RequiredScaleMode requiredScaleMode() { return RequiredScaleMode::Enabled; }

private:
	inline uint16_t noteFromCoords(int32_t x, int32_t y) { return noteFromPadIndex(padIndexFromCoords(x, y)); }

	inline uint16_t padIndexFromCoords(int32_t x, int32_t y) {
		return getState().inKey.scrollOffset + x + y * getState().inKey.rowInterval;
	}

	inline uint16_t noteFromPadIndex(uint16_t padIndex) {
		ModesArray& scaleNotes = getScaleNotes();
		uint8_t scaleNoteCount = getScaleNoteCount();

		uint8_t octave = padIndex / scaleNoteCount;
		uint8_t octaveNoteIndex = padIndex % scaleNoteCount;
		return octave * kOctaveSize + getRootNote() + scaleNotes[octaveNoteIndex];
	}

	inline uint16_t padIndexFromNote(uint16_t note) {
		ModesArray& scaleNotes = getScaleNotes();
		uint8_t scaleNoteCount = getScaleNoteCount();
		int16_t rootNote = getRootNote();

		uint8_t padScaleOffset = 0;
		for (uint8_t idx = 0; idx < scaleNoteCount; ++idx) {
			if (scaleNotes[idx] == (((note + kOctaveSize) - rootNote) % kOctaveSize)) {
				padScaleOffset = idx;
				break;
			}
		}
		int32_t octave = (((note + kOctaveSize) - rootNote) / kOctaveSize) - 1;
		// Make sure we don't go into negative because our root note is lower than C-2
		return std::max<uint16_t>(0, octave * scaleNoteCount + padScaleOffset);
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

	Colour noteColours[kDisplayHeight * kMaxInKeyRowInterval + kDisplayWidth];
};

}; // namespace deluge::gui::ui::keyboard::layout
