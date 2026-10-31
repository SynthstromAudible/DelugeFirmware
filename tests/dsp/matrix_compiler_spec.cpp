/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The matrix compiler against today's routing: every destination keeps its cables, in today's chain order, with the
// conversion, depth, range link and depth shape today's patching gives each.

#include "alloc_seams.h"
#include "param_regions.h"
#include "patch_cases.h"

#include "synth/matrix/compiler.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

using namespace deluge::synth;
using namespace deluge::synth::matrix;
using namespace deluge::synth::test;

namespace {

struct Compiled {
	CompiledCase routed;
	std::vector<std::byte> storage;
	const MatrixProgram* program{};
};

std::unique_ptr<Compiled> compile(uint32_t seed) {
	auto out = std::make_unique<Compiled>();
	const PatchCase c = random_case(seed);
	compile_case(c, out->routed);
	out->storage.resize(program_size(out->routed.build) + alignof(MatrixProgram));
	auto* at = out->storage.data();
	while (reinterpret_cast<std::uintptr_t>(at) % alignof(MatrixProgram) != 0) {
		++at;
	}
	out->program = compile_program(out->routed.build, {at, program_size(out->routed.build)});
	return out;
}

/// Each way the program differs from the routing it compiled, counted.
int32_t mismatches(const Compiled& c) {
	int32_t bad = 0;
	const RoutingBuild& routing = c.routed.build;
	for (int32_t g = 0; g < 2; ++g) {
		const ScopeView& scope = c.program->scopes[g];
		bad += scope.num_destinations != routing.num_destinations[g] ? 1 : 0;
		bad += scope.sources_patched != routing.sources_patched[g] ? 1 : 0;
		bool params_started = false;
		uint64_t cabled = 0;
		int32_t num_depth = 0;
		for (int32_t d = 0; d < scope.num_destinations; ++d) {
			const RoutedDestination& routed = routing.destinations[g][d];
			const Destination& dest = scope.destinations[d];
			const bool depth = !routed.target.is_param();
			// Depth destinations first, numbered in routing order
			bad += (depth && params_started) ? 1 : 0;
			params_started = params_started || !depth;
			bad +=
			    (depth ? dest.chain != ChainKind::DEPTH : dest.chain != chain_of(routed.target.param_index())) ? 1 : 0;
			bad += (depth ? dest.param != num_depth++ : dest.param != routed.target.param_index()) ? 1 : 0;
			bad += dest.num_cables != routed.end_cable - routed.first_cable ? 1 : 0;
			bad += dest.sources != routed.sources ? 1 : 0;
			if (!depth) {
				cabled |= uint64_t{1} << (routed.target.param_index() - (g == 0 ? 0 : int32_t{params::FIRST_GLOBAL}));
			}
			for (int32_t k = 0; k < dest.num_cables; ++k) {
				const int32_t at = dest.first_cable + k;
				const int32_t c_index = routed.first_cable + k;
				const RoutedCable& cable = routing.cables[c_index];
				const uint8_t slot = scope.cable_slot[at];
				bad += scope.slot_source[slot] != std::to_underlying(cable.source) ? 1 : 0;
				bad += scope.slot_conversion[slot] != conversion_of(cable.source, cable.polarity, depth) ? 1 : 0;
				bad += scope.cable_depth[at] != c_index ? 1 : 0;
				const uint8_t range =
				    (depth || cable.range_index < 0) ? kNoRange : static_cast<uint8_t>(cable.range_index);
				bad += scope.cable_range[at] != range ? 1 : 0;
				const DepthMode mode = dest.chain == ChainKind::ADD
				                           ? depth_mode_of(cable.source, routed.target.param_index())
				                           : DepthMode::PLAIN;
				bad += scope.cable_mode[at] != mode ? 1 : 0;
			}
		}
		bad += cabled != scope.cabled ? 1 : 0;
		// The slots are distinct
		for (int32_t a = 0; a < scope.num_slots; ++a) {
			for (int32_t b = a + 1; b < scope.num_slots; ++b) {
				bad += (scope.slot_source[a] == scope.slot_source[b]
				        && scope.slot_conversion[a] == scope.slot_conversion[b])
				           ? 1
				           : 0;
			}
		}
	}
	return bad;
}

/// The chains the params had by their position in the enum, kept to show the table sets every param as they did.
ChainKind region_chain(int32_t p) {
	const int32_t first_hybrid = scope_of(p) == 0 ? int32_t{regions::kLocalBipolar} : int32_t{regions::kGlobalBipolar};
	return p < first_hybrid ? ChainKind::MULTIPLY : ChainKind::ADD;
}

/// The curves the params had by their position in the enum.
CurveKind region_curve(int32_t p) {
	const bool voice = scope_of(p) == 0;
	const int32_t first_non_volume = voice ? int32_t{regions::kLocalLinear} : int32_t{regions::kGlobalLinear};
	const int32_t first_hybrid = voice ? int32_t{regions::kLocalBipolar} : int32_t{regions::kGlobalBipolar};
	const int32_t first_exp = voice ? int32_t{regions::kLocalExponential} : int32_t{regions::kGlobalExponential};
	if (p < first_non_volume) {
		return CurveKind::SQUARE;
	}
	if (p < first_hybrid) {
		return CurveKind::LINEAR;
	}
	if (p < first_exp) {
		return CurveKind::BIPOLAR;
	}
	if (params::LOCAL_ENV_0_DECAY <= p && p <= params::LOCAL_ENV_3_RELEASE) {
		return CurveKind::RELEASE_TABLE;
	}
	if (params::LOCAL_ENV_0_ATTACK <= p && p <= params::LOCAL_ENV_3_ATTACK) {
		return CurveKind::EXPONENTIAL_INVERTED;
	}
	return CurveKind::EXPONENTIAL;
}

/// The preset ranges as getParamRange gave them, kept to show the table sets every param as it did.
int32_t old_range(int32_t p) {
	switch (p) {
	case params::LOCAL_ENV_0_ATTACK:
	case params::LOCAL_ENV_1_ATTACK:
	case params::LOCAL_ENV_2_ATTACK:
	case params::LOCAL_ENV_3_ATTACK:
		return 536870912 * 1.5;

	case params::GLOBAL_DELAY_RATE:
		return 536870912;

	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
		return 536870912;

	case params::LOCAL_LPF_FREQ:
		return 536870912 * 1.4;

		// For phase width, we have this higher (than I previously did) because these are hibrid params, meaning that
		// with a source (e.g. LFO) patched to them, the might have up to 1073741824 added to them
		// - which would take us to the max user "preset value", which is what we want for phase width
	default:
		return 1073741824;
	}
}

/// The neutral values as getParamNeutralValue gave them.
int32_t old_neutral(int32_t p) {
	switch (p) {
	case params::LOCAL_OSC_A_VOLUME:
	case params::LOCAL_OSC_B_VOLUME:
	case params::GLOBAL_VOLUME_POST_REVERB_SEND:
	case params::LOCAL_NOISE_VOLUME:
	case params::GLOBAL_REVERB_AMOUNT:
	case params::GLOBAL_VOLUME_POST_FX:
	case params::LOCAL_VOLUME:
		return 134217728;

	case params::LOCAL_MODULATOR_0_VOLUME:
	case params::LOCAL_MODULATOR_1_VOLUME:
		return 33554432;

	case params::LOCAL_LPF_FREQ:
		return 2000000;
	case params::LOCAL_HPF_FREQ:
		return 2672947;

	case params::GLOBAL_LFO_FREQ_1:
	case params::GLOBAL_LFO_FREQ_2:
	case params::LOCAL_LFO_LOCAL_FREQ_1:
	case params::LOCAL_LFO_LOCAL_FREQ_2:
	case params::GLOBAL_MOD_FX_RATE:
		return 121739; // lfoRateTable[userValue];

	case params::LOCAL_LPF_RESONANCE:
	case params::LOCAL_HPF_RESONANCE:
	case params::LOCAL_LPF_MORPH:
	case params::LOCAL_HPF_MORPH:
	case params::LOCAL_FOLD:
		return 25 * 10737418; // Room to be quadrupled

	case params::LOCAL_PAN:
	case params::LOCAL_OSC_A_PHASE_WIDTH:
	case params::LOCAL_OSC_B_PHASE_WIDTH:
		return 0;

	case params::LOCAL_ENV_0_ATTACK:
	case params::LOCAL_ENV_1_ATTACK:
	case params::LOCAL_ENV_2_ATTACK:
	case params::LOCAL_ENV_3_ATTACK:
		return 4096; // attackRateTable[userValue];

	case params::LOCAL_ENV_0_RELEASE:
	case params::LOCAL_ENV_1_RELEASE:
	case params::LOCAL_ENV_2_RELEASE:
	case params::LOCAL_ENV_3_RELEASE:
		return 140 << 9; // releaseRateTable[userValue];

	case params::LOCAL_ENV_0_DECAY:
	case params::LOCAL_ENV_1_DECAY:
	case params::LOCAL_ENV_2_DECAY:
	case params::LOCAL_ENV_3_DECAY:
		return 70 << 9; // releaseRateTable[userValue] >> 1;

	case params::LOCAL_ENV_0_SUSTAIN:
	case params::LOCAL_ENV_1_SUSTAIN:
	case params::LOCAL_ENV_2_SUSTAIN:
	case params::LOCAL_ENV_3_SUSTAIN:
	case params::GLOBAL_DELAY_FEEDBACK:
		return 1073741824; // 536870912;

	case params::LOCAL_MODULATOR_0_FEEDBACK:
	case params::LOCAL_MODULATOR_1_FEEDBACK:
	case params::LOCAL_CARRIER_0_FEEDBACK:
	case params::LOCAL_CARRIER_1_FEEDBACK:
		return 5931642;

	case params::GLOBAL_DELAY_RATE:
	case params::GLOBAL_ARP_RATE:
	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
		return kMaxSampleValue; // Means we have space to 8x (3-octave-shift) the pitch if we want... (wait, I've since
		                        // made it 16x smaller)

	case params::GLOBAL_MOD_FX_DEPTH:
		return 526133494; // 2% lower than 536870912

	default:
		return 0;
	}
}

bool region_bipolar(int32_t p) {
	return (regions::kLocalBipolar <= p && p < regions::kLocalExponential)
	       || (regions::kGlobalBipolar <= p && p < regions::kGlobalExponential);
}

} // namespace

