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

// The sample read head's reads and read loop (dsp/fixed/sample_read/read_head.h) against the legacy reader's records:
// its native, windowed-sinc and linear runs (R7 to R9) and its whole reads (R11), with their faults.

#include "dsp/fixed/sample_read/read_head.h"

#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <vector>

#include "read_head_chunks.h"
#include "time_stretcher_records.h"

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;
using deluge::dsp::Q;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::Increment;
using deluge::dsp::fixed::sample_read::ReadHead;
using read_head_chunks::geometry;
using read_head_chunks::HandChunks;
using read_head_chunks::RecordChunks;
using read_head_chunks::same;
using read_head_chunks::state_of;

Direction direction(int8_t d) {
	return d == 1 ? Direction::FORWARD : Direction::REVERSE;
}

std::vector<Q<31>> words(r::Span s) {
	std::vector<Q<31>> out;
	for (uint32_t i = 0; i < s.count; i++) {
		out.push_back(Q<31>::from_raw(r::kPlaybackWords[s.offset + i]));
	}
	return out;
}

bool same_words(const std::vector<Q<31>>& a, r::Span b) {
	const std::vector<Q<31>> want = words(b);
	if (a.size() < want.size()) {
		return false;
	}
	for (std::size_t i = 0; i < want.size(); i++) {
		if (a[i].raw() != want[i].raw()) {
			return false;
		}
	}
	return true;
}

struct Tally {
	std::size_t bad = 0;
	int32_t misses = 0;
	int32_t first_bad = -1;
};

Tally read_mismatches() {
	Tally t;
	int32_t index = 0;
	for (const auto& row : r::kReadGoldens) {
		const r::ReadSourceRecord& src = row.source;
		RecordChunks chunks(src, row.before.ended ? std::nullopt : std::optional<int32_t>(row.before.chunk_index));
		ReadHead::State state = state_of(row.before, src);
		std::vector<Q<31>> out = words(row.out_before);
		out.resize(out.size() + 64);
		ReadHead::Output output{.out = out,
		                        .out_channels = row.out_channels,
		                        .amplitude = Q<31>::from_raw(row.amplitude),
		                        .step = Q<31>::from_raw(row.step)};
		std::vector<std::byte> cache(row.cache_bytes.count + 64);
		std::byte* cache_write = row.cache ? cache.data() : nullptr;
		if (row.increment == (1u << 24)) {
			ReadHead::read_native(state, chunks, output, row.frames, direction(row.direction));
		}
		else {
			ReadHead::read_resampled(state, chunks, output, row.frames, Increment::from_raw(row.increment), row.width,
			                         row.kernel, cache_write, direction(row.direction));
		}
		std::size_t bad = same_words(out, row.out_after) ? 0 : 1;
		for (uint32_t i = 0; i < row.cache_bytes.count; i++) {
			bad += static_cast<uint8_t>(cache[i]) == r::kReadBytes[row.cache_bytes.offset + i] ? 0 : 1;
		}
		bad += same(state, row.after, src) ? 0 : 1;
		if (bad != 0 && t.first_bad < 0) {
			t.first_bad = index;
		}
		t.bad += bad;
		t.misses += chunks.misses();
		index++;
	}
	return t;
}

// One whole read (R11) from its record, with a low-level loop or without (the record does not say which).
template <class Row>
bool loop_matches(const Row& row, bool low_level_loop, int32_t& misses) {
	const r::ReadSourceRecord& src = row.source;
	RecordChunks chunks(src, row.before.ended ? std::nullopt : std::optional<int32_t>(row.before.chunk_index));
	ReadHead::State state = state_of(row.before, src);
	const int32_t fb = src.channels * src.byte_depth;
	const ReadHead::ReadBounds bounds{
	    .loop_start_frame =
	        low_level_loop ? std::optional<int32_t>((src.loop_start_byte - src.data_start) / fb) : std::nullopt,
	    .endless_silence = row.endless_silence};
	std::vector<Q<31>> out = words(row.out_before);
	out.resize(out.size() + 64);
	ReadHead::Output output{.out = out,
	                        .out_channels = row.out_channels,
	                        .amplitude = Q<31>::from_raw(row.amplitude),
	                        .step = Q<31>::from_raw(row.step)};
	std::vector<std::byte> cache(static_cast<std::size_t>(row.frames) * 6 + 64);
	const bool reading =
	    ReadHead::read(state, chunks, bounds, output, row.frames, Increment::from_raw(row.increment), row.width,
	                   row.kernel, row.cache ? cache.data() : nullptr, direction(row.direction));
	misses += chunks.misses();
	return reading == row.reading_after && same_words(out, row.out_after) && same(state, row.after, src);
}

template <class Rows>
Tally loop_mismatches(const Rows& rows) {
	Tally t;
	int32_t index = 0;
	for (const auto& row : rows) {
		int32_t misses = 0;
		const bool ok = loop_matches(row, false, misses) || loop_matches(row, true, misses);
		if (!ok && t.first_bad < 0) {
			t.first_bad = index;
		}
		t.bad += ok ? 0 : 1;
		t.misses += misses;
		index++;
	}
	return t;
}
} // namespace

