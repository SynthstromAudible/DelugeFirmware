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
#include "processing/engines/audio_engine.h"

namespace deluge::dsp::filter {
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
    28415, // 30000, // 52
    20000, // 23900,
    17000, // 19000, // 54
    17000, // 19000,
    17000, // 19000, // 56
    17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000,
};

q31_t LpLadderFilter::setConfig(q31_t lpfFrequency, q31_t lpfResonance, FilterMode lpfmode, q31_t lpfMorph,
                                q31_t filterGain) {
	lpfMode = lpfmode;
	morph = lpfMorph;
	// Hot transistor ladder - needs oversampling and stuff
	if (lpfMode == FilterMode::TRANSISTOR_24DB_DRIVE) {

		int32_t resonance = ONE_Q31 - (lpfResonance << 2); // Limits it
		processedResonance = ONE_Q31 - resonance;          // Always between 0 and 2. 1 represented as 1073741824

		int32_t logFreq = quickLog(lpfFrequency);

		doOversampling = false; // StorageManager::devVarA;

		logFreq = std::min(logFreq, (int32_t)63 << 24);

		if (AudioEngine::cpuDireness < 14 && (logFreq >> 24) > 51) {
			int32_t resonanceThreshold = interpolateTableSigned(logFreq, 30, resonanceThresholdsForOversampling, 6);
			doOversampling = (processedResonance > resonanceThreshold);
		}

		if (doOversampling) {

			lpfFrequency >>= 1;

			logFreq -= 33554432;

			// Adjustment for how the oversampling shifts the frequency just slightly
			lpfFrequency -= (multiply_32x32_rshift32_rounded(logFreq, lpfFrequency) >> 8) * 34;
			// + (lpfFrequency >> 8) * StorageManager::devVarB;

			// Enforce a max frequency. Otherwise we'll generate stuff which will cause problems for down-sampling
			// again. But only if resonance is high. If it's low, we need to be able to get the freq high, to let all
			// the HF through that we want to hear
			lpfFrequency = std::min((int32_t)39056384, lpfFrequency);

			int32_t resonanceLimit = interpolateTableSigned(logFreq, 30, resonanceLimitTable, 6);

			processedResonance = std::min(processedResonance, resonanceLimit);
		}
	}

	// Cold transistor ladder
	if ((lpfMode == FilterMode::TRANSISTOR_24DB) || (lpfMode == FilterMode::TRANSISTOR_12DB)) {
		// Some long-winded stuff to make it so if frequency goes really low, resonance goes down. This is tuned a bit,
		// but isn't perfect

		int32_t howMuchToKeep = ONE_Q31 - 1 * 33;

		int32_t resonanceUpperLimit = 510000000; // Prone to feeding back lots

		int32_t resonance = ONE_Q31 - (std::min(lpfResonance, resonanceUpperLimit) << 2); // Limits it
		resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;

		// ONE_Q31 - rawResonance2;
		// Always between 0 and 2. 1 represented as 1073741824
		processedResonance = ONE_Q31 - resonance;
		processedResonance = multiply_32x32_rshift32_rounded(processedResonance, howMuchToKeep) << 1;
	}

	curveFrequency(lpfFrequency);
	// for backwards compatibility, equivalent to prior lower limit on tan
	moveability = std::max(fc, (q31_t)4317840);
	// min moveability is 4317840
	//  Half ladder
	if (lpfMode == FilterMode::TRANSISTOR_12DB) {
		// Between -2 and 0. 1 represented as 1073741824
		int32_t moveabilityNegative = moveability - 1073741824;
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

		// 1 represented as 67108864
		int32_t onePlusThing =
		    67108864
		    + (multiply_32x32_rshift32_rounded(
		        moveability,
		        multiply_32x32_rshift32_rounded(
		            moveability, multiply_32x32_rshift32_rounded(
		                             moveability, multiply_32x32_rshift32_rounded(moveability, processedResonance)))));
		divideByTotalMoveabilityAndProcessedResonance = (q31_t)(72057594037927936.0 / (double)onePlusThing);
	}

	if (lpfMode != FilterMode::TRANSISTOR_24DB_DRIVE) { // Cold transistor ladder only
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
		// overallOscAmplitude <<= 2;
		filterGain *= 0.8;
	}
	return filterGain;
}

