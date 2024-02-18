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

namespace deluge::gui::ui::keyboard::controls {

class VelocityColumn : public ControlColumn {
public:
	VelocityColumn(uint8_t velocity) : vDisplay(velocity), velocity32(velocity << kVelModShift){};

	void renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) override;
	bool handleVerticalEncoder(int8_t pad, int32_t offset) override;
	void handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
	               KeyboardLayout* layout) override;

private:
	// use higher precision internally so that scaling and stepping is cleaner
	uint32_t velocityMax = 127 << kVelModShift;
	uint32_t velocityMin = 15 << kVelModShift;
	uint32_t velocityStep = (velocityMax - velocityMin) / 7;
	uint32_t velocity32;
	uint32_t vDisplay;
};

} // namespace deluge::gui::ui::keyboard::controls
