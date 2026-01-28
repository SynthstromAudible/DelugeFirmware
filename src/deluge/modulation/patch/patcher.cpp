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
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"
#include <etl/vector.h>
#include <utility>

namespace params = deluge::modulation::params;

// If NULL Destination, that means no cables - just the preset value
void Patcher::recalculateFinalValueForParamWithNoCables(int32_t p, Sound& sound,
                                                        ParamManagerForTimeline& param_manager) {

	int32_t cable_combination = (p < config.firstHybridParam) ? combineCablesLinear(nullptr, p, sound, param_manager)
	                                                          : combineCablesExp(nullptr, p, sound, param_manager);

	int32_t final_value = 0;
	int32_t param_neutral_value = paramNeutralValues[p];

	if (p < config.firstHybridParam) {
		if (p < config.firstNonVolumeParam) {
			final_value = getFinalParameterValueVolume(param_neutral_value, cable_combination);
		}
		else {
			final_value = getFinalParameterValueLinear(param_neutral_value, cable_combination);
		}
	}
	else if (p < config.firstZoneParam) {
		// Hybrid params
		final_value = getFinalParameterValueHybrid(param_neutral_value, cable_combination);
	}
	else if (p < config.firstExpParam) {
		// Zone params: output cables only, DSP combines with preset via ZoneBasedParam
		final_value = cable_combination;
	}
	else {
		final_value = getFinalParameterValueExpWithDumbEnvelopeHack(param_neutral_value, cable_combination, p);
	}

	param_final_values_[p - config.firstParam] = final_value;
}

int32_t rangeFinalValues[kMaxNumPatchCables]; // TODO: storing these in permanent memory per voice could save a tiny bit
                                              // of time... actually so minor though, maybe not worth it.

// You may as well check sourcesChanged before calling this.
void Patcher::performPatching(uint32_t sourcesChanged, Sound& sound, ParamManagerForTimeline& param_manager) {

	PatchCableSet& patch_cable_set = *param_manager.getPatchCableSet();
	int32_t globality = config.globality;
	Destination* destination = patch_cable_set.destinations[globality];
	if (destination == nullptr) {
		return;
	}

	sourcesChanged &= patch_cable_set.sourcesPatchedToAnything[globality];
	if (sourcesChanged == 0u) {
		return;
	}

	// First, "range" Destinations
	for (int32_t i = 0; destination->destinationParamDescriptor.data < (uint32_t)0xFFFFFF00; destination++, i++) {
		if (!(destination->sources & sourcesChanged)) {
			continue; // And don't increment i.
		}

		int32_t cables_combination = combineCablesLinearForRangeParam(destination, param_manager);
		rangeFinalValues[i] = getFinalParameterValueLinear(536870912, cables_combination);
	}

	// param_id, cable_combo
	etl::vector<std::pair<int32_t, int32_t>, params::kNumParams> cable_combos;

	// Now normal, actual params/destinations
	for (; destination->destinationParamDescriptor.data < (config.firstHybridParam | 0xFFFFFF00); destination++) {
		if ((destination->sources & sourcesChanged) == 0u) {
			continue;
		}

		int32_t param = destination->destinationParamDescriptor.getJustTheParam();
		cable_combos.push_back({param, combineCablesLinear(destination, param, sound, param_manager)});
	}

	for (; destination->sources != 0u; destination++) {
		if ((destination->sources & sourcesChanged) == 0u) {
			continue;
		}

		int32_t param = destination->destinationParamDescriptor.getJustTheParam();
		cable_combos.push_back({param, combineCablesExp(destination, param, sound, param_manager)});
	}

	// sort by param_id
	std::ranges::sort(cable_combos, std::less(), &std::pair<int32_t, int32_t>::first);

	auto* iterator = cable_combos.begin();

	// Now, turn those cableCombinations into paramFinalValues.
	// Volume params
	for (; iterator < cable_combos.end() && iterator->first < config.firstNonVolumeParam; iterator++) {
		auto [param, combo] = *iterator;
		param_final_values_[param - config.firstParam] = getFinalParameterValueVolume(paramNeutralValues[param], combo);
	}

	// Linear params
	for (; iterator < cable_combos.end() && iterator->first < config.firstHybridParam; iterator++) {
		auto [param, cable_combo] = *iterator;
		param_final_values_[param - config.firstParam] =
		    getFinalParameterValueLinear(paramNeutralValues[param], cable_combo);
	}

	// Hybrid params
	for (; iterator < cable_combos.end() && iterator->first < config.firstZoneParam; iterator++) {
		auto [param, cable_combo] = *iterator;
		param_final_values_[param - config.firstParam] =
		    getFinalParameterValueHybrid(paramNeutralValues[param], cable_combo);
	}

	// Zone params: output cables only, DSP combines with preset via ZoneBasedParam
	for (; iterator < cable_combos.end() && iterator->first < config.firstExpParam; iterator++) {
		auto [param, cable_combo] = *iterator;
		param_final_values_[param - config.firstParam] = cable_combo;
	}

	// Exp params
	for (; iterator < cable_combos.end() && iterator->first < config.endParams; iterator++) {
		auto [param, cable_combo] = *iterator;
		param_final_values_[param - config.firstParam] =
		    getFinalParameterValueExpWithDumbEnvelopeHack(paramNeutralValues[param], cable_combo, param);
	}
}

