/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "processing/metronome/metronome.h"

#include "util/fixedpoint.h"

#include <cmath>

void Metronome::render(std::span<deluge::dsp::StereoSample> buffer, int32_t volume_post_fx) {
	if (!sounding()) {
		return;
	}
	click_.configure({.level = multiply_32x32_rshift32(static_cast<int32_t>(volume_), volume_post_fx)});
	click_.renderBlock(buffer, buffer);
}

void Metronome::setVolume(int32_t linear_param) {
	// Unqualified, as the menu's volume has always been computed: the overload <cmath> leaves in scope.
	volume_ = (exp(float(linear_param) / 200.0f) - 1.0) * float(1 << 27);
}
