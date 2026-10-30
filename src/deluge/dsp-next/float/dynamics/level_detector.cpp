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
#include "dsp-next/float/dynamics/level_detector.h"

#include "dsp-next/core/wrapping.h"

#include <cmath>

namespace deluge::dsp::fp {

namespace {

/// |x|, wrapping at INT32_MIN as the target's negate does.
[[nodiscard]] q31 wrapping_abs(q31 x) {
	return x < 0 ? wrapping_sub(0, x) : x;
}

} // namespace

LevelDetector::LevelDetector(Settings settings)
    : attack_((-1000.0f / kSampleRateHz) / settings.attack_ms),
      release_((-1000.0f / kSampleRateHz) / settings.release_ms) {
}

float LevelDetector::follow(float current, float target, float num_samples) const {
	const float rate = (target > current) ? attack_ : release_;
	return target + (std::exp(rate * num_samples) * (current - target));
}

StereoLevel LevelDetector::measure(std::span<const StereoSample> block) {
	q31 sum_l = 0;
	q31 sum_r = 0;
	for (const StereoSample frame : block) {
		sum_l = wrapping_add(sum_l, wrapping_abs(frame.l));
		sum_r = wrapping_add(sum_r, wrapping_abs(frame.r));
	}
	const auto n = static_cast<float>(block.size());
	// Each mean is rounded to float before the smoothing weighs the block by its length against the last level.
	float mean_l = static_cast<float>(sum_l) / n;
	float mean_r = static_cast<float>(sum_r) / n;
	mean_l = ((mean_l * n) + smoothed_l_) / (1 + n);
	mean_r = ((mean_r * n) + smoothed_r_) / (1 + n);
	smoothed_l_ = follow(smoothed_l_, mean_l, n);
	const float level_l = std::log(smoothed_l_ + 1e-24f);
	smoothed_r_ = follow(smoothed_r_, mean_r, n);
	return {.l = level_l, .r = std::log(smoothed_r_ + 1e-24f)};
}

void LevelDetector::reset() {
	smoothed_l_ = 0;
	smoothed_r_ = 0;
}

} // namespace deluge::dsp::fp
