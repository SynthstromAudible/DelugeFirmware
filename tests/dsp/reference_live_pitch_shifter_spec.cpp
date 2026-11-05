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

// The live pitch shifter's oracle (reference_live_pitch_shifter.h) over every workload (pitch_shift_workloads.h): it
// makes the records the legacy classes made (pitch_shift_workload_records.h), and the workloads reach every path.

#include "pitch_shift_workload_records.h"
#include "pitch_shift_workloads.h"
#include "reference_live_pitch_shifter.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

namespace pw = pitch_shift_workloads;
namespace ref = deluge::dsp::test::reference_pitch_shift;

[[nodiscard]] const pw::WorkloadRecord& recorded(const pw::Workload& w) {
	return pw::kRecordedReference.at(static_cast<std::size_t>(w.id));
}

} // namespace

// clang-format off
describe reference_live_pitch_shifter("the live pitch shifter's oracle", ${
	it("lists the workloads in WorkloadId order", _ {
		expect(pw::all_workloads().size()).to_equal(pw::kWorkloadCount);
		for (std::size_t i = 0; i < pw::all_workloads().size(); ++i) {
			expect(static_cast<std::size_t>(pw::all_workloads()[i].id)).to_equal(i);
		}
	});

	it("the oracle renders each workload as recorded", _ {
		for (const pw::Workload& w : pw::all_workloads()) {
			expect(pw::record<pw::ReferenceSystem>(w)).to_equal(recorded(w));
		}
	});

	it("the workloads reach every path", _ {
		ref::reset_counts();
		for (const pw::Workload& w : pw::all_workloads()) {
			std::ignore = pw::record<pw::ReferenceSystem>(w);
		}
		const ref::Counts& c = ref::counts;
		expect(c.hops_up).to_be_greater_than(std::size_t{0});
		expect(c.hops_down).to_be_greater_than(std::size_t{0});
		expect(c.cuts).to_be_greater_than(std::size_t{0});
		expect(c.shortenings).to_be_greater_than(std::size_t{0});
		expect(c.sign_flips).to_be_greater_than(std::size_t{0});
		expect(c.early_flips).to_be_greater_than(std::size_t{0});
		expect(c.sub_sample_offsets).to_be_greater_than(std::size_t{0});
		expect(c.direct_blocks).to_be_greater_than(std::size_t{0});
		expect(c.sinc_blocks).to_be_greater_than(std::size_t{0});
		expect(c.linear_blocks).to_be_greater_than(std::size_t{0});
	});
});
// clang-format on

CPPSPEC_MAIN(reference_live_pitch_shifter);
