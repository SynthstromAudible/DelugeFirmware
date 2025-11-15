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

#include "velocity.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "storage/flash_storage.h"

using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

PLACE_SDRAM_TEXT void VelocityColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column,
                                                   KeyboardLayout* layout) {
	uint8_t brightness = 1;
	uint8_t otherChannels = 0;
	uint32_t velocityVal = velocityMin;

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool velocity_selected =
		    vDisplay >= (y > 0 ? (velocityVal - (velocityStep - 1)) : 0) && vDisplay <= velocityVal + kHalfStep;
		otherChannels = velocity_selected ? 0xf0 : 0;
		uint8_t base = velocity_selected ? 0xff : brightness + 0x04;
		image[y][column] = {base, otherChannels, otherChannels};
		velocityVal += velocityStep;
		brightness += 10;
	}
}

PLACE_SDRAM_TEXT bool VelocityColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	if (pad == 7) {
		velocityMax += offset << kVelModShift;
		velocityMax = std::clamp(velocityMax, velocityMin, (uint32_t)127 << kVelModShift);
		display->displayPopup(velocityMax >> kVelModShift);
		velocityStep = (velocityMax - velocityMin) / 7;
		return true;
	}
	else if (pad == 0) {
		velocityMin += offset << kVelModShift;
		velocityMin = std::clamp((int32_t)velocityMin, (int32_t)1, (int32_t)velocityMax);
		display->displayPopup(velocityMin >> kVelModShift);
		velocityStep = (velocityMax - velocityMin) / 7;
		return true;
	}
	return false;
};

PLACE_SDRAM_TEXT void VelocityColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                          KeyboardLayout* layout) {
	// Restore previously set Velocity
	vDisplay = storedVelocity;
	layout->velocity = (storedVelocity + kHalfStep) >> kVelModShift;
};

PLACE_SDRAM_TEXT void VelocityColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                PressedPad pad, KeyboardLayout* layout) {
	if (pad.active) {
		vDisplay = velocityMin + pad.y * velocityStep;
		layout->velocity = (vDisplay + kHalfStep) >> kVelModShift;
		display->displayPopup(layout->velocity);
	}
	else if (!pad.padPressHeld || FlashStorage::keyboardFunctionsVelocityGlide) {
		// short press or momentary velocity is off, latch the display velocity from the ON press
		storedVelocity = vDisplay;
		layout->velocity = (storedVelocity + kHalfStep) >> kVelModShift;
	}
	else {
		// revert to previous value
		vDisplay = storedVelocity;
		layout->velocity = (storedVelocity + kHalfStep) >> kVelModShift;
	}
};
} // namespace deluge::gui::ui::keyboard::controls
