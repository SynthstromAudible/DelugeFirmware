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

// Automodulator DSP - auto-wah/filter/tremolo/comb effect
// Features:
// - SVF filter for auto-wah
// - Comb filter with modulated delay
// - Tremolo/VCA
// - LFO with tempo sync option
// - Stereo phase offset

#include "definitions.h"
#include "dsp/phi_triangle.hpp"
#include "dsp/stereo_sample.h"
#include "memory/memory_allocator_interface.h"
#include "model/sync.h"
#include "util/fixedpoint.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace deluge::dsp {

// ============================================================================
// Constants
// ============================================================================

// Pre-computed reciprocals to convert divisions to multiplications (faster on ARM)
constexpr float kOneOverQ31Max = 1.0f / 2147483647.0f; // For q31 → float normalization
constexpr float kDerivNormScale = 1.0f / 33554432.0f;  // For derivative → [-1,+1] range
constexpr float kQ31MaxFloat = 2147483647.0f;          // For float → q31 conversion
constexpr float kPhaseMaxFloat = 4294967295.0f;        // For float → uint32 phase

// Smoothing coefficient for buffer-level modulation updates
// ~0.25 gives smooth 4-buffer (~12ms) transition at 44.1kHz/128 samples
constexpr float kModSmoothCoeff = 0.25f;
constexpr q31_t kModSmoothCoeffQ = static_cast<q31_t>(kModSmoothCoeff * 2147483647.0f);

// Pitch tracking constants
constexpr int32_t kNoteCodeInvalid = -1; // No pitch info available
constexpr float kA4Hz = 440.0f;          // A4 reference frequency

// LFO rate lookup table (Hz values for each zone, exponentially spaced)
// Max ~10Hz ensures at least 3 buffers per cycle for clean interpolation
constexpr float kAutomodLfoRates[8] = {
    0.1f, 0.2f, 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 10.0f,
};

// LFO rate range for phi triangle mode (free-running)
constexpr float kLfoRateMin = 0.1f;  // Hz (slowest)
constexpr float kLfoRateMax = 10.0f; // Hz (fastest, ~35 buffers/cycle)

// ============================================================================
// Phi Triangle Configuration Constants
// ============================================================================

/// Phi triangle config for LFO rate (used when phaseOffset > 0)
/// BIPOLAR: positive = free-running Hz, negative = tempo-synced subdivision
constexpr phi::PhiTriConfig kLfoRateTriangle = {phi::kPhi075, 0.7f, 0.0f, true};

/// Phi triangle bank for filter output mixing (derived from type)
constexpr std::array<phi::PhiTriConfig, 3> kFilterMixBank = {{
    {phi::kPhi200, 0.6f, 0.00f, false}, // [0] Lowpass: φ^2.0, 60% duty
    {phi::kPhi300, 0.5f, 0.33f, false}, // [1] Bandpass: φ^3.0, 50% duty, offset
    {phi::kPhi400, 0.4f, 0.67f, false}, // [2] Highpass: φ^4.0, 40% duty, offset
}};

/// Phi triangle config for stereo phase offset derived from mod
constexpr phi::PhiTriConfig kStereoOffsetTriangle = {phi::kPhi150, 0.7f, 0.0f, false};

/// Phi triangle config for LFO initial phase derived from mod
constexpr phi::PhiTriConfig kLfoInitialPhaseTriangle = {phi::kPhi250, 1.0f, 0.0f, false};

/// Phi triangle config for envelope→depth influence (BIPOLAR)
constexpr phi::PhiTriConfig kEnvDepthInfluenceTriangle = {phi::kPhi150, 0.5f, 0.0f, true};

/// Phi triangle config for envelope→phase influence (BIPOLAR)
constexpr phi::PhiTriConfig kEnvPhaseInfluenceTriangle = {phi::kPhi175, 0.5f, 0.5f, true};

/// Phi triangle config for derivative envelope→depth influence (BIPOLAR)
constexpr phi::PhiTriConfig kEnvDerivDepthInfluenceTriangle = {phi::kPhi125, 0.5f, 0.25f, true};

