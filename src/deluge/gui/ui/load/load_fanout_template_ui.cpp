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

#include "gui/ui/load/load_fanout_template_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "io/midi/midi_fanout.h"
#include "model/action/action_logger.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include <string.h>

using namespace deluge;

LoadFanOutTemplateUI loadFanOutTemplateUI{};

bool LoadFanOutTemplateUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool LoadFanOutTemplateUI::opened() {
	Error error = createFoldersRecursiveIfNotExists(MIDIFanOut::kSnapshotsFolder);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	error = beginSlotSession();
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	actionLogger.deleteAllLogs();

	error = setupForLoadingTemplate(); // Sets currentDir and draws the QWERTY interface.
	if (error != Error::NONE) {
		renderingNeededRegardlessOfUI();
		display->displayError(error);
		return false;
	}

	focusRegained();

	return true;
}

Error LoadFanOutTemplateUI::setupForLoadingTemplate() {
	if (display->haveOLED()) {
		title = "Load snapshot";
	}

	enteredText.clear();

	Error error = currentDir.set(MIDIFanOut::kSnapshotsFolder);
	if (error != Error::NONE) {
		return error;
	}

	error = arrivedInNewFolder(0, nullptr, MIDIFanOut::kSnapshotsFolder);
	if (error != Error::NONE) {
		return error;
	}

	drawKeys();

	if (display->have7SEG()) {
		displayText(false);
	}

	return Error::NONE;
}

void LoadFanOutTemplateUI::folderContentsReady(int32_t entryDirection) {
}

void LoadFanOutTemplateUI::enterKeyPress() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	// If it's a directory...
	if (currentFileItem->isFolder) {
		Error error = goIntoFolder(currentFileItem->filename.get());
		if (error != Error::NONE) {
			display->displayError(error);
			close();
			return;
		}
	}
	else {
		Error error = performLoad();
		if (error != Error::NONE) {
			display->displayError(error);
			return;
		}
		display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_FAN_OUT_TEMPLATE_LOADED));
		close();
	}
}

ActionResult LoadFanOutTemplateUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}

	if (on && b == BACK) {
		// don't allow navigation backwards if we're in the default folder
		if (!strcmp(currentDir.get(), MIDIFanOut::kSnapshotsFolder)) {
			close();
			return ActionResult::DEALT_WITH;
		}
	}

	return LoadUI::buttonAction(b, on, inCardRoutine);
}

ActionResult LoadFanOutTemplateUI::padAction(int32_t x, int32_t y, int32_t on) {
	if (x < kDisplayWidth) {
		return LoadUI::padAction(x, y, on);
	}
	LoadUI::exitAction();
	return ActionResult::DEALT_WITH;
}

Error LoadFanOutTemplateUI::performLoad() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}

	return MIDIFanOut::loadGroupTemplate(&currentFileItem->filePointer, MIDIFanOut::templateGroupIndex);
}
