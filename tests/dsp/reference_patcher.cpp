/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

#include "reference_patcher.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "synth/matrix/descriptors.h" // kParamNeutral, kParamRange
#include "synth/modulation/cable_depth.h"
#include "util/param_functions.h" // getFinalParameterValueExpWithDumbEnvelopeHack
#include <algorithm>
#include <array>
#include <etl/vector.h>
#include <functional>
#include <utility>

namespace deluge::synth::test {

namespace params = deluge::modulation::params;
using deluge::synth::RoutedCable;
using deluge::synth::RoutedDestination;

// If NULL Destination, that means no cables - just the preset value
void ReferencePatcher::recalculate_final_value_for_param_with_no_cables(int32_t p,
                                                                        const ReferenceBlock& control_block) {

	int32_t cable_combination = (p < config_.first_hybrid_param) ? combine_cables_linear(nullptr, p, control_block)
	                                                             : combine_cables_exp(nullptr, p, control_block);

	int32_t final_value = 0;
	int32_t param_neutral_value = deluge::synth::matrix::kParamNeutral[p];

	if (p < config_.first_hybrid_param) {
		if (p < config_.first_non_volume_param) {
			final_value = deluge::dsp::fixed::curve_square(param_neutral_value, cable_combination);
		}
		else {
			final_value = deluge::dsp::fixed::curve_linear(param_neutral_value, cable_combination);
		}
	}
	else {
		if (p < config_.first_exp_param) {
			final_value = deluge::dsp::fixed::curve_bipolar(param_neutral_value, cable_combination); // Hybrid - add
		}
		else {
			final_value = getFinalParameterValueExpWithDumbEnvelopeHack(param_neutral_value, cable_combination, p);
		}
	}

	param_final_values_[p - config_.first_param] = final_value;
}

// You may as well check sourcesChanged before calling this.
void ReferencePatcher::perform_patching(uint32_t sources_changed, const ReferenceBlock& control_block) {

	int32_t globality = config_.globality;
	const deluge::synth::RoutedDestination* destination = control_block.routing.destinations[globality];
	if (destination == nullptr) {
		return;
	}

	sources_changed &= control_block.routing.sources_patched[globality];
	if (sources_changed == 0u) {
		return;
	}

	// First, "range" Destinations
	for (int32_t i = 0; destination->target.word < static_cast<uint32_t>(0xFFFFFF00); destination++, i++) {
		if ((destination->sources & sources_changed) == 0u) {
			continue; // And don't increment i.
		}

		int32_t cables_combination = combine_cables_linear_for_range_param(destination, control_block);
		range_final_values_[i] = deluge::dsp::fixed::curve_linear(536870912, cables_combination);
	}

	// param_id, cable_combo
	etl::vector<std::pair<int32_t, int32_t>, params::kNumParams> cable_combos;

	// Now normal, actual params/destinations
	for (; destination->target.word < (config_.first_hybrid_param | 0xFFFFFF00); destination++) {
		if ((destination->sources & sources_changed) == 0u) {
			continue;
		}

		int32_t param = destination->target.param_index();
		cable_combos.push_back({param, combine_cables_linear(destination, param, control_block)});
	}

	for (; destination->sources != 0u; destination++) {
		if ((destination->sources & sources_changed) == 0u) {
			continue;
		}

		int32_t param = destination->target.param_index();
		cable_combos.push_back({param, combine_cables_exp(destination, param, control_block)});
	}

	// sort by param_id
	std::ranges::sort(cable_combos, std::less(), &std::pair<int32_t, int32_t>::first);

	auto* iterator = cable_combos.begin();

	// Now, turn those cableCombinations into paramFinalValues.
	// Volume params
	for (; iterator < cable_combos.end() && iterator->first < config_.first_non_volume_param; iterator++) {
		auto [param, combo] = *iterator;
		param_final_values_[param - config_.first_param] =
		    deluge::dsp::fixed::curve_square(deluge::synth::matrix::kParamNeutral[param], combo);
	}

	// Linear params
	for (; iterator < cable_combos.end() && iterator->first < config_.first_hybrid_param; iterator++) {
		auto [param, cable_combo] = *iterator;
		param_final_values_[param - config_.first_param] =
		    deluge::dsp::fixed::curve_linear(deluge::synth::matrix::kParamNeutral[param], cable_combo);
	}

	// Hybrid params
	for (; iterator < cable_combos.end() && iterator->first < config_.first_exp_param; iterator++) {
		auto [param, cable_combo] = *iterator;
		param_final_values_[param - config_.first_param] =
		    deluge::dsp::fixed::curve_bipolar(deluge::synth::matrix::kParamNeutral[param], cable_combo);
	}

	// Exp params
	for (; iterator < cable_combos.end() && iterator->first < config_.end_params; iterator++) {
		auto [param, cable_combo] = *iterator;
		param_final_values_[param - config_.first_param] = getFinalParameterValueExpWithDumbEnvelopeHack(
		    deluge::synth::matrix::kParamNeutral[param], cable_combo, param);
	}
}

int32_t ReferencePatcher::cable_to_linear_param_without_range_adjustment(int32_t running_total, int32_t source_value,
                                                                         int32_t cable_strength) {
	int32_t scaled_source = multiply_32x32_rshift32(source_value, cable_strength);

	// 0 to 1073741824; 536870912 counts as "1" for next multiplication
	int32_t made_positive = (scaled_source + 536870912);
	int32_t pre_limits = multiply_32x32_rshift32(running_total, made_positive);
	return lshift_and_saturate<3>(pre_limits);
}

int32_t ReferencePatcher::cable_to_linear_param(int32_t running_total, const deluge::synth::RoutedCable& patch_cable,
                                                int32_t source_value, int32_t cable_strength) {
	int32_t scaled_source = multiply_32x32_rshift32(source_value, cable_strength);
	scaled_source = deluge::synth::apply_range(scaled_source, range_of_(patch_cable));

	// 0 to 1073741824; 536870912 counts as "1" for next multiplication
	int32_t made_positive = (scaled_source + 536870912);
	int32_t pre_limits = multiply_32x32_rshift32(running_total, made_positive);
	return lshift_and_saturate<3>(pre_limits);
}

// Exp param cable combinations saturate rather than wrap: wrapping would flip a param at one extreme of its range
// straight to the other (e.g. cutoff fully closed to fully open) mid-note.
int32_t ReferencePatcher::cable_to_exp_param_without_range_adjustment(int32_t running_total, int32_t source_value,
                                                                      int32_t cable_strength) {
	return add_saturate(running_total, multiply_32x32_rshift32(source_value, cable_strength));
}

int32_t ReferencePatcher::cable_to_exp_param(int32_t running_total, const deluge::synth::RoutedCable& patch_cable,
                                             int32_t source_value, int32_t cable_strength) {
	int32_t scaled_source = multiply_32x32_rshift32(source_value, cable_strength);
	return add_saturate(running_total, deluge::synth::apply_range(scaled_source, range_of_(patch_cable)));
}

[[gnu::always_inline]] inline int32_t
ReferencePatcher::combine_cables_linear_for_range_param(const deluge::synth::RoutedDestination* destination,
                                                        const ReferenceBlock& control_block) {
	int32_t running_total = 536870912; // 536870912 means "1". runningTotalCombination will not be allowed
	                                   // to get bigger than 2147483647, which means "4".

	// For each patch cable affecting the range of this cable (got that?)
	for (int32_t cable = destination->first_cable; cable < destination->end_cable; cable++) {
		const deluge::synth::RoutedCable& patch_cable = control_block.routing.cables[cable];
		PatchSource source = patch_cable.source;
		int32_t source_value = source_values_[std::to_underlying(source)];

		// Special exception: If we're patching aftertouch to range. Normally, unlike other sources, aftertouch goes
		// from 0 to 2147483647. This is because we want it to have no effect at its negative extreme, which isn't
		// normally what we want. However, when patched to range, we do want this again, so "transpose" it here.
		if (source == PatchSource::AFTERTOUCH) {
			source_value = (source_value - 1073741824) << 1;
		}
		else {
			source_value = deluge::synth::to_polarity(source, patch_cable.polarity, source_value);
		}

		int32_t cable_strength = control_block.depths[cable];
		running_total = cable_to_linear_param_without_range_adjustment(running_total, source_value, cable_strength);
	}

	return running_total - 536870912;
	// return value is ideally between -536870912 and 536870912, but may get up to 1610612736 if the result above was
	// "4" because of multiple patch cables being multiplied together
}

// Linear param - combine all cables by multiplying their values (values centred around 1). Inputs effectively range
// from "0" to "2". Output (product) clips off at "4". Call this if (p < getFirstHybridParam()) - "Pan" sits at the end
// of the linear params and is the exception to the rule - it doesn't want this multiplying treatment
[[gnu::always_inline]] inline int32_t
ReferencePatcher::combine_cables_linear(const deluge::synth::RoutedDestination* destination, int32_t p,
                                        const ReferenceBlock& control_block) {
	int32_t running_total = 536870912; // 536870912 means "1". runningTotalCombination will not be allowed
	                                   // to get bigger than 2147483647, which means "4".

	// Do the "preset value" (which we treat like a "cable" here)
	running_total = cable_to_linear_param_without_range_adjustment(running_total, control_block.preset_value(p),
	                                                               deluge::synth::matrix::kParamRange[p]);

	if (destination != nullptr) {
		// For each patch cable affecting this parameter
		for (int32_t cable = destination->first_cable; cable < destination->end_cable; cable++) {
			const deluge::synth::RoutedCable& patch_cable = control_block.routing.cables[cable];
			PatchSource source = patch_cable.source;
			int32_t source_value = source_values_[std::to_underlying(source)];
			source_value = deluge::synth::to_polarity(source, patch_cable.polarity, source_value);
			int32_t cable_strength = control_block.depths[cable];
			running_total = cable_to_linear_param(running_total, patch_cable, source_value, cable_strength);
		}
	}

	return running_total - 536870912;
	// return value is ideally between -536870912 and 536870912, but may get up to 1610612736 if the result above was
	// "4" because of multiple patch cables being multiplied together
}

// Exp param - combine all cables by adding their values (centred around 0)
// Call this if (p >= getFirstHybridParam())
[[gnu::always_inline]] inline int32_t
ReferencePatcher::combine_cables_exp(const deluge::synth::RoutedDestination* destination, int32_t param,
                                     const ReferenceBlock& control_block) {

	int32_t running_total = 0;

	if (destination != nullptr) {
		// For each patch cable affecting this parameter
		for (int32_t c = destination->first_cable; c < destination->end_cable; c++) {
			const deluge::synth::RoutedCable& patch_cable = control_block.routing.cables[c];
			int32_t source_value = source_values_[std::to_underlying(patch_cable.source)];
			source_value = deluge::synth::to_polarity(patch_cable.source, patch_cable.polarity, source_value);
			int32_t cable_strength = deluge::synth::modified_depth(control_block.depths[c], patch_cable.source, param);
			running_total = cable_to_exp_param(running_total, patch_cable, source_value, cable_strength);
		}

		// Hack for wave index params - make the patching (but not the preset value) stretch twice as far, to allow the
		// opposite end to be reached even if the user's preset value is all the way to one end. These params are
		// "hybrid" ones, and probably in a perfect world I would have made the other ones behave the same way. But I
		// can't break users' songs.
		if (param == params::LOCAL_OSC_A_WAVE_INDEX || param == params::LOCAL_OSC_B_WAVE_INDEX) {
			running_total = lshift_and_saturate<1>(running_total);
		}
	}

	// Do the "preset value" (which we treat like a "cable" here)
	running_total = cable_to_exp_param_without_range_adjustment(running_total, control_block.preset_value(param),
	                                                            deluge::synth::matrix::kParamRange[param]);

	return running_total;
}

// NOTE: parameter preset values can't be bigger than 536870912, otherwise overflowing will occur

void ReferencePatcher::perform_initial_patching(const ReferenceBlock& control_block) {
	// In this function, we are sneaky and write the "cable combination" working value in to paramFinalValues, before
	// going back over that array with the final step. This saves memory. Didn't seem to have a real effect on speed.

	// The contents of this bit could be optimized further by doing even more passes, splitting up e.g.
	// combineCablesLinear into iterations, which sometimes could be done on all params at once. Or, could just
	// pre-compute and copy "final values" for where there's no patching

	// First for the params whose cables get multiplied, we go do them all as if they have no patching, and then for
	// the few which do, we overwrite those values
	for (int32_t p = config_.first_param; p < config_.first_hybrid_param; p++) {
		param_final_values_[p - config_.first_param] = combine_cables_linear(nullptr, p, control_block);
	}

	// And now we do the same for params whose cables get added
	for (int32_t p = config_.first_hybrid_param; p < config_.end_params; p++) {
		param_final_values_[p - config_.first_param] = combine_cables_exp(nullptr, p, control_block);
	}

	const deluge::synth::RoutedDestination* destination = control_block.routing.destinations[config_.globality];
	if (destination != nullptr) {

		// First, "range" Destinations
		for (int32_t i = 0; destination->target.word < static_cast<uint32_t>(0xFFFFFF00); destination++, i++) {
			int32_t cables_combination = combine_cables_linear_for_range_param(destination, control_block);

			range_final_values_[i] = deluge::dsp::fixed::curve_linear(536870912, cables_combination);
		}

		// Now, regular Destinations going directly to a param
		for (; destination->target.word < (config_.first_hybrid_param | 0xFFFFFF00); destination++) {
			int32_t p = destination->target.param_index();
			param_final_values_[p - config_.first_param] = combine_cables_linear(destination, p, control_block);
		}
		for (; destination->sources != 0u; destination++) {
			int32_t p = destination->target.param_index();
			param_final_values_[p - config_.first_param] = combine_cables_exp(destination, p, control_block);
		}
	}

	// And now convert those "cable combinations" into "final values".
	// Volume params
	for (int32_t param = config_.first_param; param < config_.first_non_volume_param; param++) {
		param_final_values_[param - config_.first_param] = deluge::dsp::fixed::curve_square(
		    deluge::synth::matrix::kParamNeutral[param], param_final_values_[param - config_.first_param]);
	}

	// Linear params
	for (int32_t param = config_.first_non_volume_param; param < config_.first_hybrid_param; param++) {
		param_final_values_[param - config_.first_param] = deluge::dsp::fixed::curve_linear(
		    deluge::synth::matrix::kParamNeutral[param], param_final_values_[param - config_.first_param]);
	}

	// Hybrid params
	for (int32_t param = config_.first_hybrid_param; param < config_.first_exp_param; param++) {
		param_final_values_[param - config_.first_param] = deluge::dsp::fixed::curve_bipolar(
		    deluge::synth::matrix::kParamNeutral[param], param_final_values_[param - config_.first_param]);
	}

	// Exp params
	for (int32_t param = config_.first_exp_param; param < config_.end_params; param++) {
		param_final_values_[param - config_.first_param] = getFinalParameterValueExpWithDumbEnvelopeHack(
		    deluge::synth::matrix::kParamNeutral[param], param_final_values_[param - config_.first_param], param);
	}
}

} // namespace deluge::synth::test
