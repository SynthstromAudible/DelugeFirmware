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

#include "dsp/mic_velocity/velocity_map.h"
#include <algorithm>
#include <cmath>

namespace deluge::dsp::mic_velocity {

namespace {

/// Inverse-distance-squared weighting of the calibration points' values
float interpolate(std::array<float, kNumCalibrationPoints> const& values, int32_t x, int32_t y) {
	float sum = 0;
	float weights = 0;
	for (int32_t p = 0; p < kNumCalibrationPoints; p++) {
		float const dx = static_cast<float>(x - kCalibrationPoints[p].x);
		float const dy = static_cast<float>(y - kCalibrationPoints[p].y);
		float const d2 = dx * dx + dy * dy;
		if (d2 == 0) {
			return values[p];
		}
		sum += values[p] / d2;
		weights += 1.f / d2;
	}
	return sum / weights;
}

} // namespace

Calibration::Calibration(PointLevels const& softDb, PointLevels const& hardDb) : softDb_(softDb), hardDb_(hardDb) {
	for (int32_t y = 0; y < kHeight; y++) {
		for (int32_t x = 0; x < kWidth; x++) {
			softAt_[y][x] = interpolate(softDb_, x, y);
			hardAt_[y][x] = interpolate(hardDb_, x, y);
		}
	}
}

uint8_t velocityFor(Calibration const& calibration, VelocityCurve const& curve, int32_t x, int32_t y, float levelDb) {
	float const soft = calibration.softAt(x, y);
	float const hard = calibration.hardAt(x, y);
	float const range = std::max(hard - soft, 1.f);
	float const t = std::clamp((levelDb - soft) / range, 0.f, 1.f);
	// Shape +50 bends it to t^(1/4), -50 to t^4
	float const exponent = std::pow(4.f, -static_cast<float>(curve.shape) / VelocityCurve::kMaxShape);
	float const shaped = std::pow(t, exponent);
	float const low = curve.minVelocity;
	float const high = std::max(curve.maxVelocity, curve.minVelocity);
	return static_cast<uint8_t>(std::clamp(std::lround(low + (high - low) * shaped), 1L, 127L));
}

TapCheck checkPoint(std::array<float, 2> const& softDb, std::array<float, 2> const& hardDb, float* softOut,
                    float* hardOut) {
	if (std::abs(softDb[0] - softDb[1]) > kMaxTapSpreadDb) {
		return TapCheck::SOFT_UNEVEN;
	}
	if (std::abs(hardDb[0] - hardDb[1]) > kMaxTapSpreadDb) {
		return TapCheck::HARD_UNEVEN;
	}
	float const soft = (softDb[0] + softDb[1]) / 2;
	float const hard = (hardDb[0] + hardDb[1]) / 2;
	if (hard - soft < kMinRangeDb) {
		return TapCheck::TOO_CLOSE;
	}
	*softOut = soft;
	*hardOut = hard;
	return TapCheck::OK;
}

} // namespace deluge::dsp::mic_velocity
