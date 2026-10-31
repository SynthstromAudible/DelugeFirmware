/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"
#include "synth/modulation/control_block.h"
#include <cstdint>
#include <span>

enum Globality {
	GLOBALITY_LOCAL = 0,
	GLOBALITY_GLOBAL = 1,
};

class Patcher {
public:
	struct Config {
		int32_t first_param;
		int32_t first_non_volume_param;
		int32_t first_hybrid_param;
		int32_t first_exp_param;
		int32_t end_params;
		uint8_t globality;
	};

	Patcher(const Config& config, std::span<int32_t> source_values, std::span<int32_t> param_final_values)
	    : config_(config), source_values_(source_values), param_final_values_(param_final_values) {};
	void perform_initial_patching(const ControlBlock& control_block);
	void perform_patching(uint32_t sources_changed, const ControlBlock& control_block);
	void recalculate_final_value_for_param_with_no_cables(int32_t param, const ControlBlock& control_block);

private:
	int32_t combine_cables_linear_for_range_param(const deluge::synth::RoutedDestination* destination,
	                                              const ControlBlock& control_block);
	int32_t combine_cables_linear(const deluge::synth::RoutedDestination* destination, int32_t param,
	                              const ControlBlock& control_block);
	int32_t combine_cables_exp(const deluge::synth::RoutedDestination* destination, int32_t param,
	                           const ControlBlock& control_block);
	static int32_t cable_to_linear_param_without_range_adjustment(int32_t running_total, int32_t source_value,
	                                                              int32_t cable_strength);
	static int32_t cable_to_linear_param(int32_t running_total, const deluge::synth::RoutedCable& cable,
	                                     int32_t source_value, int32_t cable_strength);
	static int32_t cable_to_exp_param_without_range_adjustment(int32_t running_total, int32_t source_value,
	                                                           int32_t cable_strength);
	static int32_t cable_to_exp_param(int32_t running_total, const deluge::synth::RoutedCable& cable,
	                                  int32_t source_value, int32_t cable_strength);

	Config config_;
	std::span<int32_t> source_values_{};
	std::span<int32_t> param_final_values_{};
};
