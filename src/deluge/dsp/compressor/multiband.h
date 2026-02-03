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
#include "dsp/filter/allpass_crossover.h"
#include "dsp/filter/ladder_components.h"
#include "dsp/filter/lr_crossover.h"
#include "dsp/phi_triangle.hpp"
#include "dsp/stereo_sample.h"
#include "dsp/util.hpp"
#include "dsp/zone_param.hpp" // For computeZoneQ31
#include "io/debug/fx_benchmark.h"
#include "io/debug/print.h"
#include "modulation/params/param.h"
#include "storage/field_serialization.h"
#include "util/fixedpoint.h"
#include "util/functions.h"
#include <array>
#include <cmath>
#include <span>

// FX Benchmarking: Use -DENABLE_FX_BENCHMARK=ON in cmake to profile multiband compressor
// Outputs JSON cycle counts for crossover, envelope, and recombine stages via sysex

namespace deluge::dsp {

// ============================================================================
// Pre-shifted Gain for Efficient Fixed-Point Multiplication
// ============================================================================
// Q9.22 Fixed-Point Gain Representation
// ============================================================================
// Gains stored as q9.22: 9 integer bits (0-512), 22 fractional bits
// Max gain: 512x (+54dB), min useful: ~0.00000024x (-132dB)
// Unity gain (1.0) = 2^22 = 4194304

/// Q9.22 gain constants
static constexpr int32_t kGainQ22Unity = 1 << 22;               // 1.0x = 4194304
static constexpr int32_t kGainQ22MaxBand = 256 * kGainQ22Unity; // 256x max for band gain (+48dB)
static constexpr int32_t kGainQ22MaxOutput = 8 * kGainQ22Unity; // 8x max for output gain (+18dB)
// Post-multiply shift to convert from intermediate to q31 output
// vqdmulhq_s32 gives (a*b) >> 31, we have q31*q9.22 = 2^53, so result is 2^22
// We want 2^31, so shift left by 9
static constexpr int32_t kGainQ22Shift = 9;

/// Convert float gain to q9.22 fixed-point, clamped to max
/// @param gain Float gain value
/// @param maxGain Maximum allowed gain in q9.22 (for clamping)
/// @return Gain in q9.22 format
[[gnu::always_inline]] inline int32_t floatToGainQ22(float gain, int32_t maxGain = kGainQ22MaxBand) {
	if (gain <= 0.0f) {
		return 0;
	}
	int32_t result = static_cast<int32_t>(gain * static_cast<float>(kGainQ22Unity));
	return std::min(result, maxGain);
}

/// Apply q9.22 gain to a q31 sample with saturation (scalar version)
/// @param sample Input sample in q31
/// @param gainQ22 Gain in q9.22 format
/// @return Gained sample, saturated to q31 range
[[gnu::always_inline]] inline q31_t applyGainQ22(q31_t sample, int32_t gainQ22) {
	// multiply_32x32_rshift32 gives (sample * gain) >> 32, then << 1 to match vqdmulhq
	// Result is (q31 * q9.22) >> 31 = 2^22, shift left by 9 to get q31
	q31_t scaled = multiply_32x32_rshift32(sample, gainQ22) << 1;
	return lshiftAndSaturate<kGainQ22Shift>(scaled);
}

// ============================================================================
// NEON-Vectorized Gain Application (4 samples in parallel)
// ============================================================================

/// Apply q9.22 gain to 4 q31 samples using NEON SIMD
/// @param samples 4 input samples as NEON vector
/// @param gainVec Gain broadcast to all lanes (q9.22 format)
/// @param shiftVec Pre-computed shift vector (vdupq_n_s32(kGainQ22Shift))
/// @return 4 gained samples, saturated to q31 range
[[gnu::always_inline]] inline int32x4_t applyGainQ22Neon(int32x4_t samples, int32x4_t gainVec, int32x4_t shiftVec) {
	// vqdmulhq_s32 gives (a*b*2) >> 32 = (a*b) >> 31
	// For q31 sample * q9.22 gain: result is sample*gain * 2^53 >> 31 = sample*gain * 2^22
	// We want q31 output (sample*gain * 2^31), so shift left by 9
	int32x4_t scaled = vqdmulhq_s32(samples, gainVec);
	return vqshlq_s32(scaled, shiftVec);
}

// Keep legacy ShiftedGain for any remaining uses (can be removed later)
struct ShiftedGain {
	q31_t mantissa;
	int8_t shift;
};

[[gnu::always_inline]] inline ShiftedGain floatToShiftedGain(float gain) {
	if (gain <= 0.0f) {
		return {0, 0};
	}
	int8_t shift = 0;
	float normalized = gain;
	while (normalized >= 1.0f && shift < 6) {
		normalized *= 0.5f;
		shift++;
	}
	while (normalized < 0.5f && shift > -5) {
		normalized *= 2.0f;
		shift--;
	}
	return {static_cast<q31_t>(normalized * static_cast<float>(ONE_Q31)), shift};
}

[[gnu::always_inline]] inline q31_t applyShiftedGain(q31_t sample, ShiftedGain gain) {
	q31_t scaled = multiply_32x32_rshift32(sample, gain.mantissa) << 1;
	if (gain.shift > 0) {
		return lshiftAndSaturateUnknown(scaled, static_cast<uint8_t>(gain.shift));
	}
	return (gain.shift < 0) ? (scaled >> (-gain.shift)) : scaled;
}

[[gnu::always_inline]] inline int32x4_t applyShiftedGainNeon(int32x4_t samples, int32x4_t mantissa, int8_t shift) {
	int32x4_t scaled = vqdmulhq_s32(samples, mantissa);
	if (shift > 0) {
		return vqshlq_s32(scaled, vdupq_n_s32(shift));
	}
	return (shift < 0) ? vshlq_s32(scaled, vdupq_n_s32(shift)) : scaled;
}

/// A single-band compressor with both upward and downward compression (OTT-style).
/// Designed to be used as part of a multiband compressor.
class BandCompressor {
public:
	BandCompressor() = default;

	/// Configure the compressor parameters
	/// @param attack Attack time (0 to ONE_Q31)
	/// @param release Release time (0 to ONE_Q31)
	/// @param thresholdDown Threshold for downward compression (0 to ONE_Q31)
	/// @param thresholdUp Threshold for upward compression (0 to ONE_Q31)
	/// @param ratioDown Downward compression ratio (0 to ONE_Q31)
	/// @param ratioUp Upward compression ratio (0 to ONE_Q31)
	void setup(q31_t attack, q31_t release, q31_t thresholdDown, q31_t thresholdUp, q31_t ratioDown, q31_t ratioUp) {
		setAttack(attack);
		setRelease(release);
		setThresholdDown(thresholdDown);
		setThresholdUp(thresholdUp);
		setRatioDown(ratioDown);
		setRatioUp(ratioUp);
	}

	void setAttack(q31_t attack) {
		attackKnob_ = attack;
		// Map 0-ONE_Q31 to 0.5ms - 100ms (exponential curve)
		attackMS_ = 0.5f + (fastExp(2.0f * float(attack) / ONE_Q31f) - 1.0f) * 15.0f;
		attack_ = (-1000.0f / kSampleRate) / attackMS_;
	}

	void setRelease(q31_t release) {
		releaseKnob_ = release;
		// Map 0-ONE_Q31 to 5ms - 500ms (exponential curve)
		releaseMS_ = 5.0f + (fastExp(2.0f * float(release) / ONE_Q31f) - 1.0f) * 75.0f;
		release_ = (-1000.0f / kSampleRate) / releaseMS_;
	}

	void setThresholdDown(q31_t t) {
		thresholdDownKnob_ = t;
		// Map 0-ONE_Q31 to 0.2-1.0 (conventional: knob up = higher threshold = less compression)
		// Higher thresholdDown_ = higher internal threshold = signal must be louder to trigger compression
		thresholdDown_ = 0.2f + 0.8f * (float(t) / ONE_Q31f);
	}

	void setThresholdUp(q31_t t) {
		thresholdUpKnob_ = t;
		thresholdUp_ = 0.2f + 0.8f * (float(t) / ONE_Q31f);
	}

	void setRatioDown(q31_t r) {
		ratioDownKnob_ = r;
		// Map 0-ONE_Q31 to 0-1.0 (0 = no compression / 1:1 ratio, 1 = full limiting)
		fractionDown_ = float(r) / ONE_Q31f;
	}

	void setRatioUp(q31_t r) {
		ratioUpKnob_ = r;
		// Map 0-ONE_Q31 to 0-1.0 with power curve for balanced feel
		// Quiet signals are typically 3-4x further below threshold than loud signals are above,
		// so x^3 curve compensates: at 50% knob, fractionUp=0.125 vs fractionDown=0.5
		float linear = float(r) / ONE_Q31f;
		fractionUp_ = linear * linear * linear; // x^3 curve
	}

	/// Set per-band output level (0 to ONE_Q31) - applied after compression
	/// CCW = -inf, 12:00 = 0dB, CW = +20dB
	/// This controls the mix balance of bands (like OTT's L/M/H sliders)
	void setOutputLevel(q31_t g) {
		outputLevelKnob_ = g;
		float normalized = float(g) / ONE_Q31f;
		if (normalized <= 0.5f) {
			// 0 to 0.5 maps to 0x to 1.0x (-inf to 0dB)
			outputLevel_ = normalized * 2.0f;
		}
		else {
			// 0.5 to 1.0 maps to 1.0x to 10.0x (0dB to +20dB)
			outputLevel_ = 1.0f + (normalized - 0.5f) * 2.0f * 9.0f;
		}
	}

	/// Set bandwidth (gap between up and down thresholds)
	/// When BW=0, thresholds are equal. When BW=max, maximum gap.
	void setBandwidth(q31_t bw) {
		bandwidthKnob_ = bw;
		// Bandwidth as a fraction of available headroom (0 to 0.6)
		float bandwidthFraction = 0.6f * (float(bw) / ONE_Q31f);
		// thresholdUp = thresholdDown + bandwidth offset
		// This creates a "dead zone" where no compression happens
		thresholdUp_ = std::min(1.0f, thresholdDown_ + bandwidthFraction);
	}

	[[nodiscard]] q31_t getAttack() const { return attackKnob_; }
	[[nodiscard]] q31_t getRelease() const { return releaseKnob_; }
	[[nodiscard]] float getAttackMS() const { return attackMS_; }
	[[nodiscard]] float getReleaseMS() const { return releaseMS_; }
	[[nodiscard]] q31_t getThresholdDown() const { return thresholdDownKnob_; }
	[[nodiscard]] q31_t getThresholdUp() const { return thresholdUpKnob_; }
	[[nodiscard]] q31_t getRatioDown() const { return ratioDownKnob_; }
	[[nodiscard]] q31_t getRatioUp() const { return ratioUpKnob_; }
	[[nodiscard]] q31_t getOutputLevel() const { return outputLevelKnob_; }
	[[nodiscard]] q31_t getBandwidth() const { return bandwidthKnob_; }
	[[nodiscard]] float getOutputLevelLinear() const { return outputLevel_; }

	/// Get bandwidth for display in dB (0-36dB range)
	/// This represents the "dead zone" gap between up and down thresholds
	[[nodiscard]] float getBandwidthForDisplay() const {
		// Bandwidth fraction 0-0.6 maps to approximately 0-36dB
		// (assuming ~60dB reference dynamic range)
		float bandwidthFraction = 0.6f * (float(bandwidthKnob_) / ONE_Q31f);
		return bandwidthFraction * 60.0f; // 0-36dB
	}

	/// Get threshold for display in dB (approximately -60dB to -12dB range)
	/// Lower values = more aggressive compression (compresses quieter signals)
	[[nodiscard]] float getThresholdForDisplay() const {
		// thresholdDown_ is 0.2-1.0, representing fraction of dynamic range
		// Map 0.2-1.0 to -60dB to -12dB (conventional: lower threshold = more compression)
		// When thresholdDown_=0.2 (knob low), display -60dB (more compression)
		// When thresholdDown_=1.0 (knob high), display -12dB (less compression)
		constexpr float minDB = -60.0f;
		constexpr float maxDB = -12.0f;
		float normalized = (thresholdDown_ - 0.2f) / 0.8f; // 0 to 1
		return minDB + normalized * (maxDB - minDB);       // -60dB to -12dB
	}

	/// Get ratio for display in x:1 format (1.0 = no compression, higher = more compression)
	[[nodiscard]] float getRatioForDisplay() const {
		// fractionDown_ is 0-1, where 0 = no compression, 1 = limiting
		// Convert to ratio: ratio = 1 / (1 - fraction), clamped
		if (fractionDown_ >= 0.99f) {
			return 100.0f; // Essentially limiting
		}
		return 1.0f / (1.0f - fractionDown_);
	}

	/// Reset the compressor state
	void reset() {
		envelope_ = 0.0f;
		rms_ = 0.0f;
		level_ = 0.0f;
		lastFrameCount_ = 0;
	}

