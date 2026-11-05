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

// The oscillator specs' phase and increment words, as the oscillators take them.

#include "dsp-next/core/fixed_point.h"

#include <cstdint>

/// A phase or increment word as the oscillators take it.
[[nodiscard]] constexpr deluge::dsp::UQ<32> uq(uint32_t word) {
	return deluge::dsp::UQ<32>::from_raw(word);
}
