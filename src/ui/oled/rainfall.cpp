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

#include "ui/oled/rainfall.h"

#include "ui/oled/frame.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace deluge::ui::oled {

namespace {
// Extent a streak occupies along its direction of travel, in the doubled u = x + y axis. Cell i
// sits at (x - i*size, y - i*size), so each cell costs 2*size of u.
constexpr float span_along_travel(int32_t size, int32_t length) {
	return 2.0f * static_cast<float>((length - 1) * size);
}

// The Deluge logo, as cells on an 11x10 grid packed (x << 4) | y. Transcribed verbatim from the
// bootloader's logoPixels, in the same packing so the two are visibly the same table. The 25 cells
// form seven diagonal runs of three or four -- the same shape a Drop is, which is what lets the
// logo fall through the field as one of its own rather than as a special case.
constexpr uint8_t kLogoPixels[] = {
    (1 << 4) | 0,  (2 << 4) | 1, (3 << 4) | 2, (4 << 4) | 3, (4 << 4) | 0, (5 << 4) | 1, (6 << 4) | 2,
    (7 << 4) | 3,  (0 << 4) | 2, (1 << 4) | 3, (2 << 4) | 4, (3 << 4) | 5, (5 << 4) | 5, (6 << 4) | 6,
    (7 << 4) | 7,  (1 << 4) | 6, (2 << 4) | 7, (3 << 4) | 8, (4 << 4) | 9, (8 << 4) | 5, (9 << 4) | 6,
    (10 << 4) | 7, (5 << 4) | 7, (6 << 4) | 8, (7 << 4) | 9,
};
static_assert(std::size(kLogoPixels) == Rainfall::kLogoCells);
} // namespace

float Rainfall::separation_squared(const Streak& a, const Streak& b) {
	// Across travel. Every drop moves along (1, 1), so x - y is invariant under motion -- and
	// stays invariant even between two drops moving at different speeds. Two streaks with a large
	// offset here can never touch, whatever they do along travel.
	const float across = (a.x - a.y) - (b.x - b.y);

	// Along travel, as the interval [tail, head] in u = x + y.
	const float a_head = a.x + a.y;
	const float b_head = b.x + b.y;
	const float a_tail = a_head - span_along_travel(a.size, a.length);
	const float b_tail = b_head - span_along_travel(b.size, b.length);

	// Zero when the intervals overlap, in which case the streaks are separated purely across
	// travel and the term below drops out.
	const float along = std::max(0.0f, std::max(a_tail, b_tail) - std::min(a_head, b_head));

	// Both axes are sqrt(2) times the true distances, so the true squared distance is halved.
	return 0.5f * (across * across + along * along);
}

uint32_t Rainfall::next_random() {
	rng_state_ = rng_state_ * 1664525u + 1013904223u;
	return rng_state_;
}

float Rainfall::next_random_float() {
	return static_cast<float>(next_random()) * (1.0f / 4294967296.0f);
}

void Rainfall::roll(Drop& drop) {
	// Depth drives both size and speed. The panel is 1-bit, so only three block sizes exist and
	// depth is quantised for size -- but several speeds share each size, which is what stops the
	// field reading as three sheets sliding in lockstep.
	const float depth = next_random_float();
	drop.size = (depth > 0.66f) ? 3 : ((depth > 0.33f) ? 2 : 1);

	// Quantised to kSpeedStep, and that granularity is the whole point rather than an economy.
	// A drop only moves when its accumulated position crosses a pixel boundary, so a speed with a
	// small denominator crosses on a short repeating cycle: 1.25 px/frame advances 1, 1, 1, 2 for
	// ever. An arbitrary real speed crosses on a quasi-periodic schedule instead, so a drop that
	// mostly moves every frame stalls for one at unpredictable moments -- which reads as judder,
	// not as slower motion. Every multiple of a quarter is also exactly representable, so the
	// cadence never drifts however long the screensaver runs.
	const float raw = kSpeedFar + (kSpeedNear - kSpeedFar) * depth;
	drop.speed = std::round(raw / kSpeedStep) * kSpeedStep;
	// Three or four cells, the streak lengths used in the bootloader's logo.
	drop.length = (next_random_float() < 0.5f) ? 3 : 4;
}

void Rainfall::place(Drop& drop, bool seeded) {
	const float reach = reach_of(drop.size, drop.length);

	if (seeded) {
		// Scattering: anywhere on the panel, so the first frame after activation is already full.
		drop.x = next_random_float() * (kWidth + reach) - reach;
		drop.y = next_random_float() * (kVisibleHeight + reach) + kTopmost - reach;
	}
	else if (next_random_float() < kSpawnFromTop) {
		drop.x = next_random_float() * (kWidth + reach) - reach;
		drop.y = kTopmost - reach - 1.0f;
	}
	else {
		// Through the left edge, so drops keep arriving in the region the rightward drift empties.
		drop.x = -reach - 1.0f;
		drop.y = next_random_float() * kVisibleHeight + kTopmost;
	}

	// Pin y to x by a whole-pixel offset, and keep it there: advance() derives y rather than
	// integrating it. Two things depend on this, and both are required to stop the streak
	// wiggling. The axes must share a fractional phase, so cell_at() crosses both pixel boundaries
	// on the same frame and every step is a clean diagonal. And y must not drift from x through
	// float rounding, which it does if both are accumulated independently -- the two sit at
	// different magnitudes, so the same += speed rounds differently and they slip a frame apart.
	// Either failure makes a drop step right on one frame and down on the next, oscillating about
	// its own axis: on a 3x3 block, a visible wiggle.
	drop.axis_offset = static_cast<int32_t>(std::lround(drop.x - drop.y));
	drop.y = drop.x - static_cast<float>(drop.axis_offset);
}

