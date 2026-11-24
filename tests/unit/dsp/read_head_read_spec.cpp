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

	it("reads a stereo sample linearly into a stereo output as the legacy's readSamplesLinear<2, 2> (R9)", _ {
		using deluge::dsp::fixed::sample_read::mixed_in;
		HandChunks chunks(geometry(44, 2, 2, 64, Direction::FORWARD), 64, true);
		chunks.hold(0);
		const uint32_t inc = 3u << 23; // 1.5: steps of one and of two
		const auto amplitude = Q<31>::from_raw(0x4000'0000);
		const auto step = Q<31>::from_raw(0x0010'0000);
		ReadHead::State state;
		state.cursor = ReadHead::Reading{8};
		std::vector<Q<31>> out(12);
		ReadHead::Output output{.out = out, .out_channels = 2, .amplitude = amplitude, .step = step};
		std::byte* no_cache = nullptr;
		ReadHead::read_resampled(state, chunks, output, 6, Increment::from_raw(inc), 2, 0, no_cache,
		                         Direction::FORWARD);
		// The legacy's loop, its taps read one by one through ReadHead::tap
		std::vector<Q<31>> want(12);
		int32_t frame = 8;
		uint32_t phase = 0;
		int16_t newest[2] = {0, 0};
		int16_t previous[2] = {0, 0};
		Q<31> gain = amplitude;
		for (std::size_t i = 0; i < 6; i++) {
			if (i != 0) {
				phase += inc;
				const auto steps = static_cast<int32_t>(phase >> 24);
				if (steps != 0) {
					phase &= (1u << 24) - 1;
					frame += steps > 2 ? steps - 2 : 0;
					for (int c = 0; c < 2; c++) {
						previous[c] = steps >= 2 ? ReadHead::tap(chunks, frame, c, Direction::FORWARD) : newest[c];
					}
					frame += steps >= 2 ? 1 : 0;
					for (int c = 0; c < 2; c++) {
						newest[c] = ReadHead::tap(chunks, frame, c, Direction::FORWARD);
					}
					frame++;
				}
			}
			const auto s2 = static_cast<int16_t>(phase >> 9);
			const auto s1 = static_cast<int16_t>(32767 - s2);
			gain = deluge::dsp::add_wrap(gain, step);
			for (int c = 0; c < 2; c++) {
				want[(2 * i) + c] = mixed_in(Q<31>{}, Q<31>::from_raw((previous[c] * s1) + (newest[c] * s2)), gain);
			}
		}
		for (std::size_t i = 0; i < 12; i++) {
			expect(out[i].raw()).to_equal(want[i].raw());
		}
		expect(std::get<ReadHead::Reading>(state.cursor).frame).to_equal(frame);
		expect(static_cast<int64_t>(state.phase.raw())).to_equal(static_cast<int64_t>(phase));
		expect(output.amplitude.raw()).to_equal(gain.raw());
		expect(static_cast<int32_t>(state.window.window_r()[0])).to_equal(newest[1]);
		// No frames: nothing mixed, nothing stepped
		std::vector<Q<31>> none(2);
		ReadHead::Output empty{.out = none, .out_channels = 2, .amplitude = amplitude, .step = step};
		ReadHead::read_resampled(state, chunks, empty, 0, Increment::from_raw(inc), 2, 0, no_cache,
		                         Direction::FORWARD);
		expect(std::get<ReadHead::Reading>(state.cursor).frame).to_equal(frame);
		expect(none[0].raw()).to_equal(0);
	});

	it("feeds a mono sinc read below its native rate only on the outputs that cross a frame (R8)", _ {
		HandChunks chunks(geometry(44, 1, 2, 64, Direction::FORWARD), 64, true);
		chunks.hold(0);
		ReadHead::State state;
		state.cursor = ReadHead::Reading{8};
		std::vector<Q<31>> out(4);
		ReadHead::Output output{.out = out, .out_channels = 1, .amplitude = Q<31>{}, .step = Q<31>{}};
		std::byte* no_cache = nullptr;
		ReadHead::read_resampled(state, chunks, output, 4, Increment::from_raw(1u << 22), 16, 0, no_cache,
		                         Direction::FORWARD);
		expect(std::get<ReadHead::Reading>(state.cursor).frame).to_equal(8); // three quarter steps cross no frame
		expect(static_cast<int64_t>(state.phase.raw())).to_equal(int64_t{3} << 22);
	});

	it("jumps a sinc step of more than 16 frames over the frames before the window's last 16 (R8)", _ {
		for (const int8_t channels : {int8_t{1}, int8_t{2}}) {
			HandChunks chunks(geometry(44, channels, 2, 200, Direction::FORWARD), 200, true);
			chunks.hold(0);
			ReadHead::State state;
			state.cursor = ReadHead::Reading{8};
			std::vector<Q<31>> out(4);
			ReadHead::Output output{.out = out, .out_channels = channels, .amplitude = Q<31>{}, .step = Q<31>{}};
			std::byte* no_cache = nullptr;
			ReadHead::read_resampled(state, chunks, output, 2, Increment::from_raw(20u << 24), 16, 0, no_cache,
			                         Direction::FORWARD);
			expect(std::get<ReadHead::Reading>(state.cursor).frame).to_equal(28);
			// The newest tap is the step's last frame; the oldest of the 16 fed is four past the cursor
			expect(static_cast<int32_t>(state.window.window_l()[0]))
			    .to_equal(static_cast<int32_t>(ReadHead::tap(chunks, 27, 0, Direction::FORWARD)));
			expect(static_cast<int32_t>(state.window.window_l()[15]))
			    .to_equal(static_cast<int32_t>(ReadHead::tap(chunks, 12, 0, Direction::FORWARD)));
		}
	});

	it("feeds a stereo head's tail zeros into a stereo output, each counted (R9)", _ {
		HandChunks chunks(geometry(44, 2, 2, 64, Direction::FORWARD), 64, true);
		ReadHead::State state;
		state.cursor = ReadHead::Ended{3};
		std::vector<Q<31>> out(8, Q<31>::from_raw(5));
		ReadHead::Output output{.out = out, .out_channels = 2, .amplitude = Q<31>::from_raw(0x4000'0000), .step = Q<31>{}};
		std::byte* no_cache = nullptr;
		ReadHead::read_resampled(state, chunks, output, 4, Increment::from_raw(2u << 24), 2, 0, no_cache,
		                         Direction::FORWARD);
		expect(std::get<ReadHead::Ended>(state.cursor).zero_taps).to_equal(9); // three steps of two zeros
		for (const Q<31> word : out) {
			expect(word.raw()).to_equal(5); // silence mixed in leaves the mix as it was
		}
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
