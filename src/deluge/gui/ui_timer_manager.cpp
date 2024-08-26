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
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/hid_sysex.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"

#include <algorithm>

extern "C" {
#include "RZA1/oled/oled_low_level.h"
}

UITimerManager uiTimerManager{};
extern void inputRoutine();
extern void batteryLEDBlink();

UITimerManager::UITimerManager() {
	timeNextEvent = 2147483647;
}

void UITimerManager::routine() {

	int32_t timeTilNextEvent = (uint32_t)(timeNextEvent - AudioEngine::audioSampleTimer);
	if (timeTilNextEvent >= 0) {
		return;
	}

	for (int32_t i = 0; i < util::to_underlying(TimerName::NUM_TIMERS); i++) {
		auto name = static_cast<TimerName>(i);
		auto& timer = timers_[i];
		if (timer.active) {

			int32_t timeTil = (uint32_t)(timer.triggerTime - AudioEngine::audioSampleTimer);
			if (timeTil < 0) {
				timer.active = false;

				switch (name) {

				case TimerName::TAP_TEMPO_SWITCH_OFF:
					playbackHandler.tapTempoAutoSwitchOff();
					break;

				case TimerName::MIDI_LEARN_FLASH:
					view.midiLearnFlash();
					break;

				case TimerName::DEFAULT_ROOT_NOTE:
					if (getCurrentUI() == &keyboardScreen) {
						keyboardScreen.flashDefaultRootNote();
					}
					else if (getCurrentUI()->getUIContextType() == UIType::INSTRUMENT_CLIP) {
						instrumentClipView.flashDefaultRootNote();
					}
					break;

				case TimerName::PLAY_ENABLE_FLASH: {
					view.flashPlayRoutine();
					break;
				}
				case TimerName::DISPLAY:
					if (display->haveOLED()) {
						auto* oled = static_cast<deluge::hid::display::OLED*>(display);
						oled->timerRoutine();
					}
					else {
						display->timerRoutine();
					}

					break;

				case TimerName::LOADING_ANIMATION:
					if (display->haveOLED()) {
						auto* oled = static_cast<deluge::hid::display::OLED*>(display);
						oled->timerRoutine();
					}
					else {
						display->timerRoutine();
					}

					break;

				case TimerName::LED_BLINK:
				case TimerName::LED_BLINK_TYPE_1:
					indicator_leds::ledBlinkTimeout(i - util::to_underlying(TimerName::LED_BLINK));
					break;

				case TimerName::LEVEL_INDICATOR_BLINK:
					indicator_leds::blinkKnobIndicatorLevelTimeout();
					break;

				case TimerName::SHORTCUT_BLINK:
					soundEditor.blinkShortcut();
					break;

				case TimerName::INTERPOLATION_SHORTCUT_BLINK:
					automationView.blinkInterpolationShortcut();
					break;

				case TimerName::PAD_SELECTION_SHORTCUT_BLINK:
					automationView.blinkPadSelectionShortcut();
					break;

				case TimerName::NOTE_ROW_BLINK:
					instrumentClipView.blinkSelectedNoteRow();
					break;

				case TimerName::SELECTED_CLIP_PULSE:
					sessionView.gridPulseSelectedClip();
					break;

				case TimerName::MATRIX_DRIVER:
					PadLEDs::timerRoutine();
					break;

				case TimerName::UI_SPECIFIC: {
					ActionResult result = getCurrentUI()->timerCallback();
					if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
						timer.active = true; // Come back soon and try again.
					}
					break;
				}
				case TimerName::BACK_MENU_EXIT: {

					getCurrentUI()->exitUI();
					break;
				}

				case TimerName::DISPLAY_AUTOMATION:
					if (((getCurrentUI() == &automationView) || (getRootUI() == &automationView))
					    && automationView.inAutomationEditor()) {

						automationView.displayAutomation();

						if (getCurrentUI() == &soundEditor) {
							soundEditor.getCurrentMenuItem()->readValueAgain();
						}
					}

					else {
						view.displayAutomation();
					}
					break;

				case TimerName::SEND_MIDI_FEEDBACK_FOR_AUTOMATION:
					// midi follow and midi feedback enabled
					// re-send midi cc's because learned parameter values may have changed
					// only send updates when playback is active
					if (playbackHandler.isEitherClockActive()
					    && (midiEngine.midiFollowFeedbackAutomation != MIDIFollowFeedbackAutomationMode::DISABLED)) {
						uint32_t sendRate = 0;
						if (midiEngine.midiFollowFeedbackAutomation == MIDIFollowFeedbackAutomationMode::LOW) {
							sendRate = kLowFeedbackAutomationRate;
						}
						else if (midiEngine.midiFollowFeedbackAutomation == MIDIFollowFeedbackAutomationMode::MEDIUM) {
							sendRate = kMediumFeedbackAutomationRate;
						}
						else if (midiEngine.midiFollowFeedbackAutomation == MIDIFollowFeedbackAutomationMode::HIGH) {
							sendRate = kHighFeedbackAutomationRate;
						}
						// check time elapsed since previous automation update is greater than or equal to send rate
						// if so, send another automation feedback message
						if ((AudioEngine::audioSampleTimer - midiFollow.timeAutomationFeedbackLastSent) >= sendRate) {
							view.sendMidiFollowFeedback(nullptr, kNoSelection, true);
							midiFollow.timeAutomationFeedbackLastSent = AudioEngine::audioSampleTimer;
						}
					}
					// if automation feedback was previously sent and now playback is stopped,
					// send one more update to sync controller with deluge's current values
					// for automated params only
					else if (midiFollow.timeAutomationFeedbackLastSent != 0) {
						view.sendMidiFollowFeedback(nullptr, kNoSelection, true);
						midiFollow.timeAutomationFeedbackLastSent = 0;
					}
					break;

				case TimerName::READ_INPUTS:
					inputRoutine();
					break;

				case TimerName::BATT_LED_BLINK:
					batteryLEDBlink();
					break;

				case TimerName::GRAPHICS_ROUTINE:
					if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) > kNumBytesInColUpdateMessage) {
						getCurrentUI()->graphicsRoutine();
					}
					setTimer(TimerName::GRAPHICS_ROUTINE, 15);
					break;

				case TimerName::OLED_LOW_LEVEL:
					if (deluge::hid::display::have_oled_screen) {
						oledLowLevelTimerCallback();
					}
					break;

				case TimerName::OLED_CONSOLE:
					if (display->haveOLED()) {
						auto* oled = static_cast<deluge::hid::display::OLED*>(display);
						oled->consoleTimerEvent();
					}
					break;

				case TimerName::OLED_SCROLLING_AND_BLINKING:
					if (display->haveOLED()) {
						deluge::hid::display::OLED::scrollingAndBlinkingTimerEvent();
					}
					break;

				case TimerName::SYSEX_DISPLAY:
					HIDSysex::sendDisplayIfChanged();
					break;
				}
			}
		}
	}

	workOutNextEventTime();
}