	/// Calculate gain adjustment for the band based on current RMS level.
	/// Returns a linear gain multiplier.
	/// @param numSamples Number of samples in the current buffer
	/// @param songVolumedB Reference volume level in dB (log scale)
	/// @param knee Soft knee amount (0 = hard, 1 = soft, affects transition around threshold)
	/// @param skew Up/down balance (-1 = upward only, 0 = balanced, +1 = downward only)
	/// @param frameCount Global frame counter for gap detection
	[[nodiscard]] float calculateGain(float numSamples, float songVolumedB, float knee = 0.0f, float skew = 0.0f,
	                                  uint32_t frameCount = 0) {
		// Detect gap in processing - if more than 1 frame since last call,
		// there was a pause. Calculate natural decay over the gap.
		uint32_t previousFrameCount = lastFrameCount_;
		lastFrameCount_ = frameCount;
		bool gapDetected = (frameCount > 0) && (previousFrameCount > 0) && (frameCount > previousFrameCount + 1);

		float threshDowndB = songVolumedB * thresholdDown_;
		float threshUpdB = songVolumedB * thresholdUp_;

		// Calculate how far above/below thresholds we are
		float diffDown = rms_ - threshDowndB;
		float diffUp = threshUpdB - rms_;

		// Knee width in dB (0 = hard knee, up to 12dB soft knee)
		// Pre-compute reciprocal to avoid division in knee calculations
		float kneeWidthdB = knee * 12.0f;
		float halfKnee = kneeWidthdB * 0.5f;
		float invTwoKnee = (kneeWidthdB > 0.1f) ? (0.5f / kneeWidthdB) : 0.0f;

		// Downward compression with soft knee
		float over;
		if (kneeWidthdB < 0.1f || diffDown > halfKnee) {
			// Hard knee or fully above knee region
			over = std::max(0.0f, diffDown);
		}
		else if (diffDown > -halfKnee) {
			// In soft knee transition region - quadratic interpolation
			float x = diffDown + halfKnee; // 0 to kneeWidth
			over = x * x * invTwoKnee;
		}
		else {
			// Below knee region
			over = 0.0f;
		}

		// Upward compression with soft knee
		float under;
		if (kneeWidthdB < 0.1f || diffUp > halfKnee) {
			// Hard knee or fully below knee region
			under = std::max(0.0f, diffUp);
		}
		else if (diffUp > -halfKnee) {
			// In soft knee transition region
			float x = diffUp + halfKnee;
			under = x * x * invTwoKnee;
		}
		else {
			// Above knee region
			under = 0.0f;
		}

		// Apply up/down skew to balance compression types
		// skew = -1: upward only, skew = 0: balanced, skew = +1: downward only
		float upwardFactor = std::clamp(1.0f - skew, 0.0f, 1.0f);
		float downwardFactor = std::clamp(1.0f + skew, 0.0f, 1.0f);

		// Combined target with skew applied
		float target = -over * fractionDown_ * downwardFactor + under * fractionUp_ * upwardFactor;

		// Run envelope follower - if gap detected, calculate natural decay
		if (gapDetected) {
			// Processing just resumed after a pause
			// Calculate how many samples elapsed during the gap
			uint32_t gapFrames = frameCount - previousFrameCount - 1;
			float gapSamples = static_cast<float>(gapFrames) * numSamples;

			// During silence, target would be 0 (no compression needed)
			// Decay envelope toward 0 using release time constant
			// This simulates what would have happened if we processed silence
			envelope_ = envelope_ * fastExp(release_ * gapSamples);

			// Reset level tracking since we have no history of what happened during gap
			level_ = 0.0f;
			rms_ = 0.0f;
		}
		else {
			envelope_ = runEnvelope(envelope_, target, numSamples);
		}

		// Convert to linear gain, clamped to prevent overflow
		// Range: -20dB (0.1x) to +30dB (31.6x) for aggressive upward compression
		float gain = fastExp(envelope_);
		return std::clamp(gain, 0.1f, 31.6f);
	}

	/// Update the level from stereo band buffers (NEON-optimized)
	/// @param bufferL Left channel samples
	/// @param bufferR Right channel samples
	/// @param numSamples Number of samples in each buffer
	/// @param alpha Pre-computed IIR alpha (hoisted from response calculation)
	/// @param oneMinusAlpha Pre-computed (1-alpha)
	/// @param useAvg Use average instead of max for less stereo linking
	/// @param stride Sample stride (1=every sample, 2=every other). Use 1 for high freq bands.
	/// Optimized: NEON SIMD for 4x throughput, alpha hoisted out of audio loop
	void updateLevel(const q31_t* bufferL, const q31_t* bufferR, size_t numSamples, float alpha, float oneMinusAlpha,
	                 bool useAvg, int32_t stride = 1) {
		q31_t peak = 0;

		// NEON peak detection - process 4 samples at a time
		// Stride affects step size: stride=1 processes all, stride=2 skips every other group
		const size_t step = static_cast<size_t>(4 * stride);
		const size_t vectorLen = (numSamples / step) * step;
		int32x4_t peakVec = vdupq_n_s32(0);

		if (useAvg) {
			// Average mode: (|L| + |R|) / 2 for less aggressive stereo linking
			for (size_t i = 0; i < vectorLen; i += step) {
				int32x4_t L = vld1q_s32(&bufferL[i]);
				int32x4_t R = vld1q_s32(&bufferR[i]);
				L = vabsq_s32(L);
				R = vabsq_s32(R);
				int32x4_t avg = vhaddq_s32(L, R); // Halving add = (L + R) / 2
				peakVec = vmaxq_s32(peakVec, avg);
			}
		}
		else {
			// Max mode: max(|L|, |R|) for tighter compression control
			for (size_t i = 0; i < vectorLen; i += step) {
				int32x4_t L = vld1q_s32(&bufferL[i]);
				int32x4_t R = vld1q_s32(&bufferR[i]);
				L = vabsq_s32(L);
				R = vabsq_s32(R);
				int32x4_t maxLR = vmaxq_s32(L, R);
				peakVec = vmaxq_s32(peakVec, maxLR);
			}
		}

		// Horizontal max reduction (ARMv7-compatible)
		int32x2_t peak2 = vmax_s32(vget_low_s32(peakVec), vget_high_s32(peakVec));
		peak2 = vpmax_s32(peak2, peak2);
		peak = vget_lane_s32(peak2, 0);

		// Handle remainder samples (scalar fallback)
		for (size_t i = vectorLen; i < numSamples; ++i) {
			q31_t L = bufferL[i];
			q31_t R = bufferR[i];
			L = (L < 0) ? -L : L;
			R = (R < 0) ? -R : R;
			q31_t s = useAvg ? ((L >> 1) + (R >> 1)) : ((L > R) ? L : R);
			peak = (s > peak) ? s : peak;
		}

		// Float IIR smoothing with pre-computed alpha: level = level * (1-alpha) + peak * alpha
		float peakF = static_cast<float>(peak);
		level_ = level_ * oneMinusAlpha + peakF * alpha;

		// Convert to log domain for threshold comparison (used by calculateGain)
		rms_ = fastLog(std::max(level_, 1.0f));
	}

	/// Get current gain reduction in dB (for metering)
	[[nodiscard]] float getGainReductionDB() const { return envelope_; }

	/// Get current input level in log domain (for metering)
	[[nodiscard]] float getInputLevelLog() const { return rms_; }

	/// Get threshold in log domain (for metering tick marks)
	[[nodiscard]] float getThresholdLog() const { return thresholdDown_; }

private:
	[[nodiscard]] float runEnvelope(float current, float target, float numSamples) const {
		// Attack = envelope moving AWAY from unity (0) - compression/expansion starting
		// Release = envelope moving TOWARD unity (0) - compression/expansion ending
		// This works correctly for both downward (negative envelope) and upward (positive envelope)
		bool movingAwayFromUnity = std::abs(target) > std::abs(current);
		float timeConstant = movingAwayFromUnity ? attack_ : release_;
		return target + fastExp(timeConstant * numSamples) * (current - target);
	}

	float attack_ = -1000.0f / kSampleRate;
	float release_ = -1000.0f / kSampleRate;
	float attackMS_ = 1.0f;
	float releaseMS_ = 100.0f;
	float thresholdDown_ = 0.8f;
	float thresholdUp_ = 0.5f;
	float fractionDown_ = 0.5f;
	float fractionUp_ = 0.5f;
	float envelope_ = 0.0f;
	float rms_ = 0.0f;
	float level_ = 0.0f;
	uint32_t lastFrameCount_ = 0; // For gap detection

	q31_t attackKnob_ = ONE_Q31 / 4;  // Default ~10ms
	q31_t releaseKnob_ = ONE_Q31 / 4; // Default ~100ms
	q31_t thresholdDownKnob_ = 0;
	q31_t thresholdUpKnob_ = 0;
	q31_t ratioDownKnob_ = 0;
	q31_t ratioUpKnob_ = 0;
	q31_t outputLevelKnob_ = ONE_Q31 / 2; // Default 0dB (unity) - 12:00 position
	q31_t bandwidthKnob_ = ONE_Q31 / 2;   // Default medium bandwidth
	float outputLevel_ = 1.0f;            // Linear output level multiplier (matches knob default)
};

/// Character zone names for display
enum class CharacterZone : uint8_t {
	Width = 0,  // Stereo width variations
	Timing = 1, // Per-band timing offsets
	Skew = 2,   // Per-band up/down skew
	Punch = 3,  // Fast attack, transient emphasis
	Air = 4,    // High frequency emphasis
	Rich = 5,   // Upward compression focus
	OTT = 6,    // Classic OTT character
	OWLTT = 7   // Extreme/experimental - OTT cranked to 11
};

/// Vibe zone names for display - controls phase relationships between oscillations
enum class VibeZone : uint8_t {
	Sync = 0,    // All oscillations in phase
	Spread = 1,  // Evenly spread phases (120° apart)
	Pairs = 2,   // Band pairs in/out of phase
	Cascade = 3, // Progressive phase shift across bands
	Invert = 4,  // Opposite phases between parameters
	Pulse = 5,   // Clustered phases creating pulses
	Drift = 6,   // Slowly varying phase relationships
	Chaos = 7    // Rapidly oscillating phase offsets
};

/// 3-band multiband compressor with OTT-style upward/downward compression.
/// Uses allpass crossover for perfect phase-coherent band splitting.
class MultibandCompressor {
public:
	static constexpr int kNumBands = 3;
	/// Zone counts derived from param definitions (single source of truth)
	static constexpr int kNumCharacterZones =
	    modulation::params::getZoneParamInfo(modulation::params::UNPATCHED_MB_COMPRESSOR_CHARACTER).zoneCount;
	static constexpr int kNumVibeZones =
	    modulation::params::getZoneParamInfo(modulation::params::UNPATCHED_MB_COMPRESSOR_VIBE).zoneCount;

	enum class Band : uint8_t { Low = 0, Mid = 1, High = 2 };

	MultibandCompressor() {
		// Default OTT-style settings - initialize all crossover types
		crossoverAllpass1_.setLowCrossover(200.0f);
		crossoverAllpass1_.setHighCrossover(2000.0f);
		crossoverAllpass2_.setLowCrossover(200.0f);
		crossoverAllpass2_.setHighCrossover(2000.0f);
		crossoverAllpass3_.setLowCrossover(200.0f);
		crossoverAllpass3_.setHighCrossover(2000.0f);
		crossoverLR2_.setLowCrossover(200.0f);
		crossoverLR2_.setHighCrossover(2000.0f);
		crossoverLR2Fast_.setLowCrossover(200.0f);
		crossoverLR2Fast_.setHighCrossover(2000.0f);
		crossoverLR4_.setLowCrossover(200.0f);
		crossoverLR4_.setHighCrossover(2000.0f);
		crossoverLR4Fast_.setLowCrossover(200.0f);
		crossoverLR4Fast_.setHighCrossover(2000.0f);
		crossoverTwisted_.setLowCrossover(200.0f);
		crossoverTwisted_.setHighCrossover(2000.0f);
		crossoverTwist3_.setLowCrossover(200.0f);
		crossoverTwist3_.setHighCrossover(2000.0f);

		// Set default parameters for each band
		// Use setter functions to keep knob values in sync with actual values
		for (auto& band : bands_) {
			band.setAttack(ONE_Q31 / 4);
			band.setRelease(ONE_Q31 / 4);
			band.setThresholdDown(ONE_Q31 / 2);
			band.setRatioDown(0); // Start at 1:1 ratio (transparent)
			band.setRatioUp(0);   // Start at 1:1 ratio (transparent)
			band.setBandwidth(ONE_Q31 / 2);
			band.setOutputLevel(ONE_Q31 / 2); // 12:00 = 0dB (unity)
		}

		// Initialize global parameters via setters to keep knob values in sync
		setOutputGain(ONE_Q31 / 2); // 12:00 = 0dB (unity)
		setCharacter(0);            // Default to 0 (Width zone start) - neutral settings
		setUpDownSkew(ONE_Q31 / 2);
		setVibe(0); // Default to 0 (Sync zone start) - all in phase
	}

	/// Set crossover frequency between low and mid bands
	void setLowCrossover(float freqHz) {
		crossoverAllpass1_.setLowCrossover(freqHz);
		crossoverAllpass2_.setLowCrossover(freqHz);
		crossoverAllpass3_.setLowCrossover(freqHz);
		crossoverLR2_.setLowCrossover(freqHz);
		crossoverLR2Fast_.setLowCrossover(freqHz);
		crossoverLR4_.setLowCrossover(freqHz);
		crossoverLR4Fast_.setLowCrossover(freqHz);
		crossoverTwisted_.setLowCrossover(freqHz);
		crossoverTwist3_.setLowCrossover(freqHz);
	}

	/// Set crossover frequency between mid and high bands
	void setHighCrossover(float freqHz) {
		crossoverAllpass1_.setHighCrossover(freqHz);
		crossoverAllpass2_.setHighCrossover(freqHz);
		crossoverAllpass3_.setHighCrossover(freqHz);
		crossoverLR2_.setHighCrossover(freqHz);
		crossoverLR2Fast_.setHighCrossover(freqHz);
		crossoverLR4_.setHighCrossover(freqHz);
		crossoverLR4Fast_.setHighCrossover(freqHz);
		crossoverTwisted_.setHighCrossover(freqHz);
		crossoverTwist3_.setHighCrossover(freqHz);
	}

	/// Get low crossover frequency in Hz
	[[nodiscard]] float getLowCrossoverHz() const { return crossoverAllpass1_.getLowCrossoverHz(); }

	/// Get high crossover frequency in Hz
	[[nodiscard]] float getHighCrossoverHz() const { return crossoverAllpass1_.getHighCrossoverHz(); }

	/// Set crossover type (ordered by CPU cost, cheapest first):
	/// 0=AP 6dB, 1=Quirky, 2=Twisted, 3=Weird, 4=LR2 Fast, 5=LR2, 6=LR4 Fast, 7=LR4, 8=Inverted, 9=Twist3
	void setCrossoverType(uint8_t type) { crossoverType_ = std::min(type, static_cast<uint8_t>(9)); }

	/// Get crossover type (0-7, ordered by CPU cost)
	[[nodiscard]] uint8_t getCrossoverType() const { return crossoverType_; }