// clang-format off
describe read_head_read_spec_body("deluge::dsp::fixed::sample_read::ReadHead::read", ${
	it("reads each recorded run as the legacy did (R7 to R9)", _ {
		const Tally t = read_mismatches();
		expect(t.first_bad).to_equal(-1);
		expect(t.bad).to_equal(std::size_t{0});
		expect(t.misses).to_equal(0);
	});

	it("keeps a 24-bit sample's three bytes in a native read and its top 16 bits in a resampled one (R7, R8)", _ {
		// One stereo 24-bit frame at byte 44: left 0x123456, right 0xABCDEF, little-endian
		HandChunks chunks(geometry(44, 2, 3, 64, Direction::FORWARD), 64, true);
		const uint8_t frame[6] = {0x56, 0x34, 0x12, 0xEF, 0xCD, 0xAB};
		for (int32_t i = 0; i < 6; i++) {
			chunks.byte(44 + 6 + i) = static_cast<std::byte>(frame[i]);
		}
		chunks.hold(0);
		ReadHead::State state;
		state.cursor = ReadHead::Reading{1};
		std::vector<Q<31>> out(2);
		// Full scale and still: mixed_in adds the sample's word, as its Q31 times (almost) 1.0, rounded
		ReadHead::Output output{.out = out, .out_channels = 2, .amplitude = Q<31>::from_raw(INT32_MAX),
		                        .step = Q<31>::from_raw(0)};
		ReadHead::read_native(state, chunks, output, 1, Direction::FORWARD);
		expect(out[0].raw()).to_equal(deluge::dsp::fixed::sample_read::mixed_in(Q<31>{}, Q<31>::from_raw(0x12345600),
		                                                                          Q<31>::from_raw(INT32_MAX))
		                                  .raw());
		expect(static_cast<int32_t>(ReadHead::tap(chunks, 1, 0, Direction::FORWARD))).to_equal(0x1234);
		expect(static_cast<int32_t>(ReadHead::tap(chunks, 1, 1, Direction::FORWARD)))
		    .to_equal(static_cast<int32_t>(static_cast<int16_t>(0xABCD)));
	});

	it("halves a sinc read's mono toward zero and a linear one's down (R8, R9)", _ {
		using deluge::dsp::halve_floor;
		using deluge::dsp::halve_toward_zero;
		const Q<31> l = Q<31>::from_raw(-3);
		const Q<31> r = Q<31>::from_raw(0);
		expect(add_wrap(halve_toward_zero(l), halve_toward_zero(r)).raw()).to_equal(-1);
		expect(add_wrap(halve_floor(l), halve_floor(r)).raw()).to_equal(-2);
	});

	it("writes the cache's top three bytes, left then right (R8)", _ {
		HandChunks chunks(geometry(44, 2, 2, 64, Direction::FORWARD), 64, true);
		chunks.hold(0);
		ReadHead::State state;
		state.cursor = ReadHead::Reading{8};
		std::vector<Q<31>> out(4);
		ReadHead::Output output{.out = out, .out_channels = 2, .amplitude = Q<31>::from_raw(0), .step = Q<31>::from_raw(0)};
		std::vector<std::byte> cache(12);
		std::byte* write = cache.data();
		ReadHead::read_resampled(state, chunks, output, 2, Increment::from_raw(3u << 23), 16, 0, write,
		                         Direction::FORWARD);
		expect(static_cast<int32_t>(write - cache.data())).to_equal(12); // two frames, two channels, three bytes
	});

	it("leaves an ended head's zero count as a sinc read found it, and counts each of a linear read's zeros", _ {
		// The legacy's sinc read stepped a copy of its cursor and wrote the copy back, so the zeros its body fed past
		// the end were not counted; its linear read stepped the cursor itself
		HandChunks chunks(geometry(44, 1, 2, 64, Direction::FORWARD), 64, true);
		std::vector<Q<31>> out(8);
		ReadHead::Output output{.out = out, .out_channels = 1, .amplitude = Q<31>::from_raw(0), .step = Q<31>::from_raw(0)};
		std::byte* no_cache = nullptr;
		ReadHead::State sinc;
		sinc.cursor = ReadHead::Ended{3};
		ReadHead::read_resampled(sinc, chunks, output, 4, Increment::from_raw(2u << 24), 16, 0, no_cache,
		                         Direction::FORWARD);
		expect(std::get<ReadHead::Ended>(sinc.cursor).zero_taps).to_equal(3);
		ReadHead::State linear;
		linear.cursor = ReadHead::Ended{3};
		ReadHead::read_resampled(linear, chunks, output, 4, Increment::from_raw(2u << 24), 2, 0, no_cache,
		                         Direction::FORWARD);
		expect(std::get<ReadHead::Ended>(linear.cursor).zero_taps).to_equal(9); // three steps of two zeros
	});

	it("loops check and read as the legacy did (R11)", _ {
		const Tally goldens = loop_mismatches(r::kReadLoopGoldens);
		expect(goldens.first_bad).to_equal(-1);
		const Tally faults = loop_mismatches(r::kReadHeadFaults);
		expect(faults.first_bad).to_equal(-1);
	});
});
// clang-format on

DELUGE_SPEC(read_head_read_spec, read_head_read_spec_body)
