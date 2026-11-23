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

// The time stretcher's hop planner (dsp/fixed/timestretch/hop_planner.h) against the legacy stretcher's records: the
// jitter (B1), the repitched widths (B2), the perc search (B3), the back edge (B4), the hop and crossfade lengths (B5),
// the past-end test (B6), the pre-margin loop crossfade (B7), the start clamp (B8), and each recorded hop's plan whole.

#include "dsp/fixed/timestretch/hop_planner.h"

#include "time_stretcher_records.h"

#include <cppspec.hpp>
#include <cstdint>
#include <vector>

#include "support/spec_main.h"

namespace {
using deluge::dsp::Q;
using deluge::dsp::fixed::Cong;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::Increment;
namespace ts = deluge::dsp::fixed::timestretch;
namespace r = time_stretcher_records;

Direction dir(int32_t d) {
	return d == 1 ? Direction::FORWARD : Direction::REVERSE;
}

ts::HopShape shape_of(int32_t min, int32_t max, int32_t share, int32_t quarters, int32_t jitter) {
	return {min, max, Q<31>::from_raw(share), quarters, Q<23>::from_raw(jitter)};
}

ts::PercWindow perc_of(r::Span s, int32_t origin, int32_t earliest, int32_t latest) {
	return {{&r::kPercBytes[s.offset], s.count}, origin, earliest, latest};
}

template <class Rows>
int32_t hop_length_mismatches(const Rows& rows) {
	int32_t bad = 0;
	for (const auto& row : rows) {
		const auto l = ts::hop_lengths(row.best, Increment::from_raw(row.increment),
		                               shape_of(0, 0, row.crossfade_share, row.crossfade_quarters, 0), row.ceiling);
		bad += (l.hop_frames != row.hop_frames || l.crossfade_frames != row.crossfade
		        || l.ramp.increment != row.ramp_increment)
		           ? 1
		           : 0;
	}
	return bad;
}
} // namespace

