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

#include "dsp/fast_math.h"
#include "dsp/phi_triangle.hpp"
#include "dsp/util.hpp"
#include "util/fixedpoint.h"
#include "util/functions.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace deluge::dsp {

/**
 * Parameters for table-based shaper - consolidated for efficient passing
 *
 * All parameters are normalized 0-1 range:
 * - drive: Overall intensity (0 = bypass)
 * - inflatorWeight: Inflator basis weight (punchy, expand-compress)
 * - polyWeight: Polynomial basis weight (soft saturation, tanh-like)
 * - hardKneeWeight: Hard knee basis weight (crisp, aggressive)
 * - chebyWeight: Chebyshev T5 basis weight (fold, synthy)
 * - sineFoldWeight: Sine folder basis weight (harmonic-rich)
 * - rectifierWeight: Rectifier basis weight (diode, asymmetric)
 * - threshold: Linear zone size (1 = all linear, 0 = always saturate)
 * - asymmetry: Even harmonics (0.5 = symmetric)
 */
struct TableShaperParams {
	float drive{0.0f};
	float inflatorWeight{1.0f};
	float polyWeight{0.0f};
	float hardKneeWeight{0.0f};
	float chebyWeight{0.0f};
	float sineFoldWeight{0.0f};
	float rectifierWeight{0.0f};
	float threshold{1.0f};
	float asymmetry{0.5f};
	float deadzoneWidth{0.0f};      // 0 = no deadzone, 1 = 90% deadzone (10% passthrough)
	float deadzonePhase{0.5f};      // Center of passthrough window: 0.5 = x=0 (zero crossing)
	float hysteresis{0.0f};         // Bipolar [-1,+1]: direction-dependent transfer offset
	                                // Positive: punchy attack, soft decay (tube-like)
	                                // Negative: soft attack, gritty decay (swell-like)
	float hystMixInfluence{0.0f};   // How much mix knob affects hysteresis strength [0,1]
	float driftMultIntensity{0.0f}; // Multiplicative drift intensity [-1,+1] (sag/boost)
	float driftAddIntensity{0.0f};  // Additive drift intensity [-1,+1] (pull/push from center)
	float driftStereoOffset{0.0f};  // Stereo decorrelation: R channel slope multiplier offset [-1,1]
	float subIntensity{0.0f};       // Subharmonic gain boost intensity from phi triangle [0,1]
	int8_t subRatio{2};             // Subharmonic ZC threshold: 2=octave, 3=twelfth, 4=2oct, 5=2oct+3rd, 6=2oct+5th
	int32_t stride{64};             // ZC detection stride [1,128]: lower=more freq, higher=bass-only + feedback comb
	float feedback{0.0f};           // Feedback intensity [0,0.8]: comb filter at 44100/stride Hz
	int8_t rotation{0};             // Bit rotation amount [0,31]: aliasing effect (0=passthrough)
	float slewIntensity{0.0f};      // Unipolar [0,1]: intensity for LPF or integrator (bit selects which)
	float preExpandAmount{0.0f};    // Pre-expansion intensity [0,1] (0=linear, 1=50% boost at zero crossing)

	/// Clamp all parameters to valid ranges
	void clamp() {
		drive = std::clamp(drive, 0.0f, 1.0f);
		inflatorWeight = std::clamp(inflatorWeight, 0.0f, 1.0f);
		polyWeight = std::clamp(polyWeight, 0.0f, 1.0f);
		hardKneeWeight = std::clamp(hardKneeWeight, 0.0f, 1.0f);
		chebyWeight = std::clamp(chebyWeight, 0.0f, 1.0f);
		sineFoldWeight = std::clamp(sineFoldWeight, 0.0f, 1.0f);
		rectifierWeight = std::clamp(rectifierWeight, 0.0f, 1.0f);
		threshold = std::clamp(threshold, 0.0f, 1.0f);
		asymmetry = std::clamp(asymmetry, 0.0f, 1.0f);
		deadzoneWidth = std::clamp(deadzoneWidth, 0.0f, 1.0f);
		deadzonePhase = std::clamp(deadzonePhase, 0.0f, 1.0f);
		hysteresis = std::clamp(hysteresis, -1.0f, 1.0f);
		hystMixInfluence = std::clamp(hystMixInfluence, 0.0f, 1.0f);
		driftMultIntensity = std::clamp(driftMultIntensity, -1.0f, 1.0f);
		driftAddIntensity = std::clamp(driftAddIntensity, -1.0f, 1.0f);
		driftStereoOffset = std::clamp(driftStereoOffset, -1.0f, 1.0f);
		subIntensity = std::clamp(subIntensity, 0.0f, 1.0f);
		subRatio = std::clamp(subRatio, static_cast<int8_t>(2), static_cast<int8_t>(6));
		stride = std::clamp(stride, static_cast<int32_t>(1), static_cast<int32_t>(128));
		feedback = std::clamp(feedback, 0.0f, 0.8f);
		rotation = std::clamp(rotation, static_cast<int8_t>(0), static_cast<int8_t>(31));
		slewIntensity = std::clamp(slewIntensity, 0.0f, 1.0f);
		preExpandAmount = std::clamp(preExpandAmount, 0.0f, 1.0f);
	}

	bool operator!=(const TableShaperParams& o) const {
		return drive != o.drive || inflatorWeight != o.inflatorWeight || polyWeight != o.polyWeight
		       || hardKneeWeight != o.hardKneeWeight || chebyWeight != o.chebyWeight
		       || sineFoldWeight != o.sineFoldWeight || rectifierWeight != o.rectifierWeight || threshold != o.threshold
		       || asymmetry != o.asymmetry || deadzoneWidth != o.deadzoneWidth || deadzonePhase != o.deadzonePhase
		       || hysteresis != o.hysteresis || hystMixInfluence != o.hystMixInfluence
		       || driftMultIntensity != o.driftMultIntensity || driftAddIntensity != o.driftAddIntensity
		       || driftStereoOffset != o.driftStereoOffset || subIntensity != o.subIntensity || subRatio != o.subRatio
		       || stride != o.stride || feedback != o.feedback || rotation != o.rotation
		       || slewIntensity != o.slewIntensity || preExpandAmount != o.preExpandAmount;
	}
};

/**
 * Table-based Parametric Shaper
 *
 * Features:
 * - 6 basis functions for rich harmonic exploration
 * - Drive parameter where 0 = linear bypass (transparent)
 * - Separate weights for each basis function
 * - Double-buffered lookup tables with IIR crossfade for click-free updates
 */
class TableShaperCore {
public:
	// =============================================================================
	// TABLE REGENERATION CONFIGURATION
	// =============================================================================
	// Table size vs click tradeoff:
	// - 2048: Best quality for wavefolders, but regeneration may cause minor clicks
	// - 1024: Click-free regeneration, minimal quality difference for most curves
	// - 512/256/128: Faster regeneration, noticeable smoothing on sharp features
	// Linear interpolation adds 16-bit fractional precision between entries.
	static constexpr size_t kTableSize = 1024;
	// =============================================================================

	static constexpr float kTableScale = static_cast<float>(kTableSize) / 2.0f;

	TableShaperCore() = default; // Tables start empty, allocated on first non-linear use

	/// Set all parameters - deferred regeneration (call regenerateIfDirty from non-audio context)
	void setParameters(const TableShaperParams& p) {
		TableShaperParams clamped = p;
		clamped.clamp();
		if (clamped != params_) {
			params_ = clamped;
			tablesDirty_ = true;
		}
	}

	/// Pre-allocate buffers (call from UI thread before scheduling regeneration)
	/// This ensures no allocation happens during the deferred regeneration task
	void ensureBuffersAllocated() {
		if (fTables_[0].size() != kTableSize + 1) {
			fTables_[0].resize(kTableSize + 1);
		}
		if (fTables_[1].size() != kTableSize + 1) {
			fTables_[1].resize(kTableSize + 1);
		}
		if (fTableTempFloat_.size() != kTableSize + 1) {
			fTableTempFloat_.resize(kTableSize + 1);
		}
	}

	/// Call from non-audio context (UI routine, etc) to regenerate tables
	void regenerateIfDirty() {
		if (tablesDirty_) {
			regenerateTables();
		}
	}

	/// Get current parameters
	[[nodiscard]] const TableShaperParams& getParameters() const { return params_; }

	/// Check if effect is effectively bypassed (transparent) based on current params
	/// Only checks drive (X axis) - threshold shouldn't cause bypass since user explicitly set X > 0
	/// Note: This checks params_, NOT the isLinear_ flag (which is for audio thread sync)
	[[nodiscard]] bool isLinear() const { return params_.drive < 0.001f; }

	// =============================================================================
	// CONSTANTS FOR MIX-DEPENDENT BLEND CALCULATIONS (exposed for hoisting)
	// =============================================================================
	static constexpr int32_t kInt32Max = 2147483647;
	static constexpr int64_t kInt32Max64 = 2147483647;
	static constexpr int32_t kOne_Q16 = 65536;
	static constexpr int32_t kMaxMix = 131072;
	static constexpr int32_t kBaseSlope = 256;
	static constexpr int32_t kSlopeShift = 20;

