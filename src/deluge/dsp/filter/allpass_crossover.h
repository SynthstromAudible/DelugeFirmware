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

namespace deluge::dsp::filter {

/// Output structure for the three frequency bands
struct CrossoverBands {
	q31_t low;
	q31_t mid;
	q31_t high;
};

/// Allpass Subtraction Crossover - 3-band filter with perfect phase-coherent reconstruction.
/// Also known as "Complementary Allpass Crossover".
///
/// The bands sum back EXACTLY to the original signal with no phase distortion.
/// This is the key advantage over Linkwitz-Riley crossovers which have phase shifts.
///
/// Method: LP = (input + allpass) / 2, HP = (input - allpass) / 2
/// At crossover: both bands are at -3dB (vs -6dB for Linkwitz-Riley)
///
/// Characteristics:
/// - Perfect reconstruction: LOW + MID + HIGH = input (exactly)
/// - 6dB/oct slopes (gentle, smooth band blending)
/// - -3dB at crossover frequency
/// - Minimal CPU cost (~2 first-order allpasses)
/// - Good for dynamics processing where band isolation isn't critical
///
/// For steeper slopes (12dB/oct), use LR2Crossover instead (see lr_crossover.h).
///
/// Template parameter ORDER (only ORDER=1 is recommended for 3-band: Higher orders dont work properly, but are
/// interesting for DOTT):
template <int ORDER>
class AllpassCrossover {
	static_assert(ORDER >= 1 && ORDER <= 5, "ORDER must be 1-5");

public:
	using Bands = CrossoverBands;

	AllpassCrossover() = default;

	/// Set the low-mid crossover frequency
	/// @param freqHz Crossover frequency in Hz (typically 100-500Hz)
	void setLowCrossover(float freqHz) {
		lowCrossoverHz_ = freqHz;
		lowCoeff_ = calculateCoefficient(freqHz);
	}

	/// Set the mid-high crossover frequency
	/// @param freqHz Crossover frequency in Hz (typically 1000-5000Hz)
	void setHighCrossover(float freqHz) {
		highCrossoverHz_ = freqHz;
		highCoeff_ = calculateCoefficient(freqHz);
	}

	/// Get the current low crossover frequency in Hz
	[[nodiscard]] float getLowCrossoverHz() const { return lowCrossoverHz_; }

	/// Get the current high crossover frequency in Hz
	[[nodiscard]] float getHighCrossoverHz() const { return highCrossoverHz_; }

	/// Process a stereo sample pair using NEON (parallel L/R processing)
	[[gnu::always_inline]] inline void processStereo(q31_t inputL, q31_t inputR, Bands& outL, Bands& outR) {
		// Pack L/R into NEON vector
		int32x2_t input = {inputL, inputR};

		// Step 1: Low crossover splits input into LOW and REST
		int32x2_t apLow = input;
		for (int i = 0; i < ORDER; ++i) {
			apLow = stereoState_.apLow[i].doAPF(apLow, lowCoeff_);
		}
		// low = (input + apLow) >> 1
		int32x2_t low = vhadd_s32(input, apLow);
		// rest = (input - apLow) >> 1
		int32x2_t rest = vhsub_s32(input, apLow);

		// Step 2: High crossover splits REST into MID and HIGH
		int32x2_t apHigh = rest;
		for (int i = 0; i < ORDER; ++i) {
			apHigh = stereoState_.apHigh[i].doAPF(apHigh, highCoeff_);
		}
		// mid = (rest + apHigh) >> 1
		int32x2_t mid = vhadd_s32(rest, apHigh);
		// high = (rest - apHigh) >> 1
		int32x2_t high = vhsub_s32(rest, apHigh);

		// Unpack results
		outL = {vget_lane_s32(low, 0), vget_lane_s32(mid, 0), vget_lane_s32(high, 0)};
		outR = {vget_lane_s32(low, 1), vget_lane_s32(mid, 1), vget_lane_s32(high, 1)};
	}

