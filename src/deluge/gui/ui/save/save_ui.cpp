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

#include "gui/context_menu/save_song_or_instrument.h"
#include "gui/ui/save/save_ui.h"
#include "hid/matrix/matrix_driver.h"
#include "hid/display/numeric_driver.h"
#include "storage/storage_manager.h"
#include "hid/led/pad_leds.h"
#include "hid/led/indicator_leds.h"
#include "gui/ui/ui.h"
#include "hid/buttons.h"
#include "gui/ui_timer_manager.h"
#include "storage/file_item.h"

using namespace deluge;

bool SaveUI::currentFolderIsEmpty;

SaveUI::SaveUI() {
	mayDefaultToBrandNewNameOnEntry = true;
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
	indicator_leds::blinkLed(IndicatorLED::SAVE);
	return SlotBrowser::focusRegained();
}

/*
// TODO: in the future, there may be a case to be made for moving this to LoadOrSaveUI.
// Check the very similar variations in LoadSongUI and LoadInstrumentPresetUI
void SaveUI::displayText(bool blinkImmediately) {

	if (enteredText.isEmpty() && !currentFolderIsEmpty) {
		indicator_leds::ledBlinkTimeout(0, true, !blinkImmediately);
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
		if (!dealtWith) {
			displayText(false);
		}
#endif
	}
}

int SaveUI::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	FileItem* currentFileItem = getCurrentFileItem();

	// Save button
	if (b == SAVE && !Buttons::isShiftButtonPressed()) {
		return mainButtonAction(on);
	}

	// Select encoder button - we want to override default behaviour here and potentially do nothing, so user doesn't save over something by accident.
	else if (b == SELECT_ENC && currentFileItem && !currentFileItem->isFolder) {}

	else {
		return SlotBrowser::buttonAction(b, on, inCardRoutine);
	}

	return ACTION_RESULT_DEALT_WITH;
}

int SaveUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {
		convertToPrefixFormatIfPossible();

		bool available = gui::context_menu::saveSongOrInstrument.setupAndCheckAvailability();

		if (available) {
			currentUIMode = UI_MODE_NONE;
			numericDriver.setNextTransitionDirection(1);
			openUI(&gui::context_menu::saveSongOrInstrument);
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
