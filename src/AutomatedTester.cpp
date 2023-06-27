/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

// This experiment never really got fleshed out.

#include "AutomatedTester.h"
#include "definitions.h"
#include "Encoders.h"
#include "functions.h"
#include "AudioEngine.h"
#include <new>

extern "C" {
#include "uart_all_cpus.h"
}

#if AUTOMATED_TESTER_ENABLED

class TestState;

class TestAction {
public:
	TestAction() {}
	virtual TestState* perform() = 0; // Returns new state, or NULL to stay in same state
	virtual int getTimeBetween() = 0; // Return 0 to say don't do this action at all for now
};

class TestState {
public:
	virtual TestAction* const* getActions() = 0;
};

//---------------------------------------------

class ChangePresetTestAction final : public TestAction {
public:
	TestState* perform() {
		AutomatedTester::turnSelectEncoder((getRandom255() >= 128) ? 1 : -1);
		return NULL;
	}
	int getTimeBetween() { return 2 * 44100; }
} changePresetTestAction;

class PlayButtonTestAction final : public TestAction {
public:
	TestState* perform() {
		AutomatedTester::doMomentaryButtonPress(playButtonX, playButtonY);
		return NULL;
	}
	int getTimeBetween() { return 1 * 44100; }
} playButtonTestAction;

class InstrumentClipViewTestState final : public TestState {
public:
	TestAction* const* getActions() {
		static TestAction* const actions[] = {&changePresetTestAction, &playButtonTestAction, NULL};
		return actions;
	};
} instrumentClipViewTestState;

//---------------------------------------------

namespace AutomatedTester {

uint32_t timeLastCall;
TestState* currentState = &instrumentClipViewTestState;

void init() {
}

void turnSelectEncoder(int offset) {
	Encoders::encoders[ENCODER_THIS_CPU_SELECT].detentPos += offset;
}

void doMomentaryButtonPress(int x, int y) {
	int value = (y + displayHeight * 2) * 9 + x;
	uartInsertFakeChar(UART_ITEM_PIC, value);
	uartInsertFakeChar(UART_ITEM_PIC, 252);
	uartInsertFakeChar(UART_ITEM_PIC, value);
}

void possiblyDoSomething() {
	uint32_t timeNow = AudioEngine::audioSampleTimer;
	uint32_t timeSinceLast = timeNow - timeLastCall;
	if (!timeSinceLast) return;

	TestAction* const* actions = currentState->getActions();
	while (*actions) {
		int timeBetween = (*actions)->getTimeBetween();
		if (timeBetween) {

			uint32_t randomThing = ((uint64_t)(uint32_t)getNoise() * timeBetween) >> 32;
			if (randomThing < timeSinceLast) {
				TestState* newState = (*actions)->perform();
				if (newState) {
					currentState = newState;
					break;
				}
			}
		}

		actions++;
	}

	timeLastCall = timeNow;
}
} // namespace AutomatedTester

#endif
