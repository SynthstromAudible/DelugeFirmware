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

#include "definitions.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/layout/isomorphic.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include <limits>

#define VEL_COL kDisplayWidth
#define MOD_COL (kDisplayWidth + 1)

namespace deluge::gui::ui::keyboard::layout {

void ColumnControlsKeyboard::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	velocityMinHeld = false;
	velocityMaxHeld = false;

	modMinHeld = false;
	modMaxHeld = false;

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		auto pressed = presses[idxPress];
		if (pressed.active) {
			// in note columns
			if (pressed.x == VEL_COL) { // in velocity columns (audition pads)
				velocity32 = velocityMin + pressed.y * velocityStep;
				velocity = velocity32 >> kVelModShift;
				display->displayPopup(velocity);
				if (pressed.y == 0) {
					velocityMinHeld = true;
				}
				else {
					velocityMaxHeld = true;
				}
			}
			else if (pressed.x == MOD_COL) { // in mod columns (audition pads)
				mod32 = modMin + pressed.y * modStep;
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
				    modelStack->addTimelineCounter(currentSong->currentClip);
				currentInstrument()->processParamFromInputMIDIChannel(CC_NUMBER_Y_AXIS, mod32,
				                                                      modelStackWithTimelineCounter);
				display->displayPopup(mod32 >> kVelModShift);
				if (pressed.y == 0) {
					modMinHeld = true;
				}
				else {
					modMaxHeld = true;
				}
			}
		}
	}
}

void ColumnControlsKeyboard::handleVerticalEncoder(int32_t offset) {
	verticalEncoderHandledByColumns(offset);
}

bool ColumnControlsKeyboard::verticalEncoderHandledByColumns(int32_t offset) {
	if (velocityMinHeld) {
		velocityMin += offset << kVelModShift;
		velocityMin = std::clamp(velocityMin, (int32_t)0, velocityMax);
		display->displayPopup(velocityMin >> kVelModShift);
		velocityStep = (velocityMax - velocityMin) / 7;
		return true;
	}
	if (velocityMaxHeld) {
		velocityMax += offset << kVelModShift;
		velocityMax = std::clamp(velocityMax, velocityMin, (int32_t)127 << kVelModShift);
		display->displayPopup(velocityMax >> kVelModShift);
		velocityStep = (velocityMax - velocityMin) / 7;
		return true;
	}
	if (modMinHeld) {
		modMin += offset << kVelModShift;
		modMin = std::clamp(modMin, (int32_t)0, modMax);
		display->displayPopup(modMin >> kVelModShift);
		modStep = (modMax - modMin) / 7;
		return true;
	}
	if (modMaxHeld) {
		modMax += offset << kVelModShift;
		modMax = std::clamp(modMax, modMin, (int32_t)127 << kVelModShift);
		display->displayPopup(modMax >> kVelModShift);
		modStep = (modMax - modMin) / 7;
		return true;
	}
	return false;
}

void ColumnControlsKeyboard::renderSidebarPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
	// Iterate over velocity and mod pads in sidebar
	uint8_t brightness = 1;
	uint8_t otherChannels = 0;
	uint32_t velocityVal = velocityMin;
	uint32_t modVal = modMin;

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool velocity_selected =
		    velocity32 >= (y > 0 ? (velocityVal - (velocityStep - 1)) : 0) && velocity32 <= velocityVal;
		otherChannels = velocity_selected ? 0xf0 : 0;
		image[y][VEL_COL][0] = velocity_selected ? 0xff : brightness + 0x04;
		image[y][VEL_COL][1] = otherChannels;
		image[y][VEL_COL][2] = otherChannels;
		velocityVal += velocityStep;

		bool mod_selected = mod32 >= (y > 0 ? (modVal - (modStep - 1)) : 0) && mod32 <= modVal;
		otherChannels = mod_selected ? 0xf0 : 0;
		image[y][MOD_COL][1] = otherChannels;
		image[y][MOD_COL][0] = otherChannels;
		image[y][MOD_COL][2] = mod_selected ? 0xff : brightness + 0x04;
		modVal += modStep;

		brightness += 10;
	}
}

} // namespace deluge::gui::ui::keyboard::layout
