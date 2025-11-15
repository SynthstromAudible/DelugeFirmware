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

#include "gui/ui/load/load_pattern_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/root_ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "io/debug/log.h"
#include "model/action/action_logger.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

using namespace deluge;

static constexpr const char* PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER = "PATTERNS/RHYTHMIC/KIT";
static constexpr const char* PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER = "PATTERNS/RHYTHMIC/DRUM";
static constexpr const char* PATTERN_MELODIC_DEFAULT_FOLDER = "PATTERNS/MELODIC";

PLACE_SDRAM_BSS LoadPatternUI loadPatternUI{};

bool LoadPatternUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool LoadPatternUI::opened() {
	// Start Pattern Paste Action

	if (!getRootUI()->toClipMinder() || (getCurrentOutputType() == OutputType::AUDIO)) {
		return false;
	}

	Error error = createFoldersRecursiveIfNotExists(PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}
	error = createFoldersRecursiveIfNotExists(PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}
	error = createFoldersRecursiveIfNotExists(PATTERN_MELODIC_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
	overwriteExisting = true;

	if (getCurrentOutputType() == OutputType::KIT) {
		if (getRootUI()->getAffectEntire()) {
			defaultDir = PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER;
			favouritesManager.setCategory(PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER);
			title = "Load Kit Pattern";
			selectedDrumOnly = false;
		}
		else {
			defaultDir = PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER;
			favouritesManager.setCategory(PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER);
			title = "Load Drum Pattern";
			selectedDrumOnly = true;
		}
	}
	else {
		defaultDir = std::string(PATTERN_MELODIC_DEFAULT_FOLDER);
		favouritesManager.setCategory(PATTERN_MELODIC_DEFAULT_FOLDER);
		title = "Load Pattern";
		selectedDrumOnly = false;
	}

	favouritesChanged();
	currentDir.set(defaultDir.c_str());

	error = beginSlotSession(); // Requires currentDir to be set. (Not anymore?)
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	error = setupForLoadingPattern(); // Sets currentDir.
	if (error != Error::NONE) {
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads, in call to setupForLoadingMidiDeviceDefinition().
		display->displayError(error);
		return false;
	}

	focusRegained();

	return true;
}

void LoadPatternUI::setupLoadPatternUI(bool overwriteExistingState, bool noScalingState) {
	overwriteExisting = overwriteExistingState;
	noScaling = noScalingState;
	previewOnly = true;
	if (!overwriteExisting) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PATTERN_NOOVERWRITE));
	}
	if (noScaling) {
		instrumentClipView.patternClear();
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PATTERN_NOSCALING));
	}
	performLoad();
}

void LoadPatternUI::selectEncoderAction(int8_t offset) {
	if (noScaling) {
		instrumentClipView.patternClear();
	}
	LoadUI::selectEncoderAction(offset);
}

void LoadPatternUI::currentFileChanged(int32_t movementDirection) {
	if (!overwriteExisting && actionLogger.firstAction[BEFORE]
	    && actionLogger.firstAction[BEFORE]->type == ActionType::PATTERN_PASTE) {
		actionLogger.revert(BEFORE);
		// Create a new Action where the Events can be added
		actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
	}
	if (noScaling) {
		instrumentClipView.patternClear();
	}
}

// If OLED, then you should make sure renderUIsForOLED() gets called after this.
Error LoadPatternUI::setupForLoadingPattern() {
	enteredText.clear();

	if (display->haveOLED()) {
		fileIcon = deluge::hid::display::OLED::midiIcon;
		fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
		fileIconPt2Width = 1;
	}

	String searchFilename;

	Error error = currentDir.set(defaultDir.c_str());
	if (error != Error::NONE) {
		return error;
	}

	if (!searchFilename.isEmpty()) {
		Error error = searchFilename.concatenate(".XML");
		if (error != Error::NONE) {
			return error;
		}
	}

	error = arrivedInNewFolder(0, searchFilename.get(), defaultDir.c_str());
	if (error != Error::NONE) {
		return error;
	}

	currentLabelLoadError = (fileIndexSelected >= 0) ? Error::NONE : Error::UNSPECIFIED;

	drawKeys();

	if (display->have7SEG()) {
		displayText(false);
	}

	return Error::NONE;
}

void LoadPatternUI::folderContentsReady(int32_t entryDirection) {
}

void LoadPatternUI::enterKeyPress() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	// If it's a directory...
	if (currentFileItem->isFolder) {

		Error error = goIntoFolder(currentFileItem->filename.get());

		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}

	else {
		previewOnly = false;
		performLoad();
		close();
	}
}

ActionResult LoadPatternUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Load button
	if (b == LOAD) {
		previewOnly = false;
		return mainButtonAction(on);
	}
	else if (b == PLAY) {
		// Need to use special preview mode for this as on constant playing, big Midi files can lead to stucked notes
		FileItem* currentFileItem = getCurrentFileItem();
		if (!currentFileItem->isFolder) {
			if (on) {
				previewOnly = true;

				performLoad();
				// rerenndering Keyboard
				renderingNeededRegardlessOfUI();
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_PATTERN_PREVIEW));
			}
		}
		instrumentClipView.patternPreview();
		// rerenndering Keyboard

		return ActionResult::DEALT_WITH;
	}
	else {
		if (on && b == BACK) {
			// don't allow navigation backwards if we're in the default folder
			if (!strcmp(currentDir.get(), defaultDir.c_str())) {
				// Undo all Changes made during Pattern Preview
				if (actionLogger.firstAction[BEFORE]
				    && actionLogger.firstAction[BEFORE]->type == ActionType::PATTERN_PASTE) {
					actionLogger.closeAction(ActionType::PATTERN_PASTE);
					actionLogger.revert(BEFORE, false, false);
				}
				close();
				return ActionResult::DEALT_WITH;
			}
		}
		return LoadUI::buttonAction(b, on, inCardRoutine);
	}
}

ActionResult LoadPatternUI::padAction(int32_t x, int32_t y, int32_t on) {
	if (x < kDisplayWidth) {
		return LoadUI::padAction(x, y, on);
	}
	else {
		LoadUI::exitAction();
		return ActionResult::DEALT_WITH;
	}
}

Error LoadPatternUI::performLoad() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		// Make it say "NONE" on numeric Deluge, for
		// consistency with old times.
		return display->haveOLED() ? Error::FILE_NOT_FOUND : Error::NO_FURTHER_FILES_THIS_DIRECTION;
	}

	if (currentFileItem->isFolder) {
		return Error::NONE;
	}

	if (!previewOnly && !noScaling) {
		if (actionLogger.firstAction[BEFORE] && actionLogger.firstAction[BEFORE]->type == ActionType::PATTERN_PASTE) {
			actionLogger.closeAction(ActionType::PATTERN_PASTE);
			actionLogger.revert(BEFORE, false, false);
		}
		actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
	}

	String fileName;
	fileName.set(currentDir.get());
	fileName.concatenate("/");
	fileName.concatenate(enteredText.get());
	fileName.concatenate(".XML");

	Error error = StorageManager::loadPatternFile(&currentFileItem->filePointer, &fileName, overwriteExisting,
	                                              noScaling, previewOnly, selectedDrumOnly);

	if (error != Error::NONE) {
		display->displayError(currentLabelLoadError);
		return error;
	}

	return Error::NONE;
}