/// Phi triangle config for derivative envelope→phase influence (BIPOLAR)
constexpr phi::PhiTriConfig kEnvDerivPhaseInfluenceTriangle = {phi::kPhi150, 0.5f, 0.75f, true};

/// Phi triangle config for comb feedback derived from type
constexpr phi::PhiTriConfig kCombFeedbackTriangle = {phi::kPhi250, 0.8f, 0.0f, false};

/// Phi triangle config for comb wet/dry mix derived from type
constexpr phi::PhiTriConfig kCombMixTriangle = {phi::kPhi350, 0.85f, 0.33f, false};

/// Phi triangle config for tremolo depth derived from type
constexpr phi::PhiTriConfig kTremoloDepthTriangle = {phi::kPhi275, 0.7f, 0.5f, false};

/// Phi triangle config for tremolo phase offset derived from type
constexpr phi::PhiTriConfig kTremoloPhaseOffsetTriangle = {phi::kPhi225, 0.6f, 0.75f, false};

/// Phi triangle config for comb LFO depth derived from flavor
constexpr phi::PhiTriConfig kCombLfoDepthTriangle = {phi::kPhi175, 0.75f, 0.5f, false};

/// Phi triangle config for comb static offset derived from flavor
constexpr phi::PhiTriConfig kCombStaticOffsetTriangle = {phi::kPhi125, 0.85f, 0.25f, false};

/// Phi triangle config for comb LFO phase offset derived from flavor
constexpr phi::PhiTriConfig kCombLfoPhaseOffsetTriangle = {phi::kPhi175, 0.65f, 0.35f, false};

/// Phi triangle config for filter resonance derived from flavor
constexpr phi::PhiTriConfig kFilterResonanceTriangle = {phi::kPhi175, 0.8f, 0.4f, false};

/// Phi triangle config for filter cutoff base derived from flavor
constexpr phi::PhiTriConfig kFilterCutoffBaseTriangle = {phi::kPhi150, 0.75f, 0.0f, false};

/// Phi triangle config for filter cutoff LFO depth derived from flavor
constexpr phi::PhiTriConfig kFilterCutoffLfoDepthTriangle = {phi::kPhi200, 0.5f, 0.6f, false};

/// Phi triangle config for envelope attack time derived from flavor
constexpr phi::PhiTriConfig kEnvAttackTriangle = {phi::kPhi125, 0.7f, 0.15f, false};

/// Phi triangle config for envelope release/decay time derived from flavor
constexpr phi::PhiTriConfig kEnvReleaseTriangle = {phi::kPhi175, 0.7f, 0.65f, false};

/// Phi triangle bank for filter LFO response strength (derived from flavor)
constexpr std::array<phi::PhiTriConfig, 3> kFilterLfoResponseBank = {{
    {phi::kPhi125, 0.5f, 0.00f, false}, // [0] LP response: φ^1.25
    {phi::kPhi175, 0.5f, 0.40f, false}, // [1] BP response: φ^1.75, offset
    {phi::kPhi225, 0.5f, 0.70f, false}, // [2] HP response: φ^2.25, offset
}};

/// Phi triangle bank for filter LFO phase offset (derived from flavor)
constexpr std::array<phi::PhiTriConfig, 3> kFilterPhaseOffsetBank = {{
    {phi::kPhi100, 0.5f, 0.00f, false}, // [0] LP phase: slower evolution
    {phi::kPhi150, 0.6f, 0.33f, false}, // [1] BP phase: moderate evolution
    {phi::kPhi200, 0.7f, 0.66f, false}, // [2] HP phase: faster evolution
}};

