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

#include <sound.h>
#include "FilterSet.h"
#include "functions.h"
#include "FilterSetConfig.h"
#include "TimeStretcher.h"
#include "storagemanager.h"

FilterSet::FilterSet() {
}

void FilterSet::renderHPF(int32_t* outputSample, FilterSetConfig* filterSetConfig, int extraSaturation) {
	int32_t input = *outputSample;

	int32_t firstHPFOutput = input - hpfHPF1.doFilter(input, filterSetConfig->hpfMoveability);

	int32_t feedbacksValue = hpfHPF3.getFeedbackOutput(filterSetConfig->hpfHPF3Feedback)
	                         + hpfLPF1.getFeedbackOutput(filterSetConfig->hpfLPF1Feedback);

	int32_t a =
	    multiply_32x32_rshift32_rounded(filterSetConfig->divideByTotalMoveability, firstHPFOutput + feedbacksValue)
	    << (4 + 1);

	// Only saturate / anti-alias if lots of resonance
	if (filterSetConfig->hpfProcessedResonance > 900000000) { // 890551738
		a = getTanHAntialiased(a, &hpfLastWorkingValue, 2 + extraSaturation);
	}
	else {
		hpfLastWorkingValue = (uint32_t)lshiftAndSaturate<2>(a) + 2147483648u;
		if (filterSetConfig->hpfProcessedResonance > 750000000) { // 400551738
			a = getTanHUnknown(a, 2 + extraSaturation);
		}
	}

	hpfLPF1.doFilter(a - hpfHPF3.doFilter(a, filterSetConfig->hpfMoveability), filterSetConfig->hpfMoveability);

	a = multiply_32x32_rshift32_rounded(a, filterSetConfig->hpfDivideByProcessedResonance) << (8 - 1); // Normalization

	*outputSample = a;
}

#define HPF_LONG_SATURATION 3

void FilterSet::renderHPFLong(int32_t* outputSample, int32_t* endSample, FilterSetConfig* filterSetConfig,
                              int numSamples, int sampleIncrement) {

	bool needToFixSaturation = false;

	if (!hpfDoingAntialiasingNow && filterSetConfig->hpfDoAntialiasing) {
		needToFixSaturation = true;
	}

	hpfDoingAntialiasingNow = filterSetConfig->hpfDoAntialiasing;

	if (!hpfOnLastTime) {
		hpfOnLastTime = true;
		hpfDivideByTotalMoveabilityLastTime = filterSetConfig->divideByTotalMoveability;
		hpfDivideByProcessedResonanceLastTime = filterSetConfig->hpfDivideByProcessedResonance;
		needToFixSaturation = true;
		hpfHPF1.reset();
		hpfLPF1.reset();
		hpfHPF3.reset();
	}

	int32_t hpfDivideByTotalMoveabilityNow = hpfDivideByTotalMoveabilityLastTime;
	int32_t hpfDivideByTotalMoveabilityIncrement =
	    (int32_t)(filterSetConfig->divideByTotalMoveability - hpfDivideByTotalMoveabilityNow) / (int32_t)numSamples;
	hpfDivideByTotalMoveabilityLastTime = filterSetConfig->divideByTotalMoveability;

	int32_t hpfDivideByProcessedResonanceNow = hpfDivideByProcessedResonanceLastTime;
	int32_t hpfDivideByProcessedResonanceIncrement =
	    (int32_t)(filterSetConfig->hpfDivideByProcessedResonance - hpfDivideByProcessedResonanceNow)
	    / (int32_t)numSamples;
	hpfDivideByProcessedResonanceLastTime = filterSetConfig->hpfDivideByProcessedResonance;

	do {

		int32_t input = *outputSample;

		int32_t firstHPFOutput = input - hpfHPF1.doFilter(input, filterSetConfig->hpfMoveability);

		int32_t feedbacksValue = hpfHPF3.getFeedbackOutput(filterSetConfig->hpfHPF3Feedback)
		                         + hpfLPF1.getFeedbackOutput(filterSetConfig->hpfLPF1Feedback);

		hpfDivideByTotalMoveabilityNow += hpfDivideByTotalMoveabilityIncrement;
		int32_t a = multiply_32x32_rshift32_rounded(hpfDivideByTotalMoveabilityNow, firstHPFOutput + feedbacksValue)
		            << (4 + 1);

		// Only saturate / anti-alias if lots of resonance
		if (hpfDoingAntialiasingNow) { // 890551738
			if (needToFixSaturation) {
				hpfLastWorkingValue = (uint32_t)lshiftAndSaturate<HPF_LONG_SATURATION>(a) + 2147483648u;
				needToFixSaturation = false;
			}
			a = getTanHAntialiased(a, &hpfLastWorkingValue, HPF_LONG_SATURATION);
		}
		else {
			if (filterSetConfig->hpfProcessedResonance > 750000000) { // 400551738
				a = getTanH<HPF_LONG_SATURATION>(a);
			}
		}

		hpfLPF1.doFilter(a - hpfHPF3.doFilter(a, filterSetConfig->hpfMoveability), filterSetConfig->hpfMoveability);

		hpfDivideByProcessedResonanceNow += hpfDivideByProcessedResonanceIncrement;
		a = multiply_32x32_rshift32_rounded(a, hpfDivideByProcessedResonanceNow) << (8 - 1); // Normalization

		*outputSample = a;
		outputSample += sampleIncrement;
	} while (outputSample < endSample);
}

