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

#include "dsp/core/fixed_point.h"
#include "dsp/fixed/curves/quick_log.h"
#include "dsp/fixed/interpolation/table_interpolation.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace deluge::dsp::fixed::timestretch {

/// @file
/// @brief The time stretcher's hop tuning: how wide its hops may be and how they crossfade, by stretch ratio (T1 to T3
/// of docs/dev/time_stretcher_contracts.md). The tables are the stretcher's, tuned by ear.

/// @brief A stretch ratio: source frames per output frame, 2^24 = 1.0. Read unsigned (decision 3).
using Ratio = UQ<24>;

/// @brief The shape of the hops at one ratio.
struct HopShape {
	int32_t min_width;          ///< Output frames.
	int32_t max_width;          ///< Output frames.
	Q<31> crossfade_share;      ///< Of the hop; table values of 128 and over wrap negative (T3).
	int32_t crossfade_quarters; ///< The crossfade's fixed part, in units of 4 frames.
	Q<23> jitter_share;         ///< The most the minimum width may move at random, as a share of itself.
	constexpr bool operator==(const HopShape&) const = default;
};

namespace hop_tables {
// Fine: 17 entries, -12 to +12 semitones of speed in steps of 1.5. Coarse: 5 entries, -2 to +2 octaves.
inline constexpr std::array<int16_t, 17> kMinWidthFine{3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000,
                                                       2500, 2000, 1500, 1000, 900,  800,  700,  600};
inline constexpr std::array<int16_t, 5> kMinWidthCoarse{2500, 3000, 3000, 600, 300};
inline constexpr std::array<int16_t, 17> kMaxWidthFine{6500, 7000, 8000, 9000, 9500, 9750, 10000, 11000, 11000,
                                                       7500, 8000, 6500, 5000, 4750, 4500, 4250,  4000};
inline constexpr std::array<int16_t, 5> kMaxWidthCoarse{5000, 6500, 11000, 4000, 2500};
inline constexpr std::array<int16_t, 17> kShareFine{160, 140, 125, 110, 90, 70, 50, 20, 0,
                                                    20,  20,  20,  20,  17, 14, 11, 9};
inline constexpr std::array<int16_t, 5> kShareCoarse{200, 160, 0, 9, 9};
inline constexpr std::array<int16_t, 17> kQuartersFine{10, 10, 10, 10, 10, 10, 10, 170, 60,
                                                       90, 20, 30, 40, 40, 40, 40, 40};
inline constexpr std::array<int16_t, 5> kQuartersCoarse{10, 10, 60, 40, 20};
inline constexpr std::array<int16_t, 17> kJitterFine{120, 95, 70, 45, 20, 15, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
inline constexpr std::array<int16_t, 5> kJitterCoarse{85, 120, 0, 0, 0};
} // namespace hop_tables

/// The table interpolator answers in words, and T1 and T2 are word arithmetic on them (a shift down, a wrapping
/// shift up), so the shape's typed fields are built from those words.
/// @brief The hop shape at @p ratio (T1, T2): within an octave either way of 1.0 the fine tables, interpolated over
/// the ratio's log; beyond, the coarse tables, clamped at two octaves either way.
[[nodiscard]] inline HopShape hop_shape(Ratio ratio) {
	using namespace hop_tables;
	constexpr int32_t kOctave = 32 << 20; // quick_log's octave
	constexpr int32_t kFineLow = 800 << 20;
	constexpr int32_t kFineHigh = 864 << 20;
	constexpr int32_t kCoarseLow = 768 << 20;
	constexpr int32_t kCoarseHigh = 896 << 20;
	static_assert(kFineHigh - kFineLow == 2 * kOctave && kCoarseHigh - kCoarseLow == 4 * kOctave);

	auto read = [](const int16_t* table, uint32_t position, int32_t bits, int32_t size_log2) {
		return kLinearTableInterpolator.interpolate_signed(position, bits, table, size_log2);
	};
	auto shape = [&](uint32_t position, int32_t bits, int32_t size_log2, const int16_t* min, const int16_t* max,
	                 const int16_t* share, const int16_t* quarters, const int16_t* jitter) {
		return HopShape{
		    .min_width = read(min, position, bits, size_log2) >> 16,
		    .max_width = read(max, position, bits, size_log2) >> 16,
		    // The table's word moved up 8, wrapping: entries of 128 and over turn negative (T3)
		    .crossfade_share = Q<31>::from_raw(
		        static_cast<int32_t>(static_cast<uint32_t>(read(share, position, bits, size_log2)) << 8)),
		    .crossfade_quarters = read(quarters, position, bits, size_log2) >> 16,
		    .jitter_share = Q<23>::from_raw(read(jitter, position, bits, size_log2)),
		};
	};

	// From 2^26 (two octaves up) the coarse tables are pinned at their top; read so, the ratio never meets quick_log's
	// wrap for words with bit 31 set.
	const int32_t log = ratio.raw() >= (1u << 26) ? kCoarseHigh : quick_log(ratio.raw());
	if (log >= kFineLow && log < kFineHigh) {
		return shape(static_cast<uint32_t>(log - kFineLow), 26, 4, kMinWidthFine.data(), kMaxWidthFine.data(),
		             kShareFine.data(), kQuartersFine.data(), kJitterFine.data());
	}
	const int32_t clamped = std::clamp(log, kCoarseLow, kCoarseHigh);
	// The coarse read takes this entry and the next, so the position stays below the last entry's start: the top of
	// the range reads the last entry.
	const uint32_t position = std::min(static_cast<uint32_t>(clamped - kCoarseLow), (128u << 20) - 1);
	return shape(position, 27, 2, kMinWidthCoarse.data(), kMaxWidthCoarse.data(), kShareCoarse.data(),
	             kQuartersCoarse.data(), kJitterCoarse.data());
}

} // namespace deluge::dsp::fixed::timestretch
