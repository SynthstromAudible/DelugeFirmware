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

#include "model/voice/fm_engine.h"
#include "definitions_cxx.hpp"
#include "model/voice/voice.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "util/fixedpoint.h"
#include <cstring>

using namespace deluge;
namespace params = deluge::modulation::params;

void FMEngine::render(Voice& voice, int32_t u, int32_t numSamples, int32_t overallPitchAdjust, bool stereoUnison,
                      int32_t unisonAmplitudeL, int32_t unisonAmplitudeR, const uint32_t* phaseIncrements,
                      const int32_t* sourceAmplitudes, const int32_t* sourceAmplitudesNow,
                      const int32_t* sourceAmplitudeIncrements, bool* modulatorsActive,
                      const int32_t* modulatorAmplitudeIncrements, int32_t* oscBuffer, int32_t* modulatorScratch,
                      int32_t* carrierScratch, const VoiceConfig& cfg) {

	// If overall pitch adjusted, adjust modulator pitches
	uint32_t phaseIncrementModulator[kNumModulators];
	for (int32_t m = 0; m < kNumModulators; m++) {
		phaseIncrementModulator[m] = voice.unisonParts[u].modulatorPhaseIncrement[m];
		if (phaseIncrementModulator[m] == 0xFFFFFFFF) {
			modulatorsActive[m] = false; // If frequency marked as too high
		}
	}

	if (overallPitchAdjust != kMaxSampleValue) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			if (modulatorsActive[m]) {
				if (!voice.adjustPitch(phaseIncrementModulator[m], overallPitchAdjust)) {
					modulatorsActive[m] = false;
				}
			}
		}
	}

	// Check if individual modulator pitches adjusted
	for (int32_t m = 0; m < kNumModulators; m++) {
		if (modulatorsActive[m]) {
			if (!voice.adjustPitch(phaseIncrementModulator[m],
			                       voice.params.paramFinalValues[params::LOCAL_MODULATOR_0_PITCH_ADJUST + m])) {
				modulatorsActive[m] = false;
			}
		}
	}

	int32_t* fmOscBuffer = oscBuffer;
	if (stereoUnison) {
		// buffer 0-1: stereo output, 2: modulators, 3: per-unison carriers
		fmOscBuffer = carrierScratch;
		memset(fmOscBuffer, 0, numSamples * sizeof(int32_t));
	}

	// Modulators
	if (modulatorsActive[1]) {

		// If special case where mod1 is modulating mod0 but mod0 is inactive, go away...
		if (cfg.modulator1ToModulator0 && !modulatorsActive[0]) {
			goto noModulatorsActive;
		}

		// Render mod1
		voice.renderSineWaveWithFeedback(
		    modulatorScratch, numSamples, &voice.unisonParts[u].modulatorPhase[1], voice.modulatorAmplitudeLastTime[1],
		    phaseIncrementModulator[1], voice.params.paramFinalValues[params::LOCAL_MODULATOR_1_FEEDBACK],
		    &voice.unisonParts[u].modulatorFeedback[1], false, modulatorAmplitudeIncrements[1]);

		// If mod1 is modulating mod0...
		if (cfg.modulator1ToModulator0) {
			// .. render modulator0, receiving the FM from mod1
			voice.renderFMWithFeedback(modulatorScratch, numSamples, nullptr, &voice.unisonParts[u].modulatorPhase[0],
			                           voice.modulatorAmplitudeLastTime[0], phaseIncrementModulator[0],
			                           voice.params.paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK],
			                           &voice.unisonParts[u].modulatorFeedback[0], modulatorAmplitudeIncrements[0]);
		}

		// Otherwise, so long as modulator0 is in fact active, render it separately and add it
		else if (modulatorsActive[0]) {
			voice.renderSineWaveWithFeedback(modulatorScratch, numSamples, &voice.unisonParts[u].modulatorPhase[0],
			                                 voice.modulatorAmplitudeLastTime[0], phaseIncrementModulator[0],
			                                 voice.params.paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK],
			                                 &voice.unisonParts[u].modulatorFeedback[0], true,
			                                 modulatorAmplitudeIncrements[0]);
		}
	}
	else {
		if (modulatorsActive[0]) {
			voice.renderSineWaveWithFeedback(modulatorScratch, numSamples, &voice.unisonParts[u].modulatorPhase[0],
			                                 voice.modulatorAmplitudeLastTime[0], phaseIncrementModulator[0],
			                                 voice.params.paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK],
			                                 &voice.unisonParts[u].modulatorFeedback[0], false,
			                                 modulatorAmplitudeIncrements[0]);
		}
		else {
noModulatorsActive:
			for (int32_t s = 0; s < kNumSources; s++) {
				if (sourceAmplitudes[s]) {
					voice.renderSineWaveWithFeedback(
					    fmOscBuffer, numSamples, &voice.unisonParts[u].sources[s].oscPos, sourceAmplitudesNow[s],
					    phaseIncrements[s], voice.params.paramFinalValues[params::LOCAL_CARRIER_0_FEEDBACK + s],
					    &voice.unisonParts[u].sources[s].carrierFeedback, true, sourceAmplitudeIncrements[s]);
				}
			}

			goto carriersDone;
		}
	}

	// Carriers
	for (int32_t s = 0; s < kNumSources; s++) {
		if (sourceAmplitudes[s]) {
			voice.renderFMWithFeedbackAdd(
			    fmOscBuffer, numSamples, modulatorScratch, &voice.unisonParts[u].sources[s].oscPos,
			    sourceAmplitudesNow[s], phaseIncrements[s],
			    voice.params.paramFinalValues[params::LOCAL_CARRIER_0_FEEDBACK + s],
			    &voice.unisonParts[u].sources[s].carrierFeedback, sourceAmplitudeIncrements[s]);
		}
	}

carriersDone: {}
	if (stereoUnison) {
		// double up the temp buffer
		for (int32_t i = 0; i < numSamples; i++) {
			oscBuffer[(i << 1)] += multiply_32x32_rshift32(fmOscBuffer[i], unisonAmplitudeL) << 2;
			oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(fmOscBuffer[i], unisonAmplitudeR) << 2;
		}
	}
}
