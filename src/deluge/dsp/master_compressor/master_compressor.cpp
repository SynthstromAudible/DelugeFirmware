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
#include "io/debug/print.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "util/fast_fixed_math.h"
MasterCompressor::MasterCompressor() {
	//compressor.setAttack((float)attack / 100.0);
	attack = attackRateTable[2] << 2;
	release = releaseRateTable[5] << 2;
	//compressor.setRelease((float)release / 100.0);
	//compressor.setThresh((float)threshold / 100.0);
	//compressor.setRatio(1.0 / ((float)ratio / 100.0));
	shape = getParamFromUserValue(Param::Unpatched::COMPRESSOR_SHAPE, 2);
	//an appropriate range is 0-50*one q 15
	threshold = ONE_Q31;
	follower = true;
	//this is about a 1:1 ratio
	ratio = ONE_Q31 >> 1;
	syncLevel = SyncLevel::SYNC_LEVEL_NONE;
	currentVolumeL = 0;
	currentVolumeR = 0;
	//auto make up gain
	updateER();
}
//16 is ln(1<<24) - 1, i.e. where we start clipping
//since this applies to output
void MasterCompressor::updateER() {

	//int32_t volumePostFX = getParamNeutralValue(Param::Global::VOLUME_POST_FX);
	float songVolume;
	if (currentSong) {
		int32_t volumePostFX =
		    getFinalParameterValueVolume(
		        134217728, cableToLinearParamShortcut(currentSong->paramManager.getUnpatchedParamSet()->getValue(
		                       Param::Unpatched::GlobalEffectable::VOLUME)))
		    >> 1;
		songVolume = std::log(volumePostFX);
	}
	else {
		songVolume = 16;
	}
	threshdb = songVolume * (threshold / ONE_Q31f);
	//16 is about the level of a single synth voice at max volume
	er = (songVolume - threshdb) * (float(ratio) / ONE_Q31);
}

void MasterCompressor::render(StereoSample* buffer, uint16_t numSamples, q31_t volAdjustL, q31_t volAdjustR) {
	updateER();
	meanVolume = calc_rms(buffer, numSamples);

	q31_t over = std::max<float>(0, (meanVolume - threshdb) / 21) * ONE_Q31;

	if (over > 0) {
		registerHit(over);
	}
	out = Compressor::render(numSamples, shape);

	out = multiply_32x32_rshift32(out, ratio) << 1;

	//21 is the max internal volume (i.e. one_q31)
	//min ratio is 8 up to 1 (i.e. infinity/brick wall, 1 db reduction per db over)
	//base is arbitrary for scale, important part is the shape
	//this will be negative
	float reduction = 21 * (out / ONE_Q31f);
	//So basically this limits the gain to not clip
	//18 - meanVolume is the magic amount that makes sure the output
	//won't exceed 1<<24
	float dbGain = std::min<float>(1 + er + reduction, 18 - meanVolume);
	//additionally this is the most gain available without overflow
	dbGain = std::min(dbGain, 2.0f);
	float gain = exp((dbGain + lastGain) / 2);
	lastGain = dbGain;
	float finalVolumeL = gain * float(volAdjustL >> 8);
	float finalVolumeR = gain * float(volAdjustR >> 8);

	q31_t amplitudeIncrementL = ((int32_t)((finalVolumeL - (currentVolumeL >> 8)) / float(numSamples))) << 8;
	q31_t amplitudeIncrementR = ((int32_t)((finalVolumeR - (currentVolumeR >> 8)) / float(numSamples))) << 8;

	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;

	do {

		currentVolumeL += amplitudeIncrementL;
		currentVolumeR += amplitudeIncrementR;
		// Apply post-fx and post-reverb-send volume
		thisSample->l = multiply_32x32_rshift32(thisSample->l, currentVolumeL) << 1;
		thisSample->r = multiply_32x32_rshift32(thisSample->r, currentVolumeR) << 1;

	} while (++thisSample != bufferEnd);
	//for LEDs
	//9 converts to dB, quadrupled for display range since a 30db reduction is basically killing the signal
	gainReduction = std::clamp<int32_t>(-(reduction) * 9 * 4, 0, 127);
}

//output range is 0-21 (2^31)
//dac clipping is at 16
float MasterCompressor::calc_rms(StereoSample* buffer, uint16_t numSamples) {
	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	q31_t sum = 0;
	q31_t offset = 0; //to remove dc offset
	float lastMean = mean;
	do {
		q31_t s = std::abs(thisSample->l) + std::abs(thisSample->r);
		sum += multiply_32x32_rshift32(s, s) << 1;
		offset += thisSample->l;

	} while (++thisSample != bufferEnd);

	float ns = float(numSamples);
	float rms = ONE_Q31 * sqrt((float(sum) / ONE_Q31f) / ns);
	float dc = std::abs(offset) / ns;
	//warning this is not good math but it's pretty close and way cheaper than doing it properly
	mean = rms - dc / 1.4f;
	mean = std::max(mean, 1.0f);

	float logmean = std::log((mean + lastMean) / 2);

	return logmean;
}

void MasterCompressor::setup(int32_t a, int32_t r, int32_t t, int32_t rat) {
	attack = a;
	release = r;
	threshold = t;
	ratio = rat;
	updateER();
}
