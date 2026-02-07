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

#include "dsp/disperser.h"
#include "dsp/util.hpp"

namespace deluge::dsp {

using namespace phi;

namespace {
// Frequency modulation: non-monotonic triangle oscillator
// fm is not used here - this generates the fm values for other triangles
[[gnu::always_inline]] inline float freqMod(float pos, double phRaw, float phiFreq, float range = 0.5f,
                                            float duty = 0.8f) {
	return 1.0f + triangleSimpleUnipolar(wrapPhase((pos + phRaw) * phiFreq), duty) * range;
}

// Phi triangle (unipolar 0-1)
// fm modulates the position traversal rate, NOT the phase offset
// This prevents chaotic jumps at high gamma when fm changes slightly
[[gnu::always_inline]] inline float phiTri(float pos, double phRaw, float phiFreq, float fm, float offset,
                                           float duty = 0.8f) {
	// Separate: fm scales position traversal, phRaw shifts uniformly
	double phase = static_cast<double>(pos) * phiFreq * fm + (phRaw + offset) * phiFreq;
	return triangleSimpleUnipolar(wrapPhase(phase), duty);
}

// Phi triangle (bipolar -1 to +1)
// fm modulates the position traversal rate, NOT the phase offset
[[gnu::always_inline]] inline float phiTriBi(float pos, double phRaw, float phiFreq, float fm, float offset,
                                             float duty = 0.5f) {
	double phase = static_cast<double>(pos) * phiFreq * fm + (phRaw + offset) * phiFreq;
	return triangleFloat(wrapPhase(phase), duty);
}
} // namespace

/**
 * Topology Zone Descriptions:
 *
 * Zone 0: Cascade - classic disperser with position→Q (Pinch) mapping
 *         Low position = low Q (broad, subtle), high = high Q (sharp, resonant)
 * Zone 1: Ladder - progressive cross-coupling through cascade
 * Zone 2: Stereo Spread - L/R get different frequency offsets
 * Zone 3: Cross-Coupled - L↔R feedback mixing between stages
 * Zone 4: Parallel - two parallel cascades for thick, chorus-like character
 * Zone 5: Nested - Schroeder-style nested allpass structure
 * Zone 6: Diffuse - randomized per-stage coefficient variation
 * Zone 7: Spring - chirp/spring reverb character
 */

DisperserTopoParams computeDisperserTopoParams(q31_t smoothedTopo, const DisperserParams* params,
                                               float twistPhaseOffset) {
	auto zoneInfo = computeZoneQ31(smoothedTopo, kDisperserNumZones);

	DisperserTopoParams result;
	result.zone = zoneInfo.index;

	// Position within current zone (0-1)
	float pos = std::clamp(zoneInfo.position, 0.0f, 1.0f);

	// Get phase offset from secret knob (topoPhaseOffset) - keep double precision
	double phRaw = params ? params->phases.effectiveTopo() : 0.0;

	// For detuning/harmonicBlend: add twist meta position to phase
	// This allows twist to "rotate" through topo's parameter evolution
	double phRawDet = phRaw + static_cast<double>(twistPhaseOffset);

	// Per-param frequency modulation using phi triangles (non-monotonic)
	float fm0 = freqMod(pos, phRaw, kPhi025);
	float fm1 = freqMod(pos, phRaw, kPhi050);
	float fm2 = freqMod(pos, phRaw, kPhi075);

	// Frequency modulation for detuning/harmonicBlend/emphasis uses phRawDet
	float fmDet = freqMod(pos, phRawDet, kPhi033);
	float fmHarm = freqMod(pos, phRawDet, kPhi067);
	float fmEmph = freqMod(pos, phRawDet, kPhi100);

	// Q phi triangle - shared across ALL topos
	// At phRaw=0: Q goes 0.5→20.0 monotonically (like cascade)
	// As phRaw increases: phase shifts (diverges from baseline)
	constexpr float kPhiN150 = 1.0f / kPhi150; // φ^-1.5 ≈ 0.486
	constexpr float kLog40 = 3.6888794541f;    // log(40) for fast power: 40^x = exp(x*log40)
	float qTriangle = triangleSimpleUnipolar(wrapPhase((pos + phRaw) * kPhiN150), 1.0f);
	result.q = 0.5f * expf(qTriangle * kLog40); // 0.5 to 20.0 range (fast: 40^x = exp(x*log40))

	// Each zone uses these triangles differently
	// The param meanings vary by topology - DSP dispatch interprets them
	switch (result.zone) {
	case 0: // Cascade: classic kHz Disperser behavior
		// param0/1 can still evolve with phi-triangles for subtle modulation
		result.param0 = phiTri(pos, phRaw, kPhi025, fm0, 0.1f);
		result.param1 = phiTri(pos, phRaw, kPhi050, fm1, 0.3f);
		result.param2 = 0.0f; // spread=0 for classic cascade (all stages same freq)
		result.lrOffset = 0.0f;
		// Cascade: subtle detuning at low Q (shimmer), less at high Q (focus)
		// Uses phRawDet so twist meta position can rotate through the pattern
		// Duty 0.63 for smooth shimmer at low Q
		result.detuning = phiTri(pos, phRawDet, kPhi033 * 0.5f, fmDet, 0.2f, 0.63f) * (1.0f - pos * 0.7f);
		// Harmonics increase with Q (sharper = more overtone emphasis)
		result.harmonicBlend = phiTri(pos, phRawDet, kPhi067, fmHarm, 0.4f) * pos;
		// Cascade: subtle emphasis that increases with Q (sharper = more spectral contrast)
		// Duty 0.35: quick rise, slow fall - more time with negative (low freq) emphasis
		result.emphasis = phiTriBi(pos, phRawDet, kPhi100, fmEmph, 0.55f, 0.35f) * pos * 0.6f;
		break;

	case 1: // Ping-Pong: alternation depth, L/R phase, freq split
		result.param0 = phiTri(pos, phRaw, kPhi025, fm0, 0.1f, 0.8f);
		result.param1 = phiTri(pos, phRaw, kPhi050, fm1, 0.3f, 0.7f);
		result.param2 = phiTri(pos, phRaw, kPhi075, fm2, 0.6f, 0.6f);
		result.lrOffset = result.param1 * 0.5f; // L/R phase difference
		// Ping-pong: moderate detuning for stereo shimmer
		result.detuning = phiTri(pos, phRawDet, kPhi050 * 0.5f, fmDet, 0.3f, 0.54f);
		// Balanced harmonics evolving with alternation
		result.harmonicBlend = phiTri(pos, phRawDet, kPhi033, fmHarm, 0.5f, 0.5f);
		// Ping-pong: emphasis alternates for stereo spectral interest
		// Duty 0.6: slower rise, quicker fall - more time with positive (high freq) emphasis
		result.emphasis = phiTriBi(pos, phRawDet, kPhi075, fmEmph, 0.4f, 0.6f) * 0.5f;
		break;

	case 2: // Bimodal (Formant) - stages cluster into two frequency groups
		// Position controls separation between modes (0=together, 1=5 octaves apart)
		// Safe because modes reach toward each other, keeping stages bounded
		result.param0 = pos * 5.0f; // 0-5 octave separation
		// param1 evolves balance between modes via phi-triangle
		result.param1 = phiTri(pos, phRaw, kPhi075, fm1, 0.5f, 0.6f);
		result.lrOffset = pos * 0.4f; // L/R get opposite modes at high separation
		// Bimodal: detuning increases with separation (formant shimmer)
		// Duty 0.59 for smooth formant transitions
		result.detuning = phiTri(pos, phRawDet, kPhi025 * 0.5f, fmDet, 0.15f, 0.59f) * (0.3f + pos * 0.7f);
		// Harmonics follow mode balance (shifting timbral emphasis)
		result.harmonicBlend = phiTri(pos, phRawDet, kPhi075, fmHarm, 0.5f, 0.6f);
		// Bimodal: emphasis follows mode separation (more contrast at wider separation)
		// Duty 0.45: near-symmetric with slight low-freq bias
		result.emphasis = phiTriBi(pos, phRawDet, kPhi050, fmEmph, 0.3f, 0.45f) * (0.2f + pos * 0.6f);
		break;

	case 3: // Cross-Coupled: cross amount, asymmetry, damping
		result.param0 = phiTri(pos, phRaw, kPhi050, fm0, 0.2f);
		result.param1 = phiTriBi(pos, phRaw, kPhi075, fm1, 0.5f) * 0.5f + 0.5f;
		result.param2 = phiTri(pos, phRaw, kPhi025, fm2, 0.8f);
		result.lrOffset = (result.param1 - 0.5f) * 0.3f; // Asymmetry creates offset
		// Cross: detuning from asymmetry (swirling stereo)
		// Duty 0.5 for more percussive swirl character
		result.detuning = std::abs(result.param1 - 0.5f) * phiTri(pos, phRawDet, kPhi067 * 0.5f, fmDet, 0.4f, 0.5f);
		// Harmonics from cross amount (more coupling = richer harmonics)
		result.harmonicBlend = phiTri(pos, phRawDet, kPhi050, fmHarm, 0.2f) * 0.8f;
		// Cross: emphasis from asymmetry for swirling spectral contrast
		// Duty 0.7: slow rise, fast fall - extended high-freq dwell for swirl character
		result.emphasis = (result.param1 - 0.5f) * phiTriBi(pos, phRawDet, kPhi033, fmEmph, 0.6f, 0.7f) * 0.7f;
		break;

	case 4: // Parallel: two cascades in parallel for thick chorus-like character
		// param0: spread between paths (path A centered lower, path B centered higher)
		result.param0 = phiTri(pos, phRaw, kPhi050, fm0, 0.3f, 0.6f);
		// param1: balance evolution between paths
		result.param1 = phiTri(pos, phRaw, kPhi075, fm1, 0.5f, 0.5f);
		result.param2 = phiTri(pos, phRaw, kPhi025, fm2, 0.4f, 0.7f);
		// L/R offset creates stereo width between parallel paths
		result.lrOffset = pos * 0.5f;
		// Parallel: strong detuning for thick chorus effect
		// The parallel structure + detuning creates ensemble-like thickness
		result.detuning = phiTri(pos, phRawDet, kPhi067 * 0.5f, fmDet, 0.5f, 0.55f);
		// Harmonics evolve with path spread
		result.harmonicBlend = phiTri(pos, phRawDet, kPhi033, fmHarm, 0.4f, 0.6f);
		// Parallel: moderate emphasis for timbral contrast between paths
		// Duty 0.5: symmetric for balanced parallel blend
		result.emphasis = phiTriBi(pos, phRawDet, kPhi050, fmEmph, 0.4f, 0.5f) * 0.5f;
		break;

	case 5: // Nested: nesting depth, inner/outer balance
		result.param0 = phiTri(pos, phRaw, kPhi033, fm0, 0.2f);
		result.param1 = phiTri(pos, phRaw, kPhi075, fm1, 0.45f);
		result.param2 = phiTri(pos, phRaw, kPhi050, fm2, 0.7f);
		result.lrOffset = result.param2 * 0.2f;
		// Nested: detuning for Schroeder diffusion shimmer
		result.detuning = phiTri(pos, phRawDet, kPhi033 * 0.5f, fmDet, 0.3f, 0.54f);
		// Harmonics evolve with nesting depth
		result.harmonicBlend = phiTri(pos, phRawDet, kPhi067, fmHarm, 0.45f, 0.5f);
		// Nested: emphasis for Schroeder spectral contrast
		// Duty 0.4: quick rise, extended fall - low-freq warmth for diffusion
		result.emphasis = phiTriBi(pos, phRawDet, kPhi100, fmEmph, 0.5f, 0.4f) * 0.55f;
		break;

	case 6: // Diffuse: randomness, correlation, drift
		result.param0 = phiTri(pos, phRaw, kPhi050, fm0, 0.15f);
		result.param1 = phiTri(pos, phRaw, kPhi033, fm1, 0.4f);
		result.param2 = phiTri(pos, phRaw, kPhi075, fm2, 0.65f);
		result.lrOffset = result.param0 * 0.4f; // Decorrelation
		// Diffuse: high detuning for maximum shimmer
		result.detuning = phiTri(pos, phRawDet, kPhi050 * 0.5f, fmDet, 0.4f, 0.63f);
		// Harmonics follow randomness
		result.harmonicBlend = phiTri(pos, phRawDet, kPhi025, fmHarm, 0.5f, 0.6f);
		// Diffuse: strong emphasis for maximum spectral variety
		// Duty 0.65: high-freq bias for bright diffusion character
		result.emphasis = phiTriBi(pos, phRawDet, kPhi067, fmEmph, 0.35f, 0.65f) * 0.7f;
		break;

	case 7: // Spring: chirp character, decay, density
		result.param0 = phiTri(pos, phRaw, kPhi025, fm0, 0.1f);
		result.param1 = phiTri(pos, phRaw, kPhi050, fm1, 0.3f);
		result.param2 = phiTri(pos, phRaw, kPhi075, fm2, 0.55f);
		result.lrOffset = result.param2 * 0.15f;
		// Spring: moderate detuning for spring reverb character
		result.detuning = phiTri(pos, phRawDet, kPhi075 * 0.5f, fmDet, 0.25f, 0.45f);
		// Harmonics evolve for spring timbre
		result.harmonicBlend = phiTri(pos, phRawDet, kPhi033, fmHarm, 0.35f, 0.6f);
		// Spring: moderate emphasis for spring timbral character
		// Duty 0.5: symmetric for balanced spring response
		result.emphasis = phiTriBi(pos, phRawDet, kPhi050, fmEmph, 0.45f, 0.5f) * 0.45f;
		break;

	default:
		break;
	}

	return result;
}

/**
 * Twist Zone Descriptions:
 *
 * Maximum chirp architecture - transient emphasis for bigger chirps.
 *
 * Zone 0: Width - Stereo spread via L/R frequency offset
 * Zone 1: Punch - Transient emphasis before dispersion (bigger chirps!)
 * Zone 2: Curve - Frequency distribution (low cluster → linear → high cluster)
 * Zone 3: Chirp - Transient-triggered delay for chirp echoes
 * Zone 4: QTilt - Q varies across stages (uniform → high sharp → low sharp)
 * Zones 5-7: Meta - All effects combined with φ-triangle evolution
 */

DisperserTwistParams computeDisperserTwistParams(q31_t smoothedTwist, const DisperserParams* params) {
	auto zoneInfo = computeZoneQ31(smoothedTwist, kDisperserNumZones);
	constexpr q31_t kZone5Start = getZoneStart(5, kDisperserNumZones);

	DisperserTwistParams result;

	// Get combined phase offset (twistPhaseOffset + 100*gammaPhase)
	double phRaw = params ? params->phases.effectiveMeta() : 0.0;

	// When phase offset is active, use full phi-triangle evolution across entire range
	// This matches table shaper's behavior: phaseOffset > 0 = full parameter interference
	if (phRaw != 0.0) {
		// Full range phi-triangle evolution (like meta zones, but across all 8 zones)
		float pos = static_cast<float>(smoothedTwist) / static_cast<float>(ONE_Q31);
		pos = std::clamp(pos, 0.0f, 1.0f);

		// Per-effect frequency modulation using phi triangles (non-monotonic)
		float fmW = freqMod(pos, phRaw, kPhi025);
		float fmP = freqMod(pos, phRaw, kPhi033);
		float fmC = freqMod(pos, phRaw, kPhi050);
		float fmQ = freqMod(pos, phRaw, kPhi067);
		float fmD = freqMod(pos, phRaw, kPhi075);

		// Width: scale * param pattern
		float wS = std::min(phiTri(pos, phRaw, kPhi025, fmW, 0.166f, 0.8f) * 2.0f, 1.0f);
		float wP = phiTri(pos, phRaw, kPhi050, fmW, 0.984f, 0.7f);
		result.width = wS * wP;

		// Punch evolves - more punch during certain phases
		result.punch = phiTri(pos, phRaw, kPhi033, fmP, 0.3f, 0.7f);

		// Curve sweeps bipolar
		float curveRaw = phiTriBi(pos, phRaw, kPhi050, fmC, 0.5f);
		result.spreadCurve = 0.5f + curveRaw * 0.5f; // Map to 0-1

		// Chirp feedback evolves (delay time from freq knob)
		result.chirpAmount = phiTri(pos, phRaw, kPhi067, fmD, 0.4f, 0.6f);

		// Q tilt sweeps bipolar
		result.qTilt = phiTriBi(pos, phRaw, kPhiN025, fmQ, 0.7f) * 0.8f;

		// Phase offset for topo: twist position rotates through topo's phi triangle patterns
		// 5 cycles per full sweep (like sine shaper)
		result.phaseOffset = pos * 5.0f;

		// LFO rate scale: 0.25×–2× with 70% duty (30% deadzone at minimum rate)
		float fmLfo = freqMod(pos, phRaw, kPhi100);
		result.lfoRateScale = 0.25f + phiTri(pos, phRaw, kPhi150, fmLfo, 0.6f, 0.70f) * 1.75f;
	}
	else if (zoneInfo.index < 5) {
		// Zones 0-4: Individual effects (phaseOffset == 0 only)
		float pos = std::clamp(zoneInfo.position, 0.0f, 1.0f);

		switch (zoneInfo.index) {
		case 0: // Width - stereo spread only
			result.width = pos;
			break;

		case 1: // Punch - transient emphasis before dispersion
			// pos controls transient boost amount (0 = none, 1 = max ~12dB)
			// More punch = bigger chirps because transients have more energy to disperse
			result.punch = pos;
			break;

		case 2: // Curve - bipolar frequency spread distribution
			// pos=0: stages cluster at LOW frequencies
			// pos=0.5: LINEAR distribution (even spacing)
			// pos=1: stages cluster at HIGH frequencies
			result.spreadCurve = pos;
			break;

		case 3: // Chirp - feedback-based chirp echoes
			// pos controls feedback amount (delay time from freq knob)
			// Higher feedback = more repeating chirp echoes
			result.chirpAmount = pos;
			break;

		case 4: // QTilt - Q varies across stages
			// Continuous: 0→0 tilt, 0.5→+1 tilt (high sharp), 1→-1 tilt (low sharp)
			if (pos < 0.5f) {
				result.qTilt = pos * 2.0f; // 0 → +1
			}
			else {
				result.qTilt = 3.0f - pos * 4.0f; // +1 → -1
			}
			break;
		}
	}
	else {
		// Zones 5-7: Meta - all effects with φ-triangle evolution (phaseOffset == 0)
		float pos = static_cast<float>(smoothedTwist - kZone5Start) / static_cast<float>(ONE_Q31 - kZone5Start);
		pos = std::clamp(pos, 0.0f, 1.0f);

		// Per-effect frequency modulation using phi triangles (non-monotonic)
		// phRaw is 0 here, so this gives baseline behavior
		float fmW = freqMod(pos, phRaw, kPhi025);
		float fmP = freqMod(pos, phRaw, kPhi033);
		float fmC = freqMod(pos, phRaw, kPhi050);
		float fmQ = freqMod(pos, phRaw, kPhi067);
		float fmD = freqMod(pos, phRaw, kPhi075);

		// Width: scale * param pattern
		float wS = std::min(phiTri(pos, phRaw, kPhi025, fmW, 0.166f, 0.8f) * 2.0f, 1.0f);
		float wP = phiTri(pos, phRaw, kPhi050, fmW, 0.984f, 0.7f);
		result.width = wS * wP;

		// Punch evolves - more punch during certain phases
		result.punch = phiTri(pos, phRaw, kPhi033, fmP, 0.3f, 0.7f);

		// Curve sweeps bipolar
		float curveRaw = phiTriBi(pos, phRaw, kPhi050, fmC, 0.5f);
		result.spreadCurve = 0.5f + curveRaw * 0.5f; // Map to 0-1

		// Chirp feedback evolves (delay time from freq knob)
		result.chirpAmount = phiTri(pos, phRaw, kPhi067, fmD, 0.4f, 0.6f);

		// Q tilt sweeps bipolar
		result.qTilt = phiTriBi(pos, phRaw, kPhiN025, fmQ, 0.7f) * 0.8f;

		// Phase offset for topo: twist meta position rotates through topo's phi triangle patterns
		// 5 cycles per full meta sweep (like sine shaper)
		result.phaseOffset = pos * 5.0f;

		// LFO rate scale: 0.25×–2× with 70% duty (30% deadzone at minimum rate)
		float fmLfo = freqMod(pos, phRaw, kPhi100);
		result.lfoRateScale = 0.25f + phiTri(pos, phRaw, kPhi150, fmLfo, 0.6f, 0.70f) * 1.75f;
	}

	return result;
}

} // namespace deluge::dsp