	/// Reset all filter states
	void reset() {
		for (int i = 0; i < ORDER; ++i) {
			stereoState_.apLow[i].reset();
			stereoState_.apHigh[i].reset();
		}
	}

private:
	/// NEON stereo filter state (processes L/R in parallel)
	struct StereoState {
		std::array<StereoFilterComponent, ORDER> apLow;  // Low crossover stages
		std::array<StereoFilterComponent, ORDER> apHigh; // High crossover stages
	};

	/// Calculate the allpass coefficient for a given frequency.
	/// For BasicFilterComponent::doAPF(): coeff = tan(pi * fc / fs) / (1 + tan(pi * fc / fs))
	[[nodiscard]] static q31_t calculateCoefficient(float freqHz) {
		float fc = freqHz / static_cast<float>(kSampleRate);
		// Clamp to valid range to prevent instability
		fc = std::clamp(fc, 0.001f, 0.49f);
		float wc = deluge::dsp::fastTan(3.14159265358979f * fc);
		// Coefficient for BasicFilterComponent::doAPF()
		float coeff = wc / (1.0f + wc);
		return static_cast<q31_t>(coeff * ONE_Q31);
	}

	StereoState stereoState_{};

	q31_t lowCoeff_ = calculateCoefficient(200.0f);   // Default 200Hz
	q31_t highCoeff_ = calculateCoefficient(2000.0f); // Default 2kHz

	float lowCrossoverHz_ = 200.0f;
	float highCrossoverHz_ = 2000.0f;
};

// Type aliases for convenience
// ORDER=1 is correct 6dB/oct crossover; higher orders are "broken" but creatively useful
using AllpassCrossoverLR1 = AllpassCrossover<1>; // 6dB/oct - cheapest, correct
using AllpassCrossoverLR2 = AllpassCrossover<2>; // "Quirky" - creative/experimental
using AllpassCrossoverLR3 = AllpassCrossover<3>; // "Weird" - creative/experimental
using AllpassCrossoverLR5 = AllpassCrossover<5>; // 30dB/oct - experimental

/// "Twisted" crossover - 2 stages with mixed coefficients.
/// Same cost as ORDER=2 (4 ops) but blends coefficients between stages.
/// twist=0: behaves like Quirky (same coeff both stages)
/// twist=1: fully twisted (stage2 uses opposite crossover's coeff)
/// Creates asymmetric phase smearing between bands - interesting for creative use.
class AllpassCrossoverTwisted {
public:
	using Bands = CrossoverBands;

	AllpassCrossoverTwisted() = default;

	void setLowCrossover(float freqHz) {
		lowCrossoverHz_ = freqHz;
		lowCoeff_ = calculateCoefficient(freqHz);
		updateBlendedCoeffs();
	}

	void setHighCrossover(float freqHz) {
		highCrossoverHz_ = freqHz;
		highCoeff_ = calculateCoefficient(freqHz);
		updateBlendedCoeffs();
	}

	/// Set twist amount (0.0 = like Quirky, 1.0 = fully twisted)
	void setTwist(float twist) {
		twist_ = std::clamp(twist, 0.0f, 1.0f);
		updateBlendedCoeffs();
	}

	[[nodiscard]] float getTwist() const { return twist_; }
	[[nodiscard]] float getLowCrossoverHz() const { return lowCrossoverHz_; }
	[[nodiscard]] float getHighCrossoverHz() const { return highCrossoverHz_; }

