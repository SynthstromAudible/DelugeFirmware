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

// The time stretcher's crossfade ramps and head gains (dsp/fixed/timestretch/crossfade.h) against the legacy
// stretcher's records: each ramp's step (X1) over the goldens and a sweep of frame counts, and each segment's head
// gains (X2) over the goldens.

#include "dsp/fixed/timestretch/crossfade.h"

#include "time_stretcher_records.h"

#include <cppspec.hpp>
#include <cstdint>
#include <type_traits>

#include "support/spec_main.h"

namespace {
using deluge::dsp::Q;
using deluge::dsp::fixed::timestretch::CrossfadeRamp;
using deluge::dsp::fixed::timestretch::HeadGains;
using deluge::dsp::fixed::timestretch::segment_gains;
namespace r = time_stretcher_records;

static_assert(std::is_trivially_copyable_v<CrossfadeRamp> && sizeof(CrossfadeRamp) == 8);

CrossfadeRamp ramp_for(r::RampKind kind, int32_t frames) {
	switch (kind) {
	case r::RampKind::kHop:
		return CrossfadeRamp::over_hop(frames);
	case r::RampKind::kLoop:
		return CrossfadeRamp::over_loop(frames);
	case r::RampKind::kFudge:
		return CrossfadeRamp::over_fudge(frames);
	case r::RampKind::kCacheTail:
		break;
	}
	return CrossfadeRamp::over_cache_tail(frames);
}

template <class Rows>
int32_t ramp_mismatches(const Rows& rows) {
	int32_t bad = 0;
	for (const auto& row : rows) {
		const CrossfadeRamp ramp = ramp_for(row.kind, row.frames);
		bad += (ramp.increment != row.increment || ramp.progress != 0) ? 1 : 0;
	}
	return bad;
}
} // namespace

// clang-format off
describe stretch_crossfade_spec_body("deluge::dsp::fixed::timestretch::CrossfadeRamp", ${
	it("steps each ramp as the legacy did", _ {
		expect(ramp_mismatches(r::kRampGoldens)).to_equal(0);
		expect(ramp_mismatches(r::kRampSweep)).to_equal(0);
	});

	it("gives the hand-checked increments", _ {
		expect(CrossfadeRamp::over_hop(1024).increment).to_equal(16384u);
		expect(CrossfadeRamp::over_loop(1024).increment).to_equal(16384u);
		expect(CrossfadeRamp::over_fudge(1024).increment).to_equal(16385u);
		expect(CrossfadeRamp::over_hop(1000).increment).to_equal(16777u);
		expect(CrossfadeRamp::over_loop(1000).increment).to_equal(16778u);
		expect(CrossfadeRamp::over_fudge(1000).increment).to_equal(16778u);
		expect(CrossfadeRamp::over_cache_tail(255).increment).to_equal(65794u);
		expect(CrossfadeRamp::over_hop(1).increment).to_equal(16777216u);
	});

	it("is done when default-made and once progress reaches 2^24", _ {
		expect(CrossfadeRamp{}.done() ? 1 : 0).to_equal(1);
		expect(CrossfadeRamp{(1u << 24) - 1, 0}.done() ? 1 : 0).to_equal(0);
	});

	it("gives each segment's head gains as the legacy did", _ {
		int32_t bad = 0;
		for (const auto& row : r::kSegmentGoldens) {
			CrossfadeRamp ramp{row.progress, row.increment};
			const HeadGains g = segment_gains(ramp, row.frames, Q<31>::from_raw(row.level), Q<31>::from_raw(row.level_step));
			bad += (ramp.progress != row.progress_after || g.newer.raw() != row.newer || g.newer_step.raw() != row.newer_step
			        || g.older.raw() != row.older || g.older_step.raw() != row.older_step) ? 1 : 0;
		}
		expect(r::kSegmentGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(stretch_crossfade_spec, stretch_crossfade_spec_body)