inline int32_t FilterSet::do24dBLPFOnSample(int32_t input, FilterSetConfig* filterSetConfig, int saturationLevel) {

	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	int32_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
	int32_t distanceToGo = noise - noiseLastValue;
	noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
	int32_t moveability =
	    filterSetConfig->moveability + multiply_32x32_rshift32(filterSetConfig->moveability, noiseLastValue);

	int32_t feedbacksSum = (lpfLPF1.getFeedbackOutputWithoutLshift(filterSetConfig->lpf1Feedback)
	                        + lpfLPF2.getFeedbackOutputWithoutLshift(filterSetConfig->lpf2Feedback)
	                        + lpfLPF3.getFeedbackOutputWithoutLshift(filterSetConfig->lpf3Feedback)
	                        + lpfLPF4.getFeedbackOutputWithoutLshift(filterSetConfig->divideBy1PlusTannedFrequency))
	                       << 2;

	// Note: in the line above, we "should" halve filterSetConfig->divideBy1plusg to get it into the 1=1073741824 range. But it doesn't sound as good.
	// Primarily it stops us getting to full resonance. But even if we allow further resonance increase, the sound just doesn't quite compare.
	// Lucky I discovered this by mistake

	int32_t x = multiply_32x32_rshift32_rounded(
	                (input - (multiply_32x32_rshift32_rounded(feedbacksSum, filterSetConfig->processedResonance) << 3)),
	                filterSetConfig->divideByTotalMoveabilityAndProcessedResonance)
	            << 2;

	// Only saturate if resonance is high enough. Surprisingly, saturation makes no audible difference until very near the point of feedback
	if (saturationLevel) {
		x = getTanHUnknown(x, saturationLevel);
	}

	return lpfLPF4.doFilter(
	           lpfLPF3.doFilter(lpfLPF2.doFilter(lpfLPF1.doFilter(x, moveability), moveability), moveability),
	           moveability)
	       << 1;
}

inline int32_t FilterSet::doDriveLPFOnSample(int32_t input, FilterSetConfig* filterSetConfig, int extraSaturation) {

	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	int32_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
	int32_t distanceToGo = noise - noiseLastValue;
	noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
	int32_t moveability =
	    filterSetConfig->moveability + multiply_32x32_rshift32(filterSetConfig->moveability, noiseLastValue);

	int32_t feedbacksSum = (lpfLPF1.getFeedbackOutputWithoutLshift(filterSetConfig->lpf1Feedback)
	                        + lpfLPF2.getFeedbackOutputWithoutLshift(filterSetConfig->lpf2Feedback)
	                        + lpfLPF3.getFeedbackOutputWithoutLshift(filterSetConfig->lpf3Feedback)
	                        + lpfLPF4.getFeedbackOutputWithoutLshift(filterSetConfig->divideBy1PlusTannedFrequency))
	                       << 2;

	// Note: in the line above, we "should" halve filterSetConfig->divideBy1plusg to get it into the 1=1073741824 range. But it doesn't sound as good.
	// Primarily it stops us getting to full resonance. But even if we allow further resonance increase, the sound just doesn't quite compare.
	// Lucky I discovered this by mistake

	// Saturate feedback
	feedbacksSum = getTanHUnknown(feedbacksSum, 6 + extraSaturation);

	// We don't saturate the input anymore, because that's the place where we'd get the most aliasing!
	int32_t x = multiply_32x32_rshift32_rounded(
	                (input - (multiply_32x32_rshift32_rounded(feedbacksSum, filterSetConfig->processedResonance) << 3)),
	                filterSetConfig->divideByTotalMoveabilityAndProcessedResonance)
	            << 2;

	int32_t a = lpfLPF1.doFilter(x, moveability);

	int32_t b = lpfLPF2.doFilter(a, moveability);

	int32_t c = lpfLPF3.doFilter(b, moveability);

	int32_t d = lpfLPF4.doFilter(c, moveability) << 1;

	return d;
}

