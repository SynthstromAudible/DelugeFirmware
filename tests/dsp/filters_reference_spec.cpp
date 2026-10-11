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

// The filters' and the compressor's verbatim references (reference_filters.h) against the firmware's classes, over
// every recorded workload (filter_workloads.h): each block's output, the gain each target returns, the generator's
// draws and the whole state must agree. A firmware object's state is read through its copy's layout. What both render
// is pinned (filter_workload_records.h): each block's output and draws, the values drawn, and the state at the end.

#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"
#include "dsp-next/fixed/filter/dry_wet_fade.h"
#include "dsp-next/fixed/filter/filter_set.h"
#include "dsp-next/fixed/filter/high_pass_ladder.h"
#include "dsp-next/fixed/filter/low_pass_ladder.h"
#include "dsp-next/fixed/filter/state_variable_filter.h"
#include "filter_workload_records.h"
#include "filter_workloads.h"
#include "reference_filters.h"

#include <algorithm>
#include <bit>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fixed = deluge::dsp::fixed;
namespace ref = filters_reference;
using deluge::dsp::q31;
using filter_workloads::Digest;

/// @return @p object's bytes read as its reference copy @p R, whose members it shares in the same order.
template <typename R, typename T>
[[nodiscard]] R as_reference(const T& object) {
	static_assert(sizeof(R) == sizeof(T));
	static_assert(std::is_trivially_copyable_v<T>);
	return std::bit_cast<R>(object);
}

/// @brief The firmware's filters and compressor as a workload family: each state digested through its copy.
struct FirmwareFamily {
	using LowPassLadder = fixed::LowPassLadder;
	using HighPassLadder = fixed::HighPassLadder;
	using StateVariableFilter = fixed::StateVariableFilter;
	using DryWetFade = fixed::DryWetFade;
	using FilterSet = fixed::FilterSet;
	using Compressor = fixed::RmsFeedbackCompressor;

	[[nodiscard]] static fixed::FilterScratch filter_scratch(std::span<q31> fade, std::span<q31> parallel) {
		return {.fade = fade, .parallel = parallel};
	}

	static void digest(const fixed::LowPassLadder& x, Digest& d) {
		as_reference<ref::ReferenceLowPassLadder>(x).digest(d);
	}
	static void digest(const fixed::HighPassLadder& x, Digest& d) {
		as_reference<ref::ReferenceHighPassLadder>(x).digest(d);
	}
	static void digest(const fixed::StateVariableFilter& x, Digest& d) {
		as_reference<ref::ReferenceStateVariableFilter>(x).digest(d);
	}
	static void digest(const fixed::DryWetFade& x, Digest& d) { as_reference<ref::ReferenceDryWetFade>(x).digest(d); }
	static void digest(const fixed::FilterSet& x, Digest& d) { as_reference<ref::ReferenceFilterSet>(x).digest(d); }
	static void digest(const fixed::RmsFeedbackCompressor& x, Digest& d) {
		as_reference<ref::ReferenceRmsFeedbackCompressor>(x).digest(d);
	}
};

using filter_workloads::ReferenceFamily;

