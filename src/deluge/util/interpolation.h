/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

// Fixed-point table interpolation — small, pure leaf helpers (no engine/UI/storage dependencies),
// split out of the monolithic util/functions.cpp so DSP components that only need a table lookup
// (e.g. the envelope's curves) can be built/tested in isolation.

/// Linearly interpolate into a 1-D `uint16_t` table. @p input is a fixed-point index with
/// @p numBitsInInput fractional bits; @p numBitsInTableSize is log2 of the table length.
int32_t interpolateTable(uint32_t input, int32_t numBitsInInput, const uint16_t* table, int32_t numBitsInTableSize = 8);

/// Inverse of @ref interpolateTable: find the (fixed-point) input that yields @p tableValueBig,
/// by binary-searching the monotonic table. Returns a value with @p numBitsInLookupOutput bits.
uint32_t interpolateTableInverse(int32_t tableValueBig, int32_t numBitsInLookupOutput, const uint16_t* table,
                                 int32_t numBitsInTableSize = 8);

/// Decay/curve lookups: @ref interpolateTable into the small 8- and 4-bit decay tables.
int32_t getDecay8(uint32_t input, uint8_t numBitsInInput);
int32_t getDecay4(uint32_t input, uint8_t numBitsInInput);