	// Threshold calculation constants (64-bit version - legacy)
	static constexpr int32_t kMaxSlope = kBaseSlope + ((static_cast<int64_t>(kMaxMix) * kMaxMix) >> kSlopeShift);
	static constexpr int32_t kBlendTarget = kOne_Q16 << 8;
	static constexpr int32_t kRequiredDiffQ16 = (kBlendTarget + kMaxSlope - 1) / kMaxSlope;
	static constexpr int64_t kThresholdForFullWet = -(static_cast<int64_t>(kRequiredDiffQ16) << 15);
	static constexpr int64_t kThresholdRange = kInt32Max64 - kThresholdForFullWet;

	// 32-bit threshold constants (shifted down by 8 bits to fit in int32)
	// This eliminates 64-bit arithmetic in the per-sample path
	static constexpr int32_t kThresholdShift = 8;
	static constexpr int32_t kThresholdForFullWet32 = static_cast<int32_t>(kThresholdForFullWet >> kThresholdShift);
	static constexpr int32_t kInt32MaxShifted = kInt32Max >> kThresholdShift;
	static constexpr int32_t kThresholdRange32 = kInt32MaxShifted - kThresholdForFullWet32;

	/// Compute baseSlope from mixNorm_Q16 (call once per buffer for hoisting)
	/// baseSlope = kBaseSlope + (mixNorm² >> kSlopeShift)
	[[gnu::always_inline]] static int32_t computeBaseSlope(int32_t mixNorm_Q16) {
		int64_t mixSquared = static_cast<int64_t>(mixNorm_Q16) * mixNorm_Q16;
		return kBaseSlope + static_cast<int32_t>(mixSquared >> kSlopeShift);
	}

	/// Compute threshold64 from mixNorm_Q16 (call once per buffer for hoisting)
	/// Maps mix range to threshold: [INT32_MAX at mix=0] to [negative at mix=max]
	[[gnu::always_inline]] static int64_t computeThreshold64(int32_t mixNorm_Q16) {
		return kInt32Max64 - ((kThresholdRange * mixNorm_Q16) >> 17);
	}

	/// Compute 32-bit threshold from mixNorm_Q16 (faster than 64-bit version)
	/// Threshold is shifted down by kThresholdShift bits to fit in int32
	/// Use with processInt32Fast() for vanilla mode (no extras)
	[[gnu::always_inline]] static int32_t computeThreshold32(int32_t mixNorm_Q16) {
		// Same calculation as 64-bit but in shifted domain
		// Note: multiplication needs int64 to avoid overflow (8.5M * 131072 = 1.1T)
		return kInt32MaxShifted - static_cast<int32_t>((static_cast<int64_t>(kThresholdRange32) * mixNorm_Q16) >> 17);
	}

	/// Compute blendSlope_Q8 from baseSlope (call once per buffer for hoisting)
	/// blendSlope = baseSlope * blendAggression (from X axis)
	[[gnu::always_inline]] int32_t computeBlendSlope_Q8(int32_t baseSlope) const {
		return (baseSlope * blendAggression_Q8_) >> 8;
	}

	/// Get isLinear flag (call once per buffer for hoisting)
	[[gnu::always_inline]] bool getIsLinear() const { return isLinear_.load(std::memory_order_acquire); }

	/// Get target table index (call once per buffer for hoisting)
	[[gnu::always_inline]] int8_t getTargetTableIndex() const {
		return targetTableIndex_.load(std::memory_order_acquire);
	}

	/// Process a single sample using integer-only path with Q16 mix parameter
	/// @param input Input sample in raw signal format (e.g., ~23M for FM, ~1.4M for subtractive)
	/// @param driveGain_Q26 Pre-computed drive gain in Q26 format (allows up to 32x)
	/// @param mixNorm_Q16 Normalized mix in Q16.16 (65536 = 1.0, 131072 = 2.0 full wet)
	/// @return Output sample at same level as input (unity gain when undriven)
	[[gnu::always_inline]] int32_t processInt32Q16(int32_t input, int32_t driveGain_Q26, int32_t mixNorm_Q16 = 131072) {
		// Linear drive with Q26 gain (<<6 recovers from Q26 multiply, saturating)
		int32_t afterDrive = lshiftAndSaturate<6>(multiply_32x32_rshift32(input, driveGain_Q26));

		// Fast path: bypass when linear (tables may be deallocated)
		// acquire ordering ensures we see all table writes if isLinear_ is false
		if (isLinear_.load(std::memory_order_acquire)) {
			return afterDrive; // Return driven signal (consistent with dry path)
		}

		// Scale to fill table range (saturating left shift)
		int32_t scaledInput = lshiftAndSaturateUnknown(afterDrive, inputScaleShift_);

		// Amplitude-dependent blend (branchless abs)
		int32_t clampedInput = std::max(scaledInput, static_cast<int32_t>(-2147483647));
		int32_t sign = clampedInput >> 31;
		int32_t absInput = (clampedInput ^ sign) - sign;

		// Compute mix-dependent values (could be hoisted via processInt32Q16Hoisted)
		int32_t baseSlope = computeBaseSlope(mixNorm_Q16);
		int32_t blendSlope_Q8 = (baseSlope * blendAggression_Q8_) >> 8;
		int64_t threshold64 = computeThreshold64(mixNorm_Q16);

		// diff = absInput - threshold
		int64_t diff64 = static_cast<int64_t>(absInput) - threshold64;
		if (diff64 <= 0) {
			return afterDrive; // Return driven (but unshaped) signal
		}

		// Convert diff to Q16 for blend calculation
		int32_t diff_clamped = static_cast<int32_t>(std::min(diff64, static_cast<int64_t>(kInt32Max)));
		int32_t diff_Q16 = diff_clamped >> 15;

		// Linear blend calculation: blend = diff * slope
		int32_t blend_Q16 = (diff_Q16 * blendSlope_Q8) >> 8;

		// Clamp to [0, 65536] (0 to 1.0)
		if (blend_Q16 > kOne_Q16) {
			blend_Q16 = kOne_Q16;
		}

		// Table lookup
		uint32_t tableInput = static_cast<uint32_t>(scaledInput) + 2147483648u;

		// TEMPORARY: Crossfade disabled for benchmarking - use target table directly
		int8_t target = targetTableIndex_.load(std::memory_order_acquire);
		int32_t lookup = lookupFunctionIntDirect(tableInput, target);

		// Blend at scaled level (both signals clipped to same peak)
		int32_t blend_Q30 = blend_Q16 << 14;
		int32_t oneMinusBlend_Q30 = (kOne_Q16 << 14) - blend_Q30;

		int32_t dryPart = multiply_32x32_rshift32(scaledInput, oneMinusBlend_Q30) << 2;
		int32_t wetPart = multiply_32x32_rshift32(lookup, blend_Q30) << 2;
		int32_t blended = dryPart + wetPart;

		// Scale back to original level using bit shift (Fix 1: restored from float divide)
		return blended >> inputScaleShift_;
	}

	/// Process a single sample using integer-only path (legacy float interface)
	/// Uses stored inputScaleShift_ set via setExpectedPeak()
	/// @param input Input sample in raw signal format (e.g., ~23M for FM, ~1.4M for subtractive)
	/// @param driveGain_Q30 Pre-computed drive gain in Q30 format
	/// @param mixNorm Normalized mix (0 = full dry, 2 = full wet), for amplitude-dependent blend
	/// @return Output sample at same level as input (unity gain when undriven)
	[[gnu::always_inline]] int32_t processInt32(int32_t input, int32_t driveGain_Q30, float mixNorm = 2.0f) {
		// Convert float mixNorm to Q16 and call the integer version
		int32_t mixNorm_Q16 = static_cast<int32_t>(mixNorm * 65536.0f);
		return processInt32Q16(input, driveGain_Q30, mixNorm_Q16);
	}

