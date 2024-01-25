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

struct PatchableInfo {
	int32_t paramFinalValuesOffset;
	int32_t sourceValuesOffset;
	uint8_t firstParam;
	uint8_t firstNonVolumeParam;
	uint8_t firstHybridParam;
	uint8_t firstExpParam;
	uint8_t endParams;
	uint8_t globality;
};

class Patcher {
public:
	Patcher(const PatchableInfo* newInfo);
	void performInitialPatching(Sound* sound, ParamManager* paramManager);
	void performPatching(uint32_t sourcesChanged, Sound* sound, ParamManagerForTimeline* paramManager);
	void recalculateFinalValueForParamWithNoCables(int32_t p, Sound* sound, ParamManagerForTimeline* paramManager);

private:
	void applyRangeAdjustment(int32_t* patchedValue, PatchCable* patchCable);
	int32_t combineCablesLinearForRangeParam(Destination const* destination, ParamManager* paramManager);
	int32_t combineCablesLinear(Destination const* destination, uint32_t p, Sound* sound, ParamManager* paramManager);
	int32_t combineCablesExp(Destination const* destination, uint32_t p, Sound* sound, ParamManager* paramManager);
	void cableToLinearParamWithoutRangeAdjustment(int32_t sourceValue, int32_t cableStrength,
	                                              int32_t* runningTotalCombination);
	void cableToLinearParam(int32_t sourceValue, int32_t cableStrength, int32_t* runningTotalCombination,
	                        PatchCable* patchCable);
	void cableToExpParamWithoutRangeAdjustment(int32_t sourceValue, int32_t cableStrength,
	                                           int32_t* runningTotalCombination);
	void cableToExpParam(int32_t sourceValue, int32_t cableStrength, int32_t* runningTotalCombination,
	                     PatchCable* patchCable);
	int32_t* getParamFinalValuesPointer();
	int32_t getSourceValue(PatchSource s);

	const PatchableInfo* const patchableInfo;
};
