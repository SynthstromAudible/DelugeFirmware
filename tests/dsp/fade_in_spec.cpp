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

// FadeIn: the filters' fade-in as a mixer kernel, `Mono<FadeIn>` over stereo frames (the level stepped per channel,
// left then right) and `Mono<FadeInMono>` over samples, against the verbatim DryWetFade (reference_filters.h). A whole
// fade at every block length 1–9, in both forms, run only while the fade is active as its owners run it; the fade
// workloads (filter_workloads.h), begun, zeroed, begun again and restarted mid-fade; the events; and the end of the
// fade, the dry level compared with 0.001 as a double.

#include "dsp-next/fixed/filter/fade_in.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "filter_workload_records.h"
#include "filter_workloads.h"
#include "kernel_filters.h"
#include "reference_filters.h"

#include <cmath>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

namespace {

static_assert(mixer_kernel<FadeIn>);
static_assert(mixer_kernel<FadeInMono>);
static_assert(!moded_kernel<FadeIn>);
static_assert(!std::is_polymorphic_v<FadeIn>);
static_assert(mixer<Mono<FadeIn>>);
static_assert(mixer<Mono<FadeInMono>>);
static_assert(std::is_same_v<FadeIn::value_type, StereoSample>);
static_assert(std::is_same_v<FadeInMono::value_type, q31>);
static_assert(std::is_same_v<FadeIn::State, FadeInMono::State>);

using filter_workloads::WorkloadId;

/// The longest run a whole fade takes, in blocks: the fade lasts about 690 steps, and every block steps it at least
/// once.
constexpr size_t kMaxBlocks = 1000;

/// What one comparison of a whole fade found.
struct WholeFade {
	size_t blocks = 0;               ///< How many blocks the fade ran, active before each.
	std::vector<std::string> parted; ///< Each block where the kernel parted from the reference, and what parted.
};

/// @brief A whole fade from `begin` in blocks of @p frames, in stereo or mono, through the kernel's wrapper and the
/// reference, each block rendered only while the fade is active, as the filters run it.
/// @details The dry signal is seeded noise at a voice's level and the wet its complement, so every blend moves.
template <bool kStereo>
WholeFade whole_fade(size_t frames) {
	filters_reference::ReferenceDryWetFade reference;
	kernel_filters::FadeWrapper<kStereo> kernel;
	reference.begin();
	kernel.template on<&FadeIn::begin>();
	const size_t n = frames * (kStereo ? 2 : 1);
	std::vector<q31> dry(n);
	std::vector<q31> wet(n);
	std::vector<q31> out(n);
	std::vector<q31> scratch(n);
	WholeFade result;
	uint32_t k = 0;
	while (result.blocks < kMaxBlocks) {
		const bool active = reference.active();
		if (FadeIn::active(kernel.state()) != active) {
			result.parted.push_back("block " + std::to_string(result.blocks) + ": active");
		}
		if (!active) {
			break;
		}
		for (size_t i = 0; i < n; ++i) {
			dry[i] = static_cast<q31>(filter_workloads::detail::mix(k++)) >> 4;
			wet[i] = filter_workloads::fade_wet(dry[i]);
		}
		std::ranges::copy(wet, out.begin());
		std::ranges::copy(dry, scratch.begin());
		reference.blend(out, scratch);
		const std::vector<q31> expected = out;
		std::ranges::copy(wet, out.begin());
		kernel_filters::blend<kStereo>(kernel, out, dry);
		if (out != expected) {
			result.parted.push_back("block " + std::to_string(result.blocks) + ": output");
		}
		if (kernel.state().dry != reference.dry() || kernel.state().wet != reference.wet()) {
			result.parted.push_back("block " + std::to_string(result.blocks) + ": state");
		}
		++result.blocks;
	}
	return result;
}

/// @return Each fade workload where the kernels part from the reference, named with the block and what parted.
std::vector<std::string> partings() {
	std::vector<std::string> parted;
	for (const WorkloadId id : {WorkloadId::FADE_MONO, WorkloadId::FADE_STEREO}) {
		const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
		const auto parting = kernel_filters::first_parting(workload);
		if (parting) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
			                 + parting->what);
		}
	}
	return parted;
}

} // namespace

// clang-format off
describe fade_in("FadeIn", ${
	it("starts where the legacy fade does: dry 1 and wet kOneQ31", _ {
		const FadeIn::State s{};
		expect(s.dry).to_equal(1.0F);
		expect(s.wet).to_equal(kOneQ31);
	});

	it("begins at dry 1 and wet 0, and zeroes to dry 0 and wet 0, which ends the fade", _ {
		FadeIn::State s{.dry = 0.5F, .wet = 123};
		FadeIn::begin(s);
		expect(s.dry).to_equal(1.0F);
		expect(s.wet).to_equal(q31{0});
		expect(FadeIn::active(s)).to_be_true();
		FadeIn::zero(s);
		expect(s.dry).to_equal(0.0F);
		expect(s.wet).to_equal(q31{0});
		expect(FadeIn::active(s)).to_be_false();
	});

	it("is active while the dry level, compared as a double, is not below 0.001", _ {
		expect(FadeIn::active({.dry = 0.001F, .wet = 0})).to_be_true();
		expect(FadeIn::active({.dry = std::nextafter(0.001F, 0.0F), .wet = 0})).to_be_false();
		expect(FadeIn::active({.dry = std::nanf(""), .wet = 0})).to_be_true();
	});

	it("fades in as the legacy fade does, a whole fade at every block length 1–9, stepping per channel in stereo", _ {
		for (size_t frames = 1; frames <= 9; ++frames) {
			const WholeFade stereo = whole_fade<true>(frames);
			expect(stereo.parted).to_equal(std::vector<std::string>{});
			expect(stereo.blocks).to_be_greater_than(size_t{0});
			expect(stereo.blocks).to_be_less_than(kMaxBlocks);
			const WholeFade mono = whole_fade<false>(frames);
			expect(mono.parted).to_equal(std::vector<std::string>{});
			expect(mono.blocks).to_be_less_than(kMaxBlocks);
			// Stereo steps the level twice per frame, so its fade ends in about half the frames.
			expect(stereo.blocks * 2).to_be_less_than(mono.blocks + 2);
		}
	});

	it("renders the fade workloads as the reference does, mono and stereo: each block's output, and its levels", _ {
		expect(partings()).to_equal(std::vector<std::string>{});
	});

	it("renders the fade workloads as recorded", _ {
		for (const WorkloadId id : {WorkloadId::FADE_MONO, WorkloadId::FADE_STEREO}) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			expect(kernel_filters::record(workload)).to_equal(filter_workloads::kRecorded.at(static_cast<size_t>(id)));
		}
	});
});
// clang-format on

CPPSPEC_MAIN(fade_in);
