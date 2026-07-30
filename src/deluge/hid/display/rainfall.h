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
#include <cstddef>
#include <cstdint>

namespace deluge::hid::display {

/// @brief A field of falling diagonal rain streaks, in the visual grammar of the Deluge logo.
///
/// Each drop is a rigid streak of three or four square cells stepping down-right at 45 degrees,
/// exactly as the streaks in the bootloader's logo do. Nothing ever draws the logo itself.
///
/// @note Deliberately has no display dependency, so the motion maths is unit-testable on the host
///       and the ASCII preview harness can drive it. That is why the panel dimensions below are
///       repeated rather than included -- cpu_specific.h is target-only.
/// @see docs/superpowers/specs/2026-07-30-deluge-rain-screensaver-design.md
class Rainfall {
public:
	/// @name Panel geometry
	/// @brief Mirrors OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS and OLED_MAIN_TOPMOST_PIXEL.
	///        screensaver.cpp static_asserts these against the real macros, so they cannot drift.
	/// @{
	static constexpr int32_t kWidth = 128;
	static constexpr int32_t kHeight = 48;
	static constexpr int32_t kTopmost = 5;
	static constexpr int32_t kVisibleHeight = kHeight - kTopmost;
	/// @}

	/// @name Tuning
	/// @{

	/// Drops in the field at once.
	static constexpr size_t kNumDrops = 34;
	/// Speed of the most distant drops, px per frame, applied to both axes.
	static constexpr float kSpeedFar = 0.45f;
	/// Speed of the nearest drops, px per frame, applied to both axes.
	static constexpr float kSpeedNear = 1.4f;

	/// @brief Chance that a respawning drop enters through the top edge rather than the left.
	///
	/// The ratio of the two edge lengths, which is what makes the flux uniform across the panel.
	/// Spawning only along the top starves the lower-right: everything drifts right, so a drop
	/// seeded past x = kWidth - kVisibleHeight leaves the right edge before it reaches the bottom.
	static constexpr float kSpawnFromTop = static_cast<float>(kWidth) / static_cast<float>(kWidth + kVisibleHeight);

	/// Candidate positions tried per spawn before settling for the roomiest one found.
	static constexpr int32_t kSpawnAttempts = 6;

	/// @brief Minimum distance allowed between two live drops of the same block size, px.
	///
	/// @note Only same-size pairs are checked. A near streak passing over a far one is the
	///       parallax reading correctly, and preventing it would need per-frame work.
	/// @param size Block size in pixels: 1, 2 or 3.
	/// @return The minimum permitted distance in pixels.
	[[nodiscard]] static constexpr float minGapFor(int32_t size) {
		return (size == 3) ? 5.0f : ((size == 2) ? 4.0f : 3.0f);
	}

	/// @}

	/// @brief A single square block of a streak.
	///
	/// @warning Unclipped -- the caller clips to the panel.
	struct Block {
		int32_t x;    ///< left edge, pixels
		int32_t y;    ///< top edge, pixels
		int32_t size; ///< 1, 2 or 3, pixels square
	};

	/// @brief A streak's shape and position, without its motion. Exposed so the spacing rule can
	///        be unit-tested against hand-built cases.
	struct Streak {
		float x;        ///< leading cell, left edge
		float y;        ///< leading cell, top edge
		int32_t size;   ///< 1, 2 or 3
		int32_t length; ///< 3 or 4 cells
	};

	/// @brief Squared distance between two streaks, px squared.
	///
	/// Both are parallel segments at 45 degrees, so this is closed-form: the offset across travel
	/// if the two overlap along travel, otherwise the hypotenuse of the across- and along-gaps.
	///
	/// @note Squared, so the caller compares against a squared minimum and no sqrt is needed.
	[[nodiscard]] static float separationSquared(const Streak& a, const Streak& b);

private:
	/// @brief Distance a streak's tail trails behind its head, on each axis, in pixels.
	[[nodiscard]] static constexpr float reachOf(int32_t size, int32_t length) {
		return static_cast<float>((length - 1) * size);
	}
};

} // namespace deluge::hid::display
