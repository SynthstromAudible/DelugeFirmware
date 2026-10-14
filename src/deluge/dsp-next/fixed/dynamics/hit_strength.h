/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "dsp-next/core/sample.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <cstdint>

/// @file
/// @brief A sidechain hit's strength: two hits before one block combined, and a hit waiting for the next block.
/// @details The one home of the rule that combines hits: the duck envelope's pending hit and the audio engine's hits
/// gathered before a block both use it. Free of the kernel core, so a caller that only combines hits needs no argon.
namespace deluge::dsp::fixed {

/// @brief Two hit strengths (Q31) arriving before one block, combined: the stronger half plus half the clamped sum.
/// @details Two equal hits of x combine to one and a half x. The sum is clamped at full scale (ONE_Q31) before it is
/// halved, so the result never exceeds full scale.
[[nodiscard]] [[gnu::always_inline]] constexpr q31 combine_hit_strengths(q31 a, q31 b) {
	// Full scale, ONE_Q31, unsigned: the sum is clamped to it.
	constexpr uint32_t kFullScale{ONE_Q31};
	const uint32_t sum = std::min(static_cast<uint32_t>(a) + static_cast<uint32_t>(b), kFullScale);
	return (std::max(a, b) >> 1) + static_cast<q31>(sum >> 1);
}

/// @brief A hit waiting for the next block. Strength Q31, 0 when none.
struct PendingHit {
	q31 strength{0}; ///< The hit's strength, Q31: how far below ONE_Q31 it ducks. 0 is no hit.

	/// @return This hit combined with another of strength @p more (Q31), by @ref combine_hit_strengths.
	[[nodiscard]] [[gnu::always_inline]] constexpr PendingHit with(q31 more) const {
		return {combine_hit_strengths(strength, more)};
	}

	/// @return Whether no hit is waiting.
	[[nodiscard]] [[gnu::always_inline]] constexpr bool empty() const { return strength == 0; }
};

} // namespace deluge::dsp::fixed
