/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The matrix evaluator against the rules model (matrix_rules_model.h): the same final values on random cases, for the
// rules landed so far; and one hand case per rule.

#include "alloc_seams.h"
#include "matrix_cases.h"
#include "matrix_rules_model.h"
#include "patch_cases.h"

#include <cppspec.hpp>
#include <cstdint>
#include <memory>

using namespace deluge::synth;
using namespace deluge::synth::test;

namespace {

/// The rules the evaluator follows.
constexpr Rules kLanded{.pass_depth = true};

int32_t differing_cases(uint32_t seeds) {
	int32_t differing = 0;
	for (uint32_t seed = 1; seed <= seeds; ++seed) {
		const PatchCase c = random_case(seed);
		auto compiled = std::make_unique<CompiledCase>();
		compile_case(c, *compiled);
		const ReferenceResult model = model_results(c, *compiled, kLanded);
		const ReferenceResult evaluated = evaluator_results(c, *compiled);
		differing += (model.voice != evaluated.voice || model.sound != evaluated.sound) ? 1 : 0;
	}
	return differing;
}

} // namespace

// clang-format off
describe matrix_rules_spec("the matrix rules", ${
	it("gives the rules model's final values on random cases", _{
		expect(differing_cases(2000)).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(matrix_rules_spec);
