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

// Svf: the state-variable filter as a processor kernel, `Mono<Svf>` and `Stereo<Svf>` with the fade-in, against the
// verbatim reference (reference_filters.h) over every SVF workload (filter_workloads.h): band, notch, the two
// alternating across the half-way morph with loud blocks, and a fade begun, restarted, reset and zeroed, at block
// lengths cycling 1–9, 24, 64 and 128. Each block's output, the gain, and the whole state (integrators, coefficients
// and fade) must agree, and what the kernels render must equal the record.

#include "dsp-next/fixed/filter/svf.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "filter_workload_records.h"
#include "filter_workloads.h"
#include "kernel_filters.h"
#include "reference_filters.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

namespace {

static_assert(processor_kernel<Svf>);
static_assert(moded_kernel<Svf>);
static_assert(!std::is_polymorphic_v<Svf>);
static_assert(processor<Mono<Svf>>);
static_assert(processor<Stereo<Svf>>);
static_assert(Svf::Coefficients{}.mode == Svf::Mode::BAND);

using filter_workloads::WorkloadId;

/// Every SVF workload: each mode steady, the modes switching, and the fade, mono and stereo.
constexpr std::array<WorkloadId, 8> kSvfWorkloads{
    WorkloadId::SVF_BAND_MONO,    WorkloadId::SVF_BAND_STEREO,      WorkloadId::SVF_NOTCH_MONO,
    WorkloadId::SVF_NOTCH_STEREO, WorkloadId::SVF_MODE_SWITCH_MONO, WorkloadId::SVF_MODE_SWITCH_STEREO,
    WorkloadId::SVF_FADE_MONO,    WorkloadId::SVF_FADE_STEREO,
};

/// @return Each SVF workload where the kernels part from the reference, named with the block and what parted.
std::vector<std::string> partings() {
	std::vector<std::string> parted;
	for (const WorkloadId id : kSvfWorkloads) {
		const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
		const auto parting = kernel_filters::first_parting(workload);
		if (parting) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
			                 + parting->what);
		}
	}
	return parted;
}

/// Settings across the range: dark and resonant, mid, and bright with the morph past half.
const std::vector<FilterConfig> kConfigs{
    {.frequency = 1 << 22, .resonance = 0, .morph = 0, .allow_oversampling = true},
    {.frequency = 1 << 24, .resonance = 1 << 28, .morph = 1 << 27, .allow_oversampling = true},
    {.frequency = 50000000, .resonance = 536870911, .morph = (1 << 29) - 1, .allow_oversampling = false},
};

} // namespace

// clang-format off
describe svf("Svf", ${
	it("renders every SVF workload as the reference does, mono and stereo: each block's output and gain, and the whole "
	   "state", _ {
		expect(partings()).to_equal(std::vector<std::string>{});
	});

	it("renders every SVF workload as recorded", _ {
		for (const WorkloadId id : kSvfWorkloads) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			expect(kernel_filters::record(workload)).to_equal(filter_workloads::kRecorded.at(static_cast<size_t>(id)));
		}
	});

	it("passes the gain through as its compensation, as the reference's configure returns it", _ {
		for (const FilterConfig& config : kConfigs) {
			for (const SvfMode mode : {SvfMode::BAND, SvfMode::NOTCH}) {
				for (const q31 gain : {q31{0}, q31{1 << 29}, q31{kOneQ31}}) {
					filters_reference::ReferenceStateVariableFilter reference;
					const Svf::Coefficients c = Svf::coefficients({.gain = gain, .config = config, .mode = mode});
					expect(Svf::gain(c)).to_equal(reference.configure(gain, config, mode));
					expect(Svf::gain(c)).to_equal(gain);
				}
			}
		}
	});

	it("maps the slot's mode onto its own", _ {
		const FilterConfig config = kConfigs[1];
		expect(Svf::coefficients({.gain = 0, .config = config, .mode = SvfMode::BAND}).mode).to_equal(Svf::Mode::BAND);
		expect(Svf::coefficients({.gain = 0, .config = config, .mode = SvfMode::NOTCH}).mode)
		    .to_equal(Svf::Mode::NOTCH);
	});
});
// clang-format on

CPPSPEC_MAIN(svf);
