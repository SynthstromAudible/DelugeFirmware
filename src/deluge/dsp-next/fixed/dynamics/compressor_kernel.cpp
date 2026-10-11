/*
 * Copyright © 2023-2026 Mark Adams, Synthstrom Audible Limited
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

#include "dsp-next/fixed/dynamics/compressor_kernel.h"

#include "definitions_cxx.hpp" // kSampleRate

#include <algorithm>
#include <cmath>

namespace deluge::dsp::fixed {

float ratio_fraction(q31 ratio) {
	return 0.5f + (static_cast<float>(ratio) / ONE_Q31f) / 2;
}

float compressor_attack_ms(q31 attack) {
	// The exp runs from 1 to about 7.4, about 2.7 at half the knob.
	return 0.5 + (std::exp(2 * static_cast<float>(attack) / ONE_Q31f) - 1) * 10;
}

float compressor_release_ms(q31 release) {
	// The exp runs from 1 to about 7.4, about 2.7 at half the knob.
	return 50 + (std::exp(2 * static_cast<float>(release) / ONE_Q31f) - 1) * 50;
}

float compressor_ratio(q31 ratio) {
	return 1 / (1 - ratio_fraction(ratio));
}

float compressor_sidechain_hz(q31 sidechain_hpf) {
	// The exp runs from 1 to about 4.5: about 33 Hz at half the knob, 104 Hz at the top.
	return (std::exp(1.5 * static_cast<float>(sidechain_hpf) / ONE_Q31f) - 1) * 30;
}

Compressor::Coefficients Compressor::coefficients(const Settings& s) {
	const CompressorKnobs& k = s.knobs;
	const float attack_ms = compressor_attack_ms(k.attack);
	const float release_ms = compressor_release_ms(k.release);
	const float fc = compressor_sidechain_hz(k.sidechain_hpf) / static_cast<float>(kSampleRate);
	const float wc = fc / (1 + fc);
	return {
	    .attack_rate = (-1000.0f / kSampleRate) / attack_ms,
	    .release_rate = (-1000.0f / kSampleRate) / release_ms,
	    .attack_ms = attack_ms,
	    .release_ms = release_ms,
	    .fraction = ratio_fraction(k.ratio),
	    .base_gain = s.base_gain,
	    .detector_moveability = static_cast<q31>(wc * static_cast<float>(ONE_Q31)),
	    // ONE_Q31 is fully wet: the blend is no modulated param, so it needs no headroom.
	    .dry = ONE_Q31 - k.blend,
	    .wet = k.blend,
	    .mode = k.blend == ONE_Q31 ? Mode::WET : Mode::BLEND,
	};
}

void Compressor::reset(State& s) {
	s.reduction_envelope = 0;
	s.makeup = 0;
	s.mean = 0;
	s.seeded = false;
}

void Compressor::set_levels(State& s, const Levels& l) {
	s.levels = l;
	s.threshold = 1 - 0.8f * (static_cast<float>(l.threshold) / ONE_Q31f);
}

void Compressor::seed(State& s, StereoSample first) {
	AntialiasedTanh::seed(kSaturator, s.saturation_l, first.l);
	AntialiasedTanh::seed(kSaturator, s.saturation_r, first.r);
	s.seeded = true;
}

void Compressor::finish_level(const Coefficients& /*c*/, State& s) {
	const float last_mean = s.mean;
	const auto ns = static_cast<float>(s.n * 2);
	s.mean = (static_cast<float>(s.sum) / ONE_Q31f) / ns;
	// A one-pole IIR rather than a long FIR: close, and far cheaper. The more samples in the block, the more weight on
	// its own mean, so the response keeps its speed when the blocks grow under load.
	s.mean = (s.mean * ns + last_mean) / (1 + ns);
	const float rms = static_cast<float>(ONE_Q31) * std::sqrt(s.mean);

	// 0 to about 21.5 (full scale); the DAC clips at about 16.
	s.log_rms = std::log(std::max(rms, 1.0f));
}

} // namespace deluge::dsp::fixed
