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

#include "chord.h"
#include "gui/ui/keyboard/layout/column_controls.h"

using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

void ChordColumn::setActiveChord(ChordModeChord chord) {
	activeChord = chord;
	chordSemitoneOffsets[0] = chordTypeSemitoneOffsets[activeChord][0];
	chordSemitoneOffsets[1] = chordTypeSemitoneOffsets[activeChord][1];
	chordSemitoneOffsets[2] = chordTypeSemitoneOffsets[activeChord][2];
	chordSemitoneOffsets[3] = chordTypeSemitoneOffsets[activeChord][3];
}

void ChordColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column, KeyboardLayout* layout) {
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool chord_selected = y + 1 == activeChord;
		otherChannels = chord_selected ? 0xf0 : 0;
		uint8_t base = chord_selected ? 0xff : 0x7F;
		image[y][column] = {otherChannels, base, otherChannels};
	}
}

bool ChordColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	return false;
};

void ChordColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                      KeyboardLayout* layout) {
	// Restore previously set chord
	setActiveChord(defaultChord);
};

void ChordColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                            KeyboardLayout* layout) {
	if (pad.active) {
		setActiveChord(static_cast<ChordModeChord>(pad.y + 1));
		display->displayPopup(l10n::get(chordNames[activeChord]));
	}
	else if (!pad.padPressHeld) {
		if (defaultChord != static_cast<ChordModeChord>(pad.y + 1)) {
			defaultChord = static_cast<ChordModeChord>(pad.y + 1);
		}
		else {
			defaultChord = NO_CHORD;
		}
		setActiveChord(defaultChord);
		display->displayPopup(l10n::get(chordNames[activeChord]));
	}
	else {
		setActiveChord(defaultChord);
	}
};
} // namespace deluge::gui::ui::keyboard::controls
