/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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
#include "dsp/filter/lpladder.h"
#include "definitions_cxx.hpp"
#include "dsp/filter/svf.h"
#include "io/debug/print.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <cstdint>

const int16_t resonanceThresholdsForOversampling[] = {
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, // 48
    16384, // 49
    16384, // 50
    16384,
    15500, // 52
    20735, // 14848,
    17000, // 12800, // 54
    9000,  // 4300,
    9000,  // 56
    9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,
};

const int16_t resonanceLimitTable[] = {
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, // 48
    32767, // 49
    32767, // 50
    32767,
    28415, //30000, // 52
    20000, //23900,
    17000, //19000, // 54
    17000, //19000,
    17000, //19000, // 56
    17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000,
};

q31_t LpLadderFilter::set_config(q31_t lpfFrequency, q31_t lpfResonance, LPFMode lpfMode, q31_t filterGain) {

	// Hot transistor ladder - needs oversampling and stuff
	if (lpfMode == LPFMode::TRANSISTOR_24DB_DRIVE) {

		int32_t resonance = ONE_Q31 - (lpfResonance << 2); // Limits it
		processedResonance = ONE_Q31 - resonance;          // Always between 0 and 2. 1 represented as 1073741824

		int32_t logFreq = quickLog(lpfFrequency);

		doOversampling = false; //storageManager.devVarA;

		logFreq = std::min(logFreq, (int32_t)63 << 24);

		if (AudioEngine::cpuDireness < 14 && (logFreq >> 24) > 51) {
			int32_t resonanceThreshold = interpolateTableSigned(logFreq, 30, resonanceThresholdsForOversampling, 6);
			doOversampling = (processedResonance > resonanceThreshold);
		}

		if (doOversampling) {

			lpfFrequency >>= 1;

			logFreq -= 33554432;

			// Adjustment for how the oversampling shifts the frequency just slightly
			lpfFrequency -= (multiply_32x32_rshift32_rounded(logFreq, lpfFrequency) >> 8)
			                * 34; // + (lpfFrequency >> 8) * storageManager.devVarB;

			// Enforce a max frequency. Otherwise we'll generate stuff which will cause problems for down-sampling again.
			// But only if resonance is high. If it's low, we need to be able to get the freq high, to let all the HF through that we want to hear
			lpfFrequency = std::min((int32_t)39056384, lpfFrequency);

			int32_t resonanceLimit = interpolateTableSigned(logFreq, 30, resonanceLimitTable, 6);

			processedResonance = std::min(processedResonance, resonanceLimit);
		}
	}

	// Between 0 and 8, by my making. 1 represented by 268435456
	int32_t tannedFrequency = instantTan(lshiftAndSaturate<5>(lpfFrequency));

	// Cold transistor ladder
	if ((lpfMode == LPFMode::TRANSISTOR_24DB) || (lpfMode == LPFMode::TRANSISTOR_12DB)) {
		// Some long-winded stuff to make it so if frequency goes really low, resonance goes down. This is tuned a bit, but isn't perfect
		int32_t howMuchTooLow = 0;
		if (tannedFrequency < 6000000) {
			howMuchTooLow = 6000000 - tannedFrequency;
		}

		int32_t howMuchToKeep = ONE_Q31 - 1 * 33;

		int32_t resonanceUpperLimit = 510000000; // Prone to feeding back lots
		tannedFrequency = std::max(
		    tannedFrequency,
		    (int32_t)540817); // We really want to keep the frequency from going lower than it has to - it causes problems

		int32_t resonance = ONE_Q31 - (std::min(lpfResonance, resonanceUpperLimit) << 2); // Limits it
		resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
		processedResonance =
		    ONE_Q31 - resonance; //ONE_Q31 - rawResonance2; // Always between 0 and 2. 1 represented as 1073741824
		processedResonance = multiply_32x32_rshift32_rounded(processedResonance, howMuchToKeep) << 1;
	}

	divideBy1PlusTannedFrequency =
	    (int64_t)2147483648u * 134217728
	    / (134217728 + (tannedFrequency >> 1)); // Between ~0.1 and 1. 1 represented by 2147483648
	moveability = multiply_32x32_rshift32_rounded(tannedFrequency, divideBy1PlusTannedFrequency)
	              << 4; // Between 0 and 1. 1 represented by 2147483648 I'm pretty sure

	// Half ladder
	if (lpfMode == LPFMode::TRANSISTOR_12DB) {
		int32_t moveabilityNegative = moveability - 1073741824; // Between -2 and 0. 1 represented as 1073741824
		lpf2Feedback = multiply_32x32_rshift32_rounded(moveabilityNegative, divideBy1PlusTannedFrequency) << 1;
		lpf1Feedback = multiply_32x32_rshift32_rounded(lpf2Feedback, moveability) << 1;
		divideByTotalMoveabilityAndProcessedResonance =
		    (int64_t)67108864 * 1073741824
		    / (67108864
		       + multiply_32x32_rshift32_rounded(
		           processedResonance,
		           multiply_32x32_rshift32_rounded(moveabilityNegative,
		                                           multiply_32x32_rshift32_rounded(moveability, moveability))));
	}

	// Full ladder
	else {
		lpf3Feedback = multiply_32x32_rshift32_rounded(divideBy1PlusTannedFrequency, moveability);
		lpf2Feedback = multiply_32x32_rshift32_rounded(lpf3Feedback, moveability) << 1;
		lpf1Feedback = multiply_32x32_rshift32_rounded(lpf2Feedback, moveability) << 1;
		int32_t onePlusThing =
		    67108864
		    + (multiply_32x32_rshift32_rounded(
		        moveability,
		        multiply_32x32_rshift32_rounded(
		            moveability, multiply_32x32_rshift32_rounded(
		                             moveability, multiply_32x32_rshift32_rounded(
		                                              moveability, processedResonance))))); // 1 represented as 67108864
		divideByTotalMoveabilityAndProcessedResonance = (int64_t)67108864 * 1073741824 / onePlusThing;
	}

	if (lpfMode != LPFMode::TRANSISTOR_24DB_DRIVE) { // Cold transistor ladder only
		// Extra feedback - but only if freq isn't too high. Otherwise we get aliasing
		if (tannedFrequency <= 304587486) {
			processedResonance = multiply_32x32_rshift32_rounded(processedResonance, 1150000000) << 1;
		}
		else {
			processedResonance >>= 1;
		}

		int32_t a = std::min(lpfResonance, (int32_t)536870911);
		a = 536870912 - a;
		a = multiply_32x32_rshift32(a, a) << 3;
		a = 536870912 - a;
		int32_t gainModifier = 268435456 + a;
		filterGain = multiply_32x32_rshift32(filterGain, gainModifier) << 3;
	}

	// Drive filter - increase output amplitude
	else {
		//overallOscAmplitude <<= 2;
		filterGain *= 0.8;
	}
	return filterGain;
}

