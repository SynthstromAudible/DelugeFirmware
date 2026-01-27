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

#include "deluge/dsp/shaper.h"
#include "deluge/util/fixedpoint.h"
#include "deluge/util/functions.h"
#include "dsp/stereo_sample.h"
#include "io/debug/fx_benchmark.h"
#include <climits>
#include <cmath>
#include <span>

namespace deluge::dsp {

/// Bitmask constants for selective extras control
/// Use as extrasMask parameter to enable/disable individual effects
constexpr uint8_t kExtrasSub = 1 << 0;        ///< bit 0: subharmonic gain modulation
constexpr uint8_t kExtrasFeedback = 1 << 1;   ///< bit 1: feedback comb filter
constexpr uint8_t kExtrasRotation = 1 << 2;   ///< bit 2: bit rotation (aliasing)
constexpr uint8_t kExtrasLpf = 1 << 3;        ///< bit 3: lowpass filter (unipolar slewIntensity)
constexpr uint8_t kExtrasIntegrator = 1 << 4; ///< bit 4: ZC-reset integrator (unipolar slewIntensity)
constexpr uint8_t kExtrasAll = 0x1F;          ///< all extras enabled

/// Benchmark tag strings for each extrasMask value (0-31)
/// Used with FX_BENCH_SET_TAG to track performance by extras configuration
constexpr const char* kExtrasTagStrings[32] = {
    "ext_0",  "ext_1",  "ext_2",  "ext_3",  "ext_4",  "ext_5",  "ext_6",  "ext_7",  "ext_8",  "ext_9",  "ext_10",
    "ext_11", "ext_12", "ext_13", "ext_14", "ext_15", "ext_16", "ext_17", "ext_18", "ext_19", "ext_20", "ext_21",
    "ext_22", "ext_23", "ext_24", "ext_25", "ext_26", "ext_27", "ext_28", "ext_29", "ext_30", "ext_31",
};

/// Per-sample IIR alpha for q31 parameter smoothing (~40ms time constant at 44.1kHz)
constexpr q31_t kShaperSmoothingAlpha = static_cast<q31_t>(0.0005 * ONE_Q31);

/// Rotate right by n bits (ARM optimizes to single-cycle ROR instruction)
/// Creates aliasing artifacts by moving bits in the sample word.
/// @param value Sample to rotate
/// @param n Rotation amount [0,31] (0 = passthrough)
/// @return Rotated value
[[gnu::always_inline]] inline int32_t rotateRight(int32_t value, int8_t n) {
	if (n == 0) [[likely]] {
		return value;
	}
	// GCC/Clang optimize this idiom to ARM ROR instruction
	uint32_t uval = static_cast<uint32_t>(value);
	return static_cast<int32_t>((uval >> n) | (uval << (32 - n)));
}

/// Subtractive gain staging analysis (from voice.cpp):
/// - FM: sourceAmplitude at full level → signal at ~23M peak
/// - Subtractive: oscillators scaled by >> 4 OR filterGain (both ~16x attenuation)
///
/// The shaper table is designed to handle varying input levels via the drive knob.
/// Subtractive signals use a smaller portion of the table at neutral drive.
/// FilterGain compensation only adjusts for resonance-induced level changes.
constexpr int32_t kShaperNeutralFilterGainInt = 1 << 28; // filterGain at neutral settings (integer)

/// Tolerance band for skipping gain adjustment (~1% of neutral)
/// When filterGain is within this range of neutral, gain adjust is skipped (inaudible difference)
constexpr int32_t kGainAdjustTolerance = kShaperNeutralFilterGainInt / 100;

/// Context for per-sample IIR parameter smoothing during buffer processing
struct ShaperSmoothingContext {
	q31_t current;
	q31_t alpha;
	q31_t target;
};

/// Prepare parameter smoothing for per-sample IIR processing
[[gnu::always_inline]] inline ShaperSmoothingContext prepareShaperSmoothing(q31_t state, q31_t target) {
	return {state, kShaperSmoothingAlpha, target};
}

/// Q16 smoothing alpha (~40ms time constant at 44.1kHz, matches q31 version)
/// 0.0005 * 65536 ≈ 33
constexpr int32_t kShaperSmoothingAlphaQ16 = 33;

/// Context for per-sample Q16 parameter smoothing (used for mixNorm)
struct ShaperSmoothingContextQ16 {
	int32_t current;
	int32_t alpha;
	int32_t target;
};

/// Prepare Q16 parameter smoothing for per-sample IIR processing
[[gnu::always_inline]] inline ShaperSmoothingContextQ16 prepareShaperSmoothingQ16(int32_t state, int32_t target) {
	return {state, kShaperSmoothingAlphaQ16, target};
}

/// Per-channel modulation state pointers (mono: 1 instance, stereo: L+R instances)
/// Groups all state that needs to persist between buffer calls
struct ShaperModState {
	int32_t* prevSample;      ///< Previous sample for zero-crossing detection (subharmonic)
	int32_t* slewed;          ///< Slew rate limiter state (previous output)
	int32_t* prevScaledInput; ///< Hysteresis state for slope detection
	uint8_t* zcCount;         ///< Zero-crossing counter (for subharmonic)
	int8_t* subSign;          ///< Subharmonic sign (±1, toggles every 2nd ZC)
};

/// Per-buffer computed values hoisted out of sample loop
/// Computed once at buffer start, passed to per-sample processing
struct ShaperBufferContext {
	// Blend/table parameters
	int32_t blendSlope_Q8;   ///< Pre-computed blend slope
	int32_t threshold32;     ///< Pre-computed amplitude threshold (32-bit, shifted by kThresholdShift)
	int8_t tableIdx;         ///< Target table index
	int32_t hystOffset;      ///< Hysteresis offset for slope detection
	int32_t inputScaleShift; ///< Bit shift for scaling input to table domain

	// Modulator intensities (from phi triangles, gated by extrasEnabled)
	int32_t subBoost_Q16;        ///< Subharmonic boost amount (0 = disabled)
	int8_t subRatio;             ///< Subharmonic ZC threshold: 2=octave, 3=twelfth, 4=2oct, etc.
	int32_t stride;              ///< ZC detection stride [1,128]: check every N samples
	int32_t feedback_Q16;        ///< Feedback intensity for comb filter (0 = disabled)
	int8_t rotation;             ///< Bit rotation amount [0,31] (0 = passthrough)
	int32_t lpfAlpha_Q16;        ///< Lowpass filter alpha (0 = bypass, when slewIntensity > 0)
	int32_t integratorBlend_Q16; ///< Integrator blend amount (0 = bypass, when slewIntensity < 0)

