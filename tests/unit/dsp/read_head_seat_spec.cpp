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

// The sample read head's state, seat, level copy and play byte (dsp/fixed/sample_read/read_head.h) against the
// legacy reader's records: its seats and copies (R10) and its reference byte (A0).

#include "dsp/fixed/sample_read/read_head.h"

#include <cppspec.hpp>
#include <cstdint>
#include <type_traits>

#include "read_head_chunks.h"
#include "time_stretcher_records.h"

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;
using deluge::dsp::UQ;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::ReadFacts;
using deluge::dsp::fixed::sample_read::ReadHead;
using deluge::dsp::fixed::sample_read::Residency;
using read_head_chunks::RecordChunks;
using read_head_chunks::same;
using read_head_chunks::state_of;

static_assert(deluge::dsp::fixed::sample_read::read_source<RecordChunks>);
static_assert(std::is_trivially_copyable_v<ReadHead::State>);
static_assert(sizeof(ReadHead::State) <= 96);

Direction direction(int8_t d) {
	return d == 1 ? Direction::FORWARD : Direction::REVERSE;
}

int32_t floor_div(int32_t a, int32_t b) {
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

struct Tally {
	std::size_t bad = 0;
	int32_t misses = 0;
};

// Every seat and copy, against its record.
Tally seat_mismatches() {
	Tally t;
	for (const auto& row : r::kReadSeatGoldens) {
		const r::ReadSourceRecord& src = row.source;
		RecordChunks chunks(src, row.before.ended ? std::nullopt : std::optional<int32_t>(row.before.chunk_index));
		ReadHead::State state = state_of(row.before, src);
		if (row.kind == 2) {
			// The level copy, given width 0 as a plain begin's spare is (H1)
			state = ReadHead::level_copy(state);
			state.width_last = 0;
			t.bad += same(state, row.after, src) ? 0 : 1;
			continue;
		}
		const int32_t frame = floor_div(row.byte - src.data_start, src.channels * src.byte_depth);
		const Residency residency =
		    ReadHead::seat(state, chunks, frame, UQ<24>::from_raw(row.phase), direction(row.direction));
		t.bad += static_cast<uint8_t>(residency) == row.residency ? 0 : 1;
		if (residency == Residency::READY) {
			// A seat places the cursor; H1 then sets the phase and width, which the record's after does not show yet
			const ReadHead::State want = state_of(row.after, src);
			t.bad += std::get<ReadHead::Reading>(state.cursor).frame == std::get<ReadHead::Reading>(want.cursor).frame
			             ? 0
			             : 1;
			t.bad += chunks.held() == std::optional<int32_t>(row.after.chunk_index) ? 0 : 1;
		}
		t.misses += chunks.misses();
	}
	return t;
}

std::size_t play_byte_mismatches() {
	std::size_t bad = 0;
	for (const auto& row : r::kPlayByteGoldens) {
		if (row.head.ended && row.head.zero_taps == INT32_MAX) {
			continue; // A stale cursor: the legacy's byte came from its stale pointer, which the record caps
		}
		const r::ReadSourceRecord geometry{.chunks = {0, 0},
		                                   .data_start = row.data_start,
		                                   .data_length = 0,
		                                   .chunk_size = row.chunk_size,
		                                   .end_byte = row.end_byte,
		                                   .loop_start_byte = 0,
		                                   .channels = row.channels,
		                                   .byte_depth = row.byte_depth,
		                                   .obey_markers = false,
		                                   .has_cursor = true};
		const ReadHead::State state = state_of(row.head, geometry);
		const ReadFacts facts{.data_start_byte = row.data_start,
		                      .end_byte = row.end_byte,
		                      .channels = row.channels,
		                      .byte_depth = row.byte_depth};
		bad += ReadHead::play_byte(state, facts, direction(row.direction), row.compensate) == row.play_byte ? 0 : 1;
	}
	return bad;
}

ReadHead::State head_at(int32_t frame, int8_t width) {
	ReadHead::State state;
	state.cursor = ReadHead::Reading{frame};
	state.width_last = width;
	return state;
}
} // namespace

// clang-format off
describe read_head_seat_spec_body("deluge::dsp::fixed::sample_read::ReadHead", ${
	it("seats and copies as the legacy did (R10)", _ {
		const Tally t = seat_mismatches();
		expect(t.bad).to_equal(std::size_t{0});
		expect(t.misses).to_equal(0);
	});

	it("marks each head's play byte as the legacy did (A0)", _ {
		expect(play_byte_mismatches()).to_equal(std::size_t{0});
	});

	it("compensates half the window against the play direction", _ {
		const ReadFacts facts{.data_start_byte = 44, .end_byte = 0, .channels = 2, .byte_depth = 2};
		const ReadHead::State state = head_at(100, 16);
		expect(ReadHead::play_byte(state, facts, Direction::FORWARD, true)).to_equal(44 + 400 - 32);
		expect(ReadHead::play_byte(state, facts, Direction::REVERSE, true)).to_equal(44 + 400 + 32);
		expect(ReadHead::play_byte(state, facts, Direction::FORWARD, false)).to_equal(44 + 400);
	});

	it("gives an ended head's byte past its end by its zero taps", _ {
		ReadHead::State state;
		state.cursor = ReadHead::Ended{5};
		const ReadFacts facts{.data_start_byte = 44, .end_byte = 1000, .channels = 2, .byte_depth = 2};
		expect(ReadHead::play_byte(state, facts, Direction::REVERSE, false)).to_equal(995);
	});

	it("refuses a seat outside the data", _ {
		const r::ReadSourceRecord src = r::kReadSeatGoldens[0].source;
		RecordChunks chunks(src, std::nullopt);
		ReadHead::State state = head_at(7, 4);
		const int32_t length = chunks.length_frames();
		for (const int32_t frame : {-1, length}) {
			expect(static_cast<int32_t>(ReadHead::seat(state, chunks, frame, UQ<24>{}, Direction::FORWARD)))
			    .to_equal(static_cast<int32_t>(Residency::UNAVAILABLE));
			expect(std::get<ReadHead::Reading>(state.cursor).frame).to_equal(7);
			expect(static_cast<int32_t>(state.width_last)).to_equal(4);
		}
	});
});
// clang-format on

DELUGE_SPEC(read_head_seat_spec, read_head_seat_spec_body)
