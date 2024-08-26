/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "cartridge.h"
#include "dsp/dx/engine.h"
#include "gui/ui/browser/dx_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/DX7Cartridge.h"
#include <etl/vector.h>

static bool openFile(const char* path, DX7Cartridge* data) {
	using deluge::l10n::String;
	bool didLoad = false;
	const int minSize = SMALL_SYSEX_SIZE;

	FILINFO fno;
	int result = f_stat(path, &fno);
	FSIZE_t fileSize = fno.fsize;
	if (fileSize < minSize) {
		display->displayPopup(get(String::STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	FIL file;
	// Open the file
	result = f_open(&file, path, FA_READ);
	if (result != FR_OK) {
		display->displayPopup(get(String::STRING_FOR_DX_ERROR_READ_ERROR));
		return false;
	}

	String error = String::EMPTY_STRING;
	UINT numBytesRead;
	int readsize = std::min((int)fileSize, 8192);
	auto buffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(readsize);
	if (!buffer) {
		display->displayPopup(get(String::STRING_FOR_DX_ERROR_READ_ERROR));
		goto close;
	}
	result = f_read(&file, buffer, fileSize, &numBytesRead);
	if (numBytesRead < minSize) {
		display->displayPopup(get(String::STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
		goto free;
	}

	error = data->load(buffer, numBytesRead);
	if (error != String::EMPTY_STRING) {
		// Allow loading to continue for checksum errors, but fail for other errors
		if (error != deluge::l10n::String::STRING_FOR_DX_ERROR_CHECKSUM_FAIL) {
			display->displayPopup(l10n::get(error), 3);
			return false;
		}
	}

	didLoad = true;

free:
	GeneralMemoryAllocator::get().dealloc(buffer);
close:
	f_close(&file);
	return didLoad;
}
namespace deluge::gui::menu_item {

DxCartridge dxCartridge{l10n::String::STRING_FOR_DX_CARTRIDGE};

void DxCartridge::beginSession(MenuItem* navigatedBackwardFrom) {
	readValueAgain();
}

void DxCartridge::readValueAgain() {
	if (pd == nullptr) {
		return;
	}
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}

	DxPatch* patch = soundEditor.currentSource->ensureDxPatch();
	pd->unpackProgram(patch->params, currentValue);
	soundEditor.currentSound->unassignAllVoices();
	Instrument* instrument = getCurrentInstrument();
	if (instrument->type == OutputType::SYNTH && !instrument->existsOnCard) {
		char name[11];
		pd->getProgramName(currentValue, name);
		if (name[0] != 0) {
			instrument->name.set(name);
		}
	}
}

void DxCartridge::drawPixelsForOled() {
	if (pd == nullptr) {
		return;
	}
	char names[32][11];
	pd->getProgramNames(names);

	etl::vector<std::string_view, 32> itemNames = {};
	for (int i = 0; i < pd->numPatches(); i++) {
		itemNames.push_back(names[i]);
	}
	drawItemsForOled(itemNames, currentValue - scrollPos, scrollPos);
}

void DxCartridge::drawValue() {
	char names[32][11];
	pd->getProgramNames(names);

	display->setScrollingText(names[currentValue]);
}

bool DxCartridge::tryLoad(const char* path) {
	if (pd == nullptr) {
		pd = new DX7Cartridge();
	}
	currentValue = 0;
	scrollPos = 0;

	return openFile(path, pd);
}

void DxCartridge::selectEncoderAction(int32_t offset) {
	int32_t newValue = currentValue + offset;
	int32_t numValues = pd->numPatches();

	if (display->haveOLED()) {
		if (newValue >= numValues || newValue < 0) {
			return;
		}
	}
	else {
		if (newValue >= numValues) {
			newValue %= numValues;
		}
		else if (newValue < 0) {
			newValue = (newValue % numValues + numValues) % numValues;
		}
	}

	currentValue = newValue;

	if (display->haveOLED()) {
		if (currentValue < scrollPos) {
			scrollPos = currentValue;
		}
		else if (currentValue >= scrollPos + kOLEDMenuNumOptionsVisible) {
			scrollPos++;
		}
	}

	readValueAgain();
}

MenuItem* DxCartridge::selectButtonPress() {
	soundEditor.exitCompletely();
	return nullptr;
}

} // namespace deluge::gui::menu_item
