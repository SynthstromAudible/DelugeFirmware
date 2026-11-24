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

// What one perc fill tells the time stretcher (synth/sources/sample/perc_fill.h): the chunks it touched, once each,
// and a chunk past the two a fill can touch reported rather than lost silently; and the memo of the last fill that
// decides whether a block fills (P4), against the legacy's records.

#include "synth/sources/sample/perc_fill.h"

#include <cppspec.hpp>
#include <cstdint>
#include <optional>

#include "time_stretcher_records.h"

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;
using deluge::synth::FillRange;
using deluge::synth::PercChunk;
using deluge::synth::PercFill;
using deluge::synth::PercFillMemo;

// The P4 rows the memo disagrees with: whether it fills, and where and how far.
std::size_t perc_fill_mismatches() {
	std::size_t bad = 0;
	for (const auto& row : r::kPercFillGoldens) {
		const PercFillMemo memo{.start = row.memo_start, .end = row.memo_end, .direction = row.memo_direction};
		const std::optional<FillRange> range =
		    memo.wanted(row.position_frame, row.increment, row.ratio, row.direction, row.block, row.writing_cache);
		bad += range.has_value() == row.fills ? 0 : 1;
		if (range.has_value()) {
			bad += (range->from == row.from && range->to == row.to && range->max_frames == row.max_frames) ? 0 : 1;
		}
	}
	return bad;
}
} // namespace

// clang-format off
describe perc_fill_spec_body("deluge::synth::PercFill", ${
	it("notes each chunk once, in the order first touched", _ {
		PercFill fill;
		expect(fill.touch({.reversed = false, .index = 3}) ? 1 : 0).to_equal(1);
		expect(fill.touch({.reversed = false, .index = 3}) ? 1 : 0).to_equal(1);
		expect(fill.touch({.reversed = false, .index = 4}) ? 1 : 0).to_equal(1);
		expect(static_cast<int32_t>(fill.touched_count)).to_equal(2);
		expect(fill.touched[0].index).to_equal(3);
		expect(fill.touched[1].index).to_equal(4);
	});

	it("reports a third chunk it cannot keep", _ {
		PercFill fill;
		(void)fill.touch({.reversed = false, .index = 1});
		(void)fill.touch({.reversed = false, .index = 2});
		expect(fill.touch({.reversed = false, .index = 5}) ? 1 : 0).to_equal(0);
		expect(static_cast<int32_t>(fill.touched_count)).to_equal(2);
	});

	it("fills ahead where the legacy did (P4)", _ {
		expect(perc_fill_mismatches()).to_equal(std::size_t{0});
	});

	it("skips a fill the last one already reaches, this direction", _ {
		PercFill reached;
		reached.filled_end = 10'000;
		PercFillMemo memo;
		memo.filled(1'000, reached, 1);
		constexpr uint32_t kRatio = 1u << 23;
		constexpr uint32_t kIncrement = 1u << 24; // reads 16,384 frames ahead
		// From 1,000 the read ends at 17,384, past the fill's end: a fill
		expect(memo.wanted(1'000, kIncrement, kRatio, 1, 128, false).has_value() ? 1 : 0).to_equal(1);
		PercFill further;
		further.filled_end = 20'000;
		memo.filled(1'000, further, 1);
		expect(memo.wanted(2'000, kIncrement, kRatio, 1, 128, false).has_value() ? 1 : 0).to_equal(0);
		// Reversed, the same memo does not count
		expect(memo.wanted(2'000, kIncrement, kRatio, -1, 128, false).has_value() ? 1 : 0).to_equal(1);
		// At a ratio of 1.0 nothing fills
		expect(memo.wanted(1'000, kIncrement, 1u << 24, 1, 128, false).has_value() ? 1 : 0).to_equal(0);
		const std::optional<FillRange> range = memo.wanted(30'000, kIncrement, kRatio, 1, 128, true);
		expect(range->to).to_equal(30'000 + 16'384 + 512);
		expect(range->max_frames).to_equal(128 * 32);
	});
});
// clang-format on

DELUGE_SPEC(perc_fill_spec, perc_fill_spec_body)
