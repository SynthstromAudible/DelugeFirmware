/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "util/fixedpoint.h"
#include <algorithm>
#include <arm_neon.h>
#include <cstdint>
namespace deluge::dsp::filter {
class BasicFilterComponent {
public:
	BasicFilterComponent() { reset(); }
	// moveability is tan(f)/(1+tan(f))
	[[gnu::always_inline]] inline q31_t doFilter(q31_t input, q31_t moveability) {
		q31_t a = multiply_32x32_rshift32_rounded(input - memory, moveability) << 1;
		q31_t b = a + memory;
		memory = b + a;
		return b;
	}
	[[gnu::always_inline]] inline int32_t doAPF(q31_t input, int32_t moveability) {
		q31_t a = multiply_32x32_rshift32_rounded(input - memory, moveability) << 1;
		q31_t b = a + memory;
		memory = a + b;
		return b * 2 - input;
	}
	[[gnu::always_inline]] inline void affectFilter(q31_t input, int32_t moveability) {
		memory += multiply_32x32_rshift32_rounded(input - memory, moveability) << 2;
	}
	[[gnu::always_inline]] inline void reset() { memory = 0; }
	[[gnu::always_inline]] inline q31_t getFeedbackOutput(int32_t feedbackAmount) {
		return multiply_32x32_rshift32_rounded(memory, feedbackAmount) << 2;
	}
	[[gnu::always_inline]] inline q31_t getFeedbackOutputWithoutLshift(int32_t feedbackAmount) {
		return multiply_32x32_rshift32_rounded(memory, feedbackAmount);
	}

	q31_t memory = 0;
};

/// Stereo filter component - processes L/R channels in parallel using NEON
/// Note: Do NOT add alignas() - it causes static initialization crashes
struct StereoFilterComponent {
	int32_t memory_[2]{};

	void reset() {
		memory_[0] = 0;
		memory_[1] = 0;
	}

	/// NEON-vectorized allpass filter for stereo (L/R in parallel)
	[[gnu::always_inline]] int32x2_t doAPF(int32x2_t input, int32_t moveability) {
		int32x2_t memory = vld1_s32(memory_);
		int32x2_t coeff = vdup_n_s32(moveability);
		int32x2_t diff = vsub_s32(input, memory);
		int32x2_t a = vqrdmulh_s32(diff, coeff);
		int32x2_t b = vadd_s32(a, memory);
		memory = vadd_s32(a, b);
		vst1_s32(memory_, memory);
		int32x2_t b2 = vshl_n_s32(b, 1);
		return vsub_s32(b2, input);
	}

	/// NEON-vectorized saturating allpass filter for stereo (L/R in parallel)
	/// Uses saturating add to prevent overflow artifacts (bitcrushing)
	[[gnu::always_inline]] int32x2_t doAPFSaturating(int32x2_t input, int32_t moveability) {
		int32x2_t memory = vld1_s32(memory_);
		int32x2_t coeff = vdup_n_s32(moveability);
		int32x2_t diff = vsub_s32(input, memory);
		int32x2_t a = vqrdmulh_s32(diff, coeff);
		int32x2_t b = vadd_s32(a, memory);
		memory = vadd_s32(a, b);
		vst1_s32(memory_, memory);
		// Saturating: b + (b - input) instead of b * 2 - input
		return vqadd_s32(b, vsub_s32(b, input));
	}

	/// NEON-vectorized saturating allpass with separate L/R coefficients
	/// coeffsLR is packed {coeffL, coeffR} for different frequencies per channel
	[[gnu::always_inline]] int32x2_t doAPFSaturatingLR(int32x2_t input, int32x2_t coeffsLR) {
		int32x2_t memory = vld1_s32(memory_);
		int32x2_t diff = vsub_s32(input, memory);
		int32x2_t a = vqrdmulh_s32(diff, coeffsLR);
		int32x2_t b = vadd_s32(a, memory);
		memory = vadd_s32(a, b);
		vst1_s32(memory_, memory);
		// Saturating: b + (b - input) instead of b * 2 - input
		return vqadd_s32(b, vsub_s32(b, input));
	}

