/*
 * Copyright © 2023-2024 Mark Adams
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

#include "dsp/compressor/rms_feedback.h"
#include "util/fixedpoint.h"

RMSFeedbackCompressor::RMSFeedbackCompressor() {
	setAttack(5 << 24);
	setRelease(5 << 24);
	setThreshold(0);
	setRatio(64 << 24);
	setSidechain(0);
}
// 16 is ln(1<<24) - 1, i.e. where we start clipping
// since this applies to output
void RMSFeedbackCompressor::updateER(float numSamples, q31_t finalVolume) {

	// int32_t volumePostFX = getParamNeutralValue(Param::Global::VOLUME_POST_FX);
	float songVolumedB = logf(finalVolume);

	threshdb = songVolumedB * threshold;
	// this is effectively where song volume gets applied, so we'll stick an IIR filter (e.g. the envelope) here to
	// reduce clicking
	float lastER = er;
	er = std::max<float>((songVolumedB - threshdb - 1) * ratio, 0);
	// using the envelope is convenient since it means makeup gain and compression amount change at the same rate
	er = runEnvelope(lastER, er, numSamples);
}
/// This renders at a 'neutral' volume, so that at threshold zero the volume in unchanged
void RMSFeedbackCompressor::renderVolNeutral(StereoSample* buffer, uint16_t numSamples, q31_t finalVolume) {
	// this is a bit gross - the compressor can inherently apply volume changes, but in the case of the per clip
	// compressor that's already been handled by the reverb send, and the logic there is tightly coupled such that
	// I couldn't extract correct volume levels from it.
	render(buffer, numSamples, 2 << 26, 2 << 26, finalVolume >> 3);
}

void RMSFeedbackCompressor::render(StereoSample* buffer, uint16_t numSamples, q31_t volAdjustL, q31_t volAdjustR,
                                   q31_t finalVolume) {
	// we update this every time since we won't know if the song volume changed
	updateER(numSamples, finalVolume);

	float over = std::max<float>(0, (rms - threshdb));

	state = runEnvelope(state, over, numSamples);

	float reduction = -state * ratio;

	// this is the most gain available without overflow
	float dbGain = 3.f + er + reduction;

	float gain = std::exp((dbGain));
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
	// for LEDs
	// 4 converts to dB, then quadrupled for display range since a 30db reduction is basically killing the signal
	gainReduction = std::clamp<int32_t>(-(reduction) * 4 * 4, 0, 127);
	// calc compression for next round (feedback compressor)
	rms = calcRMS(buffer, numSamples);
}

float RMSFeedbackCompressor::runEnvelope(float current, float desired, float numSamples) {
	float s{0};
	if (desired > current) {
		s = desired + std::exp(a_ * numSamples) * (current - desired);
	}
	else {
		s = desired + std::exp(r_ * numSamples) * (current - desired);
	}
	return s;
}

// output range is 0-21 (2^31)
// dac clipping is at 16
float RMSFeedbackCompressor::calcRMS(StereoSample* buffer, uint16_t numSamples) {
	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	q31_t sum = 0;
	q31_t offset = 0; // to remove dc offset
	float lastMean = mean;
	do {
		q31_t l = thisSample->l - hpfL.doFilter(thisSample->l, a);
		q31_t r = thisSample->r - hpfL.doFilter(thisSample->r, a);
		q31_t s = std::max(std::abs(l), std::abs(r));
		sum += multiply_32x32_rshift32(s, s) << 1;

	} while (++thisSample != bufferEnd);

	float ns = float(numSamples);
	mean = (float(sum) / ONE_Q31f) / ns;
	// warning this is not good math but it's pretty close and way cheaper than doing it properly
	// good math would use a long FIR, this is a one pole IIR instead
	// the more samples we have, the more weight we put on the current mean to avoid response slowing down
	// at high cpu loads
	mean = (mean * ns + lastMean) / (1 + ns);
	float rms = ONE_Q31 * std::sqrt(mean);

	float logmean = std::log(std::max(rms, 1.0f));

	return logmean;
}
