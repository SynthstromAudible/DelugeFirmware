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

#include "gui/colour/rgb.h"
#include "gui/ui/keyboard/notes_state.h"
#include "model/model_stack.h"

namespace deluge::gui::ui::keyboard {
class KeyboardLayout;
}

namespace deluge::gui::ui::keyboard::controls {

constexpr uint32_t kVelModShift = 24;

class ControlColumn {
public:
	virtual void renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) = 0;
	virtual bool handleVerticalEncoder(int8_t pad, int32_t offset) = 0;
	virtual void handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                 KeyboardLayout* layout) = 0;
	virtual void handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
	                       KeyboardLayout* layout) = 0;
};

} // namespace deluge::gui::ui::keyboard::controls
