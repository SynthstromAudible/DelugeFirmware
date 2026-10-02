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

#pragma once

#include <cstdint>

namespace deluge::storage {

/// @brief The smsysex companion protocol's "err" wire code.
///
/// These integer values are the frozen smsysex wire ABI (external hardware/software
/// reads them off the SysEx "err" attribute) and must never change, independent of
/// whatever backend implements file_io.h underneath. They were inherited verbatim from
/// FatFS's `FRESULT` at the point that type left the deluge::io boundary, so the values
/// below are not free to renumber even though the FatFS enum itself is gone.
enum class FsWireStatus : uint8_t {
	Ok = 0,
	DiskErr = 1,
	NotReady = 3,
	NoFile = 4,
	Denied = 7,
	Exist = 8,
	WriteProtected = 10,
	NoFilesystem = 13,
	Timeout = 15,
	Locked = 16,
	NotEnoughCore = 17,
	InvalidParameter = 19,
	// The next two are not produced by toWireFresult()'s deluge::io::Status mapping; smsysex.cpp
	// sets them directly for two smsysex-internal conditions that FatFS's own callers used to
	// signal via the identically-numbered FRESULT constants, so their wire values are equally
	// frozen.
	InvalidObject = 9, // closeFIL(): fid refers to no open file.
	NotEnabled = 12,   // readBlock()/writeBlock(): fid's file isn't open.
};

} // namespace deluge::storage
