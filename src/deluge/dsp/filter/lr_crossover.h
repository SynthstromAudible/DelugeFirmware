/*
 * Copyright © 2024-2025 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/fast_math.h"
#include "dsp/filter/ladder_components.h"
#include "util/fixedpoint.h"
#include <array>
#include <cmath>
#include <type_traits>

namespace deluge::dsp::filter {

/// 3-band Linkwitz-Riley crossover filter (LR2 = 12dB/oct slopes).
/// Uses cascaded first-order Butterworth filters.
///
/// LR2 characteristics:
/// - 12dB/oct (40dB/decade) slopes
/// - -6dB at crossover frequency (power-complementary)
/// - Flat summed magnitude response
///
/// Template parameter PHASE_COMPENSATED:
/// - true (default): Adds allpass to LOW band to match MID/HIGH phase.
///   Cost: 6 filter ops. Perfect phase alignment.
/// - false: Skips phase compensation for CPU efficiency.
///   Cost: 4 filter ops. ~90° phase lead in LOW band at high crossover freq.
///   Inaudible for dynamics processing.
template <bool PHASE_COMPENSATED = true>
class LR2Crossover {
public:
	using Bands = CrossoverBands;

	LR2Crossover() = default;

	/// Set the low-mid crossover frequency
	void setLowCrossover(float freqHz) {
		lowCrossoverHz_ = freqHz;
		lowCoeff_ = calculateCoefficient(freqHz);
	}

	/// Set the mid-high crossover frequency
	void setHighCrossover(float freqHz) {
		highCrossoverHz_ = freqHz;
		highCoeff_ = calculateCoefficient(freqHz);
	}

	[[nodiscard]] float getLowCrossoverHz() const { return lowCrossoverHz_; }
	[[nodiscard]] float getHighCrossoverHz() const { return highCrossoverHz_; }

	/// Process a stereo sample pair using NEON (parallel L/R processing)
	[[gnu::always_inline]] inline void processStereo(q31_t inputL, q31_t inputR, Bands& outL, Bands& outR) {
		// Pack L/R into NEON vector
		int32x2_t input = {inputL, inputR};

		// === Low crossover: split into LOW and REST ===
		int32x2_t lp1 = stereoState_.lpLow1.doFilter(input, lowCoeff_);
		int32x2_t lowRaw = stereoState_.lpLow2.doFilter(lp1, lowCoeff_);
		int32x2_t rest = vsub_s32(input, lowRaw);

		// === High crossover: split REST into MID and HIGH ===
		int32x2_t lp3 = stereoState_.lpHigh1.doFilter(rest, highCoeff_);
		int32x2_t mid = stereoState_.lpHigh2.doFilter(lp3, highCoeff_);
		int32x2_t high = vsub_s32(rest, mid);

		// === Phase compensation for LOW band (conditional) ===
		int32x2_t low;
		if constexpr (PHASE_COMPENSATED) {
			int32x2_t lowComp1 = stereoState_.apComp1.doAPF(lowRaw, highCoeff_);
			low = stereoState_.apComp2.doAPF(lowComp1, highCoeff_);
		}
		else {
			low = lowRaw;
		}

		// Unpack results
		outL = {vget_lane_s32(low, 0), vget_lane_s32(mid, 0), vget_lane_s32(high, 0)};
		outR = {vget_lane_s32(low, 1), vget_lane_s32(mid, 1), vget_lane_s32(high, 1)};
	}

	/// Reset all filter states
	void reset() {
		stereoState_.lpLow1.reset();
		stereoState_.lpLow2.reset();
		stereoState_.lpHigh1.reset();
		stereoState_.lpHigh2.reset();
		if constexpr (PHASE_COMPENSATED) {
			stereoState_.apComp1.reset();
			stereoState_.apComp2.reset();
		}
	}

private:
	/// NEON stereo filter state (processes L/R in parallel)
	struct StereoState {
		// Low crossover: two cascaded first-order for LR2
		StereoFilterComponent lpLow1, lpLow2;
		// High crossover: two cascaded first-order for LR2
		StereoFilterComponent lpHigh1, lpHigh2;
		// Phase compensation allpass for LOW band (only if PHASE_COMPENSATED)
		std::conditional_t<PHASE_COMPENSATED, StereoFilterComponent, char[0]> apComp1;
		std::conditional_t<PHASE_COMPENSATED, StereoFilterComponent, char[0]> apComp2;
	};

	/// Calculate coefficient for first-order Butterworth
	/// Same as allpass: coeff = tan(pi*fc/fs) / (1 + tan(pi*fc/fs))
	[[nodiscard]] static q31_t calculateCoefficient(float freqHz) {
		float fc = freqHz / static_cast<float>(kSampleRate);
		fc = std::clamp(fc, 0.001f, 0.49f);
		float wc = deluge::dsp::fastTan(3.14159265358979f * fc);
		float coeff = wc / (1.0f + wc);
		return static_cast<q31_t>(coeff * ONE_Q31);
	}

	StereoState stereoState_{};

	q31_t lowCoeff_ = calculateCoefficient(200.0f);
	q31_t highCoeff_ = calculateCoefficient(2000.0f);

	float lowCrossoverHz_ = 200.0f;
	float highCrossoverHz_ = 2000.0f;
};

// Type aliases for convenience
using LR2CrossoverFull = LR2Crossover<true>;  // With phase compensation (6 filter ops)
using LR2CrossoverFast = LR2Crossover<false>; // Without phase compensation (4 filter ops)

/// 3-band Linkwitz-Riley crossover filter (LR4 = 24dB/oct slopes).
/// Uses 4 cascaded first-order Butterworth filters.
///
/// LR4 characteristics:
/// - 24dB/oct (80dB/decade) slopes - sharper than LR2
/// - -6dB at crossover frequency (power-complementary)
/// - Flat summed magnitude response
///
/// Template parameter PHASE_COMPENSATED:
/// - true (default): Adds allpass to LOW band to match MID/HIGH phase.
///   Cost: 12 filter ops. Perfect phase alignment.
/// - false: Skips phase compensation for CPU efficiency.
///   Cost: 8 filter ops. Phase lead in LOW band at high crossover freq.
template <bool PHASE_COMPENSATED = true>
class LR4Crossover {
public:
	using Bands = CrossoverBands;

	LR4Crossover() = default;

	/// Set the low-mid crossover frequency
	void setLowCrossover(float freqHz) {
		lowCrossoverHz_ = freqHz;
		lowCoeff_ = calculateCoefficient(freqHz);
	}

	/// Set the mid-high crossover frequency
	void setHighCrossover(float freqHz) {
		highCrossoverHz_ = freqHz;
		highCoeff_ = calculateCoefficient(freqHz);
	}

	[[nodiscard]] float getLowCrossoverHz() const { return lowCrossoverHz_; }
	[[nodiscard]] float getHighCrossoverHz() const { return highCrossoverHz_; }

	/// Process a stereo sample pair using NEON (parallel L/R processing)
	[[gnu::always_inline]] inline void processStereo(q31_t inputL, q31_t inputR, Bands& outL, Bands& outR) {
		// Pack L/R into NEON vector
		int32x2_t input = {inputL, inputR};

		// === Low crossover: 4 cascaded LP stages for LR4 ===
		int32x2_t lp1 = stereoState_.lpLow1.doFilter(input, lowCoeff_);
		int32x2_t lp2 = stereoState_.lpLow2.doFilter(lp1, lowCoeff_);
		int32x2_t lp3 = stereoState_.lpLow3.doFilter(lp2, lowCoeff_);
		int32x2_t lowRaw = stereoState_.lpLow4.doFilter(lp3, lowCoeff_);
		int32x2_t rest = vsub_s32(input, lowRaw);

		// === High crossover: 4 cascaded LP stages for LR4 ===
		int32x2_t hp1 = stereoState_.lpHigh1.doFilter(rest, highCoeff_);
		int32x2_t hp2 = stereoState_.lpHigh2.doFilter(hp1, highCoeff_);
		int32x2_t hp3 = stereoState_.lpHigh3.doFilter(hp2, highCoeff_);
		int32x2_t mid = stereoState_.lpHigh4.doFilter(hp3, highCoeff_);
		int32x2_t high = vsub_s32(rest, mid);

		// === Phase compensation for LOW band (conditional) ===
		int32x2_t low;
		if constexpr (PHASE_COMPENSATED) {
			// 4 allpass stages to match the 4 HP stages
			int32x2_t lowComp1 = stereoState_.apComp1.doAPF(lowRaw, highCoeff_);
			int32x2_t lowComp2 = stereoState_.apComp2.doAPF(lowComp1, highCoeff_);
			int32x2_t lowComp3 = stereoState_.apComp3.doAPF(lowComp2, highCoeff_);
			low = stereoState_.apComp4.doAPF(lowComp3, highCoeff_);
		}
		else {
			low = lowRaw;
		}

		// Unpack results
		outL = {vget_lane_s32(low, 0), vget_lane_s32(mid, 0), vget_lane_s32(high, 0)};
		outR = {vget_lane_s32(low, 1), vget_lane_s32(mid, 1), vget_lane_s32(high, 1)};
	}

	/// Reset all filter states
	void reset() {
		stereoState_.lpLow1.reset();
		stereoState_.lpLow2.reset();
		stereoState_.lpLow3.reset();
		stereoState_.lpLow4.reset();
		stereoState_.lpHigh1.reset();
		stereoState_.lpHigh2.reset();
		stereoState_.lpHigh3.reset();
		stereoState_.lpHigh4.reset();
		if constexpr (PHASE_COMPENSATED) {
			stereoState_.apComp1.reset();
			stereoState_.apComp2.reset();
			stereoState_.apComp3.reset();
			stereoState_.apComp4.reset();
		}
	}

private:
	/// NEON stereo filter state (processes L/R in parallel)
	struct StereoState {
		// Low crossover: 4 cascaded first-order for LR4
		StereoFilterComponent lpLow1, lpLow2, lpLow3, lpLow4;
		// High crossover: 4 cascaded first-order for LR4
		StereoFilterComponent lpHigh1, lpHigh2, lpHigh3, lpHigh4;
		// Phase compensation allpass for LOW band (only if PHASE_COMPENSATED)
		std::conditional_t<PHASE_COMPENSATED, StereoFilterComponent, char[0]> apComp1;
		std::conditional_t<PHASE_COMPENSATED, StereoFilterComponent, char[0]> apComp2;
		std::conditional_t<PHASE_COMPENSATED, StereoFilterComponent, char[0]> apComp3;
		std::conditional_t<PHASE_COMPENSATED, StereoFilterComponent, char[0]> apComp4;
	};

	/// Calculate coefficient for first-order Butterworth
	[[nodiscard]] static q31_t calculateCoefficient(float freqHz) {
		float fc = freqHz / static_cast<float>(kSampleRate);
		fc = std::clamp(fc, 0.001f, 0.49f);
		float wc = deluge::dsp::fastTan(3.14159265358979f * fc);
		float coeff = wc / (1.0f + wc);
		return static_cast<q31_t>(coeff * ONE_Q31);
	}

	StereoState stereoState_{};

	q31_t lowCoeff_ = calculateCoefficient(200.0f);
	q31_t highCoeff_ = calculateCoefficient(2000.0f);

	float lowCrossoverHz_ = 200.0f;
	float highCrossoverHz_ = 2000.0f;
};

using LR4CrossoverFull = LR4Crossover<true>;  // With phase compensation (12 filter ops)
using LR4CrossoverFast = LR4Crossover<false>; // Without phase compensation (8 filter ops)

} // namespace deluge::dsp::filter
