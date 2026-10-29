/*
 * Derived from Dexed's math_lut, under its licence:
 *
 * Copyright 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Derived from Dexed's EngineMkI, under its licence:
 *
 * Copyright (C) 2015-2017 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 * The code is based on ppplay https://github.com/stohrendorf/ppplay and opl3
 * math documentation :
 * https://github.com/gtaylormb/opl3_fpga/blob/master/docs/opl3math/opl3math.pdf
 */

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

#include "dsp-next/fixed/dx7/tables.h"

#include <cstdint>

/// @file
/// @brief The DX7 engine's table lookups: exp2, sine and frequency by linear interpolation, and the MkI log-sine.
namespace deluge::dsp::fixed::dx7 {

/// @brief 2^x by linear interpolation of the exp2 table.
/// @param tables The engine's tables.
/// @param x The exponent in Q24, in [-14.0, 6.0], the range the operators feed; outside it the final shift is
/// negative or wider than the word.
/// @return 2^x in Q24.
[[gnu::always_inline]] inline int32_t exp2_lookup(const Tables& tables, int32_t x) {
	constexpr int kShift = 24 - kExp2LgSamples;
	const int32_t lowbits = x & ((1 << kShift) - 1);
	const int32_t index = (x >> (kShift - 1)) & ((kExp2Samples - 1) << 1);
	const int32_t dy = tables.exp2[index];
	const int32_t y0 = tables.exp2[index + 1];
	const auto y = static_cast<int32_t>(y0 + ((int64_t{dy} * lowbits) >> kShift));
	return y >> (6 - (x >> 24));
}

/// @brief sin(2π · phase / 2^24) by linear interpolation of the sine table.
/// @param tables The engine's tables.
/// @param phase The phase in Q24 cycles; any 32-bit value, read modulo one cycle.
/// @return The sine in Q24.
[[gnu::always_inline]] inline int32_t sin_lookup(const Tables& tables, int32_t phase) {
	constexpr int kShift = 24 - kSinLgSamples;
	const int32_t lowbits = phase & ((1 << kShift) - 1);
	const int32_t index = (phase >> (kShift - 1)) & ((kSinSamples - 1) << 1);
	const int32_t dy = tables.sine[index];
	const int32_t y0 = tables.sine[index + 1];
	return static_cast<int32_t>(y0 + ((int64_t{dy} * lowbits) >> kShift));
}

/// @brief The phase increment of a pitch, by linear interpolation of the frequency table.
/// @param tables The engine's tables.
/// @param logfreq The pitch in Q24 octaves above 1 Hz, in [0, 20.0), the range the voice feeds.
/// @return The phase increment in Q24 cycles per sample at 44.1 kHz.
[[gnu::always_inline]] inline int32_t freq_lookup(const Tables& tables, int32_t logfreq) {
	constexpr int kMaxLogFreqInt = 20;
	constexpr int kShift = 24 - kFreqLgSamples;
	const int32_t index = (logfreq & 0xffffff) >> kShift;
	const int32_t y0 = tables.frequency[index];
	const int32_t y1 = tables.frequency[index + 1];
	const int32_t lowbits = logfreq & ((1 << kShift) - 1);
	const auto y = static_cast<int32_t>(y0 + ((int64_t{y1 - y0} * lowbits) >> kShift));
	return y >> (kMaxLogFreqInt - (logfreq >> 24));
}

namespace detail {

/// @brief The sign bit of a MkI log value: set for the sine's negative half.
inline constexpr uint16_t kMkINegative = 0x8000;

/// @brief -log2 of the sine at @p phi, from the quarter-wave table, with the sign in kMkINegative.
/// @param phi The phase in 1/4096 cycles; bits 10 and 11 pick the quarter.
[[gnu::always_inline]] inline uint16_t mki_sin_log(const Tables& tables, uint16_t phi) {
	constexpr uint16_t kFilter = kMkISamples - 1;
	const uint16_t index = phi & kFilter;
	switch (phi & (kMkISamples * 3)) {
	case 0:
		return tables.mki_sin_log[index];
	case kMkISamples:
		return tables.mki_sin_log[index ^ kFilter];
	case kMkISamples * 2:
		return tables.mki_sin_log[index] | kMkINegative;
	default:
		return tables.mki_sin_log[index ^ kFilter] | kMkINegative;
	}
}

} // namespace detail

/// @brief The MkI operator's sine at @p phase attenuated by @p env, through the log and exp tables as the DX7's
/// chip computes it.
/// @param tables The engine's tables.
/// @param phase The phase in Q24 cycles; any 32-bit value.
/// @param env The attenuation in 1/1024 octaves. It is added to the log value in 16 bits, so a sum past 65535 wraps
/// (and can flip the sign bit), as the engine's arithmetic always has.
/// @return The sample in Q24.
[[gnu::always_inline]] inline int32_t mki_sin(const Tables& tables, int32_t phase, uint16_t env) {
	constexpr int kPhaseShift = 22 - kMkILgSamples;
	constexpr uint16_t kExpFilter = kMkISamples - 1;
	auto exp_val =
	    static_cast<uint16_t>(detail::mki_sin_log(tables, static_cast<uint16_t>(phase >> kPhaseShift)) + env);
	const bool negative = (exp_val & detail::kMkINegative) != 0;
	exp_val &= ~detail::kMkINegative;
	auto result = static_cast<uint16_t>(4096 + tables.mki_sin_exp[(exp_val & kExpFilter) ^ kExpFilter]);
	result >>= (exp_val >> 10);
	if (negative) {
		return (-result - 1) << 13;
	}
	return result << 13;
}

} // namespace deluge::dsp::fixed::dx7