void Rainfall::spawn(size_t index, bool seeded) {
	Drop best{};
	float best_clearance = -1.0f;

	for (int32_t attempt = 0; attempt < kSpawnAttempts; attempt++) {
		Drop candidate{};
		roll(candidate);
		place(candidate, seeded);

		// Only same-size drops are checked: cross-size overlap is the parallax reading correctly.
		// Slots still at size zero have not been placed yet, and never match.
		float clearance = std::numeric_limits<float>::max();
		for (size_t other = 0; other < kNumDrops; other++) {
			if (other == index || drops_[other].size != candidate.size) {
				continue;
			}
			clearance = std::min(clearance, separation_squared(streak_of(candidate), streak_of(drops_[other])));
		}

		const float min_gap = min_gap_for(candidate.size);
		if (clearance >= min_gap * min_gap) {
			drops_[index] = candidate;
			return;
		}
		if (clearance > best_clearance) {
			best = candidate;
			best_clearance = clearance;
		}
	}

	// Nothing cleared the bar. Take the roomiest candidate rather than looping: bounded work
	// matters more here than a perfect placement, and same-size drops differ in speed, so a tight
	// pair drifts apart on its own.
	drops_[index] = best;
}

void Rainfall::emit_logo() {
	// Weighted away from the smallest scale, where the far drizzle camouflages the mark.
	const float pick = next_random_float();
	float band_low = 0.0f;
	if (pick < kLogoSmallScaleChance) {
		logo_.size = 1;
		band_low = 0.0f;
	}
	else if (pick < kLogoSmallScaleChance + (1.0f - kLogoSmallScaleChance) * 0.5f) {
		logo_.size = 2;
		band_low = 0.33f;
	}
	else {
		logo_.size = 3;
		band_low = 0.66f;
	}

	// Speed is never special-cased: draw a depth from the chosen scale's own band and run it
	// through the same mapping a drop uses, so a big logo flashes past and a small one lingers.
	const float depth = band_low + next_random_float() * 0.33f;
	const float raw = kSpeedFar + (kSpeedNear - kSpeedFar) * depth;
	logo_.speed = std::round(raw / kSpeedStep) * kSpeedStep;

	const float grid_w = static_cast<float>(kLogoGridWidth * logo_.size);
	const float grid_h = static_cast<float>(kLogoGridHeight * logo_.size);
	if (next_random_float() < kSpawnFromTop) {
		logo_.x = next_random_float() * (kWidth + grid_w) - grid_w;
		logo_.y = kTopmost - grid_h;
	}
	else {
		logo_.x = -grid_w;
		logo_.y = next_random_float() * kVisibleHeight + kTopmost;
	}

	// Same contract as a Drop: pin y to x by a whole-pixel offset and derive it from here on.
	logo_.axis_offset = static_cast<int32_t>(std::lround(logo_.x - logo_.y));
	logo_.y = logo_.x - static_cast<float>(logo_.axis_offset);
	logo_.active = true;
}

void Rainfall::advance_logo() {
	if (logo_.active) {
		logo_.x += logo_.speed;
		logo_.y = logo_.x - static_cast<float>(logo_.axis_offset);
		// The grid extends down and right from its origin, so once the origin itself passes the far
		// edges every cell has gone with it.
		if (logo_.y > kHeight || logo_.x > kWidth) {
			logo_.active = false;
			logo_countdown_ = next_logo_interval();
		}
		return;
	}

	if (logo_countdown_ > 0) {
		logo_countdown_--;
		return;
	}
	emit_logo();
}

int32_t Rainfall::next_logo_interval() {
	const float span = static_cast<float>(kLogoIntervalMaxFrames - kLogoIntervalMinFrames);
	return kLogoIntervalMinFrames + static_cast<int32_t>(next_random_float() * span);
}

void Rainfall::scatter() {
	// Zeroed slots have size zero, which spawn() treats as "not yet placed" -- so each drop is
	// only spaced against the ones already down.
	drops_.fill(Drop{});
	for (size_t index = 0; index < kNumDrops; index++) {
		spawn(index, true);
	}

	// No logo at activation, and a fresh random wait: a sighting must stay a surprise rather than
	// become something that reliably greets you when the screensaver appears.
	logo_ = Logo{};
	logo_countdown_ = next_logo_interval();
}

