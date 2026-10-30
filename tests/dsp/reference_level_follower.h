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

// The app's level follower (AbsValueFollower::calcApproxRMS) before dsp-next owned it, kept as the oracle the
// LevelDetector spec checks against: verbatim but for its knob setters (unused), its StereoFloatSample (a local
// pair) and its sums and absolutes, written as the wrapping operations the target computes so the spec's
// signed-overflow sanitizer sees the same two's-complement result.

#include "dsp-next/core/stereo_sample.h"

#include <cmath>
#include <cstdint>
#include <span>

class ReferenceLevelFollower {
public:
	struct Level {
		float l;
		float r;
	};

	Level calcApproxRMS(std::span<deluge::dsp::StereoSample> buffer) {
		int32_t l = 0;
		int32_t r = 0;
		Level logMean;

		for (deluge::dsp::StereoSample sample : buffer) {
			l = wrap_add(l, wrap_abs(sample.l));
			r = wrap_add(r, wrap_abs(sample.r));
		}

		float ns = buffer.size();
		meanL = l / ns;
		meanR = r / ns;
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

	static int32_t wrap_add(int32_t a, int32_t b) {
		return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
	}
	static int32_t wrap_abs(int32_t a) { return a < 0 ? static_cast<int32_t>(0u - static_cast<uint32_t>(a)) : a; }

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
