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

#include "gui/ui/save/save_kit_row_ui.h"
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
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include <cstring>

using namespace deluge;

SaveKitRowUI saveKitRowUI{};

SaveKitRowUI::SaveKitRowUI() {
	outputTypeToLoad = OutputType::SYNTH;
}

bool SaveKitRowUI::opened() {
	// safe since we can't open this without being on a kit
	// Must set this before calling SaveUI::opened(), which uses this to work out folder name

	bool success = SaveUI::opened();
	if (!success) {                      // In this case, an error will have already displayed.
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads.
		return false;
	}

	enteredText = soundDrumToSave->drumName;
	enteredTextEditPos = enteredText.size();
	currentFolderIsEmpty = false;

	char const* defaultDir = getInstrumentFolder(outputTypeToLoad);

	currentDir = soundDrumToSave->path;
	if (currentDir.empty()) { // Would this even be able to happen?
		currentDir = defaultDir;
	}

	fileIcon = deluge::hid::display::OLED::synthIcon;
	title = "Save synth preset";

	filePrefix = "SYNT";

	// The listing dispatches asynchronously; return optimistically here. onBrowserOpened() runs
	// the blinkLed+focusRegained tail once the listing succeeds. Failure goes through the base
	// Browser::onListingFailed() (displayError + close()).
	beginListing(
	    {.action = ListingAction::Open, .direction = 0, .filenameToStartAt = enteredText, .defaultDir = defaultDir});

	return true;
}

void SaveKitRowUI::onBrowserOpened() {
	indicator_leds::blinkLed(IndicatorLED::SYNTH);
	focusRegained();
}

bool SaveKitRowUI::beforeStagedWrite(const deluge::storage::SaveRequest& request) {
	// We can't save into this slot if another Instrument in this Song already uses it
	if (currentSong->getInstrumentFromPresetSlot(outputTypeToLoad, 0, 0, request.enteredText.c_str(),
	                                             request.dirPath.c_str(), false)) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_SAME_NAME));
		display->removeWorkingAnimation();
		return false;
	}
	// Alright, we know the new slot isn't used by an Instrument in the Song, but there may be an Instrument lurking
	// in memory with that slot, which we need to just delete
	currentSong->deleteHibernatingInstrumentWithSlot(outputTypeToLoad, request.enteredText.c_str());
	return true;
}

void SaveKitRowUI::afterStagedSave(const deluge::storage::SaveRequest& request) {
	display->removeWorkingAnimation();
	// Give the Instrument in memory its new slot
	soundDrumToSave->drumName = request.enteredText.c_str();
	soundDrumToSave->path = request.dirPath.c_str();
	// There's now no chance that we saved over a preset that's already in use in the song, because we didn't allow the
	// user to select such a slot
	display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PRESET_SAVED));
	close();
}

bool SaveKitRowUI::serializeForSave(XMLSerializer& into) {
	// `into` IS the shared serialiser this writes through: writeToFileAsInstrument takes none and
	// reaches GetSerializer() itself, which is exactly why the staged document is built there.
	(void)into;
	soundDrumToSave->writeToFileAsInstrument(false, paramManagerToSave);
	return true;
}
