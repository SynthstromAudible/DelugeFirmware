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

#include "modulation/patch/patcher.h"
#include "definitions_cxx.hpp"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"
#include "util/misc.h"

namespace params = deluge::modulation::params;

Patcher::Patcher(const PatchableInfo* newInfo) : patchableInfo(newInfo) {
}

inline int32_t* Patcher::getParamFinalValuesPointer() {
	return (int32_t*)((uint32_t)this + patchableInfo->paramFinalValuesOffset);
}

inline int32_t Patcher::getSourceValue(PatchSource s) {
	return ((int32_t*)((uint32_t)this + patchableInfo->sourceValuesOffset))[util::to_underlying(s)];
}

// If NULL Destination, that means no cables - just the preset value
void Patcher::recalculateFinalValueForParamWithNoCables(int32_t p, Sound* sound,
                                                        ParamManagerForTimeline* paramManager) {

	int32_t cableCombination = (p < patchableInfo->firstHybridParam) ? combineCablesLinear(NULL, p, sound, paramManager)
	                                                                 : combineCablesExp(NULL, p, sound, paramManager);

	int32_t finalValue;
	int32_t paramNeutralValue = paramNeutralValues[p];

	if (p < patchableInfo->firstHybridParam) {
		if (p < patchableInfo->firstNonVolumeParam) {
			finalValue = getFinalParameterValueVolume(paramNeutralValue, cableCombination);
		}
		else {
			finalValue = getFinalParameterValueLinear(paramNeutralValue, cableCombination);
		}
	}
	else {
		if (p < patchableInfo->firstExpParam) {
			finalValue = getFinalParameterValueHybrid(paramNeutralValue, cableCombination); // Hybrid - add
		}
		else {
			finalValue = getFinalParameterValueExpWithDumbEnvelopeHack(paramNeutralValue, cableCombination, p);
		}
	}

	getParamFinalValuesPointer()[p] = finalValue;
}

int32_t rangeFinalValues[kMaxNumPatchCables]; // TODO: storing these in permanent memory per voice could save a tiny bit
                                              // of time... actually so minor though, maybe not worth it.

// You may as well check sourcesChanged before calling this.
void Patcher::performPatching(uint32_t sourcesChanged, Sound* sound, ParamManagerForTimeline* paramManager) {

	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();
	int32_t globality = patchableInfo->globality;
	Destination* destination = patchCableSet->destinations[globality];
	if (!destination) {
		return;
	}

	sourcesChanged &= patchCableSet->sourcesPatchedToAnything[globality];
	if (!sourcesChanged) {
		return;
	}

	// First, "range" Destinations
	int32_t i = 0;
	for (; destination->destinationParamDescriptor.data < (uint32_t)0xFFFFFF00; destination++) {
		if (!(destination->sources & sourcesChanged)) {
			continue; // And don't increment i.
		}

		int32_t cablesCombination = combineCablesLinearForRangeParam(destination, paramManager);

		rangeFinalValues[i] = getFinalParameterValueLinear(536870912, cablesCombination);
		i++;
	}

	int32_t* paramFinalValues = getParamFinalValuesPointer();

	uint8_t params[std::max<int32_t>(params::FIRST_GLOBAL, params::kNumParams - params::FIRST_GLOBAL) + 1];
	int32_t cableCombinations[std::max<int32_t>(params::FIRST_GLOBAL, params::kNumParams - params::FIRST_GLOBAL)];
	int32_t numParamsPatched = 0;

	// Go through regular Destinations going directly to a param
	{

		// Now normal, actual params/destinations
		uint32_t firstHybridParam = patchableInfo->firstHybridParam | 0xFFFFFF00;
		for (; destination->destinationParamDescriptor.data < firstHybridParam; destination++) {
			if (!(destination->sources & sourcesChanged)) {
				continue;
			}

			int32_t p = destination->destinationParamDescriptor.getJustTheParam();
			cableCombinations[numParamsPatched] = combineCablesLinear(destination, p, sound, paramManager);
			params[numParamsPatched] = p;
			numParamsPatched++;
		}

		for (; destination->sources; destination++) {
			if (!(destination->sources & sourcesChanged)) {
				continue;
			}

			int32_t p = destination->destinationParamDescriptor.getJustTheParam();
			cableCombinations[numParamsPatched] = combineCablesExp(destination, p, sound, paramManager);
			params[numParamsPatched] = p;
			numParamsPatched++;
		}
	}

	params[numParamsPatched] = 255; // To stop the below code from going past the end of the list we just created

	// Now, turn those cableCombinations into paramFinalValues. Splitting these up like this caused a massive speed-up
	// on all presets tested, somewhat surprisingly!
	{
		int32_t i = 0;

		// Volume params
		int32_t firstNonVolumeParam = patchableInfo->firstNonVolumeParam;
		for (; params[i] < firstNonVolumeParam; i++) {
			int32_t p = params[i];
			paramFinalValues[p] = getFinalParameterValueVolume(paramNeutralValues[p], cableCombinations[i]);
		}

		// Linear params
		int32_t firstHybridParam = patchableInfo->firstHybridParam;
		for (; params[i] < firstHybridParam; i++) {
			int32_t p = params[i];
			paramFinalValues[p] = getFinalParameterValueLinear(paramNeutralValues[p], cableCombinations[i]);
		}

		// Hybrid params
		int32_t firstExpParam = patchableInfo->firstExpParam;
		for (; params[i] < firstExpParam; i++) {
			int32_t p = params[i];
			paramFinalValues[p] = getFinalParameterValueHybrid(paramNeutralValues[p], cableCombinations[i]);
		}

		// Exp params
		for (; i < numParamsPatched; i++) {
			int32_t p = params[i];
			paramFinalValues[p] =
			    getFinalParameterValueExpWithDumbEnvelopeHack(paramNeutralValues[p], cableCombinations[i], p);
		}
	}
}

