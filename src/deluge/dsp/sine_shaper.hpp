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

// Sine shaper DSP - extracted from util.hpp for maintainability
// This file contains all sine shaper waveshaping algorithms, zone logic,
// harmonic extraction, and buffer processing functions.

#include "dsp/fast_math.h"         // For fastSinHalfPi (rect2 optimization)
#include "dsp/phi_triangle.hpp"    // For φ-power constants
#include "dsp/stereo_sample.h"     // For StereoSample type
#include "dsp/util.hpp"            // For smoothing helpers, polynomial primitives
#include "dsp/zone_param.hpp"      // For ZoneBasedParam template
#include "io/debug/fx_benchmark.h" // For FX benchmarking
#include "modulation/params/param.h"
#include "storage/field_serialization.h"
#include <arm_neon.h> // For NEON SIMD vectorization
#include <cstring>
#include <span>

namespace deluge::dsp {

// ============================================================================
// Smoothing Constants and Utilities
// ============================================================================
// IIR smoothing for parameter changes to avoid zipper noise.
// These were originally in dsp_ng library but are now inline here.

/// Per-sample IIR alpha for float coefficient smoothing (~5ms at 44.1kHz)
/// Higher than q31 version for faster response on harmonic coefficients
constexpr float kPerSampleAlpha = 0.005f;

/// Strided smoothing period (update coefficients every N samples)
/// Reduces CPU overhead for coefficient smoothing in tight loops
constexpr int32_t kSmoothingStride = 4;

/// Strided IIR alpha (scaled up by kSmoothingStride for equivalent response)
constexpr float kStridedAlpha = kPerSampleAlpha * kSmoothingStride;

/// Convergence epsilon for float smoothing (when |current - target| < epsilon, stop updating)
constexpr float kSmoothingConvergenceEpsilon = 1e-6f;

/// Q31 per-sample smoothing alpha - matches kPerSampleAlpha for consistent smoothing
/// (~5ms time constant at 44.1kHz, same as float version)
constexpr q31_t kParamSmoothingAlpha = static_cast<q31_t>(0.005 * 2147483647.0);

/// Context for per-sample float IIR parameter smoothing
struct FloatSmoothingContext {
	float current;
	float target;
	float alpha;
};

/// Prepare float parameter smoothing context
[[gnu::always_inline]] inline FloatSmoothingContext prepareSmoothingFloat(float& state, float target,
                                                                          [[maybe_unused]] size_t bufferSize) {
	FloatSmoothingContext ctx{state, target, kPerSampleAlpha};
	return ctx;
}

/// Context for per-sample q31 IIR parameter smoothing
struct Q31SmoothingContext {
	q31_t current;
	q31_t target;
	q31_t alpha;
};

/// Prepare q31 parameter smoothing context
[[gnu::always_inline]] inline Q31SmoothingContext prepareSmoothing(q31_t state, q31_t target,
                                                                   [[maybe_unused]] size_t bufferSize) {
	return {state, target, kParamSmoothingAlpha};
}

/// Check if a float smoothing context has converged
[[gnu::always_inline]] inline bool isConverged(const FloatSmoothingContext& ctx) {
	return std::abs(ctx.current - ctx.target) < kSmoothingConvergenceEpsilon;
}

/// Smooth a q31 parameter with IIR filter, updating state in place
/// Returns the smoothed value
[[gnu::always_inline]] inline q31_t smoothParam(q31_t* state, q31_t target) {
	q31_t diff = target - *state;
	// IIR: state += alpha * (target - state)
	*state += multiply_32x32_rshift32(diff, kParamSmoothingAlpha) << 1;
	return *state;
}

// ============================================================================
// NEON-Vectorized Coefficient Smoothing
// ============================================================================
// Process 4 float IIR updates in parallel using ARM NEON SIMD.
// Used for per-sample coefficient smoothing without the cost of 9 scalar updates.

/// Vectorized smoothing context for 4 coefficients
struct alignas(16) NeonSmoothingContext {
	float32x4_t current; // Current smoothed values
	float32x4_t target;  // Target values
	float32x4_t alpha;   // IIR coefficient (same for all 4)
};

/// Initialize NEON smoothing context from 4 current/target pairs
[[gnu::always_inline]] inline NeonSmoothingContext
prepareNeonSmoothing(float c0, float c1, float c2, float c3, float t0, float t1, float t2, float t3, float alpha) {
	NeonSmoothingContext ctx;
	alignas(16) float cur[4] = {c0, c1, c2, c3};
	alignas(16) float tgt[4] = {t0, t1, t2, t3};
	ctx.current = vld1q_f32(cur);
	ctx.target = vld1q_f32(tgt);
	ctx.alpha = vdupq_n_f32(alpha);
	return ctx;
}

/// Update 4 coefficients in parallel: current += (target - current) * alpha
[[gnu::always_inline]] inline void updateNeonSmoothing(NeonSmoothingContext& ctx) {
	float32x4_t delta = vsubq_f32(ctx.target, ctx.current);
	ctx.current = vmlaq_f32(ctx.current, delta, ctx.alpha); // fused multiply-add
}

/// Extract individual values from NEON vector
[[gnu::always_inline]] inline float getNeonLane0(float32x4_t v) {
	return vgetq_lane_f32(v, 0);
}
[[gnu::always_inline]] inline float getNeonLane1(float32x4_t v) {
	return vgetq_lane_f32(v, 1);
}
[[gnu::always_inline]] inline float getNeonLane2(float32x4_t v) {
	return vgetq_lane_f32(v, 2);
}
[[gnu::always_inline]] inline float getNeonLane3(float32x4_t v) {
	return vgetq_lane_f32(v, 3);
}

/// Check if all 4 NEON coefficients have converged (|current - target| < epsilon)
/// ARMv7 compatible - no vminvq (ARMv8 only)
[[gnu::always_inline]] inline bool isNeonConverged(const NeonSmoothingContext& ctx) {
	float32x4_t diff = vabdq_f32(ctx.current, ctx.target); // |current - target|
	float32x4_t eps = vdupq_n_f32(kSmoothingConvergenceEpsilon);
	uint32x4_t cmp = vcltq_f32(diff, eps); // diff < epsilon (all bits set if true)
	// ARMv7: AND all lanes together, then check if result is all 1s
	uint32x2_t low = vget_low_u32(cmp);
	uint32x2_t high = vget_high_u32(cmp);
	uint32x2_t anded = vand_u32(low, high); // AND lanes [0,1] with [2,3]
	uint32_t final = vget_lane_u32(anded, 0) & vget_lane_u32(anded, 1);
	return final == 0xFFFFFFFF;
}

/// Zone count derived from param definition (single source of truth)
constexpr int32_t kNumHarmonicZones =
    modulation::params::getZoneParamInfo(modulation::params::LOCAL_SINE_SHAPER_HARMONIC).zoneCount;

// Harmonic zone names for benchmarking tags (based on harmonic parameter, not twist)
// Zones 0-1: Chebyshev polynomial waveshaping
// Zones 2-6: FM-based waveshaping with different algorithms
// Zone 7: Poly cascade
inline constexpr const char* kSineShaperZoneNames[] = {
    "z0_cheby", "z1_cheby", "z2_fm", "z3_fm", "z4_fm", "z5_fm", "z6_fm", "z7_poly",
};

// ============================================================================
// Sine Shaper Distortion
// ============================================================================
// A soft-clipping waveshaper using polynomial saturation with additional harmonics.
// - Drive: Input gain before shaping (controls saturation amount)
// - Harmonic: Adds odd harmonics via polynomial shaping
// - Symmetry: DC bias before shaping (adds even harmonics via asymmetry)
// - Mix: Wet/dry blend (0 = bypass processing entirely)
//
// Patched Param Design (Implemented):
// ------------------------------------
// Twist and Harmonic are patched params with mod-matrix routing via
// LOCAL_SINE_SHAPER_TWIST and LOCAL_SINE_SHAPER_HARMONIC.
// Both have 8 zones (Twist: Width/Evens/Rect/Fdbk + 4 meta sub-zones).
// Modulation ADDs to menu setting (not multiply), with full-scale
// bipolar modulation spanning 1/8 of q31 range (~268M) = exactly 1 zone.
// This allows LFO/envelope modulation to sweep through adjacent zones
// while the menu position establishes the base zone.
//
// UI: Press encoder (no twist) opens mod routing menu.
// Push+twist is secret menu for phase offsets (twistPhaseOffset, harmonicPhaseOffset, gammaPhase).
// Clips use UNPATCHED variants (no mod matrix routing).

// Forward declaration for cached weights
struct ShaperWeights;

/// Sine shaper parameters and DSP state for one sound instance
/// Note: For sounds, harmonic uses LOCAL_SINE_SHAPER_HARMONIC, twist uses LOCAL_SINE_SHAPER_TWIST
/// (patched params with mod matrix routing). Clips use UNPATCHED variants.
struct SineTableShaperParams {
	// User-facing parameters (0-127, converted to q31_t for DSP)
	// Note: Drive is now a patched param (LOCAL_SINE_SHAPER_DRIVE), not stored here
	uint8_t symmetry{64}; // DEPRECATED: kept for XML backwards compat, use Twist param instead
	uint8_t mix{0};       // Wet/dry blend (0 = bypass). Not a patched param to reduce LOC overhead.
	// Zone base values with behavior: harmonic clips to zones, twist allows cross-zone
	ZoneBasedParam<kNumHarmonicZones, true> harmonic; // Clips to zone boundaries
	ZoneBasedParam<kNumHarmonicZones, false> twist;   // Allows cross-zone modulation
	// Phase offsets (per-patch, secret menus)
	float twistPhaseOffset{0};    // Offset for Twist param triangles (push Twist encoder)
	float harmonicPhaseOffset{0}; // Offset for Harmonic zone triangles (push Harmonic encoder)
	float gammaPhase{0};          // 100x multiplier phase (push Mix encoder)
	// DSP smoothing state (per-sound, shared across voices)
	// INT32_MIN = sentinel for "snap to target on first use"
	q31_t smoothedDrive{INT32_MIN}; // Previous drive value for parameter smoothing
	q31_t smoothedHarmonic{0};      // Previous harmonic value (vestigial, kept for serialization)
	// Smoothed zone coefficients - reused across zones, zone boundary glitches are acceptable
	float smoothedC1{1.0f}, smoothedC3L{0}, smoothedC5L{0}, smoothedC7L{0}, smoothedC9L{0};
	// R channel (c1R = c1, so only c3R/c5R/c7R/c9R needed)
	float smoothedC3R{0}, smoothedC5R{0}, smoothedC7R{0}, smoothedC9R{0};

