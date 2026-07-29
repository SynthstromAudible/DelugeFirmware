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

#include "hid/display/starfield.h"
#include <algorithm>

namespace deluge::hid::display {

uint32_t Starfield::nextRandom() {
	rngState_ = rngState_ * 1664525u + 1013904223u;
	return rngState_;
}

float Starfield::nextRandomFloat() {
	return static_cast<float>(nextRandom()) * (1.0f / 4294967296.0f);
}

void Starfield::respawn(Star& star, float z) {
	// Wide 3D range, so that even at kMaxDepth (factor ~= 0.098) the field still
	// spans the whole panel: x=+/-256 reaches +/-25px, y=+/-100 reaches +/-10px.
	star.x = nextRandomFloat() * 512.0f - 256.0f;
	star.y = nextRandomFloat() * 200.0f - 100.0f;
	star.z = z;
}

void Starfield::scatter() {
	for (Star& star : stars_) {
		// Spread across the depth range so the field is populated immediately.
		respawn(star, nextRandomFloat() * kMaxDepth + 0.1f);
	}
}

void Starfield::advance() {
	for (Star& star : stars_) {
		star.z -= kZStep;
		if (star.z <= 0.0f) {
			respawn(star, kMaxDepth);
		}
	}
}

Starfield::Projected Starfield::project(size_t i) const {
	const Star& star = stars_[i];
	const float factor = kFov / std::max(star.z, kMinDepth);
	return {
	    .x = static_cast<int32_t>(star.x * factor + kCentreX),
	    // Negate y so that positive-y is up on screen, matching the reference.
	    .y = static_cast<int32_t>(-star.y * factor + kCentreY),
	    .size = (star.z < kNearThreshold) ? 2 : 1,
	};
}

} // namespace deluge::hid::display
