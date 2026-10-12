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

// The delay's tapes read only what they have written: every recorded workload renders the same over fresh storage
// holding zeros, the allocator's fill or a second pattern, so the delay's arena may hand out recycled storage without
// zeroing it. The tapes' digest at the end is left out: it hashes the whole storage, the resampled write's run-off
// included, which nothing reads.

#include "delay_workloads.h"
#include "dsp-next/fixed/delay/delay_processor.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayProcessor;

namespace {

/// @return Sample @p i of storage nothing has written: zeros.
StereoSample zeroed(size_t /*i*/) {
	return StereoSample{.l = 0, .r = 0};
}

/// @return Sample @p i of storage another tape left behind: large values of both signs.
StereoSample poisoned(size_t i) {
	return StereoSample{.l = 0x5A5A5A5A ^ static_cast<int32_t>(i), .r = -0x5A5A5A5B ^ static_cast<int32_t>(i)};
}

} // namespace

// clang-format off
describe delay_poisoned_storage("a delay tape over storage it hasn't written", ${
	it("renders every workload the same whatever its fresh storage holds", _ {
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const delay_workloads::WorkloadRecord clean = delay_workloads::record<DelayProcessor>(workload, zeroed);
			for (const delay_workloads::Fill& fill :
			     {delay_workloads::Fill{poisoned}, delay_workloads::Fill{delay_workloads::allocator_sample}}) {
				const delay_workloads::WorkloadRecord dirty = delay_workloads::record<DelayProcessor>(workload, fill);
				expect(dirty.outputs).to_equal(clean.outputs);
				expect(dirty.lifecycle).to_equal(clean.lifecycle);
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(delay_poisoned_storage);
