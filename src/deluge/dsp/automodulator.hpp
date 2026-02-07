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
#include "dsp/util.hpp"
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

/// LFO run mode (controlled via push toggle on depth knob)
enum class AutomodLfoMode : uint8_t {
	STOP = 0,   // LFO frozen, Manual knob used directly
	ONCE = 1,   // Play one LFO cycle then freeze (retriggered on note)
	RETRIG = 2, // Running LFO, resets phase on note trigger
	FREE = 3,   // Free-running LFO, ignores note triggers
};

// ============================================================================
// Phi Triangle Configuration Constants
// ============================================================================

/// Phi triangle config for LFO rate (used when phaseOffset > 0)
/// BIPOLAR: positive = free-running Hz, negative = tempo-synced subdivision
constexpr phi::PhiTriConfig kLfoRateTriangle = {phi::kPhi075, 0.7f, 0.0f, true};

/// Phi triangle bank for filter output mixing (derived from type)
/// Slow evolution (~5x slower than other params) for gradual timbral shifts
constexpr std::array<phi::PhiTriConfig, 3> kFilterMixBank = {{
    {phi::kPhi050, 0.6f, 0.50f, false}, // [0] Lowpass: HIGH at zone 0 for classic LP
    {phi::kPhi067, 0.5f, 0.00f, false}, // [1] Bandpass: LOW at zone 0
    {phi::kPhi100, 0.4f, 0.00f, false}, // [2] Highpass: LOW at zone 0
}};

/// Phi triangle config for stereo phase offset derived from mod
constexpr phi::PhiTriConfig kStereoOffsetTriangle = {phi::kPhi150, 0.7f, 0.0f, false};

/// Phi triangle config for LFO initial phase derived from mod
constexpr phi::PhiTriConfig kLfoInitialPhaseTriangle = {phi::kPhi250, 1.0f, 0.0f, false};

/// Phi triangle config for envelope→depth influence (BIPOLAR)
/// Phase offset 0.121 = 0.25/kPhi150 produces zero at mod=0 (neutral)
constexpr phi::PhiTriConfig kEnvDepthInfluenceTriangle = {phi::kPhi150, 0.5f, 0.121f, true};

/// Phi triangle config for envelope→phase influence (BIPOLAR)
/// Phase offset 0.109 = 0.25/kPhi175 produces zero at mod=0 (neutral)
constexpr phi::PhiTriConfig kEnvPhaseInfluenceTriangle = {phi::kPhi175, 0.5f, 0.109f, true};

/// Phi triangle config for derivative envelope→depth influence (BIPOLAR)
/// Phase offset 0.135 = 0.25/kPhi125 produces zero at mod=0 (neutral)
constexpr phi::PhiTriConfig kEnvDerivDepthInfluenceTriangle = {phi::kPhi125, 0.5f, 0.135f, true};

/// Phi triangle config for derivative envelope→phase influence (BIPOLAR)
/// Phase offset 0.121 = 0.25/kPhi150 produces zero at mod=0 (neutral)
constexpr phi::PhiTriConfig kEnvDerivPhaseInfluenceTriangle = {phi::kPhi150, 0.5f, 0.121f, true};

/// Phi triangle config for envelope→LFO value contribution (BIPOLAR)
/// Phase offset 0.135 = 0.25/kPhi125 produces zero at mod=0 (neutral)
/// Positive = envelope adds to LFO, negative = envelope subtracts from LFO
constexpr phi::PhiTriConfig kEnvValueInfluenceTriangle = {phi::kPhi125, 0.5f, 0.135f, true};

/// Phi triangle config for spring filter natural frequency (ω₀)
/// Maps 0→1 to 2Hz→15Hz (buffer-rate spring on modulation signal)
/// Base 2Hz ensures smooth tracking at mod=0, higher values add character
constexpr phi::PhiTriConfig kSpringFreqTriangle = {phi::kPhi175, 0.5f, 0.25f, false};

/// Phi triangle config for spring filter damping ratio (ζ) - INVERTED
/// mod=0 → critically damped (smooth tracking), mod=1 → underdamped (bouncy)
/// Frequency-dependent boost keeps high-freq springs damped to prevent aliasing
constexpr phi::PhiTriConfig kSpringDampingTriangle = {phi::kPhi150, 0.4f, 0.4f, false};

