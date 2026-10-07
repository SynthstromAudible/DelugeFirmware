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

#include "dsp-next/core/stereo_sample.h"
#include "engines/effects/kernels.h"

#include <array>
#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief Where saturation runs: a sound's voices (always, at 5 + amount, shifting back from amount 2) or a kit's or
/// an audio track's bus (only while the amount is not 0, at 3 + amount, shifting back from amount 3).
enum class SaturationVariant : uint8_t { VOICE, CHAIN };

/// @brief What one block of saturation uses: the clipping amount (0–15) and the home's variant.
struct SaturationParams {
	uint8_t clipping_amount{0};
	SaturationVariant variant{SaturationVariant::CHAIN};
};

/// @brief Tanh saturation with antialiasing memory per channel.
class Saturation {
public:
	/// @brief Memory starting at @p initial on both channels: 2^31 is a silent working value (a voice starts at 0 and
	/// is set to 2^31 when its envelopes reset).
	constexpr explicit Saturation(uint32_t initial = 2147483648u) : last_{initial, initial} {}

	/// @brief One Q31 sample of @p channel (0 left, 1 right), as a voice saturates inside its own loop.
	[[nodiscard, gnu::always_inline]] int32_t saturate(int32_t sample, uint32_t channel,
	                                                   const SaturationParams& params) {
		const uint32_t amount = params.clipping_amount;
		if (params.variant == SaturationVariant::VOICE) {
			return tanh_antialiased(sample, last_[channel], 5 + amount) << (amount >= 2 ? amount - 2 : 0);
		}
		return tanh_antialiased(sample, last_[channel], 3 + amount) << (amount >= 3 ? amount - 3 : 0);
	}

	/// @brief A stereo block, in place; a CHAIN variant at amount 0 leaves it untouched.
	void process(std::span<dsp::StereoSample> block, const SaturationParams& params);

	/// @brief Forgets the antialiasing memory.
	void reset() { last_ = {2147483648u, 2147483648u}; }

private:
	/// The previous working value per channel (2^31 is a silent one).
	std::array<uint32_t, 2> last_{2147483648u, 2147483648u};
};

} // namespace deluge::effects
