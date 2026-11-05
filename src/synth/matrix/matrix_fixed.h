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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/fixed_point_neon.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "synth/matrix/descriptors.h"
#include "synth/modulation/cable_depth.h" // apply_range
#include "util/param_functions.h"         // getFinalParameterValueExpWithDumbEnvelopeHack

#include <argon.hpp>
#include <cstdint>
#include <limits>
#include <numbers>

namespace deluge::synth::matrix {

using dsp::q;
using dsp::Q;

/// @brief The matrix's fixed-point numeric core: the instrument's patching arithmetic, operation for operation. Each
/// scalar function is the reference its four-lane form is proven against.
/// @details A source value is Q31 (bipolar, or unipolar as a cable reads it), a cable's depth Q30 ([-2^30, 2^30]), a
/// chain's combination Q29 (2^29 is "1"). The params' final values are words in each param's format (param_format.h):
/// the curves take the neutral as Q31, the same arithmetic for every format.
struct MatrixFixed {
	using Source = Q<31>;      ///< A source value as a cable reads it.
	using Depth = Q<30>;       ///< A cable's depth.
	using Combination = Q<29>; ///< A chain's running product or sum, and its combination.

	/// Four Q31 lanes: sources, presets, neutrals, the final values' words.
	using SourceLanes = dsp::QVec<31>;
	/// Four Q30 lanes: the params' preset ranges.
	using DepthLanes = dsp::QVec<30>;
	/// Four Q29 lanes: combinations.
	using CombinationLanes = dsp::QVec<29>;

	/// "1" in the chains' Q29 and the depth results' scale.
	static constexpr Combination kOne = Combination::one();

	/// Four params' words from @p words: each param's value in its format, read as Q31 lanes.
	[[gnu::always_inline]] static SourceLanes load_words(const int32_t* words) {
		return SourceLanes::from_raw(Argon<int32_t>::Load(words));
	}
	/// Four params' words, from the Q31 lanes they were read or computed as.
	[[gnu::always_inline]] static void store_words(SourceLanes lanes, int32_t* words) { lanes.raw().StoreTo(words); }

	/// A source value as a cable reads it.
	[[gnu::always_inline]] static Source convert(Conversion conversion, Source value) {
		constexpr Source kHalf = halve_floor(Source::max());
		switch (conversion) {
		case Conversion::AS_IS:
			return value;
		case Conversion::UNIPOLAR: // half the bipolar value, rounded toward zero, centred one LSB under a half
			return add_wrap(halve_toward_zero(value), kHalf);
		case Conversion::AFTERTOUCH_BIPOLAR: // the unipolar value centred and doubled, wrapping
			return scale_shl_wrap<1>(sub_wrap(value, kHalf));
		case Conversion::AFTERTOUCH_DEPTH: // the same about the true half
			return scale_shl_wrap<1>(sub_wrap(value, q<31>(0.5)));
		}
		return value;
	}

	/// A cable's depth, shaped by @p mode.
	[[gnu::always_inline]] static Depth depth(DepthMode mode, Depth d) {
		if (mode == DepthMode::PLAIN) {
			return d;
		}
		// The depth squared to Q29, its sign kept: raw, an integer product of the two truncated factors
		int32_t output = (d.raw() >> 15) * (d.raw() >> 16);
		if (d < Depth{}) {
			output = -output;
		}
		// The square's word is the shaped depth, as modified_depth's (cable_depth.h)
		const auto squared = Q<29>::from_raw(output);
		if (mode == DepthMode::SQUARED_PITCH_VELOCITY) { // times 2/3
			return dsp::reinterpret_bits<Depth>(mul_r_as_wrap<Q<29>>(squared, q<31>(2.0 / 3.0)));
		}
		if (mode == DepthMode::SQUARED_PITCH_OTHER) { // times 1/√2
			return dsp::reinterpret_bits<Depth>(mul_r_as_wrap<Q<29>>(squared, q<31>(1.0 / std::numbers::sqrt2)));
		}
		return dsp::reinterpret_bits<Depth>(squared);
	}

	/// A cable's term: its source scaled by its depth, then by its depth result (@p range, "1" when none controls it).
	[[gnu::always_inline]] static Combination product(Source source, Depth depth, Combination range) {
		return apply_range(mul(source, depth), range);
	}

	/// A depth cable's term: its source scaled by its depth, with no range step.
	[[gnu::always_inline]] static Combination depth_product(Source source, Depth depth) { return mul(source, depth); }

	/// One step of a multiply chain: the running product times (1 + @p term), Q26, held to [−4, 4) and back at Q29.
	/// @details A term lies in [−2^29, 2^29] (apply_range holds a cable's to "1", a preset's is at most half its Q30
	/// range), so 1 + term lies in [0, 2^30] and its sum cannot wrap.
	[[gnu::always_inline]] static Combination multiply_step(Combination total, Combination term) {
		return as_wrap<Combination>(clamp_bits<29>(mul(total, add_wrap(term, kOne))));
	}

