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
/// @brief When a sample's sidecar is written.

#include <cstdint>

namespace deluge::storage {

/// @brief A failed write is tried again once a session; then the sample is left alone.
inline constexpr uint8_t kMaxSidecarFailures = 2;

/// @brief What the write rule reads about one sample.
struct SidecarProgress {
	bool keyed{false};          ///< its file's identity is known
	bool looked{false};         ///< the lookup of its sidecar has ended
	bool final{false};          ///< its length is known: not a take still recording
	uint64_t complete{0};       ///< complete level-1 entries now
	uint64_t entries{0};        ///< level-1 entries in all
	uint64_t written{0};        ///< complete level-1 entries at the last write; 0 if none
	uint8_t failures{0};        ///< writes that failed this session
	bool pitchUnwritten{false}; ///< a pitch was remembered since the last write
};

/// @brief Whether to write the sidecar now: once the lookup has ended (so a sidecar on the card is read before it is
///        replaced), when a pitch was remembered, the index became complete, or a partial index covering at least a
///        quarter of the file grew by a quarter since its last write.
///        Always inlined, so it runs from wherever its caller does.
[[nodiscard, gnu::always_inline]] constexpr bool shouldWriteSidecar(const SidecarProgress& p) {
	if (!p.keyed || !p.looked || !p.final || p.failures >= kMaxSidecarFailures) {
		return false;
	}
	if (p.pitchUnwritten) {
		return true;
	}
	if (p.complete <= p.written) {
		return false;
	}
	if (p.complete == p.entries) {
		return true;
	}
	return p.complete * 4 >= p.entries && p.complete * 4 >= p.written * 5;
}

} // namespace deluge::storage
