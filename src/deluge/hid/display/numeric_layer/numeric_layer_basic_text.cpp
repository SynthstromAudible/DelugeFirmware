/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "hid/display/numeric_layer/numeric_layer_basic_text.h"
#include "gui/ui_timer_manager.h"
#include "hid/led/indicator_leds.h"
#include "processing/engines/audio_engine.h"

NumericLayerBasicText::NumericLayerBasicText() {
	// TODO Auto-generated constructor stub
}

NumericLayerBasicText::~NumericLayerBasicText() {
	// TODO Auto-generated destructor stub
}

void NumericLayerBasicText::isNowOnTop() {
	if (blinkSpeed) {

		if (blinkSpeed == 1 && uiTimerManager.isTimerSet(TimerName::LED_BLINK)) {
			uiTimerManager.setTimerByOtherTimer(TimerName::DISPLAY, TimerName::LED_BLINK);
			if (!indicator_leds::ledBlinkState[0]) {
				currentlyBlanked = !currentlyBlanked; // Cheating
			}
		}
		else {
			int32_t speed = (blinkSpeed == 1 && !currentlyBlanked) ? kInitialFlashTime : kFlashTime;
			uiTimerManager.setTimer(TimerName::DISPLAY, speed);
		}
	}
}

bool NumericLayerBasicText::callBack() {

	currentlyBlanked = !currentlyBlanked;

	if (blinkCount != -1) {
		blinkCount--;
		if (blinkCount == 0) {
			return true;
		}
	}

	uiTimerManager.setTimer(TimerName::DISPLAY, kFlashTime);

	return false;
}

void NumericLayerBasicText::render(uint8_t* returnSegments) {
	if (!currentlyBlanked) {
		renderWithoutBlink(returnSegments);
	}
	else {
		for (int32_t c = 0; c < kNumericDisplayLength; c++) {
			returnSegments[c] = blinkedSegments[c];
		}
	}
}

void NumericLayerBasicText::renderWithoutBlink(uint8_t* returnSegments) {
	for (int32_t c = 0; c < kNumericDisplayLength; c++) {
		returnSegments[c] = segments[c];
	}
}
