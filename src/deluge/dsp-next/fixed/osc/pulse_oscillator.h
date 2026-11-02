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
#include "dsp-next/core/generator.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/bandlimited/table.h"
#include "util/misc.h" // util::constexpr_for
#include <argon.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

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
class PulseOscillator : public Generator<PulseOscillator, q31> {
	const int16_t* samples_;
	int32_t magnitude_;
	Phasor phasor_;
	uint32_t phase_offset_; ///< phase distance between the two squares; sets the duty cycle

	static constexpr int16_t kMin = std::numeric_limits<int16_t>::min();
	static constexpr int16_t kMax = std::numeric_limits<int16_t>::max();

public:
	PulseOscillator(Table table, uint32_t phase, uint32_t increment, uint32_t phase_offset)
	    : samples_{table.samples.data()}, magnitude_{table.magnitude},
	      phasor_{UQ<32>::from_raw(phase), UQ<32>::from_raw(increment)}, phase_offset_{phase_offset} {}

	/// Jump the phase (e.g. a hard-sync reset between windows). See @ref deluge::dsp::Phasor::seek.
	void seek(uint32_t phase) { phasor_.seek(UQ<32>::from_raw(phase)); }

	/// The live phase — for reading back a persistent oscillator's phase across blocks.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase().raw(); }

	/// Change the per-sample phase step for the next block (pitch change), keeping the accumulated phase.
	void set_increment(uint32_t increment) { phasor_.set_increment(UQ<32>::from_raw(increment)); }

	[[gnu::always_inline]] Argon<q31> render() {
		const uint32_t phase = phasor_.phase().raw();
		const uint32_t increment = phasor_.increment().raw();

		// Interpolation fractions for the two squares: A at the phase, B at the offset phase. Each is the 16 phase bits
		// just below the table index; the strengths below take the 15 they need.
		Argon<uint32_t> phase_vector = Argon<uint32_t>{phase}.MultiplyAdd(Argon<uint32_t>{1U, 2U, 3U, 4U}, increment);
		Argon<uint32_t> offset_vector = phase_vector + phase_offset_;
		const int32_t fraction_shift = 32 - magnitude_ - 16;
		ArgonHalf<int16_t> fraction_a = (phase_vector >> fraction_shift).Narrow().As<int16_t>();
		ArgonHalf<int16_t> fraction_b = (offset_vector >> fraction_shift).Narrow().As<int16_t>();

		// Read the {entry, next-entry} interpolation pairs for both squares as single-word lane
		// loads (the same pattern as TableOscillator; a strided gather reads the wrong samples). Each
		// word is copied rather than read through a `uint32_t*`: the index is phase-driven and so odd
		// half the time, and an alignment-qualified NEON load of it faults on ARM (see TableOscillator).
		Argon<uint32_t> pair_a{0U};
		Argon<uint32_t> pair_b{0U};
		util::constexpr_for<0, 4, 1>([&]<int i> [[gnu::always_inline]] () {
			uint32_t here = phase + increment * (i + 1);
			uint32_t index_a = here >> (32 - magnitude_);
			uint32_t word_a;
			std::memcpy(&word_a, reinterpret_cast<const std::byte*>(samples_) + (index_a << 1), sizeof(word_a));
			pair_a[i] = word_a;
			uint32_t index_b = (here + phase_offset_) >> (32 - magnitude_);
			uint32_t word_b;
			std::memcpy(&word_b, reinterpret_cast<const std::byte*>(samples_) + (index_b << 1), sizeof(word_b));
			pair_b[i] = word_b;
		});
		const auto pairs_a = Q16x2Vec<15>::from_raw(pair_a);
		const auto pairs_b = Q16x2Vec<15>::from_raw(pair_b);

		// Interpolate each square between its Q15 entries, Q31. The weight w is each fraction's low 15 bits; square A
		// is interpolated with weights w − 1 and −w, so it comes out negated: the polarity flip the ring-mod needs
		// anyway. fraction | kMin is w − 1 whatever the fraction's top bit: as a subtraction it would need a mask
		// first.
		const auto strength_a1 = Q16Half<15>::from_raw(fraction_a | kMin);
		const Q16Half<15> strength_a2 = sub_wrap(Q16Half<15>::splat(Q16<15>::min()), strength_a1);
		const QVec<31> square_a =
		    mla_doubling_wide_sat(mul_doubling_wide_sat(strength_a2, pairs_a.hi()), strength_a1, pairs_a.lo());

		const auto strength_b2 = Q16Half<15>::from_raw(fraction_b & kMax);
		const Q16Half<15> strength_b1 = sub_wrap(Q16Half<15>::splat(Q16<15>::max()), strength_b2);
		const QVec<31> square_b =
		    mla_doubling_wide_sat(mul_doubling_wide_sat(strength_b2, pairs_b.hi()), strength_b1, pairs_b.lo());

		// Ring-modulate the two squares: the rounded doubling product, then << 1 restores full scale (wrapping).
		const Argon<q31> output = scale_shl_wrap<1>(mul_doubling_r_sat(square_a, square_b)).raw();
		phasor_.seek(UQ<32>::from_raw(phase + increment * 4));
		return output;
	}
};
static_assert(block_generator<PulseOscillator>, "a pulse oscillator fills a block");
static_assert(!std::is_polymorphic_v<PulseOscillator>, "a pulse oscillator fills a block without a vptr");

} // namespace deluge::dsp::fixed