[[gnu::hot]] void LpLadderFilter::doFilter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {

	// Half ladder
	if (lpfMode == FilterMode::TRANSISTOR_12DB) {

		q31_t* currentSample = startSample;
		do {
			*currentSample = do12dBLPFOnSample(*currentSample, l);
			currentSample += sampleIncrement;
		} while (currentSample < endSample);
	}

	// Full ladder (regular)
	else if (lpfMode == FilterMode::TRANSISTOR_24DB) {

		q31_t* currentSample = startSample;
		do {
			*currentSample = do24dBLPFOnSample(*currentSample, l);

			currentSample += sampleIncrement;
		} while (currentSample < endSample);
	}

	// Full ladder (drive)
	else if (lpfMode == FilterMode::TRANSISTOR_24DB_DRIVE) {
		constexpr int32_t extraSaturationDrive = 1;
		if (doOversampling) {
			q31_t* currentSample = startSample;
			do {
				// Linear interpolation works surprisingly well here - it doesn't lead to audible aliasing. But its big
				// problem is that it kills the highest frequencies, which is especially noticeable when resonance is
				// low. This is because it'll turn all your high sine waves into triangles whose fundamental is lower in
				// amplitude. Now, if we immediately downsampled that again with no filtering, no problem, because those
				// new really high harmonics that make it triangular are still there. But after running it through the
				// ladder filter, those get doubly filtered out, leaving just the reduced-amplitude (fundamental) sine
				// waves where our old ones were. So instead, we need to do just a little bit more and take one extra,
				// previous sample into account in our interpolation. This is enough to make the HF loss inaudible
				// - although we can still see it on the spectrum analysis.

				// Insanely just doubling up our input values to oversample works better than fancy 3-sample
				// interpolation. And even, making our "interpolated" sample just a 0 and doubling the amplitude of the
				// actual sample works very nearly as well as this, but gives a little bit more aliasing on high notes
				// fed in.

				doDriveLPFOnSample(*currentSample, l);

				// Crude downsampling - just take every second sample, with no anti-aliasing filter. Works fine cos the
				// ladder LPF filter takes care of lots of those high harmonics!
				q31_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, l);

				// Only perform the final saturation stage on this one sample, which we want to keep
				*currentSample = getTanHUnknown(outputSampleToKeep, 4);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}

		else {
			q31_t* currentSample = startSample;
			do {
				q31_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, l);
				*currentSample = getTanHUnknown(outputSampleToKeep, 4);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}
	}
}
[[gnu::hot]] void LpLadderFilter::doFilterStereo(q31_t* startSample, q31_t* endSample) {

	// Half ladder
	if (lpfMode == FilterMode::TRANSISTOR_12DB) {

		q31_t* currentSample = startSample;
		do {
			*currentSample = do12dBLPFOnSample(*currentSample, l);
			currentSample += 1;
			*currentSample = do12dBLPFOnSample(*currentSample, r);
			currentSample += 1;
		} while (currentSample < endSample);
	}

	// Full ladder (regular)
	else if (lpfMode == FilterMode::TRANSISTOR_24DB) {
		q31_t* currentSample = startSample;
		do {
			*currentSample = do24dBLPFOnSample(*currentSample, l);

			currentSample += 1;
			*currentSample = do24dBLPFOnSample(*currentSample, r);

			currentSample += 1;
		} while (currentSample < endSample);
	}

	// Full ladder (drive)
	else if (lpfMode == FilterMode::TRANSISTOR_24DB_DRIVE) {
		if (doOversampling) {
			q31_t* currentSample = startSample;
			do {
				// Linear interpolation works surprisingly well here - it doesn't lead to audible aliasing. But its big
				// problem is that it kills the highest frequencies, which is especially noticeable when resonance is
				// low. This is because it'll turn all your high sine waves into triangles whose fundamental is lower in
				// amplitude. Now, if we immediately downsampled that again with no filtering, no problem, because those
				// new really high harmonics that make it triangular are still there. But after running it through the
				// ladder filter, those get doubly filtered out, leaving just the reduced-amplitude (fundamental) sine
				// waves where our old ones were. So instead, we need to do just a little bit more and take one extra,
				// previous sample into account in our interpolation. This is enough to make the HF loss inaudible
				// - although we can still see it on the spectrum analysis.

				// Insanely just doubling up our input values to oversample works better than fancy 3-sample
				// interpolation. And even, making our "interpolated" sample just a 0 and doubling the amplitude of the
				// actual sample works very nearly as well as this, but gives a little bit more aliasing on high notes
				// fed in.

				doDriveLPFOnSample(*currentSample, l);

				// Crude downsampling - just take every second sample, with no anti-aliasing filter. Works fine cos the
				// ladder LPF filter takes care of lots of those high harmonics!
				q31_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, l);

				// Only perform the final saturation stage on this one sample, which we want to keep
				*currentSample = getTanHUnknown(outputSampleToKeep, 4);

				currentSample += 1;
				doDriveLPFOnSample(*currentSample, r);

				// Crude downsampling - just take every second sample, with no anti-aliasing filter. Works fine cos the
				// ladder LPF filter takes care of lots of those high harmonics!
				outputSampleToKeep = doDriveLPFOnSample(*currentSample, r);

				// Only perform the final saturation stage on this one sample, which we want to keep
				*currentSample = getTanHUnknown(outputSampleToKeep, 4);

				currentSample += 1;
			} while (currentSample < endSample);
		}

		else {
			q31_t* currentSample = startSample;
			do {
				q31_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, l);
				*currentSample = getTanHUnknown(outputSampleToKeep, 4);

				currentSample += 1;
				outputSampleToKeep = doDriveLPFOnSample(*currentSample, r);
				*currentSample = getTanHUnknown(outputSampleToKeep, 4);

				currentSample += 1;
			} while (currentSample < endSample);
		}
	}
}
[[gnu::always_inline]] inline q31_t LpLadderFilter::do12dBLPFOnSample(q31_t input, LpLadderState& state) {
	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	q31_t noise = getNoise() >> 2; // StorageManager::devVarA;// 2;
	q31_t distanceToGo = noise - state.noiseLastValue;
	state.noiseLastValue += distanceToGo >> 7; // StorageManager::devVarB;
	q31_t noisy_m = moveability + multiply_32x32_rshift32(moveability, state.noiseLastValue);

	q31_t feedbacksSum = state.lpfLPF1.getFeedbackOutput(lpf1Feedback) + state.lpfLPF2.getFeedbackOutput(lpf2Feedback)
	                     + state.lpfLPF3.getFeedbackOutput(divideBy1PlusTannedFrequency);
	q31_t x = scaleInput(input, feedbacksSum);

	// Only saturate if resonance is high enough. Surprisingly, saturation makes no audible difference until very near
	// the point of feedback if (processedResonance > 510000000) { // Re-check this? 	x = getTanHUnknown(x, 1); //
	// Saturation
	// }

	return state.lpfLPF3.doAPF(state.lpfLPF2.doFilter(state.lpfLPF1.doFilter(x, noisy_m), noisy_m), noisy_m) << 1;
}
[[gnu::always_inline]] inline q31_t LpLadderFilter::do24dBLPFOnSample(q31_t input, LpLadderState& state) {

	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	q31_t noise = getNoise() >> 2; // StorageManager::devVarA;// 2;
	q31_t distanceToGo = noise - state.noiseLastValue;
	state.noiseLastValue += distanceToGo >> 7; // StorageManager::devVarB;
	q31_t noisy_m = moveability + multiply_32x32_rshift32(moveability, state.noiseLastValue);

	q31_t feedbacksSum = (state.lpfLPF1.getFeedbackOutputWithoutLshift(lpf1Feedback)
	                      + state.lpfLPF2.getFeedbackOutputWithoutLshift(lpf2Feedback)
	                      + state.lpfLPF3.getFeedbackOutputWithoutLshift(lpf3Feedback)
	                      + state.lpfLPF4.getFeedbackOutputWithoutLshift(divideBy1PlusTannedFrequency))
	                     << 2;

	// Note: in the line above, we "should" halve filterSetConfig->divideBy1plusg to get it into the 1=1073741824 range.
	// But it doesn't sound as good. Primarily it stops us getting to full resonance. But even if we allow further
	// resonance increase, the sound just doesn't quite compare. Lucky I discovered this by mistake

	q31_t x = scaleInput(input, feedbacksSum);

	// Only saturate if resonance is high enough. Surprisingly, saturation makes no audible difference until very near
	// the point of feedback

	// if (processedResonance > 900000000) {
	// 	x = getTanHUnknown(x, 1);
	// }

	return state.lpfLPF4.doFilter(
	           state.lpfLPF3.doFilter(state.lpfLPF2.doFilter(state.lpfLPF1.doFilter(x, noisy_m), noisy_m), noisy_m),
	           noisy_m)
	       << 1;
}

