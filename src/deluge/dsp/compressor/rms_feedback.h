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
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp/stereo_sample.h"
#include "engines/effects/state.h"
#include <cmath>
#include <span>

class [[gnu::hot]] RMSFeedbackCompressor {
public:
	RMSFeedbackCompressor();

	/// Take @p state's knob positions, recomputing the time constants, ratio, sidechain filter and blend only when they
	/// differ from the ones last taken. The threshold too, which an owner whose threshold is a param then overrides
	/// with setThreshold every block.
	void configure(const deluge::effects::CompressorState& state) {
		if (state == configured_) {
			return;
		}
		setAttack(state.attack);
		setRelease(state.release);
		if (state.threshold != configured_.threshold) {
			setThreshold(state.threshold);
		}
		setRatio(state.ratio);
		setSidechain(state.sidechain_hpf);
		setBlend(state.blend);
		configured_ = state;
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
	void render(std::span<StereoSample> buffer, q31_t volAdjustL, q31_t volAdjustR, q31_t finalVolume);

	/// Render the compressor with neutral left/right gain and with the finalVolume tweaked so the compressor applies
	/// 0db gain change at theshold zero. Used by the per-clip compressors because the clip volume is applied without
	/// the compressor being involved.
	void renderVolNeutral(std::span<StereoSample> buffer, q31_t finalVolume);

	/// Compute an updated envelope value, using the attack time constant if desired > current and the release time
	/// constant otherwise.
	[[nodiscard]] float runEnvelope(float current, float desired, float numSamples) const;

	/// Get the current attack time constant in MS for a 3db change
	[[nodiscard]] inline constexpr float getAttackMS() const { return attackMS; }

	/// Get the current release time constant in MS for a 3db change
	[[nodiscard]] inline constexpr float getReleaseMS() const { return releaseMS; }

	/// The threshold this block renders with, as a full-scale (0 to 2^31) number.
	[[nodiscard]] inline constexpr q31_t threshold() const { return thresholdKnobPos; }

	/// Set the threshold this block renders with, from a full-scale (0 to 2^31) number.
	///
	/// 0 corresponds to a threshold of 0.2, 2^31 corresponds to a theshold of 1.
	constexpr void setThreshold(q31_t t) {
		thresholdKnobPos = t;
		threshold_ = 1 - 0.8f * (float(thresholdKnobPos) / ONE_Q31f);
	}

	/// The attack time, in ms for a 3 dB change, a knob position (0 to 2^31) gives: about 0.5 to 70 ms.
	[[nodiscard]] static float attackMsOf(q31_t attack) {
		// this exp will be between 1 and 7ish, half the knob range is about 2.5
		return 0.5 + (std::exp(2 * float(attack) / ONE_Q31f) - 1) * 10;
	}

	/// The release time, in ms for a 3 dB change, a knob position (0 to 2^31) gives: about 50 to 400 ms.
	[[nodiscard]] static float releaseMsOf(q31_t release) {
		// this exp will be between 1 and 7ish, half the knob range is about 2.5
		return 50 + (std::exp(2 * float(release) / ONE_Q31f) - 1) * 50;
	}

	/// The ratio a knob position (0 to 2^31) gives, inversely: 0 is 2:1, 2^31 is 256:1.
	[[nodiscard]] static float ratioOf(q31_t ratioKnob) { return 1 / (1 - fractionOf(ratioKnob)); }

	/// The sidechain filter's cutoff in Hz a knob position (0 to 2^31) gives: about 0 to 100 Hz, 60 Hz at half.
	[[nodiscard]] static float sidechainHzOf(q31_t sidechainKnob) {
		// this exp will be between 1 and 5ish, half the knob range is about 2
		return (std::exp(1.5 * float(sidechainKnob) / ONE_Q31f) - 1) * 30;
	}

	/// The wet level a blend (Q31, ONE_Q31 full wet) gives, as a whole percentage.
	[[nodiscard]] static constexpr int32_t blendPercentOf(q31_t blend) {
		return blend > (127 << 24) ? 100 : 100 * (blend >> 24) >> 7;
	}

	/// Configure the base makeup gain. Since reduction is always negative, we only need to worry about the case where
	/// reduction == 0 to determine the maximum headroom. er can not exceed 2.08, so we have 1.35 neppers of headroom.
	///
	/// The song compressor must use 0.8 to maintain compatibility with previous songs.
	constexpr void setBaseGain(float baseGain) { baseGain_ = baseGain; }

	/// Update the internal envelope and gain reduction tracking.
	void updateER(float numSamples, q31_t finalVolume);

	/// Calculate the RMS amplitude, post internal HPF, of the samples.
	float calcRMS(std::span<StereoSample> buffer);

	/// Amount of gain reduction applied during the last render pass, in 6.2 fixed point decibels
	uint8_t gainReduction = 0;

private:
	[[nodiscard]] static float fractionOf(q31_t ratioKnob) { return 0.5f + (float(ratioKnob) / ONE_Q31f) / 2; }

	void setAttack(q31_t attack) {
		attackMS = attackMsOf(attack);
		a_ = (-1000.0f / kSampleRate) / attackMS;
	}

	void setRelease(q31_t release) {
		releaseMS = releaseMsOf(release);
		r_ = (-1000.0f / kSampleRate) / releaseMS;
	}

	void setRatio(q31_t ratioKnob) {
		fraction = fractionOf(ratioKnob);
		ratio = 1 / (1 - fraction);
	}

	void setSidechain(q31_t sidechainKnob) {
		fc_hz = sidechainHzOf(sidechainKnob);
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1 + fc);
		hpfA_ = wc * ONE_Q31;
	}

	void setBlend(q31_t blend) {
		// ONE_Q31 is full wet: safe, since the blend is no modulatable param and needs no headroom
		dry = ONE_Q31 - blend;
		wet = blend;
	}

	/// The knob positions last taken (configure): a record of when to recompute, written nowhere else.
	deluge::effects::CompressorState configured_{};

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
	/// The threshold this block renders with, as a linear level
	float threshold_ = 1;
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
	deluge::dsp::fixed::OnePole hpfL;
	deluge::dsp::fixed::OnePole hpfR;
	// for display
	float attackMS = 0;
	float releaseMS = 0;
	float ratio = 2;
	float fc_hz;

	/// How much to bump the envelope follower RMS value and reduction to get approximate iso-volume when the compressor
	/// is engaged.
	float baseGain_;

	/// The threshold this block renders with (setThreshold), full-scale.
	q31_t thresholdKnobPos = 0;
	q31_t dry;
	q31_t wet;
};
