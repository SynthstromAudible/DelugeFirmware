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

// The filters' verbatim references (reference_filters.h) against the firmware, over every recorded workload
// (filter_workloads.h). A lone filter or the fade runs as its kernel (kernel_filters.h), the set as the firmware's
// FilterSet; each state digest folds what its reference's does. Each block's output, gain, generator draws and whole
// state must agree, and the set must agree too with mono and stereo blocks alternating on one set. What the references
// render is pinned (filter_workload_records.h): each block's output and draws, the values drawn, and the state at the
// end; the firmware's renders must equal the same pins.

#include "dsp-next/fixed/filter/fade_in.h"
#include "dsp-next/fixed/filter/filter_set.h"
#include "filter_workload_records.h"
#include "filter_workloads.h"
#include "kernel_filters.h"
#include "reference_filters.h"
#include "util/synth_noise.h"

#include <algorithm>
#include <bit>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace fixed = deluge::dsp::fixed;
namespace ref = filters_reference;
using deluge::dsp::q31;
using deluge::dsp::StereoSample;
using filter_workloads::Digest;

/// @brief The firmware's FilterSet behind the reference set's interface, its state digested as
/// @ref filters_reference::ReferenceFilterSet::digest digests the reference's.
/// @details The set's state is read through `FilterSet::view()`. The one thing the reference's digest folds that the
/// firmware does not keep is the held low-pass ladder's mode and the two values its legacy configure left stale
/// (@ref kernel_filters::LadderHeld); this adapter holds them as the reference does, starting over whenever the
/// slot takes a ladder over (a fresh, zeroed ladder in the reference).
class FirmwareSet {
public:
	[[nodiscard]] q31 configure(q31 gain, const fixed::FilterConfig& low, fixed::LowPassSlot low_slot,
	                            const fixed::FilterConfig& high, fixed::HighPassSlot high_slot,
	                            fixed::FilterRoute route) {
		const q31 out = set_.configure(gain, low, low_slot, high, high_slot, route);
		const fixed::FilterSet::View v = set_.view();
		if (v.low_held != low_held_) {
			ladder_ = {};
			low_held_ = v.low_held;
		}
		if (low_slot.kind == fixed::LowPassSlot::Kind::LADDER) {
			ladder_.take(low_slot.ladder, v.low_ladder->coefficients());
		}
		return out;
	}

	void renderMono(std::span<q31> block, fixed::FilterScratch scratch) { set_.renderMono(block, scratch); }
	void renderStereo(std::span<q31> interleaved, fixed::FilterScratch scratch) {
		set_.renderStereo(interleaved, scratch);
	}

	void reset() {
		set_.reset();
		ladder_ = {};
		low_held_ = fixed::LowPassSlot::Kind::LADDER;
	}

	/// @return The set between blocks.
	[[nodiscard]] fixed::FilterSet::View view() const { return set_.view(); }

	/// @brief Folds the slots' bookkeeping and the held filters' state into @p d, in the reference's order.
	void digest(Digest& d) const {
		const fixed::FilterSet::View v = set_.view();
		d.add(v.low_held);
		if (v.low_svf != nullptr) {
			kernel_filters::digest_svf(v.low_svf->left(), v.low_svf->right(), v.low_svf->coefficients(), v.low_fade, d);
		}
		else {
			kernel_filters::digest_low_pass_ladder(ladder_, v.low_ladder->left(), v.low_ladder->right(),
			                                       v.low_ladder->coefficients(), v.low_fade, d);
		}
		d.add(v.high_held);
		if (v.high_svf != nullptr) {
			kernel_filters::digest_svf(v.high_svf->left(), v.high_svf->right(), v.high_svf->coefficients(), v.high_fade,
			                           d);
		}
		else {
			kernel_filters::digest_high_pass_ladder(v.high_ladder->left(), v.high_ladder->right(),
			                                        v.high_ladder->coefficients(), v.high_fade, d);
		}
		d.add(v.low_slot.kind);
		d.add(v.low_slot.ladder);
		d.add(v.low_slot.svf);
		d.add(v.high_slot.kind);
		d.add(v.high_slot.svf);
		d.add(v.low_last);
		d.add(v.high_last.kind);
		d.add(v.high_last.svf);
		d.add(v.route);
	}

private:
	fixed::FilterSet set_;
	kernel_filters::LadderHeld ladder_{};
	fixed::LowPassSlot::Kind low_held_ = fixed::LowPassSlot::Kind::LADDER;
};

