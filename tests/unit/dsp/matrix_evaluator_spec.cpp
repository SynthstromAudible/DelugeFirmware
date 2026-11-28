/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The matrix evaluator's uncabled params against the frozen reference patcher: a param with no cables takes its preset
// alone, as it always did. The cabled patching is matrix_rules_spec's, against the rules model.

#include "matrix_cases.h"
#include "param_regions.h"
#include "patch_cases.h"

#include "synth/matrix/compiler.h"
#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_fixed.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::synth;
using namespace deluge::synth::matrix;
using namespace deluge::synth::test;

// clang-format off
describe matrix_evaluator_spec_body("the matrix evaluator", ${
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
		block.preset_values = deluge::synth::test::raw_matrix::typed_words<deluge::dsp::Q<31>>(c.presets);
		const ScopePass pass{.scope = 0, .sources = sources, .final_values = finals};
		int32_t differing = 0;
		for (int32_t p = 0; p < regions::kVoiceRegionsEnd; ++p) {
			Evaluator<MatrixFixed>::run_uncabled_param(pass, block, p);
			reference.recalculate_final_value_for_param_with_no_cables(p, compiled->block);
			differing += finals[p] != reference_finals[p] ? 1 : 0;
		}
		expect(differing).to_equal(0);
	});

	it("gives each of the DX7's uncabled params the value its oscillator A counterpart has from the same preset", _{
		const PatchCase c = random_case(11);
		std::array<int32_t, kNumPatchSources> sources = c.sources[0];
		std::array<int32_t, params::FIRST_GLOBAL> finals{};
		const ScopePass pass{.scope = 0, .sources = sources, .final_values = finals};
		int32_t differing = 0;
		for (const int32_t dx7 : {params::LOCAL_DX7_LEVEL, params::LOCAL_DX7_AMP_MOD, params::LOCAL_DX7_PITCH_ADJUST}) {
			const int32_t osc = regions::region_param(dx7);
			for (const int32_t preset : c.presets) {
				std::array<int32_t, params::kNumParams> presets{};
				presets[dx7] = preset;
				presets[osc] = preset;
				MatrixBlock block{};
				block.preset_values = deluge::synth::test::raw_matrix::typed_words<deluge::dsp::Q<31>>(presets);
				Evaluator<MatrixFixed>::run_uncabled_param(pass, block, dx7);
				Evaluator<MatrixFixed>::run_uncabled_param(pass, block, osc);
				differing += finals[dx7] != finals[osc] ? 1 : 0;
			}
		}
		expect(differing).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(matrix_evaluator_spec, matrix_evaluator_spec_body)