	/// Process sample with pre-computed values (hoisted out of sample loop)
	/// All parameters computed once per buffer for maximum performance.
	/// Drive is applied by caller before splitting wet/dry paths - this function only scales for table.
	/// @param wetInput Wet path input (pre-driven, then slew/drift/sub applied externally)
	/// @param dryInput Dry path input (pre-driven, original signal for blending)
	/// @param blendSlope_Q8 Pre-computed from computeBlendSlope_Q8(baseSlope)
	/// @param threshold64 Pre-computed from computeThreshold64(mixNorm_Q16)
	/// @param tableIdx Pre-computed from getTargetTableIndex()
	/// @param hystOffset Hysteresis offset (0 = disabled, hoisted from getHystOffset())
	/// @param prevScaledInput Pointer to previous scaled input for slope detection (updated)
	/// @return Output sample at same level as input (driven level)
	[[gnu::always_inline]] int32_t processInt32Q16Hoisted(int32_t wetInput, int32_t dryInput, int32_t blendSlope_Q8,
	                                                      int64_t threshold64, int8_t tableIdx, int32_t hystOffset = 0,
	                                                      int32_t* prevScaledInput = nullptr) {
		// Scale for table resolution (drive already applied by caller)
		int32_t scaledWet = lshiftAndSaturateUnknown(wetInput, inputScaleShift_);
		int32_t scaledDry = lshiftAndSaturateUnknown(dryInput, inputScaleShift_);

		// Amplitude-dependent blend based on DRY signal (branchless abs)
		int32_t clampedDry = std::max(scaledDry, static_cast<int32_t>(-2147483647));
		int32_t sign = clampedDry >> 31;
		int32_t absDry = (clampedDry ^ sign) - sign;

		// Use pre-computed threshold64 and blendSlope_Q8 (hoisted)
		int64_t diff64 = static_cast<int64_t>(absDry) - threshold64;
		if (diff64 <= 0) {
			// Still update prev state for hysteresis even in bypass
			if (prevScaledInput) {
				*prevScaledInput = scaledDry;
			}
			// Return dry signal when below threshold (already at driven level)
			return dryInput;
		}

		// Convert diff to Q16 for blend calculation
		int32_t diff_clamped = static_cast<int32_t>(std::min(diff64, kInt32Max64));
		int32_t diff_Q16 = diff_clamped >> 15;

		// Linear blend calculation
		int32_t blend_Q16 = (diff_Q16 * blendSlope_Q8) >> 8;
		if (blend_Q16 > kOne_Q16) {
			blend_Q16 = kOne_Q16;
		}

		// Hysteresis: direction-dependent table offset (based on dry signal)
		// Note: prevScaledInput is null when hystOffset is 0 (optimization in shaper_buffer.h)
		int32_t offsetWet = scaledWet;
		if (prevScaledInput) {
			int32_t slope = scaledDry - *prevScaledInput;
			int32_t signMask = slope >> 31;
			int32_t hystTableOffset = (hystOffset ^ signMask) - signMask;
			*prevScaledInput = scaledDry;
			offsetWet = add_saturate(scaledWet, hystTableOffset);
		}

		// Table lookup on wet path (with hysteresis offset if enabled)
		uint32_t tableInput = static_cast<uint32_t>(offsetWet) + 2147483648u;
		int32_t lookup = lookupFunctionIntDirect(tableInput, tableIdx);

		// Blend at scaled level (dry signal, wet table lookup)
		int32_t blend_Q30 = blend_Q16 << 14;
		int32_t oneMinusBlend_Q30 = (kOne_Q16 << 14) - blend_Q30;

		int32_t dryPart = multiply_32x32_rshift32(scaledDry, oneMinusBlend_Q30) << 2;
		int32_t wetPart = multiply_32x32_rshift32(lookup, blend_Q30) << 2;
		int32_t blended = dryPart + wetPart;

		return blended >> inputScaleShift_;
	}

	/// Fast processing path for vanilla mode (gammaPhase==0, no extras)
	/// Uses 32-bit threshold arithmetic. In vanilla mode, wet and dry paths are identical
	/// (no slew/drift/sub modifiers), so we use a single scaled value for both.
	/// @param drivenInput Input sample with drive gain already applied by caller
	/// @param blendSlope_Q8 Pre-computed from computeBlendSlope_Q8(baseSlope)
	/// @param threshold32 Pre-computed from computeThreshold32(mixNorm_Q16)
	/// @param tableIdx Pre-computed from getTargetTableIndex()
	/// @return Output sample at same level as input (drive gain preserved)
	[[gnu::always_inline]] int32_t processInt32Fast(int32_t drivenInput, int32_t blendSlope_Q8, int32_t threshold32,
	                                                int8_t tableIdx) {
		// Scale up for table resolution. In vanilla mode, wet == dry (no modifiers applied),
		// so we use one scaled value for both the dry blend component and table lookup.
		// INTENTIONAL: The driven signal (with drive gain) is used as the dry reference.
		int32_t scaledDry = lshiftAndSaturateUnknown(drivenInput, inputScaleShift_);
		int32_t scaledWet = scaledDry; // Identical in vanilla mode - no wet-path modifications

		// Branchless abs for threshold check (based on dry signal level)
		int32_t clamped = std::max(scaledDry, static_cast<int32_t>(-2147483647));
		int32_t sign = clamped >> 31;
		int32_t absVal = (clamped ^ sign) - sign;

		// 32-bit threshold comparison (absVal shifted down to match threshold domain)
		int32_t absShifted = absVal >> kThresholdShift;
		int32_t diff = absShifted - threshold32;
		if (diff <= 0) {
			// Below threshold: return driven input unchanged (drive gain preserved)
			return drivenInput;
		}

		// Blend calculation (diff is in shifted domain, adjust shift accordingly)
		// Original: diff_Q16 = diff >> 15, but diff is already shifted by 8
		// So: diff_Q16 = diff >> (15 - kThresholdShift) = diff >> 7
		int32_t diff_Q16 = diff >> (15 - kThresholdShift);
		int32_t blend_Q16 = (diff_Q16 * blendSlope_Q8) >> 8;
		if (blend_Q16 > kOne_Q16) {
			blend_Q16 = kOne_Q16;
		}

		// Table lookup using wet path (same as dry in vanilla mode)
		uint32_t tableInput = static_cast<uint32_t>(scaledWet) + 2147483648u;
		int32_t lookup = lookupFunctionIntDirect(tableInput, tableIdx);

		// Blend at scaled level: output = dry * (1 - blend) + wet * blend
		// INTENTIONAL: dry component uses the driven signal (scaledDry) to preserve drive gain
		int32_t blend_Q30 = blend_Q16 << 14;
		int32_t oneMinusBlend_Q30 = (kOne_Q16 << 14) - blend_Q30;

		int32_t dryPart = multiply_32x32_rshift32(scaledDry, oneMinusBlend_Q30) << 2;
		int32_t wetPart = multiply_32x32_rshift32(lookup, blend_Q30) << 2;
		int32_t blended = dryPart + wetPart;

		// Scale back to original level (drive gain preserved in output)
		return blended >> inputScaleShift_;
	}

	/// Fast processing path with separate wet/dry inputs and hysteresis support
	/// Uses 32-bit threshold arithmetic (eliminates 64-bit ops in per-sample path).
	/// For use when extras (slew/drift/sub) modify wet path differently from dry.
	/// @param wetInput Wet path input (pre-driven, with slew/drift/sub applied)
	/// @param dryInput Dry path input (pre-driven, original signal for blending)
	/// @param blendSlope_Q8 Pre-computed from computeBlendSlope_Q8(baseSlope)
	/// @param threshold32 Pre-computed from computeThreshold32(mixNorm_Q16)
	/// @param tableIdx Pre-computed from getTargetTableIndex()
	/// @param hystOffset Hysteresis offset (0 = disabled, from getHystOffset())
	/// @param prevScaledInput Pointer to previous scaled input for slope detection (updated)
	/// @return Output sample at same level as input (drive gain preserved)
	[[gnu::always_inline]] int32_t processInt32Fast32Hoisted(int32_t wetInput, int32_t dryInput, int32_t blendSlope_Q8,
	                                                         int32_t threshold32, int8_t tableIdx,
	                                                         int32_t hystOffset = 0,
	                                                         int32_t* prevScaledInput = nullptr) {
		// Scale for table resolution (drive already applied by caller)
		int32_t scaledWet = lshiftAndSaturateUnknown(wetInput, inputScaleShift_);
		int32_t scaledDry = lshiftAndSaturateUnknown(dryInput, inputScaleShift_);

		// Amplitude-dependent blend based on DRY signal (branchless abs)
		int32_t clampedDry = std::max(scaledDry, static_cast<int32_t>(-2147483647));
		int32_t sign = clampedDry >> 31;
		int32_t absDry = (clampedDry ^ sign) - sign;

		// 32-bit threshold comparison (absVal shifted down to match threshold domain)
		int32_t absShifted = absDry >> kThresholdShift;
		int32_t diff = absShifted - threshold32;
		if (diff <= 0) {
			// Still update prev state for hysteresis even in bypass
			if (prevScaledInput) {
				*prevScaledInput = scaledDry;
			}
			// Return dry signal when below threshold (already at driven level)
			return dryInput;
		}

		// Blend calculation (diff is in shifted domain, adjust shift accordingly)
		// Original: diff_Q16 = diff >> 15, but diff is already shifted by 8
		// So: diff_Q16 = diff >> (15 - kThresholdShift) = diff >> 7
		int32_t diff_Q16 = diff >> (15 - kThresholdShift);
		int32_t blend_Q16 = (diff_Q16 * blendSlope_Q8) >> 8;
		if (blend_Q16 > kOne_Q16) {
			blend_Q16 = kOne_Q16;
		}

		// Hysteresis: direction-dependent table offset (based on dry signal)
		// Note: prevScaledInput is null when hystOffset is 0 (optimization in shaper_buffer.h)
		int32_t offsetWet = scaledWet;
		if (prevScaledInput) {
			int32_t slope = scaledDry - *prevScaledInput;
			int32_t signMask = slope >> 31;
			int32_t hystTableOffset = (hystOffset ^ signMask) - signMask;
			*prevScaledInput = scaledDry;
			offsetWet = add_saturate(scaledWet, hystTableOffset);
		}

		// Table lookup on wet path (with hysteresis offset if enabled)
		uint32_t tableInput = static_cast<uint32_t>(offsetWet) + 2147483648u;
		int32_t lookup = lookupFunctionIntDirect(tableInput, tableIdx);

		// Blend at scaled level (dry signal, wet table lookup)
		int32_t blend_Q30 = blend_Q16 << 14;
		int32_t oneMinusBlend_Q30 = (kOne_Q16 << 14) - blend_Q30;

		int32_t dryPart = multiply_32x32_rshift32(scaledDry, oneMinusBlend_Q30) << 2;
		int32_t wetPart = multiply_32x32_rshift32(lookup, blend_Q30) << 2;
		int32_t blended = dryPart + wetPart;

		return blended >> inputScaleShift_;
	}

