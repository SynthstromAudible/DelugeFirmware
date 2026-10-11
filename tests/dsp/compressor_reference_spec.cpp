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

// The compressor's verbatim reference (reference_compressor.h) against the firmware's compressor, over every recorded
// workload (compressor_workloads.h): each block's output and gain reduction, and the whole `view()`, must agree. What
// the reference renders is pinned (compressor_workload_records.h), and the firmware's renders must equal the same pins.

#include "compressor_workload_records.h"
#include "compressor_workloads.h"
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"
#include "reference_compressor.h"

#include <cppspec.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fixed = deluge::dsp::fixed;
namespace cw = compressor_workloads;
using compressor_reference::ReferenceRmsFeedbackCompressor;

/// @return @p parting named with @p workload's name and the block.
[[nodiscard]] std::string named(std::string_view workload, const cw::Parting& parting) {
	return std::string{workload} + " block " + std::to_string(parting.block) + ": " + parting.what;
}

/// @return Each workload where compressor @p B parts from compressor @p A, named with the block and what parted.
template <typename A, typename B>
[[nodiscard]] std::vector<std::string> partings() {
	std::vector<std::string> parted;
	for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
		const auto parting = cw::first_parting<A, B>(workload);
		if (parting) {
			parted.push_back(named(workload.name, *parting));
		}
	}
	return parted;
}

} // namespace

// clang-format off
describe verbatim_compressor("the compressor's verbatim reference", ${
	it("holds every workload once, in id order", _ {
		const auto& workloads = cw::all_compressor_workloads();
		expect(workloads.size()).to_equal(cw::kCompressorWorkloadCount);
		for (size_t i = 0; i < workloads.size(); ++i) {
			expect(static_cast<size_t>(workloads[i].id)).to_equal(i);
		}
	});

	it("renders every workload as today's compressor does: each block's output and gain reduction, and the whole "
	   "view", _ {
		expect(partings<ReferenceRmsFeedbackCompressor, fixed::RmsFeedbackCompressor>())
		    .to_equal(std::vector<std::string>{});
	});

	it("renders every workload as recorded: each block's output and gain reduction, and the state", _ {
		for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
			const cw::WorkloadRecord& recorded = cw::kCompressorRecorded.at(static_cast<size_t>(workload.id));
			expect(recorded.outputs.size()).to_equal(workload.blocks);
			expect(cw::record<ReferenceRmsFeedbackCompressor>(workload)).to_equal(recorded);
			expect(cw::record<fixed::RmsFeedbackCompressor>(workload)).to_equal(recorded);
		}
	});
});
// clang-format on

CPPSPEC_MAIN(verbatim_compressor);
