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

#include "definitions.h"
#include "gui/ui/keyboard/chords.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include <array>

namespace deluge::gui::ui::keyboard::layout {

/// @brief The "Harmonic" layout: a chord explorer (left) beside an in-key isomorphic visualiser (right).
///
/// Left: the 7 in-key chords, one per column, each a DISTINCT colour, shading light->dark up the rows as
/// chord richness builds (triad -> 13). Press a pad and it plays, named in Roman + absolute. Right: an
/// in-key iso panel showing the scale grid in colour with the tonic as a steady anchor; the voiced chord
/// you pick lights as one shape (the isomorphism repeats it a couple of times), and notes you play on it
/// light in their colours like a keyboard.
class KeyboardLayoutHarmonic : public ColumnControlsKeyboard {
public:
	KeyboardLayoutHarmonic() = default;
	~KeyboardLayoutHarmonic() override = default;

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override {}

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	l10n::String name() override { return l10n::String::STRING_FOR_KEYBOARD_LAYOUT_HARMONIC; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }
	RequiredScaleMode requiredScaleMode() override { return RequiredScaleMode::Enabled; }

protected:
	bool allowSidebarType(ColumnControlFunction sidebarType) override;

private:
	static constexpr int32_t kExplorerCols = 7;                // left block: the 7 in-key chords (cols 0-6)
	static constexpr int32_t kIsoStartCol = kExplorerCols + 1; // iso panel starts here; col 7 is a blank divider
	static constexpr int32_t kIsoRowStep = 3;                  // iso panel: scale-steps per row (in-key)

	uint8_t getScaleIntervals(uint8_t* ivOut);
	uint8_t buildChordAtDegree(uint8_t deg, int32_t y, const uint8_t* iv, uint8_t sc, uint8_t keyRoot,
	                           int16_t* notesOut, uint8_t maxNotes, uint8_t* rootPcOut, char* romanOut, char* absOut);
	int32_t isoNoteAt(int32_t x, int32_t y); // in-key, anchored to the explorer's octave (tonic at bottom-left)
	void drawName(const char* roman, const char* abs);

	uint16_t heldCols = 0;    // explorer columns currently held (light up as feedback)
	uint16_t chordPcMask = 0; // pitch-classes of the selected chord — the iso shape; 0 = cleared (free play)
};

}; // namespace deluge::gui::ui::keyboard::layout
