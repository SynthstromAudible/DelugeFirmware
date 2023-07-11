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

#include "processing/engines/audio_engine.h"
#include "hid/buttons.h"
#include "definitions.h"
#include "gui/ui/ui.h"
#include "gui/ui/audio_recorder.h"
#include "playback/playback_handler.h"
#include "playback/mode/playback_mode.h"
#include "gui/ui/load/load_song_ui.h"
#include "testing/hardware_testing.h"
#include "gui/views/view.h"
#include "model/mod_controllable/mod_controllable.h"

namespace Buttons {

using namespace hid::button;

bool recordButtonPressUsedUp;
uint32_t timeRecordButtonPressed;
bool buttonStates[(NUM_BUTTON_COLS + 1) * NUM_BUTTON_ROWS]; // The extra col is for "fake" buttons

int buttonAction(hid::Button b, bool on, bool inCardRoutine) {

	// Must happen up here before it's actioned, because if its action accesses SD card, we might multiple-enter this function, and don't want to then be setting this after that later action, erasing what it set
	buttonStates[static_cast<int>(b)] = on;

#if ALLOW_SPAM_MODE
	if (b == Button::X_ENC) {
		spamMode();
		return;
	}
#endif

	int result;

	// See if it was one of the mod buttons
	for (int i = 0; i < NUM_MOD_BUTTONS; i++) {

		if (b == modButton[i]) {

			if (i < 3) {
				if (buttonStates[static_cast<int>(modButton[0])] && buttonStates[static_cast<int>(modButton[1])]
				    && buttonStates[static_cast<int>(modButton[2])]) {
					ramTestLED(true);
				}
			}
			getCurrentUI()->modButtonAction(i, on);
			goto dealtWith;
		}
	}

	result = getCurrentUI()->buttonAction(b, on, inCardRoutine);

	if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) {
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	else if (result == ACTION_RESULT_DEALT_WITH) {
		goto dealtWith;
	}

	// Play button
	if (b == Button::PLAY) {
		if (on) {

			if (audioRecorder.recordingSource && isButtonPressed(Button::RECORD)) {
				// Stop output-recording at end of loop
				if (!recordButtonPressUsedUp && playbackHandler.isEitherClockActive()) {
					currentPlaybackMode->stopOutputRecordingAtLoopEnd();
				}
			}

			else {

				//if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
				playbackHandler.playButtonPressed(INTERNAL_BUTTON_PRESS_LATENCY);

				// Begin output-recording simultaneously with playback
				if (isButtonPressed(Button::RECORD) && playbackHandler.playbackState && !recordButtonPressUsedUp) {
					audioRecorder.beginOutputRecording();
				}
			}

			recordButtonPressUsedUp = true;
		}
	}

	// Record button
	else if (b == Button::RECORD) {
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
	else if (b == Button::TEMPO_ENC) {
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
	else if (b == Button::SELECT_ENC)
		     && isButtonPressed(shiftButtonX, shiftButtonY)) {
			     spamMode();
		     }
#endif

	// Mod encoder buttons
	else if (b == Button::MOD_ENCODER_0) {
		getCurrentUI()->modEncoderButtonAction(0, on);
	}
	else if (b == Button::MOD_ENCODER_1) {
		getCurrentUI()->modEncoderButtonAction(1, on);
	}

dealtWith:

	return ACTION_RESULT_DEALT_WITH;
}

bool isButtonPressed(hid::Button b) {
	return buttonStates[static_cast<int>(b)];
}

bool isShiftButtonPressed() {
	return buttonStates[static_cast<int>(Button::SHIFT)];
}

bool isNewOrShiftButtonPressed() {
#ifdef BUTTON_NEW_X
	return buttonStates[static_cast<int>(Button::NEW)];
#else
	return buttonStates[static_cast<int>(Button::SHIFT)];
#endif
}

// Correct any misunderstandings
void noPressesHappening(bool inCardRoutine) {
	for (int i = 0; i < NUM_BUTTON_COLS * NUM_BUTTON_ROWS; i++) {
		if (buttonStates[i]) {
			buttonAction(static_cast<Button>(i), false, inCardRoutine);
		}
	}
}
} // namespace Buttons