	/// Process with pre-scaled inputs (for operating entirely in scaled domain)
	/// Caller scales input once, applies LPF/sub in scaled domain, then calls this.
	/// Returns SCALED output - caller must >> inputScaleShift_ to unscale.
	/// @param scaledWet Pre-scaled wet input (after LPF/sub modifications in scaled domain)
	/// @param scaledDry Pre-scaled dry input (for threshold comparison and blend)
	/// @param blendSlope_Q8 Pre-computed from computeBlendSlope_Q8()
	/// @param threshold32 Pre-computed from computeThreshold32()
	/// @param tableIdx Pre-computed from getTargetTableIndex()
	/// @param hystOffset Hysteresis offset (0 = disabled)
	/// @param prevScaledInput Pointer to previous scaled input for slope detection
	/// @return SCALED output (caller must >> inputScaleShift_ to unscale)
	[[gnu::always_inline]] int32_t processPreScaled32(int32_t scaledWet, int32_t scaledDry, int32_t blendSlope_Q8,
	                                                  int32_t threshold32, int8_t tableIdx, int32_t hystOffset = 0,
	                                                  int32_t* prevScaledInput = nullptr) {
		// Amplitude-dependent blend based on DRY signal (branchless abs)
		int32_t clampedDry = std::max(scaledDry, static_cast<int32_t>(-2147483647));
		int32_t sign = clampedDry >> 31;
		int32_t absDry = (clampedDry ^ sign) - sign;

		// 32-bit threshold comparison (absVal shifted down to match threshold domain)
		int32_t absShifted = absDry >> kThresholdShift;
		int32_t diff = absShifted - threshold32;
		if (diff <= 0) {
			// Still update prev state for hysteresis even in bypass
			if (prevScaledInput) {
				*prevScaledInput = scaledDry;
			}
			// Return scaled dry signal when below threshold
			return scaledDry;
		}

		// Blend calculation (diff is in shifted domain, adjust shift accordingly)
		int32_t diff_Q16 = diff >> (15 - kThresholdShift);
		int32_t blend_Q16 = (diff_Q16 * blendSlope_Q8) >> 8;
		if (blend_Q16 > kOne_Q16) {
			blend_Q16 = kOne_Q16;
		}

		// Hysteresis: direction-dependent table offset (based on dry signal)
		int32_t offsetWet = scaledWet;
		if (prevScaledInput) {
			int32_t slope = scaledDry - *prevScaledInput;
			int32_t signMask = slope >> 31;
			int32_t hystTableOffset = (hystOffset ^ signMask) - signMask;
			*prevScaledInput = scaledDry;
			offsetWet = add_saturate(scaledWet, hystTableOffset);
		}

		// Table lookup on wet path (with hysteresis offset if enabled)
		uint32_t tableInput = static_cast<uint32_t>(offsetWet) + 2147483648u;
		int32_t lookup = lookupFunctionIntDirect(tableInput, tableIdx);

		// Blend at scaled level (dry signal, wet table lookup)
		int32_t blend_Q30 = blend_Q16 << 14;
		int32_t oneMinusBlend_Q30 = (kOne_Q16 << 14) - blend_Q30;

		int32_t dryPart = multiply_32x32_rshift32(scaledDry, oneMinusBlend_Q30) << 2;
		int32_t wetPart = multiply_32x32_rshift32(lookup, blend_Q30) << 2;
		return dryPart + wetPart; // Return SCALED - caller unscales
	}

	/// Deallocate tables to free memory (~4KB)
	/// Called automatically when X=0 (linear bypass)
	void deallocateTables() {
		fTables_[0].clear();
		fTables_[0].shrink_to_fit();
		fTables_[1].clear();
		fTables_[1].shrink_to_fit();
	}

