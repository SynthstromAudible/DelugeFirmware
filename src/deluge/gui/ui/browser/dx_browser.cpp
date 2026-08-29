
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
#include "libdeluge/file_prefix.h"
#include "storage/owner.h"
#include <array>
#include <span>
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

/// Path of the cartridge file to load; populated just before dispatch, read back by the op or by the
/// per-tick collect.
std::string g_cartridgeLoadPath;

/// Where a prefix-channel cartridge read lands.
///
/// `deluge_file_prefix_collect` requires an `out_cap` of at least DELUGE_FILE_PREFIX_CAPACITY
/// whatever the caller asked for -- a shorter buffer is a caller bug that silently yields an empty
/// read in a release build -- so this is sized by the capacity, not by the 8192 the request names.
/// The slot was grown to 8192 for exactly this consumer: a DX7 32-voice bank is 4104 bytes and the
/// old 4096 slot truncated it by 8, which is enough to fail its checksum.
std::array<uint8_t, DELUGE_FILE_PREFIX_CAPACITY> g_cartridgeBuffer;
/// True while a dispatched load is in flight. Guards against a second dispatch stomping
/// g_cartridgeLoadPath before the first op reads it (defensive: enterKeyPress already close()s
/// the browser before dispatching, so a second call can't normally land while one's in flight).
bool g_cartridgeLoadInFlight = false;

/// Owner-op trampoline for the SYNCHRONOUS arm — unchanged by this rung, deliberately.
///
/// The browser was already closed by enterKeyPress before this was dispatched, and it stays that
/// way: this runs on the storage owner (the fiber on device), and calling close() from here would be
/// a UI mutation from the storage tier, which is precisely what the tier model forbids and what this
/// arm has never done.
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
	beginListing({.action = ListingAction::Open, .direction = 1, .filenameToStartAt = "", .defaultDir = "DX7"});

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

		// The two arms close at different moments, and that is inherent rather than an oversight.
		//
		// SYNCHRONOUS: close here, before dispatching, exactly as before this rung. The op that
		// follows runs on the storage owner and must not touch the UI.
		//
		// ASYNCHRONOUS: do NOT close here. The read spans several ticks and the collect that ends it
		// runs on the interaction tier, where closing is legal — so the browser stays up for the
		// duration and closes on BOTH the success and the failure path once the read has actually
		// happened. Closing at dispatch time instead would leave those ticks with no UI and then pop
		// a read error over whatever had taken the browser's place.
		const bool async = deluge_file_prefix_async_active();
		if (!async) {
			close();
		}
		if (!path.empty() && !g_cartridgeLoadInFlight) {
			g_cartridgeLoadPath = std::move(path);
			g_cartridgeLoadInFlight = true;
			if (async) {
				// The request is issued by the collect rather than here: collect answers UNKNOWN for a
				// path it has not been told about, and "request it (again)" is what that means, so the
				// opening ask and every re-ask are one code path.
			}
			else if (!deluge::storage::Owner::run(&runCartridgeLoadOp, nullptr)) {
				g_cartridgeLoadInFlight = false; // dispatch dropped -> op won't run; drop the request
			}
		}

		// dx7ui.openFile(path.get());
	}
}

void DxSyxBrowser::browserGraphicsRoutine() {
	if (!g_cartridgeLoadInFlight || !deluge_file_prefix_async_active()) {
		return;
	}

	uint32_t length = 0;
	const uint8_t status = deluge_file_prefix_collect(g_cartridgeLoadPath.c_str(), g_cartridgeBuffer.data(),
	                                                  static_cast<uint32_t>(g_cartridgeBuffer.size()), &length);
	switch (status) {
	case DELUGE_FILE_PREFIX_PENDING:
		return; // being read; ask again next tick and do NOT re-request
	case DELUGE_FILE_PREFIX_UNKNOWN:
		// "Request it (again)" -- the documented meaning, and where the FIRST ask for this path is
		// issued. Never a failure to report.
		deluge_file_prefix_request(g_cartridgeLoadPath.c_str(), DELUGE_FILE_PREFIX_CAPACITY);
		return;
	case DELUGE_FILE_PREFIX_FAILED:
		// Terminal. The read did not happen, so there is nothing to parse; show the same popup the
		// synchronous arm shows for a file it could not read, then close as that arm does.
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DX_ERROR_READ_ERROR));
		g_cartridgeLoadInFlight = false;
		close();
		return;
	default: { // READY
		const std::span bytes{reinterpret_cast<const std::byte*>(g_cartridgeBuffer.data()), length};
		const bool loaded = menu_item::dxCartridge.tryLoadFromBuffer(bytes);
		g_cartridgeLoadInFlight = false;
		// close() on BOTH paths, and before the submenu: the synchronous arm's browser is already gone
		// by the time its op runs, so a failed async load that left the browser up would be the one
		// visible difference between the two.
		close();
		if (loaded) {
			soundEditor.enterSubmenu(&menu_item::dxCartridge);
		}
		return;
	}
	}
}

DxSyxBrowser dxBrowser{};
