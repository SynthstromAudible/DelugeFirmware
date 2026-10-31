/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The matrix compiler against today's routing: every destination keeps its cables, in today's chain order, with the
// conversion, depth, range link and depth shape today's patching gives each.

#include "alloc_seams.h"
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

	it("gives each param a curve and a chain by its scope's regions", _{
		expect(curve_of(params::LOCAL_VOLUME) == CurveKind::VOLUME).to_be_true();
		expect(curve_of(params::LOCAL_PAN) == CurveKind::HYBRID).to_be_true();
		expect(curve_of(params::LOCAL_LPF_FREQ) == CurveKind::EXP).to_be_true();
		expect(curve_of(params::LOCAL_ENV_0_ATTACK) == CurveKind::EXP_ATTACK).to_be_true();
		expect(curve_of(params::LOCAL_ENV_0_RELEASE) == CurveKind::RELEASE_RATE).to_be_true();
		expect(curve_of(params::GLOBAL_VOLUME_POST_FX) == CurveKind::VOLUME).to_be_true();
		expect(curve_of(params::GLOBAL_DELAY_RATE) == CurveKind::EXP).to_be_true();
		expect(chain_of(params::LOCAL_VOLUME) == ChainKind::MULTIPLY).to_be_true();
		expect(chain_of(params::LOCAL_PAN) == ChainKind::ADD).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(matrix_compiler_spec);
