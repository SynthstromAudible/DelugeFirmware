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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/compressor/compressor.h"
#include "util/functions.h"

#define INLINE inline
#include <algorithm> // for min(), max()
#include <cassert>   // for assert()
#include <cmath>
#include <cstdint>

class MasterCompressor {
public:
	MasterCompressor();
	void setup(int32_t attack, int32_t release, int32_t threshold, int32_t ratio);

	void render(StereoSample* buffer, uint16_t numSamples, q31_t volAdjustL, q31_t volAdjustR);
	float runEnvelope(float in, float numSamples);
	//attack/release in range 0 to 2^31
	inline q31_t getAttack() { return attackKnobPos; }
	inline int32_t getAttackMS() { return attackMS; }
	int32_t setAttack(q31_t attack) {
		//this exp will be between 1 and 7ish, half the knob range is about 2.5
		attackMS = 0.5 + (exp(2 * float(attack) / ONE_Q31f) - 1) * 10;
		a_ = (-1000.0 / 44100) / attackMS;
		attackKnobPos = attack;
		return attackMS;
	};
	inline q31_t getRelease() { return releaseKnobPos; }
	inline int32_t getReleaseMS() { return releaseMS; }
	int32_t setRelease(q31_t release) {
		//this exp will be between 1 and 7ish, half the knob range is about 2.5
		releaseMS = 50 + (exp(2 * float(release) / ONE_Q31f) - 1) * 50;
		r_ = (-1000.0 / 44100) / releaseMS;
		releaseKnobPos = release;
		return releaseMS;
	};
	q31_t getThreshold() { return thresholdKnobPos; }
	void setThreshold(q31_t t) {
		thresholdKnobPos = t;
		threshold = 1 - ((thresholdKnobPos >> 1) / ONE_Q31f);
		updateER();
	}
	q31_t getRatio() { return ratioKnobPos; }
	void setRatio(q31_t rat) {
		ratioKnobPos = rat;
		ratio = 0.5 + (float(ratioKnobPos) / ONE_Q31f) / 2;
		updateER();
	}

	void updateER();
	float calc_rms(StereoSample* buffer, uint16_t numSamples);
	uint8_t gainReduction;

private:
	//parameters in use
	float a_;
	float r_;
	float ratio;
	float er;
	float threshdb;
	float threshold;

	//state
	float state;
	q31_t currentVolumeL;
	q31_t currentVolumeR;
	float rms;
	float mean;

	//for display
	float attackMS;
	float releaseMS;

	//raw knob positions
	q31_t thresholdKnobPos;
	q31_t ratioKnobPos;
	q31_t attackKnobPos;
	q31_t releaseKnobPos;
};
