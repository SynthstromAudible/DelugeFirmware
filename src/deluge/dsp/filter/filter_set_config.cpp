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

#include "dsp/filter/filter_set_config.h"
#include "io/debug/print.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

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

LPLadderConfig::LPLadderConfig() = default;
q31_t LPLadderConfig::init(q31_t lpfFrequency, q31_t lpfResonance, LPFMode lpfMode, q31_t filterGain) {

	// Hot transistor ladder - needs oversampling and stuff
	if (lpfMode == LPFMode::TRANSISTOR_24DB_DRIVE) {

		int32_t resonance = ONE_Q31 - (lpfResonance << 2); // Limits it
		processedResonance = ONE_Q31 - resonance;          // Always between 0 and 2. 1 represented as 1073741824

		int32_t logFreq = quickLog(lpfFrequency);

		doOversampling = false; //storageManager.devVarA;

		logFreq = getMin(logFreq, (int32_t)63 << 24);

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
			lpfFrequency = getMin((int32_t)39056384, lpfFrequency);

			int32_t resonanceLimit = interpolateTableSigned(logFreq, 30, resonanceLimitTable, 6);

			processedResonance = getMin(processedResonance, resonanceLimit);
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
		tannedFrequency = getMax(
		    tannedFrequency,
		    (int32_t)540817); // We really want to keep the frequency from going lower than it has to - it causes problems

		int32_t resonance = ONE_Q31 - (getMin(lpfResonance, resonanceUpperLimit) << 2); // Limits it
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

		int32_t a = getMin(lpfResonance, (int32_t)536870911);
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

LPSVFConfig::LPSVFConfig() = default;
q31_t LPSVFConfig::init(q31_t lpfFrequency, q31_t lpfResonance, LPFMode lpfMode, q31_t filterGain) {
	int32_t tannedFrequency = instantTan(lshiftAndSaturate<5>(lpfFrequency));
	int32_t divideBy1PlusTannedFrequency =
	    (int64_t)2147483648u * 134217728
	    / (134217728 + (tannedFrequency >> 1)); // Between ~0.1 and 1. 1 represented by 2147483648
	moveability = multiply_32x32_rshift32_rounded(tannedFrequency, divideBy1PlusTannedFrequency)
	              << 4; // Between 0 and 1. 1 represented by 2147483648 I'm pretty sure
	// raw resonance is 0 - 536870896 (2^28ish, don't know where it comes from)
	// Multiply by 4 to bring it to the q31 0-1 range
	processedResonance = (ONE_Q31 - 4 * (lpfResonance));
	SVFInputScale = (processedResonance >> 1) + (ONE_Q31 >> 1);
	//squared q is a better match for the ladders
	//also the input scale needs to be sqrt(q) for the level compensation to work so it's a win win
	processedResonance = multiply_32x32_rshift32_rounded(processedResonance, processedResonance) << 1;
	return filterGain;
}

HPLadderConfig::HPLadderConfig() = default;
q31_t HPLadderConfig::init(q31_t hpfFrequency, q31_t hpfResonance, bool adjustVolumeForHPFResonance, q31_t filterGain) {
	int32_t extraFeedback = 1200000000;

	int32_t tannedFrequency =
	    instantTan(lshiftAndSaturate<5>(hpfFrequency)); // Between 0 and 8, by my making. 1 represented by 268435456

	int32_t hpfDivideBy1PlusTannedFrequency =
	    (int64_t)2147483648u * 134217728
	    / (134217728 + (tannedFrequency >> 1)); // Between ~0.1 and 1. 1 represented by 2147483648

	int32_t resonanceUpperLimit = 536870911;
	int32_t resonance = ONE_Q31 - (getMin(hpfResonance, resonanceUpperLimit) << 2); // Limits it

	resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
	hpfProcessedResonance =
	    ONE_Q31 - resonance; //ONE_Q31 - rawResonance2; // Always between 0 and 2. 1 represented as 1073741824

	hpfProcessedResonance = getMax(hpfProcessedResonance, (int32_t)134217728); // Set minimum resonance amount

	int32_t hpfProcessedResonanceUnaltered = hpfProcessedResonance;

	// Extra feedback
	hpfProcessedResonance = multiply_32x32_rshift32(hpfProcessedResonance, extraFeedback) << 1;

	hpfDivideByProcessedResonance = 2147483648u / (hpfProcessedResonance >> (23));

	hpfMoveability = multiply_32x32_rshift32_rounded(tannedFrequency, hpfDivideBy1PlusTannedFrequency) << 4;

	int32_t moveabilityTimesProcessedResonance =
	    multiply_32x32_rshift32(hpfProcessedResonanceUnaltered, hpfMoveability); // 1 = 536870912
	int32_t moveabilitySquaredTimesProcessedResonance =
	    multiply_32x32_rshift32(moveabilityTimesProcessedResonance, hpfMoveability); // 1 = 268435456

	hpfHPF3Feedback = -multiply_32x32_rshift32_rounded(hpfMoveability, hpfDivideBy1PlusTannedFrequency);
	hpfLPF1Feedback = hpfDivideBy1PlusTannedFrequency >> 1;

	uint32_t toDivideBy =
	    ((int32_t)268435456 - (moveabilityTimesProcessedResonance >> 1) + moveabilitySquaredTimesProcessedResonance);
	divideByTotalMoveability = (int32_t)((uint64_t)hpfProcessedResonance * 67108864 / toDivideBy);

	hpfDoAntialiasing = (hpfProcessedResonance > 900000000);

	if (adjustVolumeForHPFResonance) {
		// Adjust volume for HPF resonance
		q31_t rawResonance = getMin(hpfResonance, (q31_t)ONE_Q31 >> 2) << 2;
		q31_t squared = multiply_32x32_rshift32(rawResonance, rawResonance) << 1;
		squared = (multiply_32x32_rshift32(squared, squared) >> 4)
		          * 19; // Make bigger to have more of a volume cut happen at high resonance
		filterGain = multiply_32x32_rshift32(filterGain, ONE_Q31 - squared) << 1;
	}

	return filterGain;
}

FilterSetConfig::FilterSetConfig() {
	lpsvfconfig = LPSVFConfig();
	lpladderconfig = LPLadderConfig();
	hpladderconfig = HPLadderConfig();
}
int32_t FilterSetConfig::init(int32_t lpfFrequency, int32_t lpfResonance, int32_t hpfFrequency, int32_t hpfResonance,
                              LPFMode lpfMode, int32_t filterGain, bool adjustVolumeForHPFResonance,
                              int32_t* overallOscAmplitude) {

	hpfResonance =
	    (hpfResonance >> 21) << 21; // Insanely, having changes happen in the small bytes too often causes rustling

	if (doLPF) {
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
	if (doHPF) {
		filterGain = hpladderconfig.init(hpfFrequency, hpfResonance, adjustVolumeForHPFResonance, filterGain);
	}

	return filterGain;
}
