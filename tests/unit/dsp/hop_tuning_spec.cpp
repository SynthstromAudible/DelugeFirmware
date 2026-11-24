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

// The time stretcher's hop tuning (dsp/fixed/timestretch/hop_tuning.h) against the legacy stretcher's records: every
// ratio the goldens' hops asked for, and a sweep of the whole range, each knot and both sides of each band edge (T1 to
// T3 of docs/dev/time_stretcher_contracts.md).

#include "dsp/fixed/timestretch/hop_tuning.h"

#include "time_stretcher_records.h"

#include <cppspec.hpp>
#include <cstdint>
#include <type_traits>

#include "support/spec_main.h"

namespace {
using deluge::dsp::fixed::timestretch::hop_shape;
using deluge::dsp::fixed::timestretch::HopShape;
using deluge::dsp::fixed::timestretch::Ratio;
namespace r = time_stretcher_records;

static_assert(std::is_trivially_copyable_v<HopShape>);

// The rows of @p rows whose shape differs from the record's.
template <class Rows>
int32_t mismatches(const Rows& rows) {
	int32_t bad = 0;
	for (const auto& row : rows) {
		const HopShape s = hop_shape(Ratio::from_raw(row.ratio));
		bad += (s.min_width != row.min_width || s.max_width != row.max_width
		        || s.crossfade_share.raw() != row.crossfade_share || s.crossfade_quarters != row.crossfade_quarters
		        || s.jitter_share.raw() != row.jitter_share)
		           ? 1
		           : 0;
	}
	return bad;
}
} // namespace

// clang-format off
describe hop_tuning_spec_body("deluge::dsp::fixed::timestretch::hop_shape", ${
	it("reads every ratio the goldens' hops asked for as the legacy did", _ {
		expect(r::kHopShapeGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(mismatches(r::kHopShapeGoldens)).to_equal(0);
	});

	it("reads the whole ratio range, each knot and both sides of each band edge, as the legacy did", _ {
		expect(mismatches(r::kHopShapeSweep)).to_equal(0);
	});

	it("reads the 0-semitone knot exactly", _ {
		const HopShape s = hop_shape(Ratio::from_raw(1u << 24));
		expect(s.min_width).to_equal(3000);
		expect(s.max_width).to_equal(11000);
		expect(s.crossfade_share.raw()).to_equal(0);
		expect(s.crossfade_quarters).to_equal(60);
		expect(s.jitter_share.raw()).to_equal(0);
	});

	it("wraps a share entry of 128 and over negative at a ratio of 0.5 (T3)", _ {
		const HopShape s = hop_shape(Ratio::from_raw(1u << 23));
		expect(s.min_width).to_equal(3000);
		expect(s.max_width).to_equal(6500);
		expect(s.crossfade_share.raw()).to_equal(-1610612736);
		expect(s.crossfade_quarters).to_equal(10);
		expect(s.jitter_share.raw()).to_equal(7864320);
	});

	it("clamps beyond two octaves either way", _ {
		const HopShape low = hop_shape(Ratio::from_raw(1u << 22));
		const HopShape high = hop_shape(Ratio::from_raw(1u << 26));
		int32_t bad = 0;
		for (uint32_t q = 1; q <= (1u << 22); q += (1u << 14)) {
			bad += hop_shape(Ratio::from_raw(q)) == low ? 0 : 1;
		}
		for (uint64_t q = 1u << 26; q <= 0xFFFFFFFFu; q += (1u << 26)) {
			bad += hop_shape(Ratio::from_raw(static_cast<uint32_t>(q))) == high ? 0 : 1;
		}
		bad += hop_shape(Ratio::from_raw(0xFFFFFFFFu)) == high ? 0 : 1;
		expect(bad).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(hop_tuning_spec, hop_tuning_spec_body)
