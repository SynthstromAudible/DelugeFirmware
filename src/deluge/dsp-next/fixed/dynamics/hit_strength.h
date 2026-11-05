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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/sample.h"

#include <algorithm>
#include <cstdint>

/// @file
/// @brief A sidechain hit's strength: two hits before one block combined, and a hit waiting for the next block.
/// @details The one home of the rule that combines hits: the duck envelope's pending hit and the audio engine's hits
/// gathered before a block both use it. Free of the kernel core, so a caller that only combines hits needs no argon.
namespace deluge::dsp::fixed {

/// @brief Two hit strengths (Q31) arriving before one block, combined: the stronger half plus half the clamped sum.
/// @details Two equal hits of x combine to one and a half x. The sum is clamped at full scale (`Q<31>::max()`) before
/// it is halved, so the result never exceeds full scale.
[[nodiscard]] [[gnu::always_inline]] constexpr Q<31> combine_hit_strengths(Q<31> a, Q<31> b) {
	// Full scale, unsigned: the sum is clamped to it. Raw: an unsigned sum clamped below 2^31 has no typed form
	constexpr auto kFullScale = static_cast<uint32_t>(Q<31>::max().raw());
	const uint32_t sum = std::min(static_cast<uint32_t>(a.raw()) + static_cast<uint32_t>(b.raw()), kFullScale);
	return add_wrap(scale_shr_floor<1>(std::max(a, b)), Q<31>::from_raw(static_cast<int32_t>(sum >> 1)));
}

/// @brief A hit waiting for the next block. Strength Q31, 0 when none.
struct PendingHit {
	Q<31> strength{}; ///< The hit's strength: how far below Q<31>::max() it ducks. 0 is no hit.

	/// @return This hit combined with another of strength @p more (Q31), by @ref combine_hit_strengths.
	[[nodiscard]] [[gnu::always_inline]] constexpr PendingHit with(Q<31> more) const {
		return {combine_hit_strengths(strength, more)};
	}

	/// @return Whether no hit is waiting.
	[[nodiscard]] [[gnu::always_inline]] constexpr bool empty() const { return strength == Q<31>{}; }
};

} // namespace deluge::dsp::fixed
