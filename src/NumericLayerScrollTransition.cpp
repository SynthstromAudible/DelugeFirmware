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

#include "NumericLayerScrollTransition.h"
#include "uitimermanager.h"
#include "string.h"

NumericLayerScrollTransition::NumericLayerScrollTransition() {
	// TODO Auto-generated constructor stub
}

NumericLayerScrollTransition::~NumericLayerScrollTransition() {
	// TODO Auto-generated destructor stub
}

void NumericLayerScrollTransition::isNowOnTop() {
	uiTimerManager.setTimer(TIMER_DISPLAY, 32);
}

bool NumericLayerScrollTransition::callBack() {
	int writingTo;

	// Move characters currently displayed

	bool allBlank = true;

	if (transitionDirection == 1) {
		for (writingTo = 0; writingTo < NUMERIC_DISPLAY_LENGTH - 1; writingTo++) {
			segments[writingTo] = segments[writingTo + 1];
			if (segments[writingTo] != 0) allBlank = false;
		}
	}

	else {
		for (writingTo = NUMERIC_DISPLAY_LENGTH - 1; writingTo > 0; writingTo--) {
			segments[writingTo] = segments[writingTo - 1];
			if (segments[writingTo] != 0) allBlank = false;
		}
	}

	int8_t progressFlipped = (transitionProgress + transitionDirection) * transitionDirection;

	// Fill character at the end with either a new one, or blank space
	if (progressFlipped > 0 && next) {
		int readingFrom =
		    (transitionDirection == 1) ? transitionProgress : (NUMERIC_DISPLAY_LENGTH + transitionProgress - 1);

		uint8_t segmentsTransitioningTo[NUMERIC_DISPLAY_LENGTH];
		next->render(segmentsTransitioningTo);

		segments[writingTo] = segmentsTransitioningTo[readingFrom];
	}
	else {
		segments[writingTo] = 0;

		// TODO: make this work proper! Fails if scrolling left to a display with only the right-most character occupied

		/*
        if (allBlank) {
            transitionProgress = -transitionDirection;


            if (transitionDirection == 1) {
                for (int8_t i = 0; i < NUMERIC_DISPLAY_LENGTH - 1; i++) {
                    if (segmentsTransitioningTo[i] == 0) transitionProgress += transitionDirection;
                }
            }

            else {
                for (int8_t i = NUMERIC_DISPLAY_LENGTH - 1; i > 0; i--) {
                    if (segmentsTransitioningTo[i] == 0) transitionProgress += transitionDirection;
                }
            }
        }
        */
	}

	// Remember to continue transition, if there's some left
	transitionProgress += transitionDirection;
	if (transitionProgress * transitionDirection < NUMERIC_DISPLAY_LENGTH) {
		int timeToWait = (transitionProgress == 0) ? 160 : 32;
		uiTimerManager.setTimer(TIMER_DISPLAY, timeToWait);
		return false;
	}

	else return true;
}

void NumericLayerScrollTransition::render(uint8_t* returnSegments) {

	memcpy(returnSegments, segments, NUMERIC_DISPLAY_LENGTH);
}
