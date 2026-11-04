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

namespace deluge::synth::matrix {

using dsp::q;
using dsp::Q;

/// @brief The matrix's q31 numeric core: the instrument's patching arithmetic, operation for operation. Each scalar
/// function is the reference its four-lane form is proven against.
struct MatrixFixed {
	/// Four q31 lanes.
	using Lanes = Argon<int32_t>;

	/// "1" in the chains' Q29 and the depth results' scale.
	static constexpr int32_t kOne = 536870912;

	/// A source value as a cable reads it.
	[[gnu::always_inline]] static int32_t convert(Conversion conversion, int32_t value) {
		constexpr int32_t kHalf = std::numeric_limits<int32_t>::max() / 2;
		switch (conversion) {
		case Conversion::AS_IS:
			return value;
		case Conversion::UNIPOLAR: // half the bipolar Q31, rounded toward zero, centred one LSB under a half
			return add_wrap(halve_toward_zero(Q<31>::from_raw(value)), Q<31>::from_raw(kHalf)).raw();
		case Conversion::AFTERTOUCH_BIPOLAR: // the unipolar Q31 centred and doubled, wrapping
			return scale_shl_wrap<1>(sub_wrap(Q<31>::from_raw(value), Q<31>::from_raw(kHalf))).raw();
		case Conversion::AFTERTOUCH_DEPTH: // the same about the true half
			return scale_shl_wrap<1>(sub_wrap(Q<31>::from_raw(value), q<31>(0.5))).raw();
		}
		return value;
	}

	/// A cable's depth, shaped by @p mode.
	[[gnu::always_inline]] static int32_t depth(DepthMode mode, int32_t d) {
		if (mode == DepthMode::PLAIN) {
			return d;
		}
		// The Q30 depth squared to Q29, its sign kept: raw, an integer product of the two truncated factors
		int32_t output = (d >> 15) * (d >> 16);
		if (d < 0) {
			output = -output;
		}
		if (mode == DepthMode::SQUARED_PITCH_VELOCITY) { // times 2/3
			return mul_r_as_wrap<Q<29>>(Q<29>::from_raw(output), Q<31>::from_raw(1431655765)).raw();
		}
		if (mode == DepthMode::SQUARED_PITCH_OTHER) { // times 1/√2
			return mul_r_as_wrap<Q<29>>(Q<29>::from_raw(output), Q<31>::from_raw(1518500250)).raw();
		}
		return output;
	}

	/// A cable's term: its source scaled by its depth, then by its depth result (@p range, "1" when none controls it).
	[[gnu::always_inline]] static int32_t product(int32_t source, int32_t depth, int32_t range) {
		return apply_range(mul(Q<31>::from_raw(source), Q<30>::from_raw(depth)).raw(), range);
	}

	/// A depth cable's term: its Q31 source scaled by its Q30 depth, Q29, with no range step.
	[[gnu::always_inline]] static int32_t depth_product(int32_t source, int32_t depth) {
		return mul(Q<31>::from_raw(source), Q<30>::from_raw(depth)).raw();
	}

	/// One step of a multiply chain: the running Q29 product times (1 + @p term), Q26, held to [−4, 4) and back at Q29.
	/// @details A term lies in [−2^29, 2^29] (apply_range holds a cable's to "1", a preset's is at most half its Q30
	/// range), so 1 + term lies in [0, 2^30] and its sum cannot wrap.
	[[gnu::always_inline]] static int32_t multiply_step(int32_t total, int32_t term) {
		const Q<29> factor = add_wrap(Q<29>::from_raw(term), Q<29>::from_raw(kOne));
		return as_wrap<Q<29>>(clamp_bits<29>(mul(Q<29>::from_raw(total), factor))).raw();
	}

	/// One step of an add chain: a saturating Q29 sum.
	[[gnu::always_inline]] static int32_t add_step(int32_t total, int32_t term) {
		return add_sat(Q<29>::from_raw(total), Q<29>::from_raw(term)).raw();
	}

	/// The preset's term for param @p p: the Q31 preset times the param's Q30 range, Q29.
	[[gnu::always_inline]] static int32_t preset_term(int32_t preset, int32_t p) {
		return mul(Q<31>::from_raw(preset), Q<30>::from_raw(kParamRange[p])).raw();
	}