// clang-format off
describe matrix_compiler_spec("the matrix compiler", ${
	it("keeps every destination's cables in today's order, read and scaled as today", _{
		int32_t bad = 0;
		for (uint32_t seed = 1; seed <= 2000; ++seed) {
			bad += mismatches(*compile(seed));
		}
		expect(bad).to_equal(0);
	});

	it("sets every param's chain, curve and hybrid flag as the params' enum regions did", _{
		int32_t differing = 0;
		for (int32_t p = 0; p < params::kNumParams; ++p) {
			differing += (chain_of(p) != region_chain(p) || curve_of(p) != region_curve(p) || is_bipolar(p) != region_bipolar(p)) ? 1 : 0;
		}
		expect(differing).to_equal(0);
	});

	it("gives every param the neutral value and preset range it had", _{
		int32_t differing = 0;
		for (int32_t p = 0; p < params::kNumParams; ++p) {
			differing += (kParamNeutral[p] != old_neutral(p) || kParamRange[p] != old_range(p)) ? 1 : 0;
		}
		expect(differing).to_equal(0);
	});

	it("gives each param a curve and a chain by its scope's regions", _{
		expect(curve_of(params::LOCAL_VOLUME) == CurveKind::SQUARE).to_be_true();
		expect(curve_of(params::LOCAL_PAN) == CurveKind::BIPOLAR).to_be_true();
		expect(curve_of(params::LOCAL_LPF_FREQ) == CurveKind::EXPONENTIAL).to_be_true();
		expect(curve_of(params::LOCAL_ENV_0_ATTACK) == CurveKind::EXPONENTIAL_INVERTED).to_be_true();
		expect(curve_of(params::LOCAL_ENV_0_RELEASE) == CurveKind::RELEASE_TABLE).to_be_true();
		expect(curve_of(params::GLOBAL_VOLUME_POST_FX) == CurveKind::SQUARE).to_be_true();
		expect(curve_of(params::GLOBAL_DELAY_RATE) == CurveKind::EXPONENTIAL).to_be_true();
		expect(chain_of(params::LOCAL_VOLUME) == ChainKind::MULTIPLY).to_be_true();
		expect(chain_of(params::LOCAL_PAN) == ChainKind::ADD).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(matrix_compiler_spec);