	/// Access a specific band's compressor
	BandCompressor& getBand(Band band) { return bands_[static_cast<size_t>(band)]; }
	[[nodiscard]] const BandCompressor& getBand(Band band) const { return bands_[static_cast<size_t>(band)]; }

	/// Access band by index
	BandCompressor& getBand(size_t index) { return bands_[index]; }
	[[nodiscard]] const BandCompressor& getBand(size_t index) const { return bands_[index]; }

	/// Set wet/dry blend (0 = fully dry, ONE_Q31 = fully wet)
	void setBlend(q31_t blend) {
		wet_ = blend;
		dry_ = 1.0f - static_cast<float>(blend) / ONE_Q31f;
	}

	[[nodiscard]] q31_t getBlend() const { return wet_; }

	/// Set output gain (0 to ONE_Q31)
	/// CCW = -inf, 12:00 = 0dB, CW = +16dB
	void setOutputGain(q31_t g) {
		outputGainKnob_ = g;
		float normalized = float(g) / ONE_Q31f;
		if (normalized <= 0.5f) {
			// 0 to 0.5 maps to 0x to 1.0x (-inf to 0dB)
			outputGain_ = normalized * 2.0f;
		}
		else {
			// 0.5 to 1.0 maps to 1.0x to 6.31x (0dB to +16dB)
			outputGain_ = 1.0f + (normalized - 0.5f) * 2.0f * 5.31f;
		}
	}

	[[nodiscard]] q31_t getOutputGain() const { return outputGainKnob_; }
	[[nodiscard]] float getOutputGainLinear() const { return outputGain_; }

	/// Set threshold for all bands simultaneously (linked control)
	/// Shifts all per-band values by delta from previous linked value
	void setAllThresholds(q31_t t) {
		int64_t delta = static_cast<int64_t>(t) - static_cast<int64_t>(linkedThreshold_);
		linkedThreshold_ = t;
		for (size_t i = 0; i < kNumBands; ++i) {
			int64_t newVal = static_cast<int64_t>(bands_[i].getThresholdDown()) + delta;
			bands_[i].setThresholdDown(
			    static_cast<q31_t>(std::clamp(newVal, static_cast<int64_t>(0), static_cast<int64_t>(ONE_Q31))));
		}
	}

	/// Set ratio for all bands simultaneously (linked control)
	/// Shifts all per-band values by delta from previous linked value
	void setAllRatios(q31_t r) {
		int64_t delta = static_cast<int64_t>(r) - static_cast<int64_t>(linkedRatio_);
		linkedRatio_ = r;
		for (size_t i = 0; i < kNumBands; ++i) {
			int64_t newVal = static_cast<int64_t>(bands_[i].getRatioDown()) + delta;
			q31_t clamped =
			    static_cast<q31_t>(std::clamp(newVal, static_cast<int64_t>(0), static_cast<int64_t>(ONE_Q31)));
			bands_[i].setRatioDown(clamped);
			bands_[i].setRatioUp(clamped);
		}
	}

	/// Set attack for all bands simultaneously (linked control)
	void setAllAttacks(q31_t a) {
		for (auto& band : bands_) {
			band.setAttack(a);
		}
	}

	/// Set release for all bands simultaneously (linked control)
	void setAllReleases(q31_t r) {
		for (auto& band : bands_) {
			band.setRelease(r);
		}
	}

