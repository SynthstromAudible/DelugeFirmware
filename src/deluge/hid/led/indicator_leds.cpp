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

#include "hid/led/indicator_leds.h"
#include "RZA1/uart/sio_char.h"
#include "drivers/pic/pic.h"
#include "gui/ui_timer_manager.h"
#include <array>
#include <cstdint>

extern "C" {
#include "RZA1/gpio/gpio.h"
}

namespace indicator_leds {

bool ledStates[NUM_LED_COLS * NUM_LED_ROWS];

LedBlinker ledBlinkers[numLedBlinkers];
bool ledBlinkState[NUM_LEVEL_INDICATORS];

uint8_t knobIndicatorLevels[NUM_LEVEL_INDICATORS];

uint8_t whichLevelIndicatorBlinking;
bool levelIndicatorBlinkOn;
uint8_t levelIndicatorBlinksLeft;

uint8_t whichKnobMetering;

void setLedState(LED led, bool newState, bool allowContinuedBlinking) {

	if (!allowContinuedBlinking) {
		stopLedBlinking(led);
	}

	uint8_t l = static_cast<int32_t>(led);
	ledStates[l] = newState;

	if (newState) {
		PIC::setLEDOn(l);
	}
	else {
		PIC::setLEDOff(l);
	}
}

void blinkLed(LED led, uint8_t numBlinks, uint8_t blinkingType, bool initialState) {

	stopLedBlinking(led, true);

	// Find unallocated blinker
	int32_t i;
	for (i = 0; i < numLedBlinkers - 1; i++) {
		if (!ledBlinkers[i].active) {
			break;
		}
	}

	ledBlinkers[i].led = led;
	ledBlinkers[i].active = true;
	ledBlinkers[i].blinkingType = blinkingType;

	if (numBlinks == 255) {
		ledBlinkers[i].blinksLeft = 255;
	}
	else {
		ledBlinkers[i].returnToState = ledStates[static_cast<int32_t>(led)];
		ledBlinkers[i].blinksLeft = numBlinks * 2;
	}

	ledBlinkState[blinkingType] = initialState;
	updateBlinkingLedStates(blinkingType);

	int32_t thisInitialFlashTime;
	if (blinkingType) {
		thisInitialFlashTime = kFastFlashTime;
	}
	else {
		if (initialState) {
			thisInitialFlashTime = kInitialFlashTime;
		}
		else {
			thisInitialFlashTime = kFlashTime;
		}
	}

	uiTimerManager.setTimer(static_cast<TimerName>(util::to_underlying(TimerName::LED_BLINK) + blinkingType),
	                        thisInitialFlashTime);
}

void ledBlinkTimeout(uint8_t blinkingType, bool forceReset, bool resetToState) {
	if (forceReset) {
		ledBlinkState[blinkingType] = resetToState;
	}
	else {
		ledBlinkState[blinkingType] = !ledBlinkState[blinkingType];
	}

	bool anyActive = updateBlinkingLedStates(blinkingType);

	int32_t thisFlashTime = (blinkingType ? kFastFlashTime : kFlashTime);
	if (anyActive) {
		uiTimerManager.setTimer(static_cast<TimerName>(util::to_underlying(TimerName::LED_BLINK) + blinkingType),
		                        thisFlashTime);
	}
}

// Returns true if some blinking still active
bool updateBlinkingLedStates(uint8_t blinkingType) {
	bool anyActive = false;
	for (int32_t i = 0; i < numLedBlinkers; i++) {
		if (ledBlinkers[i].active && ledBlinkers[i].blinkingType == blinkingType) {

			// If only doing a fixed number of blinks...
			if (ledBlinkers[i].blinksLeft != 255) {
				ledBlinkers[i].blinksLeft--;

				// If no more blinks...
				if (ledBlinkers[i].blinksLeft == 0) {
					ledBlinkers[i].active = false;
					setLedState(ledBlinkers[i].led, ledBlinkers[i].returnToState, true);
					continue;
				}
			}

			// We only get here if we haven't run out of blinks..
			anyActive = true;
			setLedState(ledBlinkers[i].led, ledBlinkState[blinkingType], true);
		}
	}
	return anyActive;
}

void stopLedBlinking(LED led, bool resetState) {
	uint8_t i = getLedBlinkerIndex(led);
	if (i != 255) {
		ledBlinkers[i].active = false;
		if (resetState) {
			setLedState(led, ledBlinkers[i].returnToState, true);
		}
	}
}

uint8_t getLedBlinkerIndex(LED led) {
	for (uint8_t i = 0; i < numLedBlinkers; i++) {
		if (ledBlinkers[i].led == led && ledBlinkers[i].active) {
			return i;
		}
	}
	return 255;
}

void indicateAlertOnLed(LED led) {
	blinkLed(led, 3, 1);
}

// this sets the level only if there hasn't been a value update in 500ms
void setMeterLevel(uint8_t whichKnob, uint8_t level) {
	whichKnobMetering = whichKnob;
	if (!uiTimerManager.isTimerSet(TimerName::METER_INDICATOR_BLINK)) {
		actuallySetKnobIndicatorLevel(whichKnob, level);
	}
}

// Level is out of 128
// Set level and block metering for 500ms
void setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level) {
	if (whichKnob == whichKnobMetering) {
		uiTimerManager.setTimer(TimerName::METER_INDICATOR_BLINK, 500);
	}
	actuallySetKnobIndicatorLevel(whichKnob, level);
}

