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
#include "util/fast_fixed_math.h"
MasterCompressor::MasterCompressor() {
	//compressor.setAttack((float)attack / 100.0);
	attack = attackRateTable[2] << 2;
	release = releaseRateTable[5] << 2;
	//compressor.setRelease((float)release / 100.0);
	//compressor.setThresh((float)threshold / 100.0);
	//compressor.setRatio(1.0 / ((float)ratio / 100.0));
	shape = getParamFromUserValue(Param::Unpatched::COMPRESSOR_SHAPE, 25);
	//an appropriate range is 0-50*one q 15
	threshold = 15 * ONE_Q15;
	follower = true;
	//this is about a 1:1 ratio
	ratio = ONE_Q31 >> 3;
	syncLevel = SyncLevel::SYNC_LEVEL_NONE;
	currentVolume = 0;
}
//with floats baseline is 60-90us
void MasterCompressor::render(StereoSample* buffer, uint16_t numSamples) {
	meanVolume = calc_rms(buffer, numSamples);
	//shift up by 11 to make it a q31, where one is max possible output (21)
	q31_t over = std::max<q31_t>(0, meanVolume - threshold) << 11;
	if (over > 0) {
		registerHit(over);
	}
	out = Compressor::render(numSamples, shape);
	out = multiply_32x32_rshift32(out, ratio) << 1;
	//21 is the max output from logmean
	finalVolume = exp(21 * (out / ONE_Q31f)) * ONE_Q31;
	//base is arbitrary for scale, important part is the shape

	//copied from global effectable
	//int32_t positivePatchedValue = multiply_32x32_rshift32(out, ratio) + 536870912;
	//finalVolume = lshiftAndSaturate<5>(multiply_32x32_rshift32(positivePatchedValue, positivePatchedValue));

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
	//9 converts to dB, quadrupled for display range since a 30db reduction is basically killing the signal
	gr = std::clamp<uint8_t>(-(std::log(float(currentVolume + 1) / ONE_Q31f) * 9.0) * 4, 0, 128);
}

//output range is 0-21 (2^31)
//dac clipping is at 16
q31_t MasterCompressor::calc_rms(StereoSample* buffer, uint16_t numSamples) {
	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	q31_t sum = 0;
	q31_t offset = 0; //to remove dc offset
	q31_t last_mean = mean;
	do {
		q31_t s = std::abs(thisSample->l) + std::abs(thisSample->r);
		sum += multiply_32x32_rshift32(s, s) << 1;
		offset += thisSample->l;

	} while (++thisSample != bufferEnd);
	//we don't care about the low bits and they make visual noise
	sum = (sum >> 8) << 8;
	offset = (offset >> 8) << 8;
	float ns = float(numSamples);
	float rms = ONE_Q31 * sqrt((float(sum) / ONE_Q31f) / ns);
	float dc = std::abs(offset) / ns;
	//warning this is not good math but it's pretty close and way cheaper than doing it properly
	mean = rms - dc / 1.4f;
	mean = std::max(mean, 1.0f);

	float logmean = std::log(mean);

	return logmean * float(ONE_Q15);
}
