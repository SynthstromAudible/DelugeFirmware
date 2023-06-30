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

#include <limits>

#include "definitions.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/settings/runtime_feature_settings.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "testing/hardware_testing.h"

namespace Buttons {

bool recordButtonPressUsedUp;
uint32_t timeRecordButtonPressed;
bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS]; // The extra col is for "fake" buttons

//   Internal variables
// Records the last N times the
uint32_t lastShiftTimes[MAX_SHIFT_PRESSES_TO_STICK] = {0};
uint32_t lastShiftTimeIndex = 0;
bool shiftChanged;
bool isShiftPressed;
bool isShiftSticky;

static void checkEnableStickyKeys(uint32_t currentTime, uint32_t oldestShiftTime, uint32_t threshold, bool checkLess) {
	uint32_t delta = currentTime - oldestShiftTime;
	if (delta > std::numeric_limits<uint32_t>::max() / 2) {
		// Assume the timer wrapped
		delta = std::numeric_limits<uint32_t>::max() - oldestShiftTime;
		delta += currentTime;
	}

	if ((checkLess && delta < threshold) || (delta > threshold)) {
		// If it has been less than 1 second, toggle stickyness
		isShiftSticky = !isShiftSticky;
		// Set the entire time buffer to > 44100 before, to avoid off-by-one taps resulting in immediate
		// disable
		for (auto& time : lastShiftTimes) {
			time = currentTime - threshold;
		}

		// Notify the user that it's stickykeys time
		if (isShiftSticky) {
			numericDriver.displayPopup(HAVE_OLED ? "SHIFT STICKY" : "STCK");
		}
		else {
			numericDriver.displayPopup(HAVE_OLED ? "SHIFT UNSTUCK" : "NSTK");
		}
	}
}

int buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// Must happen up here before it's actioned, because if its action accesses SD card, we might multiple-enter this
	// function, and don't want to then be setting this after that later action, erasing what it set
	buttonStates[x][y] = on;

#if ALLOW_SPAM_MODE
	if (x == xEncButtonX && y == xEncButtonY) {
		spamMode();
		return;
	}
