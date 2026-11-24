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
// The sample read's four-frame mix (dsp/fixed/sample_read/quad_mix.h) against the scalar reads' per-frame mix: the
// amplitude stepped before each frame, the cache's top three bytes of each pre-condense word, the condense, and
// mixed_in, for every layout and a last call of 1 to 3 frames.

#include "dsp/fixed/sample_read/quad_mix.h"

#include "dsp/core/fixed_point.h"
#include "dsp/core/fixed_point_neon.h"
#include "dsp/fixed/noise/cong.h"
#include "dsp/fixed/sample_read/mixed_in.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "support/spec_main.h"

namespace {
using deluge::dsp::add_wrap;
using deluge::dsp::halve_floor;
using deluge::dsp::halve_toward_zero;
using deluge::dsp::Q;
using deluge::dsp::QVec;
using deluge::dsp::fixed::Cong;
using deluge::dsp::fixed::sample_read::Condense;
using deluge::dsp::fixed::sample_read::mixed_in;
using deluge::dsp::fixed::sample_read::QuadMix;

constexpr int32_t kFrames = 64;

struct Words {
	std::vector<Q<31>> l;
	std::vector<Q<31>> r;
	std::vector<Q<31>> out;
};

Words random_words(uint32_t seed, int32_t out_channels) {
	Cong c{seed};
	Words w;
	for (int32_t i = 0; i < kFrames; i++) {
		w.l.push_back(Q<31>::from_raw(static_cast<int32_t>(c.next())));
		w.r.push_back(Q<31>::from_raw(static_cast<int32_t>(c.next())));
	}
	for (int32_t i = 0; i < (kFrames + 4) * out_channels; i++) {
		w.out.push_back(Q<31>::from_raw(static_cast<int32_t>(c.next()) >> 1));
	}
	return w;
}

void cache_word(std::vector<std::byte>& to, Q<31> word) {
	const auto w = static_cast<uint32_t>(word.raw());
	to.push_back(static_cast<std::byte>(w >> 8));
	to.push_back(static_cast<std::byte>(w >> 16));
	to.push_back(static_cast<std::byte>(w >> 24));
}

/// The scalar mix of @p frames frames, as the reads mix each.
template <int Channels, int OutChannels, Condense C>
Q<31> scalar(Words& w, int32_t frames, Q<31> amplitude, Q<31> step, std::vector<std::byte>& cache) {
	for (int32_t f = 0; f < frames; f++) {
		amplitude = add_wrap(amplitude, step);
		Q<31> l = w.l[f];
		const Q<31> r = w.r[f];
		cache_word(cache, l);
		if constexpr (Channels == 2) {
			cache_word(cache, r);
		}
		if constexpr (Channels == 2 && OutChannels == 1) {
			if constexpr (C == Condense::TOWARD_ZERO) {
				l = add_wrap(halve_toward_zero(l), halve_toward_zero(r));
			}
			else {
				l = add_wrap(halve_floor(l), halve_floor(r));
			}
		}
		w.out[f * OutChannels] = mixed_in(w.out[f * OutChannels], l, amplitude);
		if constexpr (OutChannels == 2) {
			w.out[(f * 2) + 1] = mixed_in(w.out[(f * 2) + 1], r, amplitude);
		}
	}
	return amplitude;
}

QVec<31> load4(const std::vector<Q<31>>& v, int32_t at) {
	std::array<int32_t, 4> raw{};
	for (int32_t i = 0; i < 4; i++) {
		raw[i] = at + i < static_cast<int32_t>(v.size()) ? v[at + i].raw() : 0x7777'7777;
	}
	return QVec<31>::from_raw(vld1q_s32(raw.data()));
}

/// The quad mix of @p frames frames against the scalar: "" when the output, cache bytes and amplitude agree.
template <int Channels, int OutChannels, Condense C>
std::string compare(uint32_t seed, int32_t frames, Q<31> amplitude, Q<31> step, bool cache) {
	Words a = random_words(seed, OutChannels);
	Words b = a;
	std::vector<std::byte> want_cache;
	const Q<31> want_amp = scalar<Channels, OutChannels, C>(a, frames, amplitude, step, want_cache);
	std::vector<std::byte> got_cache(static_cast<std::size_t>((frames * Channels * 3) + 16), std::byte{0xEE});
	QuadMix<Channels, OutChannels, C> mix(b.out.data(), amplitude, step, cache ? got_cache.data() : nullptr);
	for (int32_t f = 0; f < frames; f += 4) {
		mix.put(load4(b.l, f), load4(b.r, f), frames - f < 4 ? frames - f : 4);
	}
	const std::string where =
	    std::to_string(Channels) + "->" + std::to_string(OutChannels) + " frames " + std::to_string(frames);
	if (a.out != b.out) {
		return "output differs: " + where;
	}
	if (mix.amplitude().raw() != want_amp.raw()) {
		return "amplitude differs: " + where;
	}
	if (cache) {
		const std::size_t bytes = want_cache.size();
		if (!std::equal(want_cache.begin(), want_cache.end(), got_cache.begin())
		    || mix.cache() != got_cache.data() + bytes || got_cache[bytes] != std::byte{0xEE}) {
			return "cache differs: " + where;
		}
	}
	else if (mix.cache() != nullptr) {
		return "cache moved with none: " + where;
	}
	return "";
}

template <int Channels, int OutChannels, Condense C>
std::string every_count(Q<31> amplitude, Q<31> step, bool cache) {
	std::string bad;
	for (int32_t frames = 1; frames <= kFrames && bad.empty(); frames++) {
		bad = compare<Channels, OutChannels, C>(0xC0FF'EE00U + static_cast<uint32_t>(frames), frames, amplitude, step,
		                                        cache);
	}
	return bad;
}

std::string every_layout(Q<31> amplitude, Q<31> step, bool cache) {
	std::string bad = every_count<1, 1, Condense::NONE>(amplitude, step, cache);
	bad += every_count<2, 2, Condense::NONE>(amplitude, step, cache);
	bad += every_count<2, 1, Condense::TOWARD_ZERO>(amplitude, step, cache);
	bad += every_count<2, 1, Condense::FLOOR>(amplitude, step, cache);
	return bad;
}

} // namespace

// clang-format off
describe sample_read_quad_mix_spec_body("deluge::dsp::fixed::sample_read::QuadMix", ${
	it("mixes four frames at a time as the scalar ramp and mixed_in do, every layout and condense", _ {
		expect(every_layout(Q<31>::from_raw(0x3000'0000), Q<31>::from_raw(12'345), false)).to_equal(std::string{});
	});

	it("mixes a last call of 1, 2 or 3 frames and leaves the words past it", _ {
		// every_count covers 1 to 64 frames, so every remainder; the outputs past the frames are compared whole
		expect(every_layout(Q<31>::from_raw(-0x2000'0000), Q<31>::from_raw(-777), false)).to_equal(std::string{});
	});

	it("writes the cache's top three bytes of each pre-condense word, left then right", _ {
		expect(every_layout(Q<31>::from_raw(0x7FFF'FFFF), Q<31>::from_raw(0), true)).to_equal(std::string{});
	});

	it("wraps the amplitude as the scalar add_wrap does", _ {
		expect(every_layout(Q<31>::from_raw(0x7FFF'FF00), Q<31>::from_raw(0x40), false)).to_equal(std::string{});
		expect(every_layout(Q<31>::from_raw(INT32_MIN + 0x80), Q<31>::from_raw(-0x40), true)).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(sample_read_quad_mix_spec, sample_read_quad_mix_spec_body)
