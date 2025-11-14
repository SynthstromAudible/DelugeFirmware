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

#include "hid/buttons.h"
#include "definitions_cxx.hpp"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/visualizer.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/settings/runtime_feature_settings.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"
#include "testing/hardware_testing.h"
#include <map>
#include <string>

#include "io/debug/log.h"

namespace Buttons {

bool recordButtonPressUsedUp;
uint32_t timeRecordButtonPressed;
bool selectButtonPressUsedUp;
/**
 * AudioEngine::audioSampleTimer value at which the shift button was pressed. Used to distinguish between short and
 * long shift presses, for sticky keys.
 */
uint32_t timeShiftButtonPressed;

/**
 * Whether or not the shift button is currently depressed (either physically or because a sticky shift has occured)
 */
bool shiftCurrentlyPressed = false;
/**
 * Whether shift should be left on during the release
 */
bool shiftCurrentlyStuck = false;
/**
 * Flag that's used to manage shift illumination. We can't directly write to the PIC in the button routines because we
 * might be in the card routine, this flag lets the main loop handle illuminating the button when required.
 */
bool shiftHasChangedSinceLastCheck;
/**
 * Flag that represents whether another button was pressed while shift was held, and therefore we should ignore the
 * release of the shift for the purposes of toggling sticky shift.
 */
bool considerShiftReleaseForSticky;
/**
 * Flag that represents whether another button was pressed while cross screen was held, and therefore we should ignore
 * the release of the cross screen for the purposes of toggling cross screen mode.
 */
bool considerCrossScreenReleaseForCrossScreenMode;

bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS]; // The extra col is for "fake" buttons

ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Must happen up here before it's actioned, because if its action accesses SD card, we might multiple-enter this
	// function, and don't want to then be setting this after that later action, erasing what it set
	auto xy = deluge::hid::button::toXY(b);
	buttonStates[xy.x][xy.y] = on;

	// Get current UI for visualizer checks
	UI* currentUI = getCurrentUI();

#if ALLOW_SPAM_MODE
	if (b == X_ENC) {
		spamMode();
		return;
	}
#endif

// This is a debug feature that allows us to output SYSEX debug logging button presses
// See contributing.md for more information
#if ENABLE_MATRIX_DEBUG
	D_PRINT("UI=%s, Button=%s, On=%d", getCurrentUI()->getUIName(), getButtonName(b), on);
