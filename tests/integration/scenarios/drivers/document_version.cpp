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

// document_version — the firmware version a document is read as is that document's own: its stamp, or the oldest
// official version when it has none, whatever was read before it.

#include "scenarios/drivers/driver.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"

#include <cstdio>
#include <span>
#include <string_view>

namespace {

using deluge::scenarios::driver::Io;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};
int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		failures++;
		std::fprintf(io.err, "DOCUMENT-VERSION FAIL %s\n", what);
	}
}

/// Opens @p text as a synth preset, after a read left @p before as the version being read.
void openAfter(FirmwareVersion before, std::string_view text) {
	song_firmware_version = before;
	deluge::storage::TreeDocument document;
	const Error error = document.open(std::span{reinterpret_cast<const uint8_t*>(text.data()), text.size()},
	                                  /*isJson=*/false, "sound", "synth");
	check(error == Error::NONE, "the preset opens");
	document.release();
}

constexpr auto kOldestOfficial = FirmwareVersion::official({});
const FirmwareVersion kCommunity = FirmwareVersion::parse("c1.2.1");

int document_version_main(int /*argc*/, char** /*argv*/, Io driverIo) {
	io = driverIo;

	openAfter(kCommunity, R"(<?xml version="1.0" encoding="UTF-8"?><sound mode="subtractive"></sound>)");
	check(song_firmware_version == kOldestOfficial,
	      "an unstamped preset read after a community song reads as the oldest "
	      "official version");

	openAfter(kCommunity, R"(<?xml version="1.0" encoding="UTF-8"?>)"
	                      R"(<firmwareVersion>2.0.0-beta</firmwareVersion><sound mode="subtractive"></sound>)");
	check(song_firmware_version == FirmwareVersion::parse("2.0.0-beta"), "a stamped preset reads as its stamp");

	openAfter(kOldestOfficial, R"(<?xml version="1.0" encoding="UTF-8"?>)"
	                           R"(<firmwareVersion>c1.2.1</firmwareVersion><sound mode="subtractive"></sound>)");
	check(song_firmware_version == kCommunity, "a community preset read after an unstamped one reads as its stamp");

	if (failures == 0) {
		std::fprintf(io.out, "document_version: OK\n");
	}
	return failures ? 1 : 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(document_version, document_version_main)
