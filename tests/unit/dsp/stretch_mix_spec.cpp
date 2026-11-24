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

// The time stretcher's mix arithmetic (synth/sources/sample/stretch_mix.h) against the legacy's records: the
// amplitude's set-up (X4), the older head's stop (X3) and the copy of a stretched block into a cache (W1).

#include "dsp/fixed/timestretch/crossfade.h"
#include "synth/sources/sample/stretch_mix.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "time_stretcher_records.h"

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;
using deluge::dsp::Q;
using deluge::dsp::fixed::timestretch::CrossfadeRamp;
using deluge::synth::StretchAmplitude;

std::vector<Q<31>> words(r::Span s) {
	std::vector<Q<31>> out;
	for (uint32_t i = 0; i < s.count; i++) {
		out.push_back(Q<31>::from_raw(r::kPlaybackWords[s.offset + i]));
	}
	return out;
}

// The W1 rows whose output or cache bytes differ.
std::size_t cache_write_mismatches() {
	std::size_t bad = 0;
	for (const auto& row : r::kCacheWriteGoldens) {
		const std::vector<Q<31>> mix = words(row.mix);
		std::vector<Q<31>> out = words(row.out_before);
		const std::vector<Q<31>> want = words(row.out_after);
		std::vector<std::byte> cache(row.cache.count);
		Q<31> amplitude = Q<31>::from_raw(row.amplitude);
		deluge::synth::write_cache_frames(mix, row.channels, cache, out, row.out_channels, amplitude,
		                                  Q<31>::from_raw(row.step));
		for (std::size_t i = 0; i < out.size(); i++) {
			bad += out[i].raw() == want[i].raw() ? 0 : 1;
		}
		for (uint32_t i = 0; i < row.cache.count; i++) {
			bad += static_cast<uint8_t>(cache[i]) == r::kReadBytes[row.cache.offset + i] ? 0 : 1;
		}
	}
	return bad;
}
} // namespace

// clang-format off
describe stretch_mix_spec_body("deluge::synth::StretchAmplitude", ${
	it("sets the amplitude up as the legacy did (X4)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kAmplitudeGoldens) {
			StretchAmplitude a = deluge::synth::stretch_amplitude(Q<31>::from_raw(row.amplitude), Q<31>::from_raw(row.step),
			                                                     row.pitched, row.stretching);
			const deluge::synth::HeadLevel level = deluge::synth::segment_level(a, row.writing_cache);
			bad += level.level.raw() == row.level ? 0 : 1;
			bad += level.step.raw() == row.level_step ? 0 : 1;
			deluge::synth::step_amplitude(a, row.frames, row.writing_cache);
			bad += a.amplitude.raw() == row.amplitude_after ? 0 : 1;
		}
		expect(bad).to_equal(std::size_t{0});
	});

	it("stops the older head where the legacy did (X3)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kOlderHeadGoldens) {
			const CrossfadeRamp ramp{.progress = row.progress, .increment = 0};
			// The older head reads on until its ramp is done; the shadow copy reads in its one segment at level 0, its
			// ramp done, and stops after it
			bad += !ramp.done() == !row.shadow ? 0 : 1;
			bad += (row.shadow && row.older_reading_after) ? 1 : 0;
		}
		expect(bad).to_equal(std::size_t{0});
	});

	it("writes the cache and mixes the output as the legacy did (W1)", _ {
		expect(cache_write_mismatches()).to_equal(std::size_t{0});
	});

	it("caches the top three bytes of each word doubled, left then right", _ {
		const std::array<Q<31>, 2> mix{Q<31>::from_raw(0x11223344), Q<31>::from_raw(-2)};
		std::array<Q<31>, 2> out{};
		std::array<std::byte, 6> cache{};
		Q<31> amplitude = Q<31>::from_raw(0);
		deluge::synth::write_cache_frames(mix, 2, cache, out, 2, amplitude, Q<31>::from_raw(0));
		// 0x11223344 doubled is 0x22446688; -2 doubled is 0xFFFFFFFC
		const std::array<uint8_t, 6> want{0x66, 0x44, 0x22, 0xFF, 0xFF, 0xFF};
		for (std::size_t i = 0; i < 6; i++) {
			expect(static_cast<int32_t>(cache[i])).to_equal(static_cast<int32_t>(want[i]));
		}
		expect(out[0].raw()).to_equal(0); // amplitude 0 adds nothing
	});
});
// clang-format on

DELUGE_SPEC(stretch_mix_spec, stretch_mix_spec_body)
