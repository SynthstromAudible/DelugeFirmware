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

#include "dsp/util.hpp"
#include <array>
#include <cstdint>

namespace deluge::dsp::phi {

/**
 * φ-power constants for irrational frequency ratios
 *
 * Using powers of the golden ratio (φ ≈ 1.618) creates frequencies that
 * never align, producing quasi-periodic patterns with no exact repetition.
 * This is the mathematical foundation for zone-based parameter evolution.
 */
constexpr float kPhi = 1.6180340f; // Golden ratio (φ)

// Negative powers (slower than base)
constexpr float kPhiN100 = 0.6180340f; // φ^-1 = 1/φ
constexpr float kPhiN050 = 0.7861513f; // φ^-0.5
constexpr float kPhiN025 = 0.8872984f; // φ^-0.25

// Positive powers (faster than base)
constexpr float kPhi025 = 1.1271566f; // φ^0.25
constexpr float kPhi033 = 1.1746627f; // φ^0.33
constexpr float kPhi050 = 1.2720196f; // φ^0.5
constexpr float kPhi067 = 1.3871872f; // φ^0.67
constexpr float kPhi075 = 1.4352958f; // φ^0.75
constexpr float kPhi100 = 1.6180340f; // φ^1.0
constexpr float kPhi125 = 1.8257419f; // φ^1.25
constexpr float kPhi150 = 2.0581710f; // φ^1.5
constexpr float kPhi175 = 2.3197171f; // φ^1.75
constexpr float kPhi200 = 2.6180340f; // φ^2.0
constexpr float kPhi225 = 2.9603110f; // φ^2.25

// Higher powers (for multiband compressor)
constexpr float kPhi250 = 3.3302077f; // φ^2.5
constexpr float kPhi275 = 3.7515562f; // φ^2.75
constexpr float kPhi300 = 4.2360680f; // φ^3.0
constexpr float kPhi325 = 4.7742114f; // φ^3.25
constexpr float kPhi350 = 5.3884156f; // φ^3.5
constexpr float kPhi360 = 5.7067284f; // φ^3.6
constexpr float kPhi375 = 6.0409418f; // φ^3.75
constexpr float kPhi385 = 6.4408314f; // φ^3.85
constexpr float kPhi400 = 6.8541020f; // φ^4.0

/**
 * Wrap phase to [0,1) with double precision
 *
 * Use double precision for the computation to handle large secret knob values
 * (gamma can reach 10^15 before precision issues). Result is always [0,1) so
 * float output is sufficient.
 *
 * Uses int64_t truncation instead of std::floor for ~40 cycle savings per call.
 * Safe for positive values up to ~9×10^18 (int64_t max).
 *
 * @param phase Raw phase value (may be very large from secret knobs)
 * @return Wrapped phase in [0,1)
 */
[[gnu::always_inline]] inline float wrapPhase(double phase) {
	// Fast floor via int64_t truncation (valid for positive values)
	return static_cast<float>(phase - static_cast<double>(static_cast<int64_t>(phase)));
}

/**
 * Configuration for a single phi triangle parameter
 * Used with PhiTriContext for consistent evaluation
 */
struct PhiTriConfig {
	float phiFreq;     ///< φ^n frequency multiplier (e.g., kPhi350)
	float duty;        ///< Duty cycle [0,1] - active portion of triangle
	float phaseOffset; ///< Fixed offset for spreading related params (e.g., 0.25)
	bool bipolar;      ///< Map [0,1] → [-1,1] if true
};

/**
 * Shared context for phi triangle evaluations
 *
 * Encapsulates the repetitive pattern of:
 *   ph = wrapPhase(gammaPhase * phiFreq)
 *   base = yNorm * phiFreq * freqMult * periodScale
 *   tri = triangleSimpleUnipolar(wrapPhase(base + ph + offset), duty)
 *   result = enable * (bipolar ? tri*2-1 : tri)
 *
 * Usage:
 *   PhiTriContext ctx{yNorm, freqMult, periodScale, gammaPhase};
 *   p.subIntensity = ctx.eval(kPhi350, 0.3f, 0.0f, false, dzEnable);
 */
struct PhiTriContext {
	float yNorm;       ///< Normalized Y position [0,1]
	float freqMult;    ///< Y-dependent frequency acceleration
	float periodScale; ///< Overall period scaling
	double gammaPhase; ///< Secret knob phase (double for precision at large values)