	// Gain staging
	int32_t attenGain_Q30; ///< Output attenuation in Q30 (subtractive mode, uses SMMUL)

	// Flags
	bool isLinear;         ///< True if shaper is in linear bypass
	bool lpfActive;        ///< True if lowpass filter enabled (kExtrasLpf bit set)
	bool integratorActive; ///< True if ZC-reset integrator enabled (kExtrasIntegrator bit set)
	bool needsGainAdjust;  ///< True if subtractive gain compensation needed
	uint8_t
	    extrasMask; ///< Bitmask for extras (kExtrasSub|kExtrasFeedback|kExtrasRotation|kExtrasLpf|kExtrasIntegrator)
};

/// Subharmonic gain modulation: maximum cut/boost amount at full intensity (25% decreased from 26214)
/// 19660 Q16 = ~30% (0.7x when subSign=+1, 1.3x when subSign=-1)
constexpr int32_t kSubBoostMax_Q16 = 19660;

/// Lowpass filter for transient softening (replaces slew rate limiting)
/// One-pole IIR: y[n] = y[n-1] + alpha * (x[n] - y[n-1])
/// Cutoff is note-relative: min = 1 octave above root, max = 2 octaves above
/// alpha ≈ 2π * fc / fs, at 44.1kHz: alpha_Q16 ≈ fc * 9.33
constexpr float kLpfOctaveMin = 1.0f; // Min cutoff = 2^1 = 2x note freq (one octave above)
constexpr float kLpfOctaveMax = 2.0f; // Max cutoff = 2^2 = 4x note freq (two octaves above)
constexpr int32_t kLpfAlphaScale = 9; // 2π * 65536 / 44100 ≈ 9.33
constexpr float kLpfRefFreq = 110.0f; // Reference for audio tracks (A2, gives 220-440Hz range)

/// Compute lowpass alpha from intensity and note frequency (Q16 format)
/// intensity 0 → bypass, intensity > 0 → cutoff sweeps from 2 octaves down to 1 octave above note
/// At A4 (440Hz): min=880Hz, max=1760Hz. At A2 (110Hz): min=220Hz, max=440Hz
[[gnu::always_inline]] inline int32_t computeLpfAlpha_Q16(int32_t intensity_Q16, float noteFreqHz = kLpfRefFreq) {
	// Interpolate octave offset: intensity=0 → 2 octaves, intensity=max → 1 octave above note
	float octaveRange = kLpfOctaveMax - kLpfOctaveMin;
	float octaveOffset = kLpfOctaveMax - (octaveRange * static_cast<float>(intensity_Q16) / 65536.0f);
	// Cutoff = note * 2^octaveOffset (e.g., A4=440 → 880-1760Hz range)
	float cutoff = noteFreqHz * exp2f(octaveOffset);
	// alpha = 2π * cutoff / fs, in Q16
	return static_cast<int32_t>(cutoff) * kLpfAlphaScale;
}

/// Per-sample shaper processing - shared by mono and stereo versions
/// Operates entirely in scaled domain: scale once, apply LPF/sub, process, unscale
/// @param input Raw input sample
/// @param driveGain_Q26 Smoothed drive gain (includes boost if subtractive)
/// @param ctx Pre-computed buffer context (hoisted values)
/// @param slewed Pointer to slew state in SCALED domain (updated)
/// @param prevScaledInput Pointer to hysteresis state (updated)
/// @param subSign Per-sample subharmonic sign (±1)
/// @param shaper Reference to shaper instance
/// @param scaledFeedback Scaled feedback sample to add (wet path only, 0 = none)
/// @return Processed sample (before output attenuation)
[[gnu::always_inline]] inline q31_t processShaperSample(q31_t input, int32_t driveGain_Q26,
                                                        const ShaperBufferContext& ctx, int32_t* slewed,
                                                        int32_t* prevScaledInput, int8_t subSign, TableShaper& shaper,
                                                        int32_t scaledFeedback = 0) {
	// Apply drive (boost folded into driveGain_Q26)
	q31_t drivenInput = lshiftAndSaturate<6>(multiply_32x32_rshift32(input, driveGain_Q26));

	// Scale once to table domain - all processing happens in scaled space
	int32_t scaledDry = shaper.scaleInput(drivenInput);
	int32_t scaledWet = scaledDry;

	// 1. Feedback comb: add delayed sample (wet path only, computed at stride points)
	if (scaledFeedback != 0) {
		scaledWet = add_saturate(scaledWet, scaledFeedback);
	}

	// 2. Bit rotation: create aliasing artifacts (wet path only)
	// Applied after feedback so rotation affects the comb-filtered signal
	if (ctx.rotation != 0) {
		scaledWet = rotateRight(scaledWet, ctx.rotation);
	}

	// 3a. ZC-reset integrator: triangle-ish waveshaping (negative slewIntensity)
	// Accumulates signal between zero crossings, creating smooth arcs
	// Reset happens in main loop at ZC detection points
	if (ctx.integratorActive) {
		*slewed = add_saturate(*slewed, scaledWet >> 6); // Accumulate with headroom
		// Blend: scaledWet + (integrated - scaledWet) * blend
		int64_t diff = static_cast<int64_t>(*slewed) - scaledWet;
		scaledWet += static_cast<int32_t>((diff * ctx.integratorBlend_Q16) >> 16);
	}
	// 3b. Lowpass filter: soften transients (positive slewIntensity)
	else if (ctx.lpfActive) {
		int64_t diff = static_cast<int64_t>(scaledDry) - *slewed;
		*slewed += static_cast<int32_t>((diff * ctx.lpfAlpha_Q16) >> 16);
		scaledWet = *slewed;
	}

	// 4. Subharmonic gain modulation (in scaled domain)
	// Optimization: scaledWet * (1 - subSign*boost) = scaledWet - subSign*(scaledWet*boost)
	// Uses SMMUL (single-cycle) instead of 64-bit multiply
	if (ctx.subBoost_Q16 != 0) {
		// subBoost_Q16 << 16 → Q32 format for multiply_32x32_rshift32 (max 19660<<16 = 1.29B, fits int32)
		int32_t adjustment = multiply_32x32_rshift32(scaledWet, ctx.subBoost_Q16 << 16);
		scaledWet = scaledWet - subSign * adjustment;
	}

	// Process pre-scaled inputs, returns scaled output
	int32_t scaledOut = shaper.processPreScaled32(scaledWet, scaledDry, ctx.blendSlope_Q8, ctx.threshold32,
	                                              ctx.tableIdx, ctx.hystOffset, prevScaledInput);

	// Unscale output back to signal domain
	return scaledOut >> ctx.inputScaleShift;
}

/**
 * Process a mono buffer through the TableShaper using integer-only path
 *
 * Table operates at FM signal levels. For subtractive synths, pass filterGain to
 * dynamically compute the boost needed to match FM operating levels.
 *
 * @param buffer Audio buffer to process in-place
 * @param shaper The Shaper instance (with pre-generated table)
 * @param drive Patched drive parameter (q31)
 * @param smoothedDriveGain Previous driveGain_Q26 value for smoothing (updated)
 * @param mix Wet/dry blend (q31, 0 = bypass)
 * @param smoothedThreshold32 Previous threshold32 for direct coefficient smoothing (updated)
 * @param smoothedBlendSlope_Q8 Previous blendSlope_Q8 for direct coefficient smoothing (updated)
 * @param filterGain For subtractive mode: filterGain from filter config (0 = FM mode)
 * @param hasFilters For subtractive mode: true if filters are active
 * @param state Per-channel modulation state (sub, slew, hysteresis)
 * @param extrasMask Bitmask for extras: kExtrasSub|kExtrasFeedback|kExtrasRotation|kExtrasLpf|kExtrasIntegrator
 * @param gammaPhase Secret knob phase offset for phi triangles (affects hysteresis evolution)
 * @param noteFreqHz Note frequency in Hz for LPF cutoff scaling (default 440Hz = A4)
 */
inline void shapeBufferInt32(std::span<q31_t> buffer, TableShaper& shaper, q31_t drive, q31_t* smoothedDriveGain,
                             q31_t mix, int32_t* smoothedThreshold32, int32_t* smoothedBlendSlope_Q8, q31_t filterGain,
                             bool hasFilters, ShaperModState& state, uint8_t extrasMask, float gammaPhase,
                             float noteFreqHz = kLpfRefFreq) {
	if (buffer.empty()) {
		return;
	}

	FX_BENCH_DECLARE(bench, "shaper_table");
	FX_BENCH_SET_TAG(bench, 0, kExtrasTagStrings[extrasMask & 0x1F]);
	FX_BENCH_SCOPE(bench);

	// Compute gain adjustment for subtractive mode (fixed-point, computed once per buffer)
	// filterGain=0 means FM mode (no adjustment needed)
	// filterGain>0 means subtractive: compensate for resonance-induced level changes
	// At neutral filterGain (2^28), gains = 1.0 (no adjustment)
	// High resonance (low filterGain) → boost input, attenuate output
	// Low resonance (high filterGain) → no adjustment (signal already quiet, table handles it)
	// Q30 format can only represent values <= 1.0, so we can't boost output (ratio < 1)
	int32_t filterDelta = filterGain - kShaperNeutralFilterGainInt;
	// Only boost input when filterGain < neutral (high resonance)
	bool needsGainAdjust = (filterGain > 0) && hasFilters && (filterDelta < -kGainAdjustTolerance);
	int32_t attenGain_Q30 = 1 << 30; // 1.0 in Q30

	// Compute target driveGain ONCE (hoisted p^5 calculation)
	// Fold boost into drive target to save one multiply per sample
	int32_t targetGain_Q26 = TableShaper::driveToGainQ26(drive);
	if (needsGainAdjust) {
		// One float divide per buffer for attenuation (Q30 for single-cycle SMMUL)
		// ratio > 1.0 here (filterGain < neutral), so 1/ratio < 1.0, fits in Q30
		float ratio = static_cast<float>(kShaperNeutralFilterGainInt) / static_cast<float>(filterGain);
		attenGain_Q30 = static_cast<int32_t>((1.0f / ratio) * 1073741824.0f); // 2^30
		// Fold boost into drive: (boost_Q16 × drive_Q26) >> 16 → Q26
		// Uses 64-bit intermediate to handle large boost × drive products
		int64_t boosted64 = static_cast<int64_t>(ratio * 65536.0f) * targetGain_Q26;
		targetGain_Q26 = static_cast<int32_t>(std::min(boosted64 >> 16, static_cast<int64_t>(INT32_MAX)));
	}
	// Smooth driveGain_Q26 (includes boost if subtractive) - stored value is Q26 gain
	auto gainCtx = prepareShaperSmoothing(*smoothedDriveGain, targetGain_Q26);

	// Convert mix param to Q16 normalized value (needed to derive target coefficients)
	int32_t targetMixNorm_Q16 = TableShaper::mixParamToNormQ16(mix);

	// Target coefficients (computed once per buffer from target mix)
	int32_t targetThreshold32 = TableShaper::computeThreshold32(targetMixNorm_Q16);
	int32_t targetBlendSlope = shaper.computeBlendSlope_Q8(TableShaperCore::computeBaseSlope(targetMixNorm_Q16));

	// Hoist atomic loads once per buffer (removes memory barriers from per-sample loop)
	bool isLinear = shaper.getIsLinear();
	int8_t tableIdx = shaper.getTargetTableIndex();

	// For non-fast path: derive 32-bit threshold and blendSlope from target mix
	// (per-buffer computation, ~3ms buffer vs ~40ms smoothing = negligible error)
	int32_t blendSlope_Q8 = targetBlendSlope;
	int32_t threshold32_ctx = TableShaper::computeThreshold32(targetMixNorm_Q16);

	// Hoist hysteresis offset - skip when intensity is 0 (phi triangle at zero)
	int32_t hystOffset = 0;
	int32_t* hystState = nullptr;
	if (state.prevScaledInput && gammaPhase != 0.0f) {
		hystOffset = shaper.getHystOffset();
		if (hystOffset != 0) {
			hystState = state.prevScaledInput; // Only track slope when offset active
		}
	}

	// Hoist extras parameters based on bitmask (each effect independently gated)
	int32_t subBoost_Q16 = 0;
	int8_t subRatio = 2;      // Default octave-down
	int32_t stride = 64;      // Default buffer midpoint
	int32_t feedback_Q16 = 0; // Default no feedback
	int8_t rotation = 0;      // Default no rotation
	bool lpfActive = false;
	bool integratorActive = false;
	int32_t lpfAlpha_Q16 = 0;
	int32_t integratorBlend_Q16 = 0;

	// Extras processing: extrasMask controls which effects are enabled
	if (extrasMask != 0) {
		// Stride is shared by sub and feedback - hoist if either is enabled
		if ((extrasMask & (kExtrasSub | kExtrasFeedback)) != 0) {
			stride = shaper.getStride();
		}
		// Subharmonic: needs ZC state pointers
		if ((extrasMask & kExtrasSub) != 0 && state.zcCount && state.subSign) {
			int32_t subIntensity_Q16 = shaper.getSubIntensity_Q16();
			subBoost_Q16 = static_cast<int32_t>((static_cast<int64_t>(subIntensity_Q16) * kSubBoostMax_Q16) >> 16);
			subRatio = shaper.getSubRatio();
		}
		// Feedback: needs stride (already hoisted above)
		if ((extrasMask & kExtrasFeedback) != 0) {
			feedback_Q16 = shaper.getFeedback_Q16();
		}
		// Rotation: independent
		if ((extrasMask & kExtrasRotation) != 0) {
			rotation = shaper.getRotation();
		}
		// LPF and Integrator: separate bits, both use unipolar slewIntensity
		// Integrator takes precedence if both enabled (mutually exclusive in practice)
		if (state.slewed != nullptr) {
			int32_t slewIntensity_Q16 = shaper.getSlewIntensity_Q16();
			if ((extrasMask & kExtrasIntegrator) != 0 && slewIntensity_Q16 > 0) {
				integratorActive = true;
				integratorBlend_Q16 = slewIntensity_Q16;
			}
			else if ((extrasMask & kExtrasLpf) != 0 && slewIntensity_Q16 > 0) {
				lpfActive = true;
				lpfAlpha_Q16 = computeLpfAlpha_Q16(slewIntensity_Q16, noteFreqHz);
			}
		}
	}

	// Force LPF for vanilla square waves: at gammaPhase==0, slewIntensity > 0 means square wave
	// (XYToParams sets slewIntensity for oscHarmonicWeight >= 0.8 even at gammaPhase==0)
	// This auto-enables LPF for square waves in vanilla mode without requiring extrasMask config
	if (!lpfActive && !integratorActive && state.slewed != nullptr && gammaPhase == 0.0f) {
		int32_t slewIntensity_Q16 = shaper.getSlewIntensity_Q16();
		if (slewIntensity_Q16 > 0) {
			lpfActive = true;
			lpfAlpha_Q16 = computeLpfAlpha_Q16(slewIntensity_Q16, noteFreqHz);
		}
	}

	// Build per-buffer context (hoisted values for per-sample helper)
	// Hoist input scale shift for unscaling in processShaperSample
	int32_t inputScaleShift = shaper.getInputScaleShift();

	ShaperBufferContext ctx{
	    .blendSlope_Q8 = blendSlope_Q8,
	    .threshold32 = threshold32_ctx,
	    .tableIdx = tableIdx,
	    .hystOffset = hystOffset,
	    .inputScaleShift = inputScaleShift,
	    .subBoost_Q16 = subBoost_Q16,
	    .subRatio = subRatio,
	    .stride = stride,
	    .feedback_Q16 = feedback_Q16,
	    .rotation = rotation,
	    .lpfAlpha_Q16 = lpfAlpha_Q16,
	    .integratorBlend_Q16 = integratorBlend_Q16,
	    .attenGain_Q30 = attenGain_Q30,
	    .isLinear = isLinear,
	    .lpfActive = lpfActive,
	    .integratorActive = integratorActive,
	    .needsGainAdjust = needsGainAdjust,
	    .extrasMask = extrasMask,
	};

	// Fast path: linear bypass (X=0 or table not ready)
	if (ctx.isLinear) {
		for (auto& sample : buffer) {
			gainCtx.current += multiply_32x32_rshift32(gainCtx.target - gainCtx.current, gainCtx.alpha) * 2;
			// Apply drive only (consistent with shaped path)
			sample = multiply_32x32_rshift32(sample, gainCtx.current) << 6;
		}
		*smoothedDriveGain = gainCtx.current;
		// Set coefficients to target (linear path doesn't use them, so no smoothing needed)
		*smoothedThreshold32 = targetThreshold32;
		*smoothedBlendSlope_Q8 = targetBlendSlope;
		return;
	}

	// Fast path: vanilla mode (no extras, no forced LPF)
	// Uses processWithGainFast which skips scale/unscale entirely
	// Linear interpolation over buffer for threshold32/blendSlope (IIR doesn't work for small values)
	// Drive uses IIR (large Q31 range works with multiply_32x32_rshift32)
	if (extrasMask == 0 && !lpfActive) {
		int32_t currentThreshold32 = *smoothedThreshold32;
		int32_t currentBlendSlope = *smoothedBlendSlope_Q8;

		// Snap to target on first use (when at default "dry" values) to avoid starting silent
		// Default threshold32 = kInt32MaxShifted (full dry), default blendSlope = 0 (no blend)
		bool isFirstUse = (currentThreshold32 == TableShaperCore::kInt32MaxShifted && currentBlendSlope == 0
		                   && targetMixNorm_Q16 > 0);
		if (isFirstUse) {
			currentThreshold32 = targetThreshold32;
			currentBlendSlope = targetBlendSlope;
		}

		// Close half the distance per buffer (IIR with alpha=0.5, computed once per buffer)
		currentThreshold32 = (currentThreshold32 + targetThreshold32) >> 1;
		currentBlendSlope = (currentBlendSlope + targetBlendSlope) >> 1;

		for (auto& sample : buffer) {
			// Per-sample IIR for drive (smoother sonically, no perf penalty - memory-bound)
			gainCtx.current += multiply_32x32_rshift32(gainCtx.target - gainCtx.current, gainCtx.alpha) * 2;

			q31_t driven = lshiftAndSaturate<6>(multiply_32x32_rshift32(sample, gainCtx.current));
			q31_t out = shaper.processWithGainFast(driven, currentBlendSlope, currentThreshold32, tableIdx);

			if (ctx.needsGainAdjust) {
				out = multiply_32x32_rshift32(out, ctx.attenGain_Q30) << 2;
			}

			sample = out;
		}
		*smoothedDriveGain = gainCtx.current;
		// Store current values (closes half distance per buffer, converges exponentially)
		*smoothedThreshold32 = currentThreshold32;
		*smoothedBlendSlope_Q8 = currentBlendSlope;
		return;
	}

	// ========================================================================
	// Path summary:
	//   1. Linear bypass (isLinear): drive only, ~minimal cycles
	//   2. Vanilla fast path (extrasMask==0 && !lpfActive): shaper only, ~1830 cycles
	//   3. Full slow path (below): extras + hysteresis, ~2400+ cycles
	// ========================================================================

	// Slow path: full processing with sub/lpf extras
	// Per-sample linear interpolation for threshold32/blendSlope (IIR doesn't work for small values)
	int32_t currentThreshold32 = *smoothedThreshold32;
	int32_t currentBlendSlope = *smoothedBlendSlope_Q8;

	// Snap to target on first use (when at default "dry" values) to avoid starting silent
	bool isFirstUse =
	    (currentThreshold32 == TableShaperCore::kInt32MaxShifted && currentBlendSlope == 0 && targetMixNorm_Q16 > 0);
	if (isFirstUse) {
		currentThreshold32 = targetThreshold32;
		currentBlendSlope = targetBlendSlope;
	}

	// Close half the distance per buffer (IIR with alpha=0.5, computed once per buffer)
	currentThreshold32 = (currentThreshold32 + targetThreshold32) >> 1;
	currentBlendSlope = (currentBlendSlope + targetBlendSlope) >> 1;

	// Update ctx with smoothed values for this buffer
	ctx.threshold32 = currentThreshold32;
	ctx.blendSlope_Q8 = currentBlendSlope;

	// Split loops: simple path (hysteresis + optional rotation) vs full extras
	// Rotation is single-cycle ROR, no stride/state needed - can use simple path
	// Mask 0x1B = sub|feedback|lpf|integrator - these need the full path
	// Also check lpfActive: forced LPF for square waves needs full path for processShaperSample
	if ((ctx.extrasMask & 0x1B) == 0 && !ctx.lpfActive) {
		// Simple path: scale → optional rotation → processPreScaled32 → unscale
		// Handles: no extras (0), rotation-only (4), or both with hysteresis
		for (auto& sample : buffer) {
			gainCtx.current += multiply_32x32_rshift32(gainCtx.target - gainCtx.current, gainCtx.alpha) * 2;
			q31_t driven = lshiftAndSaturate<6>(multiply_32x32_rshift32(sample, gainCtx.current));
			int32_t scaledDry = shaper.scaleInput(driven);
			int32_t scaledWet = (ctx.rotation != 0) ? rotateRight(scaledDry, ctx.rotation) : scaledDry;
			int32_t scaledOut = shaper.processPreScaled32(scaledWet, scaledDry, ctx.blendSlope_Q8, ctx.threshold32,
			                                              ctx.tableIdx, ctx.hystOffset, hystState);
			q31_t out = scaledOut >> ctx.inputScaleShift;
			if (ctx.needsGainAdjust) {
				out = multiply_32x32_rshift32(out, ctx.attenGain_Q30) << 2;
			}
			sample = out;
		}
	}
	else {
		// Full extras path: stride loop + all extras processing
		bool needsStrideLoop = state.prevSample
		                       && ((ctx.subBoost_Q16 != 0 && state.zcCount && state.subSign) || ctx.feedback_Q16 > 0
		                           || ctx.integratorActive);
		int32_t strideCounter = 0;

		for (auto& sample : buffer) {
			gainCtx.current += multiply_32x32_rshift32(gainCtx.target - gainCtx.current, gainCtx.alpha) * 2;

			q31_t input = sample;
			int32_t scaledFeedback = 0;

			if (needsStrideLoop) {
				strideCounter++;
				if (strideCounter >= ctx.stride) {
					strideCounter = 0;
					int32_t prev = *state.prevSample;

					if (ctx.feedback_Q16 > 0) {
						q31_t drivenPrev = lshiftAndSaturate<6>(multiply_32x32_rshift32(prev, gainCtx.current));
						int32_t scaledPrev = shaper.scaleInput(drivenPrev);
						scaledFeedback =
						    static_cast<int32_t>((static_cast<int64_t>(scaledPrev) * ctx.feedback_Q16) >> 16);
					}

					*state.prevSample = input;

					bool zc = (input ^ prev) < 0;
					if (zc) {
						if (ctx.subBoost_Q16 != 0 && state.zcCount && state.subSign) {
							(*state.zcCount)++;
							if (*state.zcCount >= ctx.subRatio) {
								*state.subSign = -*state.subSign;
								*state.zcCount = 0;
							}
						}
						if (ctx.integratorActive && state.slewed) {
							*state.slewed = 0;
						}
					}
				}
			}

			int8_t currentSubSign = (state.subSign && ctx.subBoost_Q16 != 0) ? *state.subSign : 1;
			q31_t out = processShaperSample(input, gainCtx.current, ctx, state.slewed, hystState, currentSubSign,
			                                shaper, scaledFeedback);

			if (ctx.needsGainAdjust) {
				out = multiply_32x32_rshift32(out, ctx.attenGain_Q30) << 2;
			}

			sample = out;
		}
	}

	*smoothedDriveGain = gainCtx.current;
	// Store current values (closes half distance per buffer, converges exponentially)
	*smoothedThreshold32 = currentThreshold32;
	*smoothedBlendSlope_Q8 = currentBlendSlope;
}

/**
 * Process a stereo buffer through the TableShaper using integer-only path
 *
 * Table operates at FM signal levels. For subtractive synths, pass filterGain to
 * dynamically compute the boost needed to match FM operating levels.
 *
 * Optimizations:
 * - p^5 drive curve computed once per buffer, Q26 gain smoothed per-sample
 * - Filter gain adjustment uses Q16 fixed-point (one float divide per buffer)
 *
 * @param buffer Stereo audio buffer to process in-place
 * @param shaper The Shaper instance (with pre-generated table)
 * @param drive Patched drive parameter (q31)
 * @param smoothedDriveGain Previous driveGain_Q26 value for smoothing (updated, stores Q26 gain not raw drive)
 * @param mix Wet/dry blend (q31, 0 = bypass)
 * @param smoothedThreshold32 Previous threshold32 for direct coefficient smoothing (updated)
 * @param smoothedBlendSlope_Q8 Previous blendSlope_Q8 for direct coefficient smoothing (updated)
 * @param filterGain For subtractive mode: pass the filterGain from filter config.
 *                   For FM mode: pass 0 (no boost needed).
 * @param hasFilters For subtractive mode: true if filters are active
 * @param prevScaledInputL Pointer to previous scaled input for left channel hysteresis (updated, can be null)
 * @param prevScaledInputR Pointer to previous scaled input for right channel hysteresis (updated, can be null)
 * @param prevSampleL Pointer to previous L sample for zero-crossing detection (subharmonic)
 * @param prevSampleR Pointer to previous R sample for zero-crossing detection (subharmonic)
 * @param zcCountL Pointer to L channel zero-crossing counter (for subharmonic)
 * @param zcCountR Pointer to R channel zero-crossing counter (for subharmonic)
 * @param subSignL Pointer to L channel subharmonic sign (±1)
 * @param subSignR Pointer to R channel subharmonic sign (±1)
 * @param extrasMask Bitmask for extras: kExtrasSub|kExtrasFeedback|kExtrasRotation|kExtrasLpf|kExtrasIntegrator
 * @param gammaPhase Secret knob phase offset for phi triangles (affects hysteresis evolution)
 * @param slewedL Pointer to L channel slew rate limiter state (previous output)
 * @param slewedR Pointer to R channel slew rate limiter state (previous output)
 * @param noteFreqHz Note frequency in Hz for LPF cutoff scaling (default 440Hz = A4)
 */
inline void shapeBufferInt32(std::span<StereoSample> buffer, TableShaper& shaper, q31_t drive, q31_t* smoothedDriveGain,
                             q31_t mix, int32_t* smoothedThreshold32, int32_t* smoothedBlendSlope_Q8, q31_t filterGain,
                             bool hasFilters, int32_t* prevScaledInputL, int32_t* prevScaledInputR,
                             int32_t* prevSampleL, int32_t* prevSampleR, uint8_t* zcCountL, uint8_t* zcCountR,
                             int8_t* subSignL, int8_t* subSignR, uint8_t extrasMask, float gammaPhase, int32_t* slewedL,
                             int32_t* slewedR, float noteFreqHz = kLpfRefFreq) {
	if (buffer.empty()) {
		return;
	}

	FX_BENCH_DECLARE(bench, "shaper_table");
	FX_BENCH_SET_TAG(bench, 0, kExtrasTagStrings[extrasMask & 0x1F]);
	FX_BENCH_SCOPE(bench);

	// Compute gain adjustment for subtractive mode (fixed-point, computed once per buffer)
	// filterGain=0 means FM mode (no adjustment needed)
	// filterGain>0 means subtractive: compensate for resonance-induced level changes
	// At neutral filterGain (2^28), gains = 1.0 (no adjustment)
	// High resonance (low filterGain) → boost input, attenuate output
	// Low resonance (high filterGain) → no adjustment (signal already quiet, table handles it)
	// Q30 format can only represent values <= 1.0, so we can't boost output (ratio < 1)
	int32_t filterDelta = filterGain - kShaperNeutralFilterGainInt;
	// Only boost input when filterGain < neutral (high resonance)
	bool needsGainAdjust = (filterGain > 0) && hasFilters && (filterDelta < -kGainAdjustTolerance);
	int32_t attenGain_Q30 = 1 << 30; // 1.0 in Q30

	// Compute target driveGain ONCE (hoisted p^5 calculation)
	// Fold boost into drive target to save one multiply per sample
	int32_t targetGain_Q26 = TableShaper::driveToGainQ26(drive);
	if (needsGainAdjust) {
		// One float divide per buffer for attenuation (Q30 for single-cycle SMMUL)
		// ratio > 1.0 here (filterGain < neutral), so 1/ratio < 1.0, fits in Q30
		float ratio = static_cast<float>(kShaperNeutralFilterGainInt) / static_cast<float>(filterGain);
		attenGain_Q30 = static_cast<int32_t>((1.0f / ratio) * 1073741824.0f); // 2^30
		// Fold boost into drive: (boost_Q16 × drive_Q26) >> 16 → Q26
		// Uses 64-bit intermediate to handle large boost × drive products
		int64_t boosted64 = static_cast<int64_t>(ratio * 65536.0f) * targetGain_Q26;
		targetGain_Q26 = static_cast<int32_t>(std::min(boosted64 >> 16, static_cast<int64_t>(INT32_MAX)));
	}
	// Smooth driveGain_Q26 (includes boost if subtractive) - stored value is Q26 gain
	auto gainCtx = prepareShaperSmoothing(*smoothedDriveGain, targetGain_Q26);

	// Convert mix param to Q16 normalized value (needed to derive target coefficients)
	int32_t targetMixNorm_Q16 = TableShaper::mixParamToNormQ16(mix);

	// Target coefficients (computed once per buffer from target mix)
	int32_t targetThreshold32 = TableShaper::computeThreshold32(targetMixNorm_Q16);
	int32_t targetBlendSlope = shaper.computeBlendSlope_Q8(TableShaperCore::computeBaseSlope(targetMixNorm_Q16));

	// Hoist atomic loads once per buffer (removes memory barriers from per-sample loop)
	bool isLinear = shaper.getIsLinear();
	int8_t tableIdx = shaper.getTargetTableIndex();

	// For non-fast path: derive 32-bit threshold and blendSlope from target mix
	// (per-buffer computation, ~3ms buffer vs ~40ms smoothing = negligible error)
	int32_t blendSlope_Q8 = targetBlendSlope;
	int32_t threshold32_ctx = TableShaper::computeThreshold32(targetMixNorm_Q16);

	// Hoist hysteresis offset - skip when intensity is 0 (phi triangle at zero)
	int32_t hystOffset = 0;
	int32_t* hystStateL = nullptr;
	int32_t* hystStateR = nullptr;
	if (prevScaledInputL && prevScaledInputR && gammaPhase != 0.0f) {
		hystOffset = shaper.getHystOffset();
		if (hystOffset != 0) {
			hystStateL = prevScaledInputL; // Only track slope when offset active
			hystStateR = prevScaledInputR;
		}
	}

	// Hoist extras parameters based on bitmask (each effect independently gated)
	int32_t subBoost_Q16 = 0;
	int8_t subRatio = 2;      // Default octave-down
	int32_t stride = 64;      // Default buffer midpoint
	int32_t feedback_Q16 = 0; // Default no feedback
	int8_t rotation = 0;      // Default no rotation
	bool lpfActive = false;
	bool integratorActive = false;
	int32_t lpfAlpha_Q16 = 0;
	int32_t integratorBlend_Q16 = 0;

	// Extras processing: extrasMask controls which effects are enabled
	if (extrasMask != 0) {
		// Stride is shared by sub and feedback - hoist if either is enabled
		if ((extrasMask & (kExtrasSub | kExtrasFeedback)) != 0) {
			stride = shaper.getStride();
		}
		// Subharmonic: needs ZC state pointers
		if ((extrasMask & kExtrasSub) != 0 && zcCountL && zcCountR && subSignL && subSignR) {
			int32_t subIntensity_Q16 = shaper.getSubIntensity_Q16();
			subBoost_Q16 = static_cast<int32_t>((static_cast<int64_t>(subIntensity_Q16) * kSubBoostMax_Q16) >> 16);
			subRatio = shaper.getSubRatio();
		}
		// Feedback: needs stride (already hoisted above)
		if ((extrasMask & kExtrasFeedback) != 0) {
			feedback_Q16 = shaper.getFeedback_Q16();
		}
		// Rotation: independent
		if ((extrasMask & kExtrasRotation) != 0) {
			rotation = shaper.getRotation();
		}
		// LPF and Integrator: separate bits, both use unipolar slewIntensity
		// Integrator takes precedence if both enabled (mutually exclusive in practice)
		if (slewedL && slewedR) {
			int32_t slewIntensity_Q16 = shaper.getSlewIntensity_Q16();
			if ((extrasMask & kExtrasIntegrator) != 0 && slewIntensity_Q16 > 0) {
				integratorActive = true;
				integratorBlend_Q16 = slewIntensity_Q16;
			}
			else if ((extrasMask & kExtrasLpf) != 0 && slewIntensity_Q16 > 0) {
				lpfActive = true;
				lpfAlpha_Q16 = computeLpfAlpha_Q16(slewIntensity_Q16, noteFreqHz);
			}
		}
	}

	// Force LPF for vanilla square waves: at gammaPhase==0, slewIntensity > 0 means square wave
	// (XYToParams sets slewIntensity for oscHarmonicWeight >= 0.8 even at gammaPhase==0)
	// This auto-enables LPF for square waves in vanilla mode without requiring extrasMask config
	if (!lpfActive && !integratorActive && slewedL && slewedR && gammaPhase == 0.0f) {
		int32_t slewIntensity_Q16 = shaper.getSlewIntensity_Q16();
		if (slewIntensity_Q16 > 0) {
			lpfActive = true;
			lpfAlpha_Q16 = computeLpfAlpha_Q16(slewIntensity_Q16, noteFreqHz);
		}
	}

	// Build per-buffer context (hoisted values for per-sample helper)
	// Hoist input scale shift for unscaling in processShaperSample
	int32_t inputScaleShift = shaper.getInputScaleShift();

	ShaperBufferContext ctx{
	    .blendSlope_Q8 = blendSlope_Q8,
	    .threshold32 = threshold32_ctx,
	    .tableIdx = tableIdx,
	    .hystOffset = hystOffset,
	    .inputScaleShift = inputScaleShift,
	    .subBoost_Q16 = subBoost_Q16,
	    .subRatio = subRatio,
	    .stride = stride,
	    .feedback_Q16 = feedback_Q16,
	    .rotation = rotation,
	    .lpfAlpha_Q16 = lpfAlpha_Q16,
	    .integratorBlend_Q16 = integratorBlend_Q16,
	    .attenGain_Q30 = attenGain_Q30,
	    .isLinear = isLinear,
	    .lpfActive = lpfActive,
	    .integratorActive = integratorActive,
	    .needsGainAdjust = needsGainAdjust,
	    .extrasMask = extrasMask,
	};

	// Fast path: linear bypass (X=0 or table not ready)
	if (ctx.isLinear) {
		for (auto& sample : buffer) {
			gainCtx.current += multiply_32x32_rshift32(gainCtx.target - gainCtx.current, gainCtx.alpha) * 2;
			// Apply drive only (consistent with shaped path)
			sample.l = multiply_32x32_rshift32(sample.l, gainCtx.current) << 6;
			sample.r = multiply_32x32_rshift32(sample.r, gainCtx.current) << 6;
		}
		*smoothedDriveGain = gainCtx.current;
		// Set coefficients to target (linear path doesn't use them, so no smoothing needed)
		*smoothedThreshold32 = targetThreshold32;
		*smoothedBlendSlope_Q8 = targetBlendSlope;
		return;
	}

	// Fast path: vanilla mode (no extras, no forced LPF)
	// Linear interpolation over buffer for threshold32/blendSlope (IIR doesn't work for small values)
	// Drive uses IIR (large Q31 range works with multiply_32x32_rshift32)
	if (ctx.extrasMask == 0 && !lpfActive) {
		int32_t currentThreshold32 = *smoothedThreshold32;
		int32_t currentBlendSlope = *smoothedBlendSlope_Q8;

		// Snap to target on first use (when at default "dry" values) to avoid starting silent
		// Default threshold32 = kInt32MaxShifted (full dry), default blendSlope = 0 (no blend)
		bool isFirstUse = (currentThreshold32 == TableShaperCore::kInt32MaxShifted && currentBlendSlope == 0
		                   && targetMixNorm_Q16 > 0);
		if (isFirstUse) {
			currentThreshold32 = targetThreshold32;
			currentBlendSlope = targetBlendSlope;
		}

		// Close half the distance per buffer (IIR with alpha=0.5, computed once per buffer)
		currentThreshold32 = (currentThreshold32 + targetThreshold32) >> 1;
		currentBlendSlope = (currentBlendSlope + targetBlendSlope) >> 1;

		for (auto& sample : buffer) {
			// Per-sample IIR for drive (smoother sonically, no perf penalty - memory-bound)
			gainCtx.current += multiply_32x32_rshift32(gainCtx.target - gainCtx.current, gainCtx.alpha) * 2;

			// Apply drive
			q31_t drivenL = lshiftAndSaturate<6>(multiply_32x32_rshift32(sample.l, gainCtx.current));
			q31_t drivenR = lshiftAndSaturate<6>(multiply_32x32_rshift32(sample.r, gainCtx.current));

			// Fast shaper path (32-bit threshold, no wet/dry split, no extras)
			q31_t outL = shaper.processWithGainFast(drivenL, currentBlendSlope, currentThreshold32, tableIdx);
			q31_t outR = shaper.processWithGainFast(drivenR, currentBlendSlope, currentThreshold32, tableIdx);

			if (ctx.needsGainAdjust) {
				outL = multiply_32x32_rshift32(outL, ctx.attenGain_Q30) << 2;
				outR = multiply_32x32_rshift32(outR, ctx.attenGain_Q30) << 2;
			}

			sample.l = outL;
			sample.r = outR;
		}
		*smoothedDriveGain = gainCtx.current;
		// Store current values (closes half distance per buffer, converges exponentially)
		*smoothedThreshold32 = currentThreshold32;
		*smoothedBlendSlope_Q8 = currentBlendSlope;
		return;
	}

	// ========================================================================
	// Path summary:
	//   1. Linear bypass (isLinear): drive only, ~minimal cycles
	//   2. Vanilla fast path (extrasMask==0 && !lpfActive): shaper only, ~1830 cycles
	//   3. Full slow path (below): extras + hysteresis, ~2400+ cycles
	// ========================================================================

	// Slow path: full processing with sub/lpf extras
	// Per-sample linear interpolation for threshold32/blendSlope (IIR doesn't work for small values)
	int32_t currentThreshold32 = *smoothedThreshold32;
	int32_t currentBlendSlope = *smoothedBlendSlope_Q8;

	// Snap to target on first use (when at default "dry" values) to avoid starting silent
	bool isFirstUse =
	    (currentThreshold32 == TableShaperCore::kInt32MaxShifted && currentBlendSlope == 0 && targetMixNorm_Q16 > 0);
	if (isFirstUse) {
		currentThreshold32 = targetThreshold32;
		currentBlendSlope = targetBlendSlope;
	}

	// Close half the distance per buffer (IIR with alpha=0.5, computed once per buffer)
	currentThreshold32 = (currentThreshold32 + targetThreshold32) >> 1;
	currentBlendSlope = (currentBlendSlope + targetBlendSlope) >> 1;

	// Update ctx with smoothed values for this buffer
	ctx.threshold32 = currentThreshold32;
	ctx.blendSlope_Q8 = currentBlendSlope;

	// Split loops: simple path (hysteresis + optional rotation) vs full extras
	// Rotation is single-cycle ROR, no stride/state needed - can use simple path
	// Mask 0x1B = sub|feedback|lpf|integrator - these need the full path
	// Also check lpfActive: forced LPF for square waves needs full path for processShaperSample
	if ((ctx.extrasMask & 0x1B) == 0 && !ctx.lpfActive) {
		// Simple path: scale → optional rotation → processPreScaled32 → unscale
		for (auto& sample : buffer) {
			gainCtx.current += multiply_32x32_rshift32(gainCtx.target - gainCtx.current, gainCtx.alpha) * 2;
			q31_t drivenL = lshiftAndSaturate<6>(multiply_32x32_rshift32(sample.l, gainCtx.current));
			q31_t drivenR = lshiftAndSaturate<6>(multiply_32x32_rshift32(sample.r, gainCtx.current));
			int32_t scaledDryL = shaper.scaleInput(drivenL);
			int32_t scaledDryR = shaper.scaleInput(drivenR);
			int32_t scaledWetL = (ctx.rotation != 0) ? rotateRight(scaledDryL, ctx.rotation) : scaledDryL;
			int32_t scaledWetR = (ctx.rotation != 0) ? rotateRight(scaledDryR, ctx.rotation) : scaledDryR;
			int32_t scaledOutL = shaper.processPreScaled32(scaledWetL, scaledDryL, ctx.blendSlope_Q8, ctx.threshold32,
			                                               ctx.tableIdx, ctx.hystOffset, hystStateL);
			int32_t scaledOutR = shaper.processPreScaled32(scaledWetR, scaledDryR, ctx.blendSlope_Q8, ctx.threshold32,
			                                               ctx.tableIdx, ctx.hystOffset, hystStateR);
			q31_t outL = scaledOutL >> ctx.inputScaleShift;
			q31_t outR = scaledOutR >> ctx.inputScaleShift;
			if (ctx.needsGainAdjust) {
				outL = multiply_32x32_rshift32(outL, ctx.attenGain_Q30) << 2;
				outR = multiply_32x32_rshift32(outR, ctx.attenGain_Q30) << 2;
			}
			sample.l = outL;
			sample.r = outR;
		}
	}
	else {
		// Full extras path: stride loop + all extras processing
		bool needsStrideLoop = prevSampleL && prevSampleR
		                       && ((ctx.subBoost_Q16 != 0 && zcCountL && zcCountR && subSignL && subSignR)
		                           || ctx.feedback_Q16 > 0 || ctx.integratorActive);
		int32_t strideCounter = 0;

		for (auto& sample : buffer) {
			gainCtx.current += multiply_32x32_rshift32(gainCtx.target - gainCtx.current, gainCtx.alpha) * 2;

			q31_t inputL = sample.l;
			q31_t inputR = sample.r;
			int32_t scaledFeedbackL = 0;
			int32_t scaledFeedbackR = 0;

			if (needsStrideLoop) {
				strideCounter++;
				if (strideCounter >= ctx.stride) {
					strideCounter = 0;
					int32_t prevL = *prevSampleL;
					int32_t prevR = *prevSampleR;

					if (ctx.feedback_Q16 > 0) {
						q31_t drivenPrevL = lshiftAndSaturate<6>(multiply_32x32_rshift32(prevL, gainCtx.current));
						q31_t drivenPrevR = lshiftAndSaturate<6>(multiply_32x32_rshift32(prevR, gainCtx.current));
						int32_t scaledPrevL = shaper.scaleInput(drivenPrevL);
						int32_t scaledPrevR = shaper.scaleInput(drivenPrevR);
						scaledFeedbackL =
						    static_cast<int32_t>((static_cast<int64_t>(scaledPrevL) * ctx.feedback_Q16) >> 16);
						scaledFeedbackR =
						    static_cast<int32_t>((static_cast<int64_t>(scaledPrevR) * ctx.feedback_Q16) >> 16);
					}

					*prevSampleL = inputL;
					*prevSampleR = inputR;

					bool zcL = (inputL ^ prevL) < 0;
					if (zcL) {
						if (ctx.subBoost_Q16 != 0 && zcCountL && subSignL) {
							(*zcCountL)++;
							if (*zcCountL >= ctx.subRatio) {
								*subSignL = -*subSignL;
								*zcCountL = 0;
							}
						}
						if (ctx.integratorActive && slewedL) {
							*slewedL = 0;
						}
					}

					bool zcR = (inputR ^ prevR) < 0;
					if (zcR) {
						if (ctx.subBoost_Q16 != 0 && zcCountR && subSignR) {
							(*zcCountR)++;
							if (*zcCountR >= ctx.subRatio) {
								*subSignR = -*subSignR;
								*zcCountR = 0;
							}
						}
						if (ctx.integratorActive && slewedR) {
							*slewedR = 0;
						}
					}
				}
			}

			int8_t currentSubSignL = (subSignL && ctx.subBoost_Q16 != 0) ? *subSignL : 1;
			int8_t currentSubSignR = (subSignR && ctx.subBoost_Q16 != 0) ? *subSignR : 1;

			q31_t outL = processShaperSample(inputL, gainCtx.current, ctx, slewedL, hystStateL, currentSubSignL, shaper,
			                                 scaledFeedbackL);
			q31_t outR = processShaperSample(inputR, gainCtx.current, ctx, slewedR, hystStateR, currentSubSignR, shaper,
			                                 scaledFeedbackR);

			if (ctx.needsGainAdjust) {
				outL = multiply_32x32_rshift32(outL, ctx.attenGain_Q30) << 2;
				outR = multiply_32x32_rshift32(outR, ctx.attenGain_Q30) << 2;
			}

			sample.l = outL;
			sample.r = outR;
		}
	}

	*smoothedDriveGain = gainCtx.current;
	// Store current values (closes half distance per buffer, converges exponentially)
	*smoothedThreshold32 = currentThreshold32;
	*smoothedBlendSlope_Q8 = currentBlendSlope;
}

} // namespace deluge::dsp
