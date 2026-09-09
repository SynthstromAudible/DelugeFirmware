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

#include "gui/ui/save/save_pattern_ui.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/overwrite_file.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include <string.h>

using namespace deluge;

static constexpr const char* PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER = "PATTERNS/RHYTHMIC/KIT";
static constexpr const char* PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER = "PATTERNS/RHYTHMIC/DRUM";
static constexpr const char* PATTERN_MELODIC_DEFAULT_FOLDER = "PATTERNS/MELODIC";

SavePatternUI savePatternUI{};

SavePatternUI::SavePatternUI() {
	filePrefix = "PATTERN";
}

bool SavePatternUI::opened() {

	if (!getRootUI()->toClipMinder() || (getCurrentOutputType() == OutputType::AUDIO)) {
		return false;
	}

	// Fire-and-forget: these only ensure the default folders exist, and the listing reports a card
	// failure if one persists. See Browser::createFoldersRecursiveIfNotExists.
	(void)createFoldersRecursiveIfNotExists(PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER);
	(void)createFoldersRecursiveIfNotExists(PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER);
	(void)createFoldersRecursiveIfNotExists(PATTERN_MELODIC_DEFAULT_FOLDER);

	Instrument* currentInstrument = getCurrentInstrument();
	// Must set this before calling SaveUI::opened(), which uses this to work out folder name
	outputTypeToLoad = currentInstrument->type;

	bool success = SaveUI::opened();
	if (!success) { // In this case, an error will have already displayed.
doReturnFalse:
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads.
		return false;
	}

	currentFolderIsEmpty = false;

	std::string patternFolder = "";
	if (getCurrentOutputType() == OutputType::KIT) {
		if (getRootUI()->getAffectEntire()) {
			defaultDir = PATTERN_RHYTHMIC_KIT_DEFAULT_FOLDER;
			title = "Save Kit Pattern";
			selectedDrumOnly = false;
		}
		else {
			defaultDir = PATTERN_RHYTHMIC_DRUM_DEFAULT_FOLDER;
			title = "Save Drum Pattern";
			selectedDrumOnly = true;
		}
	}
	else {
		defaultDir = std::string(PATTERN_MELODIC_DEFAULT_FOLDER);
		title = "Save Pattern";
		selectedDrumOnly = false;
	}

tryDefaultDir:
	currentDir = defaultDir.c_str();

	fileIcon = deluge::hid::display::OLED::midiIcon;
	fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
	fileIconPt2Width = 0;

	// The listing dispatches asynchronously; return optimistically here. onBrowserOpened() runs
	// the focusRegained tail once the listing succeeds. Failure goes through the base
	// Browser::onListingFailed() (displayError + close()).
	beginListing(
	    {.action = ListingAction::Open, .direction = 0, .filenameToStartAt = enteredText, .defaultDir = defaultDir});
	return true;
}

void SavePatternUI::onBrowserOpened() {
	focusRegained();
}

ActionResult SavePatternUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}
	else {
		if (on && b == BACK) {
			// don't allow navigation backwards if we're in the default folder
			if (!strcmp(currentDir.c_str(), defaultDir.c_str())) {
				close();
				return ActionResult::DEALT_WITH;
			}
		}
		return SaveUI::buttonAction(b, on, inCardRoutine);
	}
}

bool SavePatternUI::performSave(bool mayOverwrite) {
	noteSaveTier();
	std::string filePath = getCurrentFilePath();
	Error error = Error::NONE;
	if (error != Error::NONE) {
fail:
		display->displayError(error);
		return false;
	}

	// The document is already written and stored by the time this runs: SaveUI builds it on the
	// interaction tier and stores it in one operation. Nothing is left here but telling the user.
	display->removeWorkingAnimation();
	display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PATTERN_SAVED));
	close();
	return true;
}

bool SavePatternUI::serializeForSave(XMLSerializer& into) {
	instrumentClipView.copyNotesToFile(into, selectedDrumOnly);
	return true;
}