	/// Evaluate a single phi triangle
	/// @param phiFreq φ^n frequency multiplier
	/// @param duty Duty cycle [0,1]
	/// @param phaseOffset Fixed phase offset for spreading
	/// @param bipolar Map output to [-1,1] if true
	/// @param enable Gating multiplier (0 = disabled)
	/// @return Triangle value in [0,1] or [-1,1]
	[[gnu::always_inline]] float eval(float phiFreq, float duty, float phaseOffset = 0.0f, bool bipolar = false,
	                                  float enable = 1.0f) const {
		float ph = wrapPhase(gammaPhase * phiFreq);
		float base = static_cast<float>(static_cast<double>(yNorm) * phiFreq * freqMult * periodScale);
		// Import triangleSimpleUnipolar from dsp namespace (defined in util.hpp)
		float tri = deluge::dsp::triangleSimpleUnipolar(wrapPhase(base + ph + phaseOffset), duty);
		return enable * (bipolar ? (tri * 2.0f - 1.0f) : tri);
	}

	/// Evaluate using a PhiTriConfig
	[[gnu::always_inline]] float eval(const PhiTriConfig& cfg, float enable = 1.0f) const {
		return eval(cfg.phiFreq, cfg.duty, cfg.phaseOffset, cfg.bipolar, enable);
	}

