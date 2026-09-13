
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
#include "storage/byte_channel.h" // noteIoTier, for the inline cartridge read
#include "storage/owner.h"
#include <array>
#include <span>
#include <string>

using namespace deluge::gui;

namespace {
// --- DX cartridge load ------------------------------------------------------------------------
//
// A cartridge's bytes come through the file prefix channel, collected on the browser's tick, and are
// parsed with DxCartridge::tryLoadFromBuffer(); whether it parsed decides whether we enter the cartridge
// submenu. On the storage owner, or before it starts, DxCartridge::tryLoad() reads and parses the file
// inline instead. Single caller (DxSyxBrowser::enterKeyPress, below): a file-static path buffer and
// single-flight guard are enough.

/// Path of the cartridge file being loaded, read back by the per-tick collect.
std::string g_cartridgeLoadPath;

/// Where a prefix-channel cartridge read lands.
///
/// `deluge_file_prefix_collect` requires an `out_cap` of at least DELUGE_FILE_PREFIX_CAPACITY
/// whatever the caller asked for -- a shorter buffer is a caller bug that silently yields an empty
/// read in a release build -- so this is sized by the capacity, not by the 8192 the request names.
/// The slot was grown to 8192 for exactly this consumer: a DX7 32-voice bank is 4104 bytes and the
/// old 4096 slot truncated it by 8, which is enough to fail its checksum.
/// PLACE_SDRAM_BSS, like its sibling songPreviewPrefix (load_song_ui.cpp) and for the same reason:
/// 8 KiB is worth having out of internal RAM, this is touched only by the browser tier and never by
/// the audio path, and on a build where the async arm never runs it would otherwise cost that RAM
/// unconditionally for nothing.
PLACE_SDRAM_BSS std::array<uint8_t, DELUGE_FILE_PREFIX_CAPACITY> g_cartridgeBuffer;
/// True while a load waits on the channel. Guards against a second Enter stomping
/// g_cartridgeLoadPath before the collect reads it: the browser stays open until the load
/// succeeds, so a second enterKeyPress can land while one is already in flight.
bool g_cartridgeLoadInFlight = false;
/// Consecutive ticks the load in flight has found its read unanswered (UNKNOWN or PENDING).
int32_t g_cartridgeWaitTicks = 0;
/// How many unanswered ticks a cartridge load is given: the bound every channel consumer on this ladder uses.
constexpr int32_t kMaxCartridgeWaitTicks = 600;
/// Loads that gave up at the bound. See DxSyxBrowser::cartridgeGiveUps().
int32_t g_cartridgeGiveUps = 0;

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

		// NEITHER arm closes here: the browser has to still be open when the load result comes back, so a
		// failure leaves it up and a success can clear shouldGoUpOneLevelOnBegin before it closes
		// (upstream #4866, see enterLoadedCartridge()).
		if (path.empty() || g_cartridgeLoadInFlight) {
			return;
		}
		if (deluge::storage::Owner::on_owner() || !deluge::storage::Owner::started()) {
			// Inline: on the owner a blocking read belongs here, and before it starts nothing would answer
			// the channel. Off the owner after boot this would hold the interaction tier for the whole read,
			// which is why it is noted as a tier violation, and why it is unreachable there. tryLoad() shows
			// its own popups on failure.
			deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Browser);
			if (menu_item::dxCartridge.tryLoad(path)) {
				enterLoadedCartridge();
			}
			return;
		}
		// The request is issued by the collect in browserGraphicsRoutine() rather than here: collect answers
		// UNKNOWN for a path it has not been told about, and "request it (again)" is what that means, so the
		// opening ask and every re-ask are one code path.
		g_cartridgeLoadPath = std::move(path);
		g_cartridgeLoadInFlight = true;
		g_cartridgeWaitTicks = 0;
	}
}

void DxSyxBrowser::close() {
	// Cancel a load still waiting on the channel, for the same reason Browser::close() cancels an
	// in-flight listing: the browser stays up while the read runs, so the read can outlive it.
	//
	// Without this the guard stays armed forever. Every later enterKeyPress fails its
	// !g_cartridgeLoadInFlight check and silently does nothing, and re-entering the browser collects
	// the stale path's bytes and jumps into the cartridge submenu for a file the user never chose.
	g_cartridgeLoadInFlight = false;
	g_cartridgeWaitTicks = 0;
	Browser::close();
}

void DxSyxBrowser::browserGraphicsRoutine() {
	if (!g_cartridgeLoadInFlight) {
		return;
	}

	uint32_t length = 0;
	const uint8_t status = deluge_file_prefix_collect(g_cartridgeLoadPath.c_str(), g_cartridgeBuffer.data(),
	                                                  static_cast<uint32_t>(g_cartridgeBuffer.size()), &length);
	if (status == DELUGE_FILE_PREFIX_PENDING || status == DELUGE_FILE_PREFIX_UNKNOWN) {
		if (++g_cartridgeWaitTicks >= kMaxCartridgeWaitTicks) {
			// The channel has left this read unanswered for the whole bound. Give up as a read that failed does: the
			// popup, and the browser left up so another file can be picked. Nothing to clean up: the channel copied
			// the path, and a late answer is displaced by the next publish.
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DX_ERROR_READ_ERROR));
			g_cartridgeLoadInFlight = false;
			g_cartridgeWaitTicks = 0;
			g_cartridgeGiveUps++;
			return;
		}
	}
	else {
		g_cartridgeWaitTicks = 0; // answered: READY or FAILED
	}
	switch (status) {
	case DELUGE_FILE_PREFIX_PENDING:
		return; // being read; ask again next tick and do NOT re-request
	case DELUGE_FILE_PREFIX_UNKNOWN:
		// "Request it (again)" -- the documented meaning, and where the FIRST ask for this path is
		// issued. Never a failure to report.
		deluge_file_prefix_request(g_cartridgeLoadPath.c_str(), DELUGE_FILE_PREFIX_CAPACITY);
		return;
	case DELUGE_FILE_PREFIX_FAILED:
		// Terminal. The read did not happen, so there is nothing to parse; show the popup tryLoad()
		// shows for a file it could not read, and leave the browser up, so you can pick another file.
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DX_ERROR_READ_ERROR));
		g_cartridgeLoadInFlight = false;
		return;
	default: { // READY
		const std::span bytes{reinterpret_cast<std::byte*>(g_cartridgeBuffer.data()), length};
		const bool loaded = menu_item::dxCartridge.tryLoadFromBuffer(bytes);
		g_cartridgeLoadInFlight = false;
		// A failed parse leaves the browser open and keeps shouldGoUpOneLevelOnBegin, so back still
		// returns to the DX source menu. See enterLoadedCartridge().
		if (loaded) {
			enterLoadedCartridge();
		}
		return;
	}
	}
}

bool DxSyxBrowser::cartridgeLoadInFlight() {
	return g_cartridgeLoadInFlight;
}

int32_t DxSyxBrowser::cartridgeGiveUps() {
	return g_cartridgeGiveUps;
}

DxSyxBrowser dxBrowser{};
