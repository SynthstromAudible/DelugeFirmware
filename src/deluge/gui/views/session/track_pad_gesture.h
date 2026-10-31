/*
 * Copyright © 2026 Sean Ditny
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

#include <cstdint>

// Shift is captured at press time so releasing Shift before the pad cannot
// accidentally audition a locked track.
class TrackPadGesture {
public:
	void press(uint32_t now, bool silent) {
		started = now;
		silentPress = silent;
	}
	bool shouldAudition(uint32_t now, uint32_t holdTime) const { return !silentPress && now - started >= holdTime; }

private:
	uint32_t started = 0;
	bool silentPress = false;
};
