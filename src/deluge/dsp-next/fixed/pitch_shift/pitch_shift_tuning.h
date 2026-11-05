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

#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"

#include <algorithm>
#include <array>
#include <cstdint>

/// @file
/// @brief How the live pitch shifter hops at a given shift: its search ranges, onset threshold, crossfade and hop
/// lengths, read from tables tuned by ear.

namespace deluge::dsp::fixed::pitch_shift {

/// @brief The pitch shifter's tuning at one shift.
/// @details Each value is read from two tables by the shift's log: a fine one across ±1 octave in steps of an eighth of
/// an octave (17 knots) and a coarse one across ±2 octaves in octave steps (5 knots), interpolated linearly. Beyond ±2
/// octaves the coarse tables hold their end values (quirk Q8,
/// docs/dev/live_pitch_shifter_quirks.md). The shifter evaluates it at each hop, for the
/// shift then.
struct PitchShiftTuning {
	struct Settings {
		int32_t phase_increment = 1 << 24; ///< The shift, 8.24: 2^24 is unshifted.
	};

	/// @brief The hop's tuning, in frames and samples.
	struct Coefficients {
		int32_t min_search = 0;           ///< The back-search's nearest edge, in input frames, before its random part.
		int32_t max_search = 0;           ///< The back-search's furthest edge, in input frames.
		int32_t percussive_threshold = 0; ///< How much more percussive now must be than the head to end a hop.
		int32_t crossfade_length = 0;     ///< The next crossfade, in output samples.
		int32_t max_hop_length = 0;       ///< The longest hop, in output samples.
		int32_t random_element = 0;       ///< The search's random part, Q16 of the table's units.
	};

	/// The shift's log at no shift: quick_log of 2^24.
	static constexpr int32_t kUnshiftedLog = 832 << 20;
	/// One octave of the shift's log.
	static constexpr int32_t kOctaveLog = 32 << 20;
	/// The fine tables' span, ±1 octave: 2^26 of the log.
	static constexpr int32_t kFineLow = kUnshiftedLog - kOctaveLog;
	static constexpr int32_t kFineHigh = kUnshiftedLog + kOctaveLog;
	static constexpr int32_t kFineBits = 26;
	static constexpr int32_t kFineKnotsLog2 = 4;
	/// The coarse tables' span, ±2 octaves: 2^27 of the log.
	static constexpr int32_t kCoarseLow = kUnshiftedLog - (2 * kOctaveLog);
	static constexpr int32_t kCoarseHigh = kUnshiftedLog + (2 * kOctaveLog);
	static constexpr int32_t kCoarseBits = 27;
	static constexpr int32_t kCoarseKnotsLog2 = 2;
	/// The maximum hop's tables are in hundreds of samples.
	static constexpr int32_t kHopLengthUnit = 100;

	/// @brief One tuned value's tables: fine, −12 … +12 semitones in eighths of an octave, and coarse, −24 … +24 in
	/// octaves.
	/// @details The coarse table ends with a guard knot repeating its last: the clamp's top, +24 exactly, reads the
	/// knot after the last with no weight (quirk Q12,
	/// docs/dev/live_pitch_shifter_quirks.md).
	struct Tables {
		std::array<int16_t, 17> fine;
		std::array<int16_t, 6> coarse;
	};

	static constexpr Tables kMinSearch{.fine = {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 14, 16, 17},
	                                   .coarse = {10, 10, 10, 17, 20, 20}};
	static constexpr Tables kMaxSearch{.fine = {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 10, 11, 12, 14, 16, 18, 21},
	                                   .coarse = {15, 15, 15, 21, 20, 20}};
	static constexpr Tables kPercussiveThreshold{
	    .fine = {18, 18, 18, 18, 24, 30, 25, 35, 130, 40, 40, 45, 50, 47, 45, 42, 40},
	    .coarse = {15, 18, 130, 40, 20, 20}};
	static constexpr Tables kCrossfade{.fine = {30, 31, 32, 34, 35, 30, 25, 10, 10, 15, 15, 22, 30, 16, 15, 13, 40},
	                                   .coarse = {30, 30, 10, 40, 20, 20}};
	static constexpr Tables kMaxHopLength{
	    .fine = {20, 27, 35, 42, 50, 60, 70, 90, 140, 140, 140, 140, 140, 140, 140, 140, 140},
	    .coarse = {10, 20, 140, 140, 140, 140}};
	static constexpr Tables kRandom{.fine = {0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 25, 76, 50, 57, 65, 72, 80},
	                                .coarse = {0, 0, 0, 80, 80, 80}};

	/// @brief The tuning at @p s's shift.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		const int32_t log = quick_log(static_cast<uint32_t>(s.phase_increment));
		if (log >= kFineLow && log < kFineHigh) {
			return read(static_cast<uint32_t>(log - kFineLow), kFineBits, kFineKnotsLog2, &Tables::fine);
		}
		const int32_t held = std::clamp(log, kCoarseLow, kCoarseHigh);
		return read(static_cast<uint32_t>(held - kCoarseLow), kCoarseBits, kCoarseKnotsLog2, &Tables::coarse);
	}

private:
	/// @brief Every value at @p position, a @p bits-bit position across the tables @p which names.
	template <std::size_t N>
	[[nodiscard]] static Coefficients read(uint32_t position, int32_t bits, int32_t knots_log2,
	                                       std::array<int16_t, N> Tables::* which) {
		// Each table's value, at Q16 of its units
		const auto at = [&](const Tables& t) {
			return kLinearTableInterpolator.interpolate_signed(position, bits, (t.*which).data(), knots_log2);
		};
		return {.min_search = at(kMinSearch) >> 9,
		        .max_search = at(kMaxSearch) >> 9,
		        .percussive_threshold = at(kPercussiveThreshold) >> 16,
		        .crossfade_length = at(kCrossfade) >> 12,
		        .max_hop_length = (at(kMaxHopLength) >> 16) * kHopLengthUnit,
		        .random_element = at(kRandom)};
	}
};

} // namespace deluge::dsp::fixed::pitch_shift
