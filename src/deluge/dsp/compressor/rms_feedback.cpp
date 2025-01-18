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
std::array<StereoSample, SSI_TX_BUFFER_NUM_SAMPLES> dryBuffer;

RMSFeedbackCompressor::RMSFeedbackCompressor() {
	setAttack(5 << 24);
	setRelease(5 << 24);
	setThreshold(0);
	setRatio(64 << 24);
	setSidechain(0);
	setBlend(ONE_Q31);
	// Default to the maximum useful base gain
	baseGain_ = 1.35f;
}
// 16 is ln(1<<24) - 1, i.e. where we start clipping
// since this applies to output
void RMSFeedbackCompressor::updateER(float numSamples, q31_t finalVolume) {

	// 33551360
	//  int32_t volumePostFX = getParamNeutralValue(Param::Global::VOLUME_POST_FX);
	// We offset the final volume by a minuscule amount to avoid a finalVolume of zero resulting in NaNs propagating.
	//
	// Maximum value: 2.08 neppers, since finalVolume is at most 0x7fffffff (representing ~8 in 3.29 signed fixed point)
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
void RMSFeedbackCompressor::renderVolNeutral(std::span<StereoSample> buffer, q31_t finalVolume) {
	// this is a bit gross - the compressor can inherently apply volume changes, but in the case of the per clip
	// compressor that's already been handled by the reverb send, and the logic there is tightly coupled such that
	// I couldn't extract correct volume levels from it.
	render(buffer, 1 << 27, 1 << 27, finalVolume >> 3);
}
constexpr uint8_t saturationAmount = 3;
void RMSFeedbackCompressor::render(std::span<StereoSample> buffer, q31_t volAdjustL, q31_t volAdjustR,
                                   q31_t finalVolume) {
	// make a copy for blending if we need to
	if (wet != ONE_Q31) {
		memcpy(dryBuffer.data(), buffer.data(), buffer.size_bytes());
	}

	if (!onLastTime) {
		// sets the "working level" for interpolation and anti aliasing
		lastSaturationTanHWorkingValue[0] =
		    (uint32_t)lshiftAndSaturateUnknown(buffer[0].l, saturationAmount) + 2147483648u;
		lastSaturationTanHWorkingValue[1] =
		    (uint32_t)lshiftAndSaturateUnknown(buffer[0].r, saturationAmount) + 2147483648u;
		onLastTime = true;
	}
	// we update this every time since we won't know if the song volume changed
	updateER(buffer.size(), finalVolume);

	float over = std::max<float>(0, (rms - threshdb));

	state = runEnvelope(state, over, buffer.size());

	float reduction = -state * fraction;

	// this is the most gain available without overflow
	// Amount of gain. Must not exceed 3.43 as that will result in a gain > 31
	float dbGain = baseGain_ + er + reduction;

	float gain = std::exp((dbGain));
	gain = std::min<float>(gain, 31);

	// Compute linear volume adjustments as 13.18 signed fixed-point numbers (though stored as floats due to their
	// use as an intermediate value prior to the increment computation below)
	float finalVolumeL = gain * float(volAdjustL >> 9);
	float finalVolumeR = gain * float(volAdjustR >> 9);

	// The amount we need to step the current volume so that by the end of the rendering window
	q31_t amplitudeIncrementL = ((int32_t)((finalVolumeL - (currentVolumeL >> 8)) / float(buffer.size()))) << 8;
	q31_t amplitudeIncrementR = ((int32_t)((finalVolumeR - (currentVolumeR >> 8)) / float(buffer.size()))) << 8;

	auto dry_it = dryBuffer.begin();
	for (StereoSample& sample : buffer) {
		currentVolumeL += amplitudeIncrementL;
		currentVolumeR += amplitudeIncrementR;

		// Need to shift left by 4 because currentVolumeL is a 5.26 signed number rather than a 1.30 signed.
		sample.l = multiply_32x32_rshift32(sample.l, currentVolumeL) << 4;
		sample.l = getTanHAntialiased(sample.l, &lastSaturationTanHWorkingValue[0], saturationAmount);

		sample.r = multiply_32x32_rshift32(sample.r, currentVolumeR) << 4;
		sample.r = getTanHAntialiased(sample.r, &lastSaturationTanHWorkingValue[1], saturationAmount);
		// wet/dry blend
		if (wet != ONE_Q31) {
			sample.l = multiply_32x32_rshift32(sample.l, wet);
			sample.l = multiply_accumulate_32x32_rshift32_rounded(sample.l, dry_it->l, dry);
			sample.l <<= 1; // correct for the two multiplications
			// same for r because StereoSample is a dumb class
			sample.r = multiply_32x32_rshift32(sample.r, wet);
			sample.r = multiply_accumulate_32x32_rshift32_rounded(sample.r, dry_it->r, dry);
			sample.r <<= 1;
			++dry_it; // this is a little gross but it's fine
		}
	}

	// for LEDs
	// 4 converts to dB, then quadrupled for display range since a 30db reduction is basically killing the signal
	gainReduction = std::clamp<int32_t>(-(reduction) * 4 * 4, 0, 127);
	// calc compression for next round (feedback compressor)
	rms = calcRMS(buffer);
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
float RMSFeedbackCompressor::calcRMS(std::span<StereoSample> buffer) {
	q31_t sum = 0;
	q31_t offset = 0; // to remove dc offset
	float lastMean = mean;

	for (StereoSample sample : buffer) {
		q31_t l = sample.l - hpfL.doFilter(sample.l, hpfA_);
		q31_t r = sample.r - hpfL.doFilter(sample.r, hpfA_);
		q31_t s = std::max(std::abs(l), std::abs(r));
		sum += multiply_32x32_rshift32(s, s);
	}

	float ns = buffer.size() * 2;
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
