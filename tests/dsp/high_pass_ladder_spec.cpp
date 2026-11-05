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

// HighPassLadder: the transistor-ladder high-pass as a processor kernel, `Mono` and `Stereo`. Against the
// reference (reference_filters.h) over every high-pass workload (filter_workloads.h): each block's output, the gain,
// the noise draws (none) and the whole state, the saturator's position included, at block lengths cycling 1–9, 24, 64
// and 128; and what it renders must equal the record. Every coefficient equals the reference's configure across the
// cutoff and resonance range, the saturator switches at the reference's resonances, and every switch between bypass,
// tanh and the antialiased tanh carries the state.

#include "dsp-next/core/buffer_view.h"
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
#include "util/synth_noise.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using deluge::dsp::Q;
using deluge::dsp::typed_view;
using namespace deluge::dsp::fixed;

namespace {

using filters_reference::FilterConfig; // the references' raw settings; the kernels take typed() of them

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
FilterConfig config_at(int32_t resonance) {
	return {.frequency = 1 << 23, .resonance = resonance, .morph = 1 << 26, .allow_oversampling = true};
}

/// The saturator's three settings: bypassed, tanh and the antialiased tanh, by resonance.
constexpr std::array<int32_t, 3> kSaturations{filter_workloads::detail::kHpBypass, filter_workloads::detail::kHpTanh,
                                              filter_workloads::detail::kHpSaturated};

/// @return The resonances (Q29) at which the kernel's saturator first leaves BYPASS and first leaves TANH, found by
/// bisection. The processed resonance the switch reads does not depend on the cutoff.
std::vector<int32_t> mode_changes() {
	const auto mode = [](int32_t resonance) {
		return Ladder::saturation_of(
		    Ladder::coefficients({.gain = kGain, .config = filters_reference::typed(config_at(resonance))}).mode);
	};
	std::vector<int32_t> changes;
	for (const Ladder::Saturation below : {Ladder::Saturation::BYPASS, Ladder::Saturation::TANH}) {
		int32_t low = 0;
		int32_t high = 536870911;
		while (high - low > 1) {
			const int32_t middle = low + ((high - low) / 2);
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
	for (const int32_t frequency :
	     {int32_t{1 << 20}, int32_t{1 << 22}, int32_t{1 << 23}, int32_t{1 << 24}, int32_t{1 << 26}, int32_t{kOneQ31}}) {
		for (const int32_t resonance :
		     {int32_t{0}, filter_workloads::detail::kHpBypass, filter_workloads::detail::kHpTanh,
		      filter_workloads::detail::kHpSaturated, int32_t{536870911}, int32_t{1 << 30}}) {
			for (const int32_t morph : {int32_t{0}, int32_t{1 << 26}, int32_t{536870911}}) {
				configs.push_back(
				    {.frequency = frequency, .resonance = resonance, .morph = morph, .allow_oversampling = true});
			}
		}
	}
	return configs;
}

/// A block at the rails and between: full-scale square bursts, then xorshift noise across the whole range, so the
/// morph's nudge (x × 2^4) wraps.
std::vector<int32_t> loud_block() {
	std::vector<int32_t> block;
	for (std::size_t i = 0; i < 128; ++i) {
		block.push_back((i / 8) % 2 == 0 ? int32_t{kOneQ31} : int32_t{INT32_MIN});
	}
	uint32_t state = 0x2468'ace1u;
	for (std::size_t i = 0; i < 384; ++i) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		block.push_back(static_cast<int32_t>(state));
	}
	return block;
}

/// Samples where the kernel's step parts from the reference's render over loud_block(), at each saturation with the
/// morph off, mid and full, and how many inputs the step met whose x × 2^4 leaves Q31.
std::pair<std::size_t, std::size_t> loud_partings() {
	std::size_t parted = 0;
	std::size_t wrapped = 0;
	for (const int32_t resonance : kSaturations) {
		for (const int32_t morph : {int32_t{0}, int32_t{1 << 26}, int32_t{536870911}}) {
			const FilterConfig config{.frequency = 1 << 23, .resonance = resonance, .morph = morph};
			ReferenceHighPassLadder reference;
			reference.zero(); // no fade: the step alone
			std::ignore = reference.configure(kGain, config);
			std::vector<int32_t> expected = loud_block();
			std::vector<int32_t> scratch(expected.size());
			reference.render_mono(expected, scratch);

			const Ladder::Coefficients c =
			    Ladder::coefficients({.gain = kGain, .config = filters_reference::typed(config)});
			Ladder::State s{};
			const std::vector<int32_t> in = loud_block();
			for (std::size_t i = 0; i < in.size(); ++i) {
				const int32_t out = deluge::dsp::with_mode<Ladder::Modes>(
				    c.mode, [&]<Ladder::Mode M>() { return Ladder::step<M>(c, {}, s, Q<31>::from_raw(in[i])).raw(); });
				parted += out != expected[i] ? 1 : 0;
				wrapped += morph != 0 && (in[i] > (kOneQ31 >> 4) || in[i] < (INT32_MIN >> 4)) ? 1 : 0;
			}
		}
	}
	return {parted, wrapped};
}

/// A channel's three sections' memories (Q31) and the saturator's position (unsigned over 32 bits).
using ChannelWords = std::array<int64_t, 4>;

ChannelWords words(const Ladder::State& s) {
	return {s.high_pass_1.z.raw(), s.low_pass_1.z.raw(), s.high_pass_3.z.raw(), s.saturator.position};
}

ChannelWords words(const ReferenceHighPassLadder::Channel& c) {
	return {c.high_pass_1.memory(), c.low_pass_1.memory(), c.high_pass_3.memory(), c.last_working_value};
}

} // namespace

// clang-format off
describe high_pass_ladder("HighPassLadder", ${
	it("renders every high-pass workload as the reference does, mono and stereo: each block's output, gain and noise "
	   "draws, and the whole state", _ {
		expect(partings(kHighPassWorkloads)).to_equal(std::vector<std::string>{});
	});

	it("renders every high-pass workload as recorded", _ {
		for (const WorkloadId id : kHighPassWorkloads) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			expect(kernel_filters::record(workload)).to_equal(filter_workloads::kRecorded.at(static_cast<size_t>(id)));
		}
	});

	it("steps as the reference renders at full-scale input in every saturation, the morph's nudge wrapping", _ {
		const auto [parted, wrapped] = loud_partings();
		expect(parted).to_equal(std::size_t{0});
		expect(wrapped).to_be_greater_than(std::size_t{0});
	});

	it("configures every coefficient and compensates the gain as the reference's configure does", _ {
		std::array<std::size_t, 6> modes{};
		for (const auto& config : sweep_configs()) {
			ReferenceHighPassLadder reference;
			reference.zero();
			const int32_t gain = reference.configure(kGain, config);
			const Ladder::Coefficients c = Ladder::coefficients({.gain = kGain, .config = filters_reference::typed(config)});
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

	it("switches the saturator at the reference's resonances, the morph off and on: either side of each switch, mono "
	   "and stereo", _ {
		std::vector<int32_t> scratch(kInput.size());
		const std::vector<int32_t> changes = mode_changes();
		const auto saturation_at = [](int32_t resonance) {
			return Ladder::saturation_of(Ladder::coefficients({.gain = kGain, .config = filters_reference::typed(config_at(resonance))}).mode);
		};
		expect(saturation_at(changes[0] - 1)).to_equal(Ladder::Saturation::BYPASS);
		expect(saturation_at(changes[0])).to_equal(Ladder::Saturation::TANH);
		expect(saturation_at(changes[1] - 1)).to_equal(Ladder::Saturation::TANH);
		expect(saturation_at(changes[1])).to_equal(Ladder::Saturation::ANTIALIASED);
		for (const int32_t frequency : {int32_t{1 << 20}, int32_t{1 << 23}, int32_t{1 << 26}}) {
			for (const int32_t change : changes) {
				// The morph off and on: the same switches with the cutoff FIXED and MORPHING.
				for (const auto [resonance, morph] : {std::pair{change - 1, int32_t{0}}, std::pair{change, int32_t{0}},
				                                      std::pair{change - 1, int32_t{1 << 26}}, std::pair{change, int32_t{1 << 26}}}) {
					auto config = config_at(resonance);
					config.frequency = frequency;
					config.morph = morph;
					expect(Ladder::cutoff_of(Ladder::coefficients({.gain = kGain, .config = filters_reference::typed(config)}).mode))
					    .to_equal(morph == 0 ? Ladder::Cutoff::FIXED : Ladder::Cutoff::MORPHING);
					for (const bool stereo : {false, true}) {
						ReferenceHighPassLadder reference;
						reference.zero();
						std::vector<int32_t> expected = kInput;
						const int32_t gain = reference.configure(kGain, config);
						std::vector<int32_t> out = kInput;
						if (stereo) {
							reference.render_stereo(expected, scratch);
							Stereo<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = filters_reference::typed(config)}};
							const auto frames = deluge::dsp::as_frames(deluge::dsp::typed_view(std::span{out}));
							ladder.render_block(frames, frames);
							expect(words(ladder.left())).to_equal(words(reference.left()));
							expect(words(ladder.right())).to_equal(words(reference.right()));
							expect(Ladder::gain(ladder.coefficients())).to_equal(gain);
						}
						else {
							reference.render_mono(expected, scratch);
							Mono<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = filters_reference::typed(config)}};
							ladder.render_block(typed_view(std::span{out}), typed_view(std::span{out}));
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
		std::vector<int32_t> scratch(kInput.size());
		for (const int32_t from : kSaturations) {
			for (const int32_t to : kSaturations) {
				ReferenceHighPassLadder reference;
				reference.zero();
				Stereo<Ladder> ladder;
				for (const int32_t resonance : {from, to, from}) {
					std::vector<int32_t> expected = kInput;
					std::ignore = reference.configure(kGain, config_at(resonance));
					reference.render_stereo(expected, scratch);

					const Ladder::State left = ladder.left();
					const Ladder::State right = ladder.right();
					ladder.configure({.gain = kGain, .config = filters_reference::typed(config_at(resonance))});
					expect(words(ladder.left())).to_equal(words(left));
					expect(words(ladder.right())).to_equal(words(right));

					std::vector<int32_t> out = kInput;
					const auto frames = deluge::dsp::as_frames(deluge::dsp::typed_view(std::span{out}));
					ladder.render_block(frames, frames);

					expect(out).to_equal(expected);
					expect(words(ladder.left())).to_equal(words(reference.left()));
					expect(words(ladder.right())).to_equal(words(reference.right()));
				}
			}
		}
	});

	it("starts with the saturator at position 0, silences the sections on reset and keeps the saturator's position, as "
	   "the reference does", _ {
		std::vector<int32_t> scratch(kInput.size());
		ReferenceHighPassLadder reference;
		reference.zero();
		Mono<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = filters_reference::typed(config_at(filter_workloads::detail::kHpSaturated))}};
		expect(words(ladder.state())).to_equal(words(reference.left()));
		expect(words(ladder.state())).to_equal(ChannelWords{0, 0, 0, 0});

		std::ignore = reference.configure(kGain, config_at(filter_workloads::detail::kHpSaturated));
		std::vector<int32_t> expected = kInput;
		reference.render_mono(expected, scratch);
		reference.reset(false);

		std::vector<int32_t> out = kInput;
		ladder.render_block(typed_view(std::span{out}), typed_view(std::span{out}));
		ladder.reset();

		expect(ladder.state().saturator.position).not_().to_equal(0u);
		expect(words(ladder.state())).to_equal(words(reference.left()));
		expect(words(ladder.state()))
		    .to_equal(ChannelWords{0, 0, 0, ladder.state().saturator.position});
	});

	it("silences both channels' sections on reset in stereo and keeps each channel's saturator position, as the "
	   "reference does", _ {
		std::vector<int32_t> scratch(kInput.size());
		ReferenceHighPassLadder reference;
		reference.zero();
		Stereo<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = filters_reference::typed(config_at(filter_workloads::detail::kHpSaturated))}};
		expect(words(ladder.left())).to_equal(words(reference.left()));
		expect(words(ladder.right())).to_equal(words(reference.right()));

		std::ignore = reference.configure(kGain, config_at(filter_workloads::detail::kHpSaturated));
		std::vector<int32_t> expected = kInput;
		reference.render_stereo(expected, scratch);
		reference.reset(false);

		std::vector<int32_t> out = kInput;
		const auto frames = deluge::dsp::as_frames(deluge::dsp::typed_view(std::span{out}));
		ladder.render_block(frames, frames);
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