	/// Set character (0 to ONE_Q31) - replaces knee, controls multiple internal params
	/// Divided into 8 zones: Width, Timing, Skew, Punch, Air, Rich, OTT, Wild
	/// Each zone emphasizes different aspects of the compression character
	/// Uses vibe phase offsets for OWLTT zone oscillations
	void setCharacter(q31_t c) {
		// Cache: skip recalculation if knob hasn't changed
		// Note: OWLTT zone depends on vibe phases, but user must wiggle character to update
		if (c == characterKnob_ && characterComputed_) {
			return;
		}
		characterKnob_ = c;
		characterComputed_ = true;

		// Determine zone (0-7) and position within zone (0.0-1.0)
		auto zoneInfo = computeZoneQ31(c, kNumCharacterZones);
		int32_t zone = zoneInfo.index;
		float zonePos = zoneInfo.position;

		// Compute wrapped phases from feelPhaseOffset_ (creates divergent offsets per phi constant)
		// Using small phi powers (1.1-1.6 range) so common offset -0.3 works for all
		float ph025 = phi::wrapPhase(feelPhaseOffset_ * phi::kPhi025);
		float ph033 = phi::wrapPhase(feelPhaseOffset_ * phi::kPhi033);
		float ph050 = phi::wrapPhase(feelPhaseOffset_ * phi::kPhi050);
		float ph067 = phi::wrapPhase(feelPhaseOffset_ * phi::kPhi067);
		float ph075 = phi::wrapPhase(feelPhaseOffset_ * phi::kPhi075);
		float ph100 = phi::wrapPhase(feelPhaseOffset_ * phi::kPhi100);

		// === Compute derived parameters based on zone ===
		// Each zone has characteristic curves for width, knee, timing, skew

		// Response: 0=smooth (~145ms), 1=punchy (~2ms) - extended range
		// Detection time varies from ~2ms (transient-accurate) to ~145ms (glue-like)
		switch (zone) {
		case 3:                                // Punch: very fast detection for transients
			response_ = 0.9f + zonePos * 0.1f; // 0.9→1.0 (faster)
			break;
		case 6:                                  // OTT: fast for classic aggressive response
			response_ = 0.85f + zonePos * 0.15f; // 0.85→1.0
			break;
		case 4:                                // Air: slow detection for smooth glue (70-145ms)
			response_ = 0.1f - zonePos * 0.1f; // 0.1→0.0 (slower)
			break;
		case 5:                                  // Rich: slow for warm sustain (unchanged)
			response_ = 0.15f - zonePos * 0.15f; // 0.15→0.0
			break;
		case 7: { // OWLTT: oscillates full range for dynamic breathing (0.85x freq)
			response_ = 0.5f + 0.5f * triangleFloat(zonePos * phi::kPhi150 * 0.85f + vibePhaseWidth_[0] + ph033 - 0.3f);
			break;
		}
		default:
			response_ = 0.5f; // Balanced (~9ms)
		}

		// Stereo width [bass, mid, high]: 0=mono, 1=unity, >1=enhanced, <0=inverted
		// Width zone: sweeps narrow to wide, others preserve or enhance
		// Per-band allows highs to be wider than mids for air/shimmer
		// "Weird" crossover types (1,2,3,9) allow bass stereo/inversion
		bool weirdXover = (crossoverType_ == 1 || crossoverType_ == 2 || crossoverType_ == 3 || crossoverType_ == 9);
		switch (zone) {
		case 0: // Width: sweep from slightly narrow to very enhanced (less mono collapse at default)
			bandWidth_[0] =
			    weirdXover ? (-0.2f + zonePos * 1.5f) : (0.7f + zonePos * 0.5f); // weird: -0.2→1.3, normal: 0.7→1.2
			bandWidth_[1] = 0.85f + zonePos * 0.85f;                             // 0.85→1.7 (was 0.5→1.7)
			bandWidth_[2] = 0.9f + zonePos * 1.1f;                               // 0.9→2.0 (was 0.5→2.0)
			break;
		case 4:                                                          // Air: very wide stereo, highs super enhanced
			bandWidth_[0] = weirdXover ? (0.6f + zonePos * 0.4f) : 0.7f; // Less mono collapse on bass
			bandWidth_[1] = 1.0f + zonePos * 0.5f;                       // 1.0→1.5 (was 1.0→1.2)
			bandWidth_[2] = 1.1f + zonePos * 0.7f;                       // 1.1→1.8 (was 1.1→1.5)
			break;
		case 6:                                                          // OTT: wide for that classic sound
			bandWidth_[0] = weirdXover ? (0.6f + zonePos * 0.4f) : 0.7f; // Less mono collapse on bass
			bandWidth_[1] = 1.0f + zonePos * 0.5f;                       // 1.0→1.5 (was 1.0→1.25)
			bandWidth_[2] = 1.0f + zonePos * 0.7f;                       // 1.0→1.7 (was 1.0→1.35)
			break;
		case 3: // Punch: tight mids for focused impact (but less mono collapse)
			bandWidth_[0] = weirdXover ? (0.5f + zonePos * 0.3f) : 0.7f;
			bandWidth_[1] = 0.85f + zonePos * 0.1f;  // 0.85→0.95 (was 0.7→0.8)
			bandWidth_[2] = 0.95f + zonePos * 0.15f; // 0.95→1.1 (was 0.9→1.1)
			break;
		case 7: { // OWLTT: phi-modulated per-band width oscillation (0.85x freq, stronger)
			bandWidth_[0] =
			    weirdXover
			        ? (-0.3f + 1.2f * triangleFloat(zonePos * phi::kPhi100 * 0.85f + vibePhaseWidth_[0] + ph025 - 0.3f))
			        : 0.5f;
			bandWidth_[1] =
			    0.8f + 0.7f * triangleFloat(zonePos * phi::kPhi150 * 0.85f + vibePhaseWidth_[1] + ph033 - 0.3f);
			bandWidth_[2] =
			    0.9f + 0.7f * triangleFloat(zonePos * phi::kPhi225 * 0.85f + vibePhaseWidth_[2] + ph050 - 0.3f);
			break;
		}
		default:
			bandWidth_[0] = weirdXover ? (0.5f + zonePos * 0.5f) : 0.7f; // weird: 0.5→1.0, normal: 0.7 (less mono)
			bandWidth_[1] = 1.0f;                                        // Unity - preserve original stereo
			bandWidth_[2] = 1.0f;
		}

		// Knee: 0=hard, 1=soft
		switch (zone) {
		case 0:                            // Width: start with steepish knee, soften dramatically as width increases
			knee_ = 0.2f + zonePos * 0.6f; // 0.2→0.8 (was 0.2→0.6)
			break;
		case 4:                             // Air: soft for smoothness
			knee_ = 0.6f + zonePos * 0.35f; // 0.6→0.95 (was 0.6→0.9)
			break;
		case 5: // Rich: soft for warmth (unchanged)
			knee_ = 0.6f + zonePos * 0.3f;
			break;
		case 3:                              // Punch: very hard knee for transients
			knee_ = 0.05f + zonePos * 0.15f; // 0.05→0.2 (was 0.1→0.3, harder)
			break;
		case 6: // OTT: medium-hard for aggression
			knee_ = 0.1f + zonePos * 0.2f;
			break;
		case 7: { // OWLTT: varies with vibe phase offset (0.85x freq, stronger)
			knee_ = 0.5f + 0.45f * triangleFloat(zonePos * phi::kPhi225 * 0.85f + vibePhaseKnee_ + ph050 - 0.3f);
			break;
		}
		default:
			knee_ = 0.4f; // Medium
		}

		// Per-band timing offsets: multiplier on base attack/release (0.5x to 2x)
		// Stored as offset from 1.0 (so 0 = no change, -0.5 = half speed, +1.0 = double)
		switch (zone) {
		case 1:                                 // Timing: sweep from uniform to very differentiated
			timingOffset_[0] = -0.7f * zonePos; // Low slower (was -0.5)
			timingOffset_[1] = 0.0f;
			timingOffset_[2] = 0.7f * zonePos; // High faster (was 0.5)
			break;
		case 3:                                        // Punch: very fast attack across all bands
			timingOffset_[0] = -0.5f - zonePos * 0.3f; // (was -0.4 - 0.2)
			timingOffset_[1] = -0.4f - zonePos * 0.3f; // (was -0.3 - 0.2)
			timingOffset_[2] = -0.3f - zonePos * 0.3f; // (was -0.2 - 0.2)
			break;
		case 4:                                       // Air: slow bass, fast high band (more contrast)
			timingOffset_[0] = 0.4f + zonePos * 0.3f; // 0.4→0.7 (was 0.3→0.5, even slower bass)
			timingOffset_[1] = 0.0f;
			timingOffset_[2] = -0.6f - zonePos * 0.3f; // -0.6→-0.9 (was -0.5→-0.8, faster highs)
			break;
		case 6:                                         // OTT: classic fast timing, even more aggressive
			timingOffset_[0] = -0.4f - zonePos * 0.15f; // -0.4→-0.55 (was -0.3→-0.4)
			timingOffset_[1] = -0.5f - zonePos * 0.15f; // -0.5→-0.65 (was -0.4→-0.5)
			timingOffset_[2] = -0.6f - zonePos * 0.15f; // -0.6→-0.75 (was -0.5→-0.6)
			break;
		case 7: { // OWLTT: chaos (with vibe phase offsets, 0.85x freq, stronger)
			timingOffset_[0] =
			    0.6f * triangleFloat(zonePos * phi::kPhi300 * 0.85f + vibePhaseTiming_[0] + ph067 - 0.3f);
			timingOffset_[1] =
			    0.6f * triangleFloat(zonePos * phi::kPhi350 * 0.85f + 0.333f + vibePhaseTiming_[1] + ph075 - 0.3f);
			timingOffset_[2] =
			    0.6f * triangleFloat(zonePos * phi::kPhi375 * 0.85f + 0.667f + vibePhaseTiming_[2] + ph100 - 0.3f);
			break;
		}
		default:
			timingOffset_[0] = timingOffset_[1] = timingOffset_[2] = 0.0f;
		}

		// Per-band skew: -1=upward, 0=balanced, +1=downward
		switch (zone) {
		case 2:                                       // Skew: sweep through more extreme skew variations
			skewOffset_[0] = -0.85f + zonePos * 1.5f; // Low: -0.85→0.65 (was -0.7→0.5)
			skewOffset_[1] = 0.0f;
			skewOffset_[2] = 0.85f - zonePos * 1.5f; // High: 0.85→-0.65 (was 0.7→-0.5)
			break;
		case 4:                                       // Air: strong upward on highs (more pronounced)
			skewOffset_[0] = 0.1f * zonePos;          // slight downward on bass for contrast
			skewOffset_[1] = -0.4f * zonePos;         // -0.4 upward on mids (was -0.3)
			skewOffset_[2] = -0.7f - zonePos * 0.25f; // -0.7→-0.95 (was -0.6→-0.9, stronger upward highs)
			break;
		case 5:                                       // Rich: upward emphasis (more pronounced)
			skewOffset_[0] = -0.4f - zonePos * 0.35f; // -0.4→-0.75 (was -0.3→-0.6)
			skewOffset_[1] = -0.5f - zonePos * 0.35f; // -0.5→-0.85 (was -0.4→-0.7)
			skewOffset_[2] = -0.3f - zonePos * 0.25f; // -0.3→-0.55 (was -0.2→-0.4)
			break;
		case 6:                                     // OTT: more differentiated skew
			skewOffset_[0] = 0.2f + zonePos * 0.1f; // slight downward on bass
			skewOffset_[1] = 0.0f;
			skewOffset_[2] = -0.2f - zonePos * 0.1f; // upward on highs
			break;
		case 7: { // OWLTT: variation (with vibe phase offsets, 0.85x freq, stronger)
			skewOffset_[0] = 0.9f * triangleFloat(zonePos * phi::kPhi350 * 0.85f + vibePhaseSkew_[0] + ph075 - 0.3f);
			skewOffset_[1] =
			    0.9f * triangleFloat(zonePos * phi::kPhi300 * 0.85f + 0.167f + vibePhaseSkew_[1] + ph067 - 0.3f);
			skewOffset_[2] =
			    0.9f * triangleFloat(zonePos * phi::kPhi375 * 0.85f + 0.333f + vibePhaseSkew_[2] + ph100 - 0.3f);
			break;
		}
		default:
			skewOffset_[0] = skewOffset_[1] = skewOffset_[2] = 0.0f;
		}

		// Zone multipliers for vibe/feel modulation: [zone][param]
		// Params: 0-2=width, 3=knee, 4-6=timing, 7-9=skew
		// clang-format off
		static constexpr float kVibeZoneMult[7][10] = {
			{0.45f, 0.55f, 0.65f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, // Width (was 0.3-0.5)
			{0.0f, 0.0f, 0.0f, 0.0f, 0.55f, 0.55f, 0.55f, 0.0f, 0.0f, 0.0f}, // Timing (was 0.4)
			{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.65f, 0.65f, 0.65f}, // Skew (was 0.5)
			{0.3f, 0.3f, 0.3f, 0.3f, 0.4f, 0.4f, 0.4f, 0.0f, 0.0f, 0.0f}, // Punch (was 0.2-0.3)
			{0.3f, 0.4f, 0.55f, 0.3f, 0.4f, 0.0f, 0.4f, 0.0f, 0.4f, 0.55f}, // Air (was 0.2-0.4)
			{0.0f, 0.0f, 0.0f, 0.4f, 0.0f, 0.0f, 0.0f, 0.55f, 0.55f, 0.4f}, // Rich (was 0.3-0.4)
			{0.0f, 0.4f, 0.55f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.0f, 0.3f}, // OTT (was 0.2-0.4)
		};
		static constexpr float kFeelZoneMult[7][10] = {
			{0.22f, 0.28f, 0.35f, 0.22f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f}, // Width (was 0.15-0.25)
			{0.00f, 0.00f, 0.00f, 0.00f, 0.28f, 0.28f, 0.28f, 0.00f, 0.00f, 0.00f}, // Timing (was 0.2)
			{0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.35f, 0.35f, 0.35f}, // Skew (was 0.25)
			{0.15f, 0.15f, 0.15f, 0.15f, 0.22f, 0.22f, 0.22f, 0.00f, 0.00f, 0.00f}, // Punch (was 0.1-0.15)
			{0.15f, 0.22f, 0.28f, 0.15f, 0.22f, 0.00f, 0.22f, 0.00f, 0.22f, 0.28f}, // Air (was 0.1-0.2)
			{0.00f, 0.00f, 0.00f, 0.22f, 0.00f, 0.00f, 0.00f, 0.28f, 0.28f, 0.22f}, // Rich (was 0.15-0.2)
			{0.00f, 0.22f, 0.28f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.00f, 0.15f}, // OTT (was 0.1-0.2)
		};
		// clang-format on

		// Apply vibe and feelPhaseOffset modulation to non-OWLTT zones
		if (zone != 7) {
			float vibeModAmount = 0.55f * (static_cast<float>(vibeKnob_) / ONE_Q31f); // (was 0.3)
			bool applyVibe = vibeModAmount > 0.01f;
			bool applyFeel = std::abs(feelPhaseOffset_) > 0.01f;

			if (applyVibe || applyFeel) {
				const float* vm = kVibeZoneMult[zone];
				const float* fm = kFeelZoneMult[zone];

				// Phi triangles for feel modulation (small phi powers, common -0.3 offset peaks near pos=1)
				float phiTri[6] = {
				    triangleFloat(zonePos * phi::kPhi025 + ph025 - 0.3f),
				    triangleFloat(zonePos * phi::kPhi033 + ph033 - 0.3f),
				    triangleFloat(zonePos * phi::kPhi050 + ph050 - 0.3f),
				    triangleFloat(zonePos * phi::kPhi067 + ph067 - 0.3f),
				    triangleFloat(zonePos * phi::kPhi075 + ph075 - 0.3f),
				    triangleFloat(zonePos * phi::kPhi100 + ph100 - 0.3f),
				};
				// Vibe triangles (pre-computed phases, staggered by 0.33)
				float vibeTri[10] = {
				    triangleFloat(vibePhaseWidth_[0]),          triangleFloat(vibePhaseWidth_[1] + 0.33f),
				    triangleFloat(vibePhaseWidth_[2] + 0.67f),  triangleFloat(vibePhaseKnee_),
				    triangleFloat(vibePhaseTiming_[0]),         triangleFloat(vibePhaseTiming_[1] + 0.33f),
				    triangleFloat(vibePhaseTiming_[2] + 0.67f), triangleFloat(vibePhaseSkew_[0]),
				    triangleFloat(vibePhaseSkew_[1] + 0.33f),   triangleFloat(vibePhaseSkew_[2] + 0.67f),
				};
				// Phi index mapping: width uses 0,1,2; knee uses 2; timing uses 3,4,5; skew uses 4,3,5
				static constexpr int8_t kPhiIdx[10] = {0, 1, 2, 2, 3, 4, 5, 4, 3, 5};

				// Apply to width
				for (int i = 0; i < 3; i++) {
					if (applyVibe && vm[i] != 0.0f)
						bandWidth_[i] += vibeModAmount * vm[i] * vibeTri[i];
					if (applyFeel && fm[i] != 0.0f)
						bandWidth_[i] += fm[i] * phiTri[kPhiIdx[i]];
				}
				// Apply to knee
				if (applyVibe && vm[3] != 0.0f)
					knee_ += vibeModAmount * vm[3] * vibeTri[3];
				if (applyFeel && fm[3] != 0.0f)
					knee_ += fm[3] * phiTri[kPhiIdx[3]];
				knee_ = std::clamp(knee_, 0.0f, 1.0f);
				// Apply to timing
				for (int i = 0; i < 3; i++) {
					if (applyVibe && vm[4 + i] != 0.0f)
						timingOffset_[i] += vibeModAmount * vm[4 + i] * vibeTri[4 + i];
					if (applyFeel && fm[4 + i] != 0.0f)
						timingOffset_[i] += fm[4 + i] * phiTri[kPhiIdx[4 + i]];
				}
				// Apply to skew
				for (int i = 0; i < 3; i++) {
					if (applyVibe && vm[7 + i] != 0.0f)
						skewOffset_[i] += vibeModAmount * vm[7 + i] * vibeTri[7 + i];
					if (applyFeel && fm[7 + i] != 0.0f)
						skewOffset_[i] += fm[7 + i] * phiTri[kPhiIdx[7 + i]];
				}
			}
		}

		// Update pre-computed envelope alpha values
		updateEnvelopeAlpha();
	}

	/// Get character knob value
	[[nodiscard]] q31_t getCharacter() const { return characterKnob_; }

	/// Get current character zone for display
	[[nodiscard]] CharacterZone getCharacterZone() const {
		auto info = computeZoneQ31(characterKnob_, kNumCharacterZones);
		return static_cast<CharacterZone>(info.index);
	}

	/// Get position within current zone (0-127 for display)
	[[nodiscard]] int32_t getCharacterZonePosition() const {
		auto info = computeZoneQ31(characterKnob_, kNumCharacterZones);
		return zonePositionToDisplay(info.position);
	}

	/// Get stereo width for band (0=bass, 1=mid, 2=high) - 0=mono, 1=unity, >1=enhanced, <0=inverted
	[[nodiscard]] float getWidth(size_t band = 0) const { return (band < 3) ? bandWidth_[band] : 1.0f; }

	/// Get knee value (internal, computed from character)
	[[nodiscard]] float getKnee() const { return knee_; }

	/// Get per-band skew offset (added to global skew)
	[[nodiscard]] float getBandSkewOffset(size_t band) const { return (band < kNumBands) ? skewOffset_[band] : 0.0f; }

	/// Get per-band timing offset (multiplier adjustment)
	[[nodiscard]] float getBandTimingOffset(size_t band) const {
		return (band < kNumBands) ? timingOffset_[band] : 0.0f;
	}

	/// Set up/down ratio skew (0 = favor upward, ONE_Q31/2 = balanced, ONE_Q31 = favor downward)
	/// Controls the balance between upward and downward compression
	void setUpDownSkew(q31_t s) {
		upDownSkewKnob_ = s;
		// Map to -1.0 to +1.0 (-1 = all upward, 0 = balanced, +1 = all downward)
		upDownSkew_ = (float(s) / ONE_Q31f) * 2.0f - 1.0f;
	}

	/// Set vibe (0 to ONE_Q31) - controls phase relationships between Feel oscillations
	/// Divided into 8 zones: Sync, Spread, Pairs, Cascade, Invert, Pulse, Drift, Twist
	/// When vibePhaseOffset > 0: Full phi-triangle evolution across ALL zones
	void setVibe(q31_t v) {
		vibeKnob_ = v;
		// Invalidate character cache - OWLTT zone depends on vibe phases
		characterComputed_ = false;

		// Determine zone (0-7) and position within zone (0.0-1.0)
		auto zoneInfo = computeZoneQ31(v, kNumVibeZones);
		int32_t zone = zoneInfo.index;
		float zonePos = zoneInfo.position;

		// Twist modulation: 10 triangle periods, 75% duty cycle (zero until halfway up vibe)
		// Global vibe position (0.0 to 1.0 across entire knob range)
		float globalVibePos = static_cast<float>(v) / static_cast<float>(ONE_Q31);
		if (globalVibePos < 0.5f) {
			// First 75% of duty cycle: twist stays at maximum (1.0)
			vibeTwist_ = 1.0f;
		}
		else {
			// Last 25% of duty cycle: triangle modulation with blend ramping up
			float rampPos = (globalVibePos - 0.5f) * 2.0f; // 0 to 1 in second half
			// Wrap phase with fmod for precision at high values
			float twistPhase = std::fmod(rampPos * 10.0f + vibePhaseOffset_, 1.0f);
			float triangleVal = triangleFloat(twistPhase); // -1 to +1
			// Blend: 0 at vibe=50%, 1 at vibe=100%
			float blend = rampPos;
			// At zero blend: full twist (1.0). At full blend: modulated by triangle (0.5 to 1.0)
			vibeTwist_ = 1.0f - blend * 0.5f * (1.0f - triangleVal);
		}

		// Check if phaseOffset is engaged - use full phi-triangle evolution across all zones
		if (vibePhaseOffset_ != 0.0f) {
			// Full range phi-triangle evolution (like Twist zones, but across all 8 zones)
			float pos = globalVibePos;

			// Use double for phase wrapping to maintain precision at large values
			double phRaw = static_cast<double>(vibePhaseOffset_);

			// Per-frequency phase offsets - irrational frequencies create non-repeating divergence
			float ph225 = phi::wrapPhase(phRaw * phi::kPhi225);
			float ph300 = phi::wrapPhase(phRaw * phi::kPhi300);
			float ph350 = phi::wrapPhase(phRaw * phi::kPhi350);
			float ph375 = phi::wrapPhase(phRaw * phi::kPhi375);
			float ph400 = phi::wrapPhase(phRaw * phi::kPhi400);
			float ph325 = phi::wrapPhase(phRaw * phi::kPhi325);
			float ph360 = phi::wrapPhase(phRaw * phi::kPhi360);
			float ph385 = phi::wrapPhase(phRaw * phi::kPhi385);

			float wpVal = 0.5f * triangleFloat(pos * phi::kPhi225 - 0.25f + ph225);
			vibePhaseWidth_ = {wpVal, wpVal, wpVal};
			vibePhaseKnee_ = 0.5f * triangleFloat(pos * phi::kPhi300 + ph300);
			vibePhaseTiming_[0] = 0.5f * triangleFloat(pos * phi::kPhi350 - 0.25f + ph350);
			vibePhaseTiming_[1] = 0.5f * triangleFloat(pos * phi::kPhi375 + 0.083f + ph375);
			vibePhaseTiming_[2] = 0.5f * triangleFloat(pos * phi::kPhi400 + 0.417f + ph400);
			vibePhaseSkew_[0] = 0.5f * triangleFloat(pos * phi::kPhi325 + 0.25f + ph325);
			vibePhaseSkew_[1] = 0.5f * triangleFloat(pos * phi::kPhi360 - 0.083f + ph360);
			vibePhaseSkew_[2] = 0.5f * triangleFloat(pos * phi::kPhi385 + 0.583f + ph385);
			return;
		}

		// Standard discrete zone behavior (vibePhaseOffset == 0)
		switch (zone) {
		case 0: // Sync: all in phase, sweep from 0 to slight offset
			vibePhaseWidth_ = {zonePos * 0.1f, zonePos * 0.1f, zonePos * 0.1f};
			vibePhaseKnee_ = zonePos * 0.1f;
			vibePhaseTiming_ = {zonePos * 0.1f, zonePos * 0.1f, zonePos * 0.1f};
			vibePhaseSkew_ = {zonePos * 0.1f, zonePos * 0.1f, zonePos * 0.1f};
			break;

		case 1: // Spread: evenly spread phases (120° = 0.333 apart)
			vibePhaseWidth_ = {0.0f, 0.0f, 0.0f};
			vibePhaseKnee_ = 0.167f * zonePos;
			vibePhaseTiming_ = {0.0f, 0.333f * zonePos, 0.667f * zonePos};
			vibePhaseSkew_ = {0.0f, 0.333f * zonePos, 0.667f * zonePos};
			break;

		case 2: // Pairs: low+high in phase, mid opposite
			vibePhaseWidth_ = {0.0f, 0.0f, 0.0f};
			vibePhaseKnee_ = 0.5f * zonePos;
			vibePhaseTiming_ = {0.0f, 0.5f * zonePos, 0.0f};
			vibePhaseSkew_ = {0.0f, 0.5f * zonePos, 0.0f};
			break;

		case 3: // Cascade: progressive phase shift
			vibePhaseWidth_ = {0.25f * zonePos, 0.25f * zonePos, 0.25f * zonePos};
			vibePhaseKnee_ = 0.5f * zonePos;
			vibePhaseTiming_ = {0.0f, 0.25f * zonePos, 0.5f * zonePos};
			vibePhaseSkew_ = {0.0f, 0.333f * zonePos, 0.667f * zonePos};
			break;

		case 4: // Invert: width/knee vs timing/skew opposite
			vibePhaseWidth_ = {0.0f, 0.0f, 0.0f};
			vibePhaseKnee_ = 0.0f;
			vibePhaseTiming_ = {0.5f * zonePos, 0.5f * zonePos, 0.5f * zonePos};
			vibePhaseSkew_ = {0.5f * zonePos, 0.5f * zonePos, 0.5f * zonePos};
			break;

		case 5: // Pulse: clustered phases creating pulses
			vibePhaseWidth_ = {0.0f, 0.0f, 0.0f};
			vibePhaseKnee_ = 0.1f * zonePos;
			vibePhaseTiming_ = {0.0f, 0.05f * zonePos, 0.1f * zonePos};
			vibePhaseSkew_ = {0.5f, 0.55f * zonePos, 0.6f * zonePos};
			break;

		case 6: // Drift: slowly varying relationships
			vibePhaseWidth_ = {0.2f * zonePos, 0.2f * zonePos, 0.2f * zonePos};
			vibePhaseKnee_ = 0.3f * zonePos;
			vibePhaseTiming_ = {0.1f * zonePos, 0.2f * zonePos, 0.4f * zonePos};
			vibePhaseSkew_ = {0.15f * zonePos, 0.35f * zonePos, 0.25f * zonePos};
			break;

		case 7: { // Twist: phi-power frequencies (uses pos within zone only when phaseOffset==0)
			float wpVal = 0.5f * triangleFloat(zonePos * phi::kPhi225 - 0.25f);
			vibePhaseWidth_ = {wpVal, wpVal, wpVal};
			vibePhaseKnee_ = 0.5f * triangleFloat(zonePos * phi::kPhi300);
			vibePhaseTiming_[0] = 0.5f * triangleFloat(zonePos * phi::kPhi350 - 0.25f);
			vibePhaseTiming_[1] = 0.5f * triangleFloat(zonePos * phi::kPhi375 + 0.083f);
			vibePhaseTiming_[2] = 0.5f * triangleFloat(zonePos * phi::kPhi400 + 0.417f);
			vibePhaseSkew_[0] = 0.5f * triangleFloat(zonePos * phi::kPhi325 + 0.25f);
			vibePhaseSkew_[1] = 0.5f * triangleFloat(zonePos * phi::kPhi360 - 0.083f);
			vibePhaseSkew_[2] = 0.5f * triangleFloat(zonePos * phi::kPhi385 + 0.583f);
			break;
		}

		default:
			vibePhaseWidth_ = {0.0f, 0.0f, 0.0f};
			vibePhaseKnee_ = 0.0f;
			vibePhaseTiming_ = {0.0f, 0.0f, 0.0f};
			vibePhaseSkew_ = {0.0f, 0.0f, 0.0f};
		}
	}

	/// Get vibe knob value
	[[nodiscard]] q31_t getVibe() const { return vibeKnob_; }

	/// Set vibe phase offset (secret menu parameter, unbounded - wraps in DSP)
	void setVibePhaseOffset(float phase) { vibePhaseOffset_ = phase; }

	/// Get vibe phase offset
	[[nodiscard]] float getVibePhaseOffset() const { return vibePhaseOffset_; }

	/// Set feel phase offset (secret menu parameter, unbounded - wraps per phi constant)
	/// Push+twist on Feel encoder to adjust. Shifts all phi triangles in Feel zones.
	void setFeelPhaseOffset(float phase) {
		feelPhaseOffset_ = phase;
		characterComputed_ = false; // Invalidate cache to recompute with new phase
	}

	/// Get feel phase offset
	[[nodiscard]] float getFeelPhaseOffset() const { return feelPhaseOffset_; }

	/// Get current vibe zone for display
	[[nodiscard]] VibeZone getVibeZone() const {
		auto info = computeZoneQ31(vibeKnob_, kNumVibeZones);
		return static_cast<VibeZone>(info.index);
	}

	/// Get position within current vibe zone (0-127 for display)
	[[nodiscard]] int32_t getVibeZonePosition() const {
		auto info = computeZoneQ31(vibeKnob_, kNumVibeZones);
		return zonePositionToDisplay(info.position);
	}

	/// Get the linked threshold value
	[[nodiscard]] q31_t getLinkedThreshold() const { return linkedThreshold_; }

	/// Get the linked ratio value
	[[nodiscard]] q31_t getLinkedRatio() const { return linkedRatio_; }

	/// Get the linked attack value (from first band)
	[[nodiscard]] q31_t getLinkedAttack() const { return bands_[0].getAttack(); }

	/// Get the linked release value (from first band)
	[[nodiscard]] q31_t getLinkedRelease() const { return bands_[0].getRelease(); }

	/// Get the up/down skew value
	[[nodiscard]] q31_t getUpDownSkew() const { return upDownSkewKnob_; }

	// ========== Enable/Disable Zone ==========

	/// Check if multiband compressor is enabled
	[[nodiscard]] bool isEnabled() const { return enabledZone_ > (ONE_Q31 / 2); }

	/// Get the enabled zone value
	[[nodiscard]] q31_t getEnabledZone() const { return enabledZone_; }

	/// Set the enabled zone value
	void setEnabledZone(q31_t zone) { enabledZone_ = zone; }

	// ========== Linked Bandwidth ==========

	/// Get the linked bandwidth value (shared across all bands)
	[[nodiscard]] q31_t getLinkedBandwidth() const { return linkedBandwidth_; }

	/// Set the linked bandwidth value
	/// Shifts all per-band values by delta from previous linked value
	void setLinkedBandwidth(q31_t bw) {
		int64_t delta = static_cast<int64_t>(bw) - static_cast<int64_t>(linkedBandwidth_);
		linkedBandwidth_ = bw;
		for (size_t i = 0; i < kNumBands; ++i) {
			int64_t newVal = static_cast<int64_t>(bands_[i].getBandwidth()) + delta;
			bands_[i].setBandwidth(
			    static_cast<q31_t>(std::clamp(newVal, static_cast<int64_t>(0), static_cast<int64_t>(ONE_Q31))));
		}
	}

	// ========== Per-Band Offsets ==========

	/// Get threshold offset for a specific band (for XML persistence)
	/// Computes offset dynamically as (band_value - linked_value)
	[[nodiscard]] q31_t getThresholdOffset(size_t band) const {
		if (band >= kNumBands) {
			return 0;
		}
		return static_cast<q31_t>(static_cast<int64_t>(bands_[band].getThresholdDown())
		                          - static_cast<int64_t>(linkedThreshold_));
	}

	/// Set threshold offset for a specific band (for XML persistence)
	/// When loading from XML, applies offset to reach target per-band value
	void setThresholdOffset(size_t band, q31_t offset) {
		if (band < kNumBands) {
			// Apply offset to linked value to set per-band value
			q31_t net = static_cast<q31_t>(std::clamp(static_cast<int64_t>(linkedThreshold_) + offset,
			                                          static_cast<int64_t>(0), static_cast<int64_t>(ONE_Q31)));
			bands_[band].setThresholdDown(net);
		}
	}

	/// Get ratio offset for a specific band (for XML persistence)
	/// Computes offset dynamically as (band_value - linked_value)
	[[nodiscard]] q31_t getRatioOffset(size_t band) const {
		if (band >= kNumBands) {
			return 0;
		}
		return static_cast<q31_t>(static_cast<int64_t>(bands_[band].getRatioDown())
		                          - static_cast<int64_t>(linkedRatio_));
	}

	/// Set ratio offset for a specific band (for XML persistence)
	/// When loading from XML, applies offset to reach target per-band value
	void setRatioOffset(size_t band, q31_t offset) {
		if (band < kNumBands) {
			// Apply offset to linked value to set per-band value
			q31_t net = static_cast<q31_t>(std::clamp(static_cast<int64_t>(linkedRatio_) + offset,
			                                          static_cast<int64_t>(0), static_cast<int64_t>(ONE_Q31)));
			bands_[band].setRatioDown(net);
			bands_[band].setRatioUp(net);
		}
	}

	/// Get bandwidth offset for a specific band (for XML persistence)
	/// Computes offset dynamically as (band_value - linked_value)
	[[nodiscard]] q31_t getBandwidthOffset(size_t band) const {
		if (band >= kNumBands) {
			return 0;
		}
		return static_cast<q31_t>(static_cast<int64_t>(bands_[band].getBandwidth())
		                          - static_cast<int64_t>(linkedBandwidth_));
	}

	/// Set bandwidth offset for a specific band (for XML persistence)
	/// When loading from XML, applies offset to reach target per-band value
	void setBandwidthOffset(size_t band, q31_t offset) {
		if (band < kNumBands) {
			// Apply offset to linked value to set per-band value
			q31_t net = static_cast<q31_t>(std::clamp(static_cast<int64_t>(linkedBandwidth_) + offset,
			                                          static_cast<int64_t>(0), static_cast<int64_t>(ONE_Q31)));
			bands_[band].setBandwidth(net);
		}
	}

	// ========== Net Values (Actual Per-Band Values) ==========

	/// Get actual threshold for a specific band
	[[nodiscard]] q31_t getNetThreshold(size_t band) const {
		return (band < kNumBands) ? bands_[band].getThresholdDown() : 0;
	}

	/// Get actual ratio for a specific band
	[[nodiscard]] q31_t getNetRatio(size_t band) const { return (band < kNumBands) ? bands_[band].getRatioDown() : 0; }

	/// Get actual bandwidth for a specific band
	[[nodiscard]] q31_t getNetBandwidth(size_t band) const {
		return (band < kNumBands) ? bands_[band].getBandwidth() : 0;
	}

	/// Reset all filter and compressor states
	void reset() {
		crossoverAllpass1_.reset();
		crossoverAllpass2_.reset();
		crossoverAllpass3_.reset();
		crossoverLR2_.reset();
		crossoverLR2Fast_.reset();
		crossoverLR4_.reset();
		crossoverLR4Fast_.reset();
		crossoverTwisted_.reset();
		crossoverTwist3_.reset();
		for (auto& band : bands_) {
			band.reset();
		}
		saturationStateL_.fill(0);
		saturationStateR_.fill(0);
		dcBlockL_.reset();
		dcBlockR_.reset();
	}

	/// Render the multiband compressor in-place
	/// Pure dynamics processor - output gain knob is the only gain control.
	/// At 1:1 ratio with output gain at unity, this is transparent.
	/// Thresholds are absolute (referenced to full scale), independent of track volume.
	/// @param buffer Stereo audio buffer to process
	void render(std::span<StereoSample> buffer) {
		if (buffer.empty()) {
			return;
		}

		// Crossover type names for benchmarking (must match crossoverType_ 0-9)
		static const char* kXoverNames[] = {"ap1_6dB",  "quirky",   "twisted",  "weird",    "lr2_fast",
		                                    "lr2_full", "lr4_fast", "lr4_full", "inverted", "twist3"};
		const char* xoverTag = kXoverNames[crossoverType_ < 10 ? crossoverType_ : 0];

		FX_BENCH_DECLARE(benchTotal, "multiband", "total");
		FX_BENCH_DECLARE(benchXover, "multiband", "crossover");
		FX_BENCH_DECLARE(benchEnv, "multiband", "envelope");
		FX_BENCH_DECLARE(benchRecomb, "multiband", "recombine");
		FX_BENCH_SET_TAG(benchTotal, 1, xoverTag);
		FX_BENCH_SET_TAG(benchXover, 1, xoverTag);
		FX_BENCH_SET_TAG(benchEnv, 1, xoverTag);
		FX_BENCH_SET_TAG(benchRecomb, 1, xoverTag);
		FX_BENCH_START(benchTotal);
		FX_BENCH_START(benchXover);

		// Increment frame counter for gap detection in band compressors
		++frameCount_;

		// Store dry signal if blending
		static std::array<StereoSample, SSI_TX_BUFFER_NUM_SAMPLES> dryBuffer;
		if (wet_ != ONE_Q31) {
			std::copy(buffer.begin(), buffer.end(), dryBuffer.begin());
		}

		// Temporary buffers for each band
		static std::array<q31_t, SSI_TX_BUFFER_NUM_SAMPLES> bandBufferL[kNumBands];
		static std::array<q31_t, SSI_TX_BUFFER_NUM_SAMPLES> bandBufferR[kNumBands];

		// Apply twist modulation to Twisted/Twist3 crossovers (only affects types 2 and 9)
		if (crossoverType_ == 2) {
			crossoverTwisted_.setTwist(vibeTwist_);
		}
		else if (crossoverType_ == 9) {
			crossoverTwist3_.setTwist(vibeTwist_);
		}

		// Split into bands using selected crossover type (ordered by CPU cost):
		// Crossover types: 0=AP 6dB, 1=Quirky, 2=Twisted, 3=Weird, 4=LR2 Fast, 5=LR2, 6=LR4 Fast, 7=LR4
		// Separate loops per crossover type for better branch prediction and potential vectorization
		switch (crossoverType_) {
		case 1: // Quirky - ORDER=2 allpass (creative)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverAllpass2_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		case 2: // Twisted - mixed coefficients (creative)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverTwisted_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		case 3: // Weird - ORDER=3 allpass (creative)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverAllpass3_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		case 4: // LR2 Fast - no phase compensation (4 filter ops/channel)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverLR2Fast_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		case 5: // LR2 Full - with phase compensation (6 filter ops/channel)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverLR2_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		case 6: // LR4 Fast - no phase compensation (8 filter ops/channel)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverLR4Fast_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		case 7: // LR4 Full - with phase compensation (12 filter ops/channel)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverLR4_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		case 8: // Inverted - AP1 with swapped low/high bands (creative, same cost as AP1)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverAllpass1_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				// Swap low and high bands for inverted frequency response
				bandBufferL[0][i] = bandsL.high; // Was low
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.low; // Was high
				bandBufferR[0][i] = bandsR.high;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.low;
			}
			break;
		case 9: // Twist3 - 3 stages with progressive coefficient blending (6 ops/ch)
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverTwist3_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		case 0:
		default:
			for (size_t i = 0; i < buffer.size(); ++i) {
				filter::CrossoverBands bandsL, bandsR;
				crossoverAllpass1_.processStereo(buffer[i].l, buffer[i].r, bandsL, bandsR);
				bandBufferL[0][i] = bandsL.low;
				bandBufferL[1][i] = bandsL.mid;
				bandBufferL[2][i] = bandsL.high;
				bandBufferR[0][i] = bandsR.low;
				bandBufferR[1][i] = bandsR.mid;
				bandBufferR[2][i] = bandsR.high;
			}
			break;
		}

		FX_BENCH_STOP(benchXover);
		FX_BENCH_START(benchEnv);

		// Fixed threshold reference - thresholds are absolute, independent of track volume
		// Using full scale (ONE_Q31) as reference: log(2^31) ≈ 21.49
		constexpr float kThresholdRefdB = 21.49f;

		// Process each band - envelope detection
		// updateLevel scans all samples for accurate peak tracking
		// Use average (less stereo linking) when high band width > 1 (enhanced stereo)
		bool useAvgEnvelope = bandWidth_[2] > 1.0f;
		// Peak detection stride per band - safe based on Nyquist limits
		// Bass (<200Hz): stride=4 safe (200Hz → 220 samples/cycle, 4x stride → 55 samples/cycle)
		// Mid (200-2kHz): stride=2 safe (2kHz → 22 samples/cycle, 2x stride → 11 samples/cycle)
		// High (>2kHz): stride=1 required (up to 22kHz near Nyquist)
		static constexpr std::array<int32_t, kNumBands> kPeakStride = {4, 2, 1};

		std::array<float, kNumBands> bandGains;
		for (size_t b = 0; b < kNumBands; ++b) {
			// Calculate level using NEON-optimized peak detection with band-appropriate stride
			bands_[b].updateLevel(bandBufferL[b].data(), bandBufferR[b].data(), buffer.size(), alpha_, oneMinusAlpha_,
			                      useAvgEnvelope, kPeakStride[b]);

			// Calculate compression gain
			// Use character-derived knee and add per-band skew offset to global skew
			float bandSkew = std::clamp(upDownSkew_ + skewOffset_[b], -1.0f, 1.0f);
			bandGains[b] = bands_[b].calculateGain(static_cast<float>(buffer.size()), kThresholdRefdB, knee_, bandSkew,
			                                       frameCount_);
		}

		FX_BENCH_STOP(benchEnv);
		FX_BENCH_START(benchRecomb);

		// Fused gain apply + recombine loop
		// This combines compression gain, per-band output level, stereo width, and output gain
		// into a single pass over the data, reducing memory bandwidth

		// Compute target gains in q9.22 format
		std::array<int32_t, kNumBands> targetBandGainQ22;
		for (size_t b = 0; b < kNumBands; ++b) {
			float combinedGain = bandGains[b] * bands_[b].getOutputLevelLinear();
			targetBandGainQ22[b] = floatToGainQ22(combinedGain, kGainQ22MaxBand);
		}
		int32_t targetOutputGainQ22 = floatToGainQ22(outputGain_, kGainQ22MaxOutput);

		// Check convergence - skip smoothing if all gains are within threshold of target
		bool gainsConverged = true;
		for (size_t b = 0; b < kNumBands; ++b) {
			if (std::abs(smoothedBandGain_[b] - targetBandGainQ22[b]) > kGainConvergenceThreshold) {
				gainsConverged = false;
				break;
			}
		}
		if (std::abs(smoothedOutputGain_ - targetOutputGainQ22) > kGainConvergenceThreshold) {
			gainsConverged = false;
		}

		// Current smoothed gains are already in q9.22, ready for NEON
		std::array<int32_t, kNumBands> bandGainQ22 = smoothedBandGain_;
		int32_t outputGainQ22 = smoothedOutputGain_;

		// Pre-compute per-band stereo width as fixed-point
		// Width 0=mono, 1=unity, >1=enhanced, <0=inverted - clamped to [-2, 2] to avoid overflow
		q31_t widthFixedBass = static_cast<q31_t>(std::clamp(bandWidth_[0], -2.0f, 2.0f) * (ONE_Q31f / 2.0f));
		q31_t widthFixedMid = static_cast<q31_t>(std::clamp(bandWidth_[1], -2.0f, 2.0f) * (ONE_Q31f / 2.0f));
		q31_t widthFixedHigh = static_cast<q31_t>(std::clamp(bandWidth_[2], -2.0f, 2.0f) * (ONE_Q31f / 2.0f));

		q31_t peakThisBuffer = 0;                          // Track peak for metering
		std::array<q31_t, kNumBands> bandPeakThisBuffer{}; // Per-band peak tracking (post-level)
		const bool doMetering = meteringEnabled_;

		// Pre-broadcast gain and width values for NEON (4-sample parallel processing)
		int32x4_t gainVec0 = vdupq_n_s32(bandGainQ22[0]);
		int32x4_t gainVec1 = vdupq_n_s32(bandGainQ22[1]);
		int32x4_t gainVec2 = vdupq_n_s32(bandGainQ22[2]);
		int32x4_t gainVecOut = vdupq_n_s32(outputGainQ22);
		int32x4_t widthVecBass = vdupq_n_s32(widthFixedBass);
		int32x4_t widthVecMid = vdupq_n_s32(widthFixedMid);
		int32x4_t widthVecHigh = vdupq_n_s32(widthFixedHigh);
		// Fixed shift vector for q9.22 gain application (hoisted out of loop)
		const int32x4_t shiftVecQ22 = vdupq_n_s32(kGainQ22Shift);

		// NEON peak tracking vectors (reduced to scalar at end)
		int32x4_t peakVec = vdupq_n_s32(0);
		int32x4_t bandPeakVec0 = vdupq_n_s32(0);
		int32x4_t bandPeakVec1 = vdupq_n_s32(0);
		int32x4_t bandPeakVec2 = vdupq_n_s32(0);

		// Soft clip knee points: bands at +6dB, output at 0dBFS
		// When disabled, bypass soft clip calls entirely (saves ~8 NEON ops per iteration)
		const bool doSoftClip = softClipEnabled_;
		const int32_t bandClipKnee = EFFECTIVE_0DBFS_Q31 * 2;
		const int32_t outputClipKnee = EFFECTIVE_0DBFS_Q31;

		const size_t numSamples = buffer.size();
		const size_t vectorLen = numSamples & ~3; // Round down to multiple of 4

		// Main NEON loop - process 4 samples at a time
		// Gain smoothing stride counter (update every 8 samples = 2 iterations)
		int32_t smoothingCounter = 0;

		for (size_t i = 0; i < vectorLen; i += 4) {
			// Strided gain smoothing - update every kGainSmoothingStride samples
			if (!gainsConverged && (smoothingCounter & (kGainSmoothingStride / 4 - 1)) == 0) {
				// Integer IIR smooth toward target gains: gain += (target - gain) >> shift
				for (size_t b = 0; b < kNumBands; ++b) {
					smoothedBandGain_[b] += (targetBandGainQ22[b] - smoothedBandGain_[b]) >> kGainSmoothingShift;
					bandGainQ22[b] = smoothedBandGain_[b];
				}
				smoothedOutputGain_ += (targetOutputGainQ22 - smoothedOutputGain_) >> kGainSmoothingShift;
				outputGainQ22 = smoothedOutputGain_;

				// Rebuild NEON gain vectors with updated values
				gainVec0 = vdupq_n_s32(bandGainQ22[0]);
				gainVec1 = vdupq_n_s32(bandGainQ22[1]);
				gainVec2 = vdupq_n_s32(bandGainQ22[2]);
				gainVecOut = vdupq_n_s32(outputGainQ22);
			}
			++smoothingCounter;

			// === Band 0 (bass): M/S with per-band width (default 50%) ===
			int32x4_t L0 = vld1q_s32(&bandBufferL[0][i]);
			int32x4_t R0 = vld1q_s32(&bandBufferR[0][i]);
			int32x4_t mid0 = vhaddq_s32(L0, R0);
			int32x4_t side0 = vhsubq_s32(L0, R0);
			int32x4_t sideScaled0 = vqdmulhq_s32(side0, widthVecBass);
			int32x4_t msL0 = vaddq_s32(mid0, sideScaled0);
			int32x4_t msR0 = vsubq_s32(mid0, sideScaled0);
			int32x4_t scaledL0 = applyGainQ22Neon(msL0, gainVec0, shiftVecQ22);
			int32x4_t scaledR0 = applyGainQ22Neon(msR0, gainVec0, shiftVecQ22);
			if (doSoftClip) {
				scaledL0 = softClip_NEON(scaledL0, bandClipKnee);
				scaledR0 = softClip_NEON(scaledR0, bandClipKnee);
			}

			int32x4_t sumL = scaledL0;
			int32x4_t sumR = scaledR0;

			if (doMetering) {
				bandPeakVec0 = vmaxq_s32(bandPeakVec0, vmaxq_s32(vabsq_s32(scaledL0), vabsq_s32(scaledR0)));
			}

			// === Band 1 (mid): M/S with per-band width ===
			int32x4_t L1 = vld1q_s32(&bandBufferL[1][i]);
			int32x4_t R1 = vld1q_s32(&bandBufferR[1][i]);
			int32x4_t mid1 = vhaddq_s32(L1, R1);
			int32x4_t side1 = vhsubq_s32(L1, R1);
			int32x4_t sideScaled1 = vqdmulhq_s32(side1, widthVecMid);
			int32x4_t msL1 = vaddq_s32(mid1, sideScaled1);
			int32x4_t msR1 = vsubq_s32(mid1, sideScaled1);
			int32x4_t scaledL1 = applyGainQ22Neon(msL1, gainVec1, shiftVecQ22);
			int32x4_t scaledR1 = applyGainQ22Neon(msR1, gainVec1, shiftVecQ22);
			if (doSoftClip) {
				scaledL1 = softClip_NEON(scaledL1, bandClipKnee);
				scaledR1 = softClip_NEON(scaledR1, bandClipKnee);
			}
			sumL = vqaddq_s32(sumL, scaledL1);
			sumR = vqaddq_s32(sumR, scaledR1);

			if (doMetering) {
				bandPeakVec1 = vmaxq_s32(bandPeakVec1, vmaxq_s32(vabsq_s32(scaledL1), vabsq_s32(scaledR1)));
			}

			// === Band 2 (high): M/S with per-band width ===
			int32x4_t L2 = vld1q_s32(&bandBufferL[2][i]);
			int32x4_t R2 = vld1q_s32(&bandBufferR[2][i]);
			int32x4_t mid2 = vhaddq_s32(L2, R2);
			int32x4_t side2 = vhsubq_s32(L2, R2);
			int32x4_t sideScaled2 = vqdmulhq_s32(side2, widthVecHigh);
			int32x4_t msL2 = vaddq_s32(mid2, sideScaled2);
			int32x4_t msR2 = vsubq_s32(mid2, sideScaled2);
			int32x4_t scaledL2 = applyGainQ22Neon(msL2, gainVec2, shiftVecQ22);
			int32x4_t scaledR2 = applyGainQ22Neon(msR2, gainVec2, shiftVecQ22);
			if (doSoftClip) {
				scaledL2 = softClip_NEON(scaledL2, bandClipKnee);
				scaledR2 = softClip_NEON(scaledR2, bandClipKnee);
			}
			sumL = vqaddq_s32(sumL, scaledL2);
			sumR = vqaddq_s32(sumR, scaledR2);

			if (doMetering) {
				bandPeakVec2 = vmaxq_s32(bandPeakVec2, vmaxq_s32(vabsq_s32(scaledL2), vabsq_s32(scaledR2)));
			}

			// === Output gain + soft clip ===
			int32x4_t outLVec = applyGainQ22Neon(sumL, gainVecOut, shiftVecQ22);
			int32x4_t outRVec = applyGainQ22Neon(sumR, gainVecOut, shiftVecQ22);
			if (doSoftClip) {
				outLVec = softClip_NEON(outLVec, outputClipKnee);
				outRVec = softClip_NEON(outRVec, outputClipKnee);
			}

			if (doMetering) {
				peakVec = vmaxq_s32(peakVec, vmaxq_s32(vabsq_s32(outLVec), vabsq_s32(outRVec)));
			}

			// Extract lanes and store to buffer
			// DC block bypassed - symmetric soft clip doesn't introduce DC offset
			if constexpr (kBypassDCBlock) {
				vst1q_lane_s32(&buffer[i + 0].l, outLVec, 0);
				vst1q_lane_s32(&buffer[i + 0].r, outRVec, 0);
				vst1q_lane_s32(&buffer[i + 1].l, outLVec, 1);
				vst1q_lane_s32(&buffer[i + 1].r, outRVec, 1);
				vst1q_lane_s32(&buffer[i + 2].l, outLVec, 2);
				vst1q_lane_s32(&buffer[i + 2].r, outRVec, 2);
				vst1q_lane_s32(&buffer[i + 3].l, outLVec, 3);
				vst1q_lane_s32(&buffer[i + 3].r, outRVec, 3);
			}
			else {
				// DC block path (scalar - has state dependency)
				q31_t out0L = vgetq_lane_s32(outLVec, 0);
				q31_t out0R = vgetq_lane_s32(outRVec, 0);
				buffer[i + 0].l = out0L - dcBlockL_.doFilter(out0L, kDCBlockCoeff);
				buffer[i + 0].r = out0R - dcBlockR_.doFilter(out0R, kDCBlockCoeff);

				q31_t out1L = vgetq_lane_s32(outLVec, 1);
				q31_t out1R = vgetq_lane_s32(outRVec, 1);
				buffer[i + 1].l = out1L - dcBlockL_.doFilter(out1L, kDCBlockCoeff);
				buffer[i + 1].r = out1R - dcBlockR_.doFilter(out1R, kDCBlockCoeff);

				q31_t out2L = vgetq_lane_s32(outLVec, 2);
				q31_t out2R = vgetq_lane_s32(outRVec, 2);
				buffer[i + 2].l = out2L - dcBlockL_.doFilter(out2L, kDCBlockCoeff);
				buffer[i + 2].r = out2R - dcBlockR_.doFilter(out2R, kDCBlockCoeff);

				q31_t out3L = vgetq_lane_s32(outLVec, 3);
				q31_t out3R = vgetq_lane_s32(outRVec, 3);
				buffer[i + 3].l = out3L - dcBlockL_.doFilter(out3L, kDCBlockCoeff);
				buffer[i + 3].r = out3R - dcBlockR_.doFilter(out3R, kDCBlockCoeff);
			}
		}

		// Handle remainder samples with scalar fallback (0-3 samples)
		for (size_t i = vectorLen; i < numSamples; ++i) {
			q31_t sumL = 0, sumR = 0;

			// Band 0 (bass): M/S with per-band width
			q31_t mid0 = (bandBufferL[0][i] >> 1) + (bandBufferR[0][i] >> 1);
			q31_t side0 = (bandBufferL[0][i] >> 1) - (bandBufferR[0][i] >> 1);
			q31_t sideScaled0 = multiply_32x32_rshift32(side0, widthFixedBass) << 1;
			q31_t gainedL0 = applyGainQ22(mid0 + sideScaled0, bandGainQ22[0]);
			q31_t gainedR0 = applyGainQ22(mid0 - sideScaled0, bandGainQ22[0]);
			sumL = add_saturate(sumL, doSoftClip ? softClip(gainedL0, bandClipKnee) : gainedL0);
			sumR = add_saturate(sumR, doSoftClip ? softClip(gainedR0, bandClipKnee) : gainedR0);

			// Band 1 (mid): M/S with per-band width
			q31_t mid1 = (bandBufferL[1][i] >> 1) + (bandBufferR[1][i] >> 1);
			q31_t side1 = (bandBufferL[1][i] >> 1) - (bandBufferR[1][i] >> 1);
			q31_t sideScaled1 = multiply_32x32_rshift32(side1, widthFixedMid) << 1;
			q31_t gainedL1 = applyGainQ22(mid1 + sideScaled1, bandGainQ22[1]);
			q31_t gainedR1 = applyGainQ22(mid1 - sideScaled1, bandGainQ22[1]);
			sumL = add_saturate(sumL, doSoftClip ? softClip(gainedL1, bandClipKnee) : gainedL1);
			sumR = add_saturate(sumR, doSoftClip ? softClip(gainedR1, bandClipKnee) : gainedR1);

			// Band 2 (high): M/S with per-band width
			q31_t mid2 = (bandBufferL[2][i] >> 1) + (bandBufferR[2][i] >> 1);
			q31_t side2 = (bandBufferL[2][i] >> 1) - (bandBufferR[2][i] >> 1);
			q31_t sideScaled2 = multiply_32x32_rshift32(side2, widthFixedHigh) << 1;
			q31_t gainedL2 = applyGainQ22(mid2 + sideScaled2, bandGainQ22[2]);
			q31_t gainedR2 = applyGainQ22(mid2 - sideScaled2, bandGainQ22[2]);
			sumL = add_saturate(sumL, doSoftClip ? softClip(gainedL2, bandClipKnee) : gainedL2);
			sumR = add_saturate(sumR, doSoftClip ? softClip(gainedR2, bandClipKnee) : gainedR2);

			// Output gain + soft clip
			q31_t gainedOutL = applyGainQ22(sumL, outputGainQ22);
			q31_t gainedOutR = applyGainQ22(sumR, outputGainQ22);
			q31_t outL = doSoftClip ? softClip(gainedOutL, outputClipKnee) : gainedOutL;
			q31_t outR = doSoftClip ? softClip(gainedOutR, outputClipKnee) : gainedOutR;

			// DC block bypassed - symmetric soft clip doesn't introduce DC offset
			if constexpr (kBypassDCBlock) {
				buffer[i].l = outL;
				buffer[i].r = outR;
			}
			else {
				buffer[i].l = outL - dcBlockL_.doFilter(outL, kDCBlockCoeff);
				buffer[i].r = outR - dcBlockR_.doFilter(outR, kDCBlockCoeff);
			}
		}

		// Reduce NEON peak vectors to scalars (ARMv7 horizontal max)
		if (doMetering) {
			// peakThisBuffer = horizontal max of peakVec
			int32x2_t peak2 = vmax_s32(vget_low_s32(peakVec), vget_high_s32(peakVec));
			peak2 = vpmax_s32(peak2, peak2);
			peakThisBuffer = vget_lane_s32(peak2, 0);

			// bandPeakThisBuffer[0]
			int32x2_t bp0 = vmax_s32(vget_low_s32(bandPeakVec0), vget_high_s32(bandPeakVec0));
			bp0 = vpmax_s32(bp0, bp0);
			bandPeakThisBuffer[0] = vget_lane_s32(bp0, 0);

			// bandPeakThisBuffer[1]
			int32x2_t bp1 = vmax_s32(vget_low_s32(bandPeakVec1), vget_high_s32(bandPeakVec1));
			bp1 = vpmax_s32(bp1, bp1);
			bandPeakThisBuffer[1] = vget_lane_s32(bp1, 0);

			// bandPeakThisBuffer[2]
			int32x2_t bp2 = vmax_s32(vget_low_s32(bandPeakVec2), vget_high_s32(bandPeakVec2));
			bp2 = vpmax_s32(bp2, bp2);
			bandPeakThisBuffer[2] = vget_lane_s32(bp2, 0);
		}

		// Metering calculations - only run when analyzer is enabled
		if (doMetering) {
			// Update per-band output peaks with decay (~50ms)
			// This is cheap: just max and multiply per band
			for (size_t b = 0; b < kNumBands; ++b) {
				bandOutputPeak_[b] = std::max(bandPeakThisBuffer[b], static_cast<q31_t>(bandOutputPeak_[b] * 0.95f));
			}

			// Track output level using peak from this buffer
			outputPeak_ = std::max(peakThisBuffer, static_cast<q31_t>(outputPeak_ * 0.95f));

			// Increment refresh counter - only do expensive calculations on refresh frames
			if (++meterRefreshCounter_ >= kMeterRefreshBuffers) {
				meterRefreshCounter_ = 0;
				meterNeedsRefresh_ = true;

				// Saturation detection with hold timer (~500ms) - only on refresh frames
				constexpr q31_t saturationThreshold = EFFECTIVE_0DBFS_Q31;
				for (size_t b = 0; b < kNumBands; ++b) {
					if (bandOutputPeak_[b] > saturationThreshold) {
						bandSaturationHoldCounter_[b] = kIndicatorHoldBuffers / kMeterRefreshBuffers;
					}
					else if (bandSaturationHoldCounter_[b] > 0) {
						bandSaturationHoldCounter_[b]--;
					}
					bandSaturating_[b] = (bandSaturationHoldCounter_[b] > 0);
				}

				// Clipping detection with hold timer - only on refresh frames
				constexpr q31_t clipThreshold = EFFECTIVE_0DBFS_Q31;
				if (outputPeak_ > clipThreshold) {
					clippingHoldCounter_ = kIndicatorHoldBuffers / kMeterRefreshBuffers;
				}
				else if (clippingHoldCounter_ > 0) {
					clippingHoldCounter_--;
				}
				clipping_ = (clippingHoldCounter_ > 0);
			}
		}

		// Final gain convergence - snap to target if close enough
		// This ensures we fully reach target over multiple buffers
		if (!gainsConverged) {
			for (size_t b = 0; b < kNumBands; ++b) {
				if (std::abs(smoothedBandGain_[b] - targetBandGainQ22[b]) < kGainConvergenceThreshold) {
					smoothedBandGain_[b] = targetBandGainQ22[b];
				}
			}
			if (std::abs(smoothedOutputGain_ - targetOutputGainQ22) < kGainConvergenceThreshold) {
				smoothedOutputGain_ = targetOutputGainQ22;
			}
		}

		// Apply wet/dry blend
		if (wet_ != ONE_Q31) {
			for (size_t i = 0; i < buffer.size(); ++i) {
				buffer[i].l = static_cast<q31_t>(static_cast<float>(buffer[i].l) * wet_ / ONE_Q31f
				                                 + static_cast<float>(dryBuffer[i].l) * dry_);
				buffer[i].r = static_cast<q31_t>(static_cast<float>(buffer[i].r) * wet_ / ONE_Q31f
				                                 + static_cast<float>(dryBuffer[i].r) * dry_);
			}
		}

		FX_BENCH_STOP(benchRecomb);
		FX_BENCH_STOP(benchTotal);
	}

