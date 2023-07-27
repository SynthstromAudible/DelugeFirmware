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

#include "gui/ui_timer_manager.h"
#include "definitions_cxx.hpp"
#include "gui/ui/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/numeric_driver.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/clip/clip_minder.h"
#include "model/clip/instrument_clip_minder.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

extern "C" {
#include "RZA1/oled/oled_low_level.h"
#include "RZA1/uart/sio_char.h"
}

UITimerManager uiTimerManager{};
extern void inputRoutine();
extern void batteryLEDBlink();

UITimerManager::UITimerManager() {
	timeNextEvent = 2147483647;

	for (int i = 0; i < NUM_TIMERS; i++) {
		timers[i].active = false;
	}
}

void UITimerManager::routine() {

	int32_t timeTilNextEvent = (uint32_t)(timeNextEvent - AudioEngine::audioSampleTimer);
	if (timeTilNextEvent >= 0) {
		return;
	}

	for (int i = 0; i < NUM_TIMERS; i++) {
		if (timers[i].active) {

			int32_t timeTil = (uint32_t)(timers[i].triggerTime - AudioEngine::audioSampleTimer);
			if (timeTil < 0) {

				timers[i].active = false;

				switch (i) {

				case TIMER_TAP_TEMPO_SWITCH_OFF:
					playbackHandler.tapTempoAutoSwitchOff();
					break;

				case TIMER_MIDI_LEARN_FLASH:
					view.midiLearnFlash();
					break;

				case TIMER_DEFAULT_ROOT_NOTE:
					if (getCurrentUI() == &instrumentClipView) {
						instrumentClipView.flashDefaultRootNote();
					}
					else if (getCurrentUI() == &keyboardScreen) {
						keyboardScreen.flashDefaultRootNote();
					}
					break;

				case TIMER_PLAY_ENABLE_FLASH:
					if (getRootUI() == &sessionView) {
						sessionView.flashPlayRoutine();
					}
					break;

				case TIMER_DISPLAY:
#if HAVE_OLED
					OLED::timerRoutine();
#else
					numericDriver.timerRoutine();
#endif
					break;

				case TIMER_LED_BLINK:
				case TIMER_LED_BLINK_TYPE_1:
					indicator_leds::ledBlinkTimeout(i - TIMER_LED_BLINK);
					break;

				case TIMER_LEVEL_INDICATOR_BLINK:
					indicator_leds::blinkKnobIndicatorLevelTimeout();
					break;

				case TIMER_SHORTCUT_BLINK:
					soundEditor.blinkShortcut();
					break;

				case TIMER_MATRIX_DRIVER:
					PadLEDs::timerRoutine();
					break;

				case TIMER_UI_SPECIFIC: {
					ActionResult result = getCurrentUI()->timerCallback();
					if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
						timers[i].active = true; // Come back soon and try again.
					}
					break;
				}

				case TIMER_DISPLAY_AUTOMATION:
					view.displayAutomation();
					break;

				case TIMER_READ_INPUTS:
					inputRoutine();
					break;

				case TIMER_BATT_LED_BLINK:
					batteryLEDBlink();
					break;

				case TIMER_GRAPHICS_ROUTINE:
					if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) > kNumBytesInColUpdateMessage) {
						getCurrentUI()->graphicsRoutine();
					}
					setTimer(TIMER_GRAPHICS_ROUTINE, 15);
					break;

#if HAVE_OLED
				case TIMER_OLED_LOW_LEVEL:
					oledLowLevelTimerCallback();
					break;

				case TIMER_OLED_CONSOLE:
					OLED::consoleTimerEvent();
					break;

				case TIMER_OLED_SCROLLING_AND_BLINKING:
					OLED::scrollingAndBlinkingTimerEvent();
					break;
#endif
				}
			}
		}
	}

	workOutNextEventTime();
}

void UITimerManager::setTimer(int i, int ms) {
	setTimerSamples(i, ms * 44);
}

void UITimerManager::setTimerSamples(int i, int samples) {
	timers[i].triggerTime = AudioEngine::audioSampleTimer + samples;
	timers[i].active = true;

	int32_t oldTimeTilNextEvent = (uint32_t)(timeNextEvent - AudioEngine::audioSampleTimer);
	if (samples < oldTimeTilNextEvent) {
		timeNextEvent = timers[i].triggerTime;
	}
}

void UITimerManager::setTimerByOtherTimer(int i, int j) {
	timers[i].triggerTime = timers[j].triggerTime;
	timers[i].active = true;
}

void UITimerManager::unsetTimer(int i) {
	timers[i].active = false;
	workOutNextEventTime();
}

bool UITimerManager::isTimerSet(int i) {
	return timers[i].active;
}

void UITimerManager::workOutNextEventTime() {

	int32_t timeTilNextEvent = 2147483647;

	for (int i = 0; i < NUM_TIMERS; i++) {
		if (timers[i].active) {
			int32_t timeTil = timers[i].triggerTime - AudioEngine::audioSampleTimer;
			if (timeTil < timeTilNextEvent) {
				timeTilNextEvent = timeTil;
			}
		}
	}

	timeNextEvent = AudioEngine::audioSampleTimer + (uint32_t)timeTilNextEvent;
}