	/// One step of an add chain: a saturating sum.
	[[gnu::always_inline]] static Combination add_step(Combination total, Combination term) {
		return add_sat(total, term);
	}

	/// The preset's term for param @p p: the preset times the param's Q30 range.
	[[gnu::always_inline]] static Combination preset_term(Source preset, int32_t p) {
		return mul(preset, Depth::from_raw(kParamRange[p]));
	}

	/// An add chain's doubling for a wavetable index: its cables reach twice as far, held to the 31-bit range first, so
	/// the top is 2^31 − 2.
	[[gnu::always_inline]] static Combination double_saturating(Combination total) {
		return scale_shl_wrap<1>(clamp_bits<31>(total));
	}

	/// A depth destination's result from its chain's combination.
	[[gnu::always_inline]] static Combination depth_result(Combination combination) {
		return deluge::dsp::fixed::curve_linear(kOne, combination);
	}

	/// Param @p p's final value from its chain's combination, as the param's word.
	[[gnu::always_inline]] static int32_t curve(CurveKind kind, int32_t p, Combination combination) {
		// The neutral's word read at Q31: its units are the param's, the curves' arithmetic the same for every format
		const auto neutral = Source::from_raw(kParamNeutral[p]);
		switch (kind) {
		case CurveKind::SQUARE:
			return deluge::dsp::fixed::curve_square(neutral, combination).raw();
		case CurveKind::LINEAR:
			return deluge::dsp::fixed::curve_linear(neutral, combination).raw();
		case CurveKind::BIPOLAR:
			return deluge::dsp::fixed::curve_bipolar(neutral, combination).raw();
		case CurveKind::EXPONENTIAL:
		case CurveKind::EXPONENTIAL_INVERTED:
		case CurveKind::RELEASE_TABLE:
			return getFinalParameterValueExpWithDumbEnvelopeHack(neutral, combination, p);
		}
		return combination.raw();
	}

	// The four-lane forms, each equal lane for lane to the scalar functions above.

	/// mul (smmul) for @p a never INT32_MIN: the doubling high multiply, then halved. NEON saturates only INT32_MIN
	/// times INT32_MIN; SIMDe's portable form, which the host and spec builds run, saturates whenever @p a is
	/// INT32_MIN.
	template <int A, int B>
	[[gnu::always_inline]] static dsp::QVec<A + B - 32> mul_by_doubling(dsp::QVec<A> a, dsp::QVec<B> b) {
		return as_floor<dsp::QVec<A + B - 32>>(mul_doubling_sat(a, b));
	}

	/// Four params' preset terms, from their presets and their (positive) param ranges.
	[[gnu::always_inline]] static CombinationLanes preset_terms(SourceLanes preset, DepthLanes range) {
		return mul_by_doubling(range, preset);
	}

	/// An uncabled multiply-chain param's combination, multiply_step(kOne, term) - kOne: the chain's Q29 step drops
	/// the term's low three bits and cannot saturate, as the term lies in [-2^30, 2^30]. An add chain's is the term.
	[[gnu::always_inline]] static CombinationLanes uncabled_multiply(CombinationLanes term) {
		return scale_shl_wrap<3>(scale_shr_floor<3>(term));
	}

	/// The square, linear and bipolar curves; the neutral values are positive.
	[[gnu::always_inline]] static SourceLanes curve_square(SourceLanes neutral, CombinationLanes combination) {
		const CombinationLanes positive = add_wrap(combination, CombinationLanes::splat(kOne));
		// Raw: the square is an integer product of the two truncated factors, Q27, as the scalar curve's
		const auto squared = dsp::QVec<27>::from_raw(positive.raw().ShiftRight<16>() * positive.raw().ShiftRight<15>());
		return as_wrap<SourceLanes>(clamp_bits<27>(mul_by_doubling(neutral, squared)));
	}
	[[gnu::always_inline]] static SourceLanes curve_linear(SourceLanes neutral, CombinationLanes combination) {
		const CombinationLanes positive = add_wrap(combination, CombinationLanes::splat(kOne));
		return as_wrap<SourceLanes>(clamp_bits<29>(mul_by_doubling(neutral, positive)));
	}
	[[gnu::always_inline]] static SourceLanes curve_bipolar(SourceLanes neutral, CombinationLanes combination) {
		// A quarter of the neutral plus half the combination, at Q29: two shifted values, so no wrap
		const CombinationLanes pre_limits =
		    add_wrap(as_floor<CombinationLanes>(neutral), scale_shr_floor<1>(combination));
		return as_wrap<SourceLanes>(clamp_bits<29>(pre_limits));
	}
};

} // namespace deluge::synth::matrix