	// Weight computation cache - skip recomputation when params unchanged
	// Saves ~2000 cycles/buffer when harmonic/twist are static (common case)
	q31_t cachedHarmonic{0};
	float cachedPhaseHarmonic{0};
	float cachedPhaseHarmonicFreqMod{0}; // For blend weight frequency modulation
	float cachedStereoWidth{0};
	float cachedStereoPhaseOffset{0};
	float cachedStereoFreqMult{1.0f};
	int32_t cachedZone{-1}; // -1 = cache invalid
	// Cached target weights (10 floats: c1 shared, c3-c9 per channel)
	float cachedTargetC1{0};
	float cachedTargetC3L{0}, cachedTargetC5L{0}, cachedTargetC7L{0}, cachedTargetC9L{0};
	float cachedTargetC3R{0}, cachedTargetC5R{0}, cachedTargetC7R{0}, cachedTargetC9R{0};

	// ========================================================================
	// Encapsulated Processing API
	// ========================================================================
	// Simplifies callsites by hiding param combination, smoothing, and twist computation.
	// Use prepareVoiceParams() for patched params (voices), prepareClipParams() for unpatched (clips).

	/// Check if effect is enabled (mix > 0)
	[[nodiscard]] bool isEnabled() const { return mix > 0; }

	/// Get mix as q31_t
	[[nodiscard]] q31_t getMixQ31() const { return static_cast<q31_t>(mix) << 24; }

