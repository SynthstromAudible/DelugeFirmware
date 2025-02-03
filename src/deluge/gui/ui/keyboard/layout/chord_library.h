/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
 * Copyright © 2024 Madeline Scyphers
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
#include <array>

namespace deluge::gui::ui::keyboard::layout {

constexpr int8_t kVerticalPages = ((kUniqueChords + kDisplayHeight - 1) / kDisplayHeight); // Round up division

/// @brief Represents a keyboard layout for chord-based input.
class KeyboardLayoutChordLibrary : public ColumnControlsKeyboard {
public:
	KeyboardLayoutChordLibrary() = default;
	~KeyboardLayoutChordLibrary() override = default;

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	l10n::String name() override { return l10n::String::STRING_FOR_KEYBOARD_LAYOUT_CHORD_LIBRARY; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }

protected:
	bool allowSidebarType(ColumnControlFunction sidebarType) override;

private:
	void drawChordName(int16_t noteCode, const char* chordName = "", const char* voicingName = "");
	inline uint8_t noteFromCoords(int32_t x) { return getState().chordLibrary.noteOffset + x; }
	inline int32_t getChordNo(int32_t y) { return getState().chordLibrary.chordList.chordRowOffset + y; }

	std::array<RGB, kOctaveSize> noteColours;
	std::array<RGB, kVerticalPages> pageColours;
	bool initializedNoteOffset = false;
};

}; // namespace deluge::gui::ui::keyboard::layout
