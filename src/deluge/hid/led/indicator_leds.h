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

namespace IndicatorLEDs {

constexpr uint8_t uartBase =
#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
	152;
#else
	120;
#endif

constexpr uint8_t fromXY(int x, int y) {
#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
	return uartBase + x + y * 9;
#else
	return uartBase + x + y * 10;
#endif
}

typedef uint8_t IndicatorLED;

enum KnownIndicatorLEDs : IndicatorLED {
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
};

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
const IndicatorLED modLed[6] = {
	fromXY(0,2),
	fromXY(0,3),
	fromXY(1,3),
	fromXY(1,2),
	fromXY(2,2),
	fromXY(3,2),
};
#else
const IndicatorLED modLed[8] = {
	fromXY(1,0),
	fromXY(1,1),
	fromXY(1,2),
	fromXY(1,3),
	fromXY(2,0),
	fromXY(2,1),
	fromXY(2,2),
	fromXY(2,3),
};
#endif

struct LedBlinker {
	IndicatorLED led;
	bool active;
	uint8_t blinksLeft;
	bool returnToState;
	uint8_t blinkingType;
};

extern bool ledBlinkState[];

void setLedState(IndicatorLED led, bool newState, bool allowContinuedBlinking = false);
void blinkLed(IndicatorLED led, uint8_t numBlinks = 255, uint8_t blinkingType = 0, bool initialState = true);
void ledBlinkTimeout(uint8_t blinkingType, bool forceRestart = false, bool resetToState = true);
void indicateAlertOnLed(IndicatorLED led);
void setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level);
void clearKnobIndicatorLevels();
void blinkKnobIndicator(int whichKnob);
void stopBlinkingKnobIndicator(int whichKnob);
void blinkKnobIndicatorLevelTimeout();
uint8_t getLedBlinkerIndex(IndicatorLED led);
void stopLedBlinking(IndicatorLED led, bool resetState = false);
bool updateBlinkingLedStates(uint8_t blinkingType);
bool isKnobIndicatorBlinking(int whichKnob);

} // namespace IndicatorLEDs
