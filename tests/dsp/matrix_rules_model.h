/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The matrix rules model: the specs' oracle for the mod matrix's rules. It walks a routing as the Patcher did (depth
// destinations, then param destinations, cables in routing order) in plain scalar arithmetic; each field of Rules
// switches one rule from the Patcher's form to the uniform rules of the audible window (see the matrix-area spec).
// Its depth results live where the caller says.

#pragma once

#include "definitions_cxx.hpp"
#include "modulation/params/param.h"
#include "patch_cases.h"
#include "synth/modulation/cable.h"
#include "synth/modulation/patch_routing.h"

#include <array>
#include <cstdint>
#include <span>

namespace deluge::synth::test {

/// Which of the uniform rules apply; a rule left off keeps the Patcher's form.
struct Rules {};

class RulesPatcher {
public:
	using Config = ReferencePatcher::Config;

	RulesPatcher(const Config& config, const Rules& rules, std::span<int32_t> source_values,
	             std::span<int32_t> param_final_values, std::span<int32_t, kMaxCables> range_final_values)
	    : config_(config), rules_(rules), source_values_(source_values), param_final_values_(param_final_values),
	      range_final_values_(range_final_values) {}
	void perform_initial_patching(const ReferenceBlock& control_block);
	void perform_patching(uint32_t sources_changed, const ReferenceBlock& control_block);
	void recalculate_final_value_for_param_with_no_cables(int32_t p, const ReferenceBlock& control_block);

private:
	[[nodiscard]] int32_t range_of_(const RoutedCable& cable) const {
		return cable.range_index < 0 ? 536870912 : range_final_values_[cable.range_index];
	}
	int32_t combine_cables_linear_for_range_param(const RoutedDestination* destination,
	                                              const ReferenceBlock& control_block);
	int32_t combine_cables_linear(const RoutedDestination* destination, int32_t p, const ReferenceBlock& control_block);
	int32_t combine_cables_exp(const RoutedDestination* destination, int32_t param,
	                           const ReferenceBlock& control_block);
	static int32_t cable_to_linear_param_without_range_adjustment(int32_t running_total, int32_t source_value,
	                                                              int32_t cable_strength);
	int32_t cable_to_linear_param(int32_t running_total, const RoutedCable& patch_cable, int32_t source_value,
	                              int32_t cable_strength);
	static int32_t cable_to_exp_param_without_range_adjustment(int32_t running_total, int32_t source_value,
	                                                           int32_t cable_strength);
	int32_t cable_to_exp_param(int32_t running_total, const RoutedCable& patch_cable, int32_t source_value,
	                           int32_t cable_strength);

	Config config_;
	Rules rules_;
	std::span<int32_t> source_values_;
	std::span<int32_t> param_final_values_;
	std::span<int32_t, kMaxCables> range_final_values_;
};

/// Each scope's final values after the initial patching and each of @p c's three blocks under @p rules, the sound's
/// pass then the voice's in each block. Without the per-pass rule, every pass shares one depth-result array.
inline ReferenceResult model_results(const PatchCase& c, const CompiledCase& compiled, const Rules& rules) {
	ReferenceResult out;
	std::array<int32_t, kNumPatchSources> voice_sources = c.sources[0];
	std::array<int32_t, kNumGlobalSources> sound_sources{};
	std::copy_n(c.sources[0].begin(), kNumGlobalSources, sound_sources.begin());
	std::array<int32_t, params::FIRST_GLOBAL> voice_finals{};
	std::array<int32_t, kNumGlobalParams> sound_finals{};
	std::array<int32_t, kMaxCables> shared_ranges{};
	RulesPatcher voice{kVoiceConfig, rules, voice_sources, voice_finals, shared_ranges};
	RulesPatcher sound{kSoundConfig, rules, sound_sources, sound_finals, shared_ranges};
	sound.perform_initial_patching(compiled.block);
	out.sound[0] = sound_finals;
	voice.perform_initial_patching(compiled.block);
	out.voice[0] = voice_finals;
	for (size_t b = 0; b < c.sources_changed.size(); ++b) {
		voice_sources = c.sources[b + 1];
		std::copy_n(c.sources[b + 1].begin(), kNumGlobalSources, sound_sources.begin());
		sound.perform_patching(c.sources_changed[b], compiled.block);
		out.sound[b + 1] = sound_finals;
		voice.perform_patching(c.sources_changed[b], compiled.block);
		out.voice[b + 1] = voice_finals;
	}
	return out;
}

} // namespace deluge::synth::test