	/// Get combined gain reduction for display (average of all bands)
	[[nodiscard]] uint8_t getGainReduction() const {
		float totalReduction = 0.0f;
		for (const auto& band : bands_) {
			totalReduction += std::abs(band.getGainReductionDB());
		}
		return static_cast<uint8_t>(std::clamp(totalReduction * 4.0f * 4.0f / kNumBands, 0.0f, 127.0f));
	}

	/// Get gain change for a specific band (bipolar: -127 to +127)
	/// Negative = downward compression (gain reduction) → meter bars go DOWN
	/// Positive = upward compression (gain boost) → meter bars go UP
	[[nodiscard]] int8_t getBandGainReduction(size_t bandIndex) const {
		if (bandIndex >= kNumBands) {
			return 0;
		}
		// envelope_ is in natural log units, convert to dB:
		// dB = 20 * log10(exp(envelope_)) = 20 * envelope_ / ln(10) ≈ 8.686 * envelope_
		float envelope = bands_[bandIndex].getGainReductionDB();
		// Apply noise floor - ignore very small envelope values (< 0.1dB)
		if (std::abs(envelope) < 0.012f) { // ~0.1dB in natural log units
			return 0;
		}
		float grDB = envelope * 8.686f; // Convert from natural log to dB
		// Scale ±12dB to ±127, preserve sign so bars match gain direction
		float scaled = std::clamp(grDB * (127.0f / 12.0f), -127.0f, 127.0f);
		return static_cast<int8_t>(scaled);
	}

