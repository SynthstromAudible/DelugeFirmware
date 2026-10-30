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

// The Compressor kernel: the compressor as one frame kernel under Mono, rendered in place. Driven as its owner drives
// it (TestCompressor below: configure on a knob change, the block's levels by event, the seed by event when unseeded,
// one in-place block, then the level by event), it must equal the fixed reference (reference_compressor.h) bit for bit
// on every recorded workload (compressor_workloads.h): each block's output and gain reduction, and the whole `view()`
// after every rendered block and at the end. That covers WET and BLEND switching between blocks (BLEND_SWITCH), resets
// mid-sound (the ONSET_PARTIAL_WET workloads), block lengths 1 to 9 and 128 (BLOCK_LENGTHS), and both level defects
// fixed (WRAPPED_SUM, INT32_MIN_DETECTOR).

#include "compressor_workload_records.h"
#include "compressor_workloads.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/dynamics/compressor_kernel.h"
#include "reference_compressor.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using deluge::dsp::Mono;
using deluge::dsp::q31;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::Compressor;
using deluge::dsp::fixed::CompressorKnobs;

static_assert(deluge::dsp::detail::plain_value<Compressor::Coefficients>);
static_assert(deluge::dsp::detail::plain_value<Compressor::State>);
static_assert(std::same_as<deluge::dsp::loop_state_t<Compressor>, Compressor::Loop>);
static_assert(deluge::dsp::detail::plain_value<Compressor::Block>);
static_assert(deluge::dsp::processor_kernel<Compressor>);
static_assert(deluge::dsp::moded_kernel<Compressor>);

namespace deluge::dsp::fixed {

// The kernel's mode, printed by name when an expectation fails.
inline std::ostream& operator<<(std::ostream& out, Compressor::Mode mode) {
	return out << (mode == Compressor::Mode::WET ? "WET" : "BLEND");
}

} // namespace deluge::dsp::fixed

namespace {

namespace cw = compressor_workloads;
using compressor_reference::FixedReferenceRmsFeedbackCompressor;
using compressor_reference::View;

/// @brief `Mono<Compressor>` as its owner drives it, with the compressor's calls, so a workload run plays it as it
/// plays the references.
/// @tparam kInPlace Whether a block renders in place, as the owner renders it; else from a copy of the block.
template <bool kInPlace>
class TestCompressor {
public:
	TestCompressor() : mono_{settings()} {}

	/// @brief Takes @p knobs, reconfiguring only when they changed; the threshold knob only when it changed.
	void configure(const CompressorKnobs& knobs) {
		if (knobs == configured_) {
			return;
		}
		if (knobs.threshold != configured_.threshold) {
			threshold_knob_ = knobs.threshold;
		}
		configured_ = knobs;
		mono_.configure(settings());
	}

	/// @brief The threshold the next block renders with, Q31 of the knob's travel.
	void set_threshold(q31 threshold) { threshold_knob_ = threshold; }

	/// @brief Makeup gain in nepers at no reduction.
	void set_base_gain(float base_gain) {
		base_gain_ = base_gain;
		mono_.configure(settings());
	}

	/// @brief The kernel's reset quirk.
	void reset() { mono_.reset(); }

	/// @brief The block's levels, the seed when unseeded, the block, then its level.
	/// @param block Rendered in place (or from a copy, when not kInPlace).
	/// @param volume_l, volume_r 4.27 signed.
	/// @param final_volume 3.29.
	void render(std::span<StereoSample> block, q31 volume_l, q31 volume_r, q31 final_volume) {
		mono_.template on<&Compressor::set_levels>(Compressor::Levels{
		    .volume_l = volume_l, .volume_r = volume_r, .final_volume = final_volume, .threshold = threshold_knob_});
		if (!mono_.state().seeded) {
			mono_.template on<&Compressor::seed>(block[0]);
		}
		if constexpr (kInPlace) {
			mono_.render_block(block, block);
		}
		else {
			const std::vector<StereoSample> in{block.begin(), block.end()};
			mono_.render_block(in, block);
		}
		mono_.template on<&Compressor::finish_level>();
	}

	/// @brief render at neutral channel gain, with @p final_volume (3.29) scaled.
	void render_volume_neutral(std::span<StereoSample> block, q31 final_volume) {
		render(block, 1 << 27, 1 << 27, final_volume >> 3);
	}

	/// @return The last block's gain reduction for the LEDs.
	[[nodiscard]] uint8_t gain_reduction() const { return mono_.state().gain_reduction; }

	/// @return The kernel's coefficients.
	[[nodiscard]] const Compressor::Coefficients& coefficients() const { return mono_.coefficients(); }

