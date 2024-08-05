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

#include "definitions.h"
#include "gui/ui/keyboard/chords.h"
#include "gui/ui/keyboard/layout/column_controls.h"

namespace deluge::gui::ui::keyboard::layout {

/// @brief Represents a keyboard layout for chord-based input.
class KeyboardLayoutChord : public ColumnControlsKeyboard {
public:
	KeyboardLayoutChord() = default;
	~KeyboardLayoutChord() override = default;

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	void drawChordName(int16_t noteCode, const char* chordName);

	char const* name() override { return "Chord"; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }

	uint8_t chordSemitoneOffsets[kMaxChordKeyboardSize] = {0};

private:
	inline uint16_t padIndexFromCoords(int32_t x, int32_t y) {
		return getState().chord.VoiceOffset + x + y * getState().chord.rowInterval;
	}

	void offsetPads(int32_t offset, bool shiftEnabled);

	// A modified version of noteCodeToString
	// Because sometimes the note name is not displayed correctly
	// and we need to add a null terminator to the note name string
	// TODO: work out how to fix this with the noteCodeToString function
	inline uint8_t noteFromCoords(int32_t x) { return getState().chord.VoiceOffset + x; }

	RGB noteColours[kOctaveSize];
};

}; // namespace deluge::gui::ui::keyboard::layout