inline void Patcher::applyRangeAdjustment(int32_t* patchedValue, PatchCable* patchCable) {
	int32_t small = multiply_32x32_rshift32(*patchedValue, *patchCable->rangeAdjustmentPointer);
	*patchedValue = signed_saturate<32 - 5>(small) << 3; // Not sure if these limits are as wide as they could be...
}

// Declaring these next 4 as inline made no performance difference.
inline void Patcher::cableToLinearParamWithoutRangeAdjustment(int32_t sourceValue, int32_t cableStrength,
                                                              int32_t* runningTotalCombination) {
	int32_t scaledSource = multiply_32x32_rshift32(sourceValue, cableStrength);
	int32_t madePositive =
	    (scaledSource + 536870912); // 0 to 1073741824; 536870912 counts as "1" for next multiplication
	int32_t preLimits = multiply_32x32_rshift32(*runningTotalCombination, madePositive);
	*runningTotalCombination = lshiftAndSaturate<3>(preLimits);
}

inline void Patcher::cableToLinearParam(int32_t sourceValue, int32_t cableStrength, int32_t* runningTotalCombination,
                                        PatchCable* patchCable) {
	int32_t scaledSource = multiply_32x32_rshift32(sourceValue, cableStrength);
	applyRangeAdjustment(&scaledSource, patchCable);
	int32_t madePositive =
	    (scaledSource + 536870912); // 0 to 1073741824; 536870912 counts as "1" for next multiplication
	int32_t preLimits = multiply_32x32_rshift32(*runningTotalCombination, madePositive);
	*runningTotalCombination = lshiftAndSaturate<3>(preLimits);
}

inline void Patcher::cableToExpParamWithoutRangeAdjustment(int32_t sourceValue, int32_t cableStrength,
                                                           int32_t* runningTotalCombination) {
	int32_t scaledSource = multiply_32x32_rshift32(sourceValue, cableStrength);
	*runningTotalCombination += scaledSource;
}

inline void Patcher::cableToExpParam(int32_t sourceValue, int32_t cableStrength, int32_t* runningTotalCombination,
                                     PatchCable* patchCable) {
	int32_t scaledSource = multiply_32x32_rshift32(sourceValue, cableStrength);
	applyRangeAdjustment(&scaledSource, patchCable);
	*runningTotalCombination += scaledSource;
}