#endif
	if (on) {
		// If the user presses a different button while holding shift, don't consider the shift press for the purposes
		// of toggling sticky shift.
		considerShiftReleaseForSticky = false;
		// If the user presses a different button while holding cross screen, don't consider the cross screen press for
		// for the purposes of toggling cross screen mode
		considerCrossScreenReleaseForCrossScreenMode = false;
	}

	ActionResult result;

	// See if it was one of the mod buttons
	for (int32_t i = 0; i < kNumModButtons; i++) {

		if (xy.x == modButtonX[i] && xy.y == modButtonY[i]) {

			if (i < 3) {
				if (buttonStates[modButtonX[0]][modButtonY[0]] && buttonStates[modButtonX[1]][modButtonY[1]]
				    && buttonStates[modButtonX[2]][modButtonY[2]]) {
					ramTestLED(true);
				}
			}
			getCurrentUI()->modButtonAction(i, on);
			goto dealtWith;
		}
	}

	if (b == AFFECT_ENTIRE) {
		if (on) {
			display->cancelPopup();
		}
		if (on && isShiftButtonPressed() && isButtonPressed(LEARN)) {
			if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EmulatedDisplay)
			    != RuntimeFeatureStateEmulatedDisplay::Hardware) {
				deluge::hid::display::swapDisplayType();
				goto dealtWith;
			}
		}
	}
	else if (b == BACK) [[unlikely]] {
		if (on) {
			uiTimerManager.setTimer(TimerName::BACK_MENU_EXIT, LONG_PRESS_DURATION);
		}
		else {
			uiTimerManager.unsetTimer(TimerName::BACK_MENU_EXIT);
		}
	}
	else if (b == CROSS_SCREEN_EDIT) {
		if (on) {
			// The next release has a chance of toggling cross screen mode
			considerCrossScreenReleaseForCrossScreenMode = true;
		}
	}
	else if (b == SELECT_ENC) {
		if (on) {
			selectButtonPressUsedUp = false;
		}
	}

	// Handle visualizer mode switching when visualizer is active in Arranger or Song view
	if (on && (currentUI == &sessionView || currentUI == &arrangerView)
	    && deluge::hid::display::Visualizer::isActive(view.displayVUMeter)
	    && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	    && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		if (b == SYNTH) {
			deluge::hid::display::Visualizer::setSessionMode(RuntimeFeatureStateVisualizer::VisualizerWaveform);
			goto dealtWith;
		}
		else if (b == KIT) {
			deluge::hid::display::Visualizer::setSessionMode(RuntimeFeatureStateVisualizer::VisualizerSpectrum);
			goto dealtWith;
		}
		else if (b == MIDI) {
			deluge::hid::display::Visualizer::setSessionMode(RuntimeFeatureStateVisualizer::VisualizerEqualizer);
			goto dealtWith;
		}
	}

	result = getCurrentUI()->buttonAction(b, on, inCardRoutine);

	if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	else if (result == ActionResult::DEALT_WITH) {
		goto dealtWith;
	}

	// Play button
	if (b == PLAY) {
		if (on) {

			if (audioRecorder.recordingSource > AudioInputChannel::NONE && isButtonPressed(RECORD)) {
				// Stop output-recording at end of loop
				if (!recordButtonPressUsedUp && playbackHandler.isEitherClockActive()) {
					currentPlaybackMode->stopOutputRecordingAtLoopEnd();
				}
			}

			else {

				// if (inCardRoutine) return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				playbackHandler.playButtonPressed(kInternalButtonPressLatency);

				// Begin output-recording simultaneously with playback
				if (isButtonPressed(RECORD) && playbackHandler.playbackState && !recordButtonPressUsedUp) {
					audioRecorder.beginOutputRecording();
				}
			}

			recordButtonPressUsedUp = true;
		}
	}
	// Shift button
	else if (b == SHIFT) {
		if (on) {
			timeShiftButtonPressed = AudioEngine::audioSampleTimer;

			// Shift should always be active when the button is physically pressed.
			shiftCurrentlyPressed = true;
			// The next release has a chance of activating sticky keys
			considerShiftReleaseForSticky = true;
			// Shift has changed, make sure we notify.
			shiftHasChangedSinceLastCheck = true;
		}
		else {
			uint32_t releaseTime = AudioEngine::audioSampleTimer;
			if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky) == RuntimeFeatureStateToggle::On) {
				uint32_t delta = releaseTime - timeShiftButtonPressed;
				if (delta > std::numeric_limits<uint32_t>::max() / 2) {
					// the audio sample timer has overflowed, the actual delta is time from the button down being
					// recorded -> overflow point + 0 -> current time.
					//
					// This logic technically breaks if the counter overflows more than once between shift down and up
					// but that would require the user to hold down shift for 2**32 / 44100 = 97391 seconds or about
					// 1.13 days. I think it's OK if we don't handle that.
					delta = std::numeric_limits<uint32_t>::max() - timeShiftButtonPressed;
					delta += releaseTime;
				}
				// We got a short press, maybe enable sticky keys
				// 5th of a second
				if (delta < FlashStorage::holdTime) {
					// unstick shift if another button was pressed while shift was held, or we were already stuck and
					// this short press is to get us unstuck.
					shiftCurrentlyStuck = considerShiftReleaseForSticky && !shiftCurrentlyStuck;
				}
			}

			// As long as shift isn't currently stuck (i.e. we just had a short press that enabled sticky keys), clear
			// the shift pressed flag on release.
			if (!shiftCurrentlyStuck) {
				shiftCurrentlyPressed = false;
				shiftHasChangedSinceLastCheck = true;
			}
		}
	}
	// Record button
	else if (b == RECORD) {
		// Press on
		if (on) {
			timeRecordButtonPressed = AudioEngine::audioSampleTimer;

			recordButtonPressUsedUp = false;

			if (audioRecorder.recordingSource == AudioInputChannel::NONE) {
				if (isShiftButtonPressed()) {
					audioRecorder.beginOutputRecording();
					recordButtonPressUsedUp = true;
				}
			}
		}

		// Press off
		else {
			if (display->hasPopupOfType(PopupType::THRESHOLD_RECORDING_MODE)) {
				display->cancelPopup();
			}
			else if (!recordButtonPressUsedUp
			         && (int32_t)(AudioEngine::audioSampleTimer - timeRecordButtonPressed) < kShortPressTime) {
				if (audioRecorder.isCurrentlyResampling()) {
					audioRecorder.endRecordingSoon(kInternalButtonPressLatency);
				}
				else {
					playbackHandler.recordButtonPressed();
				}
			}
		}
	}

	// Tempo encoder button
	else if (b == TEMPO_ENC) {
		if (on) {
			if (isShiftButtonPressed()) {
				playbackHandler.commandClearTempoAutomation();
			}
			else if (isButtonPressed(TAP_TEMPO)) {
				playbackHandler.commandDisplaySwingInterval();
			}
			else {
				UI* currentUI = getCurrentUI();
				bool isOLEDSessionView =
				    display->haveOLED() && (currentUI == &sessionView || currentUI == &arrangerView);
				// only display tempo pop-up if we're using 7SEG or we're not currently in Song / Arranger View
				if (!loadSongUI.isLoadingSong() && !isOLEDSessionView) {
					playbackHandler.commandDisplayTempo();
				}
			}
		}
	}

