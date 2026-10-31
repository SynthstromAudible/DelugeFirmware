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

#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32(_rounded), add_saturate, lshift_and_saturate
#include "synth/matrix/descriptors.h"
#include "synth/modulation/cable_depth.h" // apply_range
#include "util/param_functions.h"         // paramRanges, getFinalParameterValue*

#include <argon.hpp>
#include <cstdint>
#include <limits>

namespace deluge::synth::matrix {

/// @brief The matrix's q31 numeric core: today's patching arithmetic, operation for operation. Each function is the
/// scalar reference the vector forms are proven against.
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
		case Conversion::UNIPOLAR:
			return (value / 2) + kHalf;
		case Conversion::AFTERTOUCH_BIPOLAR:
			return static_cast<int32_t>(static_cast<uint32_t>(value - kHalf) * 2u);
		case Conversion::AFTERTOUCH_DEPTH:
			return static_cast<int32_t>(static_cast<uint32_t>(value - 1073741824) << 1);
		}
		return value;
	}

	/// A cable's depth, shaped by @p mode.
	[[gnu::always_inline]] static int32_t depth(DepthMode mode, int32_t d) {
		if (mode == DepthMode::PLAIN) {
			return d;
		}
		int32_t output = (d >> 15) * (d >> 16);
		if (d < 0) {
			output = -output;
		}
		if (mode == DepthMode::SQUARED_PITCH_VELOCITY) {
			return multiply_32x32_rshift32_rounded(output, 1431655765) << 1;
		}
		if (mode == DepthMode::SQUARED_PITCH_OTHER) {
			return multiply_32x32_rshift32_rounded(output, 1518500250) << 1;
		}
		return output;
	}

	/// A cable's term: its source scaled by its depth, then by its depth result (@p range, "1" when none controls it).
	[[gnu::always_inline]] static int32_t product(int32_t source, int32_t depth, int32_t range) {
		return apply_range(multiply_32x32_rshift32(source, depth), range);
	}

	/// A depth cable's term: its source scaled by its depth, with no range step.
	[[gnu::always_inline]] static int32_t depth_product(int32_t source, int32_t depth) {
		return multiply_32x32_rshift32(source, depth);
	}

	/// One step of a multiply chain: the running product times (1 + @p term), limited to "4".
	[[gnu::always_inline]] static int32_t multiply_step(int32_t total, int32_t term) {
		return lshift_and_saturate<3>(multiply_32x32_rshift32(total, term + kOne));
	}

	/// One step of an add chain: a saturating sum.
	[[gnu::always_inline]] static int32_t add_step(int32_t total, int32_t term) { return add_saturate(total, term); }

	/// The preset's term for param @p p.
	[[gnu::always_inline]] static int32_t preset_term(int32_t preset, int32_t p) {
		return multiply_32x32_rshift32(preset, paramRanges[p]);
	}

	/// An add chain's doubling for a wavetable index: its cables reach twice as far.
	[[gnu::always_inline]] static int32_t double_saturating(int32_t total) { return lshift_and_saturate<1>(total); }

	/// A depth destination's result from its chain's combination.
	[[gnu::always_inline]] static int32_t depth_result(int32_t combination) {
		return getFinalParameterValueLinear(kOne, combination);
	}

	/// Param @p p's final value from its chain's combination.
	[[gnu::always_inline]] static int32_t curve(CurveKind kind, int32_t p, int32_t combination) {
		const int32_t neutral = paramNeutralValues[p];
		switch (kind) {
		case CurveKind::VOLUME:
			return getFinalParameterValueVolume(neutral, combination);
		case CurveKind::LINEAR:
			return getFinalParameterValueLinear(neutral, combination);
		case CurveKind::HYBRID:
			return getFinalParameterValueHybrid(neutral, combination);
		case CurveKind::EXP:
		case CurveKind::EXP_ATTACK:
		case CurveKind::RELEASE_RATE:
			return getFinalParameterValueExpWithDumbEnvelopeHack(neutral, combination, p);
		}
		return combination;
	}

	// The four-lane forms, each equal lane for lane to the scalar functions above.

	/// smmul (multiply_32x32_rshift32) for @p a never INT32_MIN: the doubling high multiply, then an arithmetic shift
	/// right by 1. NEON saturates only INT32_MIN times INT32_MIN; SIMDe's portable form, which the host and spec
	/// builds run, saturates whenever @p a is INT32_MIN.
	[[gnu::always_inline]] static Lanes smmul_by_doubling(Lanes a, Lanes b) {
		return a.MultiplyFixedQMax(b).ShiftRight<1>();
	}

	/// signed_saturate<bits>, then a shift left by @p n.
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

	/// The volume, linear and hybrid curves; the neutral values are positive.
	[[gnu::always_inline]] static Lanes curve_volume(Lanes neutral, Lanes combination) {
		const Lanes positive = combination + Lanes{kOne};
		const Lanes squared = positive.ShiftRight<16>() * positive.ShiftRight<15>();
		return saturate_shift<27, 5>(smmul_by_doubling(neutral, squared));
	}
	[[gnu::always_inline]] static Lanes curve_linear(Lanes neutral, Lanes combination) {
		return saturate_shift<29, 3>(smmul_by_doubling(neutral, combination + Lanes{kOne}));
	}
	[[gnu::always_inline]] static Lanes curve_hybrid(Lanes neutral, Lanes combination) {
		return saturate_shift<29, 2>(neutral.ShiftRight<2>() + combination.ShiftRight<1>());
	}
};

} // namespace deluge::synth::matrix