	/// Process with blended coefficients based on twist amount
	[[gnu::always_inline]] inline void processStereo(q31_t inputL, q31_t inputR, Bands& outL, Bands& outR) {
		int32x2_t input = {inputL, inputR};

		// Low crossover: stage1=lowCoeff, stage2=blended toward highCoeff
		int32x2_t apLow1 = stereoState_.apLow1.doAPF(input, lowCoeff_);
		int32x2_t apLow2 = stereoState_.apLow2.doAPF(apLow1, lowStage2Coeff_);
		int32x2_t low = vhadd_s32(input, apLow2);
		int32x2_t rest = vhsub_s32(input, apLow2);

		// High crossover: stage1=highCoeff, stage2=blended toward lowCoeff
		int32x2_t apHigh1 = stereoState_.apHigh1.doAPF(rest, highCoeff_);
		int32x2_t apHigh2 = stereoState_.apHigh2.doAPF(apHigh1, highStage2Coeff_);
		int32x2_t mid = vhadd_s32(rest, apHigh2);
		int32x2_t high = vhsub_s32(rest, apHigh2);

		outL = {vget_lane_s32(low, 0), vget_lane_s32(mid, 0), vget_lane_s32(high, 0)};
		outR = {vget_lane_s32(low, 1), vget_lane_s32(mid, 1), vget_lane_s32(high, 1)};
	}

	void reset() {
		stereoState_.apLow1.reset();
		stereoState_.apLow2.reset();
		stereoState_.apHigh1.reset();
		stereoState_.apHigh2.reset();
	}

private:
	struct StereoState {
		StereoFilterComponent apLow1, apLow2;
		StereoFilterComponent apHigh1, apHigh2;
	};

	[[nodiscard]] static q31_t calculateCoefficient(float freqHz) {
		float fc = freqHz / static_cast<float>(kSampleRate);
		fc = std::clamp(fc, 0.001f, 0.49f);
		float wc = deluge::dsp::fastTan(3.14159265358979f * fc);
		float coeff = wc / (1.0f + wc);
		return static_cast<q31_t>(coeff * ONE_Q31);
	}

	/// Update blended coefficients based on twist amount
	void updateBlendedCoeffs() {
		// twist=0: stage2 uses same coeff as stage1 (like Quirky)
		// twist=1: stage2 uses opposite crossover's coeff (fully twisted)
		float t = twist_;
		float lowF = static_cast<float>(lowCoeff_);
		float highF = static_cast<float>(highCoeff_);
		lowStage2Coeff_ = static_cast<q31_t>(lowF + (highF - lowF) * t);
		highStage2Coeff_ = static_cast<q31_t>(highF + (lowF - highF) * t);
	}

	StereoState stereoState_{};
	q31_t lowCoeff_ = calculateCoefficient(200.0f);
	q31_t highCoeff_ = calculateCoefficient(2000.0f);
	q31_t lowStage2Coeff_ = lowCoeff_;   // Blended coeff for low crossover stage 2
	q31_t highStage2Coeff_ = highCoeff_; // Blended coeff for high crossover stage 2
	float twist_ = 1.0f;                 // Default fully twisted
	float lowCrossoverHz_ = 200.0f;
	float highCrossoverHz_ = 2000.0f;
};

/// "Twist3" crossover - 3 stages with progressive coefficient blending.
/// Same cost as ORDER=3 (6 ops) but blends coefficients across stages.
/// Combines Twisted's coefficient mixing with Weird's 3-stage depth.
/// Creates more extreme phase smearing than either alone.
class AllpassCrossoverTwist3 {
public:
	using Bands = CrossoverBands;

	AllpassCrossoverTwist3() = default;

	void setLowCrossover(float freqHz) {
		lowCrossoverHz_ = freqHz;
		lowCoeff_ = calculateCoefficient(freqHz);
		updateBlendedCoeffs();
	}

	void setHighCrossover(float freqHz) {
		highCrossoverHz_ = freqHz;
		highCoeff_ = calculateCoefficient(freqHz);
		updateBlendedCoeffs();
	}

	/// Set twist amount (0.0 = like Weird, 1.0 = fully twisted)
	void setTwist(float twist) {
		twist_ = std::clamp(twist, 0.0f, 1.0f);
		updateBlendedCoeffs();
	}

	[[nodiscard]] float getTwist() const { return twist_; }
	[[nodiscard]] float getLowCrossoverHz() const { return lowCrossoverHz_; }
	[[nodiscard]] float getHighCrossoverHz() const { return highCrossoverHz_; }

