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
template <size_t N>
[[gnu::always_inline]] inline std::array<float, N> evalTriangleBank(double phase, float freqMult,
                                                                    const std::array<PhiTriConfig, N>& configs) {
	std::array<float, N> results;
	for (size_t i = 0; i < N; ++i) {
		results[i] = evalTriangle(phase, freqMult, configs[i]);
	}
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
