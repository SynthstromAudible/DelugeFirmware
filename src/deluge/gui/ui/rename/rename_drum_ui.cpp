/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/views/instrument_clip_view.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/sound_editor.h"
#include "hid/matrix/matrix_driver.h"
#include "hid/display/numeric_driver.h"
#include "model/drum/kit.h"
#include "model/song/song.h"
#include "hid/led/pad_leds.h"
#include "hid/buttons.h"
#include "extern.h"
#include "model/clip/clip.h"

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

RenameDrumUI renameDrumUI{};

RenameDrumUI::RenameDrumUI() {
#if HAVE_OLED
	title = "Rename item";
#endif
}

bool RenameDrumUI::opened() {
	bool success = QwertyUI::opened();
	if (!success) {
		return false;
	}

	enteredText.set(&getDrum()->name);

	displayText();

	PadLEDs::clearMainPadsWithoutSending();
	drawKeys();
	PadLEDs::sendOutMainPadColours();
	return true;
}

bool RenameDrumUI::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	*cols = 0b11;
	return true;
}

SoundDrum* RenameDrumUI::getDrum() {
	return (SoundDrum*)soundEditor.currentSound;
}

int RenameDrumUI::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	// Back button
	if (b == BACK) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitUI();
		}
	}

	// Select encoder button
	else if (b == SELECT_ENC) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			enterKeyPress();
		}
	}

	else {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	return ACTION_RESULT_DEALT_WITH;
}

void RenameDrumUI::enterKeyPress() {

	if (enteredText.isEmpty()) {
		return;
	}

	// If actually changing it...
	if (!getDrum()->name.equalsCaseIrrespective(&enteredText)) {
		if (((Kit*)currentSong->currentClip->output)->getDrumFromName(enteredText.get())) {
			numericDriver.displayPopup(HAVE_OLED ? "Duplicate names" : "DUPLICATE");
			return;
		}
	}

	getDrum()->name.set(&enteredText);
	exitUI();
}

void RenameDrumUI::exitUI() {
	numericDriver.setNextTransitionDirection(-1);
	close();
}

int RenameDrumUI::padAction(int x, int y, int on) {

	// Audition pad
	if (x == displayWidth + 1) {
		return instrumentClipView.padAction(x, y, on);
	}

	// Main pad
	else if (x < displayWidth) {
		return QwertyUI::padAction(x, y, on);
	}

	// Otherwise, exit
	else {
		if (on && !currentUIMode) {
			if (sdRoutineLock) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitUI();
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

int RenameDrumUI::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(hid::button::X_ENC)) {
		return ACTION_RESULT_DEALT_WITH;
	}
	return instrumentClipView.verticalEncoderAction(offset, inCardRoutine);
}
