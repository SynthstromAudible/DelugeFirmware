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

#include "dsp/fixed/envelope/curve.h"

#include <algorithm>
#include <cstdint>
#include <optional>

/// @file
/// @brief EnvelopeStage: where an envelope's stage has got to, and the time one that ends hands the next.

namespace deluge::dsp::fixed {

/// @brief A stage's position, in steps of which it is kEnvelopeStageEnd long.
/// @details A rate is steps a sample, so a rate that changes is only the next block's: the position carries on, with
/// nothing kept of the old one. The integer is exact however slow the stage.
struct EnvelopeStage {
	uint32_t position = 0; ///< 0 to kEnvelopeStageEnd.

	/// @brief Moves on by @p n samples at @p rate.
	/// @param rate Any.
	/// @param n    At most 128.
	/// @return The steps past the stage's end when it ended within them, its position then left; nullopt otherwise.
	[[gnu::always_inline, nodiscard]] std::optional<uint64_t> advance(uint32_t rate, uint32_t n) {
		const uint64_t moved = position + (uint64_t{rate} * n);
		if (moved >= kEnvelopeStageEnd) [[unlikely]] {
			return moved - kEnvelopeStageEnd;
		}
		position = static_cast<uint32_t>(moved);
		return std::nullopt;
	}
};

/// @brief Where a stage at @p next_rate stands at the block's end, when the one before it, at @p rate, ran @p over
/// steps past its own end (EnvelopeStage::advance's answer): it has had that time, the fraction of a sample included.
/// @details A next rate of kEnvelopeStageEnd and above ends its stage within a sample and is taken as that, so the
/// product stays inside 64 bits: @p over is below 2^39. A rate of 0 never ran past an end, so nothing divides by it.
/// @return The position, kEnvelopeStageEnd and above when that stage has ended too.
[[nodiscard]] constexpr uint64_t carried(uint64_t over, uint32_t rate, uint32_t next_rate) {
	return rate == 0 ? 0 : over * std::min(next_rate, kEnvelopeStageEnd) / rate;
}

} // namespace deluge::dsp::fixed
