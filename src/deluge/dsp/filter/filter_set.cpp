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

#include "dsp/filter/filter_set.h"
#include "definitions_cxx.hpp"
#include "dsp/filter/filter_set_config.h"
#include "dsp/timestretch/time_stretcher.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

FilterSet::FilterSet() {
	lpsvfconfig = LPSVFConfig();
	lpladderconfig = LPLadderConfig();
	hpladderconfig = HPLadderConfig();
}

inline void FilterSet::renderLadderHPF(q31_t* outputSample, int32_t extraSaturation) {
	q31_t input = *outputSample;

	q31_t firstHPFOutput = input - hpfHPF1.doFilter(input, hpladderconfig.hpfMoveability);

	q31_t feedbacksValue = hpfHPF3.getFeedbackOutput(hpladderconfig.hpfHPF3Feedback)
	                       + hpfLPF1.getFeedbackOutput(hpladderconfig.hpfLPF1Feedback);

	q31_t a = multiply_32x32_rshift32_rounded(hpladderconfig.divideByTotalMoveability, firstHPFOutput + feedbacksValue)
	          << (4 + 1);

	// Only saturate / anti-alias if lots of resonance
	if (hpladderconfig.hpfProcessedResonance > 900000000) { // 890551738
		a = getTanHAntialiased(a, &hpfLastWorkingValue, 2 + extraSaturation);
	}
	else {
		hpfLastWorkingValue = (uint32_t)lshiftAndSaturate<2>(a) + 2147483648u;
		if (hpladderconfig.hpfProcessedResonance > 750000000) { // 400551738
			a = getTanHUnknown(a, 2 + extraSaturation);
		}
	}

	hpfLPF1.doFilter(a - hpfHPF3.doFilter(a, hpladderconfig.hpfMoveability), hpladderconfig.hpfMoveability);

	a = multiply_32x32_rshift32_rounded(a, hpladderconfig.hpfDivideByProcessedResonance) << (8 - 1); // Normalization

	*outputSample = a;
}

void FilterSet::renderHPFLong(q31_t* outputSample, q31_t* endSample, int32_t numSamples, int32_t sampleIncrement,
                              int32_t extraSaturation) {

	do {

		renderLadderHPF(outputSample, extraSaturation);
		outputSample += sampleIncrement;
	} while (outputSample < endSample);
}

