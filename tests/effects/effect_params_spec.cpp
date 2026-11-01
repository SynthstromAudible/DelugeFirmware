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

// Each effect's params from its words against the arithmetic the app's adapters used before the effects defined their
// params (model/fx/chain_inputs.cpp, model/global_effectable/global_effectable.cpp's delay working state,
// model/fx/song_stutter.cpp's stutter rate, as they read at 3b854650f), copied with each param's neutral written out:
// param_table_check pins those literals to the instrument's neutral table.

#include "effects/bitcrush.h"
#include "effects/compressor.h"
#include "effects/delay.h"
#include "effects/equalizer.h"
#include "effects/filter.h"
#include "effects/mod_fx.h"
#include "effects/reverb.h"
#include "effects/stutter.h"

#include "dsp-next/fixed/curves/param_curve.h"

#include <cppspec.hpp>
#include <cstdint>

using namespace deluge::effects;
using deluge::dsp::fixed::curve_exponential;
using deluge::dsp::fixed::curve_linear;
using deluge::dsp::fixed::curve_square;

namespace {
namespace reference {
FilterParams filter(const Filter::Words& w, bool allowOversampling) {
	return {
	    .lpf_frequency = curve_exponential(2000000, w.lpf_frequency >> 2),
	    .lpf_resonance = curve_linear(268435450, w.lpf_resonance >> 2),
	    .lpf_morph = curve_linear(268435450, w.lpf_morph >> 2),
	    .hpf_frequency = curve_exponential(2672947, w.hpf_frequency >> 2),
	    .hpf_resonance = curve_linear(268435450, w.hpf_resonance >> 2),
	    .hpf_morph = curve_linear(268435450, w.hpf_morph >> 2),
	    .lpf_frequency_param = w.lpf_frequency,
	    .lpf_morph_param = w.lpf_morph,
	    .hpf_frequency_param = w.hpf_frequency,
	    .hpf_morph_param = w.hpf_morph,
	    .allow_oversampling = allowOversampling,
	};
}
ModFx::Params modFx(const ModFx::Words& w) {
	return {.rate = curve_exponential(121739, w.rate >> 2),
	        .depth = curve_square(526133494, w.depth >> 2),
	        .offset = w.offset,
	        .feedback = w.feedback};
}
int32_t clipReverbSend(int32_t word, int32_t reverbAmountAdjust) {
	return curve_square(reverbAmountAdjust, word >> 2);
}
int32_t masterReverbSend(int32_t word) {
	return curve_square(134217728, word >> 2);
}
int32_t delayFeedback(int32_t word) {
	return curve_linear(1073741824, word >> 2);
}
int32_t delayRate(int32_t word) {
	return curve_exponential(16777216, word >> 2);
}
int32_t stutterRate(int32_t word) {
	return curve_exponential(16777216, word >> 2);
}
} // namespace reference

/// Words a sweep can't miss: the ends, around zero, the "off" sentinels, and a deterministic spread; @p f gets one
/// word per call, a different stream per @p seed.
template <class F>
void each_word(uint32_t seed, F f) {
	for (int64_t v : {int64_t{INT32_MIN}, int64_t{INT32_MIN} + 1, int64_t{-1}, int64_t{0}, int64_t{1}, int64_t{1} << 30,
	                  -(int64_t{1} << 30), int64_t{INT32_MAX} - 45, int64_t{INT32_MAX}}) {
		f(static_cast<int32_t>(v));
	}
	uint32_t x = seed;
	for (int i = 0; i < 20'000; ++i) {
		x = x * 1664525u + 1013904223u;
		f(static_cast<int32_t>(x));
	}
}

/// A second word for the same call, from another stream.
struct Stream {
	uint32_t x;
	int32_t next() {
		x = x * 22695477u + 1u;
		return static_cast<int32_t>(x);
	}
};

bool same(const FilterParams& a, const FilterParams& b) {
	return a.lpf_frequency == b.lpf_frequency && a.lpf_resonance == b.lpf_resonance && a.lpf_morph == b.lpf_morph
	       && a.hpf_frequency == b.hpf_frequency && a.hpf_resonance == b.hpf_resonance && a.hpf_morph == b.hpf_morph
	       && a.lpf_frequency_param == b.lpf_frequency_param && a.lpf_morph_param == b.lpf_morph_param
	       && a.hpf_frequency_param == b.hpf_frequency_param && a.hpf_morph_param == b.hpf_morph_param
	       && a.allow_oversampling == b.allow_oversampling;
}
} // namespace

// clang-format off
describe effect_params("an effect's params from its words", ${
	it("gives the filters' values as the chain inputs did", _ {
		int32_t bad = 0;
		Stream s{7};
		each_word(0x9E3779B9u, [&](int32_t w) {
			const Filter::Words words{w, s.next(), s.next(), s.next(), s.next(), s.next()};
			const Filter::Words rotated{s.next(), w, s.next(), w, s.next(), w};
			for (bool oversampling : {false, true}) {
				bad += !same(Filter::params_from(words, oversampling), reference::filter(words, oversampling));
				bad += !same(Filter::params_from(rotated, oversampling), reference::filter(rotated, oversampling));
			}
		});
		expect(bad).to_equal(0);
	});
	it("gives the mod FX's values as the chain inputs did", _ {
		int32_t bad = 0;
		Stream s{11};
		each_word(0x85EBCA6Bu, [&](int32_t w) {
			for (const ModFx::Words words : {ModFx::Words{w, s.next(), s.next(), s.next()},
			                                 ModFx::Words{s.next(), w, s.next(), s.next()}}) {
				const ModFx::Params ours = ModFx::params_from(words);
				const ModFx::Params theirs = reference::modFx(words);
				bad += ours.rate != theirs.rate || ours.depth != theirs.depth || ours.offset != theirs.offset
				       || ours.feedback != theirs.feedback;
			}
		});
		expect(bad).to_equal(0);
	});
	it("gives the reverb send, the delay's rate and feedback and the stutter's rate as before", _ {
		int32_t bad = 0;
		Stream s{13};
		each_word(0xC2B2AE35u, [&](int32_t w) {
			const int32_t adjust = s.next() & 0x7FFFFFFF;
			bad += final_value(Reverb::kSend, w, adjust) != reference::clipReverbSend(w, adjust);
			bad += final_value(Reverb::kSend, w) != reference::masterReverbSend(w);
			bad += final_value(Delay::kFeedback, w) != reference::delayFeedback(w);
			bad += final_value(Delay::kRate, w) != reference::delayRate(w);
			bad += final_value(Stutter::kRate, w) != reference::stutterRate(w);
		});
		expect(bad).to_equal(0);
	});
	it("uses the pass-through params' words as stored", _ {
		int32_t bad = 0;
		each_word(0x27D4EB2Fu, [&](int32_t w) {
			for (const EffectParam& p : Equalizer::kParams) {
				bad += final_value(p, w) != w;
			}
			for (const EffectParam& p : Bitcrush::kParams) {
				bad += final_value(p, w) != w;
			}
			bad += final_value(Compressor::kThreshold, w) != w;
		});
		expect(bad).to_equal(0);
	});
});
// clang-format on

CPPSPEC_SPEC(effect_params)
