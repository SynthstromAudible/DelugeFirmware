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

#include "dsp-next/core/types.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/shaping/tanh.h" // kTanhCentre
#include "dsp-next/fixed/shaping/tanh_table.h"
#include "util/param_functions.h" // lshiftAndSaturateUnknown

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed::legacy {

/// @brief Tanh saturation, antialiased: each output is interpolated between this sample's and the previous one's
/// position on the curve (@ref kTanhTable), so a jump between samples is smoothed rather than aliased. A processor
/// (`render` per sample, `renderBlock` per block) owning that previous position.
class AntialiasedTanh {
public:
	using value_type = q31;

	/// The previous position of a silent signal: the middle of the curve.
	static constexpr uint32_t kSilent = kTanhCentre;

	/// @param drive How hard the input is driven into the curve: it is scaled by 2^drive (saturating) first.
	/// @param previous The position the first sample is interpolated against.
	constexpr explicit AntialiasedTanh(uint32_t drive = 0, uint32_t previous = kSilent)
	    : drive_{drive}, previous_{previous} {}

	/// @brief Drives the input by 2^@p drive from the next sample on.
	void setDrive(uint32_t drive) { drive_ = drive; }

	/// @brief One sample (Q31) through the curve; the output's level falls as the drive rises (it is shifted down by
	/// drive + 1), so callers scale it back as their home needs.
	[[nodiscard, gnu::always_inline]] q31 render(q31 sample) {
		const uint32_t position =
		    static_cast<uint32_t>(lshiftAndSaturateUnknown(sample, static_cast<uint8_t>(drive_))) + kSilent;
		const int32_t out = bilinear_table_interpolator.interpolateSigned(
		                        position, previous_, 32, 32, std::span<const std::array<int16_t, 129>>{kTanhTable})
		                    >> (drive_ + 1);
		previous_ = position;
		return out;
	}

	/// @brief A block through the curve, sample by sample.
	void renderBlock(Signal in, Buffer out) {
		for (std::size_t i = 0; i < in.size(); i++) {
			out[i] = render(in[i]);
		}
	}

	/// @brief The position the next sample is interpolated against.
	[[nodiscard]] uint32_t previous() const { return previous_; }

private:
	uint32_t drive_;
	uint32_t previous_;
};

} // namespace deluge::dsp::fixed::legacy