	/// Write sine shaper params to file (only non-default values)
	/// Note: Drive is now a patched param (LOCAL_SINE_SHAPER_DRIVE), serialized separately
	void writeToFile(Serializer& writer) const {
		WRITE_FIELD(writer, mix, "sineShaperMix");
		WRITE_ZONE(writer, harmonic.value, "sineShaperHarmonicBase");
		WRITE_ZONE(writer, twist.value, "sineShaperTwistBase");
		WRITE_FLOAT(writer, twistPhaseOffset, "sineShaperMetaPhase", 10.0f);
		WRITE_FLOAT(writer, harmonicPhaseOffset, "sineShaperMetaPhaseH", 10.0f);
		WRITE_FLOAT(writer, gammaPhase, "sineShaperGamma", 10.0f);
	}

	/// Read a tag into sine shaper params, returns true if tag was handled
	/// Note: Drive is now a patched param (LOCAL_SINE_SHAPER_DRIVE), read separately
	bool readTag(Deserializer& reader, const char* tagName) {
		READ_FIELD(reader, tagName, mix, "sineShaperMix");
		READ_ZONE(reader, tagName, harmonic.value, "sineShaperHarmonicBase");
		READ_ZONE(reader, tagName, twist.value, "sineShaperTwistBase");
		READ_FLOAT(reader, tagName, twistPhaseOffset, "sineShaperMetaPhase", 10.0f);
		READ_FLOAT(reader, tagName, harmonicPhaseOffset, "sineShaperMetaPhaseH", 10.0f);
		READ_FLOAT(reader, tagName, gammaPhase, "sineShaperGamma", 10.0f);
		return false;
	}
};

/// Per-voice state for sine shaper DSP (must be separate from per-sound params)
/// Used in Voice (per-voice) and GlobalEffectableForClip (per-clip)
struct SineShaperVoiceState {
	// DC blocker state (removes DC from asymmetry)
	q31_t dcBlockerL{0};
	q31_t dcBlockerR{0};
	// Feedback recirculation state (Twist Zone 3)
	q31_t feedbackL{0};
	q31_t feedbackR{0};
	// Zone 0 (Width) stereo LFO phase accumulator (0.0 to 1.0, wraps)
	float stereoLfoPhase{0.0f};
	// Per-sample drive gain smoothing (persists across buffers for continuity)
	// Negative sentinel means uninitialized (will snap to target on first use)
	float smoothedDriveGain{-1.0f};
};

/// Output HPF coefficient for ~100Hz cutoff at 44.1kHz
/// alpha = 2π * fc / fs = 2π * 100 / 44100 ≈ 0.01425
/// Replaces 5Hz DC blocker - removes sub-bass rumble from waveshaping
/// Feedback taps post-HPF so inherits the filtering
constexpr q31_t kOutputHpfAlpha = static_cast<q31_t>(0.01425 * ONE_Q31);

/// Neutral filterGain value for subtractive mode gain compensation
/// Matches kShaperNeutralFilterGainInt from shaper_buffer.h
/// At this level: boostGain = 1.0 (no adjustment needed)
/// High resonance (low filterGain) → boost; low resonance (high filterGain) → attenuate
constexpr int32_t kSineShaperNeutralFilterGain = 1 << 28;

// Zone 1 "357" Chebyshev Harmonic Extraction
// See docs/dev/sine_shaper_chebyshev.md for detailed design rationale

/**
 * Generic 4-weight blend for triangle-phased parameter morphing
 *
 * Used by both Zone 1/2 (Chebyshev harmonics) and Zone 3 (FM modes).
 * Weights are computed using log-scaled triangles with irrational frequencies
 * to create smooth, non-periodic transitions through parameter space.
 */
struct BlendWeights4 {
	float w0; // First mode weight (Zone 1/2: T3, Zone 3: Add)
	float w1; // Second mode weight (Zone 1/2: T5, Zone 3: Ring)
	float w2; // Third mode weight (Zone 1/2: T7, Zone 3: FM)
	float w3; // Fourth mode weight (Zone 1/2: T9, Zone 3: Fold)
};

/// Blend weight triangle configs - irrational frequency ratios avoid periodicity
/// Uses 4 separate unipolar triangles with 80% duty for consistent audibility.
/// w2/w3 use same freq but offset to drift in and out of phase.
constexpr std::array<phi::PhiTriConfig, 4> kBlendWeightBank = {{
    {2.019f, 0.8f, 0.00f, false}, // w0: √29/2 * 0.75 (~2.0 cycles/zone), 80% duty
    {2.356f, 0.8f, 0.94f, false}, // w1: π * 0.75 (~2.4 cycles/zone)
    {2.771f, 0.8f, 0.05f, false}, // w2: e²/2 * 0.75, 80% duty for audibility
    {2.771f, 0.8f, 0.55f, false}, // w3: same freq, 0.5 offset for phase variation
}};

/**
 * Compute 4 normalized blend weights using phi triangle bank
 *
 * Uses log-scaled triangles with irrational frequency ratios to create
 * smooth, non-periodic transitions. All 4 weights use separate unipolar
 * triangles with overlapping phases to ensure smooth crossfades.
 *
 * @param posInZone Position 0.0 to 1.0 within the zone
 * @param gammaPhase Raw phase offset (double for precision with large gamma values)
 * @return Normalized weights (sum to 1.0)
 */
inline BlendWeights4 computeBlendWeights4(float posInZone, double gammaPhase = 0.0) {
	posInZone = std::clamp(posInZone, 0.0f, 1.0f);

	constexpr float kMinWeight = 0.01f; // -40dB floor (prevents div by zero)

	// Convert linear triangle (0-1) to log-scaled weight (0.01-1.0, ~40dB range)
	auto linearToLog = [](float linear) {
		if (linear <= 0.0f) {
			return kMinWeight;
		}
		float x3 = linear * linear * linear;
		return kMinWeight + x3 * (1.0f - kMinWeight); // 0.01 + x³ * 0.99
	};

	// Evaluate all 4 triangles via evalTriangleBank
	// Combined phase = posInZone + gammaPhase; freqMult = 1.0 (no modulation for blend weights)
	auto triValues = phi::evalTriangleBank(static_cast<double>(posInZone) + gammaPhase, 1.0f, kBlendWeightBank);

	// Post-processing: log scaling for all unipolar triangles
	float w0 = linearToLog(triValues[0]);
	float w1 = linearToLog(triValues[1]);
	float w2 = linearToLog(triValues[2]);
	float w3 = linearToLog(triValues[3]);

	// Normalize weights to sum to 1.0
	float wSum = w0 + w1 + w2 + w3;
	return {w0 / wSum, w1 / wSum, w2 / wSum, w3 / wSum};
}

/**
 * Precomputed blended polynomial coefficients for Zone 1/2 "3579"
 * Computed once per buffer using Horner's method for efficient per-sample evaluation
 *
 * The blended polynomial is: P(x) = c1*x + c3*x³ + c5*x⁵ + c7*x⁷ + c9*x⁹
 * Using Horner's method: P(x) = x * (c1 + x² * (c3 + x² * (c5 + x² * (c7 + c9*x²))))
 *
 * This reduces Zone 1/2 from ~94 to ~30 cycles/sample (comparable to TanH+ADAA)
 *
 * Zone 1 "3579": Raw input, unbounded output (edgy, integer overflow wraps)
 * Zone 2 "3579wm": Sine-preprocessed input, bounded output (warm, FM-like)
 *
 * Bipolar harmonic: positive uses w7 (7th), negative uses w9 (9th) - never both.
 * H9 contributes to c7 via its x⁷ term, so c7 is always computed.
 */
struct ShaperWeights {
	float c1; // Coefficient for x (cancels fundamental from higher-order terms)
	float c3; // Coefficient for x³
	float c5; // Coefficient for x⁵
	float c7; // Coefficient for x⁷ (from w7 or w9's H9 contribution)
	float c9; // Coefficient for x⁹ (only when w9 active, else 0)
};

/**
 * Helper to compute polynomial coefficients from normalized weights
 *
 * c1 must equal Σw to cancel fundamental from higher-order terms.
 * When x = sin(θ), each x^n term produces fundamental energy.
 * The normalized Chebyshev design ensures these cancel exactly when c1 = Σw.
 *
 * Normalized Chebyshev polynomials (extract sin(nθ)/n from sin(θ)):
 *   H3(x) = x - (4/3)x³
 *   H5(x) = x - 4x³ + 3.2x⁵
 *   H7(x) = x - 8x³ + 16x⁵ - (64/7)x⁷
 *   H9(x) = x - (40/3)x³ + 48x⁵ - 64x⁷ + (256/9)x⁹
 *
 * @param w3 Weight for 3rd harmonic
 * @param w5 Weight for 5th harmonic
 * @param w7 Weight for 7th harmonic
 * @param w9 Weight for 9th harmonic
 */
inline ShaperWeights weightsToCoeffs(float w3, float w5, float w7, float w9) {
	// Coefficients derived from weighted sum of normalized Chebyshev polynomials
	float c1 = w3 + w5 + w7 + w9;
	float c3 = -(w3 * (4.0f / 3.0f) + w5 * 4.0f + w7 * 8.0f + w9 * (40.0f / 3.0f));
	float c5 = w5 * 3.2f + w7 * 16.0f + w9 * 48.0f;
	float c7 = -w7 * (64.0f / 7.0f) - w9 * 64.0f;
	float c9 = w9 * (256.0f / 9.0f);
	return {c1, c3, c5, c7, c9};
}

/**
 * Compute Zone 1/2 blended polynomial coefficients from position in zone
 *
 * Uses triangle-phased weight blending with irrational frequencies
 * to create smooth, non-periodic transitions through harmonic space.
 *
 * @param posInZone Position 0.0 to 1.0 within the zone
 * @param phaseOffset Phase offset for frequency modulation (default 0.0)
 * @return Precomputed blended coefficients for Horner's method evaluation
 */
inline ShaperWeights computeShaperWeightsFromPos(float posInZone, double gammaPhase = 0.0) {
	posInZone = std::clamp(posInZone, 0.0f, 1.0f);
	BlendWeights4 weights = computeBlendWeights4(posInZone, gammaPhase);
	return weightsToCoeffs(weights.w0, weights.w1, weights.w2, weights.w3);
}

/**
 * Evaluate blended 3579 polynomial using Horner's method
 * P(x) = x * (c1 + x² * (c3 + x² * (c5 + x² * (c7 + c9*x²))))
 *
 * Shared by Zone 1 (raw) and Zone 2 (sine-preprocessed) to ensure
 * coefficient/algorithm changes apply to both zones automatically.
 *
 * When c9=0 (7th mode): inner term is just c7, no extra cost
 * When c7 comes only from w9: still need x⁷ term from H9's contribution
 *
 * @param x Preprocessed input (raw for Zone 1, sin(rawX * π/2) for Zone 2)
 * @param weights Precomputed polynomial coefficients
 * @return Polynomial result (may exceed [-1, 1] for Zone 1)
 */
[[gnu::always_inline]] inline float evaluate3579Polynomial(float x, const ShaperWeights& weights) {
	float x2 = x * x;
	// Horner's method: x * (c1 + x² * (c3 + x² * (c5 + x² * (c7 + c9*x²))))
	return x * (weights.c1 + x2 * (weights.c3 + x2 * (weights.c5 + x2 * (weights.c7 + weights.c9 * x2))));
}

/**
 * Derived values from Twist parameter for sine shaper
 * Computed once per buffer at call site
 *
 * Zone 0: Width - Stereo spread with animated phase evolution
 * Zone 1: Evens - Asymmetric compression for even harmonics
 * Zone 2: Rect - Blended rectifier (rect + rect2 with overlap)
 * Zone 3: Feedback - Output→input recirculation
 * Zone 4: Twist - Phase modulator for Harmonic zones (meta-control)
 */
struct SineShaperTwistParams {
	float stereoWidth{0.0f};          // Width: stereo spread envelope
	float stereoFreqMult{1.0f};       // Width: oscillation frequency multiplier
	float stereoPhaseOffset{0.0f};    // Width: continuous phase evolution
	float stereoLfoRate{0.0f};        // LFO rate modulation (phi triangle, 0-1 → 0.1-8Hz)
	float evenAmount{0.0f};           // Evens: positive compression amount
	float evenDryBlend{0.0f};         // Evens: negative dry blend amount
	float rectAmount{0.0f};           // Rect: rectifier blend
	float rect2Amount{0.0f};          // Rect: sine compression
	float feedbackAmount{0.0f};       // Feedback: depth (0.0 to 0.25)
	float phaseHarmonic{0.0f};        // Position offset for Harmonic (includes pos * 5.0f)
	float phaseHarmonicFreqMod{0.0f}; // Frequency modulation offset (just harmonicPhaseOffset + gamma)
};

/**
 * Derive all Twist-dependent parameters from smoothed Twist value
 * Zones 0-3: Individual effects, Zone 4+: Meta (all effects combined)
 * When phaseOffset > 0: Full phi-triangle evolution across ALL zones (like table shaper)
 * @param params Optional - provides per-patch phase offsets for meta zone
 */
SineShaperTwistParams computeSineShaperTwistParams(q31_t twist, const SineTableShaperParams* ssParams = nullptr);

/**
 * Compute drive gain from q31 drive parameter
 *
 * Drive follows shaper pattern with hybrid param (bipolar, additive modulation):
 * - Drive INT32_MIN = silence (gain = 0)
 * - Drive 0 (12 o'clock) = unity gain (gain = 1)
 * - Drive INT32_MAX = 4x overdrive (gain = 4)
 *
 * @param drive q31 drive parameter
 * @return Float gain multiplier (0.0 to 4.0)
 */
[[gnu::always_inline]] inline float computeDriveGain(q31_t drive) {
	float normalizedDrive = (static_cast<float>(drive) + 2147483648.0f) / 4294967296.0f; // 0 to 1
	return normalizedDrive * normalizedDrive * 4.0f;                                     // Square for volume curve
}

/**
 * Core sine shaping without wet/dry mix (returns wet signal only)
 *
 * Harmonic zone selects algorithm:
 * - Zones 0-1 (Chebyshev): Triangle-modulated blend of T3, T5, T7, T9
 * - Zones 2-6 (FM): Various FM synthesis modes
 * - Zone 7 (Poly): Cascaded polynomial waveshaping
 *
 * Post-gain compensation ensures peak output matches wavefolder.
 *
 * @param input Audio sample to process
 * @param driveGain Precomputed drive gain (from computeDriveGain, 0.0 to 4.0)
 * @param zone Harmonic zone index (0-7, precomputed from harmonic param)
 * @param zoneWeights Precomputed weights (hoisted from buffer loop)
 *                    All zones repurpose this struct (zone boundary glitches acceptable):
 *                      Zone 0/1: c1-c9 = Chebyshev polynomial coefficients
 *                      Zones 2-6: c1=inputGainMult, c3=w0, c5=w1, c7=w2, c9=w3
 *                      Zone 7: c1=cascadeBlend, c3=selfMulBlend
 * @param evenAmount Positive compression for even harmonics (Twist Zone 1, 0.0 to 1.0)
 * @param evenDryBlend Negative dry blend for even harmonics (Twist Zone 1, 0.0 to 1.0)
 * @param rectAmount Rectifier blend toward |result| (Twist Zone 2, 0.0 to 1.0)
 * @param rect2Amount Sine compression on positive (Twist Zone 2, 0.0 to 1.0)
 */
q31_t sineShapeCore(q31_t input, float driveGain, int32_t zone, const ShaperWeights* zoneWeights,
                    float evenAmount = 0.0f, float evenDryBlend = 0.0f, float rectAmount = 0.0f,
                    float rect2Amount = 0.0f);

/// Stereo output from sineShapeCoreStereo
struct StereoShaped {
	q31_t l;
	q31_t r;
};

/**
 * Stereo sine shaping - processes L/R together with shared zone branching
 *
 * This is more efficient than calling sineShapeCore twice because:
 * - Single zone branch instead of two
 * - Shared setup code (weight extraction, gain calculation)
 * - Potentially vectorized operations
 *
 * @param inputL Left channel input
 * @param inputR Right channel input
 * @param driveGainL Left channel drive gain
 * @param driveGainR Right channel drive gain
 * @param zone Harmonic zone index (0-7, precomputed from harmonic param)
 * @param zoneWeightsL Left channel weights
 * @param zoneWeightsR Right channel weights
 * @param evenAmount Positive compression for even harmonics
 * @param evenDryBlend Negative dry blend for even harmonics
 * @param rectAmount Rectifier blend
 * @param rect2Amount Sine compression on positive
 */
StereoShaped sineShapeCoreStereo(q31_t inputL, q31_t inputR, float driveGainL, float driveGainR, int32_t zone,
                                 const ShaperWeights* zoneWeightsL, const ShaperWeights* zoneWeightsR,
                                 float evenAmount = 0.0f, float evenDryBlend = 0.0f, float rectAmount = 0.0f,
                                 float rect2Amount = 0.0f);

/**
 * Process a mono buffer through the sine shaper with parameter smoothing
 *
 * @param buffer Audio buffer to process in place
 * @param drive Current target drive value
 * @param smoothedDrive Pointer to smoothed drive state (updated in place)
 * @param voiceState Pointer to per-voice state (DC blocker, feedback, etc.)
 * @param harmonic Raw harmonic value (smoothed internally)
 * @param mix Wet/dry blend - if 0, buffer is not modified (CPU optimization)
 * @param twist Twist parameters (evens, rect, feedback, phaseHarmonic)
 * @param params Pointer to SineTableShaperParams for coefficient smoothing
 * @param wasBypassed Pointer to bypass state flag (updated in place)
 * @param filterGain For subtractive mode: pass the filterGain from filter config.
 *                   For FM mode or subtractive without filters: pass 0.
 *                   When >0 with hasFilters, dynamically adjusts gain to normalize levels.
 * @param hasFilters For subtractive mode: true if filters are active
 */
void sineShapeBuffer(std::span<q31_t> buffer, q31_t drive, q31_t* smoothedDrive, SineShaperVoiceState* voiceState,
                     q31_t harmonic, q31_t mix, const SineShaperTwistParams& twist, SineTableShaperParams* params,
                     bool* wasBypassed = nullptr, q31_t filterGain = 0, bool hasFilters = false);

/**
 * Process a stereo buffer through the sine shaper with parameter smoothing
 *
 * Drive and Harmonic are smoothed internally.
 * Twist should be smoothed at the call site for consistent handling.
 *
 * @param buffer Stereo audio buffer to process in place
 * @param drive Current target drive value
 * @param smoothedDrive Pointer to smoothed drive state (updated in place)
 * @param voiceState Pointer to per-voice state (DC blocker, feedback, LFO, etc.)
 * @param harmonic Raw harmonic value (smoothed internally via params->smoothedHarmonic)
 * @param mix Wet/dry blend - if 0, buffer is not modified (CPU optimization)
 * @param twist Twist parameters (stereo, evens, rect, feedback, phaseHarmonic)
 * @param params Pointer to SineTableShaperParams for coefficient smoothing (required)
 * @param wasBypassed Pointer to bypass state flag (updated in place)
 * @param filterGain For subtractive mode: pass the filterGain from filter config.
 *                   For FM mode or subtractive without filters: pass 0.
 *                   When >0 with hasFilters, dynamically adjusts gain to normalize levels.
 * @param hasFilters For subtractive mode: true if filters are active
 */
void sineShapeBuffer(std::span<StereoSample> buffer, q31_t drive, q31_t* smoothedDrive,
                     SineShaperVoiceState* voiceState, q31_t harmonic, q31_t mix, const SineShaperTwistParams& twist,
                     SineTableShaperParams* params, bool* wasBypassed = nullptr, q31_t filterGain = 0,
                     bool hasFilters = false);

// ============================================================================
// Encapsulated Processing API
// ============================================================================
// Simplifies callsites by hiding param combination, smoothing, and twist computation.

/**
 * Process sine shaper for voice path (patched params with mod matrix routing)
 *
 * Encapsulates: param combination, twist param computation, buffer processing.
 * Call when sineShaper.isEnabled() returns true.
 *
 * @param buffer Stereo audio buffer to process in-place
 * @param params Sine shaper params (modified: twist, smoothedDrive updated)
 * @param state Per-voice state (modified: DC blocker, feedback, LFO phase)
 * @param driveFinal Final drive from patcher (paramFinalValues[LOCAL_SINE_SHAPER_DRIVE])
 * @param harmonicPreset Harmonic preset from param set
 * @param harmonicCables Harmonic cables from patcher (paramFinalValues[LOCAL_SINE_SHAPER_HARMONIC])
 * @param twistPreset Twist preset from param set
 * @param twistCables Twist cables from patcher (paramFinalValues[LOCAL_SINE_SHAPER_TWIST])
 * @param filterGain For subtractive mode: pass the filterGain from filter config. For FM: pass 0.
 * @param hasFilters For subtractive mode: true if filters are active
 */
void processSineShaper(std::span<StereoSample> buffer, SineTableShaperParams* params, SineShaperVoiceState* state,
                       q31_t driveFinal, q31_t harmonicPreset, q31_t harmonicCables, q31_t twistPreset,
                       q31_t twistCables, q31_t filterGain = 0, bool hasFilters = false);

/**
 * Process sine shaper mono buffer for voice path
 */
void processSineShaper(std::span<q31_t> buffer, SineTableShaperParams* params, SineShaperVoiceState* state,
                       q31_t driveFinal, q31_t harmonicPreset, q31_t harmonicCables, q31_t twistPreset,
                       q31_t twistCables, q31_t filterGain = 0, bool hasFilters = false);

} // namespace deluge::dsp
