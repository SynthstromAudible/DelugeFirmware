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
#include "gui/views/view.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/settings/runtime_feature_settings.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "testing/hardware_testing.h"

namespace Buttons {

bool recordButtonPressUsedUp;
uint32_t timeRecordButtonPressed;
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
 * release of the shift for the purposes of enabling sticky keys.
 */
bool considerShiftReleaseForSticky;

bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS]; // The extra col is for "fake" buttons

ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Must happen up here before it's actioned, because if its action accesses SD card, we might multiple-enter this
	// function, and don't want to then be setting this after that later action, erasing what it set
	auto xy = deluge::hid::button::toXY(b);
	buttonStates[xy.x][xy.y] = on;

#if ALLOW_SPAM_MODE
	if (b == X_ENC) {
		spamMode();
		return;
	}
#endif

	// If the user presses a different button while holding shift, don't consider the shift press for the purposes of
	// enabling sticky keys.
	if (on) {
		considerShiftReleaseForSticky = false;
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
				if (delta < kHoldTime) {
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
			if (!recordButtonPressUsedUp
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
				playbackHandler.displaySwingAmount();
			}
			else {
				if (getCurrentUI() != &loadSongUI) {
					playbackHandler.displayTempoByCalculation();
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

bool isShiftButtonPressed() {
	return shiftCurrentlyPressed;
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
} // namespace Buttons
