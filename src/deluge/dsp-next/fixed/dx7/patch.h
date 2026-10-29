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

#include <array>
#include <cstddef>
#include <cstdint>

/// @file
/// @brief A DX7 patch's bytes, and the ranges the engine relies on.
namespace deluge::dsp::fixed::dx7 {

/// @brief A DX7 patch's 156 bytes, in the patch's layout: six ops of 21 bytes (op 6 first), then the globals, then the
/// op-switch bits.
using PatchBytes = std::array<uint8_t, 156>;

/// @brief Clamps the bytes the engine would index, shift or overflow by to the DX7's ranges, leaving every in-range
/// patch as it is: each op's fine (op × 21 + 19) to 99, the pitch envelope's rates and levels (126 to 133) to 99, the
/// algorithm (134) to 31, the feedback (135) to 7 and the LFO delay (138) to 99. Any other byte, in range or not, has
/// defined arithmetic in the engine.
/// @details A patch can arrive with any byte value: a cartridge unpack, a single-voice sysex file or a song's hex.
/// The voice's owner applies this to the snapshot it takes per block.
[[gnu::always_inline]] constexpr void sanitise(PatchBytes& patch) {
	constexpr std::size_t kPitchEnv = 126;
	constexpr std::size_t kAlgorithm = 134;
	constexpr std::size_t kFeedback = 135;
	constexpr std::size_t kLfoDelay = 138;
	constexpr std::size_t kOpBytes = 21;
	constexpr std::size_t kFine = 19;
	// The owner calls this per block, from an app file built without -finline-functions: the bytes are reached through
	// a pointer and clamped inline, so nothing here becomes a call.
	uint8_t* const bytes = patch.data();
	const auto clamp = [](uint8_t& byte, uint8_t max) __attribute__((always_inline)) {
		if (byte > max) {
			byte = max;
		}
	};
	// fixed frequency scales (coarse & 3) × 100 + fine by 4458616: past 99 that passes INT32_MAX
	for (std::size_t op = 0; op < 6; ++op) {
		clamp(bytes[(op * kOpBytes) + kFine], 99);
	}
	for (std::size_t i = kPitchEnv; i < kPitchEnv + 8; ++i) {
		clamp(bytes[i], 99);
	}
	clamp(bytes[kAlgorithm], 31);
	clamp(bytes[kFeedback], 7);
	clamp(bytes[kLfoDelay], 99);
}

} // namespace deluge::dsp::fixed::dx7
