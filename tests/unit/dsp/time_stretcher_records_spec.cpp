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

// The time stretcher's contract records (time_stretcher_records.h), taken from the legacy code: that each array the
// components' specs read holds rows, that every span lies inside its pool, and that the quirks the contracts name as
// pinned are among them.

#include "time_stretcher_records.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;

constexpr bool inside(r::Span span, std::size_t pool) {
	return span.offset + span.count <= pool;
}

// Every span of every row, against its pool: the number of spans outside.
std::size_t spans_outside() {
	std::size_t bad = 0;
	auto words = [&](r::Span s) { bad += inside(s, r::kWords.size()) ? 0 : 1; };
	auto perc = [&](r::Span s) { bad += inside(s, r::kPercBytes.size()) ? 0 : 1; };
	auto source = [&](r::Span s) { bad += inside(s, r::kSourceBytes.size()) ? 0 : 1; };
	auto window = [&](const r::FrameWindow& w) {
		words(w.words);
		bad += w.words.count == static_cast<uint32_t>((w.last_frame - w.first_frame + 1) * w.channels) ? 0 : 1;
	};
	for (const auto& row : r::kCacheTailCopyGoldens) {
		source(row.cache_bytes);
		words(row.words);
	}
	for (const auto& row : r::kCacheTailMixGoldens) {
		words(row.tail_words);
		words(row.out_before);
		words(row.out_after);
	}
	for (const auto& row : r::kSlopeGoldens) {
		window(row.frames);
		words(row.slopes);
	}
	for (const auto& row : r::kPercAnalysisGoldens) {
		window(row.frames);
		perc(row.pixels_before);
		perc(row.pixels_after);
	}
	for (const auto& row : r::kZoneForFillGoldens) {
		words(row.zones);
	}
	for (const auto& row : r::kBeamGoldens) {
		perc(row.perc);
	}
	for (const auto& row : r::kHopPlanGoldens) {
		perc(row.perc);
	}
	for (const auto& row : r::kAveragesGoldens) {
		window(row.frames);
	}
	for (const auto& row : r::kAlignGoldens) {
		window(row.frames);
	}
	for (const auto& row : r::kAlignFaults) {
		window(row.frames);
	}
	for (const auto& reach : r::kQuirkReach) {
		bad += inside(reach.cases, r::kCaseIndex.size()) ? 0 : 1;
	}
	return bad;
}
} // namespace

// clang-format off
describe time_stretcher_records_spec_body("the time stretcher's contract records", ${
	it("holds rows for every contract the goldens, sweeps and faults reach", _ {
		const std::array<std::size_t, 31> sizes{
		    r::kHopShapeGoldens.size(), r::kHopShapeSweep.size(), r::kRampGoldens.size(), r::kRampSweep.size(),
		    r::kSegmentGoldens.size(), r::kCacheTailLimitGoldens.size(), r::kCacheTailLimitSweep.size(),
		    r::kCacheTailCopyGoldens.size(), r::kCacheTailMixGoldens.size(), r::kPercStoreGoldens.size(),
		    r::kPercStoreSweep.size(), r::kSlopeGoldens.size(), r::kPercAnalysisGoldens.size(),
		    r::kPercByteGoldens.size(), r::kZoneForFillGoldens.size(), r::kZoneOverwriteGoldens.size(),
		    r::kJitterGoldens.size(), r::kRepitchGoldens.size(), r::kBeamGoldens.size(), r::kBackEdgeGoldens.size(),
		    r::kHopLengthGoldens.size(), r::kHopLengthSweep.size(), r::kLoopCrossfadeGoldens.size(),
		    r::kStartClampGoldens.size(), r::kStartClampFaults.size(), r::kHopPlanGoldens.size(),
		    r::kAlignSetupGoldens.size(), r::kAveragesGoldens.size(), r::kSearchLimitGoldens.size(),
		    r::kAlignGoldens.size(), r::kAlignFaults.size()};
		std::size_t empty = 0;
		for (std::size_t n : sizes) {
			empty += n == 0 ? 1 : 0;
		}
		expect(empty).to_equal(std::size_t{0});
		expect(spans_outside()).to_equal(std::size_t{0});
	});

	it("pins T3's wrapped share at the ratio sample/ts_speed_slow plays", _ {
		std::size_t found = 0;
		for (const auto& row : r::kHopShapeGoldens) {
			if (row.ratio == (1u << 23)) {
				expect(row.crossfade_share).to_equal(static_cast<int32_t>(0xA0000000u));
				found++;
			}
		}
		expect(found).to_equal(std::size_t{1});
	});

	it("pins X1's quirk where the frames divide 2^24: a loop and a fudge one apart", _ {
		std::size_t pairs = 0;
		for (const auto& loop : r::kRampSweep) {
			if (loop.kind != r::RampKind::kLoop || ((1u << 24) % static_cast<uint32_t>(loop.frames)) != 0) {
				continue;
			}
			for (const auto& fudge : r::kRampSweep) {
				if (fudge.kind == r::RampKind::kFudge && fudge.frames == loop.frames) {
					expect(fudge.increment).to_equal(loop.increment + 1);
					pairs++;
				}
			}
		}
		expect(pairs > 0 ? 1 : 0).to_equal(1);
	});

	it("holds the C1 sweep's chunk-end rows, which copy nothing", _ {
		std::size_t found = 0;
		for (const auto& row : r::kCacheTailLimitSweep) {
			if (row.chunk_size - row.chunk_offset + 3 * row.channels - 1 <= 3 * row.channels) {
				expect(row.frames).to_equal(0);
				found++;
			}
		}
		expect(found > 0 ? 1 : 0).to_equal(1);
	});

	it("holds abandoned searches, each leaving its offset 0", _ {
		for (const auto& row : r::kAlignFaults) {
			expect(row.abandoned ? 1 : 0).to_equal(1);
			expect(row.offset).to_equal(0);
		}
	});

	it("says which goldens reach each quirk, T3's wrapped share among them", _ {
		expect(r::kQuirkReach.size()).to_equal(std::size_t{12});
		std::size_t wrapped = 0;
		for (const auto& reach : r::kQuirkReach) {
			if (reach.quirk == std::string_view{"T3 share wraps negative"}) {
				wrapped = reach.cases.count;
			}
		}
		expect(wrapped > 0 ? 1 : 0).to_equal(1);
	});
});
// clang-format on

DELUGE_SPEC(time_stretcher_records_spec, time_stretcher_records_spec_body)