inline q31_t FilterSet::do24dBLPFOnSample(q31_t input, int32_t saturationLevel) {

	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	q31_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
	q31_t distanceToGo = noise - noiseLastValue;
	noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
	q31_t moveability =
	    lpladderconfig.moveability + multiply_32x32_rshift32(lpladderconfig.moveability, noiseLastValue);

	q31_t feedbacksSum = (lpfLPF1.getFeedbackOutputWithoutLshift(lpladderconfig.lpf1Feedback)
	                      + lpfLPF2.getFeedbackOutputWithoutLshift(lpladderconfig.lpf2Feedback)
	                      + lpfLPF3.getFeedbackOutputWithoutLshift(lpladderconfig.lpf3Feedback)
	                      + lpfLPF4.getFeedbackOutputWithoutLshift(lpladderconfig.divideBy1PlusTannedFrequency))
	                     << 2;

	// Note: in the line above, we "should" halve filterSetConfig->divideBy1plusg to get it into the 1=1073741824 range. But it doesn't sound as good.
	// Primarily it stops us getting to full resonance. But even if we allow further resonance increase, the sound just doesn't quite compare.
	// Lucky I discovered this by mistake

	q31_t x = multiply_32x32_rshift32_rounded(
	              (input - (multiply_32x32_rshift32_rounded(feedbacksSum, lpladderconfig.processedResonance) << 3)),
	              lpladderconfig.divideByTotalMoveabilityAndProcessedResonance)
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

inline q31_t FilterSet::doDriveLPFOnSample(q31_t input, int32_t extraSaturation) {

	// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
	q31_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
	q31_t distanceToGo = noise - noiseLastValue;
	noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
	q31_t moveability =
	    lpladderconfig.moveability + multiply_32x32_rshift32(lpladderconfig.moveability, noiseLastValue);

	q31_t feedbacksSum = (lpfLPF1.getFeedbackOutputWithoutLshift(lpladderconfig.lpf1Feedback)
	                      + lpfLPF2.getFeedbackOutputWithoutLshift(lpladderconfig.lpf2Feedback)
	                      + lpfLPF3.getFeedbackOutputWithoutLshift(lpladderconfig.lpf3Feedback)
	                      + lpfLPF4.getFeedbackOutputWithoutLshift(lpladderconfig.divideBy1PlusTannedFrequency))
	                     << 2;

	// Note: in the line above, we "should" halve filterSetConfig->divideBy1plusg to get it into the 1=1073741824 range. But it doesn't sound as good.
	// Primarily it stops us getting to full resonance. But even if we allow further resonance increase, the sound just doesn't quite compare.
	// Lucky I discovered this by mistake

	// Saturate feedback
	feedbacksSum = getTanHUnknown(feedbacksSum, 6 + extraSaturation);

	// We don't saturate the input anymore, because that's the place where we'd get the most aliasing!
	q31_t x = multiply_32x32_rshift32_rounded(
	              (input - (multiply_32x32_rshift32_rounded(feedbacksSum, lpladderconfig.processedResonance) << 3)),
	              lpladderconfig.divideByTotalMoveabilityAndProcessedResonance)
	          << 2;

	q31_t a = lpfLPF1.doFilter(x, moveability);

	q31_t b = lpfLPF2.doFilter(a, moveability);

	q31_t c = lpfLPF3.doFilter(b, moveability);

	q31_t d = lpfLPF4.doFilter(c, moveability) << 1;

	return d;
}

void FilterSet::renderLPLadder(q31_t* startSample, q31_t* endSample, LPFMode lpfMode, int32_t sampleIncrement,
                               int32_t extraSaturation, int32_t extraSaturationDrive) {
	// Half ladder
	if (lpfMode == LPFMode::TRANSISTOR_12DB) {

		q31_t* currentSample = startSample;
		do {

			// For drive filter, apply some heavily lowpassed noise to the filter frequency, to add analog-ness
			q31_t noise = getNoise() >> 2; //storageManager.devVarA;// 2;
			q31_t distanceToGo = noise - noiseLastValue;
			noiseLastValue += distanceToGo >> 7; //storageManager.devVarB;
			q31_t moveability =
			    lpladderconfig.moveability + multiply_32x32_rshift32(lpladderconfig.moveability, noiseLastValue);

			q31_t feedbacksSum = lpfLPF1.getFeedbackOutput(lpladderconfig.lpf1Feedback)
			                     + lpfLPF2.getFeedbackOutput(lpladderconfig.lpf2Feedback)
			                     + lpfLPF3.getFeedbackOutput(lpladderconfig.divideBy1PlusTannedFrequency);
			q31_t x = multiply_32x32_rshift32_rounded(
			              (*currentSample
			               - (multiply_32x32_rshift32_rounded(feedbacksSum, lpladderconfig.processedResonance) << 3)),
			              lpladderconfig.divideByTotalMoveabilityAndProcessedResonance)
			          << 2;

			// Only saturate if resonance is high enough. Surprisingly, saturation makes no audible difference until very near the point of feedback
			if (true || lpladderconfig.processedResonance > 510000000) { // Re-check this?
				x = getTanHUnknown(x, 1 + extraSaturation);              // Saturation
			}

			*currentSample = lpfLPF3.doAPF(lpfLPF2.doFilter(lpfLPF1.doFilter(x, moveability), moveability), moveability)
			                 << 1;

			currentSample += sampleIncrement;
		} while (currentSample < endSample);
	}

	// Full ladder (regular)
	else if (lpfMode == LPFMode::TRANSISTOR_24DB) {

		// Only saturate if resonance is high enough
		if (lpladderconfig.processedResonance
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

		if (lpladderconfig.doOversampling) {
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

void FilterSet::renderLPSVF(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {
	q31_t* currentSample = startSample;
	do {
		SVF_outs outs = svf.doSVF(*currentSample, &lpsvfconfig);
		*currentSample = outs.lpf << 1;

		currentSample += sampleIncrement;
	} while (currentSample < endSample);
}
void FilterSet::renderLPFLong(q31_t* startSample, q31_t* endSample, LPFMode lpfMode, int32_t sampleIncrement,
                              int32_t extraSaturation, int32_t extraSaturationDrive) {

	// This should help get rid of crackling on start / stop - but doesn't
	if (!lpfOnLastTime) {
		lpfOnLastTime = true;
		lpfLPF1.reset();
		lpfLPF2.reset();
		lpfLPF3.reset();
		lpfLPF4.reset();
		svf.reset();
	}

	if (lpfMode == LPFMode::SVF) {
		renderLPSVF(startSample, endSample, sampleIncrement);
	}
	else {
		renderLPLadder(startSample, endSample, lpfMode, sampleIncrement, extraSaturation, extraSaturationDrive);
	}
}

int32_t FilterSet::set_config(int32_t lpfFrequency, int32_t lpfResonance, bool doLPF, int32_t hpfFrequency,
                              int32_t hpfResonance, bool doHPF, LPFMode lpfType, int32_t filterGain,
                              bool adjustVolumeForHPFResonance, int32_t* overallOscAmplitude) {
	LPFOn = doLPF;
	HPFOn = doHPF;
	lpfMode = lpfType;

	hpfResonance =
	    (hpfResonance >> 21) << 21; // Insanely, having changes happen in the small bytes too often causes rustling

	if (LPFOn) {
		if (lpfMode == LPFMode::SVF) {
			filterGain = lpsvfconfig.init(lpfFrequency, lpfResonance, lpfMode, filterGain);
		}
		else {
			filterGain = lpladderconfig.init(lpfFrequency, lpfResonance, lpfMode, filterGain);
		}
	}

	filterGain =
	    multiply_32x32_rshift32(filterGain, 1720000000)
	    << 1; // This changes the overall amplitude so that, with resonance on 50%, the amplitude is the same as it was pre June 2017

	// HPF
	if (HPFOn) {
		filterGain = hpladderconfig.init(hpfFrequency, hpfResonance, adjustVolumeForHPFResonance, filterGain);
	}

	return filterGain;
}
void FilterSet::copy_config(FilterSet* other) {
	memcpy(&lpladderconfig, &other->lpladderconfig, sizeof(LPLadderConfig));
	memcpy(&lpsvfconfig, &other->lpsvfconfig, sizeof(LPSVFConfig));
	memcpy(&hpladderconfig, &other->hpladderconfig, sizeof(HPLadderConfig));
	LPFOn = other->isLPFOn();
	HPFOn = other->isHPFOn();
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

SVF_outs SVFilter::doSVF(int32_t input, LPSVFConfig* lpsvfconfig) {
	q31_t high = 0;
	q31_t notch = 0;
	q31_t lowi;
	q31_t f = lpsvfconfig->moveability;
	q31_t q = lpsvfconfig->processedResonance;
	q31_t in = lpsvfconfig->SVFInputScale;

	input = multiply_32x32_rshift32(in, input);

	low = low + 2 * multiply_32x32_rshift32(band, f);
	high = input - low;
	high = high - 2 * multiply_32x32_rshift32(band, q);
	band = 2 * multiply_32x32_rshift32(high, f) + band;
	notch = high + low;

	//saturate band feedback
	band = getTanHUnknown(band, 3);

	lowi = low;
	//double sample to increase the cutoff frequency
	low = low + 2 * multiply_32x32_rshift32(band, f);
	high = input - low;
	high = high - 2 * multiply_32x32_rshift32(band, q);
	band = 2 * multiply_32x32_rshift32(high, f) + band;

	//saturate band feedback
	band = getTanHUnknown(band, 3);
	notch = high + low;

	SVF_outs result = {(lowi) + (low), band, high, notch};

	return result;
}
