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
#include "gui/menu_item/setting_edit.h"
#include "gui/ui/browser/dx_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "io/file.hpp"
#include "memory/allocate_unique.h"
#include "memory/sdram_allocator.h"
#include "model/edits/setting_edit.h"
#include "model/song/song.h"
#include "processing/dx7/dx_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/DX7Cartridge.h"
#include "util/functions.h"
#include "util/try.h"
#include <etl/vector.h>
#include <memory>

namespace deluge::gui::menu_item {

DxCartridge dxCartridge{l10n::String::STRING_FOR_DX_CARTRIDGE};

void DxCartridge::beginSession(MenuItem* navigatedBackwardFrom) {
	loadPatch();
	readValueAgain();
}

// Unpacks the currently-selected program into the live DX patch and hard-cuts any sounding voices.
// Only call this when the selected patch actually changes - NOT from a plain redraw, otherwise an
// audition note gets cut every time the menu refreshes (e.g. mod-encoder press, display change).
void DxCartridge::loadPatch() {
	if (pd == nullptr) {
		return;
	}
	loadDxProgram(*soundEditor.currentSound, soundEditor.currentSourceIndex, *pd, currentValue);
}

void DxCartridge::readValueAgain() {
	if (pd == nullptr) {
		return;
	}
	renderUIsForOled();
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

bool DxCartridge::tryLoad(std::string_view path) {
	currentValue = 0;
	scrollPos = 0;
	return deluge::gui::loadDxCartridgeFile(path);
}

bool DxCartridge::tryLoadFromBuffer(std::span<std::byte> bytes) {
	currentValue = 0;
	scrollPos = 0;
	return deluge::gui::loadDxCartridgeBytes(bytes);
}

void DxCartridge::selectEncoderAction(int32_t offset) {
	int32_t numValues = pd->numPatches();

	int32_t newValue = std::clamp<int32_t>(currentValue + offset, 0, numValues - 1);

	// if no change, just exit
	if (newValue == currentValue) {
		return;
	}

	currentValue = newValue;

	scrollPos = std::clamp<int>(newValue - 1, 0, numValues - kOLEDMenuNumOptionsVisible);

	loadPatch();
	readValueAgain(); // redraw
}

MenuItem* DxCartridge::selectButtonPress() {
	soundEditor.exitCompletely();
	return NO_NAVIGATION;
}

} // namespace deluge::gui::menu_item
