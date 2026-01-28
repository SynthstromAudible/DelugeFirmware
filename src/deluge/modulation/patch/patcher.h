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
#include <cstdint>
#include <span>

struct CableGroup;

class Sound;
class ParamManagerForTimeline;
class PatchCableSet;
struct Destination;
class ParamManager;
class PatchCable;

enum Globality {
	GLOBALITY_LOCAL = 0,
	GLOBALITY_GLOBAL = 1,
};

class Patcher {
public:
	struct Config {
		uint8_t firstParam;
		uint8_t firstNonVolumeParam;
		uint8_t firstHybridParam;
		uint8_t firstZoneParam; // Zone params: pure modulation pass-through
		uint8_t firstExpParam;
		uint8_t endParams;
		uint8_t globality;
	};

	Patcher(const Config& config, std::span<int32_t> source_values, std::span<int32_t> param_final_values)
	    : config(config), source_values_(source_values), param_final_values_(param_final_values) {};
	void performInitialPatching(Sound& sound, ParamManager& param_manager);
	void performPatching(uint32_t sources_changed, Sound& sound, ParamManagerForTimeline& param_manager);
	void recalculateFinalValueForParamWithNoCables(int32_t param, Sound& sound, ParamManagerForTimeline& param_manager);

private:
	int32_t combineCablesLinearForRangeParam(Destination const* destination, ParamManager& param_manager);
	int32_t combineCablesLinear(Destination const* destination, uint32_t param, Sound& sound,
	                            ParamManager& param_manager);
	int32_t combineCablesExp(Destination const* destination, uint32_t param, Sound& sound, ParamManager& param_manager);
	static int32_t cableToLinearParamWithoutRangeAdjustment(int32_t running_total, int32_t source_value,
	                                                        int32_t cable_strength);
	static int32_t cableToLinearParam(int32_t running_total, const PatchCable& cable, int32_t source_value,
	                                  int32_t cable_strength);
	static int32_t cableToExpParamWithoutRangeAdjustment(int32_t running_total, int32_t source_value,
	                                                     int32_t cable_strength);
	static int32_t cableToExpParam(int32_t running_total, const PatchCable& cable, int32_t source_value,
	                               int32_t cable_strength);

	const Config& config;
	std::span<int32_t> source_values_;
	std::span<int32_t> param_final_values_;
};