	/// Evaluate a bank of N phi triangles with shared enable
	/// @param configs Array of N configurations
	/// @param enable Gating multiplier applied to all
	/// @return Array of N evaluated values
	template <size_t N>
	[[gnu::always_inline]] std::array<float, N> evalBank(const std::array<PhiTriConfig, N>& configs,
	                                                     float enable = 1.0f) const {
		std::array<float, N> results;
		for (size_t i = 0; i < N; ++i) {
			results[i] = eval(configs[i], enable);
		}
		return results;
	}
};

/**
 * Evaluate a single triangle with proper phase handling
 *
 * Input args (phase, freqMult) combined with config (freq, duty, offset, bipolar).
 * Formula: wrapPhase((phase + cfg.offset) * cfg.freq * freqMult)
 *
 * @param phase Base phase (double for precision with large values)
 * @param freqMult Frequency modulation (e.g., Y-dependent acceleration), 1.0 = no mod
 * @param cfg Triangle configuration (freq, duty, offset, bipolar)
 * @return Triangle value in [0,1] or [-1,1] depending on cfg.bipolar
 */
[[gnu::always_inline]] inline float evalTriangle(double phase, float freqMult, const PhiTriConfig& cfg) {
	float wrappedPhase = wrapPhase((phase + cfg.phaseOffset) * cfg.phiFreq * freqMult);
	if (cfg.bipolar) {
		return deluge::dsp::triangleFloat(wrappedPhase, cfg.duty);
	}
	return deluge::dsp::triangleSimpleUnipolar(wrappedPhase, cfg.duty);
}

// ============================================================================
// NEON-optimized phi triangle evaluation (4-wide)
// ============================================================================

// Set to 0 to disable NEON and use scalar path for benchmarking
#ifndef PHI_TRIANGLE_USE_NEON
#define PHI_TRIANGLE_USE_NEON 0
#endif

#if PHI_TRIANGLE_USE_NEON

/**
 * NEON reciprocal with Newton-Raphson refinement (ARMv7 doesn't have vdivq_f32)
 * Two iterations gives ~24 bits of precision, sufficient for audio
 */
[[gnu::always_inline]] inline float32x4_t vrecipq_f32_nr(float32x4_t x) {
	float32x4_t recip = vrecpeq_f32(x);
	recip = vmulq_f32(recip, vrecpsq_f32(x, recip)); // First Newton-Raphson iteration
	recip = vmulq_f32(recip, vrecpsq_f32(x, recip)); // Second iteration for more precision
	return recip;
}

/**
 * Wrap 4 phases to [0,1) using float precision
 * Less precise than double version but sufficient for triangle evaluation
 */
[[gnu::always_inline]] inline float32x4_t wrapPhaseNeon(float32x4_t phase) {
	// Truncate to integer and subtract (equivalent to phase - floor(phase) for positive values)
	int32x4_t intPart = vcvtq_s32_f32(phase);
	return vsubq_f32(phase, vcvtq_f32_s32(intPart));
}

/**
 * Evaluate 4 unipolar triangles with NEON (branchless)
 * @param phase 4 wrapped phases in [0,1)
 * @param duty 4 duty cycles
 * @return 4 triangle values in [0,1]
 */
[[gnu::always_inline]] inline float32x4_t triangleUnipolarNeon(float32x4_t phase, float32x4_t duty) {
	// halfDuty = duty * 0.5
	float32x4_t halfDuty = vmulq_n_f32(duty, 0.5f);
	// invHalfDuty = 2.0 / duty (for scaling) - use reciprocal since no vdivq on ARMv7
	float32x4_t invHalfDuty = vmulq_f32(vdupq_n_f32(2.0f), vrecipq_f32_nr(duty));

	// Rising segment: phase < halfDuty → phase * invHalfDuty
	float32x4_t rising = vmulq_f32(phase, invHalfDuty);

	// Falling segment: phase < duty → (duty - phase) * invHalfDuty
	float32x4_t falling = vmulq_f32(vsubq_f32(duty, phase), invHalfDuty);

	// Select: phase < halfDuty ? rising : (phase < duty ? falling : 0)
	uint32x4_t inRising = vcltq_f32(phase, halfDuty);
	uint32x4_t inActive = vcltq_f32(phase, duty);

	// Blend: rising where inRising, falling where inActive && !inRising, 0 otherwise
	float32x4_t result = vbslq_f32(inRising, rising, falling);
	result = vbslq_f32(inActive, result, vdupq_n_f32(0.0f));

	return result;
}

/**
 * Evaluate 4 bipolar triangles with NEON (branchless)
 * @param phase 4 wrapped phases in [0,1)
 * @param duty 4 duty cycles
 * @return 4 triangle values in [-1,1]
 */
[[gnu::always_inline]] inline float32x4_t triangleBipolarNeon(float32x4_t phase, float32x4_t duty) {
	// quarterDuty = duty * 0.25, halfDuty = duty * 0.5
	float32x4_t quarterDuty = vmulq_n_f32(duty, 0.25f);
	float32x4_t halfDuty = vmulq_n_f32(duty, 0.5f);
	float32x4_t threeQuarterDuty = vaddq_f32(halfDuty, quarterDuty);
	// invQuarterDuty = 1.0 / quarterDuty - use reciprocal since no vdivq on ARMv7
	float32x4_t invQuarterDuty = vrecipq_f32_nr(quarterDuty);

	// Segment 1: phase < quarterDuty → phase / quarterDuty (0→+1)
	float32x4_t seg1 = vmulq_f32(phase, invQuarterDuty);

	// Segment 2: phase < halfDuty → (halfDuty - phase) / quarterDuty (+1→0)
	float32x4_t seg2 = vmulq_f32(vsubq_f32(halfDuty, phase), invQuarterDuty);

	// Segment 3: phase < threeQuarterDuty → -(phase - halfDuty) / quarterDuty (0→-1)
	float32x4_t seg3 = vnegq_f32(vmulq_f32(vsubq_f32(phase, halfDuty), invQuarterDuty));

	// Segment 4: phase < duty → (phase - duty) / quarterDuty (-1→0)
	float32x4_t seg4 = vmulq_f32(vsubq_f32(phase, duty), invQuarterDuty);

	// Select segments based on phase ranges
	uint32x4_t inSeg1 = vcltq_f32(phase, quarterDuty);
	uint32x4_t inSeg2 = vcltq_f32(phase, halfDuty);
	uint32x4_t inSeg3 = vcltq_f32(phase, threeQuarterDuty);
	uint32x4_t inActive = vcltq_f32(phase, duty);

	// Cascade selection: seg1 → seg2 → seg3 → seg4 → 0
	float32x4_t result = vbslq_f32(inActive, seg4, vdupq_n_f32(0.0f));
	result = vbslq_f32(inSeg3, seg3, result);
	result = vbslq_f32(inSeg2, seg2, result);
	result = vbslq_f32(inSeg1, seg1, result);

	return result;
}

/**
 * Evaluate 4 phi triangles with NEON
 * @param phase Base phase (double for precision in offset calculation)
 * @param freqMult Frequency multiplier
 * @param phiFreqs 4 phi frequency multipliers
 * @param duties 4 duty cycles
 * @param offsets 4 phase offsets
 * @param bipolarMask Bitmask: bit i set = triangle i is bipolar
 * @return 4 evaluated triangle values
 */
[[gnu::always_inline]] inline float32x4_t evalTriangle4Neon(double phase, float freqMult, float32x4_t phiFreqs,
                                                            float32x4_t duties, float32x4_t offsets,
                                                            uint32_t bipolarMask) {
	// Compute phases: (phase + offset) * phiFreq * freqMult
	// Use float for the NEON path (sufficient precision for triangle evaluation)
	float32x4_t basePhase = vdupq_n_f32(static_cast<float>(phase));
	float32x4_t phases = vaddq_f32(basePhase, offsets);
	phases = vmulq_f32(phases, phiFreqs);
	phases = vmulq_n_f32(phases, freqMult);
	phases = wrapPhaseNeon(phases);

	// Evaluate both unipolar and bipolar
	float32x4_t unipolar = triangleUnipolarNeon(phases, duties);
	float32x4_t bipolar = triangleBipolarNeon(phases, duties);

	// Select based on bipolar mask
	uint32x4_t mask = {(bipolarMask & 1) ? 0xFFFFFFFF : 0, (bipolarMask & 2) ? 0xFFFFFFFF : 0,
	                   (bipolarMask & 4) ? 0xFFFFFFFF : 0, (bipolarMask & 8) ? 0xFFFFFFFF : 0};
	return vbslq_f32(mask, bipolar, unipolar);
}

/**
 * Evaluate a bank of N triangles with proper phase handling
 *
 * Each triangle gets: wrapPhase((phase + cfg.phaseOffset) * cfg.phiFreq * freqMult)
 *
 * @param phase Base phase (double for precision)
 * @param freqMult Frequency modulation applied to all triangles
 * @param configs Array of N configurations
 * @return Array of N triangle values (unipolar [0,1] or bipolar [-1,1] per config)
 */
#endif // PHI_TRIANGLE_USE_NEON

template <size_t N>
[[gnu::always_inline]] inline std::array<float, N> evalTriangleBank(double phase, float freqMult,
                                                                    const std::array<PhiTriConfig, N>& configs) {
	std::array<float, N> results;

#if PHI_TRIANGLE_USE_NEON
	// Process in chunks of 4 using NEON
	constexpr size_t nChunks = N / 4;
	for (size_t chunk = 0; chunk < nChunks; ++chunk) {
		size_t base = chunk * 4;

		// Load config data into NEON vectors
		float32x4_t phiFreqs = {configs[base].phiFreq, configs[base + 1].phiFreq, configs[base + 2].phiFreq,
		                        configs[base + 3].phiFreq};
		float32x4_t duties = {configs[base].duty, configs[base + 1].duty, configs[base + 2].duty,
		                      configs[base + 3].duty};
		float32x4_t offsets = {configs[base].phaseOffset, configs[base + 1].phaseOffset, configs[base + 2].phaseOffset,
		                       configs[base + 3].phaseOffset};

		// Build bipolar mask
		uint32_t bipolarMask = (configs[base].bipolar ? 1 : 0) | (configs[base + 1].bipolar ? 2 : 0)
		                       | (configs[base + 2].bipolar ? 4 : 0) | (configs[base + 3].bipolar ? 8 : 0);

		// Evaluate 4 triangles at once
		float32x4_t vals = evalTriangle4Neon(phase, freqMult, phiFreqs, duties, offsets, bipolarMask);

		// Store results
		vst1q_f32(&results[base], vals);
	}

	// Handle remaining triangles (N % 4) with scalar path
	for (size_t i = nChunks * 4; i < N; ++i) {
		results[i] = evalTriangle(phase, freqMult, configs[i]);
	}
#else
	// Scalar path - simple loop
	for (size_t i = 0; i < N; ++i) {
		results[i] = evalTriangle(phase, freqMult, configs[i]);
	}
#endif

	return results;
}

/**
 * Pre-configured phi triangle bank for "extras" effects
 *
 * Bank indices:
 *   [0] subRatio  - Subharmonic ratio selector (sparse, slow)
 *   [1] stride    - ZC detection stride [1,128] (also sets comb freq)
 *   [2] feedback  - Comb filter feedback intensity [0,0.8]
 *   [3] rotation  - Bit rotation amount [0,31] (aliasing effect)
 *
 * Phase offsets spread by 0.25 for decorrelation.
 * Uses slow φ^n frequencies for gradual evolution.
 */
constexpr std::array<PhiTriConfig, 4> kExtrasBank = {{
    {kPhi050, 1.00f, 0.00f, false},  // [0] subRatio: 100% duty for testing
    {kPhi067, 1.00f, 0.25f, false},  // [1] stride: full duty for avg 64
    {kPhi075, 1.00f, 0.50f, false},  // [2] feedback: 100% duty for testing
    {kPhiN050, 1.00f, 0.75f, false}, // [3] rotation: 100% duty for testing
}};

/// Map triangle value [0,1] to subharmonic ratio {2,3,4,5,6}
/// Uses 5 equal bands with slight hysteresis overlap avoided by floor
/// @param tri Triangle value in [0,1]
/// @return ZC threshold: 2=octave, 3=twelfth, 4=2oct, 5=2oct+3rd, 6=2oct+5th
[[gnu::always_inline]] inline int8_t subRatioFromTriangle(float tri) {
	// Map [0,1] → [2,6] with 5 equal bands
	// 0.0-0.2 → 2, 0.2-0.4 → 3, 0.4-0.6 → 4, 0.6-0.8 → 5, 0.8-1.0 → 6
	int8_t ratio = static_cast<int8_t>(tri * 5.0f) + 2;
	return std::clamp(ratio, static_cast<int8_t>(2), static_cast<int8_t>(6));
}

/// Map triangle value [0,1] to ZC detection stride [1,128]
/// Biased so triangle average 0.5 → stride 64 (buffer midpoint)
/// Lower stride = more frequent ZC checks, higher = less frequent (bass-only)
/// Also determines feedback comb frequency: 44100/stride Hz
/// @param tri Triangle value in [0,1]
/// @return Stride interval in samples [1,128]
[[gnu::always_inline]] inline int32_t strideFromTriangle(float tri) {
	// Centered on 64: tri=0→1, tri=0.5→64, tri=1→128
	int32_t stride = 64 + static_cast<int32_t>((tri - 0.5f) * 128.0f);
	return std::clamp(stride, static_cast<int32_t>(1), static_cast<int32_t>(128));
}

/// Map triangle value [0,1] to feedback intensity [0,0.8]
/// Capped at 0.8 to prevent runaway oscillation (feedback < 1.0 required)
/// Combined with stride, creates comb filter at 44100/stride Hz
/// @param tri Triangle value in [0,1]
/// @return Feedback intensity [0,0.8]
[[gnu::always_inline]] inline float feedbackFromTriangle(float tri) {
	// Linear mapping with 0.8 cap for stability
	return tri * 0.8f;
}

/// Map triangle value [0,1] to bit rotation amount [0,31]
/// Creates aliasing artifacts by rotating bits in the sample word.
/// ARM ROR is single-cycle, so this is essentially free.
/// @param tri Triangle value in [0,1]
/// @return Rotation amount in bits [0,31]
[[gnu::always_inline]] inline int8_t rotationFromTriangle(float tri) {
	// Linear mapping: tri=0 → 0 bits (passthrough), tri=1 → 31 bits (max)
	// 32-bit rotation wraps back to identity, so cap at 31
	return static_cast<int8_t>(tri * 31.0f);
}

} // namespace deluge::dsp::phi
