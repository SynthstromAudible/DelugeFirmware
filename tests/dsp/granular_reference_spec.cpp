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

// The granular reference (reference_granular.h) over every workload (granular_workloads.h): that each runs clean under
// the signed-overflow sanitizer, that the workloads reach what they are for, and the reference's own rules.

#include "granular_workloads.h"
#include "reference_granular.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

namespace gw = deluge::dsp::test::granular_workloads;
using deluge::dsp::test::BasicReferenceGranular;
using deluge::dsp::test::ReferenceGranular;

/// @return The workload named @p name.
[[nodiscard]] const gw::GranularWorkload& workload_named(std::string_view name) {
	for (const gw::GranularWorkload& workload : gw::all_granular_workloads()) {
		if (name == workload.name) {
			return workload;
		}
	}
	std::abort();
}

/// @return What @p workload renders on a fresh reference, which is the size of its ring and so lives on the heap.
[[nodiscard]] gw::GranularTrace render(const gw::GranularWorkload& workload) {
	const auto fx = std::make_unique<ReferenceGranular>();
	return gw::play(workload, *fx);
}

/// @return How many of @p values are not zero.
[[nodiscard]] std::size_t nonzero(const std::vector<int32_t>& values) {
	return static_cast<std::size_t>(std::ranges::count_if(values, [](int32_t v) { return v != 0; }));
}

/// A noise source that is not the global stream: a counter, so a draw through it is visible and the global stream
/// is left alone.
struct CountingNoise {
	static inline uint32_t state = 12345;
	static inline std::size_t draws = 0;
	static uint32_t next() {
		draws += 1;
		state = (state * 1664525U) + 1013904223U;
		return state;
	}
};

} // namespace

