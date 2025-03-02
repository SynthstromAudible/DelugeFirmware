/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams
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

#include "basic_waves.h"
#include "definitions.h"
#include "processing/render_wave.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include <argon.hpp>
namespace deluge::dsp {
/* Before calling, you must:
    amplitude <<= 1; ( so that it is q31)
    amplitudeIncrement <<= 1;
*/
void renderWave(const int16_t* __restrict__ table, int32_t table_size_magnitude, int32_t amplitude,
                std::span<q31_t> buffer, uint32_t phase_increment, uint32_t phase, bool apply_amplitude,
                uint32_t phase_to_add, int32_t amplitude_increment) {

	Argon<q31_t> amplitude_vector = createAmplitudeVector(amplitude, amplitude_increment);
	Argon<q31_t> amplitude_increment_vector = amplitude_increment << 1;

	for (Argon<q31_t>& sample_vector : argon::vectorize(buffer)) {
		auto [value_vector, new_phase] =
		    waveRenderingFunctionGeneral(phase, phase_increment, phase_to_add, table, table_size_magnitude);

		if (apply_amplitude) {
			value_vector = sample_vector.MultiplyAddFixedPoint(value_vector, amplitude_vector);
			amplitude_vector = amplitude_vector + amplitude_increment_vector;
		}

		sample_vector = value_vector;
		phase = new_phase;
	}
}

/* Before calling, you must:
    amplitude <<= 1;
    amplitudeIncrement <<= 1;
*/
void renderPulseWave(const int16_t* __restrict__ table, int32_t table_size_magnitude, int32_t amplitude,
                     std::span<q31_t> buffer, uint32_t phase_increment, uint32_t phase, bool apply_amplitude,
                     uint32_t phase_to_add, int32_t amplitude_increment) {
	Argon<q31_t> amplitude_vector = createAmplitudeVector(amplitude, amplitude_increment);
	Argon<q31_t> amplitude_increment_vector = amplitude_increment << 1;

	for (Argon<q31_t>& sample_vector : argon::vectorize(buffer)) {
		auto [value_vector, new_phase] =
		    waveRenderingFunctionPulse(phase, phase_increment, phase_to_add, table, table_size_magnitude);

		if (apply_amplitude) {
			value_vector = sample_vector.MultiplyAddFixedPoint(value_vector, amplitude_vector);
			amplitude_vector = amplitude_vector + amplitude_increment_vector;
		}

		sample_vector = value_vector;
		phase = new_phase;
	}
}

uint32_t renderCrudeSawWaveWithAmplitude(int32_t* thisSample, int32_t* bufferEnd, uint32_t phaseNowNow,
                                         uint32_t phaseIncrementNow, int32_t amplitudeNow, int32_t amplitudeIncrement,
                                         int32_t numSamples) {

	int32_t* remainderSamplesEnd = thisSample + (numSamples & 3);

	while (thisSample != remainderSamplesEnd) {
		phaseNowNow += phaseIncrementNow;
		amplitudeNow += amplitudeIncrement;
		*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, (int32_t)phaseNowNow, amplitudeNow);
		++thisSample;
	}

	while (thisSample != bufferEnd) {
		phaseNowNow += phaseIncrementNow;
		amplitudeNow += amplitudeIncrement;
		*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, (int32_t)phaseNowNow, amplitudeNow);
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		amplitudeNow += amplitudeIncrement;
		*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, (int32_t)phaseNowNow, amplitudeNow);
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		amplitudeNow += amplitudeIncrement;
		*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, (int32_t)phaseNowNow, amplitudeNow);
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		amplitudeNow += amplitudeIncrement;
		*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, (int32_t)phaseNowNow, amplitudeNow);
		++thisSample;
	}

	return phaseNowNow;
}

uint32_t renderCrudeSawWaveWithoutAmplitude(int32_t* thisSample, int32_t* bufferEnd, uint32_t phaseNowNow,
                                            uint32_t phaseIncrementNow, int32_t numSamples) {

	int32_t* remainderSamplesEnd = thisSample + (numSamples & 7);

	while (thisSample != remainderSamplesEnd) {
		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;
	}

	while (thisSample != bufferEnd) {
		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;

		phaseNowNow += phaseIncrementNow;
		*thisSample = (int32_t)phaseNowNow >> 1;
		++thisSample;
	}

	return phaseNowNow;
}

