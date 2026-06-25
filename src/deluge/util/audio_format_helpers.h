/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

// Dependency-free byte/format helpers used by the audio-file header parser (and the wider codebase via
// functions.h, which re-exports this header). Kept free of the heavy firmware includes that functions.h
// pulls in (fatfs, gui, lookuptables, …) so the parser is a standalone, host-testable component.

#include <cstdint>

/// Pack ASCII chars into a little-endian integer — used to match RIFF/AIFF chunk tags read raw off disk.
[[gnu::always_inline]] constexpr uint32_t charsToIntegerConstant(char a, char b, char c, char d) {
	return (static_cast<uint32_t>(a)) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16)
	       | (static_cast<uint32_t>(d) << 24);
}

[[gnu::always_inline]] constexpr uint16_t charsToIntegerConstant(char a, char b) {
	return (static_cast<uint16_t>(a)) | (static_cast<uint16_t>(b) << 8);
}

/// Reverse all 4 bytes (ARM `rev`).
[[gnu::always_inline]] inline uint32_t swapEndianness32(uint32_t input) {
#if defined(__arm__)
	int32_t out;
	asm("rev %0, %1" : "=r"(out) : "r"(input));
	return out;
#else
	return __builtin_bswap32(input);
#endif
}

/// Reverse byte order within each 16-bit halfword independently (ARM `rev16`).
[[gnu::always_inline]] inline uint32_t swapEndianness2x16(uint32_t input) {
#if defined(__arm__)
	int32_t out;
	asm("rev16 %0, %1" : "=r"(out) : "r"(input));
	return out;
#else
	return ((input & 0x00FF00FFu) << 8) | ((input >> 8) & 0x00FF00FFu);
#endif
}

/// Parse an ASCII decimal over `[mem, memEnd)`; returns -1 on any non-digit. Accumulates in uint32_t (so a
/// huge run of digits wraps to a negative int32_t — preserved behavior). Used by the Serum `clm ` chunk.
int32_t memToUIntOrError(char const* mem, char const* const memEnd);

/// Decode a big-endian 80-bit IEEE 754 extended-precision float (10 bytes) — the AIFF COMM sample rate.
double ConvertFromIeeeExtended(unsigned char* bytes /* LCN */);
