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

#include "IndicatorLEDs.h"
#include "sio_char.h"
#include "uitimermanager.h"

extern "C" {
#include "gpio.h"
}

namespace IndicatorLEDs {

bool ledStates[NUM_LED_COLS][NUM_LED_ROWS];

LedBlinker ledBlinkers[numLedBlinkers];
bool ledBlinkState[NUM_LEVEL_INDICATORS];

uint8_t knobIndicatorLevels[NUM_LEVEL_INDICATORS];

uint8_t whichLevelIndicatorBlinking;
bool levelIndicatorBlinkOn;
uint8_t levelIndicatorBlinksLeft;

void setLedState(uint8_t x, uint8_t y, bool newState, bool allowContinuedBlinking) {

	if (!allowContinuedBlinking) stopLedBlinking(x, y);

	ledStates[x][y] = newState;

#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
	bufferPICIndicatorsUart(152 + x + y * 9 + (newState ? 36 : 0));
#else
	bufferPICIndicatorsUart(120 + x + y * 10 + (newState ? 40 : 0));
#endif
}

void blinkLed(uint8_t x, uint8_t y, uint8_t numBlinks, uint8_t blinkingType, bool initialState) {

	stopLedBlinking(x, y, true);

	// Find unallocated blinker
	int i;
	for (i = 0; i < numLedBlinkers - 1; i++) {
		if (!ledBlinkers[i].active) break;
	}

	ledBlinkers[i].x = x;
	ledBlinkers[i].y = y;
	ledBlinkers[i].active = true;
	ledBlinkers[i].blinkingType = blinkingType;

	if (numBlinks == 255) ledBlinkers[i].blinksLeft = 255;
	else {
		ledBlinkers[i].returnToState = ledStates[x][y];
		ledBlinkers[i].blinksLeft = numBlinks * 2;
	}

	ledBlinkState[blinkingType] = initialState;
	updateBlinkingLedStates(blinkingType);

	int thisInitialFlashTime;
	if (blinkingType) thisInitialFlashTime = fastFlashTime;
	else {
		if (initialState) thisInitialFlashTime = initialFlashTime;
		else thisInitialFlashTime = flashTime;
	}

	uiTimerManager.setTimer(TIMER_LED_BLINK + blinkingType, thisInitialFlashTime);
}

void ledBlinkTimeout(uint8_t blinkingType, bool forceReset, bool resetToState) {
	if (forceReset) {
		ledBlinkState[blinkingType] = resetToState;
	}
	else {
		ledBlinkState[blinkingType] = !ledBlinkState[blinkingType];
	}

	bool anyActive = updateBlinkingLedStates(blinkingType);

	int thisFlashTime = (blinkingType ? fastFlashTime : flashTime);
	if (anyActive) uiTimerManager.setTimer(TIMER_LED_BLINK + blinkingType, thisFlashTime);
}

// Returns true if some blinking still active
bool updateBlinkingLedStates(uint8_t blinkingType) {
	bool anyActive = false;
	for (int i = 0; i < numLedBlinkers; i++) {
		if (ledBlinkers[i].active && ledBlinkers[i].blinkingType == blinkingType) {

			// If only doing a fixed number of blinks...
			if (ledBlinkers[i].blinksLeft != 255) {
				ledBlinkers[i].blinksLeft--;

				// If no more blinks...
				if (ledBlinkers[i].blinksLeft == 0) {
					ledBlinkers[i].active = false;
					setLedState(ledBlinkers[i].x, ledBlinkers[i].y, ledBlinkers[i].returnToState, true);
					continue;
				}
			}

			// We only get here if we haven't run out of blinks..
			anyActive = true;
			setLedState(ledBlinkers[i].x, ledBlinkers[i].y, ledBlinkState[blinkingType], true);
		}
	}
	return anyActive;
}

void stopLedBlinking(uint8_t x, uint8_t y, bool resetState) {
	uint8_t i = getLedBlinkerIndex(x, y);
	if (i != 255) {
		ledBlinkers[i].active = false;
		if (resetState) setLedState(x, y, ledBlinkers[i].returnToState, true);
	}
}

uint8_t getLedBlinkerIndex(uint8_t x, uint8_t y) {
	for (uint8_t i = 0; i < numLedBlinkers; i++) {
		if (ledBlinkers[i].x == x && ledBlinkers[i].y == y && ledBlinkers[i].active) return i;
	}
	return 255;
}

void indicateAlertOnLed(uint8_t x, uint8_t y) {
	blinkLed(x, y, 3, 1);
}

// Level is out of 128
void setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level) {
	// If this indicator was blinking, stop it
	if (uiTimerManager.isTimerSet(TIMER_LEVEL_INDICATOR_BLINK) && whichLevelIndicatorBlinking == whichKnob) {
		uiTimerManager.unsetTimer(TIMER_LEVEL_INDICATOR_BLINK);
	}
	else {
		if (level == knobIndicatorLevels[whichKnob]) return;
	}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	// Without this here, pads turn on glitchily. Problem can also be solved by turning the baud rate down, but it's a bit late for that!
	// It's very weird that this is the only command which causes this problem.
	//if (Uart::getTxBufferFullness(UART_CHANNEL_PIC) >= 12) return;
	bufferPICIndicatorsUart(70 + whichKnob);
#else
	bufferPICIndicatorsUart(20 + whichKnob);
#endif

	int numIndicatorLedsFullyOn = level >> 5;

	int brightness = (level & 31) << 3;
	brightness = (brightness * brightness) >> 8; // Square it

	for (int i = 0; i < 4; i++) {
		int brightnessOutputValue = 0;

		if (i < numIndicatorLedsFullyOn) brightnessOutputValue = 255;
		else if (i == numIndicatorLedsFullyOn) brightnessOutputValue = brightness;
		bufferPICIndicatorsUart(brightnessOutputValue);
	}

	knobIndicatorLevels[whichKnob] = level;
}

void blinkKnobIndicator(int whichKnob) {
	if (uiTimerManager.isTimerSet(TIMER_LEVEL_INDICATOR_BLINK)) {
		uiTimerManager.unsetTimer(TIMER_LEVEL_INDICATOR_BLINK);
		if (whichLevelIndicatorBlinking != whichKnob) setKnobIndicatorLevel(whichLevelIndicatorBlinking, 64);
	}

	whichLevelIndicatorBlinking = whichKnob;
	levelIndicatorBlinkOn = false;
	levelIndicatorBlinksLeft = 26;
	blinkKnobIndicatorLevelTimeout();
}

void stopBlinkingKnobIndicator(int whichKnob) {
	if (isKnobIndicatorBlinking(whichKnob)) {
		levelIndicatorBlinksLeft = 0;
		uiTimerManager.unsetTimer(TIMER_LEVEL_INDICATOR_BLINK);
	}
}

void blinkKnobIndicatorLevelTimeout() {
	setKnobIndicatorLevel(whichLevelIndicatorBlinking, levelIndicatorBlinkOn ? 64 : 0);

	levelIndicatorBlinkOn = !levelIndicatorBlinkOn;
	if (--levelIndicatorBlinksLeft) uiTimerManager.setTimer(TIMER_LEVEL_INDICATOR_BLINK, 20);
}

bool isKnobIndicatorBlinking(int whichKnob) {
	return (levelIndicatorBlinksLeft && whichLevelIndicatorBlinking == whichKnob);
}

void clearKnobIndicatorLevels() {
	for (int i = 0; i < NUM_LEVEL_INDICATORS; i++) {
		setKnobIndicatorLevel(i, 0);
	}
}

} // namespace IndicatorLEDs
