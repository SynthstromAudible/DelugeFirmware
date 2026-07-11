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

// Todo: turn this into the open() function - which will need to also be able to return error codes?
Error SlotBrowser::beginSlotSession(bool shouldDrawKeys, bool allowIfNoFolder) {

	// We want to check the SD card is generally working here, so that if not, we can exit out before drawing the QWERTY
	// keyboard.
	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}

	// But we won't try to open the folder yet, because we don't yet know what it should be.

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
			// See if it's numeric. enteredText carries the prefix ("SONG185"), so step past it first.
			char const* numberPart = nameAfterPrefix(enteredText.get());
			if (!numberPart) {
				goto nonNumeric;
			}

			Slot thisSlot = getSlot(numberPart);
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
}

Error SlotBrowser::getCurrentFilePath(String* path) {
	path->set(&currentDir);

	Error error = path->concatenate("/");
	if (error != Error::NONE) {
		return error;
	}

	// enteredText is the real on-card name now, so it needs no reassembling.
	error = path->concatenate(&enteredText);
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