int32_t Patcher::cableToLinearParamWithoutRangeAdjustment(int32_t running_total, int32_t source_value,
                                                          int32_t cable_strength) {
	int32_t scaled_source = multiply_32x32_rshift32(source_value, cable_strength);

	// 0 to 1073741824; 536870912 counts as "1" for next multiplication
	int32_t made_positive = (scaled_source + 536870912);
	int32_t pre_limits = multiply_32x32_rshift32(running_total, made_positive);
	return lshiftAndSaturate<3>(pre_limits);
}

int32_t Patcher::cableToLinearParam(int32_t running_total, const PatchCable& patch_cable, int32_t source_value,
                                    int32_t cable_strength) {
	int32_t scaled_source = multiply_32x32_rshift32(source_value, cable_strength);
	scaled_source = patch_cable.applyRangeAdjustment(scaled_source);

	// 0 to 1073741824; 536870912 counts as "1" for next multiplication
	int32_t made_positive = (scaled_source + 536870912);
	int32_t pre_limits = multiply_32x32_rshift32(running_total, made_positive);
	return lshiftAndSaturate<3>(pre_limits);
}

int32_t Patcher::cableToExpParamWithoutRangeAdjustment(int32_t running_total, int32_t source_value,
                                                       int32_t cable_strength) {
	return running_total + multiply_32x32_rshift32(source_value, cable_strength);
}

int32_t Patcher::cableToExpParam(int32_t running_total, const PatchCable& patch_cable, int32_t source_value,
                                 int32_t cable_strength) {
	int32_t scaled_source = multiply_32x32_rshift32(source_value, cable_strength);
	return running_total + patch_cable.applyRangeAdjustment(scaled_source);
}

[[gnu::always_inline]] inline int32_t Patcher::combineCablesLinearForRangeParam(const Destination* destination,
                                                                                ParamManager& param_manager) {
	int32_t running_total = 536870912; // 536870912 means "1". runningTotalCombination will not be allowed
	                                   // to get bigger than 2147483647, which means "4".

	PatchCableSet& patch_cable_set = *param_manager.getPatchCableSet();

	// For each patch cable affecting the range of this cable (got that?)
	for (int32_t cable = destination->firstCable; cable < destination->endCable; cable++) {
		PatchCable& patch_cable = patch_cable_set.patchCables[cable];
		PatchSource source = patch_cable.from;
		int32_t source_value = source_values_[std::to_underlying(source)];

		// Special exception: If we're patching aftertouch to range. Normally, unlike other sources, aftertouch goes
		// from 0 to 2147483647. This is because we want it to have no effect at its negative extreme, which isn't
		// normally what we want. However, when patched to range, we do want this again, so "transpose" it here.
		if (source == PatchSource::AFTERTOUCH) {
			source_value = (source_value - 1073741824) << 1;
		}
		else {
			source_value = patch_cable.toPolarity(source_value);
		}

		int32_t cable_strength = patch_cable.param.getCurrentValue();
		running_total = cableToLinearParamWithoutRangeAdjustment(running_total, source_value, cable_strength);
	}

	return running_total - 536870912;
	// return value is ideally between -536870912 and 536870912, but may get up to 1610612736 if the result above was
	// "4" because of multiple patch cables being multiplied together
}

