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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/filter/ladder_components.h"
#include "dsp/stereo_sample.h"
#include <cmath>

class RMSFeedbackCompressor {
public:
	RMSFeedbackCompressor();

	/// takes in all values as knob positions in the range 0-ONE_Q31
	constexpr void setup(q31_t a, q31_t r, q31_t t, q31_t rat, q31_t fc, q31_t blend, float baseGain) {
		setAttack(a);
		setRelease(r);
		setThreshold(t);
		setRatio(rat);
		setSidechain(fc);
		setBlend(blend);
		baseGain_ = baseGain;
	}

	/// Reset the state of the compressor so no gain reduction is applied at the start of the next render window.
	void reset() {
		state = 0;
		er = 0;
		mean = 0;
		onLastTime = false;
	}

	/// Render the compressor in-place using the provided buffer.
	///
	/// Assumes the input is 24-bit peak-to-peak (-2^23 to 2^23), and keeps the output in that range.
	///
	/// @param buffer Input and output buffer.
	/// @param numSamples Length of the buffer.
	/// @param volAdjustL Linear gain to apply to the left channel as a 4.27 signed fixed point number.
	/// @param volAdjustL Linear gain to apply to the right channel as a 4.27 signed fixed point number.
	/// @param finalVolume Linear peak-to-peak volume scale, as a 3.29 fixed-point integer.
	void render(StereoSample* buffer, uint16_t numSamples, q31_t volAdjustL, q31_t volAdjustR, q31_t finalVolume);

	/// Render the compressor with neutral left/right gain and with the finalVolume tweaked so the compressor applies
	/// 0db gain change at theshold zero. Used by the per-clip compressors because the clip volume is applied without
	/// the compressor being involved.
	void renderVolNeutral(StereoSample* buffer, uint16_t numSamples, q31_t finalVolume);

	/// Compute an updated envelope value, using the attack time constant if desired > current and the release time
	/// constant otherwise.
	[[nodiscard]] float runEnvelope(float current, float desired, float numSamples) const;

	/// Get the current attack time constant in terms of the full knob range (0 to 2^31)
	[[nodiscard]] inline constexpr q31_t getAttack() const { return attackKnobPos; }

	/// Get the current attack time constant in MS for a 3db change
	[[nodiscard]] inline constexpr float getAttackMS() const { return attackMS; }

	/// Set the attack time constant from a full-scale (0 to 2^31) number.
	///
	/// The parameter is exponentially mapped to a number between 0.5 and 70-ish ms
	constexpr int32_t setAttack(q31_t attack) {
		// this exp will be between 1 and 7ish, half the knob range is about 2.5
		attackMS = 0.5 + (std::exp(2 * float(attack) / ONE_Q31f) - 1) * 10;
		a_ = (-1000.0f / kSampleRate) / attackMS;
		attackKnobPos = attack;
		return attackMS;
	};

	/// Get the current release time constant in terms of the full knob range (0 to 2^31)
	[[nodiscard]] inline constexpr q31_t getRelease() const { return releaseKnobPos; }

	/// Get the current release time constant in MS for a 3db change
	[[nodiscard]] inline constexpr float getReleaseMS() const { return releaseMS; }

	/// Set the release time constant from a full-scale (0 to 2^31) number.
	///
	/// The parameter is exponentially mapped to a number between 50 and 400-ish ms
	constexpr int32_t setRelease(q31_t release) {
		// this exp will be between 1 and 7ish, half the knob range is about 2.5
		releaseMS = 50 + (std::exp(2 * float(release) / ONE_Q31f) - 1) * 50;
		r_ = (-1000.0f / kSampleRate) / releaseMS;
		releaseKnobPos = release;
		return releaseMS;
	};

	/// Get the current theshold as a full-scale (0 to 2^31) number.
	[[nodiscard]] inline constexpr q31_t getThreshold() const { return thresholdKnobPos; }
	/// Set the threshold based on a full-scale (0 to 2^31) number.
	///
	/// 0 corresponds to a threshold of 0.2, 2^31 corresponds to a theshold of 1.
	constexpr void setThreshold(q31_t t) {
		thresholdKnobPos = t;
		threshold = 1 - 0.8f * (float(thresholdKnobPos) / ONE_Q31f);
	}

	/// Get the current ratio as a full-scale (0 to 2^31) number
	[[nodiscard]] inline constexpr q31_t getRatio() const { return ratioKnobPos; }

	/// Get the current ratio as a float
	[[nodiscard]] inline constexpr float getRatioForDisplay() const { return ratio; }

