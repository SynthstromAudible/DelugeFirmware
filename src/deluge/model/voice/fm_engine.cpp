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
#include "arm_neon_shim.h" // NEON intrinsics (SIMDe on host) for the operator renders
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/osc/sine.h" // deluge::dsp::fixed::sine / sine_fm_operator
#include "model/voice/voice.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "util/fixedpoint.h"
#include "util/functions.h" // shouldDoPanning
#include <cstring>

using namespace deluge;
namespace params = deluge::modulation::params;

void FMEngine::render(Voice& voice, int32_t numSamples, int32_t overallPitchAdjust, bool stereoUnison,
                      int32_t* sourceAmplitudes, const int32_t* sourceAmplitudesNow,
                      const int32_t* sourceAmplitudeIncrements, bool* modulatorsActive,
                      const int32_t* modulatorAmplitudeIncrements, int32_t* oscBuffer, int32_t* modulatorScratch,
                      int32_t* carrierScratch, bool& didStereoTempBuffer, const VoiceConfig& cfg) {

	if (stereoUnison) {
		// oscBuffer is always a stereo temp buffer
		didStereoTempBuffer = true;
	}

	// For each unison part
	for (int32_t u = 0; u < cfg.numUnison; u++) {

		int32_t unisonAmplitudeL, unisonAmplitudeR;
		shouldDoPanning((stereoUnison ? cfg.unisonPan[u] : 0), &unisonAmplitudeL, &unisonAmplitudeR);

		// Work out the phase increments of the two sources. If these are too high, sourceAmplitudes[s] is set to 0.
		// Yes this will affect all unison parts, which seems like it's not what we want, but since we're traversing
		// the unison parts in ascending frequency, it's fine!

		uint32_t phaseIncrements[kNumSources];
		for (int32_t s = 0; s < kNumSources; s++) {
			phaseIncrements[s] = voice.unisonParts[u].sources[s].phaseIncrementStoredValue;
		}

		// If overall pitch adjusted...
		if (overallPitchAdjust != kMaxSampleValue) {
			for (int32_t s = 0; s < kNumSources; s++) {
				if (!Voice::adjustPitch(phaseIncrements[s], overallPitchAdjust)) {
					sourceAmplitudes[s] = 0;
				}
			}
		}

		// If individual source pitch adjusted...
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!Voice::adjustPitch(phaseIncrements[s],
			                        voice.params.paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
				sourceAmplitudes[s] = 0;
			}
		}

		renderUnisonPart(voice, u, numSamples, overallPitchAdjust, stereoUnison, unisonAmplitudeL, unisonAmplitudeR,
		                 phaseIncrements, sourceAmplitudes, sourceAmplitudesNow, sourceAmplitudeIncrements,
		                 modulatorsActive, modulatorAmplitudeIncrements, oscBuffer, modulatorScratch, carrierScratch,
		                 cfg);
	}
}