// clang-format off
describe stretch_hop_planner_spec_body("deluge::dsp::fixed::timestretch::plan_hop", ${
	it("jitters the minimum width with one draw as the legacy did (B1)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kJitterGoldens) {
			Cong noise(row.noise_before);
			Cong once(row.noise_before);
			(void)once.next();
			const ts::HopShape s = ts::jittered(shape_of(row.min_width, 9999, 0, 0, row.jitter_share), noise);
			bad += (s.min_width != row.min_after || s.max_width != 9999 || noise.state() != once.state()) ? 1 : 0;
		}
		expect(bad).to_equal(0);
	});

	it("repitches the widths (B2)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kRepitchGoldens) {
			const ts::Widths w = ts::repitched(shape_of(row.min_width, row.max_width, 0, 0, 0), Increment::from_raw(row.increment));
			bad += (w.min != row.min || w.max != row.max || w.best != row.best) ? 1 : 0;
		}
		expect(bad).to_equal(0);
	});

	it("finds the best width over the perc window (B3)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kBeamGoldens) {
			const ts::Widths w{row.min, row.max, (row.min + row.max) >> 1};
			bad += ts::best_width(perc_of(row.perc, row.origin, row.earliest, row.latest), w, ts::Ratio::from_raw(row.ratio),
			                      row.position_frames, dir(row.direction)) == row.best ? 0 : 1;
		}
		expect(r::kBeamGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("places the back edge, and says whether it is past the end (B4, B6)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kBackEdgeGoldens) {
			const int32_t back = ts::back_edge(row.best, ts::Ratio::from_raw(row.ratio), row.position_frames, dir(row.direction),
			                                   row.length_frames);
			bad += (back != row.back || ts::past_end(back, dir(row.direction), row.length_frames) != row.past_end) ? 1 : 0;
		}
		expect(bad).to_equal(0);
	});

	it("sizes the hop and its crossfade (B5)", _ {
		expect(hop_length_mismatches(r::kHopLengthGoldens)).to_equal(0);
		expect(hop_length_mismatches(r::kHopLengthSweep)).to_equal(0);
	});

	it("plans the pre-margin loop crossfade (B7)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kLoopCrossfadeGoldens) {
			const ts::LoopPlan p = ts::loop_crossfade({row.loop_end_frame, row.loop_start_frame, row.cache_in_use},
			                                          row.position_frames, row.combined, ts::Ratio::from_raw(row.ratio),
			                                          Increment::from_raw(row.increment), row.min_jittered,
			                                          dir(row.direction), row.length_frames);
			bad += static_cast<uint8_t>(p.kind) == row.kind ? 0 : 1;
			if (row.kind == 1) {
				bad += (p.start_frame != row.start_frame || p.crossfade_frames != row.crossfade
				        || p.hop_frames != row.hop_frames || p.align != row.align) ? 1 : 0;
			}
			if (row.kind == 2) {
				bad += p.ceiling == row.ceiling ? 0 : 1;
			}
		}
		expect(bad).to_equal(0);
	});

	it("clamps a start before the marker whose frame is not resident (B8)", _ {
		int32_t bad = 0;
		for (const auto* rows : {&r::kStartClampGoldens}) {
			for (const auto& row : *rows) {
				bad += ts::clamp_start(row.start, row.start_marker, dir(row.direction), row.resident) == row.result ? 0 : 1;
			}
		}
		for (const auto& row : r::kStartClampFaults) {
			bad += ts::clamp_start(row.start, row.start_marker, dir(row.direction), row.resident) == row.result ? 0 : 1;
		}
		expect(r::kStartClampFaults.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("plans each recorded hop whole", _ {
		int32_t bad = 0;
		for (const auto& row : r::kHopPlanGoldens) {
			ts::HopRequest req{
			    .position_frames = row.position_frames,
			    .ratio = ts::Ratio::from_raw(row.ratio),
			    .increment = Increment::from_raw(row.increment),
			    .combined = row.combined,
			    .direction = dir(row.direction),
			    .length_frames = row.length_frames,
			    .shape = shape_of(row.min_width, row.max_width, row.crossfade_share, row.crossfade_quarters, row.jitter_share),
			    .loop = std::nullopt,
			};
			if (row.has_loop) {
				req.loop = ts::LoopAhead{row.loop_end_frame, row.loop_start_frame, row.cache_in_use};
			}
			const ts::PercWindow perc = perc_of(row.perc, row.origin, row.earliest, row.latest);
			const ts::HopPlan p = ts::plan_hop(req, row.has_perc ? &perc : nullptr);
			bad += (static_cast<uint8_t>(p.kind) != row.kind || p.start_frame != row.start_frame || p.hop_frames != row.hop_frames
			        || p.crossfade_frames != row.crossfade || p.ramp.increment != row.ramp_increment
			        || p.max_hop_frames != row.max_hop_frames) ? 1 : 0;
		}
		expect(r::kHopPlanGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("silences a head that would start at or past the end (B6)", _ {
		expect(ts::past_end(1000, Direction::FORWARD, 1000) ? 1 : 0).to_equal(1);
		expect(ts::past_end(-1, Direction::REVERSE, 1000) ? 1 : 0).to_equal(1);
		expect(ts::past_end(999, Direction::FORWARD, 1000) ? 1 : 0).to_equal(0);
	});

	it("reads a saturated ratio unsigned (decision 3)", _ {
		expect(ts::back_edge(1000, ts::Ratio::from_raw(0xFFFFFFFFu), 10000, Direction::FORWARD, 1 << 30)).to_equal(137499);
		std::vector<uint8_t> ones(64, 1);
		const ts::PercWindow perc{ones, 0, 0, 63};
		const ts::Widths w{100, 300, 200};
		const int32_t best = ts::best_width(perc, w, ts::Ratio::from_raw(0xFFFFFFFFu), 32 * 128, Direction::FORWARD);
		expect(best >= 100 && best <= 300 ? 1 : 0).to_equal(1);
	});

	it("gives a one-frame hop a one-frame crossfade (decision 3)", _ {
		const auto l = ts::hop_lengths(1, Increment::from_raw(1u << 24), ts::hop_shape(ts::Ratio::from_raw(1u << 24)), INT32_MAX);
		expect(l.hop_frames).to_equal(1);
		expect(l.crossfade_frames).to_equal(1);
		expect(l.ramp.increment).to_equal(16777216u);
	});

	it("chooses the default width with no perc window", _ {
		const ts::HopShape shape = ts::hop_shape(ts::Ratio::from_raw(1u << 24));
		const ts::HopRequest req{.position_frames = 50000, .ratio = ts::Ratio::from_raw(1u << 24),
		                         .increment = Increment::from_raw(1u << 24), .combined = 1u << 24,
		                         .direction = Direction::FORWARD, .length_frames = 1 << 20, .shape = shape, .loop = std::nullopt};
		const ts::Widths w = ts::repitched(shape, req.increment);
		expect(ts::plan_hop(req, nullptr).hop_frames).to_equal(ts::hop_lengths(w.best, req.increment, shape, INT32_MAX).hop_frames);
	});
});
// clang-format on

DELUGE_SPEC(stretch_hop_planner_spec, stretch_hop_planner_spec_body)
