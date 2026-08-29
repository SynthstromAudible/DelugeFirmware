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
#include "io/file.hpp"
#include "memory/allocate_unique.h"
#include "memory/sdram_allocator.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/DX7Cartridge.h"
#include "util/functions.h"
#include "util/try.h"
#include <etl/vector.h>
#include <memory>

static bool parseCartridge(std::span<std::byte> readbuffer, DX7Cartridge* data);

static bool openFile(std::string_view path, DX7Cartridge* data) {
	using namespace deluge;
	using enum deluge::l10n::String;
	constexpr size_t minSize = kSmallSysexSize;

	io::File file = D_TRY_CATCH_MOVE(io::File::open(path, DELUGE_FILE_READ), error, {
		return false; // fail quickly if the file doesn't exist / can't open
	});

	uint32_t filesize = D_TRY_CATCH(file.size(), error, { return false; });
	if (filesize < minSize) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	l10n::String error = EMPTY_STRING;
	int readsize = std::min((int)filesize, 8192);

	std::unique_ptr buffer = D_TRY_CATCH_MOVE((allocate_unique<std::byte, memory::sdram_allocator>(readsize)), error, {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_READ_ERROR));
		return false;
	});

	std::span<std::byte> readbuffer =
	    D_TRY_CATCH(file.read(std::span<std::byte>(buffer.get(), static_cast<size_t>(readsize))), error, {
		    return false; //
	    });

	return parseCartridge(readbuffer, data);
}

/// Everything from the size check onward: no file, no card, just bytes. Both arms end here, so the
/// popups and the checksum-tolerance rule have exactly one home.
static bool parseCartridge(std::span<std::byte> readbuffer, DX7Cartridge* data) {
	using namespace deluge;
	using enum deluge::l10n::String;
	constexpr size_t minSize = kSmallSysexSize;

	if (readbuffer.size() < minSize) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	l10n::String error = data->load(readbuffer);
	if (error != EMPTY_STRING) {
		// Allow loading to continue for checksum errors, but fail for other errors
		if (error != deluge::l10n::String::STRING_FOR_DX_ERROR_CHECKSUM_FAIL) {
			display->displayPopup(l10n::get(error), 3);
			return false;
		}
	}

	return true;
}
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

	DxPatch* patch = soundEditor.currentSource->ensureDxPatch();
	pd->unpackProgram(patch->params, currentValue);
	soundEditor.currentSound->killAllVoices();
	Instrument* instrument = getCurrentInstrument();
	if (instrument->type == OutputType::SYNTH && !instrument->mightExistOnCard) {
		char name[11];
		pd->getProgramName(currentValue, name);
		if (name[0] != 0) {
			instrument->name = name;
		}
	}
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
	if (pd == nullptr) {
		pd = new DX7Cartridge();
	}
	currentValue = 0;
	scrollPos = 0;

	return openFile(path, pd);
}

bool DxCartridge::tryLoadFromBuffer(std::span<const std::byte> bytes) {
	if (pd == nullptr) {
		pd = new DX7Cartridge();
	}
	currentValue = 0;
	scrollPos = 0;

	// DX7Cartridge::load takes a mutable span (it unpacks in place), and the prefix channel's buffer
	// is the caller's to hand over, so const is dropped here rather than by copying 8 KB.
	return parseCartridge(std::span<std::byte>{const_cast<std::byte*>(bytes.data()), bytes.size()}, pd);
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
	return nullptr;
}

} // namespace deluge::gui::menu_item
