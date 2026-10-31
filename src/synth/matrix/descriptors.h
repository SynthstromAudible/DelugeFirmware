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

#include "modulation/params/param.h"
#include "synth/modulation/cable.h"

#include <cstdint>

/// @file
/// @brief What the matrix compiler reads from a source and a destination: how a cable converts its source, how a
/// destination combines its cables and shapes the result, and how a cable's depth is shaped. Today's patching rules,
/// as data.

namespace deluge::synth::matrix {

/// How a destination combines its cables.
enum class ChainKind : uint8_t {
	MULTIPLY, ///< a product of (1 + cable) terms, the preset first (volume and linear params)
	ADD,      ///< a saturating sum, the preset last (hybrid and exp params)
	DEPTH,    ///< a product without the range step: a cable's depth, read by the cables it controls
};

/// The curve that turns a destination's combination into its final value.
enum class CurveKind : uint8_t { VOLUME, LINEAR, HYBRID, EXP, EXP_ATTACK, RELEASE_RATE };
inline constexpr int32_t kNumCurveKinds = 6;

/// How a cable's depth is shaped before it scales the source.
enum class DepthMode : uint8_t {
	PLAIN,
	SQUARED,                ///< (d >> 15) · (d >> 16), keeping d's sign: small depths stay small
	SQUARED_PITCH_VELOCITY, ///< squared, then scaled so velocity's steps are semitones
	SQUARED_PITCH_OTHER,    ///< squared, then scaled to three octaves
};

/// How a cable reads its source.
enum class Conversion : uint8_t {
	AS_IS,              ///< a bipolar cable, Y, or aftertouch read unipolar
	UNIPOLAR,           ///< v / 2 + (2^31 - 1) / 2, C's truncating division
	AFTERTOUCH_BIPOLAR, ///< (v - (2^31 - 1) / 2) · 2
	AFTERTOUCH_DEPTH,   ///< (v - 2^30) << 1: aftertouch on a depth cable, whatever its polarity
};

namespace params = modulation::params;

/// The scope param @p p belongs to: 0 for the voice, 1 for the sound.
[[nodiscard]] constexpr uint8_t scope_of(int32_t p) {
	return p < params::FIRST_GLOBAL ? 0 : 1;
}

/// The chain that combines param @p p's cables.
[[nodiscard]] constexpr ChainKind chain_of(int32_t p) {
	const int32_t first_hybrid =
	    scope_of(p) == 0 ? int32_t{params::FIRST_LOCAL__HYBRID} : int32_t{params::FIRST_GLOBAL_HYBRID};
	return p < first_hybrid ? ChainKind::MULTIPLY : ChainKind::ADD;
}

/// The curve of param @p p.
[[nodiscard]] constexpr CurveKind curve_of(int32_t p) {
	const bool voice = scope_of(p) == 0;
	const int32_t first_non_volume =
	    voice ? int32_t{params::FIRST_LOCAL_NON_VOLUME} : int32_t{params::FIRST_GLOBAL_NON_VOLUME};
	const int32_t first_hybrid = voice ? int32_t{params::FIRST_LOCAL__HYBRID} : int32_t{params::FIRST_GLOBAL_HYBRID};
	const int32_t first_exp = voice ? int32_t{params::FIRST_LOCAL_EXP} : int32_t{params::FIRST_GLOBAL_EXP};
	if (p < first_non_volume) {
		return CurveKind::VOLUME;
	}
	if (p < first_hybrid) {
		return CurveKind::LINEAR;
	}
	if (p < first_exp) {
		return CurveKind::HYBRID;
	}
	if (params::LOCAL_ENV_0_DECAY <= p && p <= params::LOCAL_ENV_3_RELEASE) {
		return CurveKind::RELEASE_RATE;
	}
	if (params::LOCAL_ENV_0_ATTACK <= p && p <= params::LOCAL_ENV_3_ATTACK) {
		return CurveKind::EXP_ATTACK;
	}
	return CurveKind::EXP;
}

/// How a cable from @p from shapes its depth into param @p p (an add-chain destination; the other chains use the
/// depth as it is).
[[nodiscard]] constexpr DepthMode depth_mode_of(PatchSource from, int32_t p) {
	switch (p) {
	case params::LOCAL_PITCH_ADJUST:
		return from == PatchSource::VELOCITY ? DepthMode::SQUARED_PITCH_VELOCITY : DepthMode::SQUARED_PITCH_OTHER;
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
	case params::GLOBAL_DELAY_RATE:
		return DepthMode::SQUARED;
	default:
		return DepthMode::PLAIN;
	}
}

/// How a cable of @p polarity from @p from reads its source; @p depth_cable for a cable that controls another's
/// depth.
[[nodiscard]] constexpr Conversion conversion_of(PatchSource from, CablePolarity polarity, bool depth_cable) {
	if (from == PatchSource::AFTERTOUCH) {
		if (depth_cable) {
			return Conversion::AFTERTOUCH_DEPTH;
		}
		return polarity == CablePolarity::UNIPOLAR ? Conversion::AS_IS : Conversion::AFTERTOUCH_BIPOLAR;
	}
	if (from == PatchSource::Y || polarity == CablePolarity::BIPOLAR) {
		return Conversion::AS_IS;
	}
	return Conversion::UNIPOLAR;
}

} // namespace deluge::synth::matrix
