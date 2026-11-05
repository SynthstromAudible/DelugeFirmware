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

#include <cstdint>

/// @file
/// @brief The A9's high-word multiplies as inline asm: the one implementation behind detail/raw_ops.h's helpers and
/// fixed_point.h's mul family at run time.
///
/// Clang has no intrinsic for them. The plain-C idiom compiles to the same instructions, but the loop vectorizer then
/// widens loops of it into NEON that runs 3-7x slower on the A9 (docs/dev/fixed_point.md). The asm is opaque to it.
#ifdef __arm__
namespace deluge::dsp::detail::arm {

/// @brief The high word of a × b, truncated (`smmul`).
[[gnu::always_inline]] inline std::int32_t smmul(std::int32_t a, std::int32_t b) {
	std::int32_t out = 0;
	asm("smmul %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
/// @brief The high word of a × b, rounded half up (`smmulr`).
[[gnu::always_inline]] inline std::int32_t smmulr(std::int32_t a, std::int32_t b) {
	std::int32_t out = 0;
	asm("smmulr %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
/// @brief acc + the high word of a × b, wrapping (`smmla`).
[[gnu::always_inline]] inline std::int32_t smmla(std::int32_t acc, std::int32_t a, std::int32_t b) {
	std::int32_t out = 0;
	asm("smmla %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(acc));
	return out;
}
/// @brief acc + the rounded high word of a × b, wrapping (`smmlar`).
[[gnu::always_inline]] inline std::int32_t smmlar(std::int32_t acc, std::int32_t a, std::int32_t b) {
	std::int32_t out = 0;
	asm("smmlar %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(acc));
	return out;
}
/// @brief The high word of acc × 2^32 − a × b, wrapping (`smmls`).
[[gnu::always_inline]] inline std::int32_t smmls(std::int32_t acc, std::int32_t a, std::int32_t b) {
	std::int32_t out = 0;
	asm("smmls %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(acc));
	return out;
}
/// @brief The rounded high word of acc × 2^32 − a × b, wrapping (`smmlsr`).
[[gnu::always_inline]] inline std::int32_t smmlsr(std::int32_t acc, std::int32_t a, std::int32_t b) {
	std::int32_t out = 0;
	asm("smmlsr %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(acc));
	return out;
}

} // namespace deluge::dsp::detail::arm
#endif
