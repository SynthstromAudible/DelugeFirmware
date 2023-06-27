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

#include "NumericLayerScrollingText.h"
#include "definitions.h"
#include "uitimermanager.h"
#include "string.h"

NumericLayerScrollingText::NumericLayerScrollingText() {
	currentDirection = 1;
	currentPos = 0;
}

NumericLayerScrollingText::~NumericLayerScrollingText() {
	// TODO Auto-generated destructor stub
}

void NumericLayerScrollingText::isNowOnTop() {
	if (length > NUMERIC_DISPLAY_LENGTH) {
		uiTimerManager.setTimer(TIMER_DISPLAY, initialDelay);
	}

	if (currentPos + NUMERIC_DISPLAY_LENGTH >= length) currentDirection = -1;
}

void NumericLayerScrollingText::render(uint8_t* returnSegments) {
	for (int i = 0; i < NUMERIC_DISPLAY_LENGTH; i++) {
		if (i + currentPos < length) returnSegments[i] = text[i + currentPos];
		else returnSegments[i] = 0;
	}
}

bool NumericLayerScrollingText::callBack() {

	currentPos += currentDirection;

	bool reachedEnd = (currentPos == 0 || (currentPos >= length - NUMERIC_DISPLAY_LENGTH && currentDirection == 1));

	if (reachedEnd) {
		currentDirection = -currentDirection;
	}

	int delayTime = reachedEnd ? 600 : ((currentDirection == 1) ? 140 : 50);
	uiTimerManager.setTimer(TIMER_DISPLAY, delayTime);

	return false;
}
