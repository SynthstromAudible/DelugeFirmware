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

#include "gui/ui/keyboard/column_controls/control_column.h"
#include "util/lookuptables/lookuptables.h"

namespace deluge::gui::ui::keyboard::controls {

enum ChordModeChord {
	NO_CHORD = 0,
	FIFTH,
	SUS2,
	MINOR,
	MAJOR,
	SUS4,
	MINOR7,
	DOMINANT7,
	MAJOR7,
	CHORD_MODE_CHORD_MAX, /* should be 9, 8 chord pads plus NO_CHORD */
};

class ChordColumn : public ControlColumn {
public:
	ChordColumn() = default;

	void renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column, KeyboardLayout* layout) override;
	bool handleVerticalEncoder(int8_t pad, int32_t offset) override;
	void handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                         KeyboardLayout* layout) override;
	void handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
	               KeyboardLayout* layout) override;

	uint8_t chordSemitoneOffsets[MAX_CHORD_NOTES] = {0};

private:
	ChordModeChord activeChord = NO_CHORD;
	ChordModeChord defaultChord = NO_CHORD;

	void setActiveChord(ChordModeChord chord);
};

} // namespace deluge::gui::ui::keyboard::controls