/// Phi triangle bank for LFO wavetable waypoints (derived from mod)
/// 9 bipolar triangles: 5 for phase deltas (0→P1→P2→P3→P4→1), 4 for amplitudes
/// Phase deltas are accumulated and normalized to guarantee monotonic ordering
/// Amplitude triangles output -1 to +1 directly
constexpr std::array<phi::PhiTriConfig, 9> kLfoWaypointBank = {{
    // Phase deltas (bipolar → abs → accumulate → normalize)
    // 5 deltas for 5 segments: 0→P1, P1→P2, P2→P3, P3→P4, P4→1
    {phi::kPhi075, 0.6f, 0.000f, true}, // [0] 0→P1 phase delta
    {phi::kPhi100, 0.5f, 0.111f, true}, // [1] P1→P2 phase delta
    {phi::kPhi125, 0.5f, 0.222f, true}, // [2] P2→P3 phase delta
    {phi::kPhi150, 0.6f, 0.333f, true}, // [3] P3→P4 phase delta
    {phi::kPhi175, 0.5f, 0.444f, true}, // [4] P4→1 phase delta
    // Amplitudes (bipolar -1 to +1) - use lower phi exponents for slower evolution
    {phi::kPhi050, 0.7f, 0.555f, true},  // [5] P1 amplitude
    {phi::kPhi067, 0.6f, 0.666f, true},  // [6] P2 amplitude
    {phi::kPhiN050, 0.6f, 0.777f, true}, // [7] P3 amplitude (negative exp for variety)
    {phi::kPhi025, 0.7f, 0.888f, true},  // [8] P4 amplitude
}};

/// Wavetable phase normalization range (leave room for endpoints at 0 and 1)
constexpr float kWaypointPhaseMin = 0.05f;
constexpr float kWaypointPhaseMax = 0.95f;

// ============================================================================
// Type Definitions
// ============================================================================

/// LFO wavetable waypoints (4 movable points between fixed endpoints at 0,0 and 1,0)
/// Pre-computes slopes for each segment to avoid divisions at runtime
struct LfoWaypointBank {
	float phase[4];     // Phase positions (0-1), should be monotonically increasing
	float amplitude[4]; // Amplitude values (-1 to +1)
	// Pre-computed for fast runtime evaluation:
	// 6 boundaries: 0, P1, P2, P3, P4, 1
	// 5 segments: 0→P1, P1→P2, P2→P3, P3→P4, P4→1
	float segStart[5]; // Start phase of each segment (0, P1, P2, P3, P4)
	float segSlope[5]; // Slope (amplitude change per phase unit) for each segment
	float segAmp[5];   // Start amplitude for each segment (0, A1, A2, A3, A4)

	// === Fast integer evaluation (pre-computed during cache update) ===
	uint32_t phaseU32[4];    // Segment boundaries as uint32 (for fast comparison)
	uint32_t segStartU32[5]; // Segment start phases as uint32
	q31_t segAmpQ[5];        // Segment start amplitudes (unipolar q31: 0 to ONE_Q31)
	q31_t segSlopeQ[5];      // Slopes scaled for: value = amp + multiply_32x32_rshift32(phaseOffset, slope) << 1

	// === IIR-style linear stepping (avoids wavetable eval per buffer) ===
	// invSegWidth: reciprocal of segment width, scaled so that:
	// step = multiply_32x32_rshift32(ampDelta, multiply_32x32_rshift32(phaseInc, invSegWidth) << 1) << 1
	// gives the per-sample step in q31
	uint32_t invSegWidthQ[5]; // 1/segWidth scaled for reciprocal multiply
};

/// State for IIR-style LFO tracking (one per LFO channel)
/// Uses exponential chase toward segment targets - organic curves without wavetable eval
struct LfoIirState {
	q31_t value{0};        // Current LFO value (bipolar q31)
	q31_t intermediate{0}; // Per-sample step (delta)
	q31_t target{0};       // Target amplitude (segment endpoint)
	int8_t segment{0};     // Current segment index (0-4)
};

/// Result of incremental LFO evaluation - start value and per-sample delta
struct LfoIncremental {
	q31_t value; // Current LFO value (bipolar q31: -ONE_Q31 to ONE_Q31)
	q31_t delta; // Per-sample delta (signed q31, add each sample)
};