	/// Process with progressively blended coefficients
	[[gnu::always_inline]] inline void processStereo(q31_t inputL, q31_t inputR, Bands& outL, Bands& outR) {
		int32x2_t input = {inputL, inputR};

		// Low crossover: stage1=low, stage2=mid-blend, stage3=high-blend
		int32x2_t apLow1 = stereoState_.apLow1.doAPF(input, lowCoeff_);
		int32x2_t apLow2 = stereoState_.apLow2.doAPF(apLow1, lowStage2Coeff_);
		int32x2_t apLow3 = stereoState_.apLow3.doAPF(apLow2, lowStage3Coeff_);
		int32x2_t low = vhadd_s32(input, apLow3);
		int32x2_t rest = vhsub_s32(input, apLow3);

		// High crossover: stage1=high, stage2=mid-blend, stage3=low-blend
		int32x2_t apHigh1 = stereoState_.apHigh1.doAPF(rest, highCoeff_);
		int32x2_t apHigh2 = stereoState_.apHigh2.doAPF(apHigh1, highStage2Coeff_);
		int32x2_t apHigh3 = stereoState_.apHigh3.doAPF(apHigh2, highStage3Coeff_);
		int32x2_t mid = vhadd_s32(rest, apHigh3);
		int32x2_t high = vhsub_s32(rest, apHigh3);

		outL = {vget_lane_s32(low, 0), vget_lane_s32(mid, 0), vget_lane_s32(high, 0)};
		outR = {vget_lane_s32(low, 1), vget_lane_s32(mid, 1), vget_lane_s32(high, 1)};
	}

	void reset() {
		stereoState_.apLow1.reset();
		stereoState_.apLow2.reset();
		stereoState_.apLow3.reset();
		stereoState_.apHigh1.reset();
		stereoState_.apHigh2.reset();
		stereoState_.apHigh3.reset();
	}

private:
	struct StereoState {
		StereoFilterComponent apLow1, apLow2, apLow3;
		StereoFilterComponent apHigh1, apHigh2, apHigh3;
	};

	[[nodiscard]] static q31_t calculateCoefficient(float freqHz) {
		float fc = freqHz / static_cast<float>(kSampleRate);
		fc = std::clamp(fc, 0.001f, 0.49f);
		float wc = deluge::dsp::fastTan(3.14159265358979f * fc);
		float coeff = wc / (1.0f + wc);
		return static_cast<q31_t>(coeff * ONE_Q31);
	}

	/// Update blended coefficients - progressive blend across 3 stages
	void updateBlendedCoeffs() {
		// twist=0: all stages use own coeff (like Weird)
		// twist=1: stage2=50% blend, stage3=100% opposite
		float t = twist_;
		float lowF = static_cast<float>(lowCoeff_);
		float highF = static_cast<float>(highCoeff_);
		// Stage 2: 50% of twist amount
		lowStage2Coeff_ = static_cast<q31_t>(lowF + (highF - lowF) * t * 0.5f);
		highStage2Coeff_ = static_cast<q31_t>(highF + (lowF - highF) * t * 0.5f);
		// Stage 3: full twist amount
		lowStage3Coeff_ = static_cast<q31_t>(lowF + (highF - lowF) * t);
		highStage3Coeff_ = static_cast<q31_t>(highF + (lowF - highF) * t);
	}

	StereoState stereoState_{};
	q31_t lowCoeff_ = calculateCoefficient(200.0f);
	q31_t highCoeff_ = calculateCoefficient(2000.0f);
	q31_t lowStage2Coeff_ = lowCoeff_;
	q31_t lowStage3Coeff_ = lowCoeff_;
	q31_t highStage2Coeff_ = highCoeff_;
	q31_t highStage3Coeff_ = highCoeff_;
	float twist_ = 1.0f; // Default fully twisted
	float lowCrossoverHz_ = 200.0f;
	float highCrossoverHz_ = 2000.0f;
};

} // namespace deluge::dsp::filter
