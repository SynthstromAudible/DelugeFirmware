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

// The stutter reference (reference_stutter.h) over every workload (stutter_workloads.h): its records, that the
// workloads reach every mode, and that it reads no storage it has not written.

#include "reference_stutter.h"
#include "stutter_workload_records.h"
#include "stutter_workloads.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

namespace sw = stutter_workloads;
using deluge::dsp::test::ReferenceStutter;

/// @return Every block's output of @p workload, its storage starting as @p fill.
[[nodiscard]] std::vector<int32_t> outputs(const sw::Workload& workload, deluge::dsp::StereoSample fill) {
	ReferenceStutter stutter;
	std::vector<int32_t> out;
	sw::play(
	    workload, stutter,
	    [&](std::size_t, std::span<const deluge::dsp::StereoSample> block) {
		    for (const deluge::dsp::StereoSample frame : block) {
			    out.push_back(frame.l);
			    out.push_back(frame.r);
		    }
	    },
	    fill);
	return out;
}

} // namespace

// clang-format off
describe reference_stutter("the stutter reference", ${
	it("renders each workload as recorded", _ {
		for (const sw::Workload& workload : sw::all_workloads()) {
			const sw::WorkloadRecord record = sw::record<ReferenceStutter>(workload);
			expect(record).to_equal(sw::kRecordedReference.at(static_cast<std::size_t>(workload.id)));
		}
	});

	it("lists the workloads in WorkloadId order", _ {
		for (std::size_t i = 0; i < sw::all_workloads().size(); ++i) {
			expect(static_cast<std::size_t>(sw::all_workloads()[i].id)).to_equal(i);
		}
		expect(sw::all_workloads().size()).to_equal(sw::kWorkloadCount);
	});

	it("reaches every mode: recording and playing, native and resampled, reversed, and ping-pong's turns", _ {
		ReferenceStutter::reset_counters();
		for (const sw::Workload& workload : sw::all_workloads()) {
			std::ignore = sw::record<ReferenceStutter>(workload);
		}
		const deluge::dsp::test::StutterCounts& counts = ReferenceStutter::counts;
		expect(counts.record_blocks).to_be_greater_than(std::size_t{0});
		expect(counts.play_blocks).to_be_greater_than(std::size_t{0});
		expect(counts.native_frames).to_be_greater_than(std::size_t{0});
		expect(counts.resampled_frames).to_be_greater_than(std::size_t{0});
		expect(counts.reverse_frames).to_be_greater_than(std::size_t{0});
		expect(counts.flips).to_be_greater_than(std::size_t{0});
	});

	it("finishes recording and plays in every workload", _ {
		for (const sw::Workload& workload : sw::all_workloads()) {
			ReferenceStutter stutter;
			sw::play(workload, stutter, [](std::size_t, std::span<const deluge::dsp::StereoSample>) {});
			expect(stutter.playing()).to_be_true();
		}
	});

	it("passes the input through while it records", _ {
		const sw::Workload& workload = sw::all_workloads()[static_cast<std::size_t>(sw::WorkloadId::RECORD_NATIVE)];
		const std::vector<int32_t> out = outputs(workload, {});
		std::vector<int32_t> in;
		for (std::size_t b = 0; b < 16; ++b) {
			for (const deluge::dsp::StereoSample frame : sw::input(128, b, false, b * 128)) {
				in.push_back(frame.l);
				in.push_back(frame.r);
			}
		}
		expect(std::vector<int32_t>(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(in.size()))).to_equal(in);
		expect(std::vector<int32_t>(out.begin() + static_cast<std::ptrdiff_t>(in.size()), out.end())).not_().to_equal(std::vector<int32_t>(out.size() - in.size(), 0));
	});

	it("reads no storage it has not written: storage starting full of noise renders the same", _ {
		for (const sw::Workload& workload : sw::all_workloads()) {
			expect(outputs(workload, {.l = 0x5a5a5a5a, .r = -0x3c3c3c3c})).to_equal(outputs(workload, {}));
		}
	});
});
// clang-format on

CPPSPEC_MAIN(reference_stutter);