inline int32_t Patcher::combineCablesLinearForRangeParam(Destination const* destination, ParamManager* paramManager) {
	int32_t runningTotalCombination = 536870912; // 536870912 means "1". runningTotalCombination will not be allowed to
	                                             // get bigger than 2147483647, which means "4".

	PatchCableSet* patch_cable_set = paramManager->getPatchCableSet();

	// For each patch cable affecting the range of this cable (got that?)
	for (int32_t c = destination->firstCable; c < destination->endCable; c++) {
		PatchCable* patch_cable = &patch_cable_set->patchCables[c];
		PatchSource s = patch_cable->from;
		int32_t source_value = getSourceValue(s);

		// Special exception: If we're patching aftertouch to range. Normally, unlike other sources, aftertouch goes
		// from 0 to 2147483647. This is because we want it to have no effect at its negative extreme, which isn't
		// normally what we want. However, when patched to range, we do want this again, so "transpose" it here.
		if (s == PatchSource::AFTERTOUCH) {
			source_value = (source_value - 1073741824) << 1;
		}
		else {
			source_value = patch_cable->toPolarity(source_value);
		}

		int32_t cableStrength = patch_cable->param.getCurrentValue();
		cableToLinearParamWithoutRangeAdjustment(source_value, cableStrength, &runningTotalCombination);
	}

	return runningTotalCombination - 536870912;
	// return value is ideally between -536870912 and 536870912, but may get up to 1610612736 if the result above was
	// "4" because of multiple patch cables being multiplied together
}

// Linear param - combine all cables by multiplying their values (values centred around 1). Inputs effectively range
// from "0" to "2". Output (product) clips off at "4". Call this if (p < getFirstHybridParam()) - "Pan" sits at the end
// of the linear params and is the exception to the rule - it doesn't want this multiplying treatment Having this inline
// makes huge ~40% performance difference to performInitialPatching, I think because in that case it knows there are no
// cables.
inline int32_t Patcher::combineCablesLinear(Destination const* destination, uint32_t p, Sound* sound,
                                            ParamManager* paramManager) {
	int32_t runningTotalCombination = 536870912; // 536870912 means "1". runningTotalCombination will not be allowed to
	                                             // get bigger than 2147483647, which means "4".

	PatchCableSet* patch_cable_set = paramManager->getPatchCableSet();

	// Do the "preset value" (which we treat like a "cable" here)
	cableToLinearParamWithoutRangeAdjustment(sound->getSmoothedPatchedParamValue(p, paramManager), paramRanges[p],
	                                         &runningTotalCombination);

	if (destination) {
		// For each patch cable affecting this parameter
		for (int32_t cable = destination->firstCable; cable < destination->endCable; cable++) {
			PatchCable* patch_cable = &patch_cable_set->patchCables[cable];
			PatchSource s = patch_cable->from;
			int32_t source_value = getSourceValue(s);
			source_value = patch_cable->toPolarity(source_value);
			int32_t cable_strength = patch_cable->param.getCurrentValue();
			cableToLinearParam(source_value, cable_strength, &runningTotalCombination, patch_cable);
		}
	}

	return runningTotalCombination - 536870912;
	// return value is ideally between -536870912 and 536870912, but may get up to 1610612736 if the result above was
	// "4" because of multiple patch cables being multiplied together
}

// Exp param - combine all cables by adding their values (centred around 0)
// Call this if (p >= getFirstHybridParam())
// Having this inline makes huge ~40% performance difference to performInitialPatching, I think because in that case it
// knows there are no cables.
inline int32_t Patcher::combineCablesExp(Destination const* destination, uint32_t p, Sound* sound,
                                         ParamManager* paramManager) {

	int32_t runningTotalCombination = 0;

	PatchCableSet* patch_cable_set = paramManager->getPatchCableSet();

	if (destination) {
		// For each patch cable affecting this parameter
		for (int32_t cable = destination->firstCable; cable < destination->endCable; cable++) {
			PatchCable* patch_cable = &patch_cable_set->patchCables[cable];
			int32_t source_value = getSourceValue(patch_cable->from);
			source_value = patch_cable->toPolarity(source_value);
			int32_t cable_strength = patch_cable_set->getModifiedPatchCableAmount(cable, p);
			cableToExpParam(source_value, cable_strength, &runningTotalCombination, patch_cable);
		}

		// Hack for wave index params - make the patching (but not the preset value) stretch twice as far, to allow the
		// opposite end to be reached even if the user's preset value is all the way to one end. These params are
		// "hybrid" ones, and probably in a perfect world I would have made the other ones behave the same way. But I
		// can't break users' songs.
		if (p == params::LOCAL_OSC_A_WAVE_INDEX || p == params::LOCAL_OSC_B_WAVE_INDEX) {
			runningTotalCombination <<= 1;
		}
	}

	// Do the "preset value" (which we treat like a "cable" here)
	cableToExpParamWithoutRangeAdjustment(sound->getSmoothedPatchedParamValue(p, paramManager), paramRanges[p],
	                                      &runningTotalCombination);

	return runningTotalCombination;
}

