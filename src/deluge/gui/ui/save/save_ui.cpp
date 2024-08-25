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

#include "gui/ui/save/save_ui.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/save_song_or_instrument.h"
#include "gui/ui/ui.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"

using namespace deluge;

bool SaveUI::currentFolderIsEmpty;

SaveUI::SaveUI() {
	mayDefaultToBrandNewNameOnEntry = true;
}

bool SaveUI::opened() {
	Error error = beginSlotSession(true, true);
	if (error != Error::NONE) {
		display->displayError(error);
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
        display->setTextAsSlot(currentSlot, currentSubSlot, currentFileExists, true, numberEditPos);
        indicator_leds::ledBlinkTimeout(0, true, !blinkImmediately);
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

		Error error = goIntoFolder(currentFileItem->filename.get());

		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else if (enteredText.isEmpty()) {} // Previously had &&currentFolderIsEmpty ... why?

	else {
		SlotBrowser::enterKeyPress();
		bool dealtWith = performSave(false);

		if (display->have7SEG()) {
			if (!dealtWith) {
				displayText(false);
			}
		}
	}
}

ActionResult SaveUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	FileItem* currentFileItem = getCurrentFileItem();

	// Save button
	if (b == SAVE && !Buttons::isShiftButtonPressed()) {
		return mainButtonAction(on);
	}

	// Pressing on select encoder button will go through here
	else {
		return SlotBrowser::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SaveUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {
		convertToPrefixFormatIfPossible();

		bool available = gui::context_menu::saveSongOrInstrument.setupAndCheckAvailability();

		if (available) {
			currentUIMode = UI_MODE_NONE;
			display->setNextTransitionDirection(1);
			openUI(&gui::context_menu::saveSongOrInstrument);
		}
		else {
			exitUIMode(UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS);
		}

		return ActionResult::DEALT_WITH;
	}
	else {
		return SlotBrowser::timerCallback();
	}
}
