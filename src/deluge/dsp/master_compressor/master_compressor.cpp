/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "dsp/master_compressor/master_compressor.h"
#include "dsp/stereo_sample.h"

MasterCompressor::MasterCompressor() {
	//compressor.setAttack((float)attack / 100.0);
	attack = attackRateTable[7] << 1;
	release = releaseRateTable[35];
	//compressor.setRelease((float)release / 100.0);
	//compressor.setThresh((float)threshold / 100.0);
	//compressor.setRatio(1.0 / ((float)ratio / 100.0));
	shape = -601295438;
	threshold = 1 * 21474836;
	follower = true;
	amount = 25 * 85899345;
	currentVolume = 0;
}
//with floats baseline is 60-90us
void MasterCompressor::render(StereoSample* buffer, uint16_t numSamples) {
	meanVolume = calc_rms(buffer, numSamples);
	over = log(meanVolume + 1) * (1 << 25);
	registerHit(over);
	out = Compressor::render(numSamples, shape);
	//copied from global effectable
	int32_t positivePatchedValue = multiply_32x32_rshift32(out, amount) + 536870912;
	finalVolume = multiply_32x32_rshift32(positivePatchedValue, positivePatchedValue) << 4;

	amplitudeIncrement = (int32_t)(finalVolume - currentVolume) / numSamples;

	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;

	do {

		currentVolume += amplitudeIncrement;

		// Apply post-fx and post-reverb-send volume
		thisSample->l = multiply_32x32_rshift32(thisSample->l, currentVolume) << 1;
		thisSample->r = multiply_32x32_rshift32(thisSample->r, currentVolume) << 1;

	} while (++thisSample != bufferEnd);
	//for LEDs
	gr = 4 * (clz(currentVolume) - 1);
}

q31_t MasterCompressor::calc_rms(StereoSample* buffer, uint16_t numSamples) {
	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	q31_t sum = 0;
	do {
		q31_t s = std::max<q31_t>(0, std::abs(thisSample->l) + std::abs(thisSample->r));
		sum += s;

	} while (++thisSample != bufferEnd);
	return sum / numSamples;
}

void MasterCompressor::setup(int32_t attack, int32_t release, int32_t threshold, int32_t ratio, int32_t makeup,
                             int32_t mix) {
	//compressor.setAttack((float)attack / 100.0);
	// attack = attackRateTable[attack] << 2;
	// release = attackRateTable[attack] << 2;
	// //compressor.setRelease((float)release / 100.0);
	// //compressor.setThresh((float)threshold / 100.0);
	// //compressor.setRatio(1.0 / ((float)ratio / 100.0));
	// amount = ratio * 21474836;
	// threshold = threshold * 21474836;
}

/*
	masterCompressorAttack = 7;
	masterCompressorRelease = 10;
	masterCompressorThresh = 10;
	masterCompressorRatio = 10;
	masterCompressorMakeup = 0;
	masterCompressorWet = 50;

*/
