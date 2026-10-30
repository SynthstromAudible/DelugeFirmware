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

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/q31_ops.h" // multiply_accumulate_32x32_rshift32_rounded
#include "dsp-next/core/ramp.h"
#include "dsp-next/core/types.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace deluge::dsp::fixed {

/// @brief The crude oscillators' VCA: out = mix + (raw * amplitude) with a *rounded* 32x32
/// multiply-accumulate, the amplitude ramping one increment per sample.
///
/// This is the sibling of @ref Amplitude for the crude (low-CPU) wave path. The two paths
/// use deliberately different VCA arithmetic — the band-limited paths use a doubling
/// `vqdmulhq`, the crude paths use `multiply_accumulate_32x32_rshift32_rounded` (scalar; the
/// legacy notes it as a ~20% saving there). Keeping both bit-exact for now means keeping both
/// arithmetics; unifying them is a later, ear-validated step.
///
/// The amplitude is **pre-incremented** each sample (`amplitude += increment; out = mac(...)`). It is a plain
/// @ref block_mixer with no vptr; the arithmetic is per sample, but it keeps a hand-written block loop rather than a
/// scalar `render(raw, mix)` under the @ref Mixer mixin.
/// @todo Give it a scalar `render` under the @ref Mixer mixin.
class CrudeAmplitude {
	int32_t amplitude_;
	int32_t increment_;

public:
	using value_type = q31; ///< q31 wave and mix samples.

	/// @param amplitude starting amplitude; the 32x32 MAC scales raw by `amplitude / 2^32`
	/// @param increment per-sample amplitude step, in the same units
	CrudeAmplitude(int32_t amplitude, int32_t increment) : amplitude_{amplitude}, increment_{increment} {}

	/// Build from an amplitude @ref deluge::dsp::Ramp (its value + per-sample increment).
	explicit CrudeAmplitude(Ramp ramp) : CrudeAmplitude(ramp.value(), ramp.increment()) {}

	/// out = raw * amplitude + mix (raw == @p input_a, mix == @p input_b), scalar rounded MAC.
	void render_block(Signal input_a, Signal input_b, Buffer output) {
		for (std::size_t i = 0; i < output.size(); ++i) {
			amplitude_ += increment_;
			output[i] = multiply_accumulate_32x32_rshift32_rounded(input_b[i], input_a[i], amplitude_);
		}
	}
};
static_assert(block_mixer<CrudeAmplitude>, "the crude VCA mixes a block");
static_assert(!std::is_polymorphic_v<CrudeAmplitude>, "the crude VCA mixes a block without a vptr");

} // namespace deluge::dsp::fixed