	/// Set the ratio based on a full-scale (0 to 2^31) number.
	///
	/// A inverse (1/x) mapping is used, where 0 corresponds to a ratio of 2:1, 2^31 corresponds to a ratio of 256:1.
	constexpr int32_t setRatio(q31_t rat) {
		ratioKnobPos = rat;
		fraction = 0.5f + (float(ratioKnobPos) / ONE_Q31f) / 2;
		ratio = 1 / (1 - fraction);
		return ratio;
	}

	/// Get the current sidechain cutoff frequency as a full-scale (0 to 2^31) integer
	q31_t getSidechain() { return sideChainKnobPos; }

	/// Get the current sidechain cutoff frequency in hertz.
	[[nodiscard]] inline constexpr float getSidechainForDisplay() const { return fc_hz; }

	/// Set the sidechain cutoff frequency from a full-scale (0 to 2^31) integer.
	///
	/// The parameter is exponentially mapped so 0 to 2^31 corresponds to about 0 to 100hz cutoff.
	constexpr int32_t setSidechain(q31_t f) {
		sideChainKnobPos = f;
		// this exp will be between 1 and 5ish, half the knob range is about 2
		// the result will then be from 0 to 100hz with half the knob range at 60hz
		fc_hz = (std::exp(1.5 * float(f) / ONE_Q31f) - 1) * 30;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1 + fc);
		hpfA_ = wc * ONE_Q31;
		return fc_hz;
	}

	/// returns blend in q31
	constexpr q31_t getBlend() { return wet; }
	/// returns blend as an integer percentage
	constexpr int32_t getBlendForDisplay() { return wet > (127 << 24) ? 100 : 100 * (wet >> 24) >> 7; }

	/// update the blend level, where blend is the wet level (i.e. ONE_Q31 is full wet)
	/// returns wet percentage
	constexpr int32_t setBlend(q31_t blend) {
		// hack to allow it to get to full wet. Safe since this isn't a modulatable param and doesn't need the headroom
		dry = ONE_Q31 - blend;
		wet = blend;
		return getBlendForDisplay();
	}

	/// Configure the base makeup gain. Since reduction is always negative, we only need to worry about the case where
	/// reduction == 0 to determine the maximum headroom. er can not exceed 2.08, so we have 1.35 neppers of headroom.
	///
	/// The song compressor must use 0.8 to maintain compatibility with previous songs.
	constexpr void setBaseGain(float baseGain) { baseGain_ = baseGain; }

	/// Update the internal envelope and gain reduction tracking.
	void updateER(float numSamples, q31_t finalVolume);

	/// Calculate the RMS amplitude, post internal HPF, of the samples.
	float calcRMS(StereoSample* buffer, uint16_t numSamples);

	/// Amount of gain reduction applied during the last render pass, in 6.2 fixed point decibels
	uint8_t gainReduction = 0;

private:
	/// Attack time constant, in inverse samples
	float a_ = (-1000.0f / kSampleRate);
	/// Release time constant, in inverse samples
	float r_ = (-1000.0f / kSampleRate);
	/// 1 - (1 / ratio), describes the actual factor by which volume changes should increase or decrease.
	///
	/// The UI currently limits this to 0.5 (a ratio of 2) to 1.0 (a ratio of 256)
	float fraction = 0.5;
	/// Internal (smoothed) version of log of the requested volume.
	///
	/// Maximum value:   2.08 neppers when finalVolume is 0x7fffffff
	/// Minimum value: -23.07 neppers when finalVolume is 0
	float er = 0;
	/// Threshold, in decibels
	float threshdb = 17;
	/// The raw threshold value
	float threshold = 1;
	/// A parameter for the internal HPF
	q31_t hpfA_ = ONE_Q15;

	/// State for the internal envelope follower
	float state = 0;
	/// Current left channel volume as a 5.26 signed fixed-point number
	q31_t currentVolumeL = 0;
	/// Current right channel volume as a 5.26 signed fixed-point number
	q31_t currentVolumeR = 0;
	/// Log-RMS value of the last render.
	float rms = 0;
	/// Mean value of the last render.
	float mean = 0;
	/// tanh working values for output saturation
	uint32_t lastSaturationTanHWorkingValue[2] = {0};
	bool onLastTime = false;

	// sidechain filter
	deluge::dsp::filter::BasicFilterComponent hpfL;
	deluge::dsp::filter::BasicFilterComponent hpfR;
	// for display
	float attackMS = 0;
	float releaseMS = 0;
	float ratio = 2;
	float fc_hz;

	/// How much to bump the envelope follower RMS value and reduction to get approximate iso-volume when the compressor
	/// is engaged.
	float baseGain_;

	// raw knob positions
	q31_t thresholdKnobPos = 0;
	q31_t ratioKnobPos = 0;
	q31_t attackKnobPos = 0;
	q31_t releaseKnobPos = 0;
	q31_t sideChainKnobPos = 0;
	q31_t dry;
	q31_t wet;
};
