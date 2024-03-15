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

#pragma once

#include "definitions_cxx.hpp"
#include <cstdint>

constexpr size_t numLedBlinkers = 4;

namespace indicator_leds {

constexpr uint8_t fromCartesian(Cartesian c) {
	return c.x + c.y * NUM_LED_COLS;
}

constexpr uint8_t fromXY(int32_t x, int32_t y) {
	return x + y * NUM_LED_COLS;
}

// clang-format off
enum class LED : uint8_t {
	AFFECT_ENTIRE     = fromCartesian(affectEntireButtonCoord),
	SESSION_VIEW      = fromCartesian(sessionViewButtonCoord),
	CLIP_VIEW         = fromCartesian(clipViewButtonCoord),
	SYNTH             = fromCartesian(synthButtonCoord),
	KIT               = fromCartesian(kitButtonCoord),
	MIDI              = fromCartesian(midiButtonCoord),
	CV                = fromCartesian(cvButtonCoord),
	KEYBOARD          = fromCartesian(keyboardButtonCoord),
	SCALE_MODE        = fromCartesian(scaleModeButtonCoord),
	CROSS_SCREEN_EDIT = fromCartesian(crossScreenEditButtonCoord),
	BACK              = fromCartesian(backButtonCoord),
	LOAD              = fromCartesian(loadButtonCoord),
	SAVE              = fromCartesian(saveButtonCoord),
	LEARN             = fromCartesian(learnButtonCoord),
	TAP_TEMPO         = fromCartesian(tapTempoButtonCoord),
	SYNC_SCALING      = fromCartesian(syncScalingButtonCoord),
	TRIPLETS          = fromCartesian(tripletsButtonCoord),
	PLAY              = fromCartesian(playButtonCoord),
	RECORD            = fromCartesian(recordButtonCoord),
	SHIFT             = fromCartesian(shiftButtonCoord),

	MOD_0 = fromXY(1,0),
	MOD_1 = fromXY(1,1),
	MOD_2 = fromXY(1,2),
	MOD_3 = fromXY(1,3),
	MOD_4 = fromXY(2,0),
	MOD_5 = fromXY(2,1),
	MOD_6 = fromXY(2,2),
	MOD_7 = fromXY(2,3),
};
// clang-format on

const LED modLed[8] = {LED::MOD_0, LED::MOD_1, LED::MOD_2, LED::MOD_3, LED::MOD_4, LED::MOD_5, LED::MOD_6, LED::MOD_7};

struct LedBlinker {
	LED led;
	bool active;
	uint8_t blinksLeft;
	bool returnToState;
	uint8_t blinkingType;
};

extern bool ledBlinkState[];

void setLedState(LED led, bool newState, bool allowContinuedBlinking = false);
void blinkLed(LED led, uint8_t numBlinks = 255, uint8_t blinkingType = 0, bool initialState = true);
void ledBlinkTimeout(uint8_t blinkingType, bool forceRestart = false, bool resetToState = true);
void indicateAlertOnLed(LED led);
void setMeterLevel(uint8_t whichKnob, uint8_t level);
void setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level, bool isBipolar = false);
void actuallySetKnobIndicatorLevel(uint8_t whichKnob, uint8_t level, bool isBipolar = false);
void clearKnobIndicatorLevels();
void blinkKnobIndicator(int32_t whichKnob, bool isBipolar);
void stopBlinkingKnobIndicator(int32_t whichKnob);
void blinkKnobIndicatorLevelTimeout();
uint8_t getLedBlinkerIndex(LED led);
void stopLedBlinking(LED led, bool resetState = false);
bool updateBlinkingLedStates(uint8_t blinkingType);
bool isKnobIndicatorBlinking(int32_t whichKnob);
int32_t getBipolarBrightnessOutputValue(int32_t whichIndicator, int32_t numIndicatorLedsFullyOn, int32_t brightness,
                                        int32_t bipolarLevel);
int32_t getBrightnessOutputValue(int32_t whichIndicator, int32_t numIndicatorLedsFullyOn, int32_t brightness);

} // namespace indicator_leds

using IndicatorLED = indicator_leds::LED;
