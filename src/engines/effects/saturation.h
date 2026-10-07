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
#include "dsp-next/fixed/shaping/antialiased_tanh.h"

#include <array>
#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief Where saturation runs: a sound's voices (at 5 + amount, shifting back up from amount 2; a voice skips it
/// at amount 0) or a kit's or an audio track's bus (while the amount is not 0, at 3 + amount, shifting back up from
/// amount 3).
enum class SaturationVariant : uint8_t { VOICE, CHAIN };

/// @brief What one block of saturation uses: the clipping amount (0–15) and the home's variant.
struct SaturationParams {
	uint8_t clipping_amount{0};
	SaturationVariant variant{SaturationVariant::CHAIN};
};

/// @brief Tanh saturation of a stereo signal: an antialiased tanh per channel.
class Saturation {
public:
	/// @brief Each channel's curve starts from @p initial (dsp::fixed::AntialiasedTanh::kSilent is silence; a voice
	/// starts from 0 and is set silent when its envelopes reset).
	constexpr explicit Saturation(uint32_t initial = dsp::fixed::AntialiasedTanh::kSilent)
	    : channels_{dsp::fixed::AntialiasedTanh{0, initial}, dsp::fixed::AntialiasedTanh{0, initial}} {}

	/// @brief One Q31 sample of @p channel (0 left, 1 right), as a voice saturates inside its own loop.
	[[nodiscard, gnu::always_inline]] int32_t saturate(int32_t sample, uint32_t channel,
	                                                   const SaturationParams& params) {
		const uint32_t amount = params.clipping_amount;
		dsp::fixed::AntialiasedTanh& curve = channels_[channel];
		if (params.variant == SaturationVariant::VOICE) {
			curve.setDrive(5 + amount);
			return curve.render(sample) << (amount >= 2 ? amount - 2 : 0);
		}
		curve.setDrive(3 + amount);
		return curve.render(sample) << (amount >= 3 ? amount - 3 : 0);
	}

	/// @brief A stereo block, in place; a CHAIN variant at amount 0 leaves it untouched.
	void process(std::span<dsp::StereoSample> block, const SaturationParams& params);

	/// @brief Both channels' curves back to silence.
	void reset() { channels_ = {dsp::fixed::AntialiasedTanh{}, dsp::fixed::AntialiasedTanh{}}; }

private:
	std::array<dsp::fixed::AntialiasedTanh, 2> channels_;
};

} // namespace deluge::effects
