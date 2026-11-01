/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The matrix evaluator's uncabled params against the frozen reference patcher: a param with no cables takes its preset
// alone, as it always did. The cabled patching is matrix_rules_spec's, against the rules model.

#include "alloc_seams.h"
#include "matrix_cases.h"
#include "patch_cases.h"

#include "synth/matrix/compiler.h"
#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_fixed.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

using namespace deluge::synth;
using namespace deluge::synth::matrix;
using namespace deluge::synth::test;

// clang-format off
describe matrix_evaluator_spec("the matrix evaluator", ${
	it("gives each uncabled param its preset's value alone", _{
		auto compiled = std::make_unique<CompiledCase>();
		const PatchCase c = random_case(7);
		compile_case(c, *compiled);
		std::array<int32_t, kNumPatchSources> sources = c.sources[0];
		std::array<int32_t, params::FIRST_GLOBAL> finals{};
		std::array<int32_t, params::FIRST_GLOBAL> reference_finals{};
		std::array<int32_t, kMaxCables> ranges{};
		ReferencePatcher reference{kVoiceConfig, sources, reference_finals, ranges};
		MatrixBlock block{};
		block.preset_values = c.presets;
		const ScopePass pass{.scope = 0, .sources = sources, .final_values = finals};
		int32_t differing = 0;
		for (int32_t p = 0; p < params::FIRST_GLOBAL; ++p) {
			Evaluator<MatrixFixed>::run_uncabled_param(pass, block, p);
			reference.recalculate_final_value_for_param_with_no_cables(p, compiled->block);
			differing += finals[p] != reference_finals[p] ? 1 : 0;
		}
		expect(differing).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(matrix_evaluator_spec);
