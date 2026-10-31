/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The matrix evaluator run over a patch case, in the shape the reference and the rules model give.

#pragma once

#include "patch_cases.h"

#include "synth/matrix/compiler.h"
#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_fixed.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace deluge::synth::test {

using namespace deluge::synth::matrix;

/// The evaluator's results for @p c, in the reference's shape.
inline ReferenceResult evaluator_results(const PatchCase& c, const CompiledCase& compiled) {
	std::vector<std::byte> storage(program_size(compiled.build) + alignof(MatrixProgram));
	std::byte* at = storage.data();
	while (reinterpret_cast<std::uintptr_t>(at) % alignof(MatrixProgram) != 0) {
		++at;
	}
	MatrixBlock block{};
	block.program = compile_program(compiled.build, {at, program_size(compiled.build)});
	block.preset_values = c.presets;
	block.depths = c.depths;

	ReferenceResult out;
	std::array<int32_t, kNumPatchSources> voice_sources = c.sources[0];
	std::array<int32_t, kNumGlobalSources> sound_sources{};
	std::copy_n(c.sources[0].begin(), kNumGlobalSources, sound_sources.begin());
	std::array<int32_t, params::FIRST_GLOBAL> voice_finals{};
	std::array<int32_t, kNumGlobalParams> sound_finals{};
	const ScopePass voice{.scope = 0, .sources = voice_sources, .final_values = voice_finals};
	const ScopePass sound{.scope = 1, .sources = sound_sources, .final_values = sound_finals};
	Evaluator<MatrixFixed>::run_initial(sound, block);
	out.sound[0] = sound_finals;
	Evaluator<MatrixFixed>::run_initial(voice, block);
	out.voice[0] = voice_finals;
	for (size_t b = 0; b < c.sources_changed.size(); ++b) {
		voice_sources = c.sources[b + 1];
		std::copy_n(c.sources[b + 1].begin(), kNumGlobalSources, sound_sources.begin());
		Evaluator<MatrixFixed>::run(sound, block, c.sources_changed[b]);
		out.sound[b + 1] = sound_finals;
		Evaluator<MatrixFixed>::run(voice, block, c.sources_changed[b]);
		out.voice[b + 1] = voice_finals;
	}
	return out;
}

} // namespace deluge::synth::test