	/// Check if tables are currently allocated
	[[nodiscard]] bool hasAllocatedTables() const { return !fTables_[0].empty(); }

private:
	/// Regenerate both f(x) and F(x) tables based on current parameters
	/// Uses fast math approximations for speed during parameter automation.
	/// When linear (X=0), deallocates tables to save memory (~20KB per instance).
	/// IMPORTANT: isLinear_ is set AFTER tables are fully populated to prevent
	/// audio thread from reading partially-initialized data.
	void regenerateTables() {
		tablesDirty_ = false;
		bool willBeLinear = isLinear(); // Check params, but don't set isLinear_ yet

		// Compute blend aggression from drive (X axis) - defer writing until after table swap
		// Quadratic curve: very gentle at low X, snappy at high X
		// Range: [0.1, 2.0] → 20x dynamic range
		// At X=0: slope so gentle that full mix range is needed for full blend
		// At X=127: snappy onset, wet kicks in quickly
		float driveSquared = params_.drive * params_.drive;
		float aggression = 0.1f + driveSquared * 1.9f; // [0.1, 2.0]
		int32_t newBlendAggression = static_cast<int32_t>(aggression * 256.0f);

		if (willBeLinear) {
			// Set linear flag FIRST - audio thread will bypass table access
			// Tables are NOT deallocated here to avoid race with audio thread
			// (deallocation happens lazily when regenerating tables for non-linear)
			// No need to touch targetTableIndex_ - audio will bypass tables when linear
			blendAggression_Q8_ = newBlendAggression;
			isLinear_.store(true, std::memory_order_release); // Release LAST
			return;
		}

		// Keep isLinear_ = true until tables are FULLY populated
		// This prevents audio thread from reading partial data

		// Determine which buffer to write to (the one NOT currently being used)
		int8_t currentTarget = targetTableIndex_.load(std::memory_order_relaxed);
		int8_t writeToIdx = 1 - currentTarget;

		// CRITICAL: Snap blend to current target BEFORE writing to inactive buffer
		// With slow IIR crossfade, blend may not have converged yet, meaning audio
		// is still reading from both tables. If we write to the "inactive" buffer
		// while audio is blending from it, we get clicks from partially-written data.
		// Snapping ensures audio reads 100% from current target, leaving writeToIdx safe.
		currentBlend_Q15_ = currentTarget ? 32768 : 0;

		// Compute effective parameters
		float drive = params_.drive;
		float k = 1.0f + drive * 9.0f; // Steepness: 1 to 10

		// Threshold (T) removed - linear zone now handled by runtime mix blend
		// float T = params_.threshold * (1.0f - params_.drive * 0.8f);
		// T = std::fmax(T, 0.05f);

		// Asymmetry ratio for positive vs negative
		float asymRatio = 0.5f + params_.asymmetry; // 0.5 to 1.5

		// Precompute inverse normalization factors for tanh (using fast approximation)
		float invTanhNormPos = 1.0f / std::fmax(0.01f, fastTanh(k * asymRatio));
		float invTanhNormNeg = 1.0f / std::fmax(0.01f, fastTanh(k * (2.0f - asymRatio)));

		// Precompute weight normalization for all 6 basis functions
		float weightSum = params_.inflatorWeight + params_.polyWeight + params_.hardKneeWeight + params_.chebyWeight
		                  + params_.sineFoldWeight + params_.rectifierWeight;
		float invWeightSum = (weightSum > 0.001f) ? (1.0f / weightSum) : 1.0f;
		bool hasWeights = (weightSum >= 0.001f);

		// Lambda to evaluate transfer function at a given x position
		// Returns f(x) in [-1, +1] range
		auto evaluateTransfer = [&](float x) -> float {
			float mag = std::fabs(x);
			float sign = (x >= 0.0f) ? 1.0f : -1.0f;

			float f_val;

			// TODO: threshold/linear zone removed - now handled by mix-dependent blend at runtime
			// if (mag < T) {
			// 	// Linear zone
			// 	f_val = x;
			// }
			// else
			{
				// Saturation zone - blend between basis functions
				float norm = mag; // Direct mapping: 0 at center, 1 at extremes

				// Intensity: overdrive for richer harmonics (affects curve shape, not just level)
				float intensity = 1.0f + params_.drive * 1.0f;
				float overdriven = norm * intensity;

				// PRE-EXPANSION: Universal pre-stage that all bases see (Oxford Inflator-style)
				// Expands quiet signals, unity at loud - baked into table, zero runtime cost
				// preExpandAmount: 0 = linear passthrough, 1 = 50% boost at zero crossing
				float preExpanded = overdriven;
				if (params_.preExpandAmount > 0.001f) {
					float absOd = std::fabs(overdriven);
					float expandFactor = 1.0f + params_.preExpandAmount * 0.5f * (1.0f - absOd * absOd);
					preExpanded = overdriven * expandFactor;
				}

				// Asymmetric k for positive/negative
				float kEff = k * ((x >= 0.0f) ? asymRatio : (2.0f - asymRatio));
				float invTanhNorm = (x >= 0.0f) ? invTanhNormPos : invTanhNormNeg;

				// BASIS 1: Inflator (expand quiet, compress loud - punchy)
				// Applies its own expansion on top of preExpanded for layered effect
				// At |x|=0: gain = 1.5 (expansion), at |x|=1: gain = 1.0 (unity → tanh compresses)
				float absPreExp = std::fabs(preExpanded);
				float expandFactor = 1.0f + 0.5f * (1.0f - absPreExp * absPreExp);
				float inflator_out = fastTanh(preExpanded * expandFactor * kEff) * invTanhNorm;

				// BASIS 2: Polynomial (soft saturation, Taylor series of tanh)
				// x - x³/3 + x⁵/5 approaches tanh for small x, softer knee than tanh
				float pe2 = preExpanded * preExpanded;
				float pe3 = pe2 * preExpanded;
				float pe5 = pe3 * pe2;
				float poly_raw = preExpanded - pe3 / 3.0f + pe5 / 5.0f;
				float poly_out = fastTanh(poly_raw * kEff) * invTanhNorm;

				// BASIS 3: Hard clip (crisp, aggressive)
				float hardClip_out = std::fmin(std::fmax(preExpanded, -1.0f), 1.0f);

				// BASIS 4: Chebyshev T5 wavefolder (fold, synthy)
				float cheby_in = preExpanded * 1.2f;
				float cheby_in2 = cheby_in * cheby_in;
				float cheby_in3 = cheby_in2 * cheby_in;
				float cheby_in5 = cheby_in3 * cheby_in2;
				float cheby_raw = 16.0f * cheby_in5 - 20.0f * cheby_in3 + 5.0f * cheby_in;
				// Fast fmod 4.0f using branchless floor (handles negative cheby_raw)
				float temp = (cheby_raw + 1.0f) * 0.25f;
				int32_t i = static_cast<int32_t>(temp);
				float floor_temp = static_cast<float>(i - (temp < static_cast<float>(i)));
				float cheby_phase = (cheby_raw + 1.0f) - 4.0f * floor_temp;
				float cheby_out = (cheby_phase <= 2.0f) ? (cheby_phase - 1.0f) : (3.0f - cheby_phase);
				cheby_out = std::fabs(cheby_out);

				// BASIS 5: Sine folder (Gold)
				constexpr float kSineFoldA = 0.4f;
				float sineFoldB = 3.14159265f * (1.0f + drive * 1.0f);
				float sineFold_raw = fastTanh(preExpanded / kSineFoldA) * std::sin(sineFoldB * preExpanded)
				                     + fastTanh(preExpanded) * 0.3f;
				float sineFold_out = std::fabs(sineFold_raw);
				sineFold_out = std::fmin(sineFold_out, 1.0f);

				// BASIS 6: Rectifier (diode)
				float bias = 0.2f * drive;
				float rect_raw = std::fabs(preExpanded + bias) - bias;
				float rect_out = fastTanh(rect_raw * 2.0f);

				// Blend using weights
				float basis_out;
				if (!hasWeights) {
					basis_out = norm;
				}
				else {
					basis_out = (inflator_out * params_.inflatorWeight + poly_out * params_.polyWeight
					             + hardClip_out * params_.hardKneeWeight + cheby_out * params_.chebyWeight
					             + sineFold_out * params_.sineFoldWeight + rect_out * params_.rectifierWeight)
					            * invWeightSum;
				}

				// Drive-dependent blend toward linear - REMOVED (redundant with runtime mix blend)
				// basis_out = norm + (basis_out - norm) * drive;

				// Output uses full [0,1] range (T-based compression removed with linear zone)
				f_val = sign * basis_out;
			}

			// Apply deadzone modifier: use tiny epsilon instead of hard zero
			// This preserves some signal for DC balancing while being effectively silent
			if (params_.deadzoneWidth > 0.001f) {
				// Max 80% deadzone (20% minimum passthrough) to avoid extreme DC imbalance
				float passthrough = 1.0f - 0.8f * params_.deadzoneWidth;
				float centerX = params_.deadzonePhase * 2.0f - 1.0f;
				float halfWindow = passthrough;
				float lowX = centerX - halfWindow;
				float highX = centerX + halfWindow;
				if (x < lowX || x > highX) {
					// ±4 bits at int16 output (avoids rounding to zero)
					constexpr float kDeadzoneEpsilon = 4.0f / 32767.0f;
					f_val = (f_val >= 0.0f) ? kDeadzoneEpsilon : -kDeadzoneEpsilon;
				}
			}

			return f_val;
		};

		// fTableTempFloat_ must be pre-allocated via ensureBuffersAllocated()

		// Generate transfer function table, find min/max for centering
		float fMax = -1e30f;
		float fMin = 1e30f;
		for (size_t i = 0; i <= kTableSize; ++i) {
			float x = (static_cast<float>(i) / kTableScale) - 1.0f;
			float val = evaluateTransfer(x);
			fTableTempFloat_[i] = val;
			if (val > fMax)
				fMax = val;
			if (val < fMin)
				fMin = val;
		}

		// Midpoint centering in float
		float midpoint = (fMax + fMin) * 0.5f;
		float peakToPeak = fMax - fMin;
		float normalizationGain = (peakToPeak > 0.001f) ? (2.0f / peakToPeak) : 1.0f;

		// Final pass: center, normalize, and convert to int16
		for (size_t i = 0; i <= kTableSize; ++i) {
			float val = (fTableTempFloat_[i] - midpoint) * normalizationGain;
			fTables_[writeToIdx][i] = static_cast<int16_t>(std::clamp(val * 32767.0f, -32767.0f, 32767.0f));
		}

		// Install new table: flip target, audio will chase it with IIR
		// We snapped blend to currentTarget above, so now we're safe to flip
		// Audio will smoothly interpolate from old table (100%) to new table
		targetTableIndex_.store(writeToIdx, std::memory_order_release);

		// Set blend aggression directly (no smoothing needed - table crossfade handles transitions)
		blendAggression_Q8_ = newBlendAggression;

		// Compute hysteresis offset from params (15% of INT32_MAX at max hysteresis = ±320M)
		hystOffset_ = static_cast<int32_t>(params_.hysteresis * 320000000.0f);
		hystMixInfluence_Q16_ = static_cast<int32_t>(params_.hystMixInfluence * 65536.0f);

		// Compute bipolar drift intensities (Q16: ±65536 = ±1.0)
		// Positive = sag/pull toward zero, negative = boost/push away from zero
		driftMultIntensity_Q16_ = static_cast<int32_t>(params_.driftMultIntensity * 65536.0f);
		driftAddIntensity_Q16_ = static_cast<int32_t>(params_.driftAddIntensity * 65536.0f);
		// Compute stereo decorrelation offset (Q16: ±65536 = ±1.0)
		driftStereoOffset_Q16_ = static_cast<int32_t>(params_.driftStereoOffset * 65536.0f);
		// Compute subharmonic gain boost intensity (Q16: 65536 = 1.0)
		subIntensity_Q16_ = static_cast<int32_t>(params_.subIntensity * 65536.0f);
		// Store subharmonic ratio directly (int8_t, already clamped in params)
		subRatio_ = params_.subRatio;
		// Store stride directly (int32_t, already clamped in params)
		stride_ = params_.stride;
		// Compute feedback intensity (Q16: 52428 = 0.8 max for stability)
		feedback_Q16_ = static_cast<int32_t>(params_.feedback * 65536.0f);
		// Store bit rotation amount directly (int8_t, already clamped in params)
		rotation_ = params_.rotation;
		// Compute slew rate limiting intensity (Q16: 65536 = 1.0)
		slewIntensity_Q16_ = static_cast<int32_t>(params_.slewIntensity * 65536.0f);

		// Release store: ensures ALL writes (tables, params) are visible before audio sees isLinear_=false
		isLinear_.store(false, std::memory_order_release);
	}

