
/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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

#include "gui/ui/browser/dx_cartridge_read.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/dx/cartridge.h"
#include "hid/display/display.h"
#include "storage/byte_channel.h" // noteIoTier, for the inline read
#include "util/boot.h"
#include <array>
#include <span>
#include <utility>

namespace deluge::gui::ui {

namespace {

/// Where a channel read lands.
///
/// `deluge_file_prefix_collect` requires an `out_cap` of at least DELUGE_FILE_PREFIX_CAPACITY whatever the caller
/// asked for -- a shorter buffer is a caller bug that silently yields an empty read in a release build -- so this is
/// sized by the capacity, not by the 8192 the request names. A DX7 32-voice bank is 4104 bytes, so the slot has to be
/// larger than 4096: eight bytes short is enough to fail its checksum.
/// PLACE_SDRAM_BSS, like its sibling songPreviewPrefix (load_song_ui.cpp) and for the same reason: 8 KiB is worth
/// having out of internal RAM, and only the browser tier touches it.
PLACE_SDRAM_BSS std::array<uint8_t, DELUGE_FILE_PREFIX_CAPACITY> g_buffer;

void readErrorPopup() {
	display->displayPopup(l10n::get(l10n::String::STRING_FOR_DX_ERROR_READ_ERROR));
}

bool libdelugeBooted() {
	return boot::finished();
}

} // namespace

DxCartridgeRead::Channel DxCartridgeRead::libdelugeChannel() {
	return {.collect = &deluge_file_prefix_collect, .request = &deluge_file_prefix_request, .booted = &libdelugeBooted};
}

DxCartridgeRead::DxCartridgeRead(Channel channel, io::StallBound::Clock clock)
    : channel_{channel}, wait_{kStallUs, clock} {
}

DxCartridgeRead::Status DxCartridgeRead::start(std::string path) {
	if (path.empty() || in_flight_) {
		return Status::Idle;
	}
	if (!channel_.booted()) {
		// Inline during boot, when nothing would answer the channel. After boot this would hold the interaction tier
		// for the whole read, which is why it is noted as a tier violation, and why it is unreachable there.
		// tryLoad() shows its own popups on failure.
		storage::bytes::noteIoTier(storage::bytes::IoTier::Browser);
		return menu_item::dxCartridge.tryLoad(path) ? Status::Loaded : Status::Failed;
	}
	path_ = std::move(path);
	in_flight_ = true;
	wait_.reset();
	return Status::Pending;
}

void DxCartridgeRead::cancel() {
	in_flight_ = false;
	wait_.reset();
}

DxCartridgeRead::Status DxCartridgeRead::poll() {
	if (!in_flight_) {
		return Status::Idle;
	}

	uint32_t length = 0;
	const DelugeChannelStatus status =
	    channel_.collect(path_.c_str(), g_buffer.data(), static_cast<uint32_t>(g_buffer.size()), &length);
	if (status == DELUGE_CHANNEL_PENDING || status == DELUGE_CHANNEL_UNKNOWN) {
		if (wait_.tick()) {
			// The channel has left this read unanswered for the whole bound. Give up as a read that failed does.
			// Nothing to clean up: the channel copied the path, and a late answer is displaced by the next request.
			readErrorPopup();
			cancel();
			give_ups_++;
			return Status::Failed;
		}
	}
	else {
		wait_.reset(); // answered: READY or FAILED
	}
	switch (status) {
	case DELUGE_CHANNEL_PENDING:
		return Status::Pending; // being read; ask again next tick and do NOT re-request
	case DELUGE_CHANNEL_UNKNOWN:
		// "Request it (again)" -- the documented meaning, and where the first ask for this path is issued.
		channel_.request(path_.c_str(), DELUGE_FILE_PREFIX_CAPACITY);
		return Status::Pending;
	case DELUGE_CHANNEL_FAILED:
		// Terminal. The read did not happen, so there is nothing to parse: the popup tryLoad() shows for a file it
		// could not read.
		readErrorPopup();
		in_flight_ = false;
		return Status::Failed;
	default: { // READY
		in_flight_ = false;
		const std::span bytes{reinterpret_cast<std::byte*>(g_buffer.data()), length};
		return menu_item::dxCartridge.tryLoadFromBuffer(bytes) ? Status::Loaded : Status::Failed;
	}
	}
}

DxCartridgeRead& dxCartridgeRead() {
	static DxCartridgeRead read;
	return read;
}

} // namespace deluge::gui::ui
