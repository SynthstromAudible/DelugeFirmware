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

#include "dsp/dx/dx7note.h"
#include "gui/ui/keyboard/column_controls/control_column.h"

namespace deluge::gui::ui::keyboard::controls {

class DXColumn : public ControlColumn {
public:
	DXColumn() = default;

	void renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) override;
	bool handleVerticalEncoder(int8_t pad, int32_t offset) override;
	void handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                         KeyboardLayout* layout) override;
	void handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
	               KeyboardLayout* layout) override;

private:
};

DxPatch* getCurrentDxPatch();

} // namespace deluge::gui::ui::keyboard::controls