/// Result of LFO rate calculation - either free Hz or synced subdivision
struct LfoRateResult {
	float value;       // Hz if free, ignored if synced
	int32_t syncLevel; // 0 = free, 1-9 = SYNC_LEVEL_WHOLE through SYNC_LEVEL_256TH
	bool triplet;      // true = triplet timing (3/2 multiplier)
};

/// Get stage enable mix from type value
struct TypeMix {
	float filter;
	float comb;
	float tremolo;
};

/// Filter output mix weights (normalized to sum to 1.0)
struct FilterMix {
	float low;  // Lowpass weight
	float band; // Bandpass weight
	float high; // Highpass weight
};

/// Filter LFO modulation parameters for each band
struct FilterLfoParams {
	float lpResponse;    // LP LFO response strength (0-1)
	float bpResponse;    // BP LFO response strength (0-1)
	float hpResponse;    // HP LFO response strength (0-1)
	float lpPhaseOffset; // LP LFO phase offset (0-1 = 0-360°)
	float bpPhaseOffset; // BP LFO phase offset
	float hpPhaseOffset; // HP LFO phase offset
};

/// Cached phi triangle results (recomputed only when zone params change)
struct AutomodPhiCache {
	// From type
	q31_t combFeedback{0};
	q31_t combMixQ{0};
	uint32_t tremPhaseOffset{0};
	q31_t tremoloDepthQ{0};
	q31_t tremoloBaseLevel{0};
	q31_t filterMixLowQ{0};
	q31_t filterMixBandQ{0};
	q31_t filterMixHighQ{0};

	// From flavor
	q31_t filterCutoffBase{0};
	q31_t filterResonance{0};
	q31_t filterModDepth{0};
	float combStaticOffset{0};
	float combLfoDepth{0};
	uint32_t combPhaseOffsetU32{0};
	// Pre-computed comb delay constants (pure 32-bit math in loop)
	int32_t combBaseDelay16{4 << 16};
	int32_t combModRangeSamples{0};
	int32_t combMinDelay16{2 << 16};
	int32_t combMaxDelay16{1534 << 16};
	q31_t envAttack{0};
	q31_t envRelease{0};
	// FilterLfoParams (inline to avoid forward decl issues)
	float lpResponse{0};
	float bpResponse{0};
	float hpResponse{0};
	float lpPhaseOffset{0};
	float bpPhaseOffset{0};
	float hpPhaseOffset{0};
	uint32_t lpPhaseOffsetU32{0};
	uint32_t bpPhaseOffsetU32{0};
	uint32_t hpPhaseOffsetU32{0};
	q31_t lpResponseQ{0};
	q31_t bpResponseQ{0};
	q31_t hpResponseQ{0};
	bool useStaticFilterMix{true};

	// From mod
	float rateValue{1.0f};
	int32_t rateSyncLevel{0};
	bool rateTriplet{false};
	uint32_t stereoPhaseOffsetRaw{0};
	float envDepthInfluence{0};
	float envPhaseInfluence{0};
	float envDerivDepthInfluence{0};
	float envDerivPhaseInfluence{0};

	// LFO wavetable waypoints (sorted by phase)
	LfoWaypointBank wavetable{};

	// Pre-computed LFO phase increment
	uint32_t lfoInc{0};

	// IIR chase coefficient - scales with LFO rate for proper tracking
	// Used as: value += multiply_32x32_rshift32(target - value, iirCoeff) << 1
	q31_t iirCoeff{0};

	// P4 phase as uint32 for fast last-segment detection (do full eval in last segment for clean wrap)
	uint32_t lastSegmentPhaseU32{0xE6666666}; // Default ~0.9, updated from wavetable
};

/// Automodulator parameters and DSP state (stored per-Sound)
struct AutomodulatorParams {
	// === Rule of 5: Proper resource management for dynamically allocated comb buffers ===

	AutomodulatorParams() = default;

	~AutomodulatorParams() { deallocateCombBuffers(); }

