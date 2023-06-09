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

#include <ArrangerView.h>
#include <RenameOutputUI.h>
#include "Output.h"
#include "matrixdriver.h"
#include "numericdriver.h"
#include "song.h"
#include "PadLEDs.h"
#include "Buttons.h"
#include "extern.h"

RenameOutputUI renameOutputUI;

RenameOutputUI::RenameOutputUI() {
}

bool RenameOutputUI::opened() {
#if HAVE_OLED
	if (output->type == OUTPUT_TYPE_AUDIO) title = "Rename track";
	else title = "Rename instrument";
#endif
	bool success = QwertyUI::opened();
	if (!success) return false;

	enteredText.set(&output->name);

	displayText();

	PadLEDs::clearMainPadsWithoutSending();
	drawKeys();
	PadLEDs::sendOutMainPadColours();
	return true;
}

bool RenameOutputUI::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	*cols = 0b11;
	return true;
}

int RenameOutputUI::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// Back button
	if (x == backButtonX && y == backButtonY) {
		if (on && !currentUIMode) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			exitUI();
		}
	}

	// Select encoder button
	else if (x == selectEncButtonX && y == selectEncButtonY) {
		if (on && !currentUIMode) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			enterKeyPress();
		}
	}

	else return ACTION_RESULT_NOT_DEALT_WITH;

	return ACTION_RESULT_DEALT_WITH;
}

void RenameOutputUI::enterKeyPress() {

	if (enteredText.isEmpty()) return;

	// If actually changing it...
	if (!output->name.equalsCaseIrrespective(&enteredText)) {
		if (currentSong->getAudioOutputFromName(&enteredText)) {
			numericDriver.displayPopup(HAVE_OLED ? "Duplicate names" : "DUPLICATE");
			return;
		}
	}

	output->name.set(&enteredText);
	exitUI();
}

void RenameOutputUI::exitUI() {
	numericDriver.setNextTransitionDirection(-1);
	close();
}

int RenameOutputUI::padAction(int x, int y, int on) {

	// Main pad
	if (x < displayWidth) {
		return QwertyUI::padAction(x, y, on);
	}

	// Otherwise, exit
	else {
		if (on && !currentUIMode) {
			if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			exitUI();
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

int RenameOutputUI::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(xEncButtonX, xEncButtonY))
		return ACTION_RESULT_DEALT_WITH;
	return arrangerView.verticalEncoderAction(offset, inCardRoutine);
}