/// @return Each workload in @p workloads where @p A and @p B part, named with the block and what parted.
template <typename A, typename B, typename Workloads>
[[nodiscard]] std::vector<std::string> partings(const Workloads& workloads) {
	std::vector<std::string> parted;
	for (const auto& workload : workloads) {
		const auto parting = filter_workloads::first_parting<A, B>(workload);
		if (parting) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
			                 + parting->what);
		}
	}
	return parted;
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
		const auto& compressors = filter_workloads::all_compressor_workloads();
		expect(compressors.size()).to_equal(filter_workloads::kCompressorWorkloadCount);
		for (size_t i = 0; i < compressors.size(); ++i) {
			expect(static_cast<size_t>(compressors[i].id)).to_equal(i);
		}
	});

	it("render every filter workload as the legacy filters do: each block's output, gain and draws, and the whole "
	   "state", _ {
		expect(partings<FirmwareFamily, ReferenceFamily>(filter_workloads::all_workloads()))
		    .to_equal(std::vector<std::string>{});
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

	it("change a slot's filter while its fade runs: before blocks 4 and 7 of every takeover, and while both slots fade "
	   "in", _ {
		using filter_workloads::WorkloadId;
		const auto fading_before = [](WorkloadId id, bool low, std::initializer_list<size_t> blocks) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			filter_workloads::FilterRun<ReferenceFamily> run{workload};
			std::vector<q31> out;
			Digest noise;
			std::vector<bool> active;
			for (size_t b = 0; b <= std::max(blocks); ++b) {
				if (std::ranges::find(blocks, b) != blocks.end()) {
					const ref::ReferenceFilterSet& set = run.filterSet();
					active.push_back(low ? set.lowFade().active() : set.highFade().active());
				}
				std::ignore = run.block(b, out, noise);
			}
			return active;
		};
		for (const bool stereo : {false, true}) {
			const auto id = [stereo](WorkloadId mono) {
				return static_cast<WorkloadId>(static_cast<uint8_t>(mono) + static_cast<uint8_t>(stereo));
			};
			for (const WorkloadId mono :
			     {WorkloadId::SET_LP_TAKEOVER_SVF_MODE_MONO, WorkloadId::SET_LP_TAKEOVER_BACK_MONO}) {
				expect(fading_before(id(mono), true, {4, 7})).to_equal(std::vector<bool>{true, true});
			}
			for (const WorkloadId mono :
			     {WorkloadId::SET_HP_TAKEOVER_SVF_MODE_MONO, WorkloadId::SET_HP_TAKEOVER_BACK_MONO}) {
				expect(fading_before(id(mono), false, {4, 7})).to_equal(std::vector<bool>{true, true});
			}
			for (const WorkloadId mono :
			     {WorkloadId::SET_FADE_LP_SVF_HP_LADDER_MONO, WorkloadId::SET_FADE_LP_LADDER_HP_SVF_MONO}) {
				expect(fading_before(id(mono), true, {3, 7})).to_equal(std::vector<bool>{true, true});
				expect(fading_before(id(mono), false, {3, 7})).to_equal(std::vector<bool>{true, true});
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

	it("render every compressor workload as the legacy compressor does: each block's output and gain reduction, and "
	   "the whole state", _ {
		expect(partings<FirmwareFamily, ReferenceFamily>(filter_workloads::all_compressor_workloads()))
		    .to_equal(std::vector<std::string>{});
	});

	it("render every filter workload as recorded: each block's output and draws, the values drawn, and the state", _ {
		for (const filter_workloads::FilterWorkload& workload : filter_workloads::all_workloads()) {
			const filter_workloads::WorkloadRecord& recorded =
			    filter_workloads::kRecorded.at(static_cast<size_t>(workload.id));
			expect(recorded.outputs.size()).to_equal(workload.blocks);
			expect(filter_workloads::record<ReferenceFamily>(workload)).to_equal(recorded);
			expect(filter_workloads::record<FirmwareFamily>(workload)).to_equal(recorded);
		}
	});

	it("render every compressor workload as recorded: each block's output and gain reduction, and the state", _ {
		for (const filter_workloads::CompressorWorkload& workload : filter_workloads::all_compressor_workloads()) {
			const filter_workloads::WorkloadRecord& recorded =
			    filter_workloads::kCompressorRecorded.at(static_cast<size_t>(workload.id));
			expect(recorded.outputs.size()).to_equal(workload.blocks);
			expect(filter_workloads::record<ReferenceFamily>(workload)).to_equal(recorded);
			expect(filter_workloads::record<FirmwareFamily>(workload)).to_equal(recorded);
		}
	});
});
// clang-format on

CPPSPEC_MAIN(verbatim_filters);
