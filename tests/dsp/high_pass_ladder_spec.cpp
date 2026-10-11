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

// HighPassLadder: the transistor-ladder high-pass as a processor kernel, `Mono` and `Stereo`. Against the verbatim
// reference (reference_filters.h) over every high-pass workload (filter_workloads.h): each block's output, the gain,
// the CONG draws (none) and the whole state, the saturator's position included, at block lengths cycling 1–9, 24, 64
// and 128; and what it renders must equal the record. Every coefficient equals the reference's configure across the
// cutoff and resonance range, the saturator switches at the reference's resonances, and every switch between bypass,
// tanh and the antialiased tanh carries the state.

#include "dsp-next/fixed/filter/high_pass_ladder.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/shaping/tanh.h" // kTanhCentre
#include "filter_workload_records.h"
#include "filter_workloads.h"
#include "kernel_filters.h"
#include "reference_filters.h"
#include "util/waves.h" // jcong, CONG

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace deluge::dsp::fixed;

namespace {

constexpr int32_t kGain = 536870912;

/// Two sines (3 and 11 cycles per 64 samples, at 2^25 and 2^24): a mono block, or 32 interleaved L/R frames.
const std::vector<int32_t> kInput{
    0,         24536521,  32591589,  24293472,  15500126,  20423895,  36182765,  45647162,  35589850,  10947281,
    -9908702,  -13932263, -6420363,  -4590277,  -18578569, -40018314, -50331648, -40018314, -18578569, -4590277,
    -6420363,  -13932263, -9908702,  10947281,  35589850,  45647162,  36182765,  20423895,  15500126,  24293472,
    32591589,  24536521,  0,         -24536521, -32591589, -24293472, -15500126, -20423895, -36182765, -45647162,
    -35589850, -10947281, 9908702,   13932263,  6420363,   4590277,   18578569,  40018314,  50331648,  40018314,
    18578569,  4590277,   6420363,   13932263,  9908702,   -10947281, -35589850, -45647162, -36182765, -20423895,
    -15500126, -24293472, -32591589, -24536521};

using deluge::dsp::Mono;
using deluge::dsp::q31;
using deluge::dsp::Stereo;
using filter_workloads::WorkloadId;
using filters_reference::ReferenceHighPassLadder;
using Ladder = HighPassLadder;

static_assert(deluge::dsp::processor_kernel<Ladder>);
static_assert(deluge::dsp::moded_kernel<Ladder>);
static_assert(!std::is_polymorphic_v<Ladder>);
static_assert(deluge::dsp::processor<Mono<Ladder>>);
static_assert(deluge::dsp::processor<Stereo<Ladder>>);

/// Every high-pass workload: the saturator bypassed, tanh, the antialiased tanh, the resonance swept through all three,
/// and the fade, mono and stereo.
constexpr std::array<WorkloadId, 10> kHighPassWorkloads{
    WorkloadId::HP_MONO,
    WorkloadId::HP_STEREO,
    WorkloadId::HP_TANH_MONO,
    WorkloadId::HP_TANH_STEREO,
    WorkloadId::HP_SATURATED_MONO,
    WorkloadId::HP_SATURATED_STEREO,
    WorkloadId::HP_RESONANCE_SWEEP_MONO,
    WorkloadId::HP_RESONANCE_SWEEP_STEREO,
    WorkloadId::HP_FADE_MONO,
    WorkloadId::HP_FADE_STEREO,
};

/// @return Each of @p ids where the kernel parts from the reference, named with the block and what parted.
std::vector<std::string> partings(std::span<const WorkloadId> ids) {
	std::vector<std::string> parted;
	for (const WorkloadId id : ids) {
		const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
		const auto parting = kernel_filters::first_parting(workload);
		if (parting) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
			                 + parting->what);
		}
	}
	return parted;
}

/// @return The high-pass settings at @p resonance (Q29), the cutoff and morph mid-range.
FilterConfig config_at(q31 resonance) {
	return {.frequency = 1 << 23, .resonance = resonance, .morph = 1 << 26, .allow_oversampling = true};
}

/// The saturator's three settings: bypassed, tanh and the antialiased tanh, by resonance.
constexpr std::array<q31, 3> kSaturations{filter_workloads::detail::kHpBypass, filter_workloads::detail::kHpTanh,
                                          filter_workloads::detail::kHpSaturated};