	/// NEON-vectorized lowpass filter for stereo (L/R in parallel)
	[[gnu::always_inline]] int32x2_t doFilter(int32x2_t input, int32_t moveability) {
		int32x2_t memory = vld1_s32(memory_);
		int32x2_t coeff = vdup_n_s32(moveability);
		int32x2_t diff = vsub_s32(input, memory);
		int32x2_t a = vqrdmulh_s32(diff, coeff);
		int32x2_t b = vadd_s32(a, memory);
		memory = vadd_s32(b, a);
		vst1_s32(memory_, memory);
		return b;
	}
};

/// Quad filter component - processes 4 channels in parallel using NEON
/// Note: Do NOT add alignas() - it causes static initialization crashes
struct QuadFilterComponent {
	int32_t memory_[4]{};

	void reset() {
		memory_[0] = 0;
		memory_[1] = 0;
		memory_[2] = 0;
		memory_[3] = 0;
	}

	/// NEON-vectorized allpass filter for 4 channels in parallel
	[[gnu::always_inline]] int32x4_t doAPF(int32x4_t input, int32_t moveability) {
		int32x4_t memory = vld1q_s32(memory_);
		int32x4_t coeff = vdupq_n_s32(moveability);
		int32x4_t diff = vsubq_s32(input, memory);
		int32x4_t a = vqrdmulhq_s32(diff, coeff);
		int32x4_t b = vaddq_s32(a, memory);
		memory = vaddq_s32(a, b);
		vst1q_s32(memory_, memory);
		int32x4_t b2 = vshlq_n_s32(b, 1);
		return vsubq_s32(b2, input);
	}

	/// NEON-vectorized lowpass filter for 4 channels in parallel
	[[gnu::always_inline]] int32x4_t doFilter(int32x4_t input, int32_t moveability) {
		int32x4_t memory = vld1q_s32(memory_);
		int32x4_t coeff = vdupq_n_s32(moveability);
		int32x4_t diff = vsubq_s32(input, memory);
		int32x4_t a = vqrdmulhq_s32(diff, coeff);
		int32x4_t b = vaddq_s32(a, memory);
		memory = vaddq_s32(b, a);
		vst1q_s32(memory_, memory);
		return b;
	}
};

/// 2nd-order biquad allpass coefficients (for variable Q disperser)
/// Computed once per buffer, shared across L/R channels
struct BiquadAllpassCoeffs {
	// Direct Form II Transposed coefficients
	// For allpass: b0=a2, b1=a1, b2=1 (mirror symmetry)
	q31_t a1{0}; // feedback coeff 1 (can exceed 1.0, stored scaled)
	q31_t a2{0}; // feedback coeff 2 (also = b0)
	// b1 = a1, b2 = 1.0 (implicit)

	/// Fast tan approximation for bilinear transform (valid for w in 0..π/2)
	/// Pade approximant: tan(w) ≈ w * (1 + w²/15) / (1 - 4w²/15)
	/// Accurate to <0.3% for w < 1.4, which covers fc < 19kHz at 44.1kHz
	[[gnu::always_inline]] static float fastTan(float w) {
		float w2 = w * w;
		return w * (1.0f + w2 * 0.0666667f) / (1.0f - w2 * 0.2666667f);
	}

	/// Compute coefficients from frequency and Q
	/// @param fc Center frequency in Hz
	/// @param Q Quality factor (0.5 = broad, 10+ = sharp/resonant)
	/// @param fs Sample rate in Hz (default 44100)
	void compute(float fc, float Q, float fs = 44100.0f) {
		// Bilinear transform: k = tan(π * fc / fs)
		float w = 3.14159265358979f * fc / fs;
		float k = fastTan(std::clamp(w, 0.001f, 1.4f)); // Clamp for approximation validity
		float k2 = k * k;
		float kQ = k / std::max(Q, 0.1f);
		float norm = 1.0f / (1.0f + kQ + k2);

		// a1 = 2*(k² - 1) * norm, can range roughly -2 to +2
		// a2 = (1 - k/Q + k²) * norm, ranges 0 to 1
		float a1f = 2.0f * (k2 - 1.0f) * norm;
		float a2f = (1.0f - kQ + k2) * norm;

		// Store a1 with 0.5x scale to fit in Q31 (will shift in processing)
		a1 = static_cast<q31_t>(std::clamp(a1f * 0.5f, -1.0f, 0.9999f) * ONE_Q31);
		a2 = static_cast<q31_t>(std::clamp(a2f, -1.0f, 0.9999f) * ONE_Q31);
	}
};

/// 2nd-order biquad allpass filter - stereo NEON implementation
/// Provides 360° phase shift with variable Q (vs 180° for 1st-order)
/// Higher Q = sharper phase transition = more "resonant" disperser sound
struct StereoBiquadAllpass {
	// State for Direct Form II Transposed (2 states per channel)
	int32_t s1_[2]{}; // L/R state 1
	int32_t s2_[2]{}; // L/R state 2