	/// Integer table lookup - direct access to specified table buffer
	/// @param input Table input position (uint32 where 0 = -1.0, UINT32_MAX = +1.0)
	/// @param tableIdx Which table buffer to read from (0 or 1)
	/// @return Lookup value scaled by 65536 (Q16.15 format)
	[[gnu::always_inline]] int32_t lookupFunctionIntDirect(uint32_t input, int8_t tableIdx) const {
		constexpr int32_t kTableBits = (kTableSize == 128)    ? 7
		                               : (kTableSize == 256)  ? 8
		                               : (kTableSize == 512)  ? 9
		                               : (kTableSize == 1024) ? 10
		                               : (kTableSize == 2048) ? 11
		                                                      : 8; // default

		const int16_t* table = fTables_[tableIdx].data();

		// Extract table index (upper kTableBits bits of input)
		int32_t whichValue = input >> (32 - kTableBits);

		// Extract fractional part (next 16 bits after index)
		constexpr int32_t rshiftAmount = 32 - 16 - kTableBits;
		uint32_t rshifted = input >> rshiftAmount;
		int32_t strength2 = rshifted & 65535;
		int32_t strength1 = 65536 - strength2;

		// Linear interpolation with int16 table entries
		// Result is scaled by 65536 (Q16.15)
		return static_cast<int32_t>(table[whichValue]) * strength1
		       + static_cast<int32_t>(table[whichValue + 1]) * strength2;
	}

	// Integer tables for fast integer-only processing
	// Scale: float [-1,1] → int16_t [-32767,32767]
	// Double-buffer: regeneration writes to inactive buffer, then flips targetTableIndex_
	std::vector<int16_t> fTables_[2];    // Two table buffers for lock-free crossfade
	std::vector<float> fTableTempFloat_; // Temp float buffer for regeneration (avoids allocation)

	// Target-chasing crossfade: audio smoothly interpolates toward targetTableIndex_
	// This eliminates all races - we only atomically store ONE value, audio chases it
	// blend_Q15: 0 = 100% table 0, 32768 = 100% table 1
	std::atomic<int8_t> targetTableIndex_{0}; // Which table we're fading toward (0 or 1)
	mutable int32_t currentBlend_Q15_{0};     // Current blend position (Q15: 0-32768)
	// IIR alpha: α_Q15 = 4 → 99% in ~500ms at 44.1kHz (n = 4.6/α = 4.6*32768/4 = 37683 samples ≈ 854ms)
	static constexpr int32_t kBlendAlpha_Q15 = 4;

	// Parameters (consolidated struct)
	TableShaperParams params_;

	// State
	bool tablesDirty_{true};
	// Linear flag: controls whether audio thread accesses tables
	// release/acquire ordering ensures tables are visible before isLinear_ becomes false
	std::atomic<bool> isLinear_{true};

	// Blend aggression: derived from X (drive), affects mix curve sharpness
	// Q8 format: 256 = 1.0x, range [26, 512] for [0.1x, 2.0x]
	// Low X = gentle transitions, high X = snappy onset
	// Set directly during regeneration (no per-sample smoothing needed)
	int32_t blendAggression_Q8_{256};

	// Hysteresis: direction-dependent table offset
	// Computed from params_.hysteresis at table generation, scaled for int32 table space
	// ~5% of INT32_MAX at max hysteresis (±107M offset)
	int32_t hystOffset_{0};
	// Mix influence on hysteresis: how much mix knob modulates hysteresis strength
	// Q16 format: 0 = no influence, 65536 = full influence
	int32_t hystMixInfluence_Q16_{0};
	// Multiplicative drift intensity: bipolar phi triangle for sag/boost
	// Q16 format: ±65536 = ±1.0 (positive = sag, negative = boost)
	int32_t driftMultIntensity_Q16_{0};
	// Additive drift intensity: bipolar phi triangle for pull/push
	// Q16 format: ±65536 = ±1.0 (positive = pull to center, negative = push from center)
	int32_t driftAddIntensity_Q16_{0};
	// Stereo decorrelation: R channel slope multiplier offset
	// Q16 format: -65536 to +65536 (-1.0 to +1.0)
	int32_t driftStereoOffset_Q16_{0};
	// Subharmonic gain boost intensity: phi triangle modulation
	// Q16 format: 0 = no boost, 65536 = full boost (~6dB when subSign=+1)
	int32_t subIntensity_Q16_{0};
	// Subharmonic ZC ratio: how many zero crossings per sign toggle
	// 2 = octave down, 3 = twelfth, 4 = 2 octaves, 5 = 2oct+3rd, 6 = 2oct+5th
	int8_t subRatio_{2};
	// ZC detection stride: check every N samples instead of every sample
	// Lower = more accurate, higher = bass-only (also sets feedback comb freq)
	int32_t stride_{64};
	// Feedback intensity: comb filter using prevSample at stride points
	// Q16 format: 0 = disabled, 52428 = 0.8 max (stability limit)
	int32_t feedback_Q16_{0};
	// Bit rotation amount: aliasing effect via ARM ROR instruction
	// Range [0,31]: 0 = passthrough, 31 = max rotation (single-cycle)
	int8_t rotation_{0};
	// Slew rate limiting intensity: phi triangle modulation
	// Q16 format: 0 = disabled, 65536 = extreme limiting
	int32_t slewIntensity_Q16_{0};

	// Expected peak level for int32 path (set at table generation time)
	// Used to normalize input/output so the table "expects" signals at this level
	// FM at max LOCAL_VOLUME + max OSC_VOLUME = 2^26 (~67M)
	// Calculation: sine(2^31) * sourceAmplitude(2^27) / 2^32 = 2^26
	// FM signal calibration (empirically determined):
	// - Theoretical max: 2^26 = 67M (sine * sourceAmplitude / 2^32, sourceAmplitude capped at 2^27)
	// - inputScale=256 (2^8) puts saturation onset near center drive for FM at max velocity
	// - At lower velocities: need positive drive to reach saturation (natural velocity response)
	// - Output = lookup >> inputScaleShift_ (unity gain: boost in, attenuate out)
	int32_t expectedPeak_{1 << 26}; // 67,108,864 - theoretical FM max (reference only)
	int32_t inputScaleShift_{7};    // Bit shift for input scaling (7 is slightly too much, 6 slightly too little)

public:
	/// Set the expected peak level for int32 processing (integer-only, no floats)
	/// Call this when synth mode changes (FM vs subtractive)
	void setExpectedPeak(int32_t peak) {
		expectedPeak_ = peak;
		// Compute shift using CLZ + 2: extra headroom for better saturation response
		// For peak = 2^26: CLZ = 5, shift = 7 (tuned empirically)
		inputScaleShift_ = (peak > 0) ? __builtin_clz(static_cast<uint32_t>(peak)) + 2 : 7;
	}

	[[nodiscard]] int32_t getExpectedPeak() const { return expectedPeak_; }
	[[nodiscard]] int32_t getInputScaleShift() const { return inputScaleShift_; }

	/// Get hysteresis offset (for hoisting to buffer level)
	[[nodiscard]] int32_t getHystOffset() const { return hystOffset_; }
	/// Get hysteresis mix influence (for hoisting to buffer level)
	[[nodiscard]] int32_t getHystMixInfluence_Q16() const { return hystMixInfluence_Q16_; }
	/// Get multiplicative drift intensity (bipolar, for hoisting to buffer level)
	[[nodiscard]] int32_t getDriftMultIntensity_Q16() const { return driftMultIntensity_Q16_; }
	/// Get additive drift intensity (bipolar, for hoisting to buffer level)
	[[nodiscard]] int32_t getDriftAddIntensity_Q16() const { return driftAddIntensity_Q16_; }
	/// Get stereo decorrelation offset (for hoisting to buffer level)
	[[nodiscard]] int32_t getDriftStereoOffset_Q16() const { return driftStereoOffset_Q16_; }
	/// Get subharmonic gain boost intensity (for hoisting to buffer level)
	[[nodiscard]] int32_t getSubIntensity_Q16() const { return subIntensity_Q16_; }
	/// Get subharmonic ZC ratio (for hoisting to buffer level)
	[[nodiscard]] int8_t getSubRatio() const { return subRatio_; }
	/// Get ZC detection stride (for hoisting to buffer level)
	[[nodiscard]] int32_t getStride() const { return stride_; }
	/// Get feedback intensity (for hoisting to buffer level)
	[[nodiscard]] int32_t getFeedback_Q16() const { return feedback_Q16_; }
	/// Get bit rotation amount (for hoisting to buffer level)
	[[nodiscard]] int8_t getRotation() const { return rotation_; }
	/// Get slew rate limiting intensity (for hoisting to buffer level)
	[[nodiscard]] int32_t getSlewIntensity_Q16() const { return slewIntensity_Q16_; }
};

