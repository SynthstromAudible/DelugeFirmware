/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

// for vertical scroll
constexpr int32_t lowestPianoOctave = 0;  // C-2 = 0
constexpr int32_t highestPianoOctave = 9; // this is the last row starting from 0
// for horizontal scroll
constexpr int32_t highestNoteOffset = 7; // how much we can shift a keyboard horizontally (7 steps = 1 oct)
// for colours
constexpr int32_t totalPianoOctaves = 16; // total number of octaves starting from -2 to 13 (max in Deluge)
constexpr int32_t colourOffset = 6;       // jump in a colour codes fromHUE between rows

// intervals of piano keys layout (0=no key)
const int32_t pianoIntervals[2][7] = {
    {0, 2, 4, 0, 7, 9, 11},  // black keys
    {1, 3, 5, 6, 8, 10, 12}, // white keys
};

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

	l10n::String name() override { return l10n::String::STRING_FOR_KEYBOARD_LAYOUT_PIANO; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }

private:
	/* intervalFromCoords return a current note interval from 0 to 12 without an octave
	 * depending on x and y. Used for color-coding and detecting noteFromCoords.
	 *   even rows (y) = white keys, odd rows = black keys
	 *   x % 7 = interval number in the pianoIntervals array (they are repeat every 7 times)
	 */
	inline uint16_t intervalFromCoords(int32_t x, int32_t y) {
		return (y == 0 || (y % 2 == 0)) ? (pianoIntervals[1][(x + getState().piano.noteOffset) % 7])
		                                : (pianoIntervals[0][(x + getState().piano.noteOffset) % 7]);
	}

	/* noteFromCoords return a note code (C-2=0) depending on current octave offset, x and y
	 *   every 2 rows +1 octave (y/2), every 7 columns +1 octave (x/7). One octave = 12 semi
	 *   result= start octave + current interval from Coords + octave by y + ocrave by x - 1 (starting from 0)
	 */
	inline uint16_t noteFromCoords(int32_t x, int32_t y) {
		return (getState().piano.scrollOffset) * 12 + intervalFromCoords(x, y) + 12 * (y / 2)
		       + 12 * ((x + getState().piano.noteOffset) / 7) - 1;
	}

	// each octave has it's own color
	RGB noteColours[totalPianoOctaves + 1];
};

}; // namespace deluge::gui::ui::keyboard::layout