	/// An add chain's doubling for a wavetable index: its cables reach twice as far, held to the 31-bit range first, so
	/// the top is 2^31 − 2.
	[[gnu::always_inline]] static int32_t double_saturating(int32_t total) {
		return scale_shl_wrap<1>(clamp_bits<31>(Q<29>::from_raw(total))).raw();
	}

	/// A depth destination's result from its chain's combination.
	[[gnu::always_inline]] static int32_t depth_result(int32_t combination) {
		return deluge::dsp::fixed::curve_linear(kOne, combination);
	}

	/// Param @p p's final value from its chain's combination.
	[[gnu::always_inline]] static int32_t curve(CurveKind kind, int32_t p, int32_t combination) {
		const int32_t neutral = kParamNeutral[p];
		switch (kind) {
		case CurveKind::SQUARE:
			return deluge::dsp::fixed::curve_square(neutral, combination);
		case CurveKind::LINEAR:
			return deluge::dsp::fixed::curve_linear(neutral, combination);
		case CurveKind::BIPOLAR:
			return deluge::dsp::fixed::curve_bipolar(neutral, combination);
		case CurveKind::EXPONENTIAL:
		case CurveKind::EXPONENTIAL_INVERTED:
		case CurveKind::RELEASE_TABLE:
			return getFinalParameterValueExpWithDumbEnvelopeHack(neutral, combination, p);
		}
		return combination;
	}

	// The four-lane forms, each equal lane for lane to the scalar functions above.

	/// smmul (multiply_32x32_rshift32) for @p a never INT32_MIN: the doubling high multiply, then an arithmetic shift
	/// right by 1. NEON saturates only INT32_MIN times INT32_MIN; SIMDe's portable form, which the host and spec
	/// builds run, saturates whenever @p a is INT32_MIN.
	/// The lanes are words in the scalar callers' formats; read as Q31, the doubled high word moved back to Q30 is
	/// their product's high word.
	[[gnu::always_inline]] static Lanes smmul_by_doubling(Lanes a, Lanes b) {
		return as_floor<dsp::QVec<30>>(mul_doubling_sat(dsp::QVec<31>::from_raw(a), dsp::QVec<31>::from_raw(b))).raw();
	}

	/// signed_saturate<bits>, then a shift left by @p n: the lane twin of clamp_bits then as_wrap, raw, as no vector
	/// clamp is named.
	template <int bits, int n>
	[[gnu::always_inline]] static Lanes saturate_shift(Lanes v) {
		return v.Max(Lanes{-(int32_t{1} << (bits - 1))}).Min(Lanes{(int32_t{1} << (bits - 1)) - 1}).ShiftLeft<n>();
	}

	/// Four params' preset terms, from their presets and their (positive) param ranges.
	[[gnu::always_inline]] static Lanes preset_terms(Lanes preset, Lanes range) {
		return smmul_by_doubling(range, preset);
	}

	/// An uncabled multiply-chain param's combination, multiply_step(kOne, term) - kOne: the chain's Q29 step drops
	/// the term's low three bits and cannot saturate, as the term lies in [-2^30, 2^30]. An add chain's is the term.
	[[gnu::always_inline]] static Lanes uncabled_multiply(Lanes term) { return term & Lanes{~int32_t{7}}; }

	/// The square, linear and bipolar curves; the neutral values are positive.
	[[gnu::always_inline]] static Lanes curve_square(Lanes neutral, Lanes combination) {
		const Lanes positive =
		    add_wrap(dsp::QVec<29>::from_raw(combination), dsp::QVec<29>::from_raw(Lanes{kOne})).raw();
		// Raw: the square is an integer product of the two truncated factors, as the scalar curve's
		const Lanes squared = positive.ShiftRight<16>() * positive.ShiftRight<15>();
		return saturate_shift<27, 5>(smmul_by_doubling(neutral, squared));
	}
	[[gnu::always_inline]] static Lanes curve_linear(Lanes neutral, Lanes combination) {
		const Lanes positive =
		    add_wrap(dsp::QVec<29>::from_raw(combination), dsp::QVec<29>::from_raw(Lanes{kOne})).raw();
		return saturate_shift<29, 3>(smmul_by_doubling(neutral, positive));
	}
	[[gnu::always_inline]] static Lanes curve_bipolar(Lanes neutral, Lanes combination) {
		return saturate_shift<29, 2>(neutral.ShiftRight<2>() + combination.ShiftRight<1>());
	}
};

} // namespace deluge::synth::matrix