// NOTE: parameter preset values can't be bigger than 536870912, otherwise overflowing will occur

void Patcher::performInitialPatching(Sound* sound, ParamManager* paramManager) {

	/*
	uint16_t startTime = *TCNT[TIMER_SYSTEM_FAST];

	for (int32_t i = 0; i < 10; i++) {
*/
	int32_t* paramFinalValues = getParamFinalValuesPointer();

	// In this function, we are sneaky and write the "cable combination" working value in to paramFinalValues, before
	// going back over that array with the final step. This saves memory. Didn't seem to have a real effect on speed.

	{
		// The contents of this bit could be optimized further by doing even more passes, splitting up e.g.
		// combineCablesLinear into iterations, which sometimes could be done on all params at once. Or, could just
		// pre-compute and copy "final values" for where there's no patching
		int32_t p = patchableInfo->firstParam;

		// First for the params whose cables get multiplied, we go do them all as if they have no patching, and then for
		// the few which do, we overwrite those values
		int32_t firstHybridParam = patchableInfo->firstHybridParam;
		for (; p < firstHybridParam; p++) {
			paramFinalValues[p] = combineCablesLinear(NULL, p, sound, paramManager);
		}

		// And now we do the same for params whose cables get added
		int32_t endParams = patchableInfo->endParams;
		for (; p < endParams; p++) {
			paramFinalValues[p] = combineCablesExp(NULL, p, sound, paramManager);
		}

		Destination* destination = paramManager->getPatchCableSet()->destinations[patchableInfo->globality];
		if (destination) {

			// First, "range" Destinations
			int32_t i = 0;
			for (; destination->destinationParamDescriptor.data < (uint32_t)0xFFFFFF00; destination++) {
				int32_t cablesCombination = combineCablesLinearForRangeParam(destination, paramManager);

				rangeFinalValues[i] = getFinalParameterValueLinear(536870912, cablesCombination);
				i++;
			}

			// Now, regular Destinations going directly to a param
			uint32_t firstHybridParamAsDescriptor = firstHybridParam | 0xFFFFFF00;
			for (; destination->destinationParamDescriptor.data < firstHybridParamAsDescriptor; destination++) {
				int32_t p = destination->destinationParamDescriptor.getJustTheParam();
				paramFinalValues[p] = combineCablesLinear(destination, p, sound, paramManager);
			}
			for (; destination->sources; destination++) {
				int32_t p = destination->destinationParamDescriptor.getJustTheParam();
				paramFinalValues[p] = combineCablesExp(destination, p, sound, paramManager);
			}
		}
	}

	// And now convert those "cable combinations" into "final values".
	{
		int32_t p = patchableInfo->firstParam;

		// Volume params
		int32_t firstNonVolumeParam = patchableInfo->firstNonVolumeParam;
		for (; p < firstNonVolumeParam; p++) {
			paramFinalValues[p] = getFinalParameterValueVolume(paramNeutralValues[p], paramFinalValues[p]);
		}

		// Linear params
		int32_t firstHybridParam = patchableInfo->firstHybridParam;
		for (; p < firstHybridParam; p++) {
			paramFinalValues[p] = getFinalParameterValueLinear(paramNeutralValues[p], paramFinalValues[p]);
		}

		// Hybrid params
		int32_t firstExpParam = patchableInfo->firstExpParam;
		for (; p < firstExpParam; p++) {
			paramFinalValues[p] = getFinalParameterValueHybrid(paramNeutralValues[p], paramFinalValues[p]);
		}

		// Exp params
		int32_t endParams = patchableInfo->endParams;
		for (; p < endParams; p++) {
			paramFinalValues[p] =
			    getFinalParameterValueExpWithDumbEnvelopeHack(paramNeutralValues[p], paramFinalValues[p], p);
		}
	}
	/*
	uint16_t endTime = *TCNT[TIMER_SYSTEM_FAST];
	uint16_t duration = endTime - startTime;

	uint32_t timePassedUSA = timerCountToUS(duration);

	D_PRINT("duration, uSec: ");
	D_PRINTLN(timePassedUSA);
*/
}
