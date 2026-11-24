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

// The sample read head's window check and chunk walk (dsp/fixed/sample_read/read_head.h) against the legacy reader's
// records (R1 to R6), and by hand: a frame straddling two chunks, a level copy at its chunk's end, R1's and R2's
// quirks and the walk's bound.

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
using deluge::dsp::UQ;
using deluge::dsp::fixed::sample_read::ChunkSpan;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::FrameRun;
using deluge::dsp::fixed::sample_read::Increment;
using deluge::dsp::fixed::sample_read::ReadHead;
using deluge::dsp::fixed::sample_read::Residency;
using read_head_chunks::geometry;
using read_head_chunks::HandChunks;
using read_head_chunks::RecordChunks;
using read_head_chunks::same;
using read_head_chunks::state_of;

Direction direction(int8_t d) {
	return d == 1 ? Direction::FORWARD : Direction::REVERSE;
}

struct Tally {
	std::size_t bad = 0;
	int32_t misses = 0;
	int32_t first_bad = -1;
};

Tally window_mismatches() {
	Tally t;
	int32_t index = 0;
	for (const auto& row : r::kWindowCheckGoldens) {
		const r::ReadSourceRecord& src = row.source;
		RecordChunks chunks(src, row.before.ended ? std::nullopt : std::optional<int32_t>(row.before.chunk_index));
		ReadHead::State state = state_of(row.before, src);
		const int32_t fb = src.channels * src.byte_depth;
		const ReadHead::ReadBounds bounds{
		    .loop_start_frame =
		        row.low_level_loop ? std::optional<int32_t>((src.loop_start_byte - src.data_start) / fb) : std::nullopt,
		    .endless_silence = row.endless_silence};
		const ReadHead::Window window = ReadHead::check(
		    state, chunks, bounds, row.frames, Increment::from_raw(row.increment), row.width, direction(row.direction));
		std::size_t bad = 0;
		bad += (window.kind == ReadHead::Window::Kind::READ) == row.ok ? 0 : 1;
		if (row.ok) {
			bad += window.frames == row.frames_after ? 0 : 1;
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

ReadHead::State reading(int32_t frame, int8_t width = 0) {
	ReadHead::State state;
	state.cursor = ReadHead::Reading{frame};
	state.width_last = width;
	return state;
}

constexpr ReadHead::ReadBounds kPlain{.loop_start_frame = std::nullopt, .endless_silence = false};
const Increment kNative = Increment::from_raw(1u << 24);
} // namespace

// clang-format off
describe read_head_window_spec_body("deluge::dsp::fixed::sample_read::ReadHead::check", ${
	it("reads a frame that straddles two chunks from the first chunk's slack, both ways", _ {
		// 24-bit stereo from byte 0: frame 5461 is bytes 32766 to 32771, across the first chunk's end
		HandChunks forward(geometry(0, 2, 3, 12'000, Direction::FORWARD), 12'000, true);
		forward.hold(0);
		ReadHead::State a = reading(5460);
		const ReadHead::Window first = ReadHead::check(a, forward, kPlain, 8, kNative, 0, Direction::FORWARD);
		expect(first.frames).to_equal(2); // frames 5460 and 5461 from chunk 0, the straddling one from its slack
		const int16_t l = ReadHead::tap(forward, 5461, 0, Direction::FORWARD);
		const int16_t r = ReadHead::tap(forward, 5461, 1, Direction::FORWARD);
		a.cursor = ReadHead::Reading{5462};
		const ReadHead::Window next = ReadHead::check(a, forward, kPlain, 8, kNative, 0, Direction::FORWARD);
		expect(next.frames).to_equal(8);
		expect(*forward.held()).to_equal(1); // the walk moved on at the next frame: offset 5462 x 6 - 32768 = 4
		HandChunks reversed(geometry(0, 2, 3, 12'000, Direction::REVERSE), 12'000, true);
		reversed.hold(1);
		ReadHead::State b = reading(5461);
		(void)ReadHead::check(b, reversed, kPlain, 8, kNative, 0, Direction::REVERSE);
		expect(*reversed.held()).to_equal(0); // at the boundary, -e into chunk 1, the walk moves before the read
		expect(static_cast<int32_t>(ReadHead::tap(reversed, 5461, 0, Direction::REVERSE))).to_equal(static_cast<int32_t>(l));
		expect(static_cast<int32_t>(ReadHead::tap(reversed, 5461, 1, Direction::REVERSE))).to_equal(static_cast<int32_t>(r));
	});

	it("cannot walk a level copy past its first chunk (R10)", _ {
		HandChunks copy(geometry(44, 2, 2, 20'000, Direction::FORWARD), 20'000, false);
		copy.hold(0);
		const int32_t boundary = copy.span(Direction::FORWARD)->boundary;
		ReadHead::State state = reading(boundary);
		const ReadHead::Window w = ReadHead::check(state, copy, kPlain, 8, kNative, 0, Direction::FORWARD);
		expect(static_cast<int32_t>(w.kind)).to_equal(static_cast<int32_t>(ReadHead::Window::Kind::STOPPED));
		expect(copy.held().has_value() ? 1 : 0).to_equal(0);
		expect(std::holds_alternative<ReadHead::Ended>(state.cursor) ? 1 : 0).to_equal(1);
	});

	it("reads 0 for taps before the chunk's floor (R1's quirk)", _ {
		// 16-bit stereo from byte 44: chunk 1's first whole frame is 8181
		HandChunks chunks(geometry(44, 2, 2, 20'000, Direction::FORWARD), 20'000, true);
		chunks.hold(1);
		ReadHead::State state = reading(8183);
		(void)ReadHead::check(state, chunks, kPlain, 8, Increment::from_raw(1u << 23), 16, Direction::FORWARD);
		expect(static_cast<int32_t>(state.window.window_l()[8])).to_equal(ReadHead::tap(chunks, 8182, 0, Direction::FORWARD));
		expect(static_cast<int32_t>(state.window.window_l()[9])).to_equal(ReadHead::tap(chunks, 8181, 0, Direction::FORWARD));
		expect(static_cast<int32_t>(state.window.window_l()[10])).to_equal(0); // frame 8180, in chunk 0
		expect(static_cast<int32_t>(state.window.window_l()[15])).to_equal(0);
	});

	it("fills a widened window's oldest taps with 0 (R2's quirk)", _ {
		HandChunks chunks(geometry(44, 2, 2, 20'000, Direction::FORWARD), 20'000, true);
		chunks.hold(0);
		ReadHead::State state = reading(1000, 2);
		(void)ReadHead::check(state, chunks, kPlain, 8, Increment::from_raw(1u << 23), 16, Direction::FORWARD);
		for (std::size_t i = 9; i < 16; i++) {
			expect(static_cast<int32_t>(state.window.window_l()[i])).to_equal(0);
		}
		expect(static_cast<int32_t>(state.width_last)).to_equal(16);
	});

	it("narrows a window by stepping its cursor back half the difference, never before the chunk's floor (R2)", _ {
		// As the legacy's jumpBackSamples: one frame at a time, stopping at the chunk's first frame
		HandChunks chunks(geometry(44, 2, 2, 20'000, Direction::FORWARD), 20'000, true);
		chunks.hold(0);
		ReadHead::State state = reading(1000, 16);
		const Increment half = Increment::from_raw(1u << 23); // a phase step crossing no frame
		expect(ReadHead::check(state, chunks, kPlain, 8, half, 2, Direction::FORWARD).kind == ReadHead::Window::Kind::READ
		           ? 1
		           : 0)
		    .to_equal(1);
		expect(std::get<ReadHead::Reading>(state.cursor).frame).to_equal(993);
		expect(static_cast<int32_t>(state.width_last)).to_equal(2);
		ReadHead::State near = reading(3, 16);
		(void)ReadHead::check(near, chunks, kPlain, 8, half, 2, Direction::FORWARD);
		expect(std::get<ReadHead::Reading>(near.cursor).frame).to_equal(0);
		HandChunks none(geometry(44, 2, 2, 20'000, Direction::FORWARD), 20'000, true);
		ReadHead::State unheld = reading(1000, 16);
		(void)ReadHead::check(unheld, none, kPlain, 8, half, 2, Direction::FORWARD);
		expect(std::get<ReadHead::Reading>(unheld.cursor).frame).to_equal(1000); // no chunk: no step back
	});

	it("steps a resampled window back to its centre when the read turns native (R5)", _ {
		// The legacy: back (width / 2) - (phase >> 23) frames, then width 0 and phase 0
		HandChunks chunks(geometry(44, 2, 2, 20'000, Direction::FORWARD), 20'000, true);
		chunks.hold(0);
		ReadHead::State state = reading(1000, 16);
		(void)ReadHead::check(state, chunks, kPlain, 8, kNative, 0, Direction::FORWARD);
		expect(std::get<ReadHead::Reading>(state.cursor).frame).to_equal(992);
		expect(static_cast<int32_t>(state.width_last)).to_equal(0);
		ReadHead::State past_half = reading(1000, 16);
		past_half.phase = UQ<24>::from_raw(3u << 22);
		(void)ReadHead::check(past_half, chunks, kPlain, 8, kNative, 0, Direction::FORWARD);
		expect(std::get<ReadHead::Reading>(past_half.cursor).frame).to_equal(993);
		expect(static_cast<int64_t>(past_half.phase.raw())).to_equal(0);
		// An ended head keeps no frame to step back from
		ReadHead::State ended = reading(1000, 16);
		ended.cursor = ReadHead::Ended{5};
		(void)ReadHead::check(ended, chunks, kPlain, 8, kNative, 0, Direction::FORWARD);
		expect(std::holds_alternative<ReadHead::Ended>(ended.cursor) ? 1 : 0).to_equal(1);
		expect(static_cast<int32_t>(ended.width_last)).to_equal(0);
	});

	it("stops a native read of a resampled head holding no chunk (R5)", _ {
		HandChunks none(geometry(44, 2, 2, 20'000, Direction::FORWARD), 20'000, true);
		ReadHead::State state = reading(1000, 16);
		expect(ReadHead::check(state, none, kPlain, 8, kNative, 0, Direction::FORWARD).kind
		               == ReadHead::Window::Kind::STOPPED
		           ? 1
		           : 0)
		    .to_equal(1);
	});

	it("fills a fresh window past a non-looping end with counted silence (R1)", _ {
		HandChunks chunks(geometry(44, 2, 2, 100, Direction::FORWARD), 100, true);
		chunks.hold(0);
		ReadHead::State state = reading(96);
		const Increment half = Increment::from_raw(1u << 23);
		// Frames 96 to 99 fill four of the eight leading taps; the end gives the other four as zeros
		expect(ReadHead::check(state, chunks, kPlain, 8, half, 16, Direction::FORWARD).kind == ReadHead::Window::Kind::READ
		           ? 1
		           : 0)
		    .to_equal(1);
		expect(std::get<ReadHead::Ended>(state.cursor).zero_taps).to_equal(4);
		expect(chunks.held().has_value() ? 1 : 0).to_equal(0);
		// A head holding no chunk fills its window from silence alone: the back taps cleared, the leading ones counted
		HandChunks none(geometry(44, 2, 2, 100, Direction::FORWARD), 100, true);
		ReadHead::State unheld = reading(50);
		(void)ReadHead::check(unheld, none, kPlain, 8, half, 16, Direction::FORWARD);
		expect(std::get<ReadHead::Ended>(unheld.cursor).zero_taps).to_equal(8);
		for (std::size_t i = 0; i < 16; i++) {
			expect(static_cast<int32_t>(unheld.window.window_l()[i])).to_equal(0);
		}
	});

	it("stops a stale head whose silence would fill the window, fresh or widened (R1, R2)", _ {
		// A restart that was not ready leaves a count past every width: the first zero it feeds reaches the width
		HandChunks none(geometry(44, 2, 2, 100, Direction::FORWARD), 100, true);
		const Increment half = Increment::from_raw(1u << 23);
		ReadHead::State fresh;
		fresh.cursor = ReadHead::Ended{ReadHead::kStaleZeroTaps};
		expect(ReadHead::check(fresh, none, kPlain, 8, half, 16, Direction::FORWARD).kind
		               == ReadHead::Window::Kind::STOPPED
		           ? 1
		           : 0)
		    .to_equal(1);
		ReadHead::State widened;
		widened.cursor = ReadHead::Ended{ReadHead::kStaleZeroTaps};
		widened.width_last = 2;
		expect(ReadHead::check(widened, none, kPlain, 8, half, 16, Direction::FORWARD).kind
		               == ReadHead::Window::Kind::STOPPED
		           ? 1
		           : 0)
		    .to_equal(1);
	});

	it("gives up after 1,024 turns of a tiny low-level loop", _ {
		HandChunks chunks(geometry(44, 2, 2, 100, Direction::FORWARD), 100, true);
		chunks.hold(0);
		const ReadHead::ReadBounds loop{.loop_start_frame = 99, .endless_silence = false};
		ReadHead::State state = reading(100 + 2000); // far past a one-frame loop: each turn wins back one frame
		expect(ReadHead::walk(state, chunks, loop, Direction::FORWARD) ? 1 : 0).to_equal(0);
		expect(chunks.held().has_value() ? 1 : 0).to_equal(0);
		expect(std::holds_alternative<ReadHead::Ended>(state.cursor) ? 1 : 0).to_equal(1);
	});

	it("checks each recorded window as the legacy did (R1 to R6)", _ {
		const Tally t = window_mismatches();
		expect(t.first_bad).to_equal(-1);
		expect(t.bad).to_equal(std::size_t{0});
		expect(t.misses).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(read_head_window_spec, read_head_window_spec_body)
