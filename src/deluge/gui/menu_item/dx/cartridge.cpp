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
#include "processing/source.h"
#include "storage/DX7Cartridge.h"
#include "util/container/static_vector.hpp"

static bool openFile(const char* path, DX7Cartridge* data) {
	bool didLoad = false;
	const int xx = 4104;

	FILINFO fno;
	int result = f_stat(path, &fno);
	FSIZE_t fileSize = fno.fsize;
	if (fileSize < xx) {
		display->displayPopup("too small!", 3);
	}

	FIL file;
	// Open the file
	result = f_open(&file, path, FA_READ);
	if (result != FR_OK) {
		display->displayPopup("read error 1", 3);
		return false;
	}

	int status;
	UINT numBytesRead;
	int readsize = std::min((int)fileSize, 8192);
	auto buffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(readsize);
	if (!buffer) {
		display->displayPopup("out of memory", 3);
		goto close;
	}
	result = f_read(&file, buffer, fileSize, &numBytesRead);
	if (numBytesRead < xx) {
		display->displayPopup("read error 2", 3);
		goto free;
	}

	status = data->load(buffer, numBytesRead);
	if (status) { // TODO: return error string :P
		display->displayPopup("load error", 3);
	}
	else {
		didLoad = true;
	}

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
}

void DxCartridge::drawPixelsForOled() {
	if (pd == nullptr) {
		return;
	}
	char names[32][11];
	pd->getProgramNames(names);

	static_vector<std::string_view, 32> itemNames = {};
	for (int i = 0; i < 32; i++) {
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

	return openFile(path, pd);
}

void DxCartridge::selectEncoderAction(int32_t offset) {
	int32_t newValue = currentValue + offset;
	int32_t numValues = 32;

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
} // namespace deluge::gui::menu_item