void LpLadderFilter::do_filter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement, int32_t extraSaturation) {

	// Half ladder
	if (lpfMode == LPFMode::TRANSISTOR_12DB) {

		q31_t* currentSample = startSample;
		do {

			// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
			q31_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
			q31_t distanceToGo = noise - noiseLastValue;
			noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
			q31_t noisy_m = moveability + multiply_32x32_rshift32(moveability, noiseLastValue);

			q31_t feedbacksSum = lpfLPF1.getFeedbackOutput(lpf1Feedback) + lpfLPF2.getFeedbackOutput(lpf2Feedback)
			                     + lpfLPF3.getFeedbackOutput(divideBy1PlusTannedFrequency);
			q31_t x = multiply_32x32_rshift32_rounded(
			              (*currentSample - (multiply_32x32_rshift32_rounded(feedbacksSum, processedResonance) << 3)),
			              divideByTotalMoveabilityAndProcessedResonance)
			          << 2;

			// Only saturate if resonance is high enough. Surprisingly, saturation makes no audible difference until very near the point of feedback
			if (true || processedResonance > 510000000) {   // Re-check this?
				x = getTanHUnknown(x, 1 + extraSaturation); // Saturation
			}

			*currentSample = lpfLPF3.doAPF(lpfLPF2.doFilter(lpfLPF1.doFilter(x, noisy_m), noisy_m), noisy_m) << 1;

			currentSample += sampleIncrement;
		} while (currentSample < endSample);
	}

	// Full ladder (regular)
	else if (lpfMode == LPFMode::TRANSISTOR_24DB) {

		// Only saturate if resonance is high enough
		if (processedResonance
		    > 900000000) { // Careful - pushing this too high leads to crackling, only at the highest frequencies, and at the top of the non-saturating resonance range
			q31_t* currentSample = startSample;
			do {
				*currentSample = do24dBLPFOnSample(*currentSample, 1 + extraSaturation);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}

		else {
			q31_t* currentSample = startSample;
			do {
				*currentSample = do24dBLPFOnSample(*currentSample, 0);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}
	}

	// Full ladder (drive)
	else if (lpfMode == LPFMode::TRANSISTOR_24DB_DRIVE) {
		int32_t extraSaturationDrive = extraSaturation >> 1;
		if (doOversampling) {
			q31_t* currentSample = startSample;
			do {
				// Linear interpolation works surprisingly well here - it doesn't lead to audible aliasing. But its big problem is that it kills the highest frequencies,
				// which is especially noticeable when resonance is low. This is because it'll turn all your high sine waves into triangles whose fundamental is lower in amplitude.
				// Now, if we immediately downsampled that again with no filtering, no problem, because those new really high harmonics that make it triangular are still there.
				// But after running it through the ladder filter, those get doubly filtered out, leaving just the reduced-amplitude (fundamental) sine waves where our old ones were.
				// So instead, we need to do just a little bit more and take one extra, previous sample into account in our interpolation. This is enough to make the HF loss inaudible
				// - although we can still see it on the spectrum analysis.

				// Insanely just doubling up our input values to oversample works better than fancy 3-sample interpolation.
				// And even, making our "interpolated" sample just a 0 and doubling the amplitude of the actual sample works very nearly as well as this,
				// but gives a little bit more aliasing on high notes fed in.

				doDriveLPFOnSample(*currentSample, extraSaturationDrive);

				// Crude downsampling - just take every second sample, with no anti-aliasing filter. Works fine cos the ladder LPF filter takes care of lots of those high harmonics!
				q31_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, extraSaturationDrive);

				// Only perform the final saturation stage on this one sample, which we want to keep
				*currentSample = getTanHUnknown(outputSampleToKeep, 3 + extraSaturationDrive);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}

		else {
			q31_t* currentSample = startSample;
			do {
				q31_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, extraSaturationDrive);
				*currentSample = getTanHUnknown(outputSampleToKeep, 3 + extraSaturationDrive);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}
	}
}

inline q31_t LpLadderFilter::do24dBLPFOnSample(q31_t input, int32_t saturationLevel) {

	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	q31_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
	q31_t distanceToGo = noise - noiseLastValue;
	noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
	q31_t noisy_m = moveability + multiply_32x32_rshift32(moveability, noiseLastValue);

	q31_t feedbacksSum =
	    (lpfLPF1.getFeedbackOutputWithoutLshift(lpf1Feedback) + lpfLPF2.getFeedbackOutputWithoutLshift(lpf2Feedback)
	     + lpfLPF3.getFeedbackOutputWithoutLshift(lpf3Feedback)
	     + lpfLPF4.getFeedbackOutputWithoutLshift(divideBy1PlusTannedFrequency))
	    << 2;

	// Note: in the line above, we "should" halve filterSetConfig->divideBy1plusg to get it into the 1=1073741824 range. But it doesn't sound as good.
	// Primarily it stops us getting to full resonance. But even if we allow further resonance increase, the sound just doesn't quite compare.
	// Lucky I discovered this by mistake

	q31_t x = multiply_32x32_rshift32_rounded(
	              (input - (multiply_32x32_rshift32_rounded(feedbacksSum, processedResonance) << 3)),
	              divideByTotalMoveabilityAndProcessedResonance)
	          << 2;

	// Only saturate if resonance is high enough. Surprisingly, saturation makes no audible difference until very near the point of feedback
	if (saturationLevel) {
		x = getTanHUnknown(x, saturationLevel);
	}

	return lpfLPF4.doFilter(lpfLPF3.doFilter(lpfLPF2.doFilter(lpfLPF1.doFilter(x, noisy_m), noisy_m), noisy_m), noisy_m)
	       << 1;
}

inline q31_t LpLadderFilter::doDriveLPFOnSample(q31_t input, int32_t extraSaturation) {

	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	q31_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
	q31_t distanceToGo = noise - noiseLastValue;
	noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
	q31_t noisy_m = moveability + multiply_32x32_rshift32(moveability, noiseLastValue);

	q31_t feedbacksSum =
	    (lpfLPF1.getFeedbackOutputWithoutLshift(lpf1Feedback) + lpfLPF2.getFeedbackOutputWithoutLshift(lpf2Feedback)
	     + lpfLPF3.getFeedbackOutputWithoutLshift(lpf3Feedback)
	     + lpfLPF4.getFeedbackOutputWithoutLshift(divideBy1PlusTannedFrequency))
	    << 2;

	// Note: in the line above, we "should" halve filterSetConfig->divideBy1plusg to get it into the 1=1073741824 range. But it doesn't sound as good.
	// Primarily it stops us getting to full resonance. But even if we allow further resonance increase, the sound just doesn't quite compare.
	// Lucky I discovered this by mistake

	// Saturate feedback
	feedbacksSum = getTanHUnknown(feedbacksSum, 6 + extraSaturation);

	// We don't saturate the input anymore, because that's the place where we'd get the most aliasing!
	q31_t x = multiply_32x32_rshift32_rounded(
	              (input - (multiply_32x32_rshift32_rounded(feedbacksSum, processedResonance) << 3)),
	              divideByTotalMoveabilityAndProcessedResonance)
	          << 2;

	q31_t a = lpfLPF1.doFilter(x, noisy_m);

	q31_t b = lpfLPF2.doFilter(a, noisy_m);

	q31_t c = lpfLPF3.doFilter(b, noisy_m);

	q31_t d = lpfLPF4.doFilter(c, noisy_m) << 1;

	return d;
}
