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
/// @brief The inputs the effects kernels' specs share: seeded full-scale frames, the block sizes they are cut into, and
/// param values across q31.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/noise/noise.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace effects_workloads {

using deluge::dsp::StereoSample;

/// The block sizes every kernel comparison cycles through: a single frame, odd sizes, a vector and one past it, the
/// engine's 128, and a pair.
constexpr std::array<std::size_t, 6> kBlockSizes{1, 7, 32, 33, 128, 2};

/// Param values across q31: both extremes and their neighbours, the bitcrush's switch-on point, the quarters and zero.
constexpr std::array<int32_t, 9> kSweep{std::numeric_limits<int32_t>::min(),
                                        std::numeric_limits<int32_t>::min() + 1,
                                        -2113929216,
                                        -0x40000000,
                                        -1,
                                        0,
                                        1,
                                        0x40000000,
                                        std::numeric_limits<int32_t>::max()};

/// @p n full-scale frames of seeded noise, left and right from one stream.
inline std::vector<StereoSample> frames(std::size_t n, uint32_t seed) {
	deluge::dsp::fixed::SeededNoise noise{seed};
	std::vector<StereoSample> out(n);
	for (StereoSample& f : out) {
		f.l = noise.render();
		f.r = noise.render();
	}
	return out;
}

/// Whether two frame blocks are identical.
inline bool same(const std::vector<StereoSample>& a, const std::vector<StereoSample>& b) {
	return std::ranges::equal(a, b, [](StereoSample x, StereoSample y) { return x.l == y.l && x.r == y.r; });
}

/// The block length at cut @p i of a run with @p left frames left: kBlockSizes in turn, the last cut short.
inline std::size_t block_at(std::size_t i, std::size_t left) {
	return std::min(kBlockSizes[i % kBlockSizes.size()], left);
}

} // namespace effects_workloads
