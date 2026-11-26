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

#include "dsp/mic_velocity/echo_canceller.h"
#include <cmath>

namespace deluge::dsp::mic_velocity {

namespace {

// NLMS step size: how much of each error the filter corrects for (0-1). Small, so a loud passage doesn't throw it.
constexpr float kStep = 0.05f;
// Under this sent power there's nothing to learn from (and dividing by it would blow the weights up)
constexpr float kMinPower = 1e-9f;
// The running sum of squares drifts in float; it's recomputed this often
constexpr int32_t kRecomputeEvery = 4096;

} // namespace

void EchoCanceller::reset() {
	sent_.fill(0);
	weights_.fill(0);
	pos_ = 0;
	power_ = 0;
	sinceRecompute_ = 0;
}

float EchoCanceller::weightEnergy() const {
	float sum = 0;
	for (float w : weights_) {
		sum += w * w;
	}
	return sum;
}

float EchoCanceller::process(float mic, float sent, bool adapt) {
	// Newest first: sent_[pos_ + k] is the sample from k samples ago, for k < kHistory
	pos_ = (pos_ + kHistory - 1) % kHistory;
	float const leaving = sent_[pos_]; // kHistory samples ago, now past the filter
	sent_[pos_] = sent;
	sent_[pos_ + kHistory] = sent;
	float const* const x = &sent_[pos_ + kDelay];
	float const entering = x[0];
	power_ += entering * entering - leaving * leaving;

	if (++sinceRecompute_ >= kRecomputeEvery || power_ < 0) {
		sinceRecompute_ = 0;
		power_ = 0;
		for (int32_t j = 0; j < kTaps; j++) {
			power_ += x[j] * x[j];
		}
	}

	float estimate = 0;
	for (int32_t j = 0; j < kTaps; j++) {
		estimate += weights_[j] * x[j];
	}
	float const error = mic - estimate;

	if (!std::isfinite(error)) { // Shouldn't happen, but never let it stick
		reset();
		return mic;
	}

	if (adapt && power_ > kMinPower) {
		float const g = kStep * error / power_;
		for (int32_t j = 0; j < kTaps; j++) {
			weights_[j] += g * x[j];
		}
	}
	return error;
}

} // namespace deluge::dsp::mic_velocity
