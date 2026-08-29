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

/// @file
/// @brief Why the file-prefix channel's slot had to grow from 4096 bytes to 8192.
///
/// The song preview was the channel's only consumer and `kSongPreviewWindow` is 4096, so the slot
/// was 4096 and an equality `static_assert` on both sides of the ABI recorded that coincidence as
/// though it were a definition. The DX7 cartridge is the second consumer, and it does not fit: a
/// 32-voice bank is `kSysexSize` = 4104 bytes.
///
/// This drives the REAL parser, not a restatement of it, because the interesting question is what
/// `DX7Cartridge::load` does with the truncation rather than what arithmetic says about 4104 > 4096.

#include "CppUTest/TestHarness.h"

#include "storage/DX7Cartridge.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace {

/// A structurally valid 32-voice bank: the six-byte header, 4096 bytes of voice data, the checksum
/// the parser recomputes, and the F7 terminator.
std::vector<std::byte> makeBank() {
	std::vector<std::byte> bank(kSysexSize, std::byte{0});
	std::ranges::copy(kSysexHeader, bank.begin());
	// Something other than zeroes, so the checksum is not trivially satisfied by an all-zero buffer.
	for (std::size_t i = 0; i < 4096; i++) {
		bank[6 + i] = static_cast<std::byte>((i * 7 + 3) & 0x7F);
	}
	bank[4102] = sysexChecksum({&bank[6], 4096});
	bank[4103] = std::byte{0xF7};
	return bank;
}

} // namespace

TEST_GROUP(DxCartridgeWindow){};

/// The whole bank parses, and reports the 32 patches that make it a cartridge rather than a single
/// voice. This is the reference the truncation below is measured against — without it, a truncation
/// that failed for some unrelated reason would look like proof.
TEST(DxCartridgeWindow, aWholeBankParsesAsThirtyTwoPatches) {
	const auto bank = makeBank();
	CHECK_EQUAL(4104u, bank.size());
	DX7Cartridge cartridge;
	auto mutableBank = bank; // load() takes a mutable span; it unpacks in place
	CHECK(cartridge.load(std::span{mutableBank}) == deluge::l10n::String::EMPTY_STRING);
	CHECK_EQUAL(32, cartridge.numPatches());
}

/// The same bank through a 4096-byte window does NOT parse, and this is what the slot growth bought.
///
/// It is not a near miss or a degraded load: the eight bytes a 4096 slot drops include the F7
/// terminator, so the parser never finds the end of the message and refuses the file outright. A
/// cartridge read through the old slot would have put an error on screen for a file that is
/// perfectly good.
TEST(DxCartridgeWindow, theSameBankThroughTheOldFourKWindowDoesNotParse) {
	auto bank = makeBank();
	std::span truncated{bank.data(), 4096};
	DX7Cartridge cartridge;
	const auto error = cartridge.load(truncated);
	CHECK_TEXT(error != deluge::l10n::String::EMPTY_STRING,
	           "a 4096-byte window must not silently accept a 4104-byte bank");
	CHECK(error == deluge::l10n::String::STRING_FOR_DX_ERROR_NO_SYSEX_END);
}
