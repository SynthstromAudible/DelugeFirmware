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

namespace deluge::hid::display {

namespace {
/// @brief Extent a streak occupies along its direction of travel, in the doubled u = x + y axis.
///
/// Cell i sits at (x - i*size, y - i*size), so each cell costs 2*size of u.
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

} // namespace deluge::hid::display
