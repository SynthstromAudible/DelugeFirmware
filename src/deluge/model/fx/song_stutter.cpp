/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "model/fx/song_stutter.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "synth/matrix/descriptors.h" // kParamNeutral, kParamRange

#include "libdeluge/system.h" // AudioExcludedSection
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "util/fixedpoint.h"
#include "util/functions.h"
#include "util/structural_edit_probe.h"

#include <algorithm>

namespace params = deluge::modulation::params;

namespace deluge::model {

namespace {
/// The sync level the stutter's rate is synced to the tempo at: fixed, as the stutter has no sync setting.
constexpr int32_t kStutterSync = 7;
} // namespace

SongStutter::SongStutter() : fx_{kStutterAllocator} {
}

Error SongStutter::begin(ModControllableAudio& owner, const deluge::effects::StutterState& settings, uint32_t rate) {
	deluge::effects::StutterChange change = fx_.prepare(rate);
	bool began = false;
	{
		AudioExcludedSection section; // the render reads the owner and plays the tape
		// A second owner's begin takes the first's tape back into the change: its stutter ends here.
		began = fx_.commit(change, settings);
		if (began) {
			owner_ = &owner;
			playing = settings;
			deluge::noteStructuralEdit(deluge::StructuralContainer::STUTTER_TAPE);
		}
	}
	fx_.finish(change);
	return began ? Error::NONE : Error::INSUFFICIENT_RAM;
}

void SongStutter::end() {
	if (!fx_.active()) {
		return;
	}
	deluge::effects::StutterChange change{};
	{
		AudioExcludedSection section; // the render reads the owner and plays the tape
		fx_.commit_end(change);
		owner_ = nullptr;
		deluge::noteStructuralEdit(deluge::StructuralContainer::STUTTER_TAPE);
	}
	fx_.finish(change);
}

StutterInput stutterInput(const ModControllableAudio& owner, ParamManager* paramManager) {
	if (currentSong == nullptr) {
		return {};
	}
	SongStutter& stutter = currentSong->live.stutter;
	deluge::effects::Stutter* const playing = stutter.stutterFor(owner);
	if (playing == nullptr) {
		return {};
	}
	return {.stutter = playing,
	        .rate = static_cast<uint32_t>(stutterRate(paramManager, stutter.lastQuantizedKnobDiff,
	                                                  currentSong->getInputTickMagnitude(),
	                                                  playbackHandler.getTimePerInternalTickInverse()))};
}

void initStutterParams(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_STUTTER_RATE), 0);
}

int32_t stutterRate(ParamManager* paramManager, int32_t knobDiff, int32_t magnitude, uint32_t timePerTickInverse) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	int32_t paramValue = unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE);

	// A quantized stutter's note length is a knob offset (zero for a free stutter), added in knob positions and held
	// to the knob's range.
	const int32_t knobPos =
	    std::clamp<int32_t>(unpatchedParams->paramValueToKnobPos(paramValue, nullptr) + knobDiff, -64, 64);
	paramValue = unpatchedParams->knobPosToParamValue(knobPos, nullptr);

	int32_t rate =
	    deluge::dsp::fixed::curve_exponential(deluge::synth::matrix::kParamNeutral[params::GLOBAL_DELAY_RATE],
	                                          deluge::dsp::fixed::preset_combination(paramValue));

	rate = multiply_32x32_rshift32(rate, timePerTickInverse);

	// Limit to the biggest number we can store...
	const int32_t lShiftAmount = kStutterSync + 6 - magnitude;
	const int32_t limit = 2147483647 >> lShiftAmount;
	rate = std::min(rate, limit);
	rate <<= lShiftAmount;
	return rate;
}

} // namespace deluge::model