/// Phi triangle config for comb feedback derived from type
constexpr phi::PhiTriConfig kCombFeedbackTriangle = {phi::kPhi250, 0.8f, 0.0f, false};

/// Phi triangle config for comb wet/dry mix derived from type
constexpr phi::PhiTriConfig kCombMixTriangle = {phi::kPhi350, 0.85f, 0.00f, false}; // No comb at zone 0

/// Phi triangle config for SVF feedback (LP output → cutoff/phase) derived from type (BIPOLAR)
/// 25% duty: positive = cutoff feedback (screaming filter), negative = phase push (chaotic)
/// Phase offset 0.1078 = 0.25/kPhi175 puts deadzone start at type=0 (no feedback until type increases)
constexpr phi::PhiTriConfig kSvfFeedbackTriangle = {phi::kPhi175, 0.25f, 0.1078f, true};

/// Phi triangle config for tremolo depth derived from flavor
constexpr phi::PhiTriConfig kTremoloDepthTriangle = {phi::kPhi275, 0.0f, 0.00f,
                                                     false}; // Disabled: band mixing provides AM

/// Phi triangle config for tremolo phase offset derived from flavor
constexpr phi::PhiTriConfig kTremoloPhaseOffsetTriangle = {phi::kPhi225, 0.6f, 0.75f, false};

/// Phi triangle config for comb LFO depth derived from flavor
constexpr phi::PhiTriConfig kCombLfoDepthTriangle = {phi::kPhi175, 0.75f, 0.00f, false}; // No comb mod at zone 0

/// Phi triangle config for comb static offset derived from flavor
constexpr phi::PhiTriConfig kCombStaticOffsetTriangle = {phi::kPhi125, 0.85f, 0.25f, false};

/// Phi triangle config for comb LFO phase offset derived from flavor
constexpr phi::PhiTriConfig kCombLfoPhaseOffsetTriangle = {phi::kPhi175, 0.65f, 0.35f, false};

/// Phi triangle for comb mono collapse (stereo width control)
/// 50% duty = 50% deadzone at 0 (full stereo), ramps to 1 (mono) for variety
constexpr phi::PhiTriConfig kCombMonoCollapseTriangle = {phi::kPhi225, 0.5f, 0.15f, false};

/// Phi triangle config for filter resonance derived from flavor
constexpr phi::PhiTriConfig kFilterResonanceTriangle = {phi::kPhi175, 0.8f, 0.4f, false};

/// Phi triangle config for filter cutoff base derived from flavor
constexpr phi::PhiTriConfig kFilterCutoffBaseTriangle = {phi::kPhi150, 0.75f, 0.0f, false};

/// Phi triangle config for filter cutoff LFO depth derived from flavor
constexpr phi::PhiTriConfig kFilterCutoffLfoDepthTriangle = {phi::kPhi200, 0.5f, 0.50f,
                                                             false}; // HIGH LFO depth at zone 0

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

// ============================================================================
// Consolidated banks for batch evaluation (performance optimization)
// ============================================================================

/// Consolidated flavor bank - all scalar flavor-derived params in one batch
/// Indices: [0]=cutoffBase, [1]=resonance, [2]=filterModDepth, [3]=attack, [4]=release,
///          [5]=combStaticOffset, [6]=combLfoDepth, [7]=combPhaseOffset, [8]=combMonoCollapse,
///          [9]=tremoloDepth, [10]=tremoloPhaseOffset
constexpr std::array<phi::PhiTriConfig, 11> kFlavorScalarBank = {{
    kFilterCutoffBaseTriangle,     // [0]
    kFilterResonanceTriangle,      // [1]
    kFilterCutoffLfoDepthTriangle, // [2]
    kEnvAttackTriangle,            // [3]
    kEnvReleaseTriangle,           // [4]
    kCombStaticOffsetTriangle,     // [5]
    kCombLfoDepthTriangle,         // [6]
    kCombLfoPhaseOffsetTriangle,   // [7]
    kCombMonoCollapseTriangle,     // [8]
    kTremoloDepthTriangle,         // [9]
    kTremoloPhaseOffsetTriangle,   // [10]
}};

