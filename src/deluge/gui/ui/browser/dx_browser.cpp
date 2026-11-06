
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
#include "gui/ui/browser/dx_cartridge_read.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include <string>

using namespace deluge::gui;
using deluge::gui::ui::DxCartridgeRead;
using deluge::gui::ui::dxCartridgeRead;

namespace {
/// After a cartridge loaded: enter its submenu, then close the browser. Both arms end here.
///
/// The close has to come after the load result is known, and the order is upstream #4866 (DX7 browser
/// "back" navigation) rather than a preference. DxBrowseMenu::beginSession sets
/// soundEditor.shouldGoUpOneLevelOnBegin = true before opening this browser, and closing it fires
/// SoundEditor::focusRegained(), which acts on that flag and pops a level. So a close that happens
/// before we know whether the cartridge loaded has already popped us back to the DX source menu by
/// the time enterSubmenu() runs -- and "back" from the cartridge then lands there instead of here.
/// Clearing the flag first, on the success path only, is what suppresses that pop; a failed load
/// still wants it, and still gets it, because it leaves the browser open.
void enterLoadedCartridge() {
	soundEditor.enterSubmenu(&menu_item::dxCartridge);
	soundEditor.shouldGoUpOneLevelOnBegin = false;
	dxBrowser.close();
}
} // namespace

DxSyxBrowser::DxSyxBrowser() {
	fileIcon = deluge::hid::display::OLED::waveIcon;
	title = "DX7 syx files";
	shouldWrapFolderContents = false;
}

static char const* allowedFileExtensionsSyx[] = {"SYX", NULL};
bool DxSyxBrowser::opened() {
	// A read the ported browser left in flight, when the guest closed under it, is not this browser's to collect.
	dxCartridgeRead().cancel();

	bool success = Browser::opened();
	if (!success)
		return false;

	allowedFileExtensions = allowedFileExtensionsSyx;

	allowFoldersSharingNameWithFile = true;
	outputTypeToLoad = OutputType::NONE;
	qwertyVisible = false;

	fileIndexSelected = 0;

	// No inline initSD() pre-flight — see the comment in SlotBrowser::beginSlotSession(). This runs on
	// the interaction tier, where mounting is card I/O it may not do. The listing checks the card
	// itself -- inline before boot, through the directory channel after it -- and reports real
	// failures via Browser::onListingFailed().

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
	std::string path{listing.dir()};
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

		// goIntoFolder() begins a listing that graphicsRoutine() runs over later ticks; failure is handled by
		// the base Browser::onListingFailed() (displayError + close()) once the listing completes.
		goIntoFolder(filenameChars);
	}
	else {
		// TODO: c.f. slotbrowser, we might just be able to pass a file pointer to the FAT loader
		std::string path = getCurrentFilePath();

		// Neither outcome closes here: the browser has to still be open when the read ends, so a failure leaves it up
		// and a success can clear shouldGoUpOneLevelOnBegin before it closes (upstream #4866, see
		// enterLoadedCartridge()). An inline read, before boot finishes, ends at once.
		if (dxCartridgeRead().start(std::move(path)) == DxCartridgeRead::Status::Loaded) {
			enterLoadedCartridge();
		}
	}
}

void DxSyxBrowser::close() {
	// Cancel a read still in flight, for the same reason Browser::close() cancels an in-flight listing: the browser
	// stays up while the read runs, so the read can outlive it. Without this, re-entering the browser collects the
	// stale path's bytes and jumps into the cartridge submenu for a file the user never chose.
	dxCartridgeRead().cancel();
	Browser::close();
}

void DxSyxBrowser::browserGraphicsRoutine() {
	// A failed read leaves the browser open and keeps shouldGoUpOneLevelOnBegin, so back still returns to the DX
	// source menu. See enterLoadedCartridge().
	if (dxCartridgeRead().poll() == DxCartridgeRead::Status::Loaded) {
		enterLoadedCartridge();
	}
}

bool DxSyxBrowser::cartridgeLoadInFlight() {
	return dxCartridgeRead().busy();
}

int32_t DxSyxBrowser::cartridgeGiveUps() {
	return dxCartridgeRead().give_ups();
}

PLACE_SDRAM_BSS DxSyxBrowser dxBrowser{};