/// @brief The firmware as a workload family, in one form: the lone filters and the fade as their kernels
/// (kernel_filters.h), the set as the firmware's (@ref FirmwareSet). Every digest folds what its reference's does, so
/// the whole state is compared and pinned.
template <bool kStereo>
struct FirmwareFamily : kernel_filters::KernelFamily<kStereo> {
	using FilterSet = FirmwareSet;

	[[nodiscard]] static fixed::FilterScratch filter_scratch(std::span<q31> fade, std::span<q31> parallel) {
		return {.fade = fade, .parallel = parallel};
	}

	using kernel_filters::KernelFamily<kStereo>::digest;
};

using filter_workloads::ReferenceFamily;

/// @return @p parting named with @p workload's name and the block.
[[nodiscard]] std::string named(std::string_view workload, const filter_workloads::Parting& parting) {
	return std::string{workload} + " block " + std::to_string(parting.block) + ": " + parting.what;
}

/// @return Whether two fades' levels are the same, bit for bit.
[[nodiscard]] bool same_fade(const ref::ReferenceDryWetFade& reference, const fixed::FadeIn::State& firmware) {
	return std::bit_cast<uint32_t>(reference.dry()) == std::bit_cast<uint32_t>(firmware.dry)
	       && reference.wet() == firmware.wet;
}

/// @return Each filter workload where the firmware parts from the reference, named with the block and what parted: a
/// lone filter or the fade as its kernel, the set as the firmware's, each with its whole state.
[[nodiscard]] std::vector<std::string> filter_partings() {
	std::vector<std::string> parted;
	for (const filter_workloads::FilterWorkload& workload : filter_workloads::all_workloads()) {
		std::optional<filter_workloads::Parting> parting;
		if (workload.target == filter_workloads::Target::FILTER_SET) {
			parting = workload.stereo
			              ? filter_workloads::first_parting<ReferenceFamily, FirmwareFamily<true>>(workload)
			              : filter_workloads::first_parting<ReferenceFamily, FirmwareFamily<false>>(workload);
		}
		else {
			parting = kernel_filters::first_parting(workload);
		}
		if (parting) {
			parted.push_back(named(workload.name, *parting));
		}
	}
	return parted;
}

/// @brief One block of a set workload's step @p s through @p set, mono or stereo, with the generator at @p cong
/// (advanced by the block's draws) and the caller's put back after.
/// @return The gain the set returns, Q31.
template <typename Scratch, typename Set>
q31 play_block(Set& set, uint32_t& cong, const filter_workloads::FilterStep& s, bool mono, std::vector<q31>& block,
               std::span<q31> fade, std::span<q31> parallel) {
	const uint32_t outer = deluge::synth_noise.state();
	deluge::synth_noise.seed(cong);
	if (s.event == filter_workloads::Event::RESET) {
		set.reset();
	}
	const q31 gain = set.configure(s.gain, s.low, s.low_slot, s.high, s.high_slot, s.route);
	const Scratch scratch{.fade = fade.first(block.size()), .parallel = parallel};
	if (mono) {
		set.renderMono(block, scratch);
	}
	else {
		set.renderStereo(block, scratch);
	}
	cong = deluge::synth_noise.state();
	deluge::synth_noise.seed(outer);
	return gain;
}