/// Consolidated type bank - all scalar type-derived params in one batch
/// Indices: [0]=combFeedback, [1]=combMix, [2]=svfFeedback
constexpr std::array<phi::PhiTriConfig, 3> kTypeScalarBank = {{
    kCombFeedbackTriangle, // [0]
    kCombMixTriangle,      // [1]
    kSvfFeedbackTriangle,  // [2] BIPOLAR: positive=cutoff feedback, negative=phase push
}};

/// Consolidated mod bank - all scalar mod-derived params in one batch
/// Indices: [0]=stereoOffset, [1]=envDepth, [2]=envPhase, [3]=envDerivDepth, [4]=envDerivPhase,
///          [5]=envValue, [6]=springFreq, [7]=springDamping
constexpr std::array<phi::PhiTriConfig, 8> kModScalarBank = {{
    kStereoOffsetTriangle,           // [0]
    kEnvDepthInfluenceTriangle,      // [1]
    kEnvPhaseInfluenceTriangle,      // [2]
    kEnvDerivDepthInfluenceTriangle, // [3]
    kEnvDerivPhaseInfluenceTriangle, // [4]
    kEnvValueInfluenceTriangle,      // [5]
    kSpringFreqTriangle,             // [6]
    kSpringDampingTriangle,          // [7]
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
// Future Feature Ideas (documented for later implementation)
// ============================================================================
//
// 1. AUDIO-RATE ENVELOPE FOLLOWING
//    - Per-sample envelope tracking instead of buffer-rate
//    - Modulates filter cutoff and/or LFO phase at audio rate
//    - Creates aggressive, hyper-responsive auto-wah
//    - Amount derived from Mod zone (0 at zone 0, increases with Mod)
//    - Needs fast attack coefficient for audio-rate response
//
// 2. FEEDBACK PATH (filter output → cutoff)
//    - Route filter output back to modulate its own cutoff
//    - At high resonance + feedback: self-oscillating filter screams
//    - Amount derived from Type (0 at zone 0, increases with Type)
//    - Store previous output sample, scale by feedback amount
//    - Add to cutoff calculation: cutoff += prevOutput * feedbackQ
//
// 3. CROSS-CHANNEL LFO MULTIPLY (L×R ring mod)
//    - Multiply L and R LFO values together
//    - Creates sum/difference frequencies (ring mod on the LFO itself)
//    - When stereo offset = 0°: squares the LFO (octave up effect)
//    - When stereo offset = 90°: interesting interference patterns
//    - Amount derived from Mod (pairs naturally with stereo offset control)
//    - crossMod = multiply_32x32_rshift32(lfoL, lfoR) << 1
//    - Blend back into both channels for metallic, inharmonic textures
//

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
	q31_t value{0};              // Current LFO value (bipolar q31)
	q31_t intermediate{0};       // Per-sample step (delta)
	q31_t target{0};             // Target amplitude (segment endpoint)
	int8_t segment{0};           // Current segment index (0-4)
	uint32_t samplesRemaining{}; // Samples until segment boundary (decremented each buffer)
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
	uint8_t slowShift; // Additional right-shift for ultra-slow rates (8/1, 4/1)
	bool triplet;      // true = triplet timing (3/2 multiplier)
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
	q31_t filterMixLowQ{0};
	q31_t filterMixBandQ{0};
	q31_t filterMixHighQ{0};
	q31_t svfFeedbackQ{0}; // BIPOLAR: positive = cutoff feedback, negative = phase push

	// From flavor
	q31_t filterCutoffBase{0};
	q31_t filterResonance{0};
	q31_t filterModDepth{0};
	float combStaticOffset{0};
	float combLfoDepth{0};
	uint32_t combPhaseOffsetU32{0};
	q31_t combMonoCollapseQ{0}; // 0 = full stereo, ONE_Q31 = mono (crossfeed comb output only)
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
	bool rateStopped{false}; // True when rate=0 (Stop mode), LFO frozen, Manual used directly
	bool rateOnce{false};    // True when rate=1 (Once mode), plays one LFO cycle then freezes
	uint32_t stereoPhaseOffsetRaw{0};
	// Env influences in q31 for integer-only per-buffer math
	q31_t envDepthInfluenceQ{0};
	q31_t envPhaseInfluenceQ{0};
	q31_t envDerivDepthInfluenceQ{0};
	q31_t envDerivPhaseInfluenceQ{0};
	q31_t envValueInfluenceQ{0}; // Envelope→LFO value contribution (bipolar)

	// Spring filter coefficients (buffer-rate 2nd-order LPF on modulation signal)
	// Computed from springFreq and springDamping phi triangles
	q31_t springOmega2Q{0};       // ω₀² coefficient for spring force
	q31_t springDampingCoeffQ{0}; // 2*ζ*ω₀ coefficient for velocity damping

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

/// DSP runtime state for automodulator - lazily allocated when effect becomes active
/// This keeps the per-Sound footprint minimal when automod is disabled
struct AutomodDspState {
	// SVF filter state
	q31_t svfLowL{0};
	q31_t svfBandL{0};
	q31_t svfLowR{0};
	q31_t svfBandR{0};

	// Envelope follower state
	q31_t envStateL{0};
	q31_t envStateR{0};
	q31_t envDerivStateL{0};
	q31_t envDerivStateR{0};

	// Spring filter state (buffer-rate 2nd-order LPF on modulation signal)
	// Position = current smoothed modulation value, Velocity = rate of change (momentum)
	q31_t springPosL{0};
	q31_t springPosR{0};
	q31_t springVelL{0};
	q31_t springVelR{0};

	// LFO phase accumulator
	uint32_t lfoPhase{0};
	uint32_t onceStartPhase{0};   // Phase where Once mode started (for cycle detection)
	bool oneCycleComplete{false}; // True when Once mode has completed its cycle

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

	// Pitch tracking cache
	int32_t prevNoteCode{-2};                   // -2 = never computed
	int32_t cachedFilterPitchRatioQ16{1 << 16}; // Filter cutoff multiplier in 16.16 (higher note = higher cutoff)
	int32_t cachedCombPitchRatioQ16{1 << 16};   // Comb delay multiplier in 16.16 (higher note = shorter delay)

	// LFO IIR state (6 channels)
	LfoIirState lfoIirL{};
	LfoIirState lfoIirR{};
	LfoIirState combLfoIirL{};
	LfoIirState combLfoIirR{};
	LfoIirState tremLfoIirL{};
	LfoIirState tremLfoIirR{};

	// Precomputed LFO stepping (recomputed when rate or wavetable changes)
	uint32_t cachedPhaseInc{0};      // Cached rate for dirty detection
	q31_t stepPerSegment[5]{};       // Per-sample amplitude step for each segment
	uint32_t samplesPerSegment[5]{}; // Number of samples to traverse each segment

	// Comb filter write index
	uint16_t combIdx{0};

	// Allpass interpolator state for comb delay line (reduces aliasing on fast modulation)
	q31_t allpassStateL{0};
	q31_t allpassStateR{0};

	// Previous buffer's filter base cutoff (for per-sample interpolation to avoid clicks)
	q31_t prevFilterBase{0};
};

/// Automodulator parameters and DSP state (stored per-Sound)
/// Uses lazy allocation: cache and dspState are only allocated when effect is active
/// When disabled (type=0), footprint is ~50 bytes instead of ~700 bytes
struct AutomodulatorParams {
	// === Rule of 5: Proper resource management for dynamically allocated state ===

	/// Default constructor - all pointers explicitly nullptr
	AutomodulatorParams() : cache(nullptr), dspState(nullptr), combBufferL(nullptr), combBufferR(nullptr) {}

	~AutomodulatorParams() { deallocateAll(); }

	// Delete copy operations - use cloneFrom() for explicit copying
	AutomodulatorParams(const AutomodulatorParams&) = delete;
	AutomodulatorParams& operator=(const AutomodulatorParams&) = delete;

	// Move operations transfer ownership
	AutomodulatorParams(AutomodulatorParams&& other) noexcept { moveFrom(other); }
	AutomodulatorParams& operator=(AutomodulatorParams&& other) noexcept {
		if (this != &other) {
			deallocateAll();
			moveFrom(other);
		}
		return *this;
	}

	/// Clone params from another instance (for preset switching, etc.)
	/// Copies menu params only - state is deallocated and will be re-allocated on use
	/// Note: freqOffset not copied here - it's in param system (GLOBAL_AUTOMOD_FREQ)
	void cloneFrom(const AutomodulatorParams& other) {
		// Copy page 1 params (freqOffset handled by param system clone)
		rate = other.rate;
		rateSynced = other.rateSynced;
		lfoMode = other.lfoMode;
		mix = other.mix;
		// Copy page 2 params
		type = other.type;
		flavor = other.flavor;
		mod = other.mod;
		typePhaseOffset = other.typePhaseOffset;
		flavorPhaseOffset = other.flavorPhaseOffset;
		modPhaseOffset = other.modPhaseOffset;
		gammaPhase = other.gammaPhase;
		// Deallocate existing state - will be re-allocated on first use
		deallocateAll();
		// Invalidate cache tracking
		invalidateCacheTracking();
	}

	// Menu params (stored/loaded) - always present
	// Page 1: Type, Flavor, Mod, Mix (zone controls)
	// Page 2: Freq (via patched param), Rate, Manual, Depth (depth has push toggle for mode)
	// Note: freqOffset is stored in param system (GLOBAL_AUTOMOD_FREQ) for mod matrix support
	uint16_t rate{6};                             // LFO rate (0=Free, 1+=sync rates; default 6 = 1/8 note)
	bool rateSynced{true};                        // true=synced subdivisions, false=ms
	AutomodLfoMode lfoMode{AutomodLfoMode::FREE}; // LFO mode: stop/once/retrig/free
	uint8_t mix{0};                               // Wet/dry (0=OFF/bypass, 1-127=active)

	// Zone params (Page 1)
	uint16_t type{0};   // 0-1023 = DSP topology blend (8 zones)
	uint16_t flavor{0}; // 0-1023, routing zones (8 zones)
	uint16_t mod{0};    // 0-1023, rate/phase zones (8 zones) - controls LFO rate when rate=0

	// Phase offsets (secret knob on each zone) for phi triangle evaluation
	float typePhaseOffset{0};
	float flavorPhaseOffset{0};
	float modPhaseOffset{0};

	// Global gamma (secret knob on mix) - multiplier for all phase offsets
	float gammaPhase{0};

	// Cache invalidation tracking (to detect when cache needs recomputation)
	// Note: freqOffset is NOT tracked here - it's applied dynamically for mod matrix support
	uint16_t prevRate{0xFFFF};
	bool prevRateSynced{false};
	AutomodLfoMode prevLfoMode{AutomodLfoMode::FREE};
	uint16_t prevType{0xFFFF};
	uint16_t prevFlavor{0xFFFF};
	uint16_t prevMod{0xFFFF};
	float prevGammaPhase{-999.0f};
	float prevTypePhaseOffset{-999.0f};
	float prevFlavorPhaseOffset{-999.0f};
	float prevModPhaseOffset{-999.0f};
	uint32_t prevTimePerTickInverse{0xFFFFFFFF};

	// Voice count tracking (for note retrigger in non-legato modes)
	uint8_t lastVoiceCount{0};

	// Held notes tracking (for Once mode retrigger)
	// Updated via notifyNoteOn/notifyNoteOff called from Sound
	uint8_t heldNotesCount{0};
	uint8_t lastHeldNotesCount{0}; // For detecting note count changes

	/// Called from Sound::noteOn - tracks held notes for Once mode
	void notifyNoteOn() { heldNotesCount++; }

	/// Called from Sound::noteOff - tracks held notes for Once mode
	void notifyNoteOff() {
		if (heldNotesCount > 0) {
			heldNotesCount--;
		}
	}

	// === LAZILY ALLOCATED POINTERS ===
	// These are only allocated when the effect is active (mix > 0)
	AutomodPhiCache* cache{nullptr};    // ~400 bytes when allocated
	AutomodDspState* dspState{nullptr}; // ~200 bytes when allocated

	// Comb filter buffers - allocated separately in SDRAM (8KB per Sound when active)
	static constexpr size_t kCombBufferSize = 1024; // Power of 2 for harmonic wrap (~23ms)
	q31_t* combBufferL{nullptr};
	q31_t* combBufferR{nullptr};

	// Counter for deferred deallocation when disabled
	// After kDeallocDelayBuffers of mix=0, deallocate comb buffers to save memory
	static constexpr uint16_t kDeallocDelayBuffers = 10000; // ~29 seconds at 344 buffers/sec
	uint16_t disabledBufferCount{0};

	/// Check if automodulator is enabled (mix > 0 = active)
	[[nodiscard]] bool isEnabled() const { return mix > 0; }

	/// Check if state is allocated (cache and dspState)
	[[nodiscard]] bool hasState() const { return cache != nullptr && dspState != nullptr; }

	/// Check if comb buffers are allocated
	[[nodiscard]] bool hasCombBuffers() const { return combBufferL != nullptr; }

	/// Ensure cache and dspState are allocated (called when effect becomes active)
	/// Returns false if allocation fails
	bool ensureStateAllocated() {
		if (cache == nullptr) {
			void* mem = delugeAlloc(sizeof(AutomodPhiCache), true);
			if (mem == nullptr) {
				return false;
			}
			cache = new (mem) AutomodPhiCache();
		}
		if (dspState == nullptr) {
			void* mem = delugeAlloc(sizeof(AutomodDspState), true);
			if (mem == nullptr) {
				return false;
			}
			dspState = new (mem) AutomodDspState();
		}
		return true;
	}

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
		if (dspState != nullptr) {
			dspState->combIdx = 0;
		}
		return true;
	}

	/// Deallocate comb buffers only
	void deallocateCombBuffers() {
		if (combBufferL != nullptr) {
			delugeDealloc(combBufferL);
			combBufferL = nullptr;
		}
		if (combBufferR != nullptr) {
			delugeDealloc(combBufferR);
			combBufferR = nullptr;
		}
	}

	/// Deallocate all lazily-allocated state
	void deallocateAll() {
		deallocateCombBuffers();
		if (dspState != nullptr) {
			dspState->~AutomodDspState();
			delugeDealloc(dspState);
			dspState = nullptr;
		}
		if (cache != nullptr) {
			cache->~AutomodPhiCache();
			delugeDealloc(cache);
			cache = nullptr;
		}
	}

private:
	/// Move all data from another instance (transfers pointer ownership)
	/// Note: freqOffset not moved here - it's in param system (GLOBAL_AUTOMOD_FREQ)
	void moveFrom(AutomodulatorParams& other) noexcept {
		// Copy page 1 params (freqOffset handled by param system move)
		rate = other.rate;
		rateSynced = other.rateSynced;
		lfoMode = other.lfoMode;
		mix = other.mix;
		// Copy page 2 params
		type = other.type;
		flavor = other.flavor;
		mod = other.mod;
		typePhaseOffset = other.typePhaseOffset;
		flavorPhaseOffset = other.flavorPhaseOffset;
		modPhaseOffset = other.modPhaseOffset;
		gammaPhase = other.gammaPhase;

		// Copy cache tracking
		prevRate = other.prevRate;
		prevRateSynced = other.prevRateSynced;
		prevLfoMode = other.prevLfoMode;
		prevType = other.prevType;
		prevFlavor = other.prevFlavor;
		prevMod = other.prevMod;
		prevGammaPhase = other.prevGammaPhase;
		prevTypePhaseOffset = other.prevTypePhaseOffset;
		prevFlavorPhaseOffset = other.prevFlavorPhaseOffset;
		prevModPhaseOffset = other.prevModPhaseOffset;
		prevTimePerTickInverse = other.prevTimePerTickInverse;
		lastVoiceCount = other.lastVoiceCount;
		heldNotesCount = other.heldNotesCount;
		lastHeldNotesCount = other.lastHeldNotesCount;
		disabledBufferCount = other.disabledBufferCount;

		// Transfer pointer ownership
		cache = other.cache;
		dspState = other.dspState;
		combBufferL = other.combBufferL;
		combBufferR = other.combBufferR;

		// Clear source pointers
		other.cache = nullptr;
		other.dspState = nullptr;
		other.combBufferL = nullptr;
		other.combBufferR = nullptr;
	}

public:
	/// Check if any cached values need recomputation
	/// Note: freqOffset is NOT included here - it's applied dynamically for mod matrix support
	[[nodiscard]] bool needsCacheUpdate(uint32_t timePerTickInverse) const {
		return (rate != prevRate || rateSynced != prevRateSynced || lfoMode != prevLfoMode || type != prevType
		        || flavor != prevFlavor || mod != prevMod || gammaPhase != prevGammaPhase
		        || typePhaseOffset != prevTypePhaseOffset || flavorPhaseOffset != prevFlavorPhaseOffset
		        || modPhaseOffset != prevModPhaseOffset || timePerTickInverse != prevTimePerTickInverse);
	}

	/// Reset DSP state (if allocated)
	void resetState() {
		if (dspState != nullptr) {
			*dspState = AutomodDspState{}; // Reset to defaults
		}
		// Clear comb buffers if allocated
		if (combBufferL != nullptr && combBufferR != nullptr) {
			std::memset(combBufferL, 0, kCombBufferSize * sizeof(q31_t));
			std::memset(combBufferR, 0, kCombBufferSize * sizeof(q31_t));
		}
	}

	/// Invalidate cache tracking (forces recomputation on next use)
	void invalidateCacheTracking() {
		prevRate = 0xFFFF;
		prevRateSynced = false;
		prevLfoMode = AutomodLfoMode::FREE; // Use a valid default
		prevType = 0xFFFF;
		prevFlavor = 0xFFFF;
		prevMod = 0xFFFF;
		prevGammaPhase = -999.0f;
		prevTypePhaseOffset = -999.0f;
		prevFlavorPhaseOffset = -999.0f;
		prevModPhaseOffset = -999.0f;
		prevTimePerTickInverse = 0xFFFFFFFF;
	}
};

// ============================================================================
// Function declarations (implementations in automodulator.cpp)
// ============================================================================

/// Evaluate filter mix from type position using phi triangles
FilterMix getFilterMixFromType(uint16_t type, float phaseOffset);

/// Get LFO initial phase from mod position
uint32_t getLfoInitialPhaseFromMod(uint16_t mod, float phaseOffset);

/// Compute LFO wavetable waypoints from mod position
/// Phase deltas are accumulated and normalized to guarantee monotonic ordering
LfoWaypointBank getLfoWaypointBank(uint16_t mod, float phaseOffset);

/// Evaluate LFO wavetable returning q31 (for DSP loop)
q31_t evalLfoWavetableQ31(uint32_t phaseU32, const LfoWaypointBank& bank);

/// Update the phi triangle cache (called when zone params change)
void updateAutomodPhiCache(AutomodulatorParams& params, uint32_t timePerTickInverse);

/// Process automodulator effect on a stereo buffer
/// @param buffer Audio buffer to process in-place
/// @param params Automodulator params and state
/// @param depth Modulation depth from param system (q31, bipolar: 0=100%, negative=less, positive=more)
/// @param freqOffset Filter frequency offset from param system (q31, bipolar)
/// @param manual Manual LFO offset from param system (q31, bipolar) - added to LFO or used directly when stopped
/// @param useInternalOsc True to use internal LFO, false for envelope follower
/// @param voiceCount Current number of active voices (for note retrigger)
/// @param timePerTickInverse For tempo sync (from playbackHandler), 0 if clock not active
/// @param noteCode Last played MIDI note for pitch tracking (-1 if none)
/// @param isLegato True if sound is in legato mode (uses note tracking instead of voice tracking)
void processAutomodulator(std::span<StereoSample> buffer, AutomodulatorParams& params, q31_t depth, q31_t freqOffset,
                          q31_t manual, bool useInternalOsc, uint8_t voiceCount = 0, uint32_t timePerTickInverse = 0,
                          int32_t noteCode = kNoteCodeInvalid, bool isLegato = false);

} // namespace deluge::dsp
