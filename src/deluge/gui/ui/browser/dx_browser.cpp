
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

#include "dx_browser.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/dx/cartridge.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "storage/owner.h"
#include <string>

using namespace deluge::gui;

namespace {
// --- DX cartridge preview dispatch ------------------------------------------------------------
//
// DxCartridge::tryLoad() reads+parses the cartridge file (FatFS open/read); its result gates
// whether we enter the cartridge submenu. That's a preview-style read, not a beginListing(Open)
// browser listing, so it dispatches directly onto the storage owner with the submenu-enter moved
// into the op's success path. Single caller (DxSyxBrowser::enterKeyPress, below): a file-static
// path buffer + single-flight guard are enough — no need for a heap-allocated context.

/// Path of the cartridge file to load; populated just before dispatch, read back by the op.
std::string g_cartridgeLoadPath;
/// True while a dispatched load is in flight. Guards against a second dispatch stomping
/// g_cartridgeLoadPath before the first op reads it (defensive: enterKeyPress already close()s
/// the browser before dispatching, so a second call can't normally land while one's in flight).
bool g_cartridgeLoadInFlight = false;

/// Owner-op trampoline: read+parse the cartridge (tryLoad already shows its own error popups on
/// failure), then on success enter the submenu — the "enter on success" tail for enterKeyPress.
void runCartridgeLoadOp(void*) {
	bool loaded = menu_item::dxCartridge.tryLoad(g_cartridgeLoadPath);
	g_cartridgeLoadInFlight = false;
	if (loaded) {
		soundEditor.enterSubmenu(&menu_item::dxCartridge);
	}
}
} // namespace

DxSyxBrowser::DxSyxBrowser() {
	fileIcon = deluge::hid::display::OLED::waveIcon;
	title = "DX7 syx files";
	shouldWrapFolderContents = false;
}

static char const* allowedFileExtensionsSyx[] = {"SYX", NULL};
bool DxSyxBrowser::opened() {

	bool success = Browser::opened();
	if (!success)
		return false;

	allowedFileExtensions = allowedFileExtensionsSyx;

	allowFoldersSharingNameWithFile = true;
	outputTypeToLoad = OutputType::NONE;
	qwertyVisible = false;

	fileIndexSelected = 0;

	// No inline initSD() pre-flight — see the comment in SlotBrowser::beginSlotSession(). This runs on
	// the interaction tier, where an off-owner initSD() reports Error::SD_CARD for "ask me from the
	// owner", failing the browser on a healthy card. The dispatched listing checks the card on the
	// owner and reports real failures via Browser::onListingFailed().

	currentDir = "DX7";

	// TODO: fill in last used name!
	// The listing now happens async: dispatch it and return optimistically. Failure goes through
	// the base Browser::onListingFailed() (displayError + close()) once the listing completes.
	beginListingOrRetryLater(
	    {.action = ListingAction::Open, .direction = 1, .filenameToStartAt = "", .defaultDir = "DX7"});

	return true;
}

// TODO: this is identical to SampleBrowser, move to parent class?
std::string DxSyxBrowser::getCurrentFilePath() {
	std::string path = currentDir;
	if (!path.empty()) {
		path.append("/");
	}

	FileItem* currentFileItem = getCurrentFileItem();

	path.append(currentFileItem->filename);

	return path;
}

void DxSyxBrowser::enterKeyPress() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	if (currentFileItem->isFolder) {
		// [SIC]
		char const* filenameChars =
		    currentFileItem->filename
		        .c_str(); // Extremely weirdly, if we try to just put this inside the parentheses in the next line,
		                  // it returns an empty string (&nothing). Surely this is a compiler error??

		// goIntoFolder() now dispatches onto the storage owner; failure is handled by the base
		// Browser::onListingFailed() (displayError + close()) once the listing completes.
		goIntoFolder(filenameChars);
	}
	else {
		// TODO: c.f. slotbrowser, we might just be able to pass a file pointer to the FAT loader
		std::string path = getCurrentFilePath();
		close();

		if (!path.empty() && !g_cartridgeLoadInFlight) {
			g_cartridgeLoadPath = std::move(path);
			g_cartridgeLoadInFlight = true;
			if (!deluge::storage::Owner::run(&runCartridgeLoadOp, nullptr)) {
				g_cartridgeLoadInFlight = false; // dispatch dropped -> op won't run; drop the request
			}
		}

		// dx7ui.openFile(path.get());
	}
}

DxSyxBrowser dxBrowser{};
