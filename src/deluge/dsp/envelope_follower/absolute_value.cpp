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

#include "absolute_value.h"
#include <cmath>

float AbsValueFollower::runEnvelope(float current, float desired, float numSamples) {
	float s;
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
StereoFloatSample AbsValueFollower::calcApproxRMS(StereoSample* buffer, uint16_t numSamples) {
	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	q31_t l = 0;
	q31_t r = 0;
	StereoFloatSample logMean;

	do {
		l += std::abs(thisSample->l);
		r += std::abs(thisSample->r);
	} while (++thisSample != bufferEnd);

	auto ns = float(numSamples);
	meanL = (float(l)) / ns;
	meanR = (float(r)) / ns;
	// warning this is not good math but it's pretty close and way cheaper than doing it properly
	// good math would use a long FIR, this is a one pole IIR instead
	// the more samples we have, the more weight we put on the current mean to avoid response slowing down
	// at high cpu loads
	meanL = (meanL * ns + lastMeanL) / (1 + ns);
	meanR = (meanR * ns + lastMeanR) / (1 + ns);

	lastMeanL = runEnvelope(lastMeanL, meanL, numSamples);
	logMean.l = std::log(lastMeanL + 1e-24f);

	lastMeanR = runEnvelope(lastMeanR, meanR, numSamples);
	logMean.r = std::log(lastMeanR + 1e-24f);

	return logMean;
}
// takes in knob positions in the range 0-ONE_Q31
void AbsValueFollower::setup(q31_t a, q31_t r) {
	setAttack(a);
	setRelease(r);
}
