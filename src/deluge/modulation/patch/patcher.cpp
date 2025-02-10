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
#include <etl/map.h>
#include <utility>

namespace params = deluge::modulation::params;

// If NULL Destination, that means no cables - just the preset value
void Patcher::recalculateFinalValueForParamWithNoCables(int32_t p, Sound* sound,
                                                        ParamManagerForTimeline* paramManager) {

	int32_t cableCombination = (p < config.firstHybridParam) ? combineCablesLinear(nullptr, p, sound, paramManager)
	                                                         : combineCablesExp(nullptr, p, sound, paramManager);

	int32_t finalValue;
	int32_t paramNeutralValue = paramNeutralValues[p];

	if (p < config.firstHybridParam) {
		if (p < config.firstNonVolumeParam) {
			finalValue = getFinalParameterValueVolume(paramNeutralValue, cableCombination);
		}
		else {
			finalValue = getFinalParameterValueLinear(paramNeutralValue, cableCombination);
		}
	}
	else {
		if (p < config.firstExpParam) {
			finalValue = getFinalParameterValueHybrid(paramNeutralValue, cableCombination); // Hybrid - add
		}
		else {
			finalValue = getFinalParameterValueExpWithDumbEnvelopeHack(paramNeutralValue, cableCombination, p);
		}
	}

	param_final_values_[p - config.firstParam] = finalValue;
}

int32_t rangeFinalValues[kMaxNumPatchCables]; // TODO: storing these in permanent memory per voice could save a tiny bit
                                              // of time... actually so minor though, maybe not worth it.