	/// Get input level for a specific band (0-127 scale for metering)
	/// dBFS-linear scale: -48dBFS = 0, -24dBFS = ~64 (half), 0dBFS = 127
	[[nodiscard]] uint8_t getBandInputLevel(size_t bandIndex) const {
		if (bandIndex >= kNumBands) {
			return 0;
		}
		// rms_ is log(level) where level is in raw sample space
		// Convert to dBFS: dBFS = 20 * log10(level / ONE_Q31) = 8.686 * (rms_ - 21.5)
		float level = bands_[bandIndex].getInputLevelLog();
		constexpr float refLevel = 21.5f; // log(ONE_Q31)
		float dBFS = 8.686f * (level - refLevel);
		// Noise floor at -48dBFS
		if (dBFS < -48.0f) {
			return 0;
		}
		// Linear mapping from [-48, 0] dBFS to [0, 127]
		float scaled = (dBFS + 48.0f) * (127.0f / 48.0f);
		return static_cast<uint8_t>(std::clamp(scaled, 0.0f, 127.0f));
	}

	/// Get output level for a specific band (0-127 scale for metering)
	/// Shows pre-saturation signal level, dBFS scale: -48dBFS = 0, 0dBFS = 127
	/// Note: 0dBFS is calibrated to downstream clip point (~24dB below internal full scale)
	[[nodiscard]] uint8_t getBandOutputLevel(size_t bandIndex) const {
		if (bandIndex >= kNumBands) {
			return 0;
		}
		q31_t peak = bandOutputPeak_[bandIndex];
		if (peak < 1000) {
			return 0;
		}
		// Use system constant for downstream clipping point
		float peakNormalized = static_cast<float>(peak) / EFFECTIVE_0DBFS_Q31f;
		// 20 * log10(x) = 8.686 * ln(x) - use fastLog for consistency
		float dB = 8.686f * fastLog(peakNormalized + 1e-10f);
		// Range: -48dBFS (bottom) to 0dBFS (full scale)
		if (dB < -48.0f) {
			return 0;
		}
		float scaled = (dB + 48.0f) * (127.0f / 48.0f);
		return static_cast<uint8_t>(std::clamp(scaled, 0.0f, 127.0f));
	}