void FMEngine::renderUnisonPart(Voice& voice, int32_t u, int32_t numSamples, int32_t overallPitchAdjust,
                                bool stereoUnison, int32_t unisonAmplitudeL, int32_t unisonAmplitudeR,
                                const uint32_t* phaseIncrements, const int32_t* sourceAmplitudes,
                                const int32_t* sourceAmplitudesNow, const int32_t* sourceAmplitudeIncrements,
                                bool* modulatorsActive, const int32_t* modulatorAmplitudeIncrements, int32_t* oscBuffer,
                                int32_t* modulatorScratch, int32_t* carrierScratch, const VoiceConfig& cfg) {

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
				if (!Voice::adjustPitch(phaseIncrementModulator[m], overallPitchAdjust)) {
					modulatorsActive[m] = false;
				}
			}
		}
	}

	// Check if individual modulator pitches adjusted
	for (int32_t m = 0; m < kNumModulators; m++) {
		if (modulatorsActive[m]) {
			if (!Voice::adjustPitch(phaseIncrementModulator[m],
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
		renderSineWaveWithFeedback(modulatorScratch, numSamples, &voice.unisonParts[u].modulatorPhase[1],
		                           voice.modulatorAmplitudeLastTime[1], phaseIncrementModulator[1],
		                           voice.params.paramFinalValues[params::LOCAL_MODULATOR_1_FEEDBACK],
		                           &voice.unisonParts[u].modulatorFeedback[1], false, modulatorAmplitudeIncrements[1]);

		// If mod1 is modulating mod0...
		if (cfg.modulator1ToModulator0) {
			// .. render modulator0, receiving the FM from mod1
			renderFMWithFeedback(modulatorScratch, numSamples, nullptr, &voice.unisonParts[u].modulatorPhase[0],
			                     voice.modulatorAmplitudeLastTime[0], phaseIncrementModulator[0],
			                     voice.params.paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK],
			                     &voice.unisonParts[u].modulatorFeedback[0], modulatorAmplitudeIncrements[0]);
		}

		// Otherwise, so long as modulator0 is in fact active, render it separately and add it
		else if (modulatorsActive[0]) {
			renderSineWaveWithFeedback(modulatorScratch, numSamples, &voice.unisonParts[u].modulatorPhase[0],
			                           voice.modulatorAmplitudeLastTime[0], phaseIncrementModulator[0],
			                           voice.params.paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK],
			                           &voice.unisonParts[u].modulatorFeedback[0], true,
			                           modulatorAmplitudeIncrements[0]);
		}
	}
	else {
		if (modulatorsActive[0]) {
			renderSineWaveWithFeedback(modulatorScratch, numSamples, &voice.unisonParts[u].modulatorPhase[0],
			                           voice.modulatorAmplitudeLastTime[0], phaseIncrementModulator[0],
			                           voice.params.paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK],
			                           &voice.unisonParts[u].modulatorFeedback[0], false,
			                           modulatorAmplitudeIncrements[0]);
		}
		else {
noModulatorsActive:
			for (int32_t s = 0; s < kNumSources; s++) {
				if (sourceAmplitudes[s]) {
					renderSineWaveWithFeedback(
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
			renderFMWithFeedbackAdd(fmOscBuffer, numSamples, modulatorScratch, &voice.unisonParts[u].sources[s].oscPos,
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

void FMEngine::renderSineWaveWithFeedback(int32_t* bufferStart, int32_t numSamples, uint32_t* phase, int32_t amplitude,
                                          uint32_t phaseIncrement, int32_t feedbackAmount, int32_t* lastFeedbackValue,
                                          bool add, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t feedbackValue = *lastFeedbackValue;
		int32_t* bufferEnd = bufferStart + numSamples;
		do {
			amplitudeNow += amplitudeIncrement;
			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			feedbackValue = dsp::fixed::sine_fm_operator(phaseNow += phaseIncrement, feedback);

			if (add) {
				*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, feedbackValue, amplitudeNow);
			}
			else {
				*thisSample = multiply_32x32_rshift32(feedbackValue, amplitudeNow);
			}
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;

		if (amplitudeIncrement) {
			do {
				auto phaseVector = Argon<uint32_t>{phaseNow}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phaseIncrement);
				phaseNow += phaseIncrement * 4;
				int32x4_t sineValueVector = dsp::fixed::sine(phaseVector);

				int32x4_t amplitudeVector;
				for (int32_t i = 0; i < 4; i++) {
					amplitudeNow += amplitudeIncrement;
					amplitudeVector[i] = amplitudeNow >> 1;
				}

				int32x4_t resultValueVector = vqdmulhq_s32(amplitudeVector, sineValueVector);

				if (add) {
					int32x4_t existingBufferContents = vld1q_s32(thisSample);
					int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
					vst1q_s32(thisSample, added);
				}
				else {
					vst1q_s32(thisSample, resultValueVector);
				}

				thisSample += 4;
			} while (thisSample < bufferEnd);
		}

		else {
			do {
				auto phaseVector = Argon<uint32_t>{phaseNow}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phaseIncrement);
				phaseNow += phaseIncrement * 4;
				int32x4_t sineValueVector = dsp::fixed::sine(phaseVector);
				int32x4_t resultValueVector = vqrdmulhq_n_s32(sineValueVector, amplitudeNow >> 1);

				if (add) {
					int32x4_t existingBufferContents = vld1q_s32(thisSample);
					int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
					vst1q_s32(thisSample, added);
				}
				else {
					vst1q_s32(thisSample, resultValueVector);
				}

				thisSample += 4;
			} while (thisSample < bufferEnd);
		}
	}
}

void FMEngine::renderFMWithFeedback(int32_t* bufferStart, int32_t numSamples, int32_t* fmBuffer, uint32_t* phase,
                                    int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
                                    int32_t* lastFeedbackValue, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;

		int32_t feedbackValue = *lastFeedbackValue;
		do {
			amplitudeNow += amplitudeIncrement;

			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			uint32_t sum = (uint32_t)*thisSample + (uint32_t)feedback;

			feedbackValue = dsp::fixed::sine_fm_operator(phaseNow += phaseIncrement, sum);
			*thisSample = multiply_32x32_rshift32(feedbackValue, amplitudeNow);
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;
		do {
			amplitudeNow += amplitudeIncrement;
			int32_t fmValue = dsp::fixed::sine_fm_operator(phaseNow += phaseIncrement, *thisSample);
			*thisSample = multiply_32x32_rshift32(fmValue, amplitudeNow);
		} while (++thisSample != bufferEnd);
	}
}

void FMEngine::renderFMWithFeedbackAdd(int32_t* bufferStart, int32_t numSamples, int32_t* fmBuffer, uint32_t* phase,
                                       int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
                                       int32_t* lastFeedbackValue, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* fmSample = fmBuffer;
		int32_t* bufferEnd = bufferStart + numSamples;

		int32_t feedbackValue = *lastFeedbackValue;
		do {
			amplitudeNow += amplitudeIncrement;

			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			uint32_t sum = (uint32_t)*(fmSample++) + (uint32_t)feedback;

			feedbackValue = dsp::fixed::sine_fm_operator(phaseNow += phaseIncrement, sum);
			*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, feedbackValue, amplitudeNow);
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;
		uint32_t* fmSample = (uint32_t*)fmBuffer;
		int32_t* bufferPreEnd = bufferEnd - 4;

		uint32x4_t phaseVector;
		for (int32_t i = 0; i < 4; i++) {
			phaseNow += phaseIncrement;
			phaseVector[i] = phaseNow;
		}

		uint32x4_t phaseIncrementVector = vdupq_n_u32(phaseIncrement << 2);

		if (amplitudeIncrement) {
			while (true) {
				uint32x4_t phaseShift = vld1q_u32(fmSample);
				int32x4_t sineValueVector =
				    dsp::fixed::sine(Argon<uint32_t>{phaseVector} + (Argon<uint32_t>{phaseShift} << 8));

				int32x4_t amplitudeVector;
				for (int32_t i = 0; i < 4; i++) {
					amplitudeNow += amplitudeIncrement;
					amplitudeVector[i] = amplitudeNow >> 1;
				}

				int32x4_t resultValueVector = vqdmulhq_s32(amplitudeVector, sineValueVector);

				int32x4_t existingBufferContents = vld1q_s32(thisSample);
				int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
				vst1q_s32(thisSample, added);

				if (thisSample >= bufferPreEnd) {
					break;
				}

				thisSample += 4;
				fmSample += 4;
				phaseVector = vaddq_u32(phaseVector, phaseIncrementVector);
			}
		}

		else {
			while (true) {
				uint32x4_t phaseShift = vld1q_u32(fmSample);
				int32x4_t sineValueVector =
				    dsp::fixed::sine(Argon<uint32_t>{phaseVector} + (Argon<uint32_t>{phaseShift} << 8));

				int32x4_t resultValueVector = vqrdmulhq_n_s32(sineValueVector, amplitudeNow >> 1);

				int32x4_t existingBufferContents = vld1q_s32(thisSample);
				int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
				vst1q_s32(thisSample, added);

				if (thisSample >= bufferPreEnd) {
					break;
				}

				thisSample += 4;
				fmSample += 4;
				phaseVector = vaddq_u32(phaseVector, phaseIncrementVector);
			}
		}
	}
}
