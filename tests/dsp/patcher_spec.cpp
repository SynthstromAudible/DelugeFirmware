/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// Today's patching arithmetic, site by site, with values worked from the formulas; and the reference patcher
// (tests/dsp/reference_patcher.*) shown to give the live Patcher's results on random cases.

#include "alloc_seams.h"
#include "patch_cases.h"

#include "synth/modulation/cable_depth.h"
#include "synth/modulation/control_block.h"
#include "synth/modulation/patcher.h"
#include "util/param_functions.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <memory>

using namespace deluge::synth;
using namespace deluge::synth::test;
namespace params = deluge::modulation::params;

void functionsInit(); // util/param_functions.cpp: fills the param scaling tables

namespace {

/// The live Patcher's results for @p c, in the reference's shape.
ReferenceResult live_results(const PatchCase& c, const CompiledCase& compiled) {
	ControlBlock block{};
	block.routing = compiled.block.routing;
	block.preset_values = compiled.block.preset_values;
	block.depths = compiled.block.depths;
	const Patcher::Config voice_config{kVoiceConfig.first_param,        kVoiceConfig.first_non_volume_param,
	                                   kVoiceConfig.first_hybrid_param, kVoiceConfig.first_exp_param,
	                                   kVoiceConfig.end_params,         kVoiceConfig.globality};
	const Patcher::Config sound_config{kSoundConfig.first_param,        kSoundConfig.first_non_volume_param,
	                                   kSoundConfig.first_hybrid_param, kSoundConfig.first_exp_param,
	                                   kSoundConfig.end_params,         kSoundConfig.globality};
	ReferenceResult out;
	std::array<int32_t, kNumPatchSources> voice_sources = c.sources[0];
	std::array<int32_t, kNumGlobalSources> sound_sources{};
	std::copy_n(c.sources[0].begin(), kNumGlobalSources, sound_sources.begin());
	std::array<int32_t, params::FIRST_GLOBAL> voice_finals{};
	std::array<int32_t, kNumGlobalParams> sound_finals{};
	Patcher voice{voice_config, voice_sources, voice_finals};
	Patcher sound{sound_config, sound_sources, sound_finals};
	// One live Patcher runs at a time over its global depth results, as on the audio tier: the sound's pass, then the
	// voice's, never interleaved within a block
	sound.perform_initial_patching(block);
	out.sound[0] = sound_finals;
	voice.perform_initial_patching(block);
	out.voice[0] = voice_finals;
	for (size_t b = 0; b < c.sources_changed.size(); ++b) {
		voice_sources = c.sources[b + 1];
		std::copy_n(c.sources[b + 1].begin(), kNumGlobalSources, sound_sources.begin());
		sound.perform_patching(c.sources_changed[b], block);
		out.sound[b + 1] = sound_finals;
		voice.perform_patching(c.sources_changed[b], block);
		out.voice[b + 1] = voice_finals;
	}
	return out;
}

} // namespace

// clang-format off
describe patcher_spec("today's patching", ${
	before_all([] { functionsInit(); });

	context("polarity", _{
		it("halves a bipolar source to unipolar with C's truncating division", _{
			expect(to_polarity(PatchSource::ENVELOPE_0, CablePolarity::UNIPOLAR, -3)).to_equal(1073741822);
			expect(to_polarity(PatchSource::ENVELOPE_0, CablePolarity::UNIPOLAR, 3)).to_equal(1073741824);
		});
		it("reads aftertouch bipolar by recentring and doubling", _{
			expect(to_polarity(PatchSource::AFTERTOUCH, CablePolarity::BIPOLAR, 1073741823)).to_equal(0);
			expect(to_polarity(PatchSource::AFTERTOUCH, CablePolarity::UNIPOLAR, 5)).to_equal(5);
		});
		it("never converts Y", _{
			expect(to_polarity(PatchSource::Y, CablePolarity::UNIPOLAR, -5)).to_equal(-5);
		});
	});

	context("depth modes", _{
		it("squares a pitch depth, keeping its sign", _{
			expect(modified_depth(-(1 << 30), PatchSource::LFO_LOCAL_1, params::LOCAL_OSC_A_PITCH_ADJUST))
			    .to_equal(-(1 << 29));
			expect(modified_depth(1 << 30, PatchSource::LFO_LOCAL_1, params::GLOBAL_DELAY_RATE)).to_equal(1 << 29);
		});
		it("leaves other params' depths alone", _{
			expect(modified_depth(12345, PatchSource::LFO_LOCAL_1, params::LOCAL_LPF_FREQ)).to_equal(12345);
		});
	});

	context("range", _{
		it("floor-shifts and clamps every cable through a range of 1", _{
			expect(apply_range(7, 536870912)).to_equal(0);                 // (7 * 2^29) >> 32 = 0
			expect(apply_range(1 << 30, 536870912)).to_equal(536870904);   // 2^27, clamped to 2^26 - 1, << 3
			expect(apply_range(-(1 << 30), 536870912)).to_equal(-(1 << 29)); // -2^27, clamped to -2^26, << 3
		});
	});

	context("the random cases", _{
		it("route cables in both scopes, depth cables among them", _{
			int32_t with_voice = 0;
			int32_t with_sound = 0;
			int32_t with_depth = 0;
			for (uint32_t seed = 1; seed <= 2000; ++seed) {
				const PatchCase c = random_case(seed);
				auto compiled = std::make_unique<CompiledCase>();
				compile_case(c, *compiled);
				with_voice += compiled->build.num_destinations[kLocal] != 0 ? 1 : 0;
				with_sound += compiled->build.num_destinations[kGlobal] != 0 ? 1 : 0;
				bool depth = false;
				for (int32_t i = 0; i < compiled->build.num_cables; ++i) {
					depth = depth || compiled->build.cables[i].range_index >= 0;
				}
				with_depth += depth ? 1 : 0;
			}
			expect(with_voice).to_be_greater_than(1500);
			expect(with_sound).to_be_greater_than(1000);
			expect(with_depth).to_be_greater_than(500);
		});
	});

	context("the reference patcher", _{
		it("gives the live Patcher's results on random cases", _{
			int32_t differing = 0;
			for (uint32_t seed = 1; seed <= 2000; ++seed) {
				const PatchCase c = random_case(seed);
				auto compiled = std::make_unique<CompiledCase>();
				compile_case(c, *compiled);
				const ReferenceResult reference = reference_results(c, *compiled, Ranges::shared);
				const ReferenceResult live = live_results(c, *compiled);
				differing += (reference.voice != live.voice || reference.sound != live.sound) ? 1 : 0;
			}
			expect(differing).to_equal(0);
		});
		// The Patcher's depth results are one array for every pass: a pass that skips a depth destination reads the
		// last value any pass wrote there. The matrix runtime keeps that until the audible window fixes it.
		it("shares its depth results across passes, which per-pass results change", _{
			int32_t leak = 0;
			for (uint32_t seed = 1; seed <= 2000; ++seed) {
				const PatchCase c = random_case(seed);
				auto compiled = std::make_unique<CompiledCase>();
				compile_case(c, *compiled);
				const ReferenceResult a = reference_results(c, *compiled, Ranges::shared);
				const ReferenceResult b = reference_results(c, *compiled, Ranges::per_pass);
				leak += (a.voice != b.voice || a.sound != b.sound) ? 1 : 0;
			}
			expect(leak).to_be_greater_than(0);
		});
	});
});
// clang-format on

CPPSPEC_MAIN(patcher_spec);