// Linear param - combine all cables by multiplying their values (values centred around 1). Inputs effectively range
// from "0" to "2". Output (product) clips off at "4". Call this if (p < getFirstHybridParam()) - "Pan" sits at the end
// of the linear params and is the exception to the rule - it doesn't want this multiplying treatment
[[gnu::always_inline]] inline int32_t Patcher::combineCablesLinear(const Destination* destination, uint32_t p,
                                                                   Sound& sound, ParamManager& param_manager) {
	int32_t running_total = 536870912; // 536870912 means "1". runningTotalCombination will not be allowed
	                                   // to get bigger than 2147483647, which means "4".

	PatchCableSet& patch_cable_set = *param_manager.getPatchCableSet();

	// Do the "preset value" (which we treat like a "cable" here)
	running_total = cableToLinearParamWithoutRangeAdjustment(
	    running_total, sound.getSmoothedPatchedParamValue(p, param_manager), paramRanges[p]);

	if (destination != nullptr) {
		// For each patch cable affecting this parameter
		for (int32_t cable = destination->firstCable; cable < destination->endCable; cable++) {
			PatchCable& patch_cable = patch_cable_set.patchCables[cable];
			PatchSource source = patch_cable.from;
			int32_t source_value = source_values_[std::to_underlying(source)];
			source_value = patch_cable.toPolarity(source_value);
			int32_t cable_strength = patch_cable.param.getCurrentValue();
			running_total = cableToLinearParam(running_total, patch_cable, source_value, cable_strength);
		}
	}

	return running_total - 536870912;
	// return value is ideally between -536870912 and 536870912, but may get up to 1610612736 if the result above was
	// "4" because of multiple patch cables being multiplied together
}

// Exp param - combine all cables by adding their values (centred around 0)
// Call this if (p >= getFirstHybridParam())
[[gnu::always_inline]] inline int32_t Patcher::combineCablesExp(const Destination* destination, uint32_t param,
                                                                Sound& sound, ParamManager& param_manager) {

	int32_t running_total = 0;

	PatchCableSet& patch_cable_set = *param_manager.getPatchCableSet();

	if (destination != nullptr) {
		// For each patch cable affecting this parameter
		for (int32_t c = destination->firstCable; c < destination->endCable; c++) {
			PatchCable& patch_cable = patch_cable_set.patchCables[c];
			int32_t source_value = source_values_[std::to_underlying(patch_cable.from)];
			source_value = patch_cable.toPolarity(source_value);
			int32_t cable_strength = patch_cable_set.getModifiedPatchCableAmount(c, param);
			running_total = cableToExpParam(running_total, patch_cable, source_value, cable_strength);
		}

		// Hack for wave index params - make the patching (but not the preset value) stretch twice as far, to allow the
		// opposite end to be reached even if the user's preset value is all the way to one end. These params are
		// "hybrid" ones, and probably in a perfect world I would have made the other ones behave the same way. But I
		// can't break users' songs.
		if (param == params::LOCAL_OSC_A_WAVE_INDEX || param == params::LOCAL_OSC_B_WAVE_INDEX) {
			running_total <<= 1;
		}
	}

	// Do the "preset value" (which we treat like a "cable" here)
	// Zone params: return cables only, DSP combines with preset via ZoneBasedParam
	bool isZoneParam = (param >= config.firstZoneParam && param < config.firstExpParam);
	if (!isZoneParam) {
		running_total = cableToExpParamWithoutRangeAdjustment(
		    running_total, sound.getSmoothedPatchedParamValue(param, param_manager), paramRanges[param]);
	}

	return running_total;
}