// You may as well check sourcesChanged before calling this.
void Patcher::performPatching(uint32_t sourcesChanged, Sound* sound, ParamManagerForTimeline* paramManager) {

	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();
	int32_t globality = config.globality;
	Destination* destination = patchCableSet->destinations[globality];
	if (destination == nullptr) {
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

	// param_id, cable_combo
	etl::map<int32_t, int32_t, params::kNumParams> cable_combos;

	// Go through regular Destinations going directly to a param
	{

		// Now normal, actual params/destinations
		for (; destination->destinationParamDescriptor.data < (config.firstHybridParam | 0xFFFFFF00); destination++) {
			if (!(destination->sources & sourcesChanged)) {
				continue;
			}

			int32_t p = destination->destinationParamDescriptor.getJustTheParam();
			cable_combos[p] = combineCablesLinear(destination, p, sound, paramManager);
		}

		for (; destination->sources; destination++) {
			if (!(destination->sources & sourcesChanged)) {
				continue;
			}

			int32_t p = destination->destinationParamDescriptor.getJustTheParam();
			cable_combos[p] = combineCablesExp(destination, p, sound, paramManager);
		}
	}

	// Now, turn those cableCombinations into paramFinalValues. Splitting these up like this caused a massive speed-up
	// on all presets tested, somewhat surprisingly!
	for (auto& [param, cable_combo] : cable_combos) {
		// Volume params
		if (param < config.firstNonVolumeParam) {
			param_final_values_[param - config.firstParam] =
			    getFinalParameterValueVolume(paramNeutralValues[param], cable_combo);
		}

		// Linear params
		else if (param < config.firstHybridParam) {
			param_final_values_[param - config.firstParam] =
			    getFinalParameterValueLinear(paramNeutralValues[param], cable_combo);
		}

		// Hybrid params
		else if (param < config.firstExpParam) {
			param_final_values_[param - config.firstParam] =
			    getFinalParameterValueHybrid(paramNeutralValues[param], cable_combo);
		}

		// Exp params
		else {
			param_final_values_[param - config.firstParam] =
			    getFinalParameterValueExpWithDumbEnvelopeHack(paramNeutralValues[param], cable_combo, param);
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

	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();

	// For each patch cable affecting the range of this cable (got that?)
	for (int32_t c = destination->firstCable; c < destination->endCable; c++) {
		PatchCable* patchCable = &patchCableSet->patchCables[c];
		PatchSource s = patchCable->from;
		int32_t sourceValue = source_values_[std::to_underlying(s)];

		// Special exception: If we're patching aftertouch to range. Normally, unlike other sources, aftertouch goes
		// from 0 to 2147483647. This is because we want it to have no effect at its negative extreme, which isn't
		// normally what we want. However, when patched to range, we do want this again, so "transpose" it here.
		if (s == PatchSource::AFTERTOUCH) {
			sourceValue = (sourceValue - 1073741824) << 1;
		}

		int32_t cableStrength = patchCable->param.getCurrentValue();
		cableToLinearParamWithoutRangeAdjustment(sourceValue, cableStrength, &runningTotalCombination);
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

	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();

	// Do the "preset value" (which we treat like a "cable" here)
	cableToLinearParamWithoutRangeAdjustment(sound->getSmoothedPatchedParamValue(p, paramManager), paramRanges[p],
	                                         &runningTotalCombination);

	if (destination) {
		// For each patch cable affecting this parameter
		for (int32_t c = destination->firstCable; c < destination->endCable; c++) {
			PatchCable* patchCable = &patchCableSet->patchCables[c];
			PatchSource s = patchCable->from;
			int32_t sourceValue = source_values_[std::to_underlying(s)];

			int32_t cableStrength = patchCable->param.getCurrentValue();
			cableToLinearParam(sourceValue, cableStrength, &runningTotalCombination, patchCable);
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

	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();

	if (destination) {
		// For each patch cable affecting this parameter
		for (int32_t c = destination->firstCable; c < destination->endCable; c++) {
			PatchCable* patchCable = &patchCableSet->patchCables[c];
			int32_t sourceValue = source_values_[std::to_underlying(patchCable->from)];

			int32_t cableStrength = patchCableSet->getModifiedPatchCableAmount(c, p);
			cableToExpParam(sourceValue, cableStrength, &runningTotalCombination, patchCable);
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
	// In this function, we are sneaky and write the "cable combination" working value in to paramFinalValues, before
	// going back over that array with the final step. This saves memory. Didn't seem to have a real effect on speed.

	{
		// The contents of this bit could be optimized further by doing even more passes, splitting up e.g.
		// combineCablesLinear into iterations, which sometimes could be done on all params at once. Or, could just
		// pre-compute and copy "final values" for where there's no patching

		// First for the params whose cables get multiplied, we go do them all as if they have no patching, and then for
		// the few which do, we overwrite those values
		for (int32_t p = config.firstParam; p < config.firstHybridParam; p++) {
			param_final_values_[p - config.firstParam] = combineCablesLinear(nullptr, p, sound, paramManager);
		}

		// And now we do the same for params whose cables get added
		for (int32_t p = config.firstHybridParam; p < config.endParams; p++) {
			param_final_values_[p - config.firstParam] = combineCablesExp(nullptr, p, sound, paramManager);
		}

		Destination* destination = paramManager->getPatchCableSet()->destinations[config.globality];
		if (destination != nullptr) {

			// First, "range" Destinations
			for (int32_t i = 0; destination->destinationParamDescriptor.data < (uint32_t)0xFFFFFF00;
			     destination++, i++) {
				int32_t cablesCombination = combineCablesLinearForRangeParam(destination, paramManager);

				rangeFinalValues[i] = getFinalParameterValueLinear(536870912, cablesCombination);
			}

			// Now, regular Destinations going directly to a param
			uint32_t firstHybridParamAsDescriptor = config.firstHybridParam | 0xFFFFFF00;
			for (; destination->destinationParamDescriptor.data < firstHybridParamAsDescriptor; destination++) {
				int32_t p = destination->destinationParamDescriptor.getJustTheParam();
				param_final_values_[p - config.firstParam] = combineCablesLinear(destination, p, sound, paramManager);
			}
			for (; destination->sources; destination++) {
				int32_t p = destination->destinationParamDescriptor.getJustTheParam();
				param_final_values_[p - config.firstParam] = combineCablesExp(destination, p, sound, paramManager);
			}
		}
	}

	// And now convert those "cable combinations" into "final values".
	{
		// Volume params
		for (int32_t p = config.firstParam; p < config.firstNonVolumeParam; p++) {
			param_final_values_[p - config.firstParam] =
			    getFinalParameterValueVolume(paramNeutralValues[p], param_final_values_[p - config.firstParam]);
		}

		// Linear params
		for (int32_t p = config.firstNonVolumeParam; p < config.firstHybridParam; p++) {
			param_final_values_[p - config.firstParam] =
			    getFinalParameterValueLinear(paramNeutralValues[p], param_final_values_[p - config.firstParam]);
		}

		// Hybrid params
		for (int32_t p = config.firstHybridParam; p < config.firstExpParam; p++) {
			param_final_values_[p - config.firstParam] =
			    getFinalParameterValueHybrid(paramNeutralValues[p], param_final_values_[p - config.firstParam]);
		}

		// Exp params
		for (int32_t p = config.firstExpParam; p < config.endParams; p++) {
			param_final_values_[p - config.firstParam] = getFinalParameterValueExpWithDumbEnvelopeHack(
			    paramNeutralValues[p], param_final_values_[p - config.firstParam], p);
		}
	}
}
