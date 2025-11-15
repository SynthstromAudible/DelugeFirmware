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

#include "mod.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "storage/flash_storage.h"

using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

PLACE_SDRAM_TEXT void ModColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column,
                                              KeyboardLayout* layout) {
	uint8_t brightness = 1;
	uint8_t otherChannels = 0;
	uint32_t modVal = modMin;

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool mod_selected = modDisplay >= (y > 0 ? (modVal - (modStep - 1)) : 0) && modDisplay <= modVal;
		otherChannels = mod_selected ? 0xf0 : 0;
		uint8_t base = mod_selected ? 0xff : brightness + 0x04;
		image[y][column] = {otherChannels, otherChannels, base};
		modVal += modStep;
		brightness += 10;
	}
}

PLACE_SDRAM_TEXT bool ModColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	if (pad == 7) {
		modMax += offset << kVelModShift;
		modMax = std::clamp(modMax, modMin, (uint32_t)127 << kVelModShift);
		display->displayPopup(modMax >> kVelModShift);
		modStep = (modMax - modMin) / 7;
		return true;
	}
	else if (pad == 0) {
		modMin += offset << kVelModShift;
		modMin = std::clamp((int32_t)modMin, (int32_t)0, (int32_t)modMax);
		display->displayPopup(modMin >> kVelModShift);
		modStep = (modMax - modMin) / 7;
		return true;
	}
	return false;
};

PLACE_SDRAM_TEXT void ModColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                     KeyboardLayout* layout) {
	// Restore previously set Modwheel
	modDisplay = storedMod;
	getCurrentInstrument()->processParamFromInputMIDIChannel(CC_NUMBER_Y_AXIS, storedMod,
	                                                         modelStackWithTimelineCounter);
};

PLACE_SDRAM_TEXT void ModColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                                           KeyboardLayout* layout) {

	if (pad.active) {
		modDisplay = modMin + pad.y * modStep;
		getCurrentInstrument()->processParamFromInputMIDIChannel(CC_NUMBER_Y_AXIS, modDisplay,
		                                                         modelStackWithTimelineCounter);
		display->displayPopup((modDisplay + kHalfStep) >> kVelModShift);
	}
	else if (!pad.padPressHeld || FlashStorage::keyboardFunctionsModwheelGlide) {
		// short press or momentary velocity is off, latch the display mod from the ON press
		storedMod = modDisplay;
		getCurrentInstrument()->processParamFromInputMIDIChannel(CC_NUMBER_Y_AXIS, storedMod,
		                                                         modelStackWithTimelineCounter);
	}
	else {
		modDisplay = storedMod;
		getCurrentInstrument()->processParamFromInputMIDIChannel(CC_NUMBER_Y_AXIS, storedMod,
		                                                         modelStackWithTimelineCounter);
	}
};
} // namespace deluge::gui::ui::keyboard::controls
