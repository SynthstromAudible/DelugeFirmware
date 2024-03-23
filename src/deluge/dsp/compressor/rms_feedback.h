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
	constexpr void setup(q31_t a, q31_t r, q31_t t, q31_t rat, q31_t fc) {
		setAttack(a);
		setRelease(r);
		setThreshold(t);
		setRatio(rat);
		setSidechain(fc);
	}
	void reset() {
		state = 0;
		er = 0;
		mean = 0;
	}
	void renderVolNeutral(StereoSample* buffer, uint16_t numSamples, q31_t finalVolume);
	void render(StereoSample* buffer, uint16_t numSamples, q31_t volAdjustL, q31_t volAdjustR, q31_t finalVolume);
	float runEnvelope(float current, float desired, float numSamples);
	// attack/release in range 0 to 2^31
	inline q31_t getAttack() { return attackKnobPos; }
	inline int32_t getAttackMS() { return attackMS; }
	constexpr int32_t setAttack(q31_t attack) {
		// this exp will be between 1 and 7ish, half the knob range is about 2.5
		attackMS = 0.5 + (std::exp(2 * float(attack) / ONE_Q31f) - 1) * 10;
		a_ = (-1000.0f / kSampleRate) / attackMS;
		attackKnobPos = attack;
		return attackMS;
	};
	inline q31_t getRelease() { return releaseKnobPos; }
	inline int32_t getReleaseMS() { return releaseMS; }
	constexpr int32_t setRelease(q31_t release) {
		// this exp will be between 1 and 7ish, half the knob range is about 2.5
		releaseMS = 50 + (std::exp(2 * float(release) / ONE_Q31f) - 1) * 50;
		r_ = (-1000.0f / kSampleRate) / releaseMS;
		releaseKnobPos = release;
		return releaseMS;
	};
	q31_t getThreshold() { return thresholdKnobPos; }
	constexpr void setThreshold(q31_t t) {
		thresholdKnobPos = t;
		threshold = 1 - 0.8f * (float(thresholdKnobPos) / ONE_Q31f);
	}
	q31_t getRatio() { return ratioKnobPos; }
	constexpr int32_t getRatioForDisplay() { return (1 / (1 - reduction)); }
	constexpr int32_t setRatio(q31_t rat) {
		ratioKnobPos = rat;
		reduction = 0.5f + (float(ratioKnobPos) / ONE_Q31f) / 2;
		return 1 / (1 - reduction);
	}
	q31_t getSidechain() { return sideChainKnobPos; }
	constexpr int32_t getSidechainForDisplay() {
		float fc_hz = (std::exp(1.5 * float(sideChainKnobPos) / ONE_Q31f) - 1) * 30;
		return fc_hz;
	}
	constexpr int32_t setSidechain(q31_t f) {
		sideChainKnobPos = f;
		// this exp will be between 1 and 5ish, half the knob range is about 2
		// the result will then be from 0 to 100hz with half the knob range at 60hz
		float fc_hz = (std::exp(1.5 * float(f) / ONE_Q31f) - 1) * 30;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1 + fc);
		a = wc * ONE_Q31;
		return fc_hz;
	}

	void updateER(float numSamples, q31_t finalVolume);
	float calcRMS(StereoSample* buffer, uint16_t numSamples);
	uint8_t gainReduction = 0;

private:
	// parameters in use
	float a_ = (-1000.0f / kSampleRate);
	float r_ = (-1000.0f / kSampleRate);
	float reduction = 2;
	float er = 0;
	float threshdb = 17;
	float threshold = 1;
	q31_t a = ONE_Q15;

	// state
	float state = 0;
	q31_t currentVolumeL = 0;
	q31_t currentVolumeR = 0;
	float rms = 0;
	float mean = 0;

	// sidechain filter
	deluge::dsp::filter::BasicFilterComponent hpfL;
	deluge::dsp::filter::BasicFilterComponent hpfR;
	// for display
	float attackMS = 0;
	float releaseMS = 0;

	// raw knob positions
	q31_t thresholdKnobPos = 0;
	q31_t ratioKnobPos = 0;
	q31_t attackKnobPos = 0;
	q31_t releaseKnobPos = 0;
	q31_t sideChainKnobPos = 0;
};
