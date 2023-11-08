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
	//attack/release in ms
	int32_t getAttack() { return attackms; }
	void setAttack(int32_t attack) {
		a_ = (-1000.0 / 44100) / (float(attack));
		attackms = attack;
	};
	int32_t getRelease() { return releasems; }
	void setRelease(int32_t release) {
		r_ = (-1000.0 / 44100) / (float(10 * release));
		releasems = release;
	};
	q31_t getThreshold() { return rawThreshold; }
	void setThreshold(q31_t t) {
		rawThreshold = t;
		threshold = 1 - ((rawThreshold >> 1) / ONE_Q31f);
		updateER();
	}
	q31_t getRatio() { return rawRatio; }
	void setRatio(q31_t rat) {
		rawRatio = rat;
		ratio = 0.5 + (float(rawRatio) / ONE_Q31f) / 2;
		updateER();
	}

	void updateER();
	float calc_rms(StereoSample* buffer, uint16_t numSamples);
	uint8_t gainReduction;

private:
	float a_;
	float r_;
	float ratio;
	float er;

	float threshdb;
	float state;
	float rms;
	float mean;
	q31_t currentVolumeL;
	q31_t currentVolumeR;

	q31_t rawThreshold;
	q31_t rawRatio;
	int32_t attackms;
	int32_t releasems;
	float threshold;
};
