/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#ifndef INDICATORLEDS_H_
#define INDICATORLEDS_H_

#include "r_typedefs.h"
#include "definitions.h"

#define numLedBlinkers 4

struct LedBlinker {
	uint8_t x;
	uint8_t y;
	bool active;
	uint8_t blinksLeft;
	bool returnToState;
	uint8_t blinkingType;
};

namespace IndicatorLEDs {

extern bool ledBlinkState[];

void setLedState(uint8_t x, uint8_t y, bool newState, bool allowContinuedBlinking = false);
void blinkLed(uint8_t x, uint8_t y, uint8_t numBlinks = 255, uint8_t blinkingType = 0, bool initialState = true);
void ledBlinkTimeout(uint8_t blinkingType, bool forceRestart = false, bool resetToState = true);
void indicateAlertOnLed(uint8_t x, uint8_t y);
void setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level);
void clearKnobIndicatorLevels();
void blinkKnobIndicator(int whichKnob);
void stopBlinkingKnobIndicator(int whichKnob);
void blinkKnobIndicatorLevelTimeout();
uint8_t getLedBlinkerIndex(uint8_t x, uint8_t y);
void stopLedBlinking(uint8_t x, uint8_t y, bool resetState = false);
bool updateBlinkingLedStates(uint8_t blinkingType);
bool isKnobIndicatorBlinking(int whichKnob);

} // namespace IndicatorLEDs

#endif /* INDICATORLEDS_H_ */
