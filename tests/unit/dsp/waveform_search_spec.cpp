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

// The waveform search shared by the time stretcher's and the pitch shifter's aligners
// (dsp/fixed/timestretch/waveform_search.h): that it stops after its shape's sign flips, and abandons where a frame
// cannot be read. The pitch shifter's aligner runs on it, so waveform_aligner_spec and the shifter's workload specs pin
// it against the shifter's reference too.

#include "dsp/fixed/timestretch/waveform_search.h"

#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <vector>

#include "support/spec_main.h"

namespace {
namespace ts = deluge::dsp::fixed::timestretch;

// Frame values that flip the window's summed change from the old totals (all zero) at every slide forward.
struct FlippingReader {
	std::vector<int32_t> values;
	mutable int32_t reads = 0;
	int32_t missing_from = INT32_MAX;
	FlippingReader() {
		values.assign(64, 0);
		int32_t sum = 0;
		for (int32_t k = 0; k + 3 < 64; k++) {
			const int32_t target = (k % 2 == 0) ? -10 : 10;
			values[k + 3] = target - sum + values[k];
			sum = target;
		}
	}
	[[nodiscard]] std::optional<int32_t> folded(int32_t frame) const {
		reads++;
		if (frame < 0 || frame >= 64 || frame >= missing_from) {
			return std::nullopt;
		}
		return values[frame];
	}
	[[nodiscard]] int32_t reach(int32_t direction, int32_t first_read) const {
		return direction == 1 ? 64 - first_read : first_read + 1;
	}
};
static_assert(ts::search_reader<FlippingReader>);

int32_t reads_until_stop(int32_t flips) {
	FlippingReader reader;
	const ts::SearchShape shape{.flips_to_stop = flips, .first_direction = 1, .turns = false, .sub_sample = false};
	(void)ts::search(reader, shape, ts::Averages{}, ts::Averages{}, 1, 40);
	return reader.reads;
}
} // namespace

// clang-format off
describe waveform_search_spec_body("deluge::dsp::fixed::timestretch::search", ${
	it("stops after the shape's flip count: one slide, four reads, per flip", _ {
		expect(reads_until_stop(2)).to_equal(2 * 4);
		expect(reads_until_stop(4)).to_equal(4 * 4);
	});

	it("abandons where a frame cannot be read", _ {
		FlippingReader reader;
		reader.missing_from = 5;
		const ts::SearchShape shape{.flips_to_stop = 40, .first_direction = 1, .turns = false, .sub_sample = false};
		const ts::SearchResult r = ts::search(reader, shape, ts::Averages{}, ts::Averages{}, 1, 40);
		expect(r.abandoned ? 1 : 0).to_equal(1);
	});

	it("searches no further than the reader's reach", _ {
		FlippingReader reader;
		const ts::SearchShape shape{.flips_to_stop = 1000, .first_direction = 1, .turns = false, .sub_sample = false};
		const ts::SearchResult r = ts::search(reader, shape, ts::Averages{}, ts::Averages{}, 1, 1000);
		expect(r.abandoned ? 1 : 0).to_equal(0);
		expect(reader.reads).to_equal((64 - 3) * 4);
	});
});
// clang-format on

DELUGE_SPEC(waveform_search_spec, waveform_search_spec_body)
