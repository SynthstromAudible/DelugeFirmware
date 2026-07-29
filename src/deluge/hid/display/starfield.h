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

/// Perspective-projection starfield: stars hold a 3D position and are projected with
/// factor = fov/z, so they appear to fly toward the viewer as z decrements.
///
/// Ported from the deluge-sdk demo firmware
/// (firmwares/demo-firmware/src/tasks/oled.rs). Deliberately has no display
/// dependency, so the projection maths is unit-testable on the host.
class Starfield {
public:
	static constexpr size_t kNumStars = 60;
	/// Stars respawn at this depth once they pass the viewer.
	static constexpr float kMaxDepth = 32.0f;
	/// Depth consumed per frame.
	static constexpr float kZStep = 0.4f;
	/// Field of view, radians.
	static constexpr float kFov = 3.14159265358979323846f;
	/// Projected screen centre: (OLED_MAIN_TOPMOST_PIXEL + OLED_MAIN_HEIGHT_PIXELS) / 2 vertically.
	static constexpr float kCentreX = 64.0f;
	static constexpr float kCentreY = 26.0f;
	/// Stars nearer than this draw as 2x2 blocks rather than single pixels.
	static constexpr float kNearThreshold = kMaxDepth * 0.4f;
	/// Floor applied to z when projecting.
	///
	/// The reference implementation relies on Rust's saturating `f32 as i32`. C++
	/// static_cast of an out-of-range float is undefined behaviour, and z can land
	/// arbitrarily close to zero, so we bound the projection factor instead. A star
	/// this close is off-screen and respawns next frame, so nothing visible changes.
	static constexpr float kMinDepth = 0.01f;

	/// A star projected onto the screen. Coordinates are unclipped: the caller clips.
	struct Projected {
		int32_t x;
		int32_t y;
		int32_t size; ///< 1 or 2, in pixels square
	};

	/// Populates the field immediately, so the first rendered frame is already full.
	Starfield() { scatter(); }

	/// Re-scatter every star across the full depth range. Does *not* reseed the LCG,
	/// so successive calls produce different fields.
	void scatter();

	/// Advance every star one frame toward the viewer, respawning any that pass it.
	void advance();

	/// Project star `i` onto the screen.
	[[nodiscard]] Projected project(size_t i) const;

	/// Depth of star `i`. Exposed for tests.
	[[nodiscard]] float depthOf(size_t i) const { return stars_[i].z; }

private:
	struct Star {
		float x; ///< 3D position, roughly -256 to +256
		float y; ///< 3D position, roughly -100 to +100
		float z; ///< depth, 0 to kMaxDepth
	};

	/// Linear congruential generator, same constants as the reference implementation.
	uint32_t nextRandom();
	/// LCG output mapped to [0, 1).
	float nextRandomFloat();
	void respawn(Star& star, float z);

	std::array<Star, kNumStars> stars_{};
	uint32_t rngState_{0xCAFE5678};
};

} // namespace deluge::hid::display
