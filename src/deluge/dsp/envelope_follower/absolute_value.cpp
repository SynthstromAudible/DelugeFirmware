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
#include "util/fixedpoint.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
/// |x|, but defined for INT_MIN (which gives INT_MAX) and one instruction. Negatives come out 1 low, which is nothing
/// next to the level this measures
inline int32_t absForLevel(int32_t x) {
	return x ^ (x >> 31);
}
} // namespace

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
// do not feed block over length 256 if it contains q31 data
StereoFloatSample AbsValueFollower::calcApproxRMS(std::span<StereoSample> buffer) {
	q31_t l = 0;
	q31_t r = 0;
	for (StereoSample sample : buffer) {
		l += absForLevel(sample.l) >> 8;
		r += absForLevel(sample.r) >> 8;
	}

	float ns = buffer.size();

	return calcApproxRMSFromMeans(256.f * l / ns, 256.f * r / ns, buffer.size());
}

StereoFloatSample AbsValueFollower::calcApproxRMSFromMeans(float newMeanL, float newMeanR, size_t numSamples) {
	StereoFloatSample logMean;
	float ns = numSamples;
	meanL = newMeanL;
	meanR = newMeanR;

	// warning this is not good math but it's pretty close and way cheaper than doing it properly
	// good math would use a long FIR, this is a one pole IIR instead
	// the more samples we have, the more weight we put on the current mean to avoid response slowing down
	// at high cpu loads
	meanL = (meanL * ns + lastMeanL) / (1 + ns);
	meanR = (meanR * ns + lastMeanR) / (1 + ns);

	lastMeanL = runEnvelope(lastMeanL, meanL, ns);
	logMean.l = std::log(lastMeanL + 1e-24f);

	lastMeanR = runEnvelope(lastMeanR, meanR, ns);
	logMean.r = std::log(lastMeanR + 1e-24f);

	return logMean;
}
// takes in knob positions in the range 0-ONE_Q31
void AbsValueFollower::setup(q31_t a, q31_t r) {
	setAttack(a);
	setRelease(r);
}