[[gnu::always_inline]] inline q31_t LpLadderFilter::doDriveLPFOnSample(q31_t input, LpLadderState& state) {

	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	q31_t noise = getNoise() >> 2; // StorageManager::devVarA;// 2;
	q31_t distanceToGo = noise - state.noiseLastValue;
	state.noiseLastValue += distanceToGo >> 7; // StorageManager::devVarB;
	q31_t noisy_m = moveability + multiply_32x32_rshift32(moveability, state.noiseLastValue);

	q31_t feedbacksSum = (state.lpfLPF1.getFeedbackOutputWithoutLshift(lpf1Feedback)
	                      + state.lpfLPF2.getFeedbackOutputWithoutLshift(lpf2Feedback)
	                      + state.lpfLPF3.getFeedbackOutputWithoutLshift(lpf3Feedback)
	                      + state.lpfLPF4.getFeedbackOutputWithoutLshift(divideBy1PlusTannedFrequency))
	                     << 2;

	// Note: in the line above, we "should" halve filterSetConfig->divideBy1plusg to get it into the 1=1073741824 range.
	// But it doesn't sound as good. Primarily it stops us getting to full resonance. But even if we allow further
	// resonance increase, the sound just doesn't quite compare. Lucky I discovered this by mistake

	// Saturate feedback
	feedbacksSum = getTanHUnknown(feedbacksSum, 7);

	// We don't saturate the input anymore, because that's the place where we'd get the most aliasing!
	q31_t x = scaleInput(input, feedbacksSum);

	q31_t a = state.lpfLPF1.doFilter(x, noisy_m);

	q31_t b = state.lpfLPF2.doFilter(a, noisy_m);

	q31_t c = state.lpfLPF3.doFilter(b, noisy_m);

	q31_t d = state.lpfLPF4.doFilter(c, noisy_m) << 1;

	return d;
}
} // namespace deluge::dsp::filter