	/// @return The compressor between blocks, from the kernel's coefficients and state.
	[[nodiscard]] View view() const {
		const Compressor::Coefficients& c = mono_.coefficients();
		const Compressor::State& s = mono_.state();
		return {
		    .configured = configured_,
		    .attack_rate = c.attack_rate,
		    .release_rate = c.release_rate,
		    .fraction = c.fraction,
		    .makeup = s.makeup,
		    .threshold = s.threshold,
		    .detector_moveability = c.detector_moveability,
		    .reduction_envelope = s.reduction_envelope,
		    .current_volume_l = s.loop.current_volume_l,
		    .current_volume_r = s.loop.current_volume_r,
		    .log_rms = s.log_rms,
		    .mean = s.mean,
		    .saturation_drive = Compressor::kSaturator.drive,
		    .saturation_l = s.loop.saturation_l,
		    .saturation_r = s.loop.saturation_r,
		    .seeded = s.seeded,
		    .detector_l = s.loop.detector_l,
		    .detector_r = s.loop.detector_r,
		    .attack_ms = c.attack_ms,
		    .release_ms = c.release_ms,
		    .base_gain = c.base_gain,
		    .threshold_knob = threshold_knob_,
		    .dry = c.dry,
		    .wet = c.wet,
		    .gain_reduction = s.gain_reduction,
		};
	}

private:
	[[nodiscard]] Compressor::Settings settings() const { return {.knobs = configured_, .base_gain = base_gain_}; }

	CompressorKnobs configured_{};
	float base_gain_ = 1.35f;
	q31 threshold_knob_ = 0;
	Mono<Compressor> mono_;
};

using InPlace = TestCompressor<true>;
using OutOfPlace = TestCompressor<false>;

/// @return Where @p workload, played through compressor @p A and through compressor @p B, first parts: each block's
/// output and gain reduction, and the whole view after each block the owner renders. A block the owner only resets
/// is not compared on its view: the reference takes a new threshold knob at once, the kernel at its next block's
/// @ref Compressor::set_levels, and both render that block alike.
template <typename A, typename B>
[[nodiscard]] std::optional<cw::Parting> first_kernel_parting(const cw::CompressorWorkload& workload) {
	cw::CompressorRun<A> a{workload};
	cw::CompressorRun<B> b{workload};
	std::vector<StereoSample> out_a;
	std::vector<StereoSample> out_b;
	for (size_t block = 0; block < workload.blocks; ++block) {
		const uint8_t ra = a.block(block, out_a);
		const uint8_t rb = b.block(block, out_b);
		if (!std::ranges::equal(out_a, out_b,
		                        [](StereoSample x, StereoSample y) { return x.l == y.l && x.r == y.r; })) {
			return cw::Parting{.block = block, .what = "output"};
		}
		if (ra != rb) {
			return cw::Parting{.block = block, .what = "gain"};
		}
		if (workload.step(block).render && a.state() != b.state()) {
			return cw::Parting{.block = block, .what = "state"};
		}
	}
	if (a.state() != b.state()) {
		return cw::Parting{.block = workload.blocks, .what = "final state"};
	}
	return std::nullopt;
}

/// @return Each workload where compressor @p B parts from compressor @p A, named with the block and what parted.
template <typename A, typename B>
[[nodiscard]] std::vector<std::string> partings() {
	std::vector<std::string> parted;
	for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
		const auto parting = first_kernel_parting<A, B>(workload);
		if (parting) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
			                 + parting->what);
		}
	}
	return parted;
}

/// @return Workload @p id.
[[nodiscard]] const cw::CompressorWorkload& workload_of(cw::CompressorWorkloadId id) {
	return cw::all_compressor_workloads().at(static_cast<size_t>(id));
}

/// @return A state with every member set away from its default, so a reset shows what it keeps.
[[nodiscard]] Compressor::State busy_state() {
	Compressor::State s{};
	s.levels = {.volume_l = 1, .volume_r = 2, .final_volume = 3, .threshold = 4};
	s.threshold = 0.5f;
	s.makeup = 1.5f;
	s.reduction_envelope = 2.5f;
	s.loop.current_volume_l = 5;
	s.loop.current_volume_r = 6;
	s.log_rms = 7.5f;
	s.mean = 0.25f;
	s.loop.saturation_l.position = 8;
	s.loop.saturation_r.position = 9;
	s.loop.detector_l.z = 10;
	s.loop.detector_r.z = 11;
	s.loop.sum = 12;
	s.n = 13;
	s.seeded = true;
	s.gain_reduction = 14;
	return s;
}

} // namespace

