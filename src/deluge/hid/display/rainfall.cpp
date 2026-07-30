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

#include "hid/display/rainfall.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace deluge::hid::display {

namespace {
// Extent a streak occupies along its direction of travel, in the doubled u = x + y axis. Cell i
// sits at (x - i*size, y - i*size), so each cell costs 2*size of u.
constexpr float spanAlongTravel(int32_t size, int32_t length) {
	return 2.0f * static_cast<float>((length - 1) * size);
}
} // namespace

float Rainfall::separationSquared(const Streak& a, const Streak& b) {
	// Across travel. Every drop moves along (1, 1), so x - y is invariant under motion -- and
	// stays invariant even between two drops moving at different speeds. Two streaks with a large
	// offset here can never touch, whatever they do along travel.
	const float across = (a.x - a.y) - (b.x - b.y);

	// Along travel, as the interval [tail, head] in u = x + y.
	const float aHead = a.x + a.y;
	const float bHead = b.x + b.y;
	const float aTail = aHead - spanAlongTravel(a.size, a.length);
	const float bTail = bHead - spanAlongTravel(b.size, b.length);

	// Zero when the intervals overlap, in which case the streaks are separated purely across
	// travel and the term below drops out.
	const float along = std::max(0.0f, std::max(aTail, bTail) - std::min(aHead, bHead));

	// Both axes are sqrt(2) times the true distances, so the true squared distance is halved.
	return 0.5f * (across * across + along * along);
}

uint32_t Rainfall::nextRandom() {
	rngState_ = rngState_ * 1664525u + 1013904223u;
	return rngState_;
}

float Rainfall::nextRandomFloat() {
	return static_cast<float>(nextRandom()) * (1.0f / 4294967296.0f);
}

void Rainfall::roll(Drop& drop) {
	// Depth drives both size and speed. The panel is 1-bit, so only three block sizes exist and
	// depth is quantised for size -- but several speeds share each size, which is what stops the
	// field reading as three sheets sliding in lockstep.
	const float depth = nextRandomFloat();
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
	drop.length = (nextRandomFloat() < 0.5f) ? 3 : 4;
}

void Rainfall::place(Drop& drop, bool seeded) {
	const float reach = reachOf(drop.size, drop.length);

	if (seeded) {
		// Scattering: anywhere on the panel, so the first frame after activation is already full.
		drop.x = nextRandomFloat() * (kWidth + reach) - reach;
		drop.y = nextRandomFloat() * (kVisibleHeight + reach) + kTopmost - reach;
	}
	else if (nextRandomFloat() < kSpawnFromTop) {
		drop.x = nextRandomFloat() * (kWidth + reach) - reach;
		drop.y = kTopmost - reach - 1.0f;
	}
	else {
		// Through the left edge, so drops keep arriving in the region the rightward drift empties.
		drop.x = -reach - 1.0f;
		drop.y = nextRandomFloat() * kVisibleHeight + kTopmost;
	}

	// Pin y to x by a whole-pixel offset, and keep it there: advance() derives y rather than
	// integrating it. Two things depend on this, and both are required to stop the streak
	// wiggling. The axes must share a fractional phase, so cellAt() crosses both pixel boundaries
	// on the same frame and every step is a clean diagonal. And y must not drift from x through
	// float rounding, which it does if both are accumulated independently -- the two sit at
	// different magnitudes, so the same += speed rounds differently and they slip a frame apart.
	// Either failure makes a drop step right on one frame and down on the next, oscillating about
	// its own axis: on a 3x3 block, a visible wiggle.
	drop.axisOffset = static_cast<int32_t>(std::lround(drop.x - drop.y));
	drop.y = drop.x - static_cast<float>(drop.axisOffset);
}

void Rainfall::spawn(size_t index, bool seeded) {
	Drop best{};
	float bestClearance = -1.0f;

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
			clearance = std::min(clearance, separationSquared(streakOf(candidate), streakOf(drops_[other])));
		}

		const float minGap = minGapFor(candidate.size);
		if (clearance >= minGap * minGap) {
			drops_[index] = candidate;
			return;
		}
		if (clearance > bestClearance) {
			best = candidate;
			bestClearance = clearance;
		}
	}

	// Nothing cleared the bar. Take the roomiest candidate rather than looping: bounded work
	// matters more here than a perfect placement, and same-size drops differ in speed, so a tight
	// pair drifts apart on its own.
	drops_[index] = best;
}

void Rainfall::scatter() {
	// Zeroed slots have size zero, which spawn() treats as "not yet placed" -- so each drop is
	// only spaced against the ones already down.
	drops_.fill(Drop{});
	for (size_t index = 0; index < kNumDrops; index++) {
		spawn(index, true);
	}
}

void Rainfall::advance() {
	for (size_t index = 0; index < kNumDrops; index++) {
		Drop& drop = drops_[index];
		// Only x is integrated; y follows it exactly, so the drop travels along its own 45 degree
		// axis and the streak never slides sideways underneath itself. See place() for why y is
		// derived rather than accumulated.
		drop.x += drop.speed;
		drop.y = drop.x - static_cast<float>(drop.axisOffset);

		const float reach = reachOf(drop.size, drop.length);
		if (drop.y - reach > kHeight || drop.x - reach > kWidth) {
			spawn(index, false);
		}
	}
}

Rainfall::Block Rainfall::cellAt(size_t drop, size_t cell) const {
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
	    .y = x - d.axisOffset - step,
	    .size = d.size,
	};
}

} // namespace deluge::hid::display
