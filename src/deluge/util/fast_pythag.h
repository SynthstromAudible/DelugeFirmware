/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

/// Approximate sqrt(x^2 + y^2) - the magnitude of a complex FFT bin - using a 257-entry table of sqrt(1 + r^2)
/// over the ratio r = min/max of the two magnitudes. Accurate to within about 0.15%, for any inputs (including
/// INT32_MIN); saturates at INT32_MAX.
int32_t fastPythag(int32_t x, int32_t y);
