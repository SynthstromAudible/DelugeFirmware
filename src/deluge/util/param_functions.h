/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "util/fixedpoint.h" // signed_saturate, multiply_32x32_rshift32
#include <cstdint>

// The parameter scaling/patching math, split out of the grab-bag util/functions.h so the synth core
// (the Patcher) can reach it without dragging in functions.h's gui/colour include. functions.h re-exports
// this header, so its existing users are unaffected. (The definitions still live in functions.cpp; moving
// them into a standalone gui-free TU is deferred to the libdeluge-synth lib step, where getExp's own util
// dependencies get untangled too.)

// Per-param scaling tables, populated by functionsInit().
extern int32_t paramRanges[];
extern int32_t paramNeutralValues[];

// bits must be *less* than 32! I.e. 31 or less
[[gnu::always_inline]] inline int32_t signed_saturate_operand_unknown(int32_t val, int32_t bits) {

	// Despite having this switch at the per-audio-sample level, it doesn't introduce any slowdown compared to just
	// always saturating by the same amount!
	switch (bits) {
	case 31:
		return signed_saturate<31>(val);
	case 30:
		return signed_saturate<30>(val);
	case 29:
		return signed_saturate<29>(val);
	case 28:
		return signed_saturate<28>(val);
	case 27:
		return signed_saturate<27>(val);
	case 26:
		return signed_saturate<26>(val);
	case 25:
		return signed_saturate<25>(val);
	case 24:
		return signed_saturate<24>(val);
	case 23:
		return signed_saturate<23>(val);
	case 22:
		return signed_saturate<22>(val);
	case 21:
		return signed_saturate<21>(val);
	case 20:
		return signed_saturate<20>(val);
	case 19:
		return signed_saturate<19>(val);
	case 18:
		return signed_saturate<18>(val);
	case 17:
		return signed_saturate<17>(val);
	case 16:
		return signed_saturate<16>(val);
	case 15:
		return signed_saturate<15>(val);
	case 14:
		return signed_saturate<14>(val);
	case 13:
		return signed_saturate<13>(val);
	default:
		return signed_saturate<12>(val);
	}
}

template <uint8_t lshift>
[[gnu::always_inline]] inline int32_t lshiftAndSaturate(int32_t val) {
	return signed_saturate<32 - lshift>(val) << lshift;
}

// lshift must be greater than 0! Not 0
[[gnu::always_inline]] inline int32_t lshiftAndSaturateUnknown(int32_t val, uint8_t lshift) {
	return signed_saturate_operand_unknown(val, 32 - lshift) << lshift;
}

// Scale `number` by 2^magnitude (negative magnitude = right-shift), saturating on positive magnitude. Lives
// here (rather than functions.h) because getExp() in param_functions.cpp needs it gui-free.
[[gnu::always_inline]] inline int32_t increaseMagnitudeAndSaturate(int32_t number, int32_t magnitude) {
	if (magnitude > 0) {
		return lshiftAndSaturateUnknown(number, magnitude);
	}
	return number >> (-magnitude);
}

int32_t getExp(int32_t presetValue, int32_t adjustment);
int32_t lookupReleaseRate(int32_t input);

int32_t getFinalParameterValueVolume(int32_t paramNeutralValue, int32_t patchedValue);
int32_t getFinalParameterValueLinear(int32_t paramNeutralValue, int32_t patchedValue);
int32_t getFinalParameterValueHybrid(int32_t paramNeutralValue, int32_t patchedValue);
int32_t getFinalParameterValueExp(int32_t paramNeutralValue, int32_t patchedValue);
int32_t getFinalParameterValueExpWithDumbEnvelopeHack(int32_t paramNeutralValue, int32_t patchedValue, int32_t p);
