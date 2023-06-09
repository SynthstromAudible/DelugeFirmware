/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include <SaveSongOrInstrumentContextMenu.h>
#include <SaveUI.h>
#include "matrixdriver.h"
#include "numericdriver.h"
#include "storagemanager.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "UI.h"
#include "Buttons.h"
#include "uitimermanager.h"
#include "FileItem.h"

bool SaveUI::currentFolderIsEmpty;

SaveUI::SaveUI() {
	allowBrandNewNames = true;
}

bool SaveUI::opened() {
	int error = beginSlotSession(true, true);
	if (error) {
		numericDriver.displayError(error);
		return false;
	}

	PadLEDs::clearSideBar();
	return true;
}

void SaveUI::focusRegained() {
	IndicatorLEDs::blinkLed(saveLedX, saveLedY);
	return SlotBrowser::focusRegained();
}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
bool SaveUI::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}
#endif

/*
// TODO: in the future, there may be a case to be made for moving this to LoadOrSaveUI.
// Check the very similar variations in LoadSongUI and LoadInstrumentPresetUI
void SaveUI::displayText(bool blinkImmediately) {

	if (enteredText.isEmpty() && !currentFolderIsEmpty) {
		IndicatorLEDs::ledBlinkTimeout(0, true, !blinkImmediately);
		numericDriver.setTextAsSlot(currentSlot, currentSubSlot, currentFileExists, true, numberEditPos);
	}

	else {
		QwertyUI::displayText(blinkImmediately);
	}
}
*/

void SaveUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();

	// If it's a directory...
	if (currentFileItem && currentFileItem->isFolder) {

		int error = goIntoFolder(currentFileItem->filename.get());

		if (error) {
			numericDriver.displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else if (enteredText.isEmpty()) {} // Previously had &&currentFolderIsEmpty ... why?

	else {
		SlotBrowser::enterKeyPress();
		bool dealtWith = performSave(false);

#if !HAVE_OLED
		if (!dealtWith) displayText(false);
#endif
	}
}

int SaveUI::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	FileItem* currentFileItem = getCurrentFileItem();

	// Save button
	if (x == saveButtonX && y == saveButtonY && !Buttons::isShiftButtonPressed()) {
		return mainButtonAction(on);
	}

	// Select encoder button - we want to override default behaviour here and potentially do nothing, so user doesn't save over something by accident.
	else if (x == selectEncButtonX && y == selectEncButtonY && currentFileItem && !currentFileItem->isFolder) {}

	else return SlotBrowser::buttonAction(x, y, on, inCardRoutine);

	return ACTION_RESULT_DEALT_WITH;
}

int SaveUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {
		convertToPrefixFormatIfPossible();

		bool available = saveSongOrInstrumentContextMenu.setupAndCheckAvailability();

		if (available) {
			currentUIMode = UI_MODE_NONE;
			numericDriver.setNextTransitionDirection(1);
			openUI(&saveSongOrInstrumentContextMenu);
		}
		else {
			exitUIMode(UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS);
		}

		return ACTION_RESULT_DEALT_WITH;
	}
	else {
		return SlotBrowser::timerCallback();
	}
}