	/// Get threshold position for metering (0.0 to 1.0, where 0=bottom, 1=top of meter)
	/// This represents where the threshold tick should be drawn on the band meter
	[[nodiscard]] float getBandThresholdPosition(size_t bandIndex) const {
		if (bandIndex >= kNumBands) {
			return 0.5f;
		}
		// thresholdDown_ is 0.2-1.0, map to 0-1 for meter position
		float threshold = bands_[bandIndex].getThresholdLog();
		return (threshold - 0.2f) / 0.8f;
	}

	/// Get output level for metering (0-127 scale)
	/// Range: -48dBFS = 0, 0dBFS = 127
	/// Note: 0dBFS is calibrated to downstream clip point (~24dB below internal full scale)
	[[nodiscard]] uint8_t getOutputLevel() const {
		if (outputPeak_ < 1000) {
			return 0;
		}
		// Use system constant for downstream clipping point
		float peakNormalized = static_cast<float>(outputPeak_) / EFFECTIVE_0DBFS_Q31f;
		// 20 * log10(x) = 8.686 * ln(x) - use fastLog for consistency
		float dB = 8.686f * fastLog(peakNormalized + 1e-10f);
		// Range: -48dBFS (bottom) to 0dBFS (full scale)
		if (dB < -48.0f) {
			return 0;
		}
		float scaled = (dB + 48.0f) * (127.0f / 48.0f);
		return static_cast<uint8_t>(std::clamp(scaled, 0.0f, 127.0f));
	}

