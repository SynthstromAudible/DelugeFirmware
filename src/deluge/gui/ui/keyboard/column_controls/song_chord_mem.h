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
#include "storage/storage_manager.h"

namespace deluge::gui::ui::keyboard::controls {

class SongChordMemColumn : public ControlColumn {
public:
	SongChordMemColumn() = default;

	void renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column, KeyboardLayout* layout) override;
	bool handleVerticalEncoder(int8_t pad, int32_t offset) override;
	void handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                         KeyboardLayout* layout) override;
	void handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
	               KeyboardLayout* layout) override;

	void writeToFile(Serializer& writer);
	void readFromFile(Deserializer& reader);

private:
	uint8_t activeChordMem = 0xFF;
	// Persistent grid-highlight of a recalled chord: write its notes into the keyboard's
	// highlightedNotes[] so the chord shape stays lit on the iso/in-key grid after release,
	// letting you see the harmony while playing a melody over it.
	uint8_t highlightSlot = 0xFF;
	void setHighlight(int32_t slot);
	void clearHighlight();
};

} // namespace deluge::gui::ui::keyboard::controls