/**
 * Helper to derive shaper parameters from XY position with combinatoric sweep
 *
 * X axis maps to drive (0 = linear bypass, 127 = full saturation)
 * Y axis creates a combinatoric sweep through basis weights, threshold, asymmetry
 * using half-rectified triangle waves with different periods
 *
 * Key behaviors:
 * - Half-rectified oscillators: each basis is OFF for ~50% of its cycle, creating
 *   gaps where only a subset of the 6 bases are active (sparse combinations)
 * - Accelerating frequency: oscillations are slow at Y=0 (easy to find sweet spots)
 *   and fast at Y=1023 (chaotic exploration with more gaps)
 * - φ-power frequency ratios: using powers of the golden ratio ensures frequencies
 *   never align, producing quasi-periodic patterns with no exact repetition
 *
 * Result: distinct character zones at low Y, fragmented/chaotic at high Y
 */
struct TableShaperXYMapper {
	// =================================================================
	// Duty cycle controls the active/gap ratio of basis oscillators
	// 0.25 = 25% active, 75% gap (very sparse, distinct characters)
	// 0.5  = 50% active, 50% gap (balanced, default)
	// 0.75 = 75% active, 25% gap (more blending, smoother)
	// 1.0  = 100% active, no gaps (original continuous triangle)
	// =================================================================
	static constexpr float kPhaseWidth = 0.5f;

	/// Derive parameters from X (0-127) and Y (0-1023) with combinatoric sweep
	/// @param x X position (0-127), maps to drive (0 = linear bypass)
	/// @param y Y position (0-1023), creates high-res combinatoric parameter sweep
	/// @return TableShaperParams with all derived values
	static TableShaperParams deriveParameters(uint8_t x, uint16_t y) {
		TableShaperParams p;
		p.drive = static_cast<float>(x) / 127.0f;

		float yNorm = static_cast<float>(y) / 1023.0f;

		// Accelerating interference: slow at Y=0, fast at Y=1023
		constexpr float kAccelFactor = 3.0f;
		float freqMult = 1.0f + yNorm * yNorm * kAccelFactor;

		// 6 Basis weights with φ-power frequency ratios for quasi-periodic coverage
		p.inflatorWeight = triangleSimpleUnipolar(yNorm * phi::kPhi225 * freqMult, kPhaseWidth);
		p.polyWeight = triangleSimpleUnipolar(yNorm * phi::kPhi200 * freqMult + 0.167f, kPhaseWidth);
		p.hardKneeWeight = triangleSimpleUnipolar(yNorm * phi::kPhi175 * freqMult + 0.333f, kPhaseWidth);
		p.chebyWeight = triangleSimpleUnipolar(yNorm * phi::kPhi250 * freqMult + 0.5f, kPhaseWidth);
		p.sineFoldWeight = triangleSimpleUnipolar(yNorm * phi::kPhi150 * freqMult + 0.667f, kPhaseWidth);
		p.rectifierWeight = triangleSimpleUnipolar(yNorm * phi::kPhi125 * freqMult + 0.833f, kPhaseWidth);

		p.threshold = triangleSimpleUnipolar(yNorm * phi::kPhi275 * freqMult + 0.25f, kPhaseWidth);

		float asymFreqMult = 1.0f + yNorm * yNorm * (kAccelFactor * 0.5f);
		p.asymmetry = 0.3f + triangleSimpleUnipolar(yNorm * phi::kPhi100 * asymFreqMult, kPhaseWidth) * 0.4f;

		// Pre-expansion: X controls intensity, Y sweeps character
		// At X=0: pure limiter (no expansion), X=max: full expansion range
		// Y sweeps from limiter (Y=0) to expander (Y~512) to limiter (Y=1023)
		p.preExpandAmount = p.drive * triangleSimpleUnipolar(yNorm * 2.0f, 1.0f);

		// SPECIAL CASE: Zone 6 "Blend" (Y=768-895) → Oxford-style inflator
		// Pure inflator + soft clip (poly/tanh), X controls expansion, symmetric
		// X=0: pure limiter, X=max: full inflator expansion
		constexpr float kZone6Start = 768.0f / 1023.0f; // ~0.751
		constexpr float kZone6End = 896.0f / 1023.0f;   // ~0.876
		if (yNorm >= kZone6Start && yNorm < kZone6End) {
			p.inflatorWeight = 1.0f;
			p.polyWeight = 0.5f; // Soft tanh-like clipping
			p.hardKneeWeight = 0.0f;
			p.chebyWeight = 0.0f;
			p.sineFoldWeight = 0.0f;
			p.rectifierWeight = 0.0f;
			p.preExpandAmount = p.drive; // X controls expansion: 0=limiter, 1=full inflator
			p.asymmetry = 0.5f;          // Symmetric (Oxford-style)
		}

		return p;
	}

	/// Derive parameters with phase offsets for secret knob integration
	/// @param x X position (0-127)
	/// @param y Y position (0-1023)
	/// @param gammaPhase Phase offset for parameter interference (from secret knob)
	/// @param periodScale Period scaling for parameter sweep rate
	/// @return TableShaperParams with all derived values
	///
	/// DESIGN NOTE: Phase Offset Scope
	/// ===============================
	/// Currently, gammaPhase rotates TWO levels of parameters:
	///   1. Algorithm superposition weights (which basis functions are active)
	///      - polyWeight, hardKneeWeight, chebyWeight, sineFoldWeight, rectifierWeight
	///      - phMult values: 0.167, 0.333, 0.5, 0.667, 0.833
	///   2. Internal algorithm parameters (how each basis behaves)
	///      - threshold (phMult: 0.25), asymmetry (phMult: 0.618)
	///
	/// Note: inflatorWeight has phMult=0 so it serves as an anchor (always present)
	///
	/// ALTERNATIVE: Only rotate internal parameters
	/// If zone names should remain semantically stable (Y=0 always "Warm", Y=512 always "Fold"),
	/// we could set phMult=0 for all 6 basis weights. This would make:
	///   - Y axis: determines WHICH algorithms are blended (zone identity)
	///   - Secret knob: tunes HOW those algorithms behave (character within zone)
	///
	/// Current behavior: Secret knob morphs both identity AND character, creating
	/// continuous exploration where zone names are approximate guides rather than
	/// fixed definitions. This is more "sound design-y" but less predictable.
	///
	static TableShaperParams deriveParametersWithPhase(uint8_t x, uint16_t y, float gammaPhase, float periodScale,
	                                                   float oscHarmonicWeight = 0.5f) {
		TableShaperParams p;
		p.drive = static_cast<float>(x) / 127.0f;

		float yNorm = static_cast<float>(y) / 1023.0f;

		constexpr float kAccelFactor = 3.0f;
		float freqMult = 1.0f + yNorm * yNorm * kAccelFactor;

		// Use double precision to preserve phase accuracy at large gammaPhase values (< 10^15 ok)
		// Pre-wrap phase offsets at different φ frequencies (like disperser/sine_shaper/multiband)
		double ph = static_cast<double>(gammaPhase);
		float ph225 = phi::wrapPhase(ph * phi::kPhi225);
		float ph200 = phi::wrapPhase(ph * phi::kPhi200);
		float ph175 = phi::wrapPhase(ph * phi::kPhi175);
		float ph250 = phi::wrapPhase(ph * phi::kPhi250);
		float ph150 = phi::wrapPhase(ph * phi::kPhi150);
		float ph125 = phi::wrapPhase(ph * phi::kPhi125);
		float ph275 = phi::wrapPhase(ph * phi::kPhi275);
		float ph100 = phi::wrapPhase(ph * phi::kPhi100);

		// Deadzone phi triangles - use slower frequencies for smooth evolution
		// Initial offset 0.75 places width in dead region [0.5,1) at gammaPhase=0
		float phDzWidth = phi::wrapPhase(ph * phi::kPhiN050); // φ^-0.5 (slower)
		float phDzPhase = phi::wrapPhase(ph * phi::kPhi033);  // φ^0.33

		// Apply phase offsets and period scaling for interference patterns (φ-power frequencies)
		// Each parameter rotates at its own irrational rate - no alignments possible
		auto base = [yNorm, freqMult, periodScale](float freq) {
			return static_cast<float>(static_cast<double>(yNorm) * freq * freqMult * periodScale);
		};

		// Fixed phase offsets (0.167, 0.333, etc.) spread basis functions across Y axis
		// These match deriveParameters() so gammaPhase=0 produces identical results
		p.inflatorWeight = triangleSimpleUnipolar(phi::wrapPhase(base(phi::kPhi225) + ph225), kPhaseWidth);
		p.polyWeight = triangleSimpleUnipolar(phi::wrapPhase(base(phi::kPhi200) + ph200 + 0.167f), kPhaseWidth);
		p.hardKneeWeight = triangleSimpleUnipolar(phi::wrapPhase(base(phi::kPhi175) + ph175 + 0.333f), kPhaseWidth);
		p.chebyWeight = triangleSimpleUnipolar(phi::wrapPhase(base(phi::kPhi250) + ph250 + 0.5f), kPhaseWidth);
		p.sineFoldWeight = triangleSimpleUnipolar(phi::wrapPhase(base(phi::kPhi150) + ph150 + 0.667f), kPhaseWidth);
		p.rectifierWeight = triangleSimpleUnipolar(phi::wrapPhase(base(phi::kPhi125) + ph125 + 0.833f), kPhaseWidth);

		p.threshold = triangleSimpleUnipolar(phi::wrapPhase(base(phi::kPhi275) + ph275 + 0.25f), kPhaseWidth);

		float asymFreqMult = 1.0f + yNorm * yNorm * (kAccelFactor * 0.5f);
		float asymBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhi100 * asymFreqMult * periodScale);
		p.asymmetry = 0.3f + triangleSimpleUnipolar(phi::wrapPhase(asymBase + ph100), kPhaseWidth) * 0.4f;

