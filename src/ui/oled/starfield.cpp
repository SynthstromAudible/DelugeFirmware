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

#include "ui/oled/starfield.h"

// For the panel band `drawBlock` clips to, which lives on `Rainfall` because that is where it
// lived before the move. Legacy has exactly one `drawBlock`, shared by both animations
// (`hid/display/screensaver.cpp:64-77`), so both draw calls share one clip here too.
#include "ui/oled/frame.h"
#include "ui/oled/rainfall.h"

#include <algorithm>

namespace deluge::ui::oled {

uint32_t Starfield::next_random() {
	rng_state_ = rng_state_ * 1664525u + 1013904223u;
	return rng_state_;
}

float Starfield::next_random_float() {
	return static_cast<float>(next_random()) * (1.0f / 4294967296.0f);
}

void Starfield::respawn(Star& star, float z) {
	// Wide 3D range so the projected field still spans the whole panel even at kMaxDepth
	// (factor ~= 0.098): x=+/-256 -> +/-25px, y=+/-100 -> +/-10px.
	star.x = next_random_float() * 512.0f - 256.0f;
	star.y = next_random_float() * 200.0f - 100.0f;
	star.z = z;
}

void Starfield::scatter() {
	for (Star& star : stars_) {
		// Spread across the full depth range rather than all starting at kMaxDepth (as advance()'s
		// respawn does), so the field already looks populated and in motion on the first frame.
		respawn(star, next_random_float() * kMaxDepth + 0.1f);
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
	    // Screen y increases downward, but the star's 3D y is defined with positive-y up (matching
	    // the reference implementation) -- negate to convert between the two conventions.
	    .y = static_cast<int32_t>(-star.y * factor + kCentreY),
	    .size = (star.z < kNearThreshold) ? 2 : 1,
	};
}

void draw_starfield(OledFrame& f, const Starfield& field) {
	// `Screensaver::renderStarfield`, `hid/display/screensaver.cpp:225-230`. Same band and same
	// block helper as `draw_rainfall`; see its `band_of` for why the band is the animation's own
	// and not the frame's panel.
	const gfx::oled::Rect band{-f.origin.x, -f.origin.y + Rainfall::kTopmost, Rainfall::kWidth,
	                           Rainfall::kVisibleHeight};
	f.region(band, InputMask::none(), [&](OledFrame& g) {
		for (size_t i = 0; i < Starfield::kNumStars; i++) {
			const Starfield::Projected star = field.project(i);
			draw_block(g, Rainfall::Block{.x = star.x, .y = star.y - Rainfall::kTopmost, .size = star.size});
		}
	});
}

} // namespace deluge::ui::oled
