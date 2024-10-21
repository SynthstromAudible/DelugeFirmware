/*
 * Copyright © 2024 Mark Adams
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

#include "dsp/stereo_sample.h"
#include <cmath>
class AbsValueFollower {
public:
	AbsValueFollower() = default;
	void setup(q31_t attack, q31_t release);

	/// Reset the state of the envelope follower
	void reset() {
		meanL = 0;
		lastMeanL = 0;
		meanR = 0;
		lastMeanR = 0;
	}

	// attack/release in range 0 to 2^31
	inline q31_t getAttack() { return attackKnobPos; }
	inline int32_t getAttackMS() { return attackMS; }
	int32_t setAttack(q31_t attack) {
		// this exp will be between 1 and 7ish, half the knob range is about 2.5
		attackMS = 0.5 + (std::exp(2 * float(attack) / ONE_Q31f) - 1) * 10;
		a_ = (-1000.0 / kSampleRate) / attackMS;
		attackKnobPos = attack;
		return attackMS;
	};

	inline q31_t getRelease() { return releaseKnobPos; }
	inline int32_t getReleaseMS() { return releaseMS; }
	int32_t setRelease(q31_t release) {
		// this exp will be between 1 and 7ish, half the knob range is about 2.5
		releaseMS = 50 + (std::exp(2 * float(release) / ONE_Q31f) - 1) * 50;
		r_ = (-1000.0 / kSampleRate) / releaseMS;
		releaseKnobPos = release;
		return releaseMS;
	};

	StereoFloatSample calcApproxRMS(StereoSample* buffer, uint16_t numSamples);

private:
	float runEnvelope(float current, float desired, float numSamples);
	// for display
	float attackMS{1};
	float releaseMS{10};
	// parameters in use
	float a_ = a_ = (-1000.0f / kSampleRate) / attackMS;

	float r_ = (-1000.0f / kSampleRate) / releaseMS;

	// state
	float state{0};
	float rms{0};
	float meanL{0};
	float lastMeanL{0};
	float meanR{0};
	float lastMeanR{0};

	// raw knob positions
	q31_t attackKnobPos{0};
	q31_t releaseKnobPos{0};
};