// clang-format off
describe reference_granular("the granular reference", ${
	it("renders every workload, a block's frames in and out, with its reverb feed and volume", _ {
		for (const gw::GranularWorkload& workload : gw::all_granular_workloads()) {
			const gw::GranularTrace trace = render(workload);
			expect(trace.samples.size()).to_equal(trace.inputs.size());
			expect(trace.sends.size()).to_equal(trace.samples.size() / 2);
			expect(trace.volumes.size()).to_equal(workload.blocks.size());
			expect(trace.samples.size()).to_be_greater_than(std::size_t{0});
		}
	});

	it("renders the same twice", _ {
		for (const gw::GranularWorkload& workload : gw::all_granular_workloads()) {
			const gw::GranularTrace first = render(workload);
			const gw::GranularTrace second = render(workload);
			expect(first.samples).to_equal(second.samples);
			expect(first.sends).to_equal(second.sends);
			expect(first.volumes).to_equal(second.volumes);
		}
	});

	it("changes the signal and feeds the reverb", _ {
		const gw::GranularTrace trace = render(workload_named("FIRST_PASS"));
		expect(trace.samples).not_().to_equal(trace.inputs);
		std::vector<int32_t> started;
		for (std::size_t b = 0; b < workload_named("FIRST_PASS").blocks.size(); ++b) {
			const std::vector<int32_t> start = gw::send_input(workload_named("FIRST_PASS").blocks[b].n, b, false);
			started.insert(started.end(), start.begin(), start.end());
		}
		expect(trace.sends).not_().to_equal(started);
	});

	it("draws every pitch class in PITCH_CLASSES, the default arm among them", _ {
		ReferenceGranular::reset_counters();
		std::ignore = render(workload_named("PITCH_CLASSES"));
		for (const std::size_t drawn : ReferenceGranular::pitch_classes) {
			expect(drawn).to_be_greater_than(std::size_t{0});
		}
	});

	it("wraps the reverb feed in FULL_SCALE", _ {
		ReferenceGranular::reset_counters();
		std::ignore = render(workload_named("FULL_SCALE"));
		expect(ReferenceGranular::wraps.send).to_be_greater_than(std::size_t{0});
	});

	it("reads no slot it has not written in the first pass: grains start behind the write index", _ {
		ReferenceGranular::reset_counters();
		std::ignore = render(workload_named("FIRST_PASS"));
		expect(ReferenceGranular::unwritten_reads).to_equal(std::size_t{0});
	});

	it("reads slots not written since the ring was acquired, after a skip and after a decay", _ {
		for (const std::string_view name : {"SKIP_MID", "DECAY_RESTART"}) {
			ReferenceGranular::reset_counters();
			std::ignore = render(workload_named(name));
			expect(ReferenceGranular::unwritten_reads).to_be_greater_than(std::size_t{0});
		}
	});

	it("is dry at the top of the mix knob: the cubic's offset wraps the wet volume to zero", _ {
		const gw::GranularWorkload dry =
		    gw::detail::Builder{}.knobs(gw::kRateHigh, gw::kMixMax, gw::kDensityHigh, gw::kRandomnessMax).run(400).build("DRY");
		const gw::GranularTrace trace = render(dry);
		// Wet exactly zero: the output is the dry product alone and the reverb feed is untouched.
		std::size_t differing = 0;
		for (std::size_t i = 0; i < trace.samples.size(); ++i) {
			differing += static_cast<std::size_t>(trace.samples[i] != q31_mult(trace.inputs[i], 2147483647));
		}
		expect(differing).to_equal(std::size_t{0});
		std::vector<int32_t> started;
		for (std::size_t b = 0; b < dry.blocks.size(); ++b) {
			const std::vector<int32_t> start = gw::send_input(dry.blocks[b].n, b, false);
			started.insert(started.end(), start.begin(), start.end());
		}
		expect(trace.sends).to_equal(started);
	});

	it("keeps the ring across a clear and zeroes it again after a skip or a decay", _ {
		const auto acquisitions = [](std::string_view name) {
			const auto fx = std::make_unique<ReferenceGranular>();
			std::ignore = gw::play(workload_named(name), *fx);
			return fx->acquisitions();
		};
		expect(acquisitions("CLEAR_MID")).to_equal(std::size_t{1});
		expect(acquisitions("SKIP_MID")).to_equal(std::size_t{2});
		expect(acquisitions("DECAY_RESTART")).to_equal(std::size_t{2});
	});

	it("releases the ring when the countdown goes below zero, and not while it is not", _ {
		const auto fx = std::make_unique<ReferenceGranular>();
		const gw::GranularWorkload& tail = workload_named("TAIL");
		std::ignore = gw::play(tail, *fx);
		expect(fx->holding()).to_equal(true);
		expect(fx->getSamplesToShutdown()).to_be_greater_than(-1);

		const auto decayed = std::make_unique<ReferenceGranular>();
		const gw::GranularWorkload& decay = workload_named("DECAY_RESTART");
		gw::GranularWorkload silent{decay.name, {decay.blocks.begin(), decay.blocks.begin() + 3000}, decay.cong_seed};
		std::ignore = gw::play(silent, *decayed);
		expect(decayed->holding()).to_equal(false);
	});

	it("goes on sounding the grains after the input stops, while the countdown is not below zero", _ {
		const gw::GranularWorkload& tail = workload_named("TAIL");
		const gw::GranularTrace trace = render(tail);
		const std::size_t first_silent = 200 * 256 * 2;
		const std::vector<int32_t> block(trace.samples.begin() + static_cast<std::ptrdiff_t>(first_silent),
		                                 trace.samples.begin() + static_cast<std::ptrdiff_t>(first_silent + 256));
		expect(nonzero(block)).to_be_greater_than(std::size_t{0});
	});

	it("sets grains free of the first pass only when a call starts at the ring's end", _ {
		const gw::GranularWorkload& blocks = workload_named("SHORT_BLOCKS");
		std::size_t short_blocks = 0;
		std::size_t short_frames = 0;
		while (blocks.blocks[short_blocks].n <= 9) {
			short_frames += blocks.blocks[short_blocks].n;
			short_blocks += 1;
		}
		expect(short_frames).to_be_greater_than(static_cast<std::size_t>(2 * kModFXGrainBufferSize));

		const auto fx = std::make_unique<ReferenceGranular>();
		const gw::GranularWorkload shorts{blocks.name, {blocks.blocks.begin(), blocks.blocks.begin() + static_cast<std::ptrdiff_t>(short_blocks)}, blocks.cong_seed};
		std::ignore = gw::play(shorts, *fx);
		expect(fx->grain_initialized()).to_equal(false);

		const auto full = std::make_unique<ReferenceGranular>();
		std::ignore = gw::play(blocks, *full);
		expect(full->grain_initialized()).to_equal(true);
	});

	it("flips to initialised in FIRST_PASS, whose blocks divide the ring", _ {
		const auto fx = std::make_unique<ReferenceGranular>();
		std::ignore = gw::play(workload_named("FIRST_PASS"), *fx);
		expect(fx->grain_initialized()).to_equal(true);
	});

	it("draws its noise through the policy and leaves the global stream alone", _ {
		jcong = gw::kCongSeed;
		CountingNoise::draws = 0;
		const auto fx = std::make_unique<BasicReferenceGranular<CountingNoise>>();
		const gw::GranularWorkload& workload = workload_named("FIRST_PASS");
		const uint32_t before = jcong;
		for (std::size_t b = 0; b < workload.blocks.size(); ++b) {
			const gw::GranularBlock& block = workload.blocks[b];
			std::vector<deluge::dsp::StereoSample> frames = gw::stereo_input(block.n, b);
			std::vector<q31_t> send(block.n, 0);
			int32_t volume = gw::kStartVolume;
			fx->processGrainFX(frames, block.rate, block.mix, block.density, block.randomness, &volume, true, gw::kSendAmount, send);
		}
		expect(CountingNoise::draws).to_be_greater_than(std::size_t{0});
		expect(jcong).to_equal(before);
	});
});
// clang-format on

CPPSPEC_MAIN(reference_granular);
