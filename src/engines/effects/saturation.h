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
#include "dsp-next/fixed/shaping/tanh.h" // kTanhCentre

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

/// @brief Tanh saturation of a stereo signal: an anti-aliased tanh per channel, whose drive follows the params on
/// every call while each channel's position carries over, so a drive change interpolates from the previous sample.
class Saturation {
	using Curve = dsp::fixed::AntialiasedTanh;

public:
	/// @brief The voice variant's drive is this plus the amount, as a power of two.
	static constexpr uint32_t kVoiceDriveBase = 5;
	/// @brief The voice variant shifts its output back up by the amount less this, from this amount on.
	static constexpr uint32_t kVoiceShiftFrom = 2;
	/// @brief The chain variant's drive is this plus the amount, as a power of two.
	static constexpr uint32_t kChainDriveBase = 3;
	/// @brief The chain variant shifts its output back up by the amount less this, from this amount on.
	static constexpr uint32_t kChainShiftFrom = 3;

	/// @brief Each channel's curve starts from the table position @p initial (dsp::fixed::kTanhCentre is silence; a
	/// voice starts from 0 and is set silent when its envelopes reset).
	/// @param initial The position each channel's first sample interpolates against, unsigned over 32 bits.
	explicit Saturation(uint32_t initial = dsp::fixed::kTanhCentre) {
		Curve::place(left_, initial);
		Curve::place(right_, initial);
	}

	/// @brief One sample of @p channel, as a voice saturates inside its own loop.
	/// @param sample The input, Q31.
	/// @param channel 0 for the left channel, 1 for the right.
	/// @param params The amount and variant, which set the drive for this sample.
	/// @return The saturated sample, Q31.
	[[nodiscard, gnu::always_inline]] int32_t saturate(int32_t sample, uint32_t channel,
	                                                   const SaturationParams& params) {
		const uint32_t amount = params.clipping_amount;
		Curve::State& position = channel == 0 ? left_ : right_;
		const bool voice = params.variant == SaturationVariant::VOICE;
		const Curve::Coefficients c = Curve::coefficients({.drive = drive(voice, amount)});
		return Curve::step(c, {}, position, sample) << shift(voice, amount);
	}

	/// @brief A stereo block, in place; a CHAIN variant at amount 0 leaves it untouched.
	/// @param block The block, Q31 per channel.
	/// @param params The amount and variant, which set the drive for the whole block.
	void process(std::span<dsp::StereoSample> block, const SaturationParams& params);

	/// @brief Both channels' curves back to silence.
	void reset() {
		left_ = {};
		right_ = {};
	}

private:
	/// @brief The drive, as a power of two, for @p amount on a voice (@p voice) or a chain.
	[[nodiscard, gnu::always_inline]] static constexpr uint32_t drive(bool voice, uint32_t amount) {
		return (voice ? kVoiceDriveBase : kChainDriveBase) + amount;
	}

	/// @brief The left shift that brings the output back up for @p amount on a voice (@p voice) or a chain.
	[[nodiscard, gnu::always_inline]] static constexpr uint32_t shift(bool voice, uint32_t amount) {
		const uint32_t from = voice ? kVoiceShiftFrom : kChainShiftFrom;
		return amount >= from ? amount - from : 0;
	}

	Curve::State left_;  ///< The left channel's position on the curve.
	Curve::State right_; ///< The right channel's position on the curve.
};

} // namespace deluge::effects