/// @return Where the firmware's set parts from the reference when @p workload (a stereo set workload) plays with its
/// even blocks mono, over the first half of the block's input, and its odd blocks stereo, on one set each: each
/// block's output, gain and generator state, both slots' fade levels, and the whole state.
[[nodiscard]] std::optional<filter_workloads::Parting>
alternating_parting(const filter_workloads::FilterWorkload& workload) {
	ref::ReferenceFilterSet reference;
	FirmwareSet firmware;
	uint32_t reference_cong = filter_workloads::kCongSeed;
	uint32_t firmware_cong = filter_workloads::kCongSeed;
	std::vector<q31> fade(2 * filter_workloads::kMaxFrames);
	std::vector<q31> parallel(2 * filter_workloads::kMaxFrames);
	for (size_t b = 0; b < workload.blocks; ++b) {
		const filter_workloads::FilterStep s = workload.step(b);
		const bool mono = b % 2 == 0;
		std::vector<q31> in = filter_workloads::filter_input(workload, b);
		if (mono) {
			in.resize(in.size() / 2);
		}
		std::vector<q31> out_reference = in;
		std::vector<q31> out_firmware = in;
		const q31 gain_reference =
		    play_block<ref::FilterScratch>(reference, reference_cong, s, mono, out_reference, fade, parallel);
		const q31 gain_firmware =
		    play_block<fixed::FilterScratch>(firmware, firmware_cong, s, mono, out_firmware, fade, parallel);
		if (out_reference != out_firmware) {
			return filter_workloads::Parting{.block = b, .what = "output"};
		}
		if (gain_reference != gain_firmware) {
			return filter_workloads::Parting{.block = b, .what = "gain"};
		}
		if (reference_cong != firmware_cong) {
			return filter_workloads::Parting{.block = b, .what = "noise"};
		}
		const fixed::FilterSet::View view = firmware.view();
		if (!same_fade(reference.lowFade(), view.low_fade) || !same_fade(reference.highFade(), view.high_fade)) {
			return filter_workloads::Parting{.block = b, .what = "fade"};
		}
		Digest reference_state;
		Digest firmware_state;
		reference.digest(reference_state);
		firmware.digest(firmware_state);
		if (reference_state.value != firmware_state.value) {
			return filter_workloads::Parting{.block = b, .what = "state"};
		}
	}
	return std::nullopt;
}

/// @return How many samples of @p workload (a parallel set) sum past q31 when its low-pass and high-pass run alone
/// and are added in 64 bits, and how many of its outputs differ from the two added wrapping.
[[nodiscard]] std::pair<size_t, size_t> parallel_wraps(const filter_workloads::FilterWorkload& workload) {
	using filter_workloads::FilterStep;
	filter_workloads::FilterWorkload low = workload;
	filter_workloads::FilterWorkload high = workload;
	low.step = [&](size_t b) {
		FilterStep s = workload.step(b);
		s.high_slot = {.kind = fixed::HighPassSlot::Kind::OFF};
		s.route = fixed::FilterRoute::HIGH_TO_LOW;
		return s;
	};
	high.step = [&](size_t b) {
		FilterStep s = workload.step(b);
		s.low_slot = {.kind = fixed::LowPassSlot::Kind::OFF};
		s.route = fixed::FilterRoute::HIGH_TO_LOW;
		return s;
	};
	filter_workloads::FilterRun<ReferenceFamily> both{workload};
	filter_workloads::FilterRun<ReferenceFamily> low_alone{low};
	filter_workloads::FilterRun<ReferenceFamily> high_alone{high};
	std::vector<q31> out;
	std::vector<q31> out_low;
	std::vector<q31> out_high;
	Digest noise;
	size_t wraps = 0;
	size_t mismatched = 0;
	for (size_t b = 0; b < workload.blocks; ++b) {
		std::ignore = both.block(b, out, noise);
		std::ignore = low_alone.block(b, out_low, noise);
		std::ignore = high_alone.block(b, out_high, noise);
		for (size_t i = 0; i < out.size(); ++i) {
			const int64_t sum = int64_t{out_low[i]} + out_high[i];
			wraps += static_cast<size_t>(sum > INT32_MAX || sum < INT32_MIN);
			mismatched += static_cast<size_t>(deluge::dsp::wrapping_add(out_low[i], out_high[i]) != out[i]);
		}
	}
	return {wraps, mismatched};
}