		// Deadzone modifier: completely disabled at gammaPhase=0, oscillates as secret knob increases
		// dzEnable gates the entire deadzone feature off when phase offset is zero
		constexpr float kDeadzoneDuty = 0.2f;
		float dzEnable = (gammaPhase != 0.0f) ? 1.0f : 0.0f;
		float dzWidthBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhiN050 * freqMult * periodScale);
		p.deadzoneWidth = dzEnable * triangleSimpleUnipolar(phi::wrapPhase(dzWidthBase + phDzWidth), kDeadzoneDuty);

		// Phase center oscillates freely (doesn't need to start inactive)
		// 30% duty cycle + squared result → spends most time near 0, occasional excursions to 1
		constexpr float kDeadzonePhaseDuty = 0.30f;
		float dzPhaseBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhi033 * freqMult * periodScale);
		float dzPhaseRaw = triangleSimpleUnipolar(phi::wrapPhase(dzPhaseBase + phDzPhase), kDeadzonePhaseDuty);
		p.deadzonePhase = dzPhaseRaw * dzPhaseRaw; // Square to spend less time near 1

		// Hysteresis: direction-dependent transfer offset
		// 50% duty (full triangle) for smooth sweep across bipolar range [-1, +1]
		// φ^0.67 frequency sits between deadzone's slow and basis weights' fast
		constexpr float kHysteresisDuty = 1.0f; // Full triangle = 50% effective duty
		float phHyst = phi::wrapPhase(ph * phi::kPhi067);
		float hystBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhi067 * freqMult * periodScale);
		float hystRaw = triangleSimpleUnipolar(phi::wrapPhase(hystBase + phHyst), kHysteresisDuty);
		p.hysteresis = dzEnable * (hystRaw * 2.0f - 1.0f); // Map [0,1] → [-1,1], gated by phase offset

		// Mix influence on hysteresis: how much the mix knob modulates hysteresis strength
		// 50% duty, φ^0.5 frequency for different evolution rate than hysteresis itself
		float phHystMix = phi::wrapPhase(ph * phi::kPhi050);
		float hystMixBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhi050 * freqMult * periodScale);
		p.hystMixInfluence = dzEnable * triangleSimpleUnipolar(phi::wrapPhase(hystMixBase + phHystMix), 0.5f);

		// Multiplicative drift: bipolar phi triangle for sag/boost
		// 40% duty for sparse sweet spots, φ^4.0 frequency
		// Positive = sag toward zero (capacitor discharge), negative = boost away from zero
		constexpr float kDriftDuty = 0.4f;
		float phDriftMult = phi::wrapPhase(ph * phi::kPhi400);
		float driftMultBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhi400 * freqMult * periodScale);
		float driftMultTri = triangleSimpleUnipolar(phi::wrapPhase(driftMultBase + phDriftMult), kDriftDuty);
		p.driftMultIntensity = dzEnable * (driftMultTri * 2.0f - 1.0f); // Map [0,1] → [-1,1]

		// Additive drift: bipolar phi triangle for pull/push (uncorrelated frequency)
		// 40% duty, φ^-1 frequency (decorrelated from multiplicative's φ^4)
		// Positive = pull toward center, negative = push from center
		float phDriftAdd = phi::wrapPhase(ph * phi::kPhiN100);
		float driftAddBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhiN100 * freqMult * periodScale);
		float driftAddTri = triangleSimpleUnipolar(phi::wrapPhase(driftAddBase + phDriftAdd + 0.5f), kDriftDuty);
		p.driftAddIntensity = dzEnable * (driftAddTri * 2.0f - 1.0f); // Map [0,1] → [-1,1]

		// Stereo correlation: controls how correlated L/R random walks are
		// |offset| = 0: fully correlated (R follows L), higher = more independent
		// φ^2.5 frequency for different evolution rate than intensity
		// Max ±30% decorrelation to keep stereo image coherent
		float phStereo = phi::wrapPhase(ph * phi::kPhi250);
		float stereoBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhi250 * freqMult * periodScale);
		float stereoTri = triangleSimpleUnipolar(phi::wrapPhase(stereoBase + phStereo), 0.5f);
		p.driftStereoOffset = dzEnable * (stereoTri * 0.6f - 0.3f); // Map [0,1] → [-0.3,+0.3], gated by phase offset

		// Subharmonic gain boost intensity: phi triangle with ~8 cycles across Y range
		// 30% duty for sparser activation, linear response
		// φ^3.5 frequency for unique evolution rate
		constexpr float kSubDuty = 0.3f;
		float phSub = phi::wrapPhase(ph * phi::kPhi350);
		float subBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhi350 * freqMult * periodScale);
		float subTri = triangleSimpleUnipolar(phi::wrapPhase(subBase + phSub), kSubDuty);
		p.subIntensity = dzEnable * subTri; // Linear response, gated by phase offset

		// Extras bank: additional cheap effects controlled by phi triangles
		// Uses PhiTriContext for consistent evaluation pattern
		phi::PhiTriContext extrasCtx{yNorm, freqMult, periodScale, gammaPhase};
		auto extras = extrasCtx.evalBank(phi::kExtrasBank, dzEnable);
		p.subRatio = phi::subRatioFromTriangle(extras[0]); // [0] = sub ratio selector
		p.stride = phi::strideFromTriangle(extras[1]);     // [1] = ZC stride
		p.feedback = phi::feedbackFromTriangle(extras[2]); // [2] = comb filter intensity
		p.rotation = phi::rotationFromTriangle(extras[3]); // [3] = bit rotation amount

		// Slew intensity: unipolar [0,1], bit selection (LPF vs integrator) via extrasMask
		// Duty cycle = oscHarmonicWeight + 0.2 (clamped to 1.0):
		// - sine (0.0) → 20% duty (minimal activation, already smooth)
		// - saw (0.5) → 70% duty (moderate)
		// - square (1.0) → 100% duty (always active - reliable detection)
		// φ^1.75 frequency (uncorrelated with others)
		// Enable for gammaPhase > 0 OR high harmonic content (square waves always)
		float slewEnable = (gammaPhase != 0.0f || oscHarmonicWeight >= 0.8f) ? 1.0f : 0.0f;
		float slewDuty = std::min(1.0f, oscHarmonicWeight + 0.2f); // Range [0.2, 1.0]
		float phSlew = phi::wrapPhase(ph * phi::kPhi175);
		float slewBase = static_cast<float>(static_cast<double>(yNorm) * phi::kPhi175 * freqMult * periodScale);
		// Unipolar triangle [0,1]: intensity for whichever slew mode is enabled by bit
		float slewTri = triangleSimpleUnipolar(phi::wrapPhase(slewBase + phSlew), slewDuty);
		p.slewIntensity = slewEnable * slewTri;

		// Pre-expansion: X controls intensity, Y+phase sweep character
		// NOT gated by dzEnable - works at gammaPhase=0 for vanilla expander/limiter zone
		// At X=0: pure limiter (no expansion), X=max: full expansion range
		// Y+phase sweeps expansion position within X-controlled intensity
		float phPreExp = phi::wrapPhase(ph * phi::kPhi050);
		float preExpBase = static_cast<float>(static_cast<double>(yNorm) * 2.0f * periodScale); // 2 cycles across Y
		p.preExpandAmount = p.drive * triangleSimpleUnipolar(phi::wrapPhase(preExpBase + phPreExp), 1.0f);

		// SPECIAL CASE: Zone 6 "Blend" at gammaPhase=0 → Oxford-style inflator
		// Only applies when secret knob is at zero (vanilla mode)
		// Pure inflator + soft clip, X controls expansion, symmetric
		if (gammaPhase == 0.0f) {
			constexpr float kZone6Start = 768.0f / 1023.0f;
			constexpr float kZone6End = 896.0f / 1023.0f;
			if (yNorm >= kZone6Start && yNorm < kZone6End) {
				p.inflatorWeight = 1.0f;
				p.polyWeight = 0.5f;
				p.hardKneeWeight = 0.0f;
				p.chebyWeight = 0.0f;
				p.sineFoldWeight = 0.0f;
				p.rectifierWeight = 0.0f;
				p.preExpandAmount = p.drive; // X controls expansion: 0=limiter, 1=full inflator
				p.asymmetry = 0.5f;
			}
		}

		return p;
	}
};

} // namespace deluge::dsp
