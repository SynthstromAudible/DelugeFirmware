/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "storage/settings_file.h"

#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "libdeluge/file_bytes.h"
#include "storage/owner.h"

#include <cstring>

namespace deluge::storage::settings {

namespace {

/// How many settings file operations have begun on the interaction tier. See
/// @ref interactionTierSettingsIo.
int32_t interactionTierSettingsIo_ = 0;

/// Translate the C ABI's `uint32_t`-shaped status byte into the type the callers switch on.
///
/// No `default:`, deliberately: `-Werror=switch` is what catches a fifth status added to the header
/// and not handled here. The trailing return is for a value that is not one of the four at all,
/// which can only come from a corrupted ABI -- reported as `Unknown` rather than asserted, because
/// "ask again" is the one answer that is safe under every interpretation.
SettingsIo fromAbi(uint8_t status) {
	switch (status) {
	case DELUGE_FILE_BYTES_PENDING:
		return SettingsIo::Pending;
	case DELUGE_FILE_BYTES_READY:
		return SettingsIo::Ready;
	case DELUGE_FILE_BYTES_UNKNOWN:
		return SettingsIo::Unknown;
	case DELUGE_FILE_BYTES_FAILED:
		return SettingsIo::Failed;
	}
	return SettingsIo::Unknown;
}

/// Whether `path` is short enough for the channel to key on at all.
///
/// Checked HERE, with a real answer, rather than passed on. The request functions silently drop an
/// over-long path and the collect then answers UNKNOWN forever -- which for a read would be an
/// infinite re-request loop with no error ever shown, and for a write would be a settings change
/// that never lands and never reports. Settings paths are compile-time literals today, so this
/// cannot fire; it is here because the bound is the channel's, not the caller's.
bool keyable(const char* path) {
	return path != nullptr && strlen(path) < DELUGE_FILE_BYTES_MAX_PATH;
}

} // namespace

bool usingSettingsChannel() {
	return !deluge::storage::Owner::on_owner() && deluge_file_bytes_async_active();
}

SettingsIo requestRead(const char* path) {
	if (!keyable(path)) {
		return SettingsIo::Failed;
	}
	deluge_file_bytes_request_read(path);
	return SettingsIo::Pending;
}

SettingsIo collectRead(const char* path, char* buf, uint32_t cap, uint32_t* outLen, DelugeStatus* outStatus) {
	if (!keyable(path) || buf == nullptr || outLen == nullptr || outStatus == nullptr) {
		return SettingsIo::Failed;
	}
	// The cast is the identity on the wire: the channel carries bytes, and the deserialiser wants
	// them as `char`. Nothing here interprets them.
	return fromAbi(deluge_file_bytes_collect_read(path, reinterpret_cast<uint8_t*>(buf), cap, outLen, outStatus));
}

SettingsIo requestWrite(const char* path, const char* bytes, uint32_t len) {
	if (!keyable(path) || bytes == nullptr || len > DELUGE_FILE_BYTES_MAX_BYTES) {
		// Terminal, not a retry. A payload too large for the channel will be too large on the next
		// tick too, and a caller that retried it would spin forever writing nothing.
		return SettingsIo::Failed;
	}
	if (!deluge_file_bytes_request_write(path, reinterpret_cast<const uint8_t*>(bytes), len)) {
		// REFUSED because the channel is busy: nothing was queued and nothing was written. `Unknown`
		// is the documented "ask again", and the caller must still be holding its intent.
		return SettingsIo::Unknown;
	}
	return SettingsIo::Pending;
}

SettingsIo collectWrite(const char* path, DelugeStatus* outStatus) {
	if (!keyable(path) || outStatus == nullptr) {
		return SettingsIo::Failed;
	}
	return fromAbi(deluge_file_bytes_collect_write(path, outStatus));
}

SettingsIo requestRename(const char* from, const char* to) {
	if (!keyable(from) || !keyable(to)) {
		return SettingsIo::Failed;
	}
	if (!deluge_file_bytes_request_rename(from, to)) {
		return SettingsIo::Unknown;
	}
	return SettingsIo::Pending;
}

SettingsIo collectRename(const char* to, DelugeStatus* outStatus) {
	if (!keyable(to) || outStatus == nullptr) {
		return SettingsIo::Failed;
	}
	return fromAbi(deluge_file_bytes_collect_rename(to, outStatus));
}

int32_t interactionTierSettingsIo() {
	return interactionTierSettingsIo_;
}

void noteSettingsIoTier() {
	if (!deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()) {
		// Counted as well as frozen, for the reason `SaveUI::noteSaveTier()` gives: the count is what
		// lets the gate report the number BEFORE the conversion, which is the evidence that the
		// conversion did something. The freeze is what keeps it from coming back.
		interactionTierSettingsIo_++;
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("TIR6");
		}
	}
}

} // namespace deluge::storage::settings
