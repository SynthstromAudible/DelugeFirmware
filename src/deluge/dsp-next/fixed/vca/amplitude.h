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
#include "dsp-next/core/fixed_point_neon.h"
#include "dsp-next/core/mixer.h"
#include "dsp-next/core/ramp.h"
#include "dsp-next/core/types.h"
#include <argon.hpp>
#include <arm_neon.h>
#include <type_traits>

namespace deluge::dsp::fixed {

/// @brief The voice VCA, expressed as a mixer: scales a raw wave block by a per-sample
/// ramped amplitude and sums it into the existing mix — `out = (raw * amp) + mix`.
///
/// The amplitude arrives as Q31 (each wave pre-shifts the voice's amplitude, see docs/dev/fixed_point.md, "Gain") and
/// is held at Q30, one fewer bit: four samples' amplitudes `amp + increment·{1,2,3,4}`, each reformatted. The doubling
/// multiply of a Q31 sample by the Q30 amplitude is Q30, summed into a mix read as Q30, as CrudeAmplitude's is. The
/// legacy `Oscillator::applyAmplitudeVectorToBuffer` computes the same bits.
class Amplitude : public Mixer<Amplitude, Q<31>> {
	QVec<30> amplitude_;           ///< The next four samples' amplitudes.
	QVec<30> amplitude_increment_; ///< Four samples' increments at Q30: the Q31 increment × 4 / 2.

public:
	/// @brief Starts the ramp at @p amplitude, rising by @p amplitude_increment per sample.
	/// @details The ramp positions `amp + increment·{1,2,3,4}` are integer multiples (`vmla.i32`, wrapping).
	/// @param amplitude           the starting amplitude (pre-shifted by the caller)
	/// @param amplitude_increment the per-sample amplitude step
	Amplitude(Q<31> amplitude, Q<31> amplitude_increment)
	    : amplitude_{as_floor<QVec<30>>(QVec<31>::from_raw(
	          Argon<int32_t>{amplitude.raw()}.MultiplyAdd(amplitude_increment.raw(), int32x4_t{1, 2, 3, 4})))},
	      // Four samples' rise at Q30: the Q31 increment × 4 is its bits × 2 read as Q30
	      amplitude_increment_{QVec<30>::splat(reinterpret_bits<Q<30>>(scale_shl_wrap<1>(amplitude_increment)))} {}

	/// Build from an amplitude @ref deluge::dsp::Ramp (its value + per-sample increment).
	explicit Amplitude(Ramp ramp) : Amplitude(ramp.value(), ramp.increment()) {}

	/// @param raw the freshly-generated wave block (read-only), Q31
	/// @param mix the existing mix to accumulate into, Q30
	/// @return raw scaled by the (advancing) amplitude, summed with mix, Q30
	[[gnu::always_inline]] QVec<31> render(QVec<31> raw, QVec<31> mix) {
		const QVec<30> scaled = mul_doubling_sat(amplitude_, raw);
		amplitude_ = add_wrap(amplitude_, amplitude_increment_);
		// The mix block holds Q30 words, the sum's format
		return reinterpret_bits<QVec<31>>(add_wrap(scaled, reinterpret_bits<QVec<30>>(mix)));
	}
};
static_assert(block_mixer<Amplitude>, "the VCA mixes a block");
static_assert(!std::is_polymorphic_v<Amplitude>, "the VCA mixes a block without a vptr");

} // namespace deluge::dsp::fixed
