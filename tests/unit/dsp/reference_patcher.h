/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The Patcher as it was before the matrix runtime replaced it, kept verbatim as the reference the matrix specs compare
// against: the same arithmetic, the same order, the same skipping. Its depth results live where the caller says: one
// array shared by every instance reproduces the Patcher's one global array; an array per instance gives each pass its
// own.

#pragma once

#include "definitions_cxx.hpp"
#include "modulation/params/param.h"
#include "synth/modulation/cable.h"
#include "synth/modulation/patch_routing.h"

#include <array>
#include <cstdint>
#include <span>

namespace deluge::synth::test {

/// What one patching call reads: the routing, each param's preset, each routed cable's depth.
struct ReferenceBlock {
	PatchRouting routing;
	std::array<int32_t, modulation::params::kNumParams> preset_values{};
	std::array<int32_t, kMaxCables> depths{};
	[[nodiscard]] int32_t preset_value(int32_t p) const { return preset_values[p]; }
};

class ReferencePatcher {
public:
	struct Config {
		int32_t first_param;
		int32_t first_non_volume_param;
		int32_t first_hybrid_param;
		int32_t first_exp_param;
		int32_t end_params;
		uint8_t globality;
	};

	ReferencePatcher(const Config& config, std::span<int32_t> source_values, std::span<int32_t> param_final_values,
	                 std::span<int32_t, kMaxCables> range_final_values)
	    : config_(config), source_values_(source_values), param_final_values_(param_final_values),
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
	std::span<int32_t> source_values_;
	std::span<int32_t> param_final_values_;
	std::span<int32_t, kMaxCables> range_final_values_;
};

} // namespace deluge::synth::test
