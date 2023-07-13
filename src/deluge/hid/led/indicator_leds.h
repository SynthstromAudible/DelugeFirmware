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

#include "RZA1/system/r_typedefs.h"
#include "definitions.h"

#define numLedBlinkers 4

namespace indicator_leds {

constexpr uint8_t uartBase =
#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
    152;
#else
    120;
#endif

constexpr uint8_t fromXY(int x, int y) {
	return x + y * NUM_LED_COLS;
}

// clang-format off
enum class LED : uint8_t {
	AFFECT_ENTIRE     = fromXY(affectEntireLedX, affectEntireLedY),
	SESSION_VIEW      = fromXY(sessionViewLedX, sessionViewLedY),
	CLIP_VIEW         = fromXY(clipViewLedX, clipViewLedY),
	SYNTH             = fromXY(synthLedX, synthLedY),
	KIT               = fromXY(kitLedX, kitLedY),
	MIDI              = fromXY(midiLedX, midiLedY),
	CV                = fromXY(cvLedX, cvLedY),
	KEYBOARD          = fromXY(keyboardLedX, keyboardLedY),
	SCALE_MODE        = fromXY(scaleModeLedX, scaleModeLedY),
	CROSS_SCREEN_EDIT = fromXY(crossScreenEditLedX, crossScreenEditLedY),
	BACK              = fromXY(backLedX, backLedY),
	LOAD              = fromXY(loadLedX, loadLedY),
	SAVE              = fromXY(saveLedX, saveLedY),
	LEARN             = fromXY(learnLedX, learnLedY),
	TAP_TEMPO         = fromXY(tapTempoLedX, tapTempoLedY),
	SYNC_SCALING      = fromXY(syncScalingLedX, syncScalingLedY),
	TRIPLETS          = fromXY(tripletsLedX, tripletsLedY),
	PLAY              = fromXY(playLedX, playLedY),
	RECORD            = fromXY(recordLedX, recordLedY),
	SHIFT             = fromXY(shiftLedX, shiftLedY),
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	MOD_0 = fromXY(0,2),
	MOD_1 = fromXY(0,3),
	MOD_2 = fromXY(1,3),
	MOD_3 = fromXY(1,2),
	MOD_4 = fromXY(2,2),
	MOD_5 = fromXY(3,2),
#else
	MOD_0 = fromXY(1,0),
	MOD_1 = fromXY(1,1),
	MOD_2 = fromXY(1,2),
	MOD_3 = fromXY(1,3),
	MOD_4 = fromXY(2,0),
	MOD_5 = fromXY(2,1),
	MOD_6 = fromXY(2,2),
	MOD_7 = fromXY(2,3),
#endif
};
// clang-format on

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
const LED modLed[6] = {LED::MOD_0, LED::MOD_1, LED::MOD_2, LED::MOD_3, LED::MOD_4, LED::MOD_5};
#else
const LED modLed[8] = {LED::MOD_0, LED::MOD_1, LED::MOD_2, LED::MOD_3, LED::MOD_4, LED::MOD_5, LED::MOD_6, LED::MOD_7};
#endif

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
void setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level);
void clearKnobIndicatorLevels();
void blinkKnobIndicator(int whichKnob);
void stopBlinkingKnobIndicator(int whichKnob);
void blinkKnobIndicatorLevelTimeout();
uint8_t getLedBlinkerIndex(LED led);
void stopLedBlinking(LED led, bool resetState = false);
bool updateBlinkingLedStates(uint8_t blinkingType);
bool isKnobIndicatorBlinking(int whichKnob);

} // namespace indicator_leds

typedef indicator_leds::LED IndicatorLED;