	// Delete copy operations - use cloneFrom() for explicit copying
	AutomodulatorParams(const AutomodulatorParams&) = delete;
	AutomodulatorParams& operator=(const AutomodulatorParams&) = delete;

	// Move operations transfer buffer ownership
	AutomodulatorParams(AutomodulatorParams&& other) noexcept { moveFrom(other); }
	AutomodulatorParams& operator=(AutomodulatorParams&& other) noexcept {
		if (this != &other) {
			deallocateCombBuffers();
			moveFrom(other);
		}
		return *this;
	}

	/// Clone params from another instance (for preset switching, etc.)
	/// Copies all params but does NOT copy comb buffers - they'll be lazy-allocated if needed
	void cloneFrom(const AutomodulatorParams& other) {
		// Copy menu params
		type = other.type;
		flavor = other.flavor;
		mod = other.mod;
		typePhaseOffset = other.typePhaseOffset;
		flavorPhaseOffset = other.flavorPhaseOffset;
		modPhaseOffset = other.modPhaseOffset;
		gammaPhase = other.gammaPhase;
		// Invalidate cache to force recomputation
		invalidateCache();
		// Reset DSP state (don't copy - let it reinitialize fresh)
		resetState();
	}

	// Menu params (stored/loaded)
	uint16_t type{0};   // 0=OFF, 1-1023 = DSP topology blend (8 zones)
	uint16_t flavor{0}; // 0-1023, routing zones (8 zones)
	uint16_t mod{0};    // 0-1023, rate/phase zones (8 zones)

	// Phase offsets (secret knob on each zone) for phi triangle evaluation
	float typePhaseOffset{0};
	float flavorPhaseOffset{0};
	float modPhaseOffset{0};

	// Global gamma (secret knob on macro) - multiplier for all phase offsets
	float gammaPhase{0};

	// Cache invalidation: previous values (initialized to invalid to force first compute)
	uint16_t prevType{0xFFFF};
	uint16_t prevFlavor{0xFFFF};
	uint16_t prevMod{0xFFFF};
	float prevGammaPhase{-999.0f};
	float prevTypePhaseOffset{-999.0f};
	float prevFlavorPhaseOffset{-999.0f};
	float prevModPhaseOffset{-999.0f};
	uint32_t prevTimePerTickInverse{0xFFFFFFFF};

	// Cached phi triangle results
	AutomodPhiCache cache;

	// DSP state (runtime, not saved)
	uint8_t lastVoiceCount{0};
	q31_t svfLowL{0};
	q31_t svfBandL{0};
	q31_t svfLowR{0};
	q31_t svfBandR{0};
	q31_t envStateL{0};
	q31_t envStateR{0};
	q31_t envDerivStateL{0};
	q31_t envDerivStateR{0};
	uint32_t lfoPhase{0};

	// Smoothed modulation state
	q31_t smoothedScaleL{0};
	q31_t smoothedScaleR{0};
	uint32_t smoothedPhasePushL{0};
	uint32_t smoothedPhasePushR{0};
	uint32_t smoothedStereoOffset{0};
	q31_t smoothedLowMixQ{0};
	q31_t smoothedBandMixQ{0};
	q31_t smoothedHighMixQ{0};

	// IIR-smoothed LFO values
	q31_t smoothedLfoL{0};
	q31_t smoothedLfoR{0};
	q31_t smoothedCombLfoL{0};
	q31_t smoothedCombLfoR{0};
	q31_t smoothedTremLfoL{0};
	q31_t smoothedTremLfoR{0};

	// Pitch tracking cache (avoid recomputing fastPow2 every buffer)
	int32_t prevNoteCode{-2};         // -2 = never computed (different from -1 = no note)
	q31_t cachedPitchCutoffOffset{0}; // Filter cutoff pitch adjustment
	float cachedPitchRatio{1.0f};     // Comb delay multiplier (1.0 = no change)

