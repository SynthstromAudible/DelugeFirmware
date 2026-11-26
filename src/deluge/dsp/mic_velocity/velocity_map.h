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

#include <array>
#include <cstdint>

/// From a thump's level to a velocity, for mic velocity.
///
/// Measured on a Deluge (2026-11), how loud a tap is in the mic depends a lot on the pad: soft taps on the bottom
/// corners came out over 20 dB quieter than on the centre, hard taps only ~9 dB. So calibration takes the softest
/// and hardest a player taps at five pads (the centre and the corners), and any pad's soft and hard levels are
/// interpolated from those. A thump's level then maps to where it falls between its pad's soft and hard, through a
/// curve, onto the velocity range.
namespace deluge::dsp::mic_velocity {

struct GridPoint {
	int8_t x;
	int8_t y;
};

/// The pads calibrated, in the order they're tapped
constexpr int32_t kNumCalibrationPoints = 5;
constexpr std::array<GridPoint, kNumCalibrationPoints> kCalibrationPoints{{{7, 4}, {0, 7}, {15, 7}, {0, 0}, {15, 0}}};

/// The softest and hardest a player taps at each calibration point (thump levels in dB of full scale), and from them
/// every pad's, worked out once when it's made
class Calibration {
public:
	/// Every pad, the sidebar included (the audition pads are its right-hand column)
	static constexpr int32_t kWidth = 18;
	static constexpr int32_t kHeight = 8;
	using PointLevels = std::array<float, kNumCalibrationPoints>;

	Calibration() = default;
	Calibration(PointLevels const& softDb, PointLevels const& hardDb);

	/// The calibrated points' levels (what's saved)
	[[nodiscard]] PointLevels const& softDb() const { return softDb_; }
	[[nodiscard]] PointLevels const& hardDb() const { return hardDb_; }

	/// A pad's soft and hard levels: the points' weighted by closeness (exact on one)
	[[nodiscard]] float softAt(int32_t x, int32_t y) const { return softAt_[clampY(y)][clampX(x)]; }
	[[nodiscard]] float hardAt(int32_t x, int32_t y) const { return hardAt_[clampY(y)][clampX(x)]; }

private:
	static int32_t clampX(int32_t x) { return x < 0 ? 0 : (x >= kWidth ? kWidth - 1 : x); }
	static int32_t clampY(int32_t y) { return y < 0 ? 0 : (y >= kHeight ? kHeight - 1 : y); }

	PointLevels softDb_{};
	PointLevels hardDb_{};
	std::array<std::array<float, kWidth>, kHeight> softAt_{};
	std::array<std::array<float, kWidth>, kHeight> hardAt_{};
};

/// How tap strength maps onto velocity (the settings)
struct VelocityCurve {
	static constexpr int32_t kMinShape = -50;
	static constexpr int32_t kMaxShape = 50;

	uint8_t minVelocity = 20;  ///< What the softest calibrated tap (and anything softer) plays at
	uint8_t maxVelocity = 127; ///< What the hardest (and anything harder) plays at
	/// 0: velocity follows the tap's level in dB. Positive: lighter taps play louder (more sensitive); negative: it
	/// takes harder taps to get high.
	int8_t shape = 25;
};

/// The velocity (1-127) a thump of levelDb plays at on pad (x, y)
[[nodiscard]] uint8_t velocityFor(Calibration const& calibration, VelocityCurve const& curve, int32_t x, int32_t y,
                                  float levelDb);

/// Checking a calibration point's taps (two soft, two hard)
enum class TapCheck : uint8_t {
	OK,
	SOFT_UNEVEN, ///< The two soft taps are too far apart: tap soft again
	HARD_UNEVEN, ///< The two hard taps are too far apart: tap hard again
	TOO_CLOSE,   ///< Soft and hard came out too alike: the whole point again
};

/// Two taps of the same strength have to come within this of each other, and hard has to beat soft by this
constexpr float kMaxTapSpreadDb = 6.f;
constexpr float kMinRangeDb = 6.f;

/// Checks a point's taps and, if they're fine, gives its soft and hard levels (each the mean of its two, in dB)
TapCheck checkPoint(std::array<float, 2> const& softDb, std::array<float, 2> const& hardDb, float* softOut,
                    float* hardOut);

} // namespace deluge::dsp::mic_velocity
