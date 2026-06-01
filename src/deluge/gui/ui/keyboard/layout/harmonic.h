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

/// @brief The "Harmonic" layout: a calm, organized map of every chord that's possible in your key.
///
/// One octave, no repeated columns, no algorithmic brain — the columns are simply the 7 diatonic chords
/// in order (i ii ♭III iv v ♭VI ♭VII), coloured by harmonic function, with chord richness climbing the
/// rows (triad at the bottom up to a 13th). Press a pad and it plays, named in Roman + absolute (e.g.
/// "Db∆7  ♭VI"). The intelligence — what to play next, why — comes from the player/teacher, not the
/// firmware; this is the instrument they explore.
///
/// X = the 7 in-key chords. Y = richness. Colour = function (tonic / subdominant / dominant). The
/// vertical encoder shifts the octave.
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
	// Degree-based, so it needs the scale: keeps your key on entry and lights the scale LED.
	RequiredScaleMode requiredScaleMode() override { return RequiredScaleMode::Enabled; }

protected:
	bool allowSidebarType(ColumnControlFunction sidebarType) override;

private:
	static constexpr int32_t kExplorerCols = 7;   // left block: the 7 in-key chords
	static constexpr int32_t kIsoRowInterval = 5; // right block: isomorphic visualiser, a 4th per row

	uint8_t getScaleIntervals(uint8_t* ivOut);
	// Build the chord for scale-degree `deg` at richness row `y` into notesOut (absolute MIDI, ascending).
	// Returns the note count and fills the Roman + absolute names and the root pitch class.
	uint8_t buildChordAtDegree(uint8_t deg, int32_t y, const uint8_t* iv, uint8_t sc, uint8_t keyRoot,
	                           int16_t* notesOut, uint8_t maxNotes, uint8_t* rootPcOut, char* romanOut, char* absOut);
	// MIDI note shown by the isomorphic panel at grid (x, y). x is the full grid column (>= kExplorerCols).
	inline int32_t isoNoteAt(int32_t x, int32_t y, int32_t isoBase) {
		return isoBase + (x - kExplorerCols) + y * kIsoRowInterval;
	}
	void drawName(const char* roman, const char* abs);

	uint16_t heldCols = 0;    // bitmask of explorer columns currently held, so the pressed column lights up
	uint16_t chordPcMask = 0; // pitch-classes of the selected chord — lit as a shape on the iso panel
	int8_t chordRootPc = -1;  // its root pitch-class, lit distinctly (white); -1 = none selected yet
};

}; // namespace deluge::gui::ui::keyboard::layout