// Just set level
void actuallySetKnobIndicatorLevel(uint8_t whichKnob, uint8_t level) {
	// If this indicator was blinking, stop it
	if (uiTimerManager.isTimerSet(TimerName::LEVEL_INDICATOR_BLINK) && whichLevelIndicatorBlinking == whichKnob) {
		uiTimerManager.unsetTimer(TimerName::LEVEL_INDICATOR_BLINK);
	}
	else {
		if (level == knobIndicatorLevels[whichKnob]) {
			return;
		}
	}

	int32_t numIndicatorLedsFullyOn = level >> 5;

	int32_t brightness = (level & 31) << 3;
	brightness = (brightness * brightness) >> 8; // Square it

	std::array<uint8_t, kNumGoldKnobIndicatorLEDs> indicator{};

	for (size_t i = 0; i < kNumGoldKnobIndicatorLEDs; i++) {
		int32_t brightnessOutputValue = 0;

		if (i < numIndicatorLedsFullyOn) {
			brightnessOutputValue = 255;
		}
		else if (i == numIndicatorLedsFullyOn) {
			brightnessOutputValue = brightness;
		}

		indicator.at(i) = brightnessOutputValue;
	}
	PIC::setGoldKnobIndicator(whichKnob, indicator);

	knobIndicatorLevels[whichKnob] = level;
}

void blinkKnobIndicator(int32_t whichKnob) {
	if (uiTimerManager.isTimerSet(TimerName::LEVEL_INDICATOR_BLINK)) {
		uiTimerManager.unsetTimer(TimerName::LEVEL_INDICATOR_BLINK);
		if (whichLevelIndicatorBlinking != whichKnob) {
			setKnobIndicatorLevel(whichLevelIndicatorBlinking, 64);
		}
	}

	whichLevelIndicatorBlinking = whichKnob;
	levelIndicatorBlinkOn = false;
	levelIndicatorBlinksLeft = 26;
	blinkKnobIndicatorLevelTimeout();
}

void stopBlinkingKnobIndicator(int32_t whichKnob) {
	if (isKnobIndicatorBlinking(whichKnob)) {
		levelIndicatorBlinksLeft = 0;
		uiTimerManager.unsetTimer(TimerName::LEVEL_INDICATOR_BLINK);
	}
}

void blinkKnobIndicatorLevelTimeout() {
	setKnobIndicatorLevel(whichLevelIndicatorBlinking, levelIndicatorBlinkOn ? 64 : 0);

	levelIndicatorBlinkOn = !levelIndicatorBlinkOn;
	if (--levelIndicatorBlinksLeft) {
		uiTimerManager.setTimer(TimerName::LEVEL_INDICATOR_BLINK, 20);
	}
}

bool isKnobIndicatorBlinking(int32_t whichKnob) {
	return (levelIndicatorBlinksLeft && whichLevelIndicatorBlinking == whichKnob);
}

void clearKnobIndicatorLevels() {
	for (int32_t i = 0; i < NUM_LEVEL_INDICATORS; i++) {
		setKnobIndicatorLevel(i, 0);
	}
}

} // namespace indicator_leds
