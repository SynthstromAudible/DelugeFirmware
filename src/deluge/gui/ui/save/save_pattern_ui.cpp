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
static constexpr const char* PATTERN_SEQUENCER_STEP_DEFAULT_FOLDER = "PATTERNS/SEQUENCER/STEP";
static constexpr const char* PATTERN_SEQUENCER_PULSE_DEFAULT_FOLDER = "PATTERNS/SEQUENCER/PULSE";

SavePatternUI savePatternUI{};

SavePatternUI::SavePatternUI() {
	filePrefix = "PATTERN";
}

bool SavePatternUI::opened() {

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
	error = createFoldersRecursiveIfNotExists(PATTERN_SEQUENCER_STEP_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}
	error = createFoldersRecursiveIfNotExists(PATTERN_SEQUENCER_PULSE_DEFAULT_FOLDER);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

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

	// Check if current clip has a sequencer mode
	InstrumentClip* currentClip = getCurrentInstrumentClip();
	bool hasSequencerMode = (currentClip && currentClip->hasSequencerMode());
	std::string sequencerModeName = hasSequencerMode ? currentClip->getSequencerModeName() : "";

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
		// Check for sequencer modes first
		if (hasSequencerMode) {
			if (sequencerModeName == "step_sequencer") {
				defaultDir = std::string(PATTERN_SEQUENCER_STEP_DEFAULT_FOLDER);
				title = "Save Step Pattern";
			}
			else if (sequencerModeName == "pulse_seq") {
				defaultDir = std::string(PATTERN_SEQUENCER_PULSE_DEFAULT_FOLDER);
				title = "Save Pulse Pattern";
			}
			else {
				// Fallback for unknown sequencer modes
				defaultDir = std::string(PATTERN_MELODIC_DEFAULT_FOLDER);
				title = "Save Pattern";
			}
		}
		else {
			// Default melodic pattern
			defaultDir = std::string(PATTERN_MELODIC_DEFAULT_FOLDER);
			title = "Save Pattern";
		}
		selectedDrumOnly = false;
	}

tryDefaultDir:
	currentDir.set(defaultDir.c_str());

	fileIcon = deluge::hid::display::OLED::midiIcon;
	fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
	fileIconPt2Width = 0;

	error = arrivedInNewFolder(0, enteredText.get(), defaultDir.c_str());
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		goto doReturnFalse;
	}

	focusRegained();
	return true;
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
			if (!strcmp(currentDir.get(), defaultDir.c_str())) {
				close();
				return ActionResult::DEALT_WITH;
			}
		}
		return SaveUI::buttonAction(b, on, inCardRoutine);
	}
}

bool SavePatternUI::performSave(bool mayOverwrite) {
	if (display->have7SEG()) {
		display->displayLoadingAnimation();
	}

	if (display->have7SEG()) {
		display->displayLoadingAnimation();
	}

	String filePath;
	Error error = getCurrentFilePath(&filePath);
	if (error != Error::NONE) {
fail:
		display->displayError(error);
		return false;
	}

	error = StorageManager::createXMLFile(filePath.get(), smSerializer, mayOverwrite, false);

	if (error == Error::FILE_ALREADY_EXISTS) {
		gui::context_menu::overwriteFile.currentSaveUI = this;

		bool available = gui::context_menu::overwriteFile.setupAndCheckAvailability();

		if (available) { // Will always be true.
			display->setNextTransitionDirection(1);
			openUI(&gui::context_menu::overwriteFile);
			return true;
		}
		else {
			error = Error::UNSPECIFIED;
			goto fail;
		}
	}

	else if (error != Error::NONE) {
		goto fail;
	}

	if (display->haveOLED()) {
		deluge::hid::display::OLED::displayWorkingAnimation("Saving");
	}

	Serializer& writer = GetSerializer();

	instrumentClipView.copyNotesToFile(writer, selectedDrumOnly);

	writer.closeFileAfterWriting();

	display->removeWorkingAnimation();
	if (error != Error::NONE) {
		goto fail;
	}

	display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PATTERN_SAVED));
	close();
	return true;
}