// Not used, obviously. Just experimenting.
void renderPDWave(const int16_t* table, const int16_t* secondTable, int32_t numBitsInTableSize,
                  int32_t numBitsInSecondTableSize, int32_t amplitude, int32_t* thisSample, int32_t* bufferEnd,
                  int32_t numSamplesRemaining, uint32_t phaseIncrementNow, uint32_t* thisPhase, bool applyAmplitude,
                  bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
                  uint32_t resetterHalfPhaseIncrement, uint32_t resetterLower, int32_t resetterDivideByPhaseIncrement,
                  uint32_t pulseWidth, uint32_t phaseToAdd, uint32_t retriggerPhase, uint32_t horizontalOffsetThing,
                  int32_t amplitudeIncrement,
                  int32_t (*waveValueFunction)(const int16_t*, int32_t, uint32_t, uint32_t, uint32_t)) {

	amplitude <<= 1;
	amplitudeIncrement <<= 1;

	float w = (float)(int32_t)pulseWidth / 2147483648;

	uint32_t phaseIncrementEachHalf[2];

	phaseIncrementEachHalf[0] = phaseIncrementNow / (w + 1);
	phaseIncrementEachHalf[1] = phaseIncrementNow / (1 - w);

	const int16_t* eachTable[2];
	eachTable[0] = table;
	eachTable[1] = secondTable;

	int32_t eachTableSize[2];
	eachTableSize[0] = numBitsInTableSize;
	eachTableSize[1] = numBitsInSecondTableSize;

	do {

		uint32_t whichHalfBefore = (*thisPhase) >> 31;

		*thisPhase += phaseIncrementEachHalf[whichHalfBefore];

		uint32_t whichHalfAfter = (*thisPhase) >> 31;

		if (whichHalfAfter != whichHalfBefore) {
			uint32_t howFarIntoNewHalf = *thisPhase & ~(uint32_t)2147483648u;

			// Going into 2nd half
			if (whichHalfAfter) {
				howFarIntoNewHalf = howFarIntoNewHalf * (w + 1) / (1 - w);
			}

			// Going into 1st half
			else {
				howFarIntoNewHalf = howFarIntoNewHalf * (1 - w) / (w + 1);
			}

			*thisPhase = (whichHalfAfter << 31) | howFarIntoNewHalf;
		}

		int32_t value = waveValueFunction(eachTable[whichHalfAfter], eachTableSize[whichHalfAfter], *thisPhase,
		                                  pulseWidth, phaseToAdd);

		if (applyAmplitude) {
			amplitude += amplitudeIncrement;
			*thisSample += multiply_32x32_rshift32(value, amplitude);
		}
		else {
			*thisSample = value;
		}
	} while (++thisSample != bufferEnd);
}

/**
 * @brief Get a table number and size, depending on the increment
 *
 * @return table_number, table_size
 */
std::pair<int32_t, int32_t> getTableNumber(uint32_t phaseIncrement) {
	if (phaseIncrement <= 1247086) {
		return {0, 13};
	}
	else if (phaseIncrement <= 1764571) {
		return {1, 12};
	}
	else if (phaseIncrement <= 2494173) {
		return {2, 12};
	}
	else if (phaseIncrement <= 3526245) {
		return {3, 11};
	}
	else if (phaseIncrement <= 4982560) {
		return {4, 11};
	}
	else if (phaseIncrement <= 7040929) {
		return {5, 11};
	}
	else if (phaseIncrement <= 9988296) {
		return {6, 11};
	}
	else if (phaseIncrement <= 14035840) {
		return {7, 11};
	}
	else if (phaseIncrement <= 19701684) {
		return {8, 11};
	}
	else if (phaseIncrement <= 28256363) {
		return {9, 11};
	}
	else if (phaseIncrement <= 40518559) {
		return {10, 11};
	}
	else if (phaseIncrement <= 55063683) {
		return {11, 11};
	}
	else if (phaseIncrement <= 79536431) {
		return {12, 11};
	}
	else if (phaseIncrement <= 113025455) {
		return {13, 11};
	}
	else if (phaseIncrement <= 165191049) {
		return {14, 10};
	}
	else if (phaseIncrement <= 238609294) {
		return {15, 10};
	}
	else if (phaseIncrement <= 306783378) {
		return {16, 10};
	}
	else if (phaseIncrement <= 429496729) {
		return {17, 10};
	}
	else if (phaseIncrement <= 715827882) {
		return {18, 9};
	}
	else {
		return {19, 9};
	}
}

const int16_t* sawTables[20] = {NULL,       NULL,       NULL,      NULL,      NULL,      NULL,      sawWave215,
                                sawWave153, sawWave109, sawWave76, sawWave53, sawWave39, sawWave27, sawWave19,
                                sawWave13,  sawWave9,   sawWave7,  sawWave5,  sawWave3,  sawWave1};
const int16_t* squareTables[20] = {NULL,         NULL,          NULL,          NULL,          NULL,
                                   NULL,         squareWave215, squareWave153, squareWave109, squareWave76,
                                   squareWave53, squareWave39,  squareWave27,  squareWave19,  squareWave13,
                                   squareWave9,  squareWave7,   squareWave5,   squareWave3,   squareWave1};
const int16_t* analogSquareTables[20] = {analogSquare_1722, analogSquare_1217, analogSquare_861, analogSquare_609,
                                         analogSquare_431,  analogSquare_305,  analogSquare_215, analogSquare_153,
                                         analogSquare_109,  analogSquare_76,   analogSquare_53,  analogSquare_39,
                                         analogSquare_27,   analogSquare_19,   analogSquare_13,  analogSquare_9,
                                         analogSquare_7,    analogSquare_5,    analogSquare_3,   analogSquare_1};

// The lower 8 are from (mystery synth A) - higher than that, it's (mystery synth B)
const int16_t* analogSawTables[20] = {
    mysterySynthASaw_1722, mysterySynthASaw_1217, mysterySynthASaw_861, mysterySynthASaw_609, mysterySynthASaw_431,
    mysterySynthASaw_305,  mysterySynthASaw_215,  mysterySynthASaw_153, mysterySynthBSaw_109, mysterySynthBSaw_76,
    mysterySynthBSaw_53,   mysterySynthBSaw_39,   mysterySynthBSaw_27,  mysterySynthBSaw_19,  mysterySynthBSaw_13,
    mysterySynthBSaw_9,    mysterySynthBSaw_7,    mysterySynthBSaw_5,   mysterySynthBSaw_3,   mysterySynthBSaw_1};
} // namespace deluge::dsp
