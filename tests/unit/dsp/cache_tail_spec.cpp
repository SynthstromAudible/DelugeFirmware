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

// The time stretcher's cache tail (dsp/fixed/timestretch/cache_tail.h) against the legacy stretcher's records: its
// limit (C1) over the goldens and a sweep of a chunk's end, the copy of the cache's words (C2), and its mix (C4).

#include "dsp/fixed/timestretch/cache_tail.h"

#include "time_stretcher_records.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/spec_main.h"

namespace {
using deluge::dsp::Q;
using deluge::dsp::fixed::timestretch::cache_tail_frames;
using deluge::dsp::fixed::timestretch::CacheTail;
using deluge::dsp::fixed::timestretch::CacheWindow;
namespace r = time_stretcher_records;

int32_t limit(int32_t to_hop, int32_t channels, int32_t offset, int32_t size, int32_t ahead) {
	return cache_tail_frames(CacheWindow{nullptr, offset, size, ahead}, to_hop, channels);
}

template <class Rows>
int32_t limit_mismatches(const Rows& rows) {
	int32_t bad = 0;
	for (const auto& row : rows) {
		bad += limit(row.frames_to_hop, row.channels, row.chunk_offset, row.chunk_size, row.bytes_written_ahead)
		               != row.frames
		           ? 1
		           : 0;
	}
	return bad;
}

std::vector<Q<31>> words_of(r::Span s) {
	std::vector<Q<31>> out;
	for (uint32_t i = 0; i < s.count; i++) {
		out.push_back(Q<31>::from_raw(r::kWords[s.offset + i]));
	}
	return out;
}
} // namespace

// clang-format off
describe cache_tail_spec_body("deluge::dsp::fixed::timestretch::CacheTail", ${
	it("limits the tail as the legacy did", _ {
		expect(limit_mismatches(r::kCacheTailLimitGoldens)).to_equal(0);
		expect(limit_mismatches(r::kCacheTailLimitSweep)).to_equal(0);
	});

	it("gives the hand-checked limits", _ {
		expect(limit(1000, 2, 100, 32768, 10000)).to_equal(255);
		expect(limit(1000, 2, 32766, 32768, 10000)).to_equal(1);
		expect(limit(1000, 2, 32767, 32768, 10000)).to_equal(0);
		expect(limit(1000, 2, 100, 32768, 6)).to_equal(0);
		expect(limit(1000, 2, 100, 32768, 7)).to_equal(1);
	});

	it("copies the cache's words, the byte before each sample included (C2)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kCacheTailCopyGoldens) {
			std::vector<Q<31>> storage(256 * static_cast<std::size_t>(row.channels));
			const auto* origin = reinterpret_cast<const std::byte*>(&r::kSourceBytes[row.cache_bytes.offset]);
			const CacheTail tail(storage, origin, row.frames, row.channels);
			bad += tail.frames() == row.frames ? 0 : 1;
			for (uint32_t i = 0; i < row.words.count; i++) {
				bad += storage[i].raw() == r::kWords[row.words.offset + i] ? 0 : 1;
			}
		}
		expect(r::kCacheTailCopyGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("mixes the tail with a floored product (C4)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kCacheTailMixGoldens) {
			std::vector<Q<31>> storage = words_of(row.tail_words);
			std::vector<Q<31>> out = words_of(row.out_before);
			CacheTail tail(storage, 255, row.tail_channels, row.position);
			tail.mix_into(out.data(), row.frames, row.out_channels, Q<31>::from_raw(row.level),
			              Q<31>::from_raw(row.level_step));
			for (uint32_t i = 0; i < row.out_after.count; i++) {
				bad += out[i].raw() == r::kWords[row.out_after.offset + i] ? 0 : 1;
			}
		}
		expect(r::kCacheTailMixGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("floors where a rounded product would round up", _ {
		std::vector<Q<31>> storage(256, Q<31>::from_raw(0));
		storage[0] = Q<31>::from_raw(3);
		CacheTail tail(storage, 1, 1, 0);
		std::array<Q<31>, 1> out{Q<31>::from_raw(0)};
		tail.mix_into(out.data(), 1, 1, Q<31>::from_raw(1 << 30), Q<31>::from_raw(0));
		expect(out[0].raw()).to_equal(0);
	});

	it("mixes nothing from a tail of no frames", _ {
		std::vector<Q<31>> storage(256, Q<31>::from_raw(12345));
		const auto* origin = reinterpret_cast<const std::byte*>(storage.data());
		CacheTail tail(storage, origin, 0, 1);
		std::array<Q<31>, 16> out{};
		out.fill(Q<31>::from_raw(7));
		tail.mix_into(out.data(), 16, 1, Q<31>::max(), Q<31>::from_raw(0));
		int32_t changed = 0;
		for (const auto& v : out) {
			changed += v.raw() == 7 ? 0 : 1;
		}
		expect(changed).to_equal(0);
	});

	it("halves each channel into a mono output, running the legacy's frames times the tail's channels", _ {
		std::vector<Q<31>> stereo(512, Q<31>::from_raw(0));
		stereo[0] = Q<31>::from_raw(1000);
		stereo[1] = Q<31>::from_raw(3000);
		stereo[2] = Q<31>::from_raw(5000);
		stereo[3] = Q<31>::from_raw(7000);
		std::vector<Q<31>> mono(256, Q<31>::from_raw(0));
		mono[0] = Q<31>::from_raw(2000);
		mono[1] = Q<31>::from_raw(6000);
		CacheTail from_stereo(stereo, 2, 2, 0);
		CacheTail from_mono(mono, 2, 1, 0);
		std::array<Q<31>, 2> a{}, b{};
		from_stereo.mix_into(a.data(), 1, 1, Q<31>::max(), Q<31>::from_raw(0));
		from_mono.mix_into(b.data(), 2, 1, Q<31>::max(), Q<31>::from_raw(0));
		expect(a[0].raw()).to_equal(b[0].raw());
		expect(a[1].raw()).to_equal(b[1].raw());
	});
});
// clang-format on

DELUGE_SPEC(cache_tail_spec, cache_tail_spec_body)