	// LFO IIR state (linear stepping through segments - avoids wavetable eval per buffer)
	LfoIirState lfoIirL{};
	LfoIirState lfoIirR{};
	LfoIirState combLfoIirL{};
	LfoIirState combLfoIirR{};
	LfoIirState tremLfoIirL{};
	LfoIirState tremLfoIirR{};

	// Comb filter state - LAZILY ALLOCATED
	static constexpr size_t kCombBufferSize = 1536;
	q31_t* combBufferL{nullptr};
	q31_t* combBufferR{nullptr};
	uint16_t combIdx{0};

	/// Check if automodulator is enabled
	[[nodiscard]] bool isEnabled() const { return type > 0; }

	/// Check if comb buffers are allocated
	[[nodiscard]] bool hasCombBuffers() const { return combBufferL != nullptr; }

	/// Allocate comb buffers (called when comb effect is first used)
	bool allocateCombBuffers() {
		if (combBufferL != nullptr) {
			return true;
		}
		combBufferL = static_cast<q31_t*>(delugeAlloc(kCombBufferSize * sizeof(q31_t), true));
		if (combBufferL == nullptr) {
			return false;
		}
		combBufferR = static_cast<q31_t*>(delugeAlloc(kCombBufferSize * sizeof(q31_t), true));
		if (combBufferR == nullptr) {
			delugeDealloc(combBufferL);
			combBufferL = nullptr;
			return false;
		}
		std::memset(combBufferL, 0, kCombBufferSize * sizeof(q31_t));
		std::memset(combBufferR, 0, kCombBufferSize * sizeof(q31_t));
		combIdx = 0;
		return true;
	}

	/// Deallocate comb buffers
	void deallocateCombBuffers() {
		if (combBufferL != nullptr) {
			delugeDealloc(combBufferL);
			combBufferL = nullptr;
		}
		if (combBufferR != nullptr) {
			delugeDealloc(combBufferR);
			combBufferR = nullptr;
		}
		combIdx = 0;
	}

private:
	/// Move all data from another instance
	void moveFrom(AutomodulatorParams& other) noexcept {
		type = other.type;
		flavor = other.flavor;
		mod = other.mod;
		typePhaseOffset = other.typePhaseOffset;
		flavorPhaseOffset = other.flavorPhaseOffset;
		modPhaseOffset = other.modPhaseOffset;
		gammaPhase = other.gammaPhase;
		prevType = other.prevType;
		prevFlavor = other.prevFlavor;
		prevMod = other.prevMod;
		prevGammaPhase = other.prevGammaPhase;
		prevTypePhaseOffset = other.prevTypePhaseOffset;
		prevFlavorPhaseOffset = other.prevFlavorPhaseOffset;
		prevModPhaseOffset = other.prevModPhaseOffset;
		prevTimePerTickInverse = other.prevTimePerTickInverse;
		cache = other.cache;
		lastVoiceCount = other.lastVoiceCount;
		svfLowL = other.svfLowL;
		svfBandL = other.svfBandL;
		svfLowR = other.svfLowR;
		svfBandR = other.svfBandR;
		envStateL = other.envStateL;
		envStateR = other.envStateR;
		envDerivStateL = other.envDerivStateL;
		envDerivStateR = other.envDerivStateR;
		lfoPhase = other.lfoPhase;
		smoothedScaleL = other.smoothedScaleL;
		smoothedScaleR = other.smoothedScaleR;
		smoothedPhasePushL = other.smoothedPhasePushL;
		smoothedPhasePushR = other.smoothedPhasePushR;
		smoothedStereoOffset = other.smoothedStereoOffset;
		smoothedLowMixQ = other.smoothedLowMixQ;
		smoothedBandMixQ = other.smoothedBandMixQ;
		smoothedHighMixQ = other.smoothedHighMixQ;
		smoothedLfoL = other.smoothedLfoL;
		smoothedLfoR = other.smoothedLfoR;
		smoothedCombLfoL = other.smoothedCombLfoL;
		smoothedCombLfoR = other.smoothedCombLfoR;
		smoothedTremLfoL = other.smoothedTremLfoL;
		smoothedTremLfoR = other.smoothedTremLfoR;
		prevNoteCode = other.prevNoteCode;
		cachedPitchCutoffOffset = other.cachedPitchCutoffOffset;
		cachedPitchRatio = other.cachedPitchRatio;
		lfoIirL = other.lfoIirL;
		lfoIirR = other.lfoIirR;
		combLfoIirL = other.combLfoIirL;
		combLfoIirR = other.combLfoIirR;
		tremLfoIirL = other.tremLfoIirL;
		tremLfoIirR = other.tremLfoIirR;
		combIdx = other.combIdx;

		// Transfer buffer ownership
		combBufferL = other.combBufferL;
		combBufferR = other.combBufferR;
		other.combBufferL = nullptr;
		other.combBufferR = nullptr;
		other.combIdx = 0;
	}

public:
	/// Check if any cached values need recomputation
	[[nodiscard]] bool needsCacheUpdate(uint32_t timePerTickInverse) const {
		return (type != prevType || flavor != prevFlavor || mod != prevMod || gammaPhase != prevGammaPhase
		        || typePhaseOffset != prevTypePhaseOffset || flavorPhaseOffset != prevFlavorPhaseOffset
		        || modPhaseOffset != prevModPhaseOffset || timePerTickInverse != prevTimePerTickInverse);
	}

