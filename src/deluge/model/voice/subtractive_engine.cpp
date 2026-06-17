/*
 * Copyright © 2014-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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

#include "model/voice/subtractive_engine.h"
#include "definitions_cxx.hpp"
#include "model/model_stack.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/voice/voice.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include <cstring>

void SubtractiveEngine::render(Voice& voice, Sound& sound, ParamManagerForTimeline* paramManager,
                               ModelStackWithSoundFlags& modelStack, int32_t* oscBuffer, int32_t numSamples,
                               bool doingOscSync, uint32_t* oscSyncPos, int32_t overallPitchAdjust,
                               const int32_t* sourceAmplitudesNow, const int32_t* sourceAmplitudeIncrements,
                               const int32_t* sourceWaveIndexIncrements, bool& anythingInOscBuffer,
                               bool& didStereoTempBuffer, bool& unassignVoiceAfter, const VoiceConfig& cfg) {

	bool unisonPartBecameInactive = false;

	uint32_t oscSyncPhaseIncrement[kMaxNumVoicesUnison];
	uint32_t sourcesToRenderInStereo = 0;

	// First, render any mono sources, and note whether there are any stereo ones
	for (int32_t s = 0; s < kNumSources; s++) {

		uint32_t* getPhaseIncrements = nullptr;
		bool getOutAfterGettingPhaseIncrements = false;

		// If we're doing osc sync and this is osc A...
		if ((s == 0) && doingOscSync) {
			getPhaseIncrements = oscSyncPhaseIncrement;
		}

		// This this source isn't active...
		if (!sound.isSourceActiveCurrently(s, paramManager)) {

			// If we're doing osc sync...
			if (getPhaseIncrements) {
				getOutAfterGettingPhaseIncrements = true;

				// Otherwise, skip it
			}
			else {
				continue;
			}
		}

		if (!sound.sources[s].renderInStereo(&sound, (SampleHolder*)voice.guides[s].audioFileHolder)) {
			voice.renderBasicSource(sound, paramManager, s, oscBuffer, numSamples, false, sourceAmplitudesNow[s],
			                        &unisonPartBecameInactive, overallPitchAdjust, (s == 1) && doingOscSync, oscSyncPos,
			                        oscSyncPhaseIncrement, sourceAmplitudeIncrements[s], getPhaseIncrements,
			                        getOutAfterGettingPhaseIncrements, sourceWaveIndexIncrements[s], cfg);
			anythingInOscBuffer = true;
		}
		else {
			sourcesToRenderInStereo |= (1 << s);
		}
	}

	// If any sources need rendering in stereo
	if (sourcesToRenderInStereo) {

		// If we've already got something mono in the buffer, copy that to the right-channel buffer
		if (anythingInOscBuffer) {
			for (int32_t i = numSamples - 1; i >= 0; i--) {
				oscBuffer[(i << 1) + 1] = oscBuffer[i];
				oscBuffer[(i << 1)] = oscBuffer[i];
			}
		}

		// Otherwise, make it blank
		else {
			memset(&oscBuffer[numSamples], 0, numSamples * sizeof(int32_t));
		}

		// Render each source that's stereo. Osc sync must still be applied here: with stereo unison spread, both
		// sources get deferred into this path (see Source::renderInStereo()), so we have to capture osc A's phase
		// increments and sync osc B to them just like the mono path above does.
		for (int32_t s = 0; s < kNumSources; s++) {
			if (sourcesToRenderInStereo & (1 << s)) {

				uint32_t* getPhaseIncrements = ((s == 0) && doingOscSync) ? oscSyncPhaseIncrement : nullptr;
				bool getOutAfterGettingPhaseIncrements =
				    getPhaseIncrements && !sound.isSourceActiveCurrently(s, paramManager);

				voice.renderBasicSource(sound, paramManager, s, oscBuffer, numSamples, true, sourceAmplitudesNow[s],
				                        &unisonPartBecameInactive, overallPitchAdjust, (s == 1) && doingOscSync,
				                        oscSyncPos, oscSyncPhaseIncrement, sourceAmplitudeIncrements[s],
				                        getPhaseIncrements, getOutAfterGettingPhaseIncrements,
				                        sourceWaveIndexIncrements[s], cfg);
			}
		}

		didStereoTempBuffer = true;
	}

	// If any unison part became inactive (for either source), and no noise-source, then it might be time to
	// unassign the voice...
	if (unisonPartBecameInactive && voice.areAllUnisonPartsInactive(modelStack)) {

		// If no filters, we can just unassign
		if (!voice.filterSet.isOn()) {
			unassignVoiceAfter = true;
		}

		// Otherwise, must do a fast-release to avoid a click
		else {
			if (voice.envelopes[0].stage() < Envelope::Stage::FAST_RELEASE) {
				voice.envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE);
			}
		}
	}
}