/// @return The resonances (Q29) at which the kernel's saturator first leaves BYPASS and first leaves TANH, found by
/// bisection. The processed resonance the switch reads does not depend on the cutoff.
std::vector<q31> mode_changes() {
	const auto mode = [](q31 resonance) {
		return Ladder::coefficients({.gain = kGain, .config = config_at(resonance)}).mode;
	};
	std::vector<q31> changes;
	for (const Ladder::Mode below : {Ladder::Mode::BYPASS, Ladder::Mode::TANH}) {
		q31 low = 0;
		q31 high = 536870911;
		while (high - low > 1) {
			const q31 middle = low + ((high - low) / 2);
			(mode(middle) <= below ? low : high) = middle;
		}
		changes.push_back(high);
	}
	return changes;
}

/// The configs the coefficient check sweeps: dark to bright, resonance off, each saturation and past the limit, morph
/// off, low and high.
std::vector<FilterConfig> sweep_configs() {
	std::vector<FilterConfig> configs;
	for (const q31 frequency : {q31{1 << 20}, q31{1 << 22}, q31{1 << 23}, q31{1 << 24}, q31{1 << 26}, q31{ONE_Q31}}) {
		for (const q31 resonance : {q31{0}, filter_workloads::detail::kHpBypass, filter_workloads::detail::kHpTanh,
		                            filter_workloads::detail::kHpSaturated, q31{536870911}, q31{1 << 30}}) {
			for (const q31 morph : {q31{0}, q31{1 << 26}, q31{536870911}}) {
				configs.push_back(
				    {.frequency = frequency, .resonance = resonance, .morph = morph, .allow_oversampling = true});
			}
		}
	}
	return configs;
}

/// A channel's three sections' memories (Q31) and the saturator's position (unsigned over 32 bits).
using ChannelWords = std::array<int64_t, 4>;

ChannelWords words(const Ladder::State& s) {
	return {s.high_pass_1.z, s.low_pass_1.z, s.high_pass_3.z, s.saturator.position};
}

ChannelWords words(const ReferenceHighPassLadder::Channel& c) {
	return {c.high_pass_1.memory(), c.low_pass_1.memory(), c.high_pass_3.memory(), c.last_working_value};
}

} // namespace

