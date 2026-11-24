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

// The time stretcher's position, loop and exit arithmetic (synth/sources/sample/stretch_position.h) against the
// legacy's records: the advance (Q1), the whole frame (Q2), a stretch's start (Q3), a synced hop (S6), the loop check
// (Q4) and the exit (E1).

#include "synth/sources/sample/stretch_position.h"

#include <cppspec.hpp>
#include <cstdint>

#include "time_stretcher_records.h"

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;
using deluge::synth::combined_increment;
using deluge::synth::Direction;
using deluge::synth::ExitFacts;
using deluge::synth::Increment;
using deluge::synth::LoopBytes;
using deluge::synth::LoopStep;
using deluge::synth::Ratio;
using deluge::synth::SourcePosition;

Direction direction(int8_t d) {
	return d == 1 ? Direction::FORWARD : Direction::REVERSE;
}

template <class Rows>
std::size_t whole_frame_mismatches(const Rows& rows) {
	std::size_t bad = 0;
	for (const auto& row : rows) {
		bad += SourcePosition(row.position).frame(direction(row.direction)) == row.frame ? 0 : 1;
	}
	return bad;
}

template <class Rows>
std::size_t start_mismatches(const Rows& rows) {
	std::size_t bad = 0;
	for (const auto& row : rows) {
		bad += SourcePosition::from_play_byte(row.play_byte, row.data_start, row.bytes_per_frame).raw() == row.position
		           ? 0
		           : 1;
	}
	return bad;
}

ExitFacts facts(bool begun, bool fudge, bool meet) {
	return {.begun_this_block = begun,
	        .fudge = fudge,
	        .ratio = Ratio::from_raw(1u << 24),
	        .cursors_meet = meet,
	        .synced = false,
	        .sync_lag = 0};
}
} // namespace

// clang-format off
describe stretch_position_spec_body("deluge::synth::SourcePosition", ${
	it("advances as the legacy did (Q1)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kAdvanceGoldens) {
			SourcePosition p(row.position);
			p.advance(combined_increment(Ratio::from_raw(row.ratio), Increment::from_raw(row.increment)), row.frames,
			          direction(row.direction));
			bad += p.raw() == row.after ? 0 : 1;
		}
		expect(bad).to_equal(std::size_t{0});
	});

	it("takes the whole frame both ways (Q2)", _ {
		expect(whole_frame_mismatches(r::kWholeFrameGoldens)).to_equal(std::size_t{0});
		expect(whole_frame_mismatches(r::kWholeFrameSweep)).to_equal(std::size_t{0});
	});

	it("starts from the play byte, rounding toward minus infinity (Q3)", _ {
		expect(start_mismatches(r::kStartPositionGoldens)).to_equal(std::size_t{0});
		expect(start_mismatches(r::kStartPositionSweep)).to_equal(std::size_t{0});
	});

	it("places a synced hop (S6)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kSyncedHopGoldens) {
			bad += SourcePosition::synced(row.start_byte, row.data_start, row.bytes_per_frame, row.frames_in,
			                              direction(row.direction))
			                   .raw()
			               == row.position
			           ? 0
			           : 1;
		}
		expect(bad).to_equal(std::size_t{0});
	});

	it("checks the loop end as the legacy did (Q4)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kLoopCheckGoldens) {
			const LoopBytes bytes{.data_start = row.data_start, .bytes_per_frame = row.bytes_per_frame,
			                      .loop_end_byte = row.loop_end_byte, .restart_byte = row.restart_byte,
			                      .looping = row.looping};
			const LoopStep step = deluge::synth::loop_step(SourcePosition(row.position), row.combined, row.frames,
			                                               direction(row.direction), bytes);
			bad += static_cast<uint8_t>(step.kind) == row.kind ? 0 : 1;
			if (step.kind == LoopStep::Kind::READ || step.kind == LoopStep::Kind::SHORTEN) {
				bad += step.frames == row.frames_after ? 0 : 1;
			}
			if (step.kind == LoopStep::Kind::RESTART) {
				bad += step.restart.raw() == row.restart ? 0 : 1;
			}
		}
		expect(bad).to_equal(std::size_t{0});
	});

	it("ends a stretch where the legacy did (E1)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kExitGoldens) {
			const ExitFacts f{.begun_this_block = row.begun_this_block, .fudge = row.fudge,
			                  .ratio = Ratio::from_raw(row.ratio), .cursors_meet = row.cursors_meet,
			                  .synced = row.synced, .sync_lag = row.lag};
			bad += deluge::synth::may_end(f) == row.ends ? 0 : 1;
		}
		expect(bad).to_equal(std::size_t{0});
	});

	it("rounds a reversed position up to its frame", _ {
		expect(SourcePosition(1).frame(Direction::REVERSE)).to_equal(1);
		expect(SourcePosition(1).frame(Direction::FORWARD)).to_equal(0);
	});

	it("starts a play byte before the data at a negative frame", _ {
		expect(SourcePosition::from_play_byte(-1, 0, 4).raw()).to_equal(-(int64_t{1} << 24));
	});

	it("never ends a stretch in the block it began, or a fudge", _ {
		expect(deluge::synth::may_end(facts(false, false, true)) ? 1 : 0).to_equal(1);
		expect(deluge::synth::may_end(facts(true, false, true)) ? 1 : 0).to_equal(0);
		expect(deluge::synth::may_end(facts(false, true, true)) ? 1 : 0).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(stretch_position_spec, stretch_position_spec_body)