	/// Reset DSP state
	void resetState() {
		svfLowL = svfBandL = svfLowR = svfBandR = 0;
		envStateL = envStateR = 0;
		envDerivStateL = envDerivStateR = 0;
		lfoPhase = 0;
		smoothedScaleL = smoothedScaleR = 0;
		smoothedPhasePushL = smoothedPhasePushR = 0;
		smoothedStereoOffset = 0;
		smoothedLowMixQ = smoothedBandMixQ = smoothedHighMixQ = 0;
		smoothedLfoL = smoothedLfoR = 0;
		smoothedCombLfoL = smoothedCombLfoR = 0;
		smoothedTremLfoL = smoothedTremLfoR = 0;
		prevNoteCode = -2; // Force recomputation
		cachedPitchCutoffOffset = 0;
		cachedPitchRatio = 1.0f;
		// Reset LFO IIR states
		lfoIirL = lfoIirR = {};
		combLfoIirL = combLfoIirR = {};
		tremLfoIirL = tremLfoIirR = {};
		combIdx = 0;
		if (combBufferL != nullptr && combBufferR != nullptr) {
			for (size_t i = 0; i < kCombBufferSize; ++i) {
				combBufferL[i] = combBufferR[i] = 0;
			}
		}
	}

	/// Invalidate cache
	void invalidateCache() {
		prevType = 0xFFFF;
		prevFlavor = 0xFFFF;
		prevMod = 0xFFFF;
		prevGammaPhase = -999.0f;
	}
};

// ============================================================================
// Small inline helpers (kept in header for performance)
// ============================================================================

/// Convert MIDI note code to Hz (A4=440Hz standard)
[[gnu::always_inline]] inline float noteCodeToHz(int32_t noteCode) {
	return kA4Hz * exp2f((static_cast<float>(noteCode) - 69.0f) / 12.0f);
}

/// Get stage enable mix from type value
[[gnu::always_inline]] inline TypeMix getTypeMix(uint16_t type) {
	if (type == 0) {
		return {0.0f, 0.0f, 0.0f};
	}
	return {1.0f, 1.0f, 1.0f};
}

// ============================================================================
// Function declarations (implementations in automodulator.cpp)
// ============================================================================

/// Get LFO rate from mod zone position
LfoRateResult getLfoRateFromMod(uint16_t mod, float phaseOffset);

/// Evaluate filter mix from type position using phi triangles
FilterMix getFilterMixFromType(uint16_t type, float phaseOffset);

/// Get filter LFO parameters from flavor position
FilterLfoParams getFilterLfoParamsFromFlavor(uint16_t flavor, float phaseOffset);