void UITimerManager::setTimer(TimerName which, int32_t ms) {
	setTimerSamples(which, ms * 44);
}

void UITimerManager::setTimerSamples(TimerName which, int32_t samples) {
	auto& timer = getTimer(which);
	timer.triggerTime = AudioEngine::audioSampleTimer + samples;
	timer.active = true;

	int32_t oldTimeTilNextEvent = (uint32_t)(timeNextEvent - AudioEngine::audioSampleTimer);
	if (samples < oldTimeTilNextEvent) {
		timeNextEvent = timer.triggerTime;
	}
}

void UITimerManager::setTimerByOtherTimer(TimerName which, TimerName fromTimer) {
	auto& timer = getTimer(which);
	auto& srcTimer = getTimer(fromTimer);
	timer.triggerTime = srcTimer.triggerTime;
	timer.active = true;
}

void UITimerManager::unsetTimer(TimerName which) {
	getTimer(which).active = false;
	workOutNextEventTime();
}

bool UITimerManager::isTimerSet(TimerName which) {
	return getTimer(which).active;
}

void UITimerManager::workOutNextEventTime() {

	int32_t timeTilNextEvent = 2147483647;

	for (auto& timer : timers_) {
		if (timer.active) {
			timeTilNextEvent =
			    std::min(static_cast<int32_t>(timer.triggerTime) - static_cast<int32_t>(AudioEngine::audioSampleTimer),
			             timeTilNextEvent);
		}
	}

	timeNextEvent = AudioEngine::audioSampleTimer + (uint32_t)timeTilNextEvent;
}
