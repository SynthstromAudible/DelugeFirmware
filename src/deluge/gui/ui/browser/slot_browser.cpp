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
#include "libdeluge/file_io.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <string.h>

// Todo: turn this into the open() function - which will need to also be able to return error codes?
Error SlotBrowser::beginSlotSession(bool shouldDrawKeys, bool allowIfNoFolder) {

	// NO inline initSD() pre-flight here. This runs on the interaction tier (thread mode), where the
	// storage C-ABI refused to act: `deluge_efatfs_mount()` failed, so initSD() answered
	// Error::SD_CARD ("the card is broken") for a card that was fine. On the Rust BSP that made every slot
	// browser — LOAD SONG included — fail with "SD card error" on a perfectly healthy mounted card.
	//
	// Nothing is lost by dropping it: the listing that follows reads the folder through the directory
	// channel (Browser::beginListing), and reports a genuine failure through Browser::onListingFailed(). The check only
	// ever bought us not drawing the QWERTY keyboard before a card error, and it paid for that by inventing card
	// errors.

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
	qwertyVisible = true;
	return Browser::horizontalEncoderAction(offset);
}

void SlotBrowser::processBackspace() {
	Browser::processBackspace();
	if (fileIndexSelected == -1) {
		predictExtendedText();
	}
}

std::string SlotBrowser::getCurrentFilePath() {
	std::string path = currentDir;

	path.append("/");

	// enteredText is the real on-card name now, so it needs no reassembling.
	path.append(enteredText);
	if (writeJsonFlag) {
		path.append(".Json");
	}
	else {
		path.append(".XML");
	}

	return path;
}