/// Get comb LFO modulation depth from flavor position
float getCombLfoDepthFromFlavor(uint16_t flavor, float phaseOffset);

/// Get comb static offset from flavor position
float getCombStaticOffsetFromFlavor(uint16_t flavor, float phaseOffset);

/// Get comb LFO phase offset from flavor position
uint32_t getCombLfoPhaseOffsetFromFlavor(uint16_t flavor, float phaseOffset);

/// Get filter resonance from flavor position
q31_t getFilterResonanceFromFlavor(uint16_t flavor, float phaseOffset);

/// Get filter cutoff base from flavor position
q31_t getFilterCutoffBaseFromFlavor(uint16_t flavor, float phaseOffset);

/// Get filter cutoff LFO depth from flavor position
q31_t getFilterCutoffLfoDepthFromFlavor(uint16_t flavor, float phaseOffset);

/// Get envelope attack coefficient from flavor position
q31_t getEnvAttackFromFlavor(uint16_t flavor, float phaseOffset);

/// Get envelope release coefficient from flavor position
q31_t getEnvReleaseFromFlavor(uint16_t flavor, float phaseOffset);

/// Get comb feedback amount from type position
q31_t getCombFeedbackFromType(uint16_t type, float phaseOffset);

/// Get comb wet/dry mix from type position
float getCombMixFromType(uint16_t type, float phaseOffset);

/// Get tremolo depth from type position
float getTremoloDepthFromType(uint16_t type, float phaseOffset);

/// Get tremolo LFO phase offset from type position
uint32_t getTremoloPhaseOffsetFromType(uint16_t type, float phaseOffset);

/// Get stereo phase offset from mod position
uint32_t getStereoOffsetFromMod(uint16_t mod, float phaseOffset);

/// Get LFO initial phase from mod position
uint32_t getLfoInitialPhaseFromMod(uint16_t mod, float phaseOffset);

/// Get envelope→depth influence from mod position
float getEnvDepthInfluenceFromMod(uint16_t mod, float phaseOffset);

/// Get envelope→phase influence from mod position
float getEnvPhaseInfluenceFromMod(uint16_t mod, float phaseOffset);

/// Get derivative envelope→depth influence from mod position
float getEnvDerivDepthInfluenceFromMod(uint16_t mod, float phaseOffset);

/// Get derivative envelope→phase influence from mod position
float getEnvDerivPhaseInfluenceFromMod(uint16_t mod, float phaseOffset);

/// Compute LFO wavetable waypoints from mod position
/// Phase deltas are accumulated and normalized to guarantee monotonic ordering
LfoWaypointBank getLfoWaypointBank(uint16_t mod, float phaseOffset);

/// Evaluate the LFO wavetable at a given phase (0-1)
/// Returns interpolated amplitude between waypoints
/// Uses ramp (0→1) as input, not triangle
float evalLfoWavetable(float t, const LfoWaypointBank& bank);

/// Evaluate LFO wavetable returning q31 (for DSP loop)
q31_t evalLfoWavetableQ31(uint32_t phaseU32, const LfoWaypointBank& bank);

/// Update the phi triangle cache (called when zone params change)
void updateAutomodPhiCache(AutomodulatorParams& params, uint32_t timePerTickInverse);

/// Process automodulator effect on a stereo buffer
/// @param buffer Audio buffer to process in-place
/// @param params Automodulator params and state
/// @param macro Macro modulation depth from param system (q31)
/// @param useInternalOsc True to use internal LFO, false for envelope follower
/// @param voiceCount Current number of active voices (for note retrigger)
/// @param timePerTickInverse For tempo sync (from playbackHandler), 0 if clock not active
/// @param noteCode Last played MIDI note for pitch tracking (-1 if none)
void processAutomodulator(std::span<StereoSample> buffer, AutomodulatorParams& params, q31_t macro, bool useInternalOsc,
                          uint8_t voiceCount = 0, uint32_t timePerTickInverse = 0, int32_t noteCode = kNoteCodeInvalid);

} // namespace deluge::dsp
