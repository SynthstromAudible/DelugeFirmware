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

#include "gui/ui/browser/slot_browser.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <string.h>

bool SlotBrowser::currentFileHasSuffixFormatNameImplied;

// Todo: turn this into the open() function - which will need to also be able to return error codes?
Error SlotBrowser::beginSlotSession(bool shouldDrawKeys, bool allowIfNoFolder) {

	currentFileHasSuffixFormatNameImplied = false;

	// We want to check the SD card is generally working here, so that if not, we can exit out before drawing the QWERTY
	// keyboard.
	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}

	// But we won't try to open the folder yet, because we don't yet know what it should be.

	/*
	FRESULT result = f_opendir(&staticDIR, currentDir.get());
	if (result != FR_OK && !allowIfNoFolder) return false;
*/

	bool success = Browser::opened();
	if (!success) {
		return Error::UNSPECIFIED;
	}

	if (shouldDrawKeys) {

		drawKeys();
	}

	return Error::NONE;
}

void SlotBrowser::focusRegained() {
	Browser::displayText(false);
}

ActionResult SlotBrowser::horizontalEncoderAction(int32_t offset) {

	if (!isNoUIModeActive()) {
		return ActionResult::DEALT_WITH;
	}
	if (display->have7SEG()) {
		FileItem* currentFileItem = getCurrentFileItem();
		if (currentFileItem) {
			// See if it's numeric. Here, filename has already had prefix removed if it's numeric.

			Slot thisSlot = getSlot(enteredText.get());
			if (thisSlot.slot < 0) {
				goto nonNumeric;
			}

			numberEditPos -= offset;
			if (numberEditPos > 2) {
				numberEditPos = 2;
			}
			else if (numberEditPos < -1) {
				numberEditPos = -1;
			}

			displayText(numberEditPos >= 0);
			return ActionResult::DEALT_WITH;
		}
	}
	{
nonNumeric:
		if (display->haveOLED()) { // Maintain consistency with before - don't do this on numeric
			qwertyVisible = true;
		}
		return Browser::horizontalEncoderAction(offset);
	}
}

void SlotBrowser::processBackspace() {
	Browser::processBackspace();
	if (display->haveOLED()) {
		if (fileIndexSelected == -1) {
			predictExtendedText();
		}
	}
	else {
		// currentFileExists = false;
		currentFileHasSuffixFormatNameImplied = false;
	}
}

void SlotBrowser::enterKeyPress() {
	convertToPrefixFormatIfPossible();
}

/*
void SlotBrowser::selectEncoderAction(int8_t offset) {
    convertToPrefixFormatIfPossible();
}
*/
// This gets called if you're gonna load the thing, or have turned the select knob to navigate, so these functions can
// treat it as a numeric format name
void SlotBrowser::convertToPrefixFormatIfPossible() {

	FileItem* currentFileItem = getCurrentFileItem();

	if (currentFileItem && currentFileHasSuffixFormatNameImplied && !enteredText.isEmpty()
	    && !currentFileItem->isFolder) {

		int32_t enteredTextLength = enteredText.getLength();

		char const* enteredTextChars = enteredText.get();

		int32_t newSubSlot = -1;
		int32_t newSlot = 0;

		int32_t multiplier = 1;

		for (int32_t i = enteredTextLength - 1; i >= 0; i--) {

			if (i == enteredTextLength - 1) {
				if (enteredTextChars[i] >= 'a' && enteredTextChars[i] <= 'z') {
					newSubSlot = enteredTextChars[i] - 'a';
					continue;
				}
				else if (enteredTextChars[i] >= 'A' && enteredTextChars[i] <= 'Z') {
					newSubSlot = enteredTextChars[i] - 'A';
					continue;
				}
			}

			if (enteredTextChars[i] >= '0' && enteredTextChars[i] <= '9') {
				newSlot += (enteredTextChars[i] - '0') * multiplier;
				multiplier *= 10;
			}

			else {
				return;
			}
		}

		if (multiplier == 1) {
			return;
		}

		enteredText.clear();
		enteredTextEditPos = 0;

		currentFileHasSuffixFormatNameImplied = false;
	}
}

Error SlotBrowser::getCurrentFilenameWithoutExtension(String* filenameWithoutExtension) {
	Error error;
	if (display->have7SEG()) {
		// If numeric...
		Slot slot = getSlot(enteredText.get());
		if (slot.slot != -1) {
			error = filenameWithoutExtension->set(filePrefix);
			if (error != Error::NONE) {
				return error;
			}
			error = filenameWithoutExtension->concatenateInt(slot.slot, 3);
			if (error != Error::NONE) {
				return error;
			}
			if (slot.subSlot != -1) {
				char buffer[2];
				buffer[0] = 'A' + slot.subSlot;
				buffer[1] = 0;
				error = filenameWithoutExtension->concatenate(buffer);
				if (error != Error::NONE) {
					return error;
				}
			}
		}
		else {
			filenameWithoutExtension->set(&enteredText);
		}
	}
	else {
		filenameWithoutExtension->set(&enteredText);
	}

	return Error::NONE;
}

Error SlotBrowser::getCurrentFilePath(String* path) {
	path->set(&currentDir);

	Error error = path->concatenate("/");
	if (error != Error::NONE) {
		return error;
	}

	String filenameWithoutExtension;
	error = getCurrentFilenameWithoutExtension(&filenameWithoutExtension);
	if (error != Error::NONE) {
		return error;
	}

	error = path->concatenate(&filenameWithoutExtension);
	if (error != Error::NONE) {
		return error;
	}
	if (writeJsonFlag) {
		error = path->concatenate(".Json");
	}
	else {
		error = path->concatenate(".XML");
	}

	return error;
}