void Rainfall::advance() {
	for (size_t index = 0; index < kNumDrops; index++) {
		Drop& drop = drops_[index];
		// Only x is integrated; y follows it exactly, so the drop travels along its own 45 degree
		// axis and the streak never slides sideways underneath itself. See place() for why y is
		// derived rather than accumulated.
		drop.x += drop.speed;
		drop.y = drop.x - static_cast<float>(drop.axis_offset);

		const float reach = reach_of(drop.size, drop.length);
		if (drop.y - reach > kHeight || drop.x - reach > kWidth) {
			spawn(index, false);
		}
	}

	advance_logo();
}

Rainfall::Block Rainfall::logo_cell_at(size_t cell) const {
	const int32_t packed = kLogoPixels[cell];
	const int32_t grid_x = packed >> 4;
	const int32_t grid_y = packed & 0x0F;
	// Both axes from the same floored x, exactly as cell_at() does, so the logo cannot wiggle.
	const int32_t x = static_cast<int32_t>(std::floor(logo_.x));
	return {
	    .x = x + grid_x * logo_.size,
	    .y = x - logo_.axis_offset + grid_y * logo_.size,
	    .size = logo_.size,
	};
}

Rainfall::Block Rainfall::cell_at(size_t drop, size_t cell) const {
	const Drop& d = drops_[drop];
	const int32_t step = static_cast<int32_t>(cell) * d.size;
	// Both axes come from the same floored x, so they cross pixel boundaries on the same frame no
	// matter what the float arithmetic does. Flooring x and y independently would not: a cast
	// truncates toward zero, so a drop still left of the panel (x negative, y not) converts its two
	// axes by different rules, and even flooring both leaves y's value exposed to rounding at a
	// different magnitude than x. Either way the drop steps right on one frame and down on the
	// next, oscillating about its own axis -- on a 3x3 block, a visible wiggle.
	const int32_t x = static_cast<int32_t>(std::floor(d.x));
	return {
	    .x = x - step,
	    .y = x - d.axis_offset - step,
	    .size = d.size,
	};
}

namespace {

/// @brief The band legacy's `drawBlock` clips to, in @p f's own coordinates.
///
/// `drawBlock` (`hid/display/screensaver.cpp:63-76`) admits `x` in
/// `[0, OLED_MAIN_WIDTH_PIXELS)` and `y` in
/// `[OLED_MAIN_TOPMOST_PIXEL, OLED_MAIN_HEIGHT_PIXELS)`, which `hid/display/screensaver.cpp:37-39`
/// static_asserts to be exactly `Rainfall::kWidth`, `kTopmost` and `kHeight`. A frame's origin is
/// the *visible* top of its own panel, so canvas `(0, 0)` sits at local `(-origin.x, -origin.y)` --
/// the conversion `popup.cpp`'s `panel_of` makes, and the reason it is needed here too: every
/// coordinate the animation computes is against the whole panel, bezel-hidden rows included.
///
/// @param f The frame to convert against.
/// @return The band, in @p f's local coordinates.
gfx::oled::Rect band_of(const OledFrame& f) {
	return {-f.origin.x, -f.origin.y + Rainfall::kTopmost, Rainfall::kWidth, Rainfall::kVisibleHeight};
}

} // namespace

void draw_block(OledFrame& f, const Rainfall::Block& block) {
	// The `size x size` filled square of `drawBlock` (`hid/display/screensaver.cpp:67-75`), as
	// `size` spans rather than `size * size` pixels: `Canvas::drawHorizontalLine` sets exactly the
	// same pixels `drawPixel` would, one page-masked byte at a time. Clipping is the frame's --
	// `band_of` has already narrowed it to the band legacy's own bounds test admits, on all four
	// edges.
	for (int32_t dy = 0; dy < block.size; dy++) {
		f.hline(block.y + dy, block.x, block.x + block.size - 1);
	}
}

void draw_rainfall(OledFrame& f, const Rainfall& field) {
	// `InputMask::none()`: this animation reads no input and claims none. The band is a region
	// rather than a manual bounds test so that the clip applies to every pixel drawn inside it,
	// including a block that straddles an edge -- which is what `drawBlock`'s own per-pixel test
	// did.
	f.region(band_of(f), InputMask::none(), [&](OledFrame& g) {
		// `Screensaver::renderRainfall`, `hid/display/screensaver.cpp:202-208`.
		for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
			for (size_t cell = 0; cell < field.length_of(drop); cell++) {
				Rainfall::Block block = field.cell_at(drop, cell);
				block.y -= Rainfall::kTopmost; // canvas-absolute -> the band's own origin
				draw_block(g, block);
			}
		}

		// The logo, `hid/display/screensaver.cpp:210-217`: every few minutes a whole logo falls
		// through the field, drawn through the same clipped block helper as the rain.
		if (field.logo_active()) {
			for (size_t cell = 0; cell < Rainfall::kLogoCells; cell++) {
				Rainfall::Block block = field.logo_cell_at(cell);
				block.y -= Rainfall::kTopmost;
				draw_block(g, block);
			}
		}
	});
}

} // namespace deluge::ui::oled
