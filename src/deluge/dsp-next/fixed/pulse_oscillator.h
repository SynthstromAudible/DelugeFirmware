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

#include "dsp-next/core/generator.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/table.h"
#include "util/misc.h" // util::constexpr_for
#include <argon.hpp>
#include <cstdint>
#include <limits>

namespace deluge::dsp::fixed {

/// @brief The perfect (mathematical) pulse / variable-width-square oscillator: a stateful
/// generator that reads a band-limited square @ref Table twice — once at the phase, once at a
/// phase offset by the pulse width — and ring-modulates the two, giving a square whose duty
/// cycle is set by the offset. Backs OscType::SQUARE when a pulse width is set.
///
/// render() is the exact legacy `waveRenderingFunctionPulse` math, raw (no amplitude/mix —
/// that is the Amplitude mixer's job, applied with the same vqdmulhq arithmetic as the
/// TableOscillator path). The seekable phase lets a @ref SyncedOscillator drive the pulse
/// under hard sync, exactly as the legacy renderOscSync did. Bit-exact, structural only.
class [[gnu::hot]] PulseOscillator : public Generator<PulseOscillator, q31> {
	const int16_t* samples_;
	int32_t magnitude_;
	Phasor phasor_;
	uint32_t phase_offset_; ///< phase distance between the two squares; sets the duty cycle

	static constexpr int16_t kMin = std::numeric_limits<int16_t>::min();
	static constexpr int16_t kMax = std::numeric_limits<int16_t>::max();

public:
	PulseOscillator(Table table, uint32_t phase, uint32_t increment, uint32_t phase_offset)
	    : samples_{table.samples.data()}, magnitude_{table.magnitude}, phasor_{phase, increment},
	      phase_offset_{phase_offset} {}

	/// Jump the phase (e.g. a hard-sync reset between windows). See @ref deluge::dsp::Phasor::seek.
	void seek(uint32_t phase) { phasor_.seek(phase); }

	[[gnu::always_inline]] Argon<q31> render() {
		const uint32_t phase = phasor_.phase();
		const uint32_t increment = phasor_.increment();

		// Interpolation fractions for the two squares: A at the phase, B at the offset phase.
		Argon<uint32_t> phase_vector = Argon<uint32_t>{phase}.MultiplyAdd(Argon<uint32_t>{1U, 2U, 3U, 4U}, increment);
		Argon<uint32_t> offset_vector = phase_vector + phase_offset_;
		ArgonHalf<int16_t> fraction_a =
		    (phase_vector >> (32 - magnitude_)).ShiftRightNarrow<16>().BitwiseAnd(kMax).As<int16_t>();
		ArgonHalf<int16_t> fraction_b =
		    (offset_vector >> (32 - magnitude_)).ShiftRightNarrow<16>().BitwiseAnd(kMax).As<int16_t>();

		// Read the {entry, next-entry} interpolation pairs for both squares as single-word lane
		// loads (the same pattern as TableOscillator; a strided gather reads the wrong samples).
		Argon<uint32_t> pair_a{0U};
		Argon<uint32_t> pair_b{0U};
		util::constexpr_for<0, 4, 1>([&]<int i>() {
			uint32_t here = phase + increment * (i + 1);
			uint32_t index_a = here >> (32 - magnitude_);
			pair_a[i].Load(reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(samples_) + (index_a << 1)));
			uint32_t index_b = (here + phase_offset_) >> (32 - magnitude_);
			pair_b[i].Load(reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(samples_) + (index_b << 1)));
		});
		ArgonHalf<int16_t> entry_a = pair_a.Narrow().As<int16_t>();
		ArgonHalf<int16_t> next_a = pair_a.ShiftRightNarrow<16>().As<int16_t>();
		ArgonHalf<int16_t> entry_b = pair_b.Narrow().As<int16_t>();
		ArgonHalf<int16_t> next_b = pair_b.ShiftRightNarrow<16>().As<int16_t>();

		// Interpolate each square. Square A is done with flipped polarity (the strengths are
		// biased by kMin), which is the polarity flip the ring-mod needs anyway.
		ArgonHalf<int16_t> strength_a1 = fraction_a | kMin;
		ArgonHalf<int16_t> strength_a2 = static_cast<int16_t>(kMin) - strength_a1;
		Argon<int32_t> square_a = strength_a2.MultiplyDoubleSaturateLong(next_a) //<
		                              .MultiplyDoubleAddSaturateLong(strength_a1, entry_a);

		ArgonHalf<int16_t> strength_b2 = fraction_b & kMax;
		ArgonHalf<int16_t> strength_b1 = static_cast<int16_t>(kMax) - strength_b2;
		Argon<int32_t> square_b = strength_b2.MultiplyDoubleSaturateLong(next_b) //<
		                              .MultiplyDoubleAddSaturateLong(strength_b1, entry_b);

		// Ring-modulate the two squares; the << 1 restores full scale.
		Argon<q31> output = square_a.MultiplyRoundFixedQMax(square_b) << 1;
		phasor_.seek(phase + increment * 4);
		return output;
	}
};

} // namespace deluge::dsp::fixed
