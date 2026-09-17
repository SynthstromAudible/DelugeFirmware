/*
 * Copyright (c) 2024 Sean Ditny
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

#include "gui/ui/load/load_midi_device_definition_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/root_ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "io/debug/log.h"
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

using namespace deluge;

#define MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER "MIDI_DEVICES/DEFINITION"

LoadMidiDeviceDefinitionUI loadMidiDeviceDefinitionUI{};

bool LoadMidiDeviceDefinitionUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool LoadMidiDeviceDefinitionUI::opened() {
	if (!getRootUI()->toClipMinder() || getCurrentOutputType() != OutputType::MIDI_OUT) {
		return false;
	}

	// Fire-and-forget: these only ensure the default folders exist, and the listing reports a card
	// failure if one persists. See Browser::createFoldersRecursiveIfNotExists.
	(void)createFoldersRecursiveIfNotExists(MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER);

	Error error = beginSlotSession(); // Requires currentDir to be set. (Not anymore?)
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	actionLogger.deleteAllLogs();

	std::string searchFilename = setupForLoadingMidiDeviceDefinition(); // Sets currentDir.
	// The listing happens async: dispatch it and return optimistically. Failure goes through the
	// base Browser::onListingFailed() (displayError + close()) once the listing completes.
	beginListing({.action = ListingAction::Open,
	              .direction = 0,
	              .filenameToStartAt = searchFilename,
	              .defaultDir = MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER});

	return true;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
// Computes the icon/title state and currentDir, and returns the filename to search for within it.
// Does NOT perform the listing itself - opened() combines this with
// MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER to dispatch an async Open listing; the post-listing tail
// lives in onBrowserOpened().
// If OLED, then you should make sure renderUIsForOLED() gets called after this.
std::string LoadMidiDeviceDefinitionUI::setupForLoadingMidiDeviceDefinition() {
	// reset
	fileIconPt2 = nullptr;
	fileIconPt2Width = 0;

	title = "Load midi device";
	fileIcon = deluge::hid::display::OLED::midiIcon;
	fileIconPt2 = deluge::hid::display::OLED::midiIconPt2;
	fileIconPt2Width = 1;

	enteredText.clear();

	std::string searchFilename;

	MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();

	// is empty we just start with nothing. currentSlot etc remain set to "zero" from before
	if (midiInstrument->deviceDefinitionFileName.empty()) {
		currentDir = MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER;
	}
	else {
		char const* fullPath = midiInstrument->deviceDefinitionFileName.c_str();

		// locate last occurence of "/" in string
		char* filename = strrchr((char*)fullPath, '/');

		// lengths of full path
		auto fullPathLength = strlen(fullPath);

		// directory
		char dir[sizeof(char) * fullPathLength + 1];

		memset(dir, 0, sizeof(char) * fullPathLength + 1);
		strncpy(dir, fullPath, fullPathLength - strlen(filename));

		currentDir = dir;
		searchFilename = ++filename;
	}

	if (!searchFilename.empty()) {
		searchFilename.append(".XML");
	}

	return searchFilename;
}
#pragma GCC diagnostic pop

void LoadMidiDeviceDefinitionUI::onBrowserOpened() {
	drawKeys();

	// Runs here so it happens after the listing actually completes, matching the Save* browsers'
	// convention.
	focusRegained();
}

void LoadMidiDeviceDefinitionUI::folderContentsReady(int32_t entryDirection) {
}

void LoadMidiDeviceDefinitionUI::definitionLoaded(Error error) {
	if (error != Error::NONE) {
		display->displayError(error);
		return;
	}
	display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MIDI_DEVICE_LOADED));
	loadMidiDeviceDefinitionUI.close();
}

void LoadMidiDeviceDefinitionUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	// If it's a directory...
	if (currentFileItem->isFolder) {
		// goIntoFolder() dispatches onto the storage owner; failure is handled by the base
		// Browser::onListingFailed() (displayError + close()) once the listing completes.
		goIntoFolder(currentFileItem->filename.c_str());
		return;
	}

	std::string fileName = currentDir.c_str();
	fileName.append("/");
	fileName.append(enteredText.c_str());
	fileName.append(".XML");

	// On the interaction tier: the browser stays open, refusing input, until definitionLoaded() is told how it ended.
	(void)deluge::gui::instrumentLoad.request(
	    {.commit = true,
	     .completion = definitionLoaded,
	     .kind = deluge::gui::InstrumentLoadRequest::Kind::MidiDefinition,
	     .definition = {.path = fileName, .instrument = (MIDIInstrument*)getCurrentOutput()}});
}

ActionResult LoadMidiDeviceDefinitionUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;
	// Refused while a definition is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return ActionResult::DEALT_WITH;
	}

	// Load button
	if (b == LOAD) {
		return mainButtonAction(on);
	}

	else {
		if (on && b == BACK) {
			// don't allow navigation backwards if we're in the default folder
			if (!strcmp(currentDir.c_str(), MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER)) {
				close();
				return ActionResult::DEALT_WITH;
			}
		}

		return LoadUI::buttonAction(b, on, inCardRoutine);
	}
}

ActionResult LoadMidiDeviceDefinitionUI::padAction(int32_t x, int32_t y, int32_t on) {
	// Refused while a definition is in flight: InstrumentLoad closes the browser's gate until it ends.
	if (deluge::gui::instrumentLoad.commitInFlight()) {
		return ActionResult::DEALT_WITH;
	}
	if (x < kDisplayWidth) {
		return LoadUI::padAction(x, y, on);
	}
	else {
		LoadUI::exitAction();
		return ActionResult::DEALT_WITH;
	}
}
