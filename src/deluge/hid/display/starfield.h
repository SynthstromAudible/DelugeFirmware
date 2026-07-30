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

/// @brief Perspective-projection starfield.
///
/// Stars hold a 3D position and are projected with factor = kFov / z, so they appear to fly toward
/// the viewer as z decrements.
///
/// @note Deliberately has no display dependency, so the projection maths is unit-testable on the
///       host.
/// @see The deluge-sdk demo firmware, firmwares/demo-firmware/src/tasks/oled.rs -- the reference
///      implementation this is ported from, and the source of the LCG constants and depth ranges.
class Starfield {
public:
	/// @name Field geometry
	/// @{

	/// Number of stars in the field.
	static constexpr size_t kNumStars = 60;
	/// Stars respawn at this depth once they pass the viewer.
	static constexpr float kMaxDepth = 32.0f;
	/// Depth consumed per frame.
	static constexpr float kZStep = 0.4f;
	/// Field of view, radians.
	static constexpr float kFov = 3.14159265358979323846f;
	/// Projected screen centre, X, in pixels.
	static constexpr float kCentreX = 64.0f;
	/// Projected screen centre, Y, in pixels: (OLED_MAIN_TOPMOST_PIXEL + OLED_MAIN_HEIGHT_PIXELS) / 2.
	static constexpr float kCentreY = 26.0f;
	/// Depth threshold: stars nearer than this (same depth units as kMaxDepth) draw as 2x2 blocks
	/// rather than single pixels.
	static constexpr float kNearThreshold = kMaxDepth * 0.4f;
	/// @brief Floor applied to z when projecting.
	///
	/// The reference implementation relies on Rust's saturating `f32 as i32`. A C++ static_cast of
	/// an out-of-range float is undefined behaviour, and z can land arbitrarily close to zero, so
	/// the projection factor is bounded instead.
	///
	/// @note A star this close is off-screen and respawns next frame, so nothing visible changes.
	static constexpr float kMinDepth = 0.01f;

	/// @}

	/// @brief A star projected onto the screen.
	///
	/// @warning Coordinates are unclipped -- the caller clips them to the panel.
	struct Projected {
		int32_t x;    ///< screen X, pixels
		int32_t y;    ///< screen Y, pixels
		int32_t size; ///< 1 or 2, in pixels square
	};

	/// @brief Construct a field that is already fully populated, so the first rendered frame is not
	///        empty.
	Starfield() { scatter(); }

	/// @brief Re-scatter every star across the full depth range.
	///
	/// @note Does *not* reseed the LCG, so successive calls produce different fields.
	void scatter();

	/// @brief Advance every star one frame toward the viewer, respawning any that pass it.
	void advance();

	/// @brief Project a star onto the screen.
	///
	/// @param i Star index, less than kNumStars.
	/// @return The star's unclipped screen position and pixel size.
	[[nodiscard]] Projected project(size_t i) const;

	/// @brief Depth of a star. Exposed for tests.
	///
	/// @param i Star index, less than kNumStars.
	/// @return The star's z, always strictly positive.
	[[nodiscard]] float depthOf(size_t i) const { return stars_[i].z; }

private:
	struct Star {
		float x; ///< 3D position, roughly -256 to +256
		float y; ///< 3D position, roughly -100 to +100
		float z; ///< depth, positive, roughly up to kMaxDepth
	};

	/// @brief Step the linear congruential generator.
	///
	/// @return The next raw LCG output.
	uint32_t nextRandom();

	/// @brief Step the linear congruential generator, mapped to [0, 1).
	///
	/// @return The next LCG output, mapped to [0, 1).
	float nextRandomFloat();

	/// @brief Give a star a fresh random x/y at the given depth.
	///
	/// @param star The star to overwrite.
	/// @param z    Depth to place it at.
	void respawn(Star& star, float z);

	std::array<Star, kNumStars> stars_{};
	uint32_t rngState_{0xCAFE5678};
};

} // namespace deluge::hid::display
