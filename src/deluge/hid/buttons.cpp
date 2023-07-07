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

bool recordButtonPressUsedUp;
uint32_t timeRecordButtonPressed;
bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS]; // The extra col is for "fake" buttons
Wren::VM* wren;

int buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	wren->buttonAction(b, on);
	return buttonActionNoRe(b, on, inCardRoutine);
}

int buttonActionNoRe(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	// Must happen up here before it's actioned, because if its action accesses SD card, we might multiple-enter this function, and don't want to then be setting this after that later action, erasing what it set
	auto xy = hid::button::toXY(b);
	buttonStates[xy.x][xy.y] = on;

#if ALLOW_SPAM_MODE
	if (b == X_ENC) {
		spamMode();
		return;
	}
#endif

	int result;

	// See if it was one of the mod buttons
	for (int i = 0; i < NUM_MOD_BUTTONS; i++) {

		if (xy.x == modButtonX[i] && xy.y == modButtonY[i]) {

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

	result = getCurrentUI()->buttonAction(b, on, inCardRoutine);

	if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) {
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	else if (result == ACTION_RESULT_DEALT_WITH) {
		goto dealtWith;
	}

	// Play button
	if (b == PLAY) {
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
	else if (b == RECORD) {
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
		     && isButtonPressed(shiftButtonX, shiftButtonY)) {
			     spamMode();
		     }
#endif

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	// Mod encoder buttons
	else if (b == MOD_ENCODER_0) {
		getCurrentUI()->modEncoderButtonAction(0, on);
	}
	else if (b == MOD_ENCODER_1) {
		getCurrentUI()->modEncoderButtonAction(1, on);
	}
#endif

dealtWith:

	return ACTION_RESULT_DEALT_WITH;
}

bool isButtonPressed(int x, int y) {
	return buttonStates[x][y];
}

bool isButtonPressed(hid::Button b) {
	auto xy = hid::button::toXY(b);
	return buttonStates[xy.x][xy.y];
}

bool isShiftButtonPressed() {
	return buttonStates[shiftButtonX][shiftButtonY];
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
				buttonAction(hid::button::fromXY(x, y), false, inCardRoutine);
			}
		}
	}
}
} // namespace Buttons
