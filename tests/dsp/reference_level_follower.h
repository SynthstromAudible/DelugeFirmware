/*
 * Copyright © 2026 Synthstrom Audible Limited
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

// The app's level follower (AbsValueFollower::calcApproxRMS and calcApproxRMSFromMeans, as upstream has it), kept as
// the oracle the LevelDetector spec checks against: verbatim but for its knob setters (unused) and its
// StereoFloatSample (a local pair).

#include "raw_frame.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

class ReferenceLevelFollower {
public:
	struct Level {
		float l;
		float r;
	};

	// do not feed block over length 256 if it contains q31 data
	Level calcApproxRMS(std::span<deluge::dsp::test::RawFrame> buffer) {
		int32_t l = 0;
		int32_t r = 0;
		for (deluge::dsp::test::RawFrame sample : buffer) {
			l += absForLevel(sample.l) >> 8;
			r += absForLevel(sample.r) >> 8;
		}

		float ns = buffer.size();

		return calcApproxRMSFromMeans(256.f * l / ns, 256.f * r / ns, buffer.size());
	}

	Level calcApproxRMSFromMeans(float newMeanL, float newMeanR, size_t numSamples) {
		Level logMean;
		float ns = numSamples;
		meanL = newMeanL;
		meanR = newMeanR;

		meanL = (meanL * ns + lastMeanL) / (1 + ns);
		meanR = (meanR * ns + lastMeanR) / (1 + ns);

		lastMeanL = runEnvelope(lastMeanL, meanL, ns);
		logMean.l = std::log(lastMeanL + 1e-24f);

		lastMeanR = runEnvelope(lastMeanR, meanR, ns);
		logMean.r = std::log(lastMeanR + 1e-24f);

		return logMean;
	}

private:
	static constexpr int32_t kSampleRate = 44100;

	/// |x|, but defined for INT_MIN (which gives INT_MAX) and one instruction. Negatives come out 1 low, which is
	/// nothing next to the level this measures
	static int32_t absForLevel(int32_t x) { return x ^ (x >> 31); }

	float runEnvelope(float current, float desired, float numSamples) {
		float s;
		if (desired > current) {
			s = desired + std::exp(a_ * numSamples) * (current - desired);
		}
		else {
			s = desired + std::exp(r_ * numSamples) * (current - desired);
		}
		return s;
	}

	float attackMS{1};
	float releaseMS{10};
	float a_ = (-1000.0f / kSampleRate) / attackMS;
	float r_ = (-1000.0f / kSampleRate) / releaseMS;
	float meanL{0};
	float lastMeanL{0};
	float meanR{0};
	float lastMeanR{0};
};