void FilterSet::renderLPFLong(int32_t* startSample, int32_t* endSample, FilterSetConfig* filterSetConfig,
                              uint8_t lpfMode, int sampleIncrement, int extraSaturation, int extraSaturationDrive) {

	// This should help get rid of crackling on start / stop - but doesn't
	if (!lpfOnLastTime) {
		lpfOnLastTime = true;
		lpfLPF1.reset();
		lpfLPF2.reset();
		lpfLPF3.reset();
		lpfLPF4.reset();
		svf.reset();
	}

	// Half ladder
	if (lpfMode == LPF_MODE_12DB) {

		int32_t* currentSample = startSample;
		do {

			// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
			int32_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
			int32_t distanceToGo = noise - noiseLastValue;
			noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
			int32_t moveability =
			    filterSetConfig->moveability + multiply_32x32_rshift32(filterSetConfig->moveability, noiseLastValue);

			int32_t feedbacksSum = lpfLPF1.getFeedbackOutput(filterSetConfig->lpf1Feedback)
			                       + lpfLPF2.getFeedbackOutput(filterSetConfig->lpf2Feedback)
			                       + lpfLPF3.getFeedbackOutput(filterSetConfig->divideBy1PlusTannedFrequency);
			int32_t x =
			    multiply_32x32_rshift32_rounded(
			        (*currentSample
			         - (multiply_32x32_rshift32_rounded(feedbacksSum, filterSetConfig->processedResonance) << 3)),
			        filterSetConfig->divideByTotalMoveabilityAndProcessedResonance)
			    << 2;

			// Only saturate if resonance is high enough. Surprisingly, saturation makes no audible difference until very near the point of feedback
			if (true || filterSetConfig->processedResonance > 510000000) { // Re-check this?
				x = getTanHUnknown(x, 1 + extraSaturation);                // Saturation
			}

			*currentSample = lpfLPF3.doAPF(lpfLPF2.doFilter(lpfLPF1.doFilter(x, moveability), moveability), moveability)
			                 << 1;

			currentSample += sampleIncrement;
		} while (currentSample < endSample);
	}

	// Full ladder (regular)
	else if (lpfMode == LPF_MODE_TRANSISTOR_24DB) {

		// Only saturate if resonance is high enough
		if (filterSetConfig->processedResonance
		    > 900000000) { // Careful - pushing this too high leads to crackling, only at the highest frequencies, and at the top of the non-saturating resonance range
			int32_t* currentSample = startSample;
			do {
				*currentSample = do24dBLPFOnSample(*currentSample, filterSetConfig, 1 + extraSaturation);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}

		else {
			int32_t* currentSample = startSample;
			do {
				*currentSample = do24dBLPFOnSample(*currentSample, filterSetConfig, 0);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}
	}

	// Full ladder (drive)
	else if (lpfMode == LPF_MODE_TRANSISTOR_24DB_DRIVE) {

		if (filterSetConfig->doOversampling) {
			int32_t* currentSample = startSample;
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

				doDriveLPFOnSample(*currentSample, filterSetConfig, extraSaturationDrive);

				// Crude downsampling - just take every second sample, with no anti-aliasing filter. Works fine cos the ladder LPF filter takes care of lots of those high harmonics!
				int32_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, filterSetConfig, extraSaturationDrive);

				// Only perform the final saturation stage on this one sample, which we want to keep
				*currentSample = getTanHUnknown(outputSampleToKeep, 3 + extraSaturationDrive);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}

		else {
			int32_t* currentSample = startSample;
			do {
				int32_t outputSampleToKeep = doDriveLPFOnSample(*currentSample, filterSetConfig, extraSaturationDrive);
				*currentSample = getTanHUnknown(outputSampleToKeep, 3 + extraSaturationDrive);

				currentSample += sampleIncrement;
			} while (currentSample < endSample);
		}
	}
	else if (lpfMode == LPF_MODE_SVF) {

		int32_t* currentSample = startSample;
		do {
			SVF_outs outs = svf.doSVF(*currentSample, filterSetConfig);
			*currentSample = outs.lpf << 1;

			currentSample += sampleIncrement;
		} while (currentSample < endSample);
	}
}

void FilterSet::reset() {
	lpfLPF1.reset();
	lpfLPF2.reset();
	lpfLPF3.reset();
	lpfLPF4.reset();

	hpfHPF1.reset();
	hpfLPF1.reset();
	hpfHPF3.reset();
	hpfLastWorkingValue = 2147483648;
	hpfDoingAntialiasingNow = false;
	hpfOnLastTime = false;
	svf.reset();
	lpfOnLastTime = false;
	noiseLastValue = 0;
}

SVF_outs SVFilter::doSVF(int32_t input, FilterSetConfig* filterSetConfig) {
	int32_t high;
	int32_t notch;
	int32_t f = filterSetConfig->moveability;
	//raw resonance is 0-2, e.g. 1 is 1073741824
	int32_t q = filterSetConfig->lpfRawResonance;
	f = add_saturation(f, (f >> 2)); //arbitrary to adjust range on gold knob
	f = add_saturation(f, 26508640); //slightly under the cutoff for C0
	//processed resonance is 2-rawresonance^2
	//compensate for resonance by lowering input level
	int32_t in = 2147483647 - filterSetConfig->processedResonance;

	low = low + multiply_32x32_rshift32(f, band);

	high = add_saturation((multiply_32x32_rshift32(input, in) << 1), 0 - low);
	high = add_saturation(high, 0 - (multiply_32x32_rshift32(q, band) << 3));
	band = multiply_32x32_rshift32(f, high) + band;

	//saturate band feedback
	band = getTanHUnknown(band, 3);
	notch = high + low;
	SVF_outs result = {low, band, high, notch};
	return result;
}
