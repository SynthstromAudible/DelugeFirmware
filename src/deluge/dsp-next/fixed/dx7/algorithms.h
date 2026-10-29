// Based on Dexed's fm_core (Copyright 2012 Google Inc., Apache License 2.0).

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

#include <cstddef>
#include <cstdint>

/// @file
/// @brief The DX7's 32 algorithms as routing flags per operator, and the constants the operators share.
namespace deluge::dsp::fixed::dx7 {

/// @brief An operator's routing in an algorithm: which bus it writes (none is the voice's output), whether it adds
/// to what is there, which bus it reads, and its place in a feedback loop.
enum Flag : uint8_t {
	OUT_BUS_ONE = 1 << 0, ///< Writes bus one.
	OUT_BUS_TWO = 1 << 1, ///< Writes bus two.
	OUT_BUS_ADD = 1 << 2, ///< Adds to the bus (or the output) rather than setting it.
	IN_BUS_ONE = 1 << 4,  ///< Reads bus one as its modulation.
	IN_BUS_TWO = 1 << 5,  ///< Reads bus two as its modulation.
	FB_IN = 1 << 6,       ///< Reads the feedback history.
	FB_OUT = 1 << 7,      ///< Writes the feedback history.
};

/// @brief Each algorithm's flags for ops 6 down to 1 (the patch's order: op 6 first).
inline constexpr uint8_t kAlgorithms[32][6] = {
    // NOLINT(*-avoid-c-arrays): read by algorithm and op index
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, // 1
    {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, // 2
    {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}, // 3
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, // 4
    {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, // 5
    {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14}, // 6
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, // 7
    {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, // 8
    {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}, // 9
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, // 10
    {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, // 11
    {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14}, // 12
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, // 13
    {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, // 14
    {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}, // 15
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, // 16
    {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, // 17
    {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14}, // 18
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, // 19
    {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, // 20
    {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}, // 21
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, // 22
    {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, // 23
    {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04}, // 24
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, // 25
    {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, // 26
    {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}, // 27
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, // 28
    {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, // 29
    {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04}, // 30
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, // 31
    {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}, // 32
};

/// @brief Op 6's flags in algorithms 4 and 6 with feedback under MkI: it alone renders the whole feedback stack
/// (three ops in 4, two in 6) straight to the output.
inline constexpr uint8_t kMkIFusedFeedback = 0xc4;

/// @brief A modern op whose gain is below this at both ends of a block is inaudible and skipped.
inline constexpr int32_t kGainLevelThresh = 1120;

/// @brief MkI's attenuation scale: 0 is loudest, kEnvMax silent.
inline constexpr int32_t kEnvMax = 1 << 14;

/// @brief A MkI op attenuated beyond this at both ends of a block is inaudible and skipped.
inline constexpr int32_t kMkILevelThresh = kEnvMax - 100;

/// @brief The longest block a voice renders, in samples: the voice adapter's block buffer, which a block never fills
/// past its length.
inline constexpr std::size_t kMaxBlock = 132;

/// @brief @p base divided by n, given @p inv_n = 2^30 / n: a block's per-sample step.
[[nodiscard]] constexpr int32_t div_n(int32_t base, int32_t inv_n) {
	return static_cast<int32_t>((static_cast<int64_t>(base) * inv_n) >> 30);
}

} // namespace deluge::dsp::fixed::dx7
