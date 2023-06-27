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

#include <AudioEngine.h>
#include "NumericLayerBasicText.h"
#include "uitimermanager.h"
#include "IndicatorLEDs.h"

NumericLayerBasicText::NumericLayerBasicText() {
	// TODO Auto-generated constructor stub
}

NumericLayerBasicText::~NumericLayerBasicText() {
	// TODO Auto-generated destructor stub
}

void NumericLayerBasicText::isNowOnTop() {
	if (blinkSpeed) {

		if (blinkSpeed == 1 && uiTimerManager.isTimerSet(TIMER_LED_BLINK)) {
			uiTimerManager.setTimerByOtherTimer(TIMER_DISPLAY, TIMER_LED_BLINK);
			if (!IndicatorLEDs::ledBlinkState[0]) currentlyBlanked = !currentlyBlanked; // Cheating
		}
		else {
			int speed = (blinkSpeed == 1 && !currentlyBlanked) ? initialFlashTime : flashTime;
			uiTimerManager.setTimer(TIMER_DISPLAY, speed);
		}
	}
}

bool NumericLayerBasicText::callBack() {

	currentlyBlanked = !currentlyBlanked;

	if (blinkCount != -1) {
		blinkCount--;
		if (blinkCount == 0) return true;
	}

	uiTimerManager.setTimer(TIMER_DISPLAY, flashTime);

	return false;
}

void NumericLayerBasicText::render(uint8_t* returnSegments) {
	if (!currentlyBlanked) renderWithoutBlink(returnSegments);
	else
		for (int c = 0; c < NUMERIC_DISPLAY_LENGTH; c++) {
			returnSegments[c] = blinkedSegments[c];
		}
}

void NumericLayerBasicText::renderWithoutBlink(uint8_t* returnSegments) {
	for (int c = 0; c < NUMERIC_DISPLAY_LENGTH; c++) {
		returnSegments[c] = segments[c];
	}
}
