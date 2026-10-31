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

#include "definitions_cxx.hpp" // kMaxSampleValue
#include "modulation/params/param.h"
#include "synth/modulation/cable.h"

#include <array>
#include <cstdint>

/// @file
/// @brief What the matrix compiler reads from a source and a destination: how a cable converts its source, how a
/// destination combines its cables and shapes the result, and how a cable's depth is shaped: the instrument's patching
/// rules, as data.

namespace deluge::synth::matrix {

/// How a destination combines its cables.
enum class ChainKind : uint8_t {
	MULTIPLY, ///< a product of (1 + cable) terms, the preset first (volume and linear params)
	ADD,      ///< a saturating sum, the preset last (hybrid and exp params)
	DEPTH,    ///< a product without the range step: a cable's depth, read by the cables it controls
};

/// The curve that turns a destination's combination into its final value.
enum class CurveKind : uint8_t {
	SQUARE,               ///< neutral · (1 + c)²: the square law, up to 4 × neutral
	LINEAR,               ///< neutral · (1 + c), up to 2 × neutral
	BIPOLAR,              ///< a position around neutral, its combination doubled (pan, phase widths, wave indexes)
	EXPONENTIAL,          ///< neutral · 2^(c · 32 + 2): frequencies, pitches, rates
	EXPONENTIAL_INVERTED, ///< the exponential of −c: envelope attacks
	RELEASE_TABLE,        ///< the release-rate table: envelope decays and releases
};

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

/// The scope param @p p belongs to: 0 for the voice, 1 for the sound.
[[nodiscard]] constexpr uint8_t scope_of(int32_t p) {
	return p < modulation::params::FIRST_GLOBAL ? 0 : 1;
}

/// @brief One param's patching rules: the chain that combines its cables, the curve that shapes the result, and
/// whether it is a bipolar param (a position whose cables' sum doubles before the preset, so a full-depth cable reaches
/// end to end from any preset).
struct ParamRules {
	ChainKind chain{ChainKind::MULTIPLY};
	CurveKind curve{CurveKind::LINEAR};
	bool bipolar{false};
	int32_t neutral{0};     ///< the final value the curve gives a combination of zero is built on
	int32_t range{1 << 30}; ///< how far the preset reaches: its term is preset · range / 2^32
};

/// Every param's rules, set param by param: the table, not a param's position in the enum, decides.
inline constexpr std::array<ParamRules, modulation::params::kNumParams> kParamRules = [] {
	using namespace modulation::params;
	constexpr bool kBipolar = true;
	std::array<ParamRules, kNumParams> table{};
	auto set = [&](int32_t p, ChainKind chain, CurveKind curve, bool bipolar, int32_t neutral, int32_t range) {
		table[p] = {.chain = chain, .curve = curve, .bipolar = bipolar, .neutral = neutral, .range = range};
	};
	set(LOCAL_OSC_A_VOLUME, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 134217728, 1073741824);
	set(LOCAL_OSC_B_VOLUME, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 134217728, 1073741824);
	set(LOCAL_VOLUME, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 134217728, 1073741824);
	set(LOCAL_NOISE_VOLUME, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 134217728, 1073741824);
	set(LOCAL_MODULATOR_0_VOLUME, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 33554432, 1073741824);
	set(LOCAL_MODULATOR_1_VOLUME, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 33554432, 1073741824);
	// The square law on purpose: the fold amount's response, and its 4 × neutral headroom
	set(LOCAL_FOLD, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 268435450, 1073741824);
	set(LOCAL_MODULATOR_0_FEEDBACK, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 5931642, 1073741824);
	set(LOCAL_MODULATOR_1_FEEDBACK, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 5931642, 1073741824);
	set(LOCAL_CARRIER_0_FEEDBACK, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 5931642, 1073741824);
	set(LOCAL_CARRIER_1_FEEDBACK, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 5931642, 1073741824);
	set(LOCAL_LPF_RESONANCE, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 268435450, 1073741824);
	set(LOCAL_HPF_RESONANCE, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 268435450, 1073741824);
	set(LOCAL_ENV_0_SUSTAIN, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 1073741824, 1073741824);
	set(LOCAL_ENV_1_SUSTAIN, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 1073741824, 1073741824);
	set(LOCAL_ENV_2_SUSTAIN, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 1073741824, 1073741824);
	set(LOCAL_ENV_3_SUSTAIN, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 1073741824, 1073741824);
	set(LOCAL_LPF_MORPH, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 268435450, 1073741824);
	set(LOCAL_HPF_MORPH, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 268435450, 1073741824);
	set(LOCAL_OSC_A_PHASE_WIDTH, ChainKind::ADD, CurveKind::BIPOLAR, kBipolar, 0, 1073741824);
	set(LOCAL_OSC_B_PHASE_WIDTH, ChainKind::ADD, CurveKind::BIPOLAR, kBipolar, 0, 1073741824);
	set(LOCAL_OSC_A_WAVE_INDEX, ChainKind::ADD, CurveKind::BIPOLAR, kBipolar, 0, 1073741824);
	set(LOCAL_OSC_B_WAVE_INDEX, ChainKind::ADD, CurveKind::BIPOLAR, kBipolar, 0, 1073741824);
	set(LOCAL_PAN, ChainKind::ADD, CurveKind::BIPOLAR, kBipolar, 0, 1073741824);
	set(LOCAL_LPF_FREQ, ChainKind::ADD, CurveKind::EXPONENTIAL, false, 2000000, 751619276);
	set(LOCAL_PITCH_ADJUST, ChainKind::ADD, CurveKind::EXPONENTIAL, false, kMaxSampleValue, 536870912);
	set(LOCAL_OSC_A_PITCH_ADJUST, ChainKind::ADD, CurveKind::EXPONENTIAL, false, kMaxSampleValue, 536870912);
	set(LOCAL_OSC_B_PITCH_ADJUST, ChainKind::ADD, CurveKind::EXPONENTIAL, false, kMaxSampleValue, 536870912);
	set(LOCAL_MODULATOR_0_PITCH_ADJUST, ChainKind::ADD, CurveKind::EXPONENTIAL, false, kMaxSampleValue, 536870912);
	set(LOCAL_MODULATOR_1_PITCH_ADJUST, ChainKind::ADD, CurveKind::EXPONENTIAL, false, kMaxSampleValue, 536870912);
	set(LOCAL_HPF_FREQ, ChainKind::ADD, CurveKind::EXPONENTIAL, false, 2672947, 1073741824);
	set(LOCAL_LFO_LOCAL_FREQ_1, ChainKind::ADD, CurveKind::EXPONENTIAL, false, 121739, 1073741824);
	set(LOCAL_LFO_LOCAL_FREQ_2, ChainKind::ADD, CurveKind::EXPONENTIAL, false, 121739, 1073741824);
	set(LOCAL_ENV_0_ATTACK, ChainKind::ADD, CurveKind::EXPONENTIAL_INVERTED, false, 4096, 805306368);
	set(LOCAL_ENV_1_ATTACK, ChainKind::ADD, CurveKind::EXPONENTIAL_INVERTED, false, 4096, 805306368);
	set(LOCAL_ENV_2_ATTACK, ChainKind::ADD, CurveKind::EXPONENTIAL_INVERTED, false, 4096, 805306368);
	set(LOCAL_ENV_3_ATTACK, ChainKind::ADD, CurveKind::EXPONENTIAL_INVERTED, false, 4096, 805306368);
	set(LOCAL_ENV_0_DECAY, ChainKind::ADD, CurveKind::RELEASE_TABLE, false, 35840, 1073741824);
	set(LOCAL_ENV_1_DECAY, ChainKind::ADD, CurveKind::RELEASE_TABLE, false, 35840, 1073741824);
	set(LOCAL_ENV_2_DECAY, ChainKind::ADD, CurveKind::RELEASE_TABLE, false, 35840, 1073741824);
	set(LOCAL_ENV_3_DECAY, ChainKind::ADD, CurveKind::RELEASE_TABLE, false, 35840, 1073741824);
	set(LOCAL_ENV_0_RELEASE, ChainKind::ADD, CurveKind::RELEASE_TABLE, false, 71680, 1073741824);
	set(LOCAL_ENV_1_RELEASE, ChainKind::ADD, CurveKind::RELEASE_TABLE, false, 71680, 1073741824);
	set(LOCAL_ENV_2_RELEASE, ChainKind::ADD, CurveKind::RELEASE_TABLE, false, 71680, 1073741824);
	set(LOCAL_ENV_3_RELEASE, ChainKind::ADD, CurveKind::RELEASE_TABLE, false, 71680, 1073741824);
	set(GLOBAL_VOLUME_POST_FX, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 134217728, 1073741824);
	set(GLOBAL_VOLUME_POST_REVERB_SEND, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 134217728, 1073741824);
	set(GLOBAL_REVERB_AMOUNT, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 134217728, 1073741824);
	set(GLOBAL_MOD_FX_DEPTH, ChainKind::MULTIPLY, CurveKind::SQUARE, false, 526133494, 1073741824);
	set(GLOBAL_DELAY_FEEDBACK, ChainKind::MULTIPLY, CurveKind::LINEAR, false, 1073741824, 1073741824);
	set(GLOBAL_DELAY_RATE, ChainKind::ADD, CurveKind::EXPONENTIAL, false, kMaxSampleValue, 536870912);
	set(GLOBAL_MOD_FX_RATE, ChainKind::ADD, CurveKind::EXPONENTIAL, false, 121739, 1073741824);
	set(GLOBAL_LFO_FREQ_1, ChainKind::ADD, CurveKind::EXPONENTIAL, false, 121739, 1073741824);
	set(GLOBAL_LFO_FREQ_2, ChainKind::ADD, CurveKind::EXPONENTIAL, false, 121739, 1073741824);
	set(GLOBAL_ARP_RATE, ChainKind::ADD, CurveKind::EXPONENTIAL, false, kMaxSampleValue, 1073741824);
	return table;
}();

/// Each param's neutral value and preset range, laid out for loading four at a time.
inline constexpr std::array<int32_t, modulation::params::kNumParams> kParamNeutral = [] {
	std::array<int32_t, modulation::params::kNumParams> out{};
	for (size_t p = 0; p < out.size(); ++p) {
		out[p] = kParamRules[p].neutral;
	}
	return out;
}();
inline constexpr std::array<int32_t, modulation::params::kNumParams> kParamRange = [] {
	std::array<int32_t, modulation::params::kNumParams> out{};
	for (size_t p = 0; p < out.size(); ++p) {
		out[p] = kParamRules[p].range;
	}
	return out;
}();

/// The chain that combines param @p p's cables.
[[nodiscard]] constexpr ChainKind chain_of(int32_t p) {
	return kParamRules[p].chain;
}

/// The curve of param @p p.
[[nodiscard]] constexpr CurveKind curve_of(int32_t p) {
	return kParamRules[p].curve;
}

/// Whether param @p p is a bipolar param.
[[nodiscard]] constexpr bool is_bipolar(int32_t p) {
	return kParamRules[p].bipolar;
}

/// How a cable from @p from shapes its depth into param @p p (an add-chain destination; the other chains use the
/// depth as it is).
[[nodiscard]] constexpr DepthMode depth_mode_of(PatchSource from, int32_t p) {
	switch (p) {
	case modulation::params::LOCAL_PITCH_ADJUST:
		return from == PatchSource::VELOCITY ? DepthMode::SQUARED_PITCH_VELOCITY : DepthMode::SQUARED_PITCH_OTHER;
	case modulation::params::LOCAL_OSC_A_PITCH_ADJUST:
	case modulation::params::LOCAL_OSC_B_PITCH_ADJUST:
	case modulation::params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case modulation::params::LOCAL_MODULATOR_1_PITCH_ADJUST:
	case modulation::params::GLOBAL_DELAY_RATE:
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
