// Based on Dexed's math_lut (Copyright 2013 Google Inc., Apache License 2.0) and EngineMkI (Copyright (C) 2015-2017
// Pascal Gauthier, GPLv3).

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
/// @brief The DX7 engine's lookup tables: one struct, built once at 44.1 kHz and lent to every lookup.
namespace deluge::dsp::fixed::dx7 {

/// @brief The engine's only sample rate; the frequency table and the envelope rates are built for it.
inline constexpr int32_t kSampleRate = 44100;

inline constexpr int kExp2LgSamples = 10; ///< log2 of the exp2 table's points.
inline constexpr int kSinLgSamples = 10;  ///< log2 of the sine table's points.
inline constexpr int kFreqLgSamples = 10; ///< log2 of the frequency table's points per octave.
inline constexpr int kMkILgSamples = 10;  ///< log2 of each MkI table's entries.

inline constexpr int kExp2Samples = 1 << kExp2LgSamples; ///< The exp2 table's points, one octave.
inline constexpr int kSinSamples = 1 << kSinLgSamples;   ///< The sine table's points, one cycle.
inline constexpr int kFreqSamples = 1 << kFreqLgSamples; ///< The frequency table's points, one octave.
inline constexpr int kMkISamples = 1 << kMkILgSamples;   ///< Each MkI table's entries.

/// @brief Every table the DX7 engine reads.
/// @details exp2 and sine hold (delta, value) pairs, the delta first, so a lookup reads its two words side by side.
struct Tables {
	/// 2^(i / 1024) in Q30 from 1.0 to 2.0, as (delta, value) pairs.
	int32_t exp2[kExp2Samples << 1]; // NOLINT(*-avoid-c-arrays): delta/value pairs, read by index per sample
	/// One sine cycle in Q24, as (delta, value) pairs.
	int32_t sine[kSinSamples << 1]; // NOLINT(*-avoid-c-arrays)
	/// Phase increments in Q24 cycles per sample at 44.1 kHz for the octave from 2^20 Hz, plus the octave's end point;
	/// a lookup shifts down to its own octave.
	int32_t frequency[kFreqSamples + 1]; // NOLINT(*-avoid-c-arrays)
	/// The MkI quarter sine's -log2 in 1/1024 steps.
	uint16_t mki_sin_log[kMkISamples]; // NOLINT(*-avoid-c-arrays)
	/// The MkI 2^x mantissa (2^(i / 1024) - 1) in Q12.
	uint16_t mki_sin_exp[kMkISamples]; // NOLINT(*-avoid-c-arrays)
};

/// @brief Fills every table of @p tables.
/// @param tables The tables to fill.
void generate(Tables& tables);

} // namespace deluge::dsp::fixed::dx7