	/// Get raw output peak value for debugging
	[[nodiscard]] q31_t getOutputPeak() const { return outputPeak_; }

	/// Check if output is clipping (exceeded 0dBFS in recent buffer)
	[[nodiscard]] bool isClipping() const { return clipping_; }

	/// Clear the clipping indicator (also auto-clears after ~500ms)
	void clearClipping() {
		clipping_ = false;
		clippingHoldCounter_ = 0;
	}

	/// Check if a specific band is saturating (hitting the tanh soft clipper)
	[[nodiscard]] bool isBandSaturating(size_t bandIndex) const {
		if (bandIndex >= kNumBands) {
			return false;
		}
		return bandSaturating_[bandIndex];
	}

private:
	/// Update pre-computed envelope alpha values from response_
	/// Called when response_ changes (in setCharacter)
	void updateEnvelopeAlpha() {
		// alpha = 0.60 - response * 0.58, range [0.02, 0.60]
		alpha_ = 0.60f - response_ * 0.58f;
		oneMinusAlpha_ = 1.0f - alpha_;
	}

	// Crossover filters - ordered by CPU cost (cheapest to most expensive)
	// Type 0: AllpassCrossoverLR1 - 6dB/oct (2 ops/ch), cheapest, default
	// Type 1: AllpassCrossoverLR2 - "Quirky" (4 ops/ch)
	// Type 2: AllpassCrossoverTwisted - "Twisted" (4 ops/ch), mixed coefficients
	// Type 3: AllpassCrossoverLR3 - "Weird" (6 ops/ch)
	// Type 4: LR2CrossoverFast - 12dB/oct (4 ops/ch), no phase comp
	// Type 5: LR2CrossoverFull - 12dB/oct (6 ops/ch), with phase comp
	// Type 6: LR4CrossoverFast - 24dB/oct (8 ops/ch), no phase comp
	// Type 7: LR4CrossoverFull - 24dB/oct (12 ops/ch), with phase comp
	filter::AllpassCrossoverLR1 crossoverAllpass1_;    // Type 0 - 6dB/oct (cheapest)
	filter::AllpassCrossoverLR2 crossoverAllpass2_;    // Type 1 - "Quirky"
	filter::AllpassCrossoverTwisted crossoverTwisted_; // Type 2 - "Twisted"
	filter::AllpassCrossoverLR3 crossoverAllpass3_;    // Type 3 - "Weird"
	filter::LR2CrossoverFast crossoverLR2Fast_;        // Type 4 - 12dB/oct no phase comp
	filter::LR2CrossoverFull crossoverLR2_;            // Type 5 - 12dB/oct with phase comp
	filter::LR4CrossoverFast crossoverLR4Fast_;        // Type 6 - 24dB/oct no phase comp
	filter::LR4CrossoverFull crossoverLR4_;            // Type 7 - 24dB/oct with phase comp
	filter::AllpassCrossoverTwist3 crossoverTwist3_;   // Type 9 - "Twist3" (Twisted+Weird)
	uint8_t crossoverType_ = 0;                        // Default to cheapest (1st order allpass)
	std::array<BandCompressor, kNumBands> bands_;
	q31_t wet_ = ONE_Q31;
	float dry_ = 0.0f;
	q31_t outputGainKnob_ = (ONE_Q31 / 5) * 3; // Default +12dB (~4x) to compensate for low band gains
	float outputGain_ = 4.18f;                 // Linear output gain multiplier (matches knob)

	// Character knob (replaces knee) - controls width, knee, timing, skew
	q31_t characterKnob_ = 0;        // Default 0 (Width zone start) - neutral settings
	bool characterComputed_ = false; // Cache flag: true after first setCharacter() call
	std::array<float, 3> bandWidth_{0.7f, 0.85f,
	                                0.9f}; // Per-band stereo width [bass, mid, high]: 0=mono, 1=unity, >1=enhanced
	float knee_ = 0.2f;                    // 0=hard, 1=soft (derived from character) - steepish default
	std::array<float, kNumBands> timingOffset_{0.0f, 0.0f, 0.0f}; // Per-band timing multiplier offset
	std::array<float, kNumBands> skewOffset_{0.0f, 0.0f, 0.0f};   // Per-band up/down skew offset

	q31_t upDownSkewKnob_ = ONE_Q31 / 2; // Default balanced
	float upDownSkew_ = 0.0f;            // -1=upward, 0=balanced, +1=downward

	// Response - controlled by Feel zone, not a separate knob
	float response_ = 0.5f; // 0=smooth/MAV, 1=punchy/peak

	// Pre-computed envelope alpha values (updated when response_ changes)
	float alpha_ = 0.31f; // 0.60 - response * 0.58
	float oneMinusAlpha_ = 0.69f;

	// Vibe knob - controls phase relationships between oscillations in Feel
	q31_t vibeKnob_ = 0;                                    // Default 0 (Sync zone start)
	std::array<float, 3> vibePhaseWidth_{0.0f, 0.0f, 0.0f}; // Phase offset for width oscillation [bass, mid, high]
	float vibePhaseKnee_ = 0.0f;                            // Phase offset for knee oscillation
	std::array<float, kNumBands> vibePhaseTiming_{0.0f, 0.0f, 0.0f}; // Phase offsets for timing
	std::array<float, kNumBands> vibePhaseSkew_{0.0f, 0.0f, 0.0f};   // Phase offsets for skew
	float vibeTwist_ = 1.0f;                                         // Twist amount for Twisted/Twist3 (0-1)
	float vibePhaseOffset_ = 0.0f;                                   // Secret phase offset for twist modulation
	float feelPhaseOffset_ = 0.0f;                                   // Secret phase offset for feel phi triangles

	// Enable/disable zone (0 = off, >ONE_Q31/2 = on)
	q31_t enabledZone_{0};

	// Linked values (global controls - per-band offsets are computed dynamically)
	q31_t linkedThreshold_{ONE_Q31 / 2};
	q31_t linkedRatio_{0};
	q31_t linkedBandwidth_{ONE_Q31 / 2};

	// Saturation state for antialiasing (per-band, per-channel)
	// Initialize to midpoint (2147483648 = 0x80000000) which represents zero signal
	// Using 0 causes DC offset because the 2D interpolation sees a transition from "large negative" to "zero"
	static constexpr uint32_t kSaturationNeutral = 2147483648u;
	std::array<uint32_t, kNumBands> saturationStateL_{kSaturationNeutral, kSaturationNeutral, kSaturationNeutral};
	std::array<uint32_t, kNumBands> saturationStateR_{kSaturationNeutral, kSaturationNeutral, kSaturationNeutral};

	// DC-blocking high-pass filter - BYPASSED
	// The soft clipper is symmetric around zero, so it doesn't introduce DC offset.
	// Asymmetric saturation (e.g., tube) would need DC blocking, but our soft clip doesn't.
	// Keeping the filter components for potential future use with asymmetric saturation.
	static constexpr bool kBypassDCBlock = true;
	static constexpr q31_t kDCBlockCoeff = static_cast<q31_t>((5.0f / kSampleRate) * ONE_Q31);
	filter::BasicFilterComponent dcBlockL_;
	filter::BasicFilterComponent dcBlockR_;

	// Output metering state
	q31_t outputPeak_{0};                                        // Peak output level for metering
	bool clipping_{false};                                       // True if output exceeded 0dBFS recently
	uint8_t clippingHoldCounter_{0};                             // Hold counter for clip indicator
	std::array<bool, kNumBands> bandSaturating_{};               // Per-band saturation indicators
	std::array<uint8_t, kNumBands> bandSaturationHoldCounter_{}; // Hold counters for saturation indicators
	std::array<q31_t, kNumBands> bandOutputPeak_{};              // Per-band output peak levels

	// Hold time for indicators (~500ms at 345 buffers/sec)
	static constexpr uint8_t kIndicatorHoldBuffers = 170;

	// Frame counter for gap detection (passed to band compressors)
	uint32_t frameCount_{0};

	// UI refresh counter for meter animation (~10fps at 44.1kHz/128 samples)
	static constexpr uint8_t kMeterRefreshBuffers = 35;
	uint8_t meterRefreshCounter_{0};
	bool meterNeedsRefresh_{false}; // Set by audio path, cleared by UI

	// Gain smoothing state - prevents zipper noise from step-wise gain changes
	// Stored in q9.22 format for direct use in audio loop (no float conversion)
	std::array<int32_t, kNumBands> smoothedBandGain_{kGainQ22Unity, kGainQ22Unity, kGainQ22Unity};
	int32_t smoothedOutputGain_{kGainQ22Unity};

	// Gain smoothing constants - update every 8 samples (2 NEON iterations)
	static constexpr int32_t kGainSmoothingStride = 8;
	// Alpha as right-shift for integer IIR: gain += (target - gain) >> kGainSmoothingShift
	// Shift of 4 gives alpha ≈ 0.0625, similar to previous 0.09 float alpha
	// Time constant: ~2ms at 44.1kHz with stride=8
	static constexpr int32_t kGainSmoothingShift = 4;
	// Convergence threshold in q9.22 - skip smoothing when within this delta of target
	// ~0.1% of unity gain = 4194 (0.001 * 2^22)
	static constexpr int32_t kGainConvergenceThreshold = kGainQ22Unity >> 10;

public:
	// ========== Serialization ==========

	/// Write multiband compressor state to file (only non-default values)
	void writeToFile(Serializer& writer) const {
		if (isEnabled()) {
			deluge::storage::writeAttributeInt(writer, "mbEnabled", 1);
		}
		WRITE_FIELD_DEFAULT(writer, crossoverType_, "mbCrossoverType", 2);
		if (!softClipEnabled_) { // Default is true, only write when disabled
			deluge::storage::writeAttributeInt(writer, "mbSoftClip", 0);
		}
		WRITE_FLOAT(writer, vibePhaseOffset_, "mbVibeTwistPhase", 10.0f);
		WRITE_FLOAT(writer, feelPhaseOffset_, "mbFeelMetaPhase", 10.0f);

		// Per-band offsets (write with index suffix)
		for (size_t i = 0; i < kNumBands; ++i) {
			q31_t thresholdOffset = getThresholdOffset(i);
			q31_t ratioOffset = getRatioOffset(i);
			q31_t bandwidthOffset = getBandwidthOffset(i);

			char tag[24];
			if (thresholdOffset != 0) {
				snprintf(tag, sizeof(tag), "mbThresholdOffset%zu", i);
				deluge::storage::writeAttributeHex(writer, tag, thresholdOffset);
			}
			if (ratioOffset != 0) {
				snprintf(tag, sizeof(tag), "mbRatioOffset%zu", i);
				deluge::storage::writeAttributeHex(writer, tag, ratioOffset);
			}
			if (bandwidthOffset != 0) {
				snprintf(tag, sizeof(tag), "mbBandwidthOffset%zu", i);
				deluge::storage::writeAttributeHex(writer, tag, bandwidthOffset);
			}
		}
	}

	/// Read a tag into multiband compressor state, returns true if tag was handled
	bool readTag(Deserializer& reader, const char* tagName) {
		if (std::strcmp(tagName, "mbEnabled") == 0) {
			int32_t enabled = deluge::storage::readAndExitTag(reader, tagName);
			setEnabledZone(enabled ? ONE_Q31 : 0);
			return true;
		}
		if (std::strcmp(tagName, "mbSoftClip") == 0) {
			softClipEnabled_ = deluge::storage::readAndExitTag(reader, tagName) != 0;
			return true;
		}
		READ_FIELD(reader, tagName, crossoverType_, "mbCrossoverType");
		READ_FLOAT(reader, tagName, vibePhaseOffset_, "mbVibeTwistPhase", 10.0f);
		READ_FLOAT(reader, tagName, feelPhaseOffset_, "mbFeelMetaPhase", 10.0f);

		// Per-band offsets (check each index)
		for (size_t i = 0; i < kNumBands; ++i) {
			char tag[24];
			q31_t value;

			snprintf(tag, sizeof(tag), "mbThresholdOffset%zu", i);
			if (std::strcmp(tagName, tag) == 0) {
				value = deluge::storage::readHexAndExitTag(reader, tagName);
				setThresholdOffset(i, value);
				return true;
			}

			snprintf(tag, sizeof(tag), "mbRatioOffset%zu", i);
			if (std::strcmp(tagName, tag) == 0) {
				value = deluge::storage::readHexAndExitTag(reader, tagName);
				setRatioOffset(i, value);
				return true;
			}

			snprintf(tag, sizeof(tag), "mbBandwidthOffset%zu", i);
			if (std::strcmp(tagName, tag) == 0) {
				value = deluge::storage::readHexAndExitTag(reader, tagName);
				setBandwidthOffset(i, value);
				return true;
			}
		}

		return false;
	}

	/// Check if meter display needs refresh (called by UI)
	/// Returns true once per refresh interval, then auto-clears
	[[nodiscard]] bool checkAndClearMeterRefresh() {
		if (meterNeedsRefresh_) {
			meterNeedsRefresh_ = false;
			return true;
		}
		return false;
	}

	/// Enable/disable metering calculations (saves CPU when analyzer is off)
	void setMeteringEnabled(bool enabled) { meteringEnabled_ = enabled; }
	[[nodiscard]] bool isMeteringEnabled() const { return meteringEnabled_; }

	/// Enable/disable soft clipping on bands and output
	void setSoftClipEnabled(bool enabled) { softClipEnabled_ = enabled; }
	[[nodiscard]] bool isSoftClipEnabled() const { return softClipEnabled_; }

private:
	bool meteringEnabled_{true}; // Metering calculations enabled (can be disabled to save CPU)
	bool softClipEnabled_{true}; // Soft clipping on bands and output (prevents harsh digital clipping)
};

} // namespace deluge::dsp