	void reset() {
		s1_[0] = s1_[1] = 0;
		s2_[0] = s2_[1] = 0;
	}

	/// Fade state toward zero (keeps 12.5% per call) for click-free zone transitions
	void fadeState() {
		s1_[0] >>= 3;
		s1_[1] >>= 3;
		s2_[0] >>= 3;
		s2_[1] >>= 3;
	}

	/// Process stereo sample through 2nd-order allpass
	/// coeffs.a1 is stored at 0.5x scale, so we shift left after multiply
	[[gnu::always_inline]] int32x2_t process(int32x2_t input, const BiquadAllpassCoeffs& coeffs) {
		int32x2_t s1 = vld1_s32(s1_);
		int32x2_t s2 = vld1_s32(s2_);
		int32x2_t a1 = vdup_n_s32(coeffs.a1);
		int32x2_t a2 = vdup_n_s32(coeffs.a2);

		// Direct Form II Transposed:
		// y = b0*x + s1  (b0 = a2 for allpass)
		// s1 = b1*x - a1*y + s2  (b1 = a1 for allpass)
		// s2 = b2*x - a2*y  (b2 = 1 for allpass)

		// y = a2*x + s1
		int32x2_t a2x = vqrdmulh_s32(input, a2);
		int32x2_t y = vqadd_s32(a2x, s1);

		// s1 = a1*(x - y) + s2
		// a1 is stored at 0.5x, so we need to shift. Use 2x multiply approach.
		int32x2_t diff = vqsub_s32(input, y);
		int32x2_t a1diff = vqrdmulh_s32(diff, a1);
		int32x2_t a1diff_scaled = vqadd_s32(a1diff, a1diff); // *2 to compensate 0.5x storage
		int32x2_t new_s1 = vqadd_s32(a1diff_scaled, s2);

		// s2 = x - a2*y
		int32x2_t a2y = vqrdmulh_s32(y, a2);
		int32x2_t new_s2 = vqsub_s32(input, a2y);

		vst1_s32(s1_, new_s1);
		vst1_s32(s2_, new_s2);

		return y;
	}

	/// Process with separate L/R coefficients (for stereo spread)
	[[gnu::always_inline]] int32x2_t processLR(int32x2_t input, const BiquadAllpassCoeffs& coeffsL,
	                                           const BiquadAllpassCoeffs& coeffsR) {
		int32x2_t s1 = vld1_s32(s1_);
		int32x2_t s2 = vld1_s32(s2_);

		// Pack L/R coefficients
		int32_t a1_arr[2] = {coeffsL.a1, coeffsR.a1};
		int32_t a2_arr[2] = {coeffsL.a2, coeffsR.a2};
		int32x2_t a1 = vld1_s32(a1_arr);
		int32x2_t a2 = vld1_s32(a2_arr);

		// y = a2*x + s1
		int32x2_t a2x = vqrdmulh_s32(input, a2);
		int32x2_t y = vqadd_s32(a2x, s1);

		// s1 = a1*(x - y) + s2 (with 2x scale compensation)
		int32x2_t diff = vqsub_s32(input, y);
		int32x2_t a1diff = vqrdmulh_s32(diff, a1);
		int32x2_t a1diff_scaled = vqadd_s32(a1diff, a1diff);
		int32x2_t new_s1 = vqadd_s32(a1diff_scaled, s2);

		// s2 = x - a2*y
		int32x2_t a2y = vqrdmulh_s32(y, a2);
		int32x2_t new_s2 = vqsub_s32(input, a2y);

		vst1_s32(s1_, new_s1);
		vst1_s32(s2_, new_s2);

		return y;
	}
};

} // namespace deluge::dsp::filter
