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

// The mod FX's reference (reference_mod_fx.h) over every workload (mod_fx_workloads.h): that each runs clean under the
// signed-overflow sanitizer, that the workloads reach what they are for, and the reference's own rules.

#include "dsp-next/fixed/noise/congruential_step.h"
#include "mod_fx_workloads.h"
#include "reference_mod_fx.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

namespace mw = deluge::dsp::test::mod_fx_workloads;
using deluge::dsp::fixed::CongruentialStep;
using deluge::dsp::test::ReferenceModFx;

/// @return The workload named @p name.
[[nodiscard]] const mw::ModFxWorkload& workload_named(std::string_view name) {
	for (const mw::ModFxWorkload& workload : mw::all_mod_fx_workloads()) {
		if (name == workload.name) {
			return workload;
		}
	}
	std::abort();
}

/// @return What @p workload renders on a fresh reference.
[[nodiscard]] mw::ModFxTrace render(const mw::ModFxWorkload& workload) {
	ReferenceModFx fx;
	return mw::play(workload, fx);
}

/// The most draws @ref draws_of counts before it gives up; a run that reaches it reports the cap, which no workload's
/// expected count equals.
constexpr std::size_t kMaxDraws = 1 << 16;

/// @return How many times the two warblers of @p workload cross their period boundaries: one draw for each, counted
/// from the phases alone. The first LFO runs at the rate; the second at 0.97 of it.
[[nodiscard]] std::size_t expected_warble_draws(const mw::ModFxWorkload& workload) {
	constexpr q31_t width = 0.97 * kOneQ31;
	std::size_t frames = 0;
	for (const mw::ModFxBlock& block : workload.blocks) {
		frames += block.n;
	}
	std::size_t draws = 0;
	for (const uint32_t rate :
	     {static_cast<uint32_t>(mw::kRate), static_cast<uint32_t>(multiply_32x32_rshift32(mw::kRate, width) << 1)}) {
		const uint32_t step = rate * 2; // the warbler sweeps at twice the rate
		uint32_t phase = 0;
		for (std::size_t i = 0; i < frames; ++i) {
			draws += static_cast<std::size_t>(phase + step < phase);
			phase += step;
		}
	}
	return draws;
}

/// @return How many times the generator was stepped from the workload's seed to where @p workload left it.
[[nodiscard]] std::size_t draws_of(const mw::ModFxWorkload& workload) {
	std::ignore = render(workload);
	const uint32_t end = deluge::synth_noise.state();
	uint32_t state = workload.cong_seed;
	std::size_t draws = 0;
	while (state != end && draws < kMaxDraws) {
		state = CongruentialStep::next(state);
		++draws;
	}
	return draws;
}

} // namespace

// clang-format off
describe reference_mod_fx("the mod FX reference", ${
	it("renders every workload, a block's frames in and out", _ {
		for (const mw::ModFxWorkload& workload : mw::all_mod_fx_workloads()) {
			const mw::ModFxTrace trace = render(workload);
			expect(trace.samples.size()).to_equal(trace.inputs.size());
			expect(trace.volumes.size()).to_equal(workload.blocks.size());
			expect(trace.samples.size()).to_be_greater_than(std::size_t{0});
		}
	});

	it("renders the same twice", _ {
		for (const mw::ModFxWorkload& workload : mw::all_mod_fx_workloads()) {
			expect(render(workload).samples).to_equal(render(workload).samples);
		}
	});

	it("changes the signal of each type, in stereo and in mono", _ {
		for (const mw::ModFxWorkload& workload : mw::all_mod_fx_workloads()) {
			if (std::string_view{workload.name}.starts_with("EACH_") || std::string_view{workload.name} == "PHASER") {
				const mw::ModFxTrace trace = render(workload);
				expect(trace.samples).not_().to_equal(trace.inputs);
			}
		}
	});

	it("wraps an add in FULL_SCALE", _ {
		ReferenceModFx::reset_wraps();
		std::ignore = render(workload_named("FULL_SCALE"));
		expect(ReferenceModFx::wraps).to_be_greater_than(std::size_t{0});
	});

	it("changes the volume but not the audio with no ring", _ {
		const mw::ModFxTrace trace = render(workload_named("NO_RING"));
		expect(trace.samples).to_equal(trace.inputs);
		for (const int32_t volume : trace.volumes) {
			expect(volume).not_().to_equal(mw::kStartVolume);
		}
	});

	it("leaves the volume and the audio alone for the type NONE and for a block with nothing in it", _ {
		const mw::ModFxWorkload& soft_off = workload_named("SOFT_OFF");
		const mw::ModFxTrace off = render(soft_off);
		for (std::size_t b = 0; b < soft_off.blocks.size(); ++b) {
			if (soft_off.blocks[b].type == ModFXType::NONE) {
				expect(off.volumes[b]).to_equal(mw::kStartVolume);
			}
		}
		const mw::ModFxWorkload& silent = workload_named("SILENT_BLOCKS");
		const mw::ModFxTrace quiet = render(silent);
		for (std::size_t b = 0; b < silent.blocks.size(); ++b) {
			if (!silent.blocks[b].sound) {
				expect(quiet.volumes[b]).to_equal(mw::kStartVolume);
			}
		}
	});

	it("draws the same noise in mono as in stereo: the second warbler steps in both", _ {
		const std::size_t expected = expected_warble_draws(workload_named("EACH_WARBLE_STEREO"));
		expect(expected).to_be_greater_than(std::size_t{0});
		expect(expected).to_be_less_than(kMaxDraws);
		expect(draws_of(workload_named("EACH_WARBLE_STEREO"))).to_equal(expected);
		expect(draws_of(workload_named("EACH_WARBLE_MONO"))).to_equal(expected);
	});

	it("draws no noise for a type that is not the warble", _ {
		expect(draws_of(workload_named("EACH_CHORUS_STEREO"))).to_equal(std::size_t{0});
		expect(draws_of(workload_named("PHASER"))).to_equal(std::size_t{0});
	});

	it("restarts the warbler when the wave changes, and carries it across blocks of the same wave", _ {
		constexpr uint32_t kInc = 0x00a00000;
		int32_t held = 0;
		const auto warbled = [&held](bool change_wave) {
			deluge::synth_noise.seed(mw::kCongSeed);
			deluge::dsp::test::ReferenceLfo lfo;
			lfo.tick(1, 0xFFFF0000U); // so that the first warbler render crosses its period boundary and draws a target
			int32_t value = 0;
			for (int i = 0; i < 50; ++i) {
				value = lfo.render(1, LFOType::WARBLER, kInc);
			}
			held = value;
			if (change_wave) {
				std::ignore = lfo.render(1, LFOType::TRIANGLE, kInc);
			}
			return lfo.render(1, LFOType::WARBLER, kInc);
		};
		expect(warbled(false)).not_().to_equal(0);
		expect(held).not_().to_equal(0);
		expect(warbled(true)).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(reference_mod_fx);
