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

	//an appropriate range is 0-50*one q 15

	thresholdKnobPos = 0;
	sideChainKnobPos = ONE_Q31 >> 1;
	//this is 2:1
	ratioKnobPos = 0;

	currentVolumeL = 0;
	currentVolumeR = 0;
	//auto make up gain
	updateER();
	setSidechain(sideChainKnobPos);
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
		songVolume = std::log(volumePostFX) - 2;
	}
	else {
		songVolume = 16;
	}
	threshdb = songVolume * threshold;
	//16 is about the level of a single synth voice at max volume
	er = std::max<float>((songVolume - threshdb - 1) * ratio, 0);
}

void MasterCompressor::render(StereoSample* buffer, uint16_t numSamples, q31_t volAdjustL, q31_t volAdjustR) {
	//we update this every time since we won't know if the song volume changed
	updateER();

	float over = std::max<float>(0, (rms - threshdb));

	float out = runEnvelope(over, numSamples);

	float reduction = -out * ratio;

	//this is the most gain available without overflow
	float dbGain = 0.85 + er + reduction;

	float gain = exp((dbGain));
	gain = std::min<float>(gain, 31);

	float finalVolumeL = gain * float(volAdjustL >> 9);
	float finalVolumeR = gain * float(volAdjustR >> 9);

	q31_t amplitudeIncrementL = ((int32_t)((finalVolumeL - (currentVolumeL >> 8)) / float(numSamples))) << 8;
	q31_t amplitudeIncrementR = ((int32_t)((finalVolumeR - (currentVolumeR >> 8)) / float(numSamples))) << 8;

	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;

	do {

		currentVolumeL += amplitudeIncrementL;
		currentVolumeR += amplitudeIncrementR;
		// Apply post-fx and post-reverb-send volume
		thisSample->l = multiply_32x32_rshift32(thisSample->l, currentVolumeL) << 2;
		thisSample->r = multiply_32x32_rshift32(thisSample->r, currentVolumeR) << 2;

	} while (++thisSample != bufferEnd);
	//for LEDs
	//9 converts to dB, quadrupled for display range since a 30db reduction is basically killing the signal
	gainReduction = std::clamp<int32_t>(-(reduction) * 9 * 4, 0, 127);
	//calc compression for next round (feedback compressor)
	rms = calc_rms(buffer, numSamples);
}

float MasterCompressor::runEnvelope(float in, float numSamples) {
	if (in > state) {
		state = in + exp(a_ * numSamples) * (state - in);
	}
	else {
		state = in + exp(r_ * numSamples) * (state - in);
	}
	return state;
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
		q31_t l = thisSample->l - hpfL.doFilter(thisSample->l, a);
		q31_t r = thisSample->r - hpfL.doFilter(thisSample->r, a);
		q31_t s = std::max(std::abs(l), std::abs(r));
		sum += multiply_32x32_rshift32(s, s) << 1;

	} while (++thisSample != bufferEnd);

	float ns = float(numSamples);
	mean = (float(sum) / ONE_Q31f) / ns;
	//warning this is not good math but it's pretty close and way cheaper than doing it properly
	//good math would use a long FIR, this is a one pole IIR instead
	//the more samples we have, the more weight we put on the current mean to avoid response slowing down
	//at high cpu loads
	mean = (mean * ns + lastMean) / (1 + ns);
	float rms = ONE_Q31 * sqrt(mean);

	float logmean = std::log(std::max(rms, 1.0f));

	return logmean;
}
//takes in knob positions in the range 0-ONE_Q31
void MasterCompressor::setup(q31_t a, q31_t r, q31_t t, q31_t rat, q31_t fc) {
	setAttack(a);
	setRelease(r);
	setThreshold(t);
	setRatio(rat);
	setSidechain(fc);
}