#endif

	int result;

	// See if it was one of the mod buttons
	for (int i = 0; i < NUM_MOD_BUTTONS; i++) {

		if (x == modButtonX[i] && y == modButtonY[i]) {

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
			if (i < 3) {
				if (buttonStates[modButtonX[0]][modButtonY[0]] && buttonStates[modButtonX[1]][modButtonY[1]]
				    && buttonStates[modButtonX[2]][modButtonY[2]]) {
					ramTestLED(true);
				}
			}
#endif
			getCurrentUI()->modButtonAction(i, on);
			goto dealtWith;
		}
	}

	result = getCurrentUI()->buttonAction(x, y, on, inCardRoutine);

	if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
	else if (result == ACTION_RESULT_DEALT_WITH) goto dealtWith;

	// Play button
	if (x == playButtonX && y == playButtonY) {
		if (on) {

			if (audioRecorder.recordingSource && isButtonPressed(recordButtonX, recordButtonY)) {
				// Stop output-recording at end of loop
				if (!recordButtonPressUsedUp && playbackHandler.isEitherClockActive()) {
					currentPlaybackMode->stopOutputRecordingAtLoopEnd();
				}
			}

			else {

				//if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
				playbackHandler.playButtonPressed(INTERNAL_BUTTON_PRESS_LATENCY);

				// Begin output-recording simultaneously with playback
				if (isButtonPressed(recordButtonX, recordButtonY) && playbackHandler.playbackState
				    && !recordButtonPressUsedUp) {
					audioRecorder.beginOutputRecording();
				}
			}

			recordButtonPressUsedUp = true;
		}
	}

	// Record button
	else if (x == recordButtonX && y == recordButtonY) {
		// Press on
		if (on) {
			timeRecordButtonPressed = AudioEngine::audioSampleTimer;

			recordButtonPressUsedUp = false;

			if (!audioRecorder.recordingSource) {

				if (isShiftButtonPressed()) {
					audioRecorder.beginOutputRecording();
					recordButtonPressUsedUp = true;
				}
			}
		}

		// Press off
		else {
			if (!recordButtonPressUsedUp
			    && (int32_t)(AudioEngine::audioSampleTimer - timeRecordButtonPressed) < (44100 >> 1)) {
				if (audioRecorder.isCurrentlyResampling()) {
					audioRecorder.endRecordingSoon(INTERNAL_BUTTON_PRESS_LATENCY);
				}
				else {
					playbackHandler.recordButtonPressed();
				}
			}
		}
	}

	// Tempo encoder button
	else if (x == tempoEncButtonX && y == tempoEncButtonY) {
		if (on) {
			if (isShiftButtonPressed()) playbackHandler.displaySwingAmount();
			else {
				if (getCurrentUI() != &loadSongUI) playbackHandler.displayTempoByCalculation();
			}
		}
	}

	// Shift button.
	else if (x == shiftButtonX && y == shiftButtonY) {
		StickyKeysMode mode = currentStickyKeysSetting();

		if (isShiftSticky) {
			if (on) {
				isShiftPressed = !isShiftPressed;
				shiftChanged = true;
			}
		}
		else {
			isShiftPressed = on;
			shiftChanged = true;
		}

		if (on) {
			lastShiftTimes[lastShiftTimeIndex] = AudioEngine::audioSampleTimer;
			lastShiftTimeIndex = (lastShiftTimeIndex + 1) % MAX_SHIFT_PRESSES_TO_STICK;
			switch (mode) {
			case StickyKeysMode::Disabled:
				break;
			case StickyKeysMode::Enabled:
				break;
			case StickyKeysMode::LongPress:
				break;
			default:
				// We're handling a n-presses sticky keys mode

				// Compute the number of presses required to activate sticky keys
				uint32_t requiredPresses =
				    (static_cast<uint32_t>(mode) - static_cast<uint32_t>(StickyKeysMode::EnabledMinPresses))
				    + MIN_SHIFT_PRESSES_TO_STICK;

				uint32_t previousTimeIndex =
				    ((lastShiftTimeIndex + MAX_SHIFT_PRESSES_TO_STICK) - requiredPresses) % MAX_SHIFT_PRESSES_TO_STICK;

				uint32_t currentTime = AudioEngine::audioSampleTimer;
				uint32_t oldestShiftTime = lastShiftTimes[previousTimeIndex];
				checkEnableStickyKeys(currentTime, oldestShiftTime, 44100, true);
				break;
			}
		}
		else if (mode == StickyKeysMode::LongPress) {
			uint32_t pressStartTime =
			    lastShiftTimes[(lastShiftTimeIndex + MAX_SHIFT_PRESSES_TO_STICK - 1) % MAX_SHIFT_PRESSES_TO_STICK];
			checkEnableStickyKeys(AudioEngine::audioSampleTimer, pressStartTime, 44100 * 2, false);
		}
	}

#if ALLOW_SPAM_MODE
	else if (x == selectEncButtonX && y == selectEncButtonY && isButtonPressed(clipViewButtonX, clipViewButtonY)
	         && isButtonPressed(shiftButtonX, shiftButtonY)) {
		spamMode();
	}
#endif

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	// Mod encoder buttons
	else if (x == modEncoder0ButtonX && y == modEncoder0ButtonY) {
		getCurrentUI()->modEncoderButtonAction(0, on);
	}
	else if (x == modEncoder1ButtonX && y == modEncoder1ButtonY) {
		getCurrentUI()->modEncoderButtonAction(1, on);
	}
#endif

dealtWith:

	return ACTION_RESULT_DEALT_WITH;
}

bool isButtonPressed(int x, int y) {
	return buttonStates[x][y];
}

StickyKeysMode currentStickyKeysSetting() {
	auto mode = static_cast<StickyKeysMode>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::StickyKeys));
	if (mode == StickyKeysMode::Enabled) {
		isShiftSticky = true;
	}

	return mode;
}

bool hasShiftChanged() {
	bool changed = shiftChanged;
	shiftChanged = false;
	return changed;
}

bool isShiftButtonPressed() {
	return isShiftPressed;
}

bool isNewOrShiftButtonPressed() {
#ifdef BUTTON_NEW_X
	return buttonStates[BUTTON_NEW_X][BUTTON_NEW_Y];
#else
	return buttonStates[shiftButtonX][shiftButtonY];
#endif
}

// Correct any misunderstandings
void noPressesHappening(bool inCardRoutine) {
	for (int x = 0; x < NUM_BUTTON_COLS; x++) {
		for (int y = 0; y < NUM_BUTTON_ROWS; y++) {
			if (buttonStates[x][y]) {
				buttonAction(x, y, false, inCardRoutine);
			}
		}
	}
}

} // namespace Buttons
