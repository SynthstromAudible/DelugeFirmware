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

enum class KeyType : uint8_t {
	BLACK,
	WHITE,
	NONE,
};

// piano keyboard layout at max zoom out
// clang-format off
const KeyType keyLayout[kDisplayHeight][kDisplayWidth] = {
	{deluge::gui::ui::keyboard::layout::KeyType::NONE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::NONE},	
	{deluge::gui::ui::keyboard::layout::KeyType::NONE, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE},
	{deluge::gui::ui::keyboard::layout::KeyType::NONE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::NONE},	
	{deluge::gui::ui::keyboard::layout::KeyType::NONE, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE},
	{deluge::gui::ui::keyboard::layout::KeyType::NONE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::NONE},	
	{deluge::gui::ui::keyboard::layout::KeyType::NONE, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE},
	{deluge::gui::ui::keyboard::layout::KeyType::NONE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::WHITE, deluge::gui::ui::keyboard::layout::KeyType::NONE},	
	{deluge::gui::ui::keyboard::layout::KeyType::NONE, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE,  deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::BLACK, deluge::gui::ui::keyboard::layout::KeyType::NONE},
};
// clang-format on

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

	int32_t zoom = 1;

private:
	void offsetPads(int32_t offset, bool shiftEnabled);
	inline uint16_t noteFromCoords(int32_t x, int32_t y) {
		int32_t yZoom = y / zoom;
		if (keyLayout[yZoom][x] == KeyType::BLACK) {
			return (noteFromPadIndex(padIndexFromCoords(x - 1, y - 1)) + 1);
		}
		else if (keyLayout[yZoom][x] == KeyType::WHITE) {
			return noteFromPadIndex(padIndexFromCoords(x, y));
		}
		else {
			return 255;
		}
	}

	inline uint16_t padIndexFromCoords(int32_t x, int32_t y) {
		y = y / zoom;

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

	RGB noteColours[kDisplayHeight * kMaxPianoRowInterval + kDisplayWidth];
};

}; // namespace deluge::gui::ui::keyboard::layout