/// @return Each block's draws in @p workload as the per-sample count @p per_block(b) gives them: one entry per block,
/// the draws over the block's samples.
template <typename PerSample>
[[nodiscard]] std::vector<uint32_t> expected_draws(const filter_workloads::FilterWorkload& workload,
                                                   PerSample per_sample) {
	std::vector<uint32_t> draws;
	for (size_t b = 0; b < workload.blocks; ++b) {
		draws.push_back(static_cast<uint32_t>(workload.samples(b)) * per_sample(b));
	}
	return draws;
}

} // namespace

// clang-format off
describe verbatim_filters("the filters' verbatim references", ${
	it("hold every workload once, in id order", _ {
		const auto& filters = filter_workloads::all_workloads();
		expect(filters.size()).to_equal(filter_workloads::kWorkloadCount);
		for (size_t i = 0; i < filters.size(); ++i) {
			expect(static_cast<size_t>(filters[i].id)).to_equal(i);
		}
	});

	it("render every filter workload as the legacy filters do: each block's output, gain and draws, and the whole "
	   "state", _ {
		expect(filter_partings()).to_equal(std::vector<std::string>{});
	});

	it("render every stereo set workload as the legacy set does with mono and stereo blocks alternating", _ {
		std::vector<std::string> parted;
		size_t played = 0;
		for (const filter_workloads::FilterWorkload& workload : filter_workloads::all_workloads()) {
			if (workload.target != filter_workloads::Target::FILTER_SET || !workload.stereo) {
				continue;
			}
			++played;
			const auto parting = alternating_parting(workload);
			if (parting) {
				parted.push_back(named(workload.name, *parting));
			}
		}
		expect(parted).to_equal(std::vector<std::string>{});
		expect(played).to_equal(size_t{16});
	});

	it("draw one value per channel-sample in the ladders, two when the drive ladder oversamples, none elsewhere", _ {
		using filter_workloads::WorkloadId;
		using filter_workloads::filter_workload;
		const auto draws = [](WorkloadId id) {
			return filter_workloads::record<ReferenceFamily>(filter_workload(id)).draws;
		};
		const auto per_sample = [](uint32_t n) { return [n](size_t) { return n; }; };
		for (const bool stereo : {false, true}) {
			const auto id = [stereo](WorkloadId mono) {
				return static_cast<WorkloadId>(static_cast<uint8_t>(mono) + static_cast<uint8_t>(stereo));
			};
			for (const WorkloadId mono : {WorkloadId::LP12_MONO, WorkloadId::LP24_MONO, WorkloadId::LPDRIVE_MONO,
			                              WorkloadId::LPDRIVE_OS_OFF_MONO}) {
				expect(draws(id(mono))).to_equal(expected_draws(filter_workload(id(mono)), per_sample(1)));
			}
			expect(draws(id(WorkloadId::LPDRIVE_OS_MONO)))
			    .to_equal(expected_draws(filter_workload(id(WorkloadId::LPDRIVE_OS_MONO)), per_sample(2)));
			expect(draws(id(WorkloadId::LP_OS_TOGGLE_MONO)))
			    .to_equal(expected_draws(filter_workload(id(WorkloadId::LP_OS_TOGGLE_MONO)),
			                             [](size_t b) { return b % 2 == 0 ? 2U : 1U; }));
			for (const WorkloadId mono : {WorkloadId::HP_RESONANCE_SWEEP_MONO, WorkloadId::SVF_MODE_SWITCH_MONO,
			                              WorkloadId::FADE_MONO}) {
				expect(draws(id(mono))).to_equal(expected_draws(filter_workload(id(mono)), per_sample(0)));
			}
		}
	});

	it("change a slot's filter while its fade runs, in the legacy set and the firmware's: before blocks 4 and 7 of "
	   "every takeover, and while both slots fade in", _ {
		using filter_workloads::WorkloadId;
		const auto fading_before = [](WorkloadId id, bool low, std::initializer_list<size_t> blocks) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			filter_workloads::FilterRun<ReferenceFamily> reference{workload};
			filter_workloads::FilterRun<FirmwareFamily<false>> mono{workload};
			filter_workloads::FilterRun<FirmwareFamily<true>> stereo{workload};
			std::vector<q31> out;
			Digest noise;
			std::vector<bool> active;
			for (size_t b = 0; b <= std::max(blocks); ++b) {
				if (std::ranges::find(blocks, b) != blocks.end()) {
					const ref::ReferenceFilterSet& set = reference.filterSet();
					active.push_back(low ? set.lowFade().active() : set.highFade().active());
					const fixed::FilterSet::View firmware =
					    workload.stereo ? stereo.filterSet().view() : mono.filterSet().view();
					active.push_back(fixed::FadeIn::active(low ? firmware.low_fade : firmware.high_fade));
				}
				std::ignore = reference.block(b, out, noise);
				if (workload.stereo) {
					std::ignore = stereo.block(b, out, noise);
				}
				else {
					std::ignore = mono.block(b, out, noise);
				}
			}
			return active;
		};
		const std::vector<bool> all_four(4, true);
		for (const bool stereo : {false, true}) {
			const auto id = [stereo](WorkloadId mono) {
				return static_cast<WorkloadId>(static_cast<uint8_t>(mono) + static_cast<uint8_t>(stereo));
			};
			for (const WorkloadId mono :
			     {WorkloadId::SET_LP_TAKEOVER_SVF_MODE_MONO, WorkloadId::SET_LP_TAKEOVER_BACK_MONO}) {
				expect(fading_before(id(mono), true, {4, 7})).to_equal(all_four);
			}
			for (const WorkloadId mono :
			     {WorkloadId::SET_HP_TAKEOVER_SVF_MODE_MONO, WorkloadId::SET_HP_TAKEOVER_BACK_MONO}) {
				expect(fading_before(id(mono), false, {4, 7})).to_equal(all_four);
			}
			for (const WorkloadId mono :
			     {WorkloadId::SET_FADE_LP_SVF_HP_LADDER_MONO, WorkloadId::SET_FADE_LP_LADDER_HP_SVF_MONO}) {
				expect(fading_before(id(mono), true, {3, 7})).to_equal(all_four);
				expect(fading_before(id(mono), false, {3, 7})).to_equal(all_four);
			}
		}
	});

	it("wrap the parallel route's sum in the full-scale workloads", _ {
		using filter_workloads::WorkloadId;
		for (const WorkloadId id : {WorkloadId::SET_PAR_FULL_SCALE_MONO, WorkloadId::SET_PAR_FULL_SCALE_STEREO}) {
			const auto [wraps, mismatched] = parallel_wraps(filter_workloads::filter_workload(id));
			expect(wraps).to_be_greater_than(size_t{0});
			expect(mismatched).to_equal(size_t{0});
		}
	});

	it("render every filter workload as recorded: each block's output and draws, the values drawn, and the state", _ {
		for (const filter_workloads::FilterWorkload& workload : filter_workloads::all_workloads()) {
			const filter_workloads::WorkloadRecord& recorded =
			    filter_workloads::kRecorded.at(static_cast<size_t>(workload.id));
			expect(recorded.outputs.size()).to_equal(workload.blocks);
			expect(filter_workloads::record<ReferenceFamily>(workload)).to_equal(recorded);
			if (workload.target == filter_workloads::Target::FILTER_SET) {
				expect(workload.stereo ? filter_workloads::record<FirmwareFamily<true>>(workload)
				                       : filter_workloads::record<FirmwareFamily<false>>(workload))
				    .to_equal(recorded);
			}
			else {
				expect(kernel_filters::record(workload)).to_equal(recorded);
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(verbatim_filters);