#if ALLOW_SPAM_MODE
	else if (b == SELECT_ENC)
		     && isButtonPressed(shiftButtonCoord.x, shiftButtonCoord.y)) {
			     spamMode();
		     }
#endif

	// Mod encoder buttons
	else if (b == MOD_ENCODER_0) {
		getCurrentUI()->modEncoderButtonAction(0, on);
	}
	else if (b == MOD_ENCODER_1) {
		getCurrentUI()->modEncoderButtonAction(1, on);
	}
dealtWith:

	return ActionResult::DEALT_WITH;
}

bool isButtonPressed(deluge::hid::Button b) {
	auto xy = deluge::hid::button::toXY(b);
	return buttonStates[xy.x][xy.y];
}

bool isAnyOfButtonsPressed(std::initializer_list<deluge::hid::Button> buttons) {
	for (const auto button : buttons) {
		if (isButtonPressed(button)) {
			return true;
		}
	}
	return false;
}

bool isShiftButtonPressed() {
	return shiftCurrentlyPressed;
}

bool isShiftStuck() {
	return shiftCurrentlyStuck;
}

void clearShiftSticky() {
	shiftCurrentlyStuck = false;
	shiftCurrentlyPressed = false;
	shiftHasChangedSinceLastCheck = true;
}

bool shiftHasChanged() {
	bool toReturn = shiftHasChangedSinceLastCheck;
	shiftHasChangedSinceLastCheck = false;
	return toReturn;
}

// Correct any misunderstandings
void noPressesHappening(bool inCardRoutine) {
	for (int32_t x = 0; x < NUM_BUTTON_COLS; x++) {
		for (int32_t y = 0; y < NUM_BUTTON_ROWS; y++) {
			if (buttonStates[x][y]) {
				buttonAction(deluge::hid::button::fromXY(x, y), false, inCardRoutine);
			}
		}
	}
}
void ignoreCurrentShiftForSticky() {
	considerShiftReleaseForSticky = false;
}

const char* getButtonName(uint8_t button) {
	switch (button) {
	case deluge::hid::button::KnownButtons::AFFECT_ENTIRE:
		return "AFFECT_ENTIRE";
	case deluge::hid::button::KnownButtons::SESSION_VIEW:
		return "SESSION_VIEW";
	case deluge::hid::button::KnownButtons::CLIP_VIEW:
		return "CLIP_VIEW";
	case deluge::hid::button::KnownButtons::SYNTH:
		return "SYNTH";
	case deluge::hid::button::KnownButtons::KIT:
		return "KIT";
	case deluge::hid::button::KnownButtons::MIDI:
		return "MIDI";
	case deluge::hid::button::KnownButtons::CV:
		return "CV";
	case deluge::hid::button::KnownButtons::KEYBOARD:
		return "KEYBOARD";
	case deluge::hid::button::KnownButtons::SCALE_MODE:
		return "SCALE_MODE";
	case deluge::hid::button::KnownButtons::CROSS_SCREEN_EDIT:
		return "CROSS_SCREEN_EDIT";
	case deluge::hid::button::KnownButtons::BACK:
		return "BACK";
	case deluge::hid::button::KnownButtons::LOAD:
		return "LOAD";
	case deluge::hid::button::KnownButtons::SAVE:
		return "SAVE";
	case deluge::hid::button::KnownButtons::LEARN:
		return "LEARN";
	case deluge::hid::button::KnownButtons::TAP_TEMPO:
		return "TAP_TEMPO";
	case deluge::hid::button::KnownButtons::SYNC_SCALING:
		return "SYNC_SCALING";
	case deluge::hid::button::KnownButtons::TRIPLETS:
		return "TRIPLETS";
	case deluge::hid::button::KnownButtons::PLAY:
		return "PLAY";
	case deluge::hid::button::KnownButtons::RECORD:
		return "RECORD";
	case deluge::hid::button::KnownButtons::SHIFT:
		return "SHIFT";
	case deluge::hid::button::KnownButtons::MOD7:
		return "MOD7";
	case deluge::hid::button::KnownButtons::X_ENC:
		return "X_ENC";
	case deluge::hid::button::KnownButtons::Y_ENC:
		return "Y_ENC";
	case deluge::hid::button::KnownButtons::MOD_ENCODER_0:
		return "MOD_ENCODER_0";
	case deluge::hid::button::KnownButtons::MOD_ENCODER_1:
		return "MOD_ENCODER_1";
	case deluge::hid::button::KnownButtons::SELECT_ENC:
		return "SELECT_ENC";
	case deluge::hid::button::KnownButtons::TEMPO_ENC:
		return "TEMPO_ENC";
	default:
		return "UNKNOWN_BUTTON";
	}
}

} // namespace Buttons
