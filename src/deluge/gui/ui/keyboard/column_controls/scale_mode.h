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
#include "model/scale/preset_scales.h"
#include "model/song/song.h"

namespace deluge::gui::ui::keyboard::controls {

class ScaleModeColumn : public ControlColumn {
public:
	ScaleModeColumn() = default;

	void renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column, KeyboardLayout* layout) override;
	bool handleVerticalEncoder(int8_t pad, int32_t offset) override;
	void handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                         KeyboardLayout* layout) override;
	void handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
	               KeyboardLayout* layout) override;

private:
	int32_t currentScalePad = -1;
	Scale previousScale = NO_SCALE;
	Scale scaleModes[8] = {MAJOR_SCALE,  MINOR_SCALE,      DORIAN_SCALE,  PHRYGIAN_SCALE,
	                       LYDIAN_SCALE, MIXOLYDIAN_SCALE, LOCRIAN_SCALE, MELODIC_MINOR_SCALE};
};

} // namespace deluge::gui::ui::keyboard::controls
