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

/// A synthetic source: a file of frames whose chunks' memory, slack included, is the file's own bytes (the loader
/// copies a neighbour's bytes into a chunk's slack), every chunk ready.
class HandChunks {
public:
	HandChunks(r::ReadSourceRecord geometry, int32_t frames, bool has_cursor)
	    : geometry_(geometry), has_cursor_(has_cursor),
	      bytes_(static_cast<size_t>(geometry.data_start + (frames * frame_bytes()) + 64), std::byte{0}) {
		geometry_.data_length = frames * frame_bytes();
		for (size_t i = 0; i < bytes_.size(); i++) {
			bytes_[i] = static_cast<std::byte>((i * 37 + 11) & 0xFF);
		}
	}
	[[nodiscard]] int32_t channels() const { return geometry_.channels; }
	[[nodiscard]] int32_t byte_depth() const { return geometry_.byte_depth; }
	[[nodiscard]] int32_t frame_bytes() const { return geometry_.channels * geometry_.byte_depth; }
	[[nodiscard]] int32_t length_frames() const { return geometry_.data_length / frame_bytes(); }
	[[nodiscard]] std::optional<int32_t> held() const { return held_; }
	void hold(int32_t index) { held_ = index; }

	[[nodiscard]] std::optional<FrameRun> run(int32_t frame, Direction direction) const {
		if (!held_) {
			return std::nullopt;
		}
		const int32_t fb = frame_bytes();
		const int32_t byte = geometry_.data_start + (frame * fb);
		const int32_t offset = byte - (*held_ * geometry_.chunk_size);
		const int32_t origin = offset + geometry_.byte_depth - 4;
		if (origin < -4 || offset + fb > geometry_.chunk_size + 7) {
			return std::nullopt; // outside the chunk's memory
		}
		const int32_t frames = direction == Direction::FORWARD ? ((geometry_.chunk_size + 7 - (offset + fb)) / fb) + 1
		                                                       : ((origin + 4) / fb) + 1;
		return FrameRun{.first = bytes_.data() + (byte + geometry_.byte_depth - 4),
		                .frames = frames,
		                .stride = fb * static_cast<int32_t>(direction),
		                .channels = geometry_.channels,
		                .byte_depth = geometry_.byte_depth};
	}
	Residency move_to(int32_t frame, Direction /*direction*/) {
		const int32_t index = (geometry_.data_start + (frame * frame_bytes())) / geometry_.chunk_size;
		if (held_ && *held_ != index && !has_cursor_) {
			held_.reset();
			return Residency::UNAVAILABLE;
		}
		held_ = index;
		return Residency::READY;
	}
	void let_go() { held_.reset(); }
	[[nodiscard]] std::optional<ChunkSpan> span(Direction direction) const {
		if (!held_) {
			return std::nullopt;
		}
		return RecordChunks::chunk_span(geometry_, *held_, direction);
	}

private:
	r::ReadSourceRecord geometry_;
	bool has_cursor_;
	std::vector<std::byte> bytes_;
	std::optional<int32_t> held_;
};
static_assert(deluge::dsp::fixed::sample_read::read_source<HandChunks>);

r::ReadSourceRecord geometry(int32_t data_start, int8_t channels, int8_t byte_depth, int32_t frames, Direction d) {
	const int32_t fb = channels * byte_depth;
	return {.chunks = {0, 0},
	        .data_start = data_start,
	        .data_length = frames * fb,
	        .chunk_size = 32768,
	        .end_byte = d == Direction::FORWARD ? data_start + (frames * fb) : data_start - fb,
	        .loop_start_byte = data_start,
	        .channels = channels,
	        .byte_depth = byte_depth,
	        .obey_markers = false,
	        .has_cursor = true};
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
