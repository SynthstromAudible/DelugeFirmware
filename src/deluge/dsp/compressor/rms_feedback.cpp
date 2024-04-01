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
	// 33551360
	//  int32_t volumePostFX = getParamNeutralValue(Param::Global::VOLUME_POST_FX);
	// We offset the final volume by a minuscule amount to avoid a finalVolume of zero resulting in NaNs propagating.
	float songVolumedB = logf(finalVolume + 1e-10);

	threshdb = songVolumedB * threshold;
	// this is effectively where song volume gets applied, so we'll stick an IIR filter (e.g. the envelope) here to
	// reduce clicking
	float lastER = er;
	er = std::max<float>((songVolumedB - threshdb - 1) * fraction, 0);
	// using the envelope is convenient since it means makeup gain and compression amount change at the same rate
	er = runEnvelope(lastER, er, numSamples);
}
/// This renders at a 'neutral' volume, so that at threshold zero the volume in unchanged
void RMSFeedbackCompressor::renderVolNeutral(StereoSample* buffer, uint16_t numSamples, q31_t finalVolume) {
	// this is a bit gross - the compressor can inherently apply volume changes, but in the case of the per clip
	// compressor that's already been handled by the reverb send, and the logic there is tightly coupled such that
	// I couldn't extract correct volume levels from it.
	render(buffer, numSamples, 1 << 27, 1 << 27, finalVolume >> 3);
}

void RMSFeedbackCompressor::render(StereoSample* buffer, uint16_t numSamples, q31_t volAdjustL, q31_t volAdjustR,
                                   q31_t finalVolume) {
	// we update this every time since we won't know if the song volume changed
	updateER(numSamples, finalVolume);

	float over = std::max<float>(0, (rms - threshdb));

	state = runEnvelope(state, over, numSamples);

	float reduction = -state * fraction;

	// this is the most gain available without overflow
	float dbGain = .85f + er + reduction;

	float gain = std::exp((dbGain));
	gain = std::min<float>(gain, 31);

	// Compute linear volume adjustments as 13.18 signed fixed-point numbers (though stored as floats due to their
	// use as an intermediate value prior to the increment computation below)
	float finalVolumeL = gain * float(volAdjustL >> 9);
	float finalVolumeR = gain * float(volAdjustR >> 9);

	// The amount we need to step the current volume so that by the end of the rendering window
	q31_t amplitudeIncrementL = ((int32_t)((finalVolumeL - (currentVolumeL >> 8)) / float(numSamples))) << 8;
	q31_t amplitudeIncrementR = ((int32_t)((finalVolumeR - (currentVolumeR >> 8)) / float(numSamples))) << 8;

	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;

	do {
		currentVolumeL += amplitudeIncrementL;
		currentVolumeR += amplitudeIncrementR;
		// Apply post-fx and post-reverb-send volume
		//
		// Need to shift left by 4 because currentVolumeL is a 5.26 signed number rather than a 1.30 signed.
		thisSample->l = multiply_32x32_rshift32(thisSample->l, currentVolumeL) << 4;
		thisSample->r = multiply_32x32_rshift32(thisSample->r, currentVolumeR) << 4;

	} while (++thisSample != bufferEnd);
	// for LEDs
	// 4 converts to dB, then quadrupled for display range since a 30db reduction is basically killing the signal
	gainReduction = std::clamp<int32_t>(-(reduction) * 4 * 4, 0, 127);
	// calc compression for next round (feedback compressor)
	rms = calcRMS(buffer, numSamples);
}

float RMSFeedbackCompressor::runEnvelope(float current, float desired, float numSamples) const {
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
		q31_t l = thisSample->l - hpfL.doFilter(thisSample->l, hpfA_);
		q31_t r = thisSample->r - hpfL.doFilter(thisSample->r, hpfA_);
		q31_t s = std::max(std::abs(l), std::abs(r));
		sum += multiply_32x32_rshift32(s, s);

	} while (++thisSample != bufferEnd);

	float ns = float(numSamples);
	mean = (2 * float(sum) / ONE_Q31f) / ns;
	// warning this is not good math but it's pretty close and way cheaper than doing it properly
	// good math would use a long FIR, this is a one pole IIR instead
	// the more samples we have, the more weight we put on the current mean to avoid response slowing down
	// at high cpu loads
	mean = (mean * ns + lastMean) / (1 + ns);
	float rms = ONE_Q31 * std::sqrt(mean);

	float logmean = std::log(std::max(rms, 1.0f));

	return logmean;
}