// NOTE: parameter preset values can't be bigger than 536870912, otherwise overflowing will occur

void Patcher::performInitialPatching(Sound& sound, ParamManager& param_manager) {
	// In this function, we are sneaky and write the "cable combination" working value in to paramFinalValues, before
	// going back over that array with the final step. This saves memory. Didn't seem to have a real effect on speed.

	// The contents of this bit could be optimized further by doing even more passes, splitting up e.g.
	// combineCablesLinear into iterations, which sometimes could be done on all params at once. Or, could just
	// pre-compute and copy "final values" for where there's no patching

	// First for the params whose cables get multiplied, we go do them all as if they have no patching, and then for
	// the few which do, we overwrite those values
	for (int32_t p = config.firstParam; p < config.firstHybridParam; p++) {
		param_final_values_[p - config.firstParam] = combineCablesLinear(nullptr, p, sound, param_manager);
	}

	// And now we do the same for params whose cables get added
	for (int32_t p = config.firstHybridParam; p < config.endParams; p++) {
		param_final_values_[p - config.firstParam] = combineCablesExp(nullptr, p, sound, param_manager);
	}

	Destination* destination = param_manager.getPatchCableSet()->destinations[config.globality];
	if (destination != nullptr) {

		// First, "range" Destinations
		for (int32_t i = 0; destination->destinationParamDescriptor.data < (uint32_t)0xFFFFFF00; destination++, i++) {
			int32_t cables_combination = combineCablesLinearForRangeParam(destination, param_manager);

			rangeFinalValues[i] = getFinalParameterValueLinear(536870912, cables_combination);
		}

		// Now, regular Destinations going directly to a param
		for (; destination->destinationParamDescriptor.data < (config.firstHybridParam | 0xFFFFFF00); destination++) {
			int32_t p = destination->destinationParamDescriptor.getJustTheParam();
			param_final_values_[p - config.firstParam] = combineCablesLinear(destination, p, sound, param_manager);
		}
		for (; destination->sources; destination++) {
			int32_t p = destination->destinationParamDescriptor.getJustTheParam();
			param_final_values_[p - config.firstParam] = combineCablesExp(destination, p, sound, param_manager);
		}
	}

	// And now convert those "cable combinations" into "final values".
	// Volume params
	for (int32_t param = config.firstParam; param < config.firstNonVolumeParam; param++) {
		param_final_values_[param - config.firstParam] =
		    getFinalParameterValueVolume(paramNeutralValues[param], param_final_values_[param - config.firstParam]);
	}

	// Linear params
	for (int32_t param = config.firstNonVolumeParam; param < config.firstHybridParam; param++) {
		param_final_values_[param - config.firstParam] =
		    getFinalParameterValueLinear(paramNeutralValues[param], param_final_values_[param - config.firstParam]);
	}

	// Hybrid params
	for (int32_t param = config.firstHybridParam; param < config.firstZoneParam; param++) {
		param_final_values_[param - config.firstParam] =
		    getFinalParameterValueHybrid(paramNeutralValues[param], param_final_values_[param - config.firstParam]);
	}

	// Zone params: cables only, DSP combines with preset via ZoneBasedParam
	// (no transformation needed - combineCablesExp already returns cables only for zone params)

	// Exp params
	for (int32_t param = config.firstExpParam; param < config.endParams; param++) {
		param_final_values_[param - config.firstParam] = getFinalParameterValueExpWithDumbEnvelopeHack(
		    paramNeutralValues[param], param_final_values_[param - config.firstParam], param);
	}
}
