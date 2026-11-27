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

/// @file
/// @brief Where a sample's sidecar lives on the card.

#include <array>
#include <cstdint>
#include <string_view>

namespace deluge::storage {

/// @brief The folder every sidecar lives under, card-relative as the byte channel takes paths.
inline constexpr std::string_view kSidecarDir = "DELUGE/PEAKS";

/// @brief Which of a key's two files.
enum class SidecarFile : uint8_t {
	PK,  ///< the sidecar
	TMP, ///< a write in progress, renamed to PK once whole
};

/// @brief A sidecar's card path: `DELUGE/PEAKS/<top byte>/<first cluster>.<PK|TMP>`, hex in upper case, 8.3 names
///        only so a lookup is one short-name match in a directory of at most 1/256 of them. NUL-terminated.
/// Always inlined, so it runs from wherever its caller does.
[[nodiscard, gnu::always_inline]] constexpr std::array<char, 32> sidecarPath(uint32_t firstCluster, SidecarFile file) {
	constexpr std::string_view kHex = "0123456789ABCDEF";
	std::array<char, 32> path{};
	size_t at = 0;
	const auto put = [&](char c) { path[at++] = c; };
	for (const char c : kSidecarDir) {
		put(c);
	}
	put('/');
	put(kHex[(firstCluster >> 28) & 15]);
	put(kHex[(firstCluster >> 24) & 15]);
	put('/');
	for (int32_t shift = 28; shift >= 0; shift -= 4) {
		put(kHex[(firstCluster >> shift) & 15]);
	}
	for (const char c : file == SidecarFile::PK ? std::string_view{".PK"} : std::string_view{".TMP"}) {
		put(c);
	}
	return path;
}

} // namespace deluge::storage