// clang-format off
describe compressor_kernel("the Compressor kernel", ${
	it("renders every workload as the fixed reference does, in place: each block's output and gain reduction, and the "
	   "whole view after every block it renders", _ {
		expect(partings<FixedReferenceRmsFeedbackCompressor, InPlace>()).to_equal(std::vector<std::string>{});
	});

	it("renders every workload as the fixed reference's records: each block's output and gain reduction, and the "
	   "state", _ {
		for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
			expect(cw::record<InPlace>(workload))
			    .to_equal(cw::kFixedCompressorRecorded.at(static_cast<size_t>(workload.id)));
		}
	});

	it("renders out of place as it does in place", _ {
		expect(partings<InPlace, OutOfPlace>()).to_equal(std::vector<std::string>{});
	});

	it("switches between WET and BLEND between the blocks of BLEND_SWITCH", _ {
		const cw::CompressorWorkload& workload = workload_of(cw::CompressorWorkloadId::BLEND_SWITCH);
		cw::CompressorRun<InPlace> run{workload};
		std::vector<StereoSample> block;
		size_t wet = 0;
		size_t blended = 0;
		size_t switches = 0;
		Compressor::Mode last = Compressor::Mode::WET;
		for (size_t b = 0; b < workload.blocks; ++b) {
			std::ignore = run.block(b, block);
			const Compressor::Mode mode = run.compressor().coefficients().mode;
			expect(mode).to_equal(workload.step(b).knobs.blend == kOneQ31 ? Compressor::Mode::WET
			                                                               : Compressor::Mode::BLEND);
			wet += static_cast<size_t>(mode == Compressor::Mode::WET);
			blended += static_cast<size_t>(mode == Compressor::Mode::BLEND);
			switches += static_cast<size_t>(b > 0 && mode != last);
			last = mode;
		}
		expect(wet).to_be_greater_than(size_t{0});
		expect(blended).to_be_greater_than(size_t{0});
		expect(switches).to_be_greater_than(size_t{2});
	});

	it("runs every block length from 1 to 9, and 128, in BLOCK_LENGTHS", _ {
		const cw::CompressorWorkload& workload = workload_of(cw::CompressorWorkloadId::BLOCK_LENGTHS);
		std::set<size_t> lengths;
		for (size_t b = 0; b < workload.blocks; ++b) {
			lengths.insert(workload.step(b).frames);
		}
		expect(lengths).to_equal(std::set<size_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 128});
	});

	it("sums a loud block's squares past 2^31 without wrapping, in WRAPPED_SUM", _ {
		const cw::CompressorWorkload& workload = workload_of(cw::CompressorWorkloadId::WRAPPED_SUM);
		Mono<Compressor> mono{Compressor::Settings{.knobs = workload.step(0).knobs}};
		int64_t largest = 0;
		for (size_t b = 0; b < workload.blocks; ++b) {
			const cw::CompressorStep step = workload.step(b);
			std::vector<StereoSample> block = cw::compressor_input(workload, b);
			mono.on<&Compressor::set_levels>(Compressor::Levels{
			    .volume_l = 1 << 27, .volume_r = 1 << 27, .final_volume = step.final_volume >> 3,
			    .threshold = step.threshold});
			if (!mono.state().seeded) {
				mono.on<&Compressor::seed>(block[0]);
			}
			mono.render_block(block, block);
			largest = std::max<int64_t>(largest, mono.state().loop.sum);
			mono.on<&Compressor::finish_level>();
			expect(mono.state().mean).not_().to_be_less_than(0.0F);
		}
		expect(largest).to_be_greater_than(int64_t{INT32_MAX});
	});

	it("resets the reduction and makeup envelopes, the mean and the seed, and keeps the rest", _ {
		Compressor::State s = busy_state();
		Compressor::reset(s);
		Compressor::State expected = busy_state();
		expected.reduction_envelope = 0;
		expected.makeup = 0;
		expected.mean = 0;
		expected.seeded = false;
		expect(s.reduction_envelope).to_equal(expected.reduction_envelope);
		expect(s.makeup).to_equal(expected.makeup);
		expect(s.mean).to_equal(expected.mean);
		expect(s.seeded).to_equal(expected.seeded);
		expect(s.threshold).to_equal(expected.threshold);
		expect(s.loop.current_volume_l).to_equal(expected.loop.current_volume_l);
		expect(s.loop.current_volume_r).to_equal(expected.loop.current_volume_r);
		expect(s.log_rms).to_equal(expected.log_rms);
		expect(s.loop.saturation_l.position).to_equal(expected.loop.saturation_l.position);
		expect(s.loop.saturation_r.position).to_equal(expected.loop.saturation_r.position);
		expect(s.loop.detector_l.z).to_equal(expected.loop.detector_l.z);
		expect(s.loop.detector_r.z).to_equal(expected.loop.detector_r.z);
		expect(s.levels.threshold).to_equal(expected.levels.threshold);
		expect(s.gain_reduction).to_equal(expected.gain_reduction);
	});

	it("takes the threshold knob into the block's levels and a fraction of the volume's level", _ {
		Compressor::State s{};
		Compressor::set_levels(s, {.volume_l = 1, .volume_r = 2, .final_volume = 3, .threshold = 1 << 30});
		expect(s.levels.volume_l).to_equal(1);
		expect(s.levels.volume_r).to_equal(2);
		expect(s.levels.final_volume).to_equal(3);
		expect(s.levels.threshold).to_equal(1 << 30);
		expect(s.threshold).to_equal(1 - 0.8f * (static_cast<float>(1 << 30) / kOneQ31f));
	});
});
// clang-format on

CPPSPEC_MAIN(compressor_kernel);