// clang-format off
describe high_pass_ladder("HighPassLadder", ${
	it("renders every high-pass workload as the reference does, mono and stereo: each block's output, gain and CONG "
	   "draws, and the whole state", _ {
		expect(partings(kHighPassWorkloads)).to_equal(std::vector<std::string>{});
	});

	it("renders every high-pass workload as recorded", _ {
		for (const WorkloadId id : kHighPassWorkloads) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			expect(kernel_filters::record(workload)).to_equal(filter_workloads::kRecorded.at(static_cast<size_t>(id)));
		}
	});

	it("configures every coefficient and compensates the gain as the reference's configure does", _ {
		std::array<std::size_t, 3> modes{};
		for (const FilterConfig& config : sweep_configs()) {
			ReferenceHighPassLadder reference;
			reference.zero();
			const q31 gain = reference.configure(kGain, config);
			const Ladder::Coefficients c = Ladder::coefficients({.gain = kGain, .config = config});
			expect(Ladder::gain(c)).to_equal(gain);
			++modes.at(static_cast<std::size_t>(c.mode));

			// Every coefficient: the adapter's digest folds them all, as the reference's folds its members.
			kernel_filters::KernelHighPassLadder<false> kernel;
			kernel.zero();
			expect(kernel.configure(kGain, config)).to_equal(gain);
			filter_workloads::Digest kernel_digest;
			filter_workloads::Digest reference_digest;
			kernel.digest(kernel_digest);
			reference.digest(reference_digest);
			expect(kernel_digest.value).to_equal(reference_digest.value);
		}
		for (const std::size_t count : modes) {
			expect(count).to_be_greater_than(0u);
		}
	});

	it("switches the saturator at the reference's resonances: either side of each switch, mono and stereo", _ {
		std::vector<q31> scratch(kInput.size());
		const std::vector<q31> changes = mode_changes();
		expect(Ladder::coefficients({.gain = kGain, .config = config_at(changes[0] - 1)}).mode)
		    .to_equal(Ladder::Mode::BYPASS);
		expect(Ladder::coefficients({.gain = kGain, .config = config_at(changes[0])}).mode).to_equal(Ladder::Mode::TANH);
		expect(Ladder::coefficients({.gain = kGain, .config = config_at(changes[1] - 1)}).mode)
		    .to_equal(Ladder::Mode::TANH);
		expect(Ladder::coefficients({.gain = kGain, .config = config_at(changes[1])}).mode)
		    .to_equal(Ladder::Mode::ANTIALIASED);
		for (const q31 frequency : {q31{1 << 20}, q31{1 << 23}, q31{1 << 26}}) {
			for (const q31 change : changes) {
				for (const q31 resonance : {change - 1, change}) {
					FilterConfig config = config_at(resonance);
					config.frequency = frequency;
					for (const bool stereo : {false, true}) {
						ReferenceHighPassLadder reference;
						reference.zero();
						std::vector<q31> expected = kInput;
						const q31 gain = reference.configure(kGain, config);
						std::vector<q31> out = kInput;
						if (stereo) {
							reference.renderStereo(expected, scratch);
							Stereo<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = config}};
							const auto frames = deluge::dsp::as_frames(out);
							ladder.renderBlock(frames, frames);
							expect(words(ladder.left())).to_equal(words(reference.left()));
							expect(words(ladder.right())).to_equal(words(reference.right()));
							expect(Ladder::gain(ladder.coefficients())).to_equal(gain);
						}
						else {
							reference.renderMono(expected, scratch);
							Mono<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = config}};
							ladder.renderBlock(out, out);
							expect(words(ladder.state())).to_equal(words(reference.left()));
							expect(Ladder::gain(ladder.coefficients())).to_equal(gain);
						}
						expect(out).to_equal(expected);
					}
				}
			}
		}
	});

	it("carries each channel's state across every switch between bypass, tanh and the antialiased tanh, the bypass "
	   "placing the saturator where the reference does", _ {
		std::vector<q31> scratch(kInput.size());
		for (const q31 from : kSaturations) {
			for (const q31 to : kSaturations) {
				ReferenceHighPassLadder reference;
				reference.zero();
				Stereo<Ladder> ladder;
				for (const q31 resonance : {from, to, from}) {
					std::vector<q31> expected = kInput;
					std::ignore = reference.configure(kGain, config_at(resonance));
					reference.renderStereo(expected, scratch);

					const Ladder::State left = ladder.left();
					const Ladder::State right = ladder.right();
					ladder.configure({.gain = kGain, .config = config_at(resonance)});
					expect(words(ladder.left())).to_equal(words(left));
					expect(words(ladder.right())).to_equal(words(right));

					std::vector<q31> out = kInput;
					const auto frames = deluge::dsp::as_frames(out);
					ladder.renderBlock(frames, frames);

					expect(out).to_equal(expected);
					expect(words(ladder.left())).to_equal(words(reference.left()));
					expect(words(ladder.right())).to_equal(words(reference.right()));
				}
			}
		}
	});

	it("starts with the saturator at position 0, silences the sections on reset and keeps the saturator's position, as "
	   "the reference does", _ {
		std::vector<q31> scratch(kInput.size());
		ReferenceHighPassLadder reference;
		reference.zero();
		Mono<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = config_at(filter_workloads::detail::kHpSaturated)}};
		expect(words(ladder.state())).to_equal(words(reference.left()));
		expect(words(ladder.state())).to_equal(ChannelWords{0, 0, 0, 0});

		std::ignore = reference.configure(kGain, config_at(filter_workloads::detail::kHpSaturated));
		std::vector<q31> expected = kInput;
		reference.renderMono(expected, scratch);
		reference.reset(false);

		std::vector<q31> out = kInput;
		ladder.renderBlock(out, out);
		ladder.reset();

		expect(ladder.state().saturator.position).not_().to_equal(0u);
		expect(words(ladder.state())).to_equal(words(reference.left()));
		expect(words(ladder.state()))
		    .to_equal(ChannelWords{0, 0, 0, ladder.state().saturator.position});
	});

	it("silences both channels' sections on reset in stereo and keeps each channel's saturator position, as the "
	   "reference does", _ {
		std::vector<q31> scratch(kInput.size());
		ReferenceHighPassLadder reference;
		reference.zero();
		Stereo<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = config_at(filter_workloads::detail::kHpSaturated)}};
		expect(words(ladder.left())).to_equal(words(reference.left()));
		expect(words(ladder.right())).to_equal(words(reference.right()));

		std::ignore = reference.configure(kGain, config_at(filter_workloads::detail::kHpSaturated));
		std::vector<q31> expected = kInput;
		reference.renderStereo(expected, scratch);
		reference.reset(false);

		std::vector<q31> out = kInput;
		const auto frames = deluge::dsp::as_frames(out);
		ladder.renderBlock(frames, frames);
		ladder.reset();

		expect(ladder.left().saturator.position).not_().to_equal(ladder.right().saturator.position);
		expect(words(ladder.left())).to_equal(words(reference.left()));
		expect(words(ladder.right())).to_equal(words(reference.right()));
		expect(words(ladder.left())).to_equal(ChannelWords{0, 0, 0, ladder.left().saturator.position});
		expect(words(ladder.right())).to_equal(ChannelWords{0, 0, 0, ladder.right().saturator.position});
	});
});
// clang-format on

CPPSPEC_MAIN(high_pass_ladder);
