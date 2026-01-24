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

#include "dsp/reverb/featherverb.hpp"
#include "dsp/phi_triangle.hpp"
#include "memory/general_memory_allocator.h"
#include "util/cfunctions.h"
#include "util/fixedpoint.h"

namespace deluge::dsp::reverb {

using namespace deluge::dsp;

// Compile-time diagnostic toggles (for permanent debugging builds)
// Runtime toggle cascadeOnly_ controls dry subtraction - via predelay encoder button
static constexpr bool kMuteEarly = false;
static constexpr bool kMuteCascade = false;
static constexpr bool kMuteCascadeFeedback = false;
static constexpr bool kBypassFdnToCascade = false;
static constexpr bool kDisableVastUndersample = false;

// === Development notes ===
//
// RESOLVED: C3 at 8x undersample caused ringing
// C0/C1/C2 at 4x works fine. C3 at 8x caused audible ringing at high Zone 3.
// Root cause likely: allpass coefficient (up to 0.7) too high at 8x rate, or
// aliasing from insufficient anti-aliasing at that decimation factor.
// Solution: All cascade stages now use uniform 4x undersample in vast mode.
//
// FEEDBACK TUNING (after 8x undersample fix):
// With C3 at 4x (not 8x), moderate feedback increases are now stable:
// - cascadeNestFeedback_ max: 0.55 (was 0.45)
// - cascadeFeedbackMult_ max: 1.0 (was 0.95)
// - cascadeNestFeedbackBase_ max: 0.4, kicks in at 0.35 (was 0.3, kicked in at 0.4)
// - vastBoost: +0.25 in zone 7 only (compensates for pre-AA filter)
// Previous experiment with 0.7/1.1 caused ringing - may have been 8x undersample issue.
//
// MULTI-TAP WRITES FOR DENSITY:
// Multi-tap reads block CPU waiting for memory; multi-tap writes can be pipelined.
// Each cascade stage writes to both normal position AND prime-offset position,
// doubling impulse density without read penalty. Sequential writes stay cache-hot.
// Enabled via kEnableMultiTapWrites in header. Offsets are prime numbers for good diffusion.

Featherverb::Featherverb() {
	// Compute buffer offsets for contiguous layout
	size_t offset = 0;

	// FDN delays (3 delays)
	for (size_t i = 0; i < kNumFdnDelays; ++i) {
		fdnOffsets_[i] = offset;
		offset += (i == 0) ? kD0MaxLength : (i == 1) ? kD1MaxLength : kD2MaxLength;
	}

	// Cascade stages (4 allpass delays) - allocate max size
	size_t cascadeMaxLengths[kNumCascade] = {
	    static_cast<size_t>(kC0BaseLength * kCascadeMaxScale), static_cast<size_t>(kC1BaseLength * kCascadeMaxScale),
	    static_cast<size_t>(kC2BaseLength * kCascadeMaxScale), static_cast<size_t>(kC3BaseLength * kCascadeMaxScale)};
	for (size_t i = 0; i < kNumCascade; ++i) {
		cascadeOffsets_[i] = offset;
		offset += cascadeMaxLengths[i];
	}

	// Predelay
	predelayOffset_ = offset;

	// Initialize defaults
	setRoomSize(0.5f);
	setDamping(0.5f);
	updateMatrix();
	updateSizes();
	updateFeedbackPattern();
}

bool Featherverb::allocate() {
	if constexpr (kUseStaticBss) {
		// Static BSS - buffer already points to staticBuffer_, just clear it
		std::memset(buffer_, 0, kBufferBytes);
	}
	else {
		// Dynamic SDRAM allocation
		if (buffer_ == nullptr) {
			buffer_ = static_cast<float*>(GeneralMemoryAllocator::get().allocMaxSpeed(kBufferBytes, nullptr));
			if (buffer_ == nullptr) {
				return false;
			}
		}
		std::memset(buffer_, 0, kBufferBytes);
	}

	// Reset state
	fdnWritePos_.fill(0);
	fdnLpState_.fill(0.0f);
	cascadeWritePos_.fill(0);
	cascadeLpState_ = 0.0f;
	cascadeLpStateMono_ = 0.0f;
	cascadeLpStateSide_ = 0.0f;
	feedbackEnvelope_ = 0.0f;
	prevC3Out_ = 0.0f;
	predelayWritePos_ = 0;
	dcBlockState_ = 0.0f;
	hpState_ = 0.0f;
	lpStateL_ = 0.0f;
	lpStateR_ = 0.0f;
	prevOutputMono_ = 0.0f;
	cascadeModDepth_ = 0.0f;
	cascadeAmpMod_ = 0.0f;

	// Reset undersampling
	undersamplePhase_ = false;
	accumIn_ = 0.0f;
	prevOutL_ = prevOutR_ = currOutL_ = currOutR_ = 0.0f;

	// Reset cascade extra undersampling
	cascadeDoubleUndersample_ = false;
	vastChainMode_ = false;
	skyChainMode_ = false;
	featherMode_ = false;
	c0Phase_ = 0;
	c0Accum_ = 0.0f;
	c0Prev_ = 0.0f;
	c1Phase_ = 0;
	c1Accum_ = 0.0f;
	c1Prev_ = 0.0f;
	c2Phase_ = 0;
	c2Accum_ = 0.0f;
	c2Prev_ = 0.0f;
	c3Phase_ = 0;
	c3Accum_ = 0.0f;
	c3Prev_ = 0.0f;
	owlD0Cache_ = 0.0f;
	owlD0ReadAccum_ = 0.0f;
	owlD0WriteAccum_ = 0.0f;
	owlD0WriteVal_ = 0.0f;
	owlD1Cache_ = 0.0f;
	owlD1ReadAccum_ = 0.0f;
	owlD1WriteAccum_ = 0.0f;
	owlD1WriteVal_ = 0.0f;
	owlD2Cache_ = 0.0f;
	owlD2ReadAccum_ = 0.0f;
	owlD2WriteAccum_ = 0.0f;
	owlD2WriteVal_ = 0.0f;

	return true;
}

void Featherverb::deallocate() {
	if constexpr (!kUseStaticBss) {
		// Dynamic allocation - free the buffer
		if (buffer_ != nullptr) {
			delugeDealloc(buffer_);
			buffer_ = nullptr;
		}
	}
	// Static BSS - nothing to deallocate
}

void Featherverb::process(std::span<int32_t> input, std::span<StereoSample> output) {
	if (buffer_ == nullptr) {
		return;
	}

	constexpr float kInputScale = 1.0f / static_cast<float>(std::numeric_limits<int32_t>::max());
	constexpr float kOutputScale =
	    static_cast<float>(std::numeric_limits<int32_t>::max()) * 32.0f; // 2x boost vs original

	const float hpCoeff = 0.995f - hpCutoff_ * 0.09f;
	const float outLpCoeff = 0.1f + lpCutoff_ * 0.85f;
	const float tailFeedback = feedback_ * feedback_; // Hoist: tail decays faster than early

	// Cache matrix
	const auto& m = matrix_;

	for (size_t frame = 0; frame < input.size(); ++frame) {
		// === Full-rate: HPF, envelope, predelay ===
		float in = static_cast<float>(input[frame]) * kInputScale;
		float hpOut = in - hpState_;
		hpState_ += (1.0f - hpCoeff) * hpOut;

		// Save input for dry subtraction BEFORE gain boost
		float inOrig = hpOut;

		in = hpOut * 1.414f; // +3dB input gain to drive reverb harder

		// Predelay (single tap)
		if (predelayLength_ > 0) {
			writePredelay(in);
			in = readPredelay(predelayLength_);
		}

		float outL, outR;

		// === 2x Undersampling ===
		accumIn_ += in;

		if (undersamplePhase_) {
			float fdnIn = accumIn_ * 0.5f;
			accumIn_ = 0.0f;

			// LFO for pitch/amplitude modulation (only compute when needed)
			float lfoTri = 0.0f;
			size_t d0Mod = 0;
			size_t d1Mod = 0;
			if (cascadeDoubleUndersample_ || skyChainMode_ || modDepth_ > 0.0f) {
				if (skyChainMode_ || vastChainMode_ || owlMode_) {
					// Sky/Vast/Owl mode: random walk for organic pitch drift
					// LCG random: state = state * 1664525 + 1013904223
					skyRandState_ = skyRandState_ * 1664525u + 1013904223u;
					float randStep = (static_cast<float>(skyRandState_ >> 16) / 32768.0f - 1.0f); // -1 to 1
					// Step size controlled by skyLfoFreq_ (0.5x to 2.5x)
					float stepSize = 0.001f * skyLfoFreq_;
					skyRandWalk_ += randStep * stepSize;
					// Soft bounds: gentle pull back toward center
					skyRandWalk_ *= 0.9995f;
					skyRandWalk_ = std::clamp(skyRandWalk_, -1.0f, 1.0f);
					// Smooth the output (one-pole LP) - lower coeff = smoother
					float smoothCoeff = vastChainMode_ ? 0.012f * skyLfoFreq_  // Extra smooth for 4x
					                                   : 0.025f * skyLfoFreq_; // Normal for Sky
					skyRandWalkSmooth_ += smoothCoeff * (skyRandWalk_ - skyRandWalkSmooth_);
					lfoTri = skyRandWalkSmooth_;
					// Z3 controls routing: low = pitch wobble, high = amp mod
					float pitchScale = 1.0f - skyLfoRouting_;
					d0Mod = static_cast<size_t>(std::max(0.0f, lfoTri * modDepth_ * pitchScale));
					d1Mod = static_cast<size_t>(std::max(0.0f, -lfoTri * modDepth_ * pitchScale));
				}
				// Other modes: no pitch modulation (d0Mod/d1Mod stay 0)
			}

			// Read FDN delays
			float d0 = fdnReadAt(0, d0Mod);
			float d1 = fdnReadAt(1, d1Mod);
			float d2 = fdnRead(2);

			// 3x3 matrix multiply
			float h0 = m[0][0] * d0 + m[0][1] * d1 + m[0][2] * d2;
			float h1 = m[1][0] * d0 + m[1][1] * d1 + m[1][2] * d2;
			float h2 = m[2][0] * d0 + m[2][1] * d1 + m[2][2] * d2;

			// Cross-channel bleed: L↔R mixing for stereo complexity
			if (crossBleed_ > 0.0f) {
				float h0Orig = h0;
				h0 += h1 * crossBleed_;
				h1 += h0Orig * crossBleed_;
			}

			// Feedback (envelope-based auto-decay removed for performance)
			float effectiveFeedback = feedback_ * fdnFeedbackScale_;

			// Damping + feedback
			h0 = onepole(h0, fdnLpState_[0], dampCoeff_) * effectiveFeedback * feedbackMult_[0];
			h1 = onepole(h1, fdnLpState_[1], dampCoeff_) * effectiveFeedback * feedbackMult_[1];
			h2 = onepole(h2, fdnLpState_[2], dampCoeff_) * effectiveFeedback * feedbackMult_[2];

			// DC blocking on FDN
			float dcSum = (h0 + h1 + h2) * 0.333f;
			dcBlockState_ += 0.007f * (dcSum - dcBlockState_);
			h0 -= dcBlockState_;
			h1 -= dcBlockState_;
			h2 -= dcBlockState_;

			// === Cascade: 4-stage with parallel/series blend ===
			// c0→c1→c2 always series, c3 input blends between cascadeIn (parallel) and c2 (series)
			// seriesMix=0 → 9 paths (sparse), seriesMix=1 → 16 paths (dense)
			float cascadeIn;
			if (kBypassFdnToCascade) {
				cascadeIn = fdnIn * 1.4f + prevC3Out_ * cascadeNestFeedback_ * tailFeedback;
			}
			else {
				// 50% dry + 50% FDN to cascade
				cascadeIn = fdnIn * 0.7f + (d0 + d1 + d2) * 0.7f + prevC3Out_ * cascadeNestFeedback_ * tailFeedback;
			}

			// c0→c1→c2 series chain with optional 4x undersample for vast rooms
			float c0, c1, c2;
			float cascadeOutL, cascadeOutR;
			float dynamicWidth = width_; // Width breathing removed (was envelope-based)

			if (vastChainMode_) {
				// === VAST CHAIN MODE (Smeared feedback like Sky) ===
				// Topology with smeared feedback loops + global recirculation:
				//   Input → C0 → D0 → C1 → D1 → C2 → D2 → C3 → output
				//                 ↑         ↑         ↑
				//            (C2→C0)    (C3→C1)    (C3)
				// Feedback passes through cascade stages for diffusion before entering delays
				// This reduces comb filter sensitivity compared to direct feedback

				// Feedback coefficients: Room → density, Zone 3 → tail length
				// Self-limiting feedback: reduce when reverb level is high (can go to zero)
				float fbEnvScale = 1.0f - std::min(feedbackEnvelope_ * 8.0f, 1.0f);
				float loopFb = feedback_ * 0.4f * delayRatio_ * fbEnvScale * skyLoopFb_; // Z3 controls density
				float globalFb = cascadeNestFeedback_ * fbEnvScale * 0.8f;

				// Pre-decimation AA filter with global C3 feedback
				float chainIn = onepole(fdnIn * 1.4f + prevC3Out_ * globalFb, cascadeAaState1_, kPreCascadeAaCoeff);

				// Z1 controls balance between feedback paths (like Sky)
				float c2Fb = loopFb * (1.6f - skyFbBalance_ * 1.4f); // 1.6 → 0.2
				float c3Fb = loopFb * (0.2f + skyFbBalance_ * 1.4f); // 0.2 → 1.6

				// C0 with 4x undersample (input → first allpass)
				c0Accum_ += chainIn;
				if (c0Phase_ == 1) {
					float avgIn = c0Accum_ * 0.5f;
					c0Prev_ = processCascadeStage(0, avgIn);
					// Smeared feedback: C2 → C0 → D0 (Z1 controls balance)
					float c2Smeared = processCascadeStage(0, c2Prev_ * c2Fb);
					c0Prev_ += c2Smeared;
					c0Accum_ = 0.0f;
				}
				c0Phase_ = (c0Phase_ + 1) & 1;
				c0 = c0Prev_;

				// D0 delay between C0 and C1 (modulated pitch)
				fdnWrite(0, c0);
				fdnWrite(0, c0); // Double write for 4x undersample
				float d0Out = fdnReadAt(0, d0Mod);

				// C1 with 4x undersample
				c1Accum_ += d0Out;
				if (c1Phase_ == 1) {
					float avgIn = c1Accum_ * 0.5f;
					c1Prev_ = processCascadeStage(1, avgIn);
					// Smeared feedback: C3 → C1 → D1 (Z1 controls balance)
					float c3Smeared = processCascadeStage(1, c3Prev_ * c3Fb);
					c1Prev_ += c3Smeared;
					c1Accum_ = 0.0f;
				}
				c1Phase_ = (c1Phase_ + 1) & 1;
				c1 = c1Prev_;

				// D1 delay between C1 and C2 (modulated pitch)
				fdnWrite(1, c1);
				fdnWrite(1, c1);
				float d1Out = fdnReadAt(1, d1Mod);

				// C2 with 4x undersample + pitch modulation
				c2Accum_ += d1Out;
				if (c2Phase_ == 1) {
					float avgIn = c2Accum_ * 0.5f;
					float c2Coeff = cascadeCoeffs_[2];
					size_t origWritePos = cascadeWritePos_[2];
					// DIAGNOSTIC: cascade pitch mod disabled
					size_t c2ModOffset = 0; // static_cast<size_t>(std::max(0.0f, lfoTri * cascadeModDepth_));
					size_t readPos = (origWritePos + c2ModOffset) % cascadeLengths_[2];
					size_t idx = cascadeOffsets_[2] + readPos;
					float delayed = buffer_[idx];
					float output = -c2Coeff * avgIn + delayed;
					float writeVal = avgIn + c2Coeff * output;
					buffer_[cascadeOffsets_[2] + origWritePos] = writeVal;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					buffer_[cascadeOffsets_[2] + cascadeWritePos_[2]] = writeVal;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					if (cascadeDoubleUndersample_) {
						size_t tapPos = (origWritePos + kMultiTapOffsets[2]) % cascadeLengths_[2];
						buffer_[cascadeOffsets_[2] + tapPos] += writeVal * kMultiTapGain;
					}
					c2Prev_ = output;
					c2Accum_ = 0.0f;
				}
				c2Phase_ = (c2Phase_ + 1) & 1;
				c2 = c2Prev_;

				// D2 delay between C2 and C3 (no direct feedback - using smeared instead)
				fdnWrite(2, c2);
				fdnWrite(2, c2);
				float d2Out = fdnRead(2);

				// C3 with 4x undersample + inverted pitch modulation
				float c3;
				c3Accum_ += d2Out;
				if (c3Phase_ == 1) {
					float avgIn = c3Accum_ * 0.5f;
					float c3Coeff = cascadeCoeffs_[3];
					size_t origWritePos = cascadeWritePos_[3];
					// DIAGNOSTIC: cascade pitch mod disabled
					size_t c3ModOffset = 0; // static_cast<size_t>(std::max(0.0f, -lfoTri * cascadeModDepth_));
					size_t readPos = (origWritePos + c3ModOffset) % cascadeLengths_[3];
					size_t idx = cascadeOffsets_[3] + readPos;
					float delayed = buffer_[idx];
					float output = -c3Coeff * avgIn + delayed;
					float writeVal = avgIn + c3Coeff * output;
					buffer_[cascadeOffsets_[3] + origWritePos] = writeVal;
					if (++cascadeWritePos_[3] >= cascadeLengths_[3])
						cascadeWritePos_[3] = 0;
					buffer_[cascadeOffsets_[3] + cascadeWritePos_[3]] = writeVal;
					if (++cascadeWritePos_[3] >= cascadeLengths_[3])
						cascadeWritePos_[3] = 0;
					if (cascadeDoubleUndersample_) {
						size_t tapPos = (origWritePos + kMultiTapOffsets[3]) % cascadeLengths_[3];
						buffer_[cascadeOffsets_[3] + tapPos] += writeVal * kMultiTapGain;
					}
					c3Prev_ = output;
					c3Accum_ = 0.0f;
				}
				// Cache LFO value when cascade stages update to avoid discontinuities
				if (c3Phase_ == 0) {
					vastLfoCache_ = lfoTri; // Update cache when c3 finishes updating
				}
				c3Phase_ = (c3Phase_ + 1) & 1;
				c3 = c3Prev_;

				// Soft clip C3 at ~-2dB below 0dBFS (0.05) with 0.2 ratio
				// Input 0.1 → output 0.06 (0dBFS), smooth transition to hard clip
				constexpr float kC3Limit = 0.05f;
				if (std::abs(c3) > kC3Limit) {
					c3 = std::copysign(kC3Limit + (std::abs(c3) - kC3Limit) * 0.2f, c3);
				}
				prevC3Out_ = c3;

				// Track feedback envelope for self-limiting (fast attack, slow release)
				float c3Abs = std::abs(c3);
				float coeff = (c3Abs > feedbackEnvelope_) ? 0.05f : 0.0003f;
				feedbackEnvelope_ += coeff * (c3Abs - feedbackEnvelope_);

				// Amplitude modulation for diffusion contour
				// Z1 controls base amplitude, Z3 controls routing (low=pitch, high=amp)
				// Use cached LFO value to stay in sync with 4x cascade update rate
				float ampMod = skyLfoAmp_ * skyLfoRouting_;
				c2 *= (1.0f + vastLfoCache_ * ampMod);
				c3 *= (1.0f - vastLfoCache_ * ampMod);

				// Mix chain outputs - stereo from early and late stages
				float cascadeMono = (c2 + c3) * 0.5f;
				float earlySide = (c0 - c1) * cascadeSideGain_ * dynamicWidth;
				float lateSide = (c2 - c3) * cascadeSideGain_ * 0.6f * dynamicWidth;
				float cascadeSide = earlySide + lateSide;
				cascadeMono = onepole(cascadeMono, cascadeLpStateMono_, kCascadeLpCoeffMono);
				cascadeSide = onepole(cascadeSide, cascadeLpStateSide_, kCascadeLpCoeffSide);
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;
				// LFO output modulation: stereo movement + amplitude breathing
				// Z3 high routes LFO here (same as amp mod routing)
				float lfoOut = vastLfoCache_ * skyLfoAmp_ * skyLfoRouting_;
				cascadeOutL *= (1.0f + lfoOut * 0.3f); // ±30% amplitude
				cascadeOutR *= (1.0f - lfoOut * 0.3f); // Opposite phase for stereo movement

				// No early reflections in vast chain mode (FDN is repurposed)
				directEarlyL_ = 0.0f;
				directEarlyR_ = 0.0f;
			}
			else if (skyChainMode_) {
				// === SKY MODE (Smeared feedback through cascades at 2x) ===
				// Feedback goes through cascade stages for diffusion BEFORE entering delays.
				// This smears the feedback signal, reducing comb filter sensitivity.
				//
				//   Input → C0 → D0 → C1 → D1 → C2 → D2 → C3 → output
				//                 ↑         ↑
				//            (C2→C0)    (C3→C1)
				//
				// C3 output → smear through C1 → write to D1
				// C2 output → smear through C0 → write to D0

				float fbEnvScale = 1.0f - std::min(feedbackEnvelope_ * 8.0f, 1.0f);
				float loopFb = feedback_ * 0.4f * delayRatio_ * fbEnvScale * skyLoopFb_; // Z3 controls density
				float globalFb = cascadeNestFeedback_ * fbEnvScale * 0.8f;

				float chainIn = fdnIn * 1.4f + prevC3Out_ * globalFb;

				// C0 at 2x rate
				c0 = processCascadeStage(0, chainIn);

				// D0 delay - feedback from C2 smeared through C0 (modulated pitch)
				// Z1 controls balance: low Z1 = more C2→C0, high Z1 = more C3→C1
				float c2Fb = loopFb * (1.6f - skyFbBalance_ * 1.4f); // 1.6 → 0.2
				float c2Smeared = processCascadeStage(0, c2Prev_ * c2Fb);
				fdnWrite(0, c0 + c2Smeared);
				float d0Out = fdnReadAt(0, d0Mod);

				// C1 at 2x rate
				c1 = processCascadeStage(1, d0Out);

				// D1 delay - feedback from C3 smeared through C1 (modulated pitch)
				float c3Fb = loopFb * (0.2f + skyFbBalance_ * 1.4f); // 0.2 → 1.6
				float c3Smeared = processCascadeStage(1, c3Prev_ * c3Fb);
				fdnWrite(1, c1 + c3Smeared);
				float d1Out = fdnReadAt(1, d1Mod);

				// C2 at 2x rate
				c2 = processCascadeStage(2, d1Out);

				// D2 delay
				fdnWrite(2, c2);
				float d2Out = fdnRead(2);

				// C3 final stage
				float c3 = processCascadeStage(3, d2Out);

				// Soft clip C3 at ~-2dB below 0dBFS (0.05) with 0.2 ratio
				constexpr float kC3Limit = 0.05f;
				if (std::abs(c3) > kC3Limit) {
					c3 = std::copysign(kC3Limit + (std::abs(c3) - kC3Limit) * 0.2f, c3);
				}

				// Store for next iteration's smeared feedback
				c2Prev_ = c2;
				c3Prev_ = c3;
				prevC3Out_ = c3;

				// Track feedback envelope for self-limiting
				float c3Abs = std::abs(c3);
				float coeff = (c3Abs > feedbackEnvelope_) ? 0.05f : 0.0003f;
				feedbackEnvelope_ += coeff * (c3Abs - feedbackEnvelope_);

				// Amplitude modulation for diffusion contour
				// Z1 controls base amplitude, Z3 controls routing (low=pitch, high=amp)
				float ampMod = skyLfoAmp_ * skyLfoRouting_; // Scale by routing blend
				c2 *= (1.0f + lfoTri * ampMod);
				c3 *= (1.0f - lfoTri * ampMod);

				// Mix chain outputs - stereo from early and late stages
				float cascadeMono = (c2 + c3) * 0.5f;
				float earlySide = (c0 - c1) * cascadeSideGain_ * dynamicWidth;
				float lateSide = (c2 - c3) * cascadeSideGain_ * 0.6f * dynamicWidth;
				float cascadeSide = earlySide + lateSide;
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;
				// LFO output modulation: stereo movement + amplitude breathing
				float lfoOut = lfoTri * skyLfoAmp_ * skyLfoRouting_;
				cascadeOutL *= (1.0f + lfoOut * 0.3f);
				cascadeOutR *= (1.0f - lfoOut * 0.3f);

				// No early reflections in nested mode (FDN is repurposed as inter-stage delays)
				directEarlyL_ = 0.0f;
				directEarlyR_ = 0.0f;
			}
			else if (featherMode_) {
				// === FEATHER/OWL MODE ===
				// Dual parallel cascades: (C0→C1) for L, (C2→C3) for R
				// Feather (zone 4): 2x undersample, Owl (zone 6): 4x undersample
				// FDN provides shared early reflections, cascades create asymmetric stereo tail
				// Z3 controls cascade recirculation for extended tails

				// Cascade feedback from previous output (Z3 controlled)
				float cascadeFb = prevC3Out_ * cascadeNestFeedback_ * 0.6f;
				float cascadeInWithFb = cascadeIn + cascadeFb;

				float cL1, cR1;
				if (cascadeDoubleUndersample_) {
					// === OWL: 4x undersample on cascades with multi-tap density ===
					// Pre-decimation AA filter
					cascadeInWithFb = onepole(cascadeInWithFb, cascadeAaState1_, kPreCascadeAaCoeff);

					// Accumulate for L cascade (C0→C1)
					c0Accum_ += cascadeInWithFb;
					if (++c0Phase_ >= 2) {
						c0Phase_ = 0;
						float cL0 = processCascadeStage(0, c0Accum_ * 0.5f);
						// Cross-channel multi-tap: L writes to R's buffer (C0 → C2)
						size_t prevPos0 = (cascadeWritePos_[0] == 0) ? cascadeLengths_[0] - 1 : cascadeWritePos_[0] - 1;
						float writeVal0 = buffer_[cascadeOffsets_[0] + prevPos0];
						size_t tapPos0 = (prevPos0 + kMultiTapOffsets[0]) % cascadeLengths_[2];
						buffer_[cascadeOffsets_[2] + tapPos0] += writeVal0 * kMultiTapGain;

						c0Accum_ = 0.0f;
						c1Accum_ += cL0;
						if (++c1Phase_ >= 2) {
							c1Phase_ = 0;
							cL1 = processCascadeStage(1, c1Accum_ * 0.5f);
							// Cross-channel multi-tap: L writes to R's buffer (C1 → C3)
							size_t prevPos1 =
							    (cascadeWritePos_[1] == 0) ? cascadeLengths_[1] - 1 : cascadeWritePos_[1] - 1;
							float writeVal1 = buffer_[cascadeOffsets_[1] + prevPos1];
							size_t tapPos1 = (prevPos1 + kMultiTapOffsets[1]) % cascadeLengths_[3];
							buffer_[cascadeOffsets_[3] + tapPos1] += writeVal1 * kMultiTapGain;

							c1Accum_ = 0.0f;
							c1Prev_ = cL1;
						}
						else {
							cL1 = c1Prev_;
						}
						c0Prev_ = cL1;
					}
					else {
						cL1 = c0Prev_;
					}

					// Accumulate for R cascade (C2→C3)
					float cascadeInR = cascadeInWithFb * 0.98f + (d0 - d1) * 0.02f;
					c2Accum_ += cascadeInR;
					if (++c2Phase_ >= 2) {
						c2Phase_ = 0;
						float cR0 = processCascadeStage(2, c2Accum_ * 0.5f);
						// Cross-channel multi-tap: R writes to L's buffer (C2 → C0)
						size_t prevPos2 = (cascadeWritePos_[2] == 0) ? cascadeLengths_[2] - 1 : cascadeWritePos_[2] - 1;
						float writeVal2 = buffer_[cascadeOffsets_[2] + prevPos2];
						size_t tapPos2 = (prevPos2 + kMultiTapOffsets[2]) % cascadeLengths_[0];
						buffer_[cascadeOffsets_[0] + tapPos2] += writeVal2 * kMultiTapGain;

						c2Accum_ = 0.0f;
						c3Accum_ += cR0;
						if (++c3Phase_ >= 2) {
							c3Phase_ = 0;
							cR1 = processCascadeStage(3, c3Accum_ * 0.5f);
							// Cross-channel multi-tap: R writes to L's buffer (C3 → C1)
							size_t prevPos3 =
							    (cascadeWritePos_[3] == 0) ? cascadeLengths_[3] - 1 : cascadeWritePos_[3] - 1;
							float writeVal3 = buffer_[cascadeOffsets_[3] + prevPos3];
							size_t tapPos3 = (prevPos3 + kMultiTapOffsets[3]) % cascadeLengths_[1];
							buffer_[cascadeOffsets_[1] + tapPos3] += writeVal3 * kMultiTapGain;

							c3Accum_ = 0.0f;
							c3Prev_ = cR1;
						}
						else {
							cR1 = c3Prev_;
						}
						c2Prev_ = cR1;
					}
					else {
						cR1 = c2Prev_;
					}
				}
				else {
					// === FEATHER: 2x undersample (every sample) ===
					float cL0 = processCascadeStage(0, cascadeInWithFb);
					cL1 = processCascadeStage(1, cL0);

					float cascadeInR = cascadeInWithFb * 0.98f + (d0 - d1) * 0.02f;
					float cR0 = processCascadeStage(2, cascadeInR);
					cR1 = processCascadeStage(3, cR0);
				}

				// Cross-feed between L and R cascades - Z3 controls coherence/feedback tilt
				float crossFeed = 0.15f + cascadeNestFeedback_ * 0.5f;
				float cascadeOutLRaw = cL1 + cR1 * crossFeed;
				float cascadeOutRRaw = cR1 + cL1 * crossFeed;

				// Apply damping (extra LPFs for Owl mode)
				float cascadeL, cascadeR;
				if (cascadeDoubleUndersample_) {
					// First apply zone-controlled damping
					float cascadeMono = (cascadeOutLRaw + cascadeOutRRaw) * 0.5f;
					// Owl mode: moderate late cascade width for spacious tail
					float cascadeSide = (cascadeOutLRaw - cascadeOutRRaw) * 0.6f;
					cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
					// Then apply fixed mid/side AA filters for 4x rate
					cascadeMono = onepole(cascadeMono, cascadeLpStateMono_, kCascadeLpCoeffMono);
					cascadeSide = onepole(cascadeSide, cascadeLpStateSide_, kCascadeLpCoeffSide);
					cascadeL = cascadeMono + cascadeSide;
					cascadeR = cascadeMono - cascadeSide;
				}
				else {
					cascadeL = onepole(cascadeOutLRaw, cascadeLpState_, cascadeDamping_);
					cascadeR = onepole(cascadeOutRRaw, cascadeLpStateMono_, cascadeDamping_);
				}

				cascadeOutL = cascadeL;
				cascadeOutR = cascadeR;
				prevC3Out_ = (cL1 + cR1) * 0.5f;

				// Keep early reflections from FDN
				if (!kMuteEarly) {
					float earlyMid = (d0 + d1) * earlyMixGain_;
					// Owl mode: narrow early (60%) for focused transients, late cascade provides width
					float earlyWidthScale = cascadeDoubleUndersample_ ? 0.6f : 1.0f;
					float earlySide = (d0 - d1) * earlyMixGain_ * dynamicWidth * earlyWidthScale;
					directEarlyL_ = (earlyMid + earlySide) * directEarlyGain_;
					directEarlyR_ = (earlyMid - earlySide) * directEarlyGain_;
				}
				else {
					directEarlyL_ = 0.0f;
					directEarlyR_ = 0.0f;
				}

				// Inject input into FDN (no cascade feedback for cleaner separation)
				h0 += fdnIn;

				// Write FDN (double write for undersampling)
				fdnWrite(0, h0);
				fdnWrite(1, h1);
				fdnWrite(2, h2);
				fdnWrite(0, h0);
				fdnWrite(1, h1);
				fdnWrite(2, h2);
			}
			else if (cascadeDoubleUndersample_) {
				// === LUSH/OWL MODE ===
				// 4x undersample on cascade stages, but keeps FDN + cascade separate
				// Lush: 3-delay FDN for early reflections, cascade for tail
				// Owl: 2-delay FDN (D0/D1 Hadamard) + D2 as nested feedback path

				// Pre-decimation AA filter
				cascadeIn = onepole(cascadeIn, cascadeAaState1_, kPreCascadeAaCoeff);

				// Owl: feedback envelope controls all feedback paths
				// Simpler approach: just limit based on feedback level
				// New input enters unscaled (fdnIn), naturally takes over when feedback is limited
				float owlFbEnvScale = 1.0f;
				float owlDelayScale = 1.0f; // Squared scale for delay writes - pulls down faster
				float c3ForFb = 0.0f;       // DC-blocked feedback signal for envelope tracking
				if (owlMode_) {
					// Envelope is now squared (outAbs²), so values are ~100x smaller
					// Scale up to compensate
					// Since envelope is already squared, use linear excess (not excess²) = quadratic on original

					float rawEnv = feedbackEnvelope_ * 320000.0f;

					/**
					 * this was pretty good.
					float excess = std::max(0.0f, rawEnv - 0.01f); // Lower threshold, release controls pumping
					// Higher strength (10) for more pulldown
					owlFbEnvScale = 1.0f / (1.0f + excess * 1000.0f);
		*/
					float excess = std::max(0.0f, rawEnv - 0.0000f); // Lower threshold, release controls pumping

					// Higher strength (10) for more pulldown
					owlFbEnvScale = 1.0f / (1.0f + excess * excess * 5000.0f);
					// Delays get squared scaling - chokes loop faster, lets cascade ring out
					owlDelayScale = owlFbEnvScale * owlFbEnvScale;
				}

				// Owl mode: interleaved D0 → C0 → D1 → C1, all at 4x undersample
				// Input → D0 → C0 → D1 → C1 → C2 → C3 → Output
				//               ↑                        ↓
				//              D2 ←──────────────────────┘
				float smearFb = 0.0f;
				float fdnFb = feedback_;     // FDN feedback - scaled separately for Owl
				float cascadeFb = feedback_; // Cascade feedback - scaled up for Owl
				if (owlMode_) {
					// feedback_ ranges 0.32-0.44, normalize to 0-1 range for Room-based scaling
					float roomNorm = (feedback_ - 0.32f) / 0.12f; // 0 at min Room, 1 at max
					// FDN feedback scales less with Room (70% range) so high Room favors cascade
					// 0.32 + 70% of Room increase = 0.32 to 0.404 instead of 0.32 to 0.44
					fdnFb = 0.32f + (feedback_ - 0.32f) * 0.7f;
					// Cascade feedback scales MORE with Room - up to 0.56 instead of 0.44
					// 0.32 + 2.0x the Room increase = 0.32 to 0.56
					cascadeFb = 0.32f + (feedback_ - 0.32f) * 2.0f;
					smearFb = cascadeFb * 0.75f * owlFbEnvScale * skyLoopFb_; // Z3 controls density

					// Accumulate D0/D1/D2 reads for 4x AA
					owlD0ReadAccum_ += d0;
					owlD1ReadAccum_ += d1;

					// C0 input: D0 + C3 global feedback (Vast-like topology)
					// Higher feedback - builds fast, limiter catches it
					float globalFbBase = 0.9f + roomNorm * 0.4f; // 0.9 at min Room, 1.3 at max
					float globalFb = (globalFbBase + cascadeNestFeedback_) * owlFbEnvScale * skyLoopFb_;
					// Cheap HPF on feedback to tame LF rumble (~20Hz at 11kHz effective rate)
					c3ForFb = prevC3Out_ - dcBlockState_;
					dcBlockState_ += 0.011f * c3ForFb;
					cascadeIn = owlD0Cache_ + c3ForFb * globalFb;

					// D0 write: input - envelope limited (damping disabled for brightness)
					// Use squared scale so delays choke faster than cascade
					// fdnFb is less sensitive to Room than cascade feedback
					float effectiveFeedback = fdnFb * fdnFeedbackScale_ * 1.8f * owlDelayScale;
					float h0Sample = fdnIn * effectiveFeedback;
					owlD0WriteAccum_ += h0Sample;
					// D1 write will be set from C0 output below
					// h2 = C3 for nested feedback (accumulated and written at 4x rate)
				}

				// C0 with 4x undersample - fed by D0
				c0Accum_ += cascadeIn;
				if (c0Phase_ == 1) {
					float avgIn = c0Accum_ * 0.5f;
					c0Prev_ = processCascadeStage(0, avgIn);
					// Owl: smeared feedback C2→C0 and C3→C0 blurs delay repeats
					if (owlMode_) {
						float c2Smeared = processCascadeStage(0, c2Prev_ * smearFb);
						float c3Smeared = processCascadeStage(0, c3Prev_ * smearFb * 0.5f);
						c0Prev_ += c2Smeared + c3Smeared;
					}
					// Double write for 4x undersample
					if (cascadeWritePos_[0] == 0) {
						cascadeWritePos_[0] = cascadeLengths_[0] - 1;
					}
					else {
						--cascadeWritePos_[0];
					}
					processCascadeStage(0, avgIn);
					c0Accum_ = 0.0f;
				}
				c0Phase_ = (c0Phase_ + 1) & 1;
				c0 = c0Prev_;

				// Owl: C0 → D1 → C1 (D1 sits between C0 and C1)
				if (owlMode_) {
					// Accumulate C0 output for D1 write - envelope limited (damping disabled for brightness)
					// Use squared scale so delays choke faster than cascade
					// fdnFb is less sensitive to Room than cascade feedback
					float h1Sample = c0 * fdnFb * fdnFeedbackScale_ * 1.8f * owlDelayScale;
					owlD1WriteAccum_ += h1Sample;
				}

				// C1 with 4x undersample - fed by D1 (Owl) or C0 (Lush)
				float c1Input = owlMode_ ? owlD1Cache_ : c0;
				c1Accum_ += c1Input;
				if (c1Phase_ == 1) {
					float avgIn = c1Accum_ * 0.5f;
					c1Prev_ = processCascadeStage(1, avgIn);
					// Owl: smeared feedback C3→C1 blurs delay repeats
					if (owlMode_) {
						float c3Smeared = processCascadeStage(1, c3Prev_ * smearFb);
						c1Prev_ += c3Smeared;
					}
					if (cascadeWritePos_[1] == 0) {
						cascadeWritePos_[1] = cascadeLengths_[1] - 1;
					}
					else {
						--cascadeWritePos_[1];
					}
					processCascadeStage(1, avgIn);
					c1Accum_ = 0.0f;
				}
				c1Phase_ = (c1Phase_ + 1) & 1;
				c1 = c1Prev_;

				// C2 with 4x undersample + pitch modulation
				c2Accum_ += c1;
				if (c2Phase_ == 1) {
					float avgIn = c2Accum_ * 0.5f;
					float c2Coeff = cascadeCoeffs_[2];
					size_t origWritePos = cascadeWritePos_[2];
					size_t c2ModOffset = static_cast<size_t>(std::max(0.0f, lfoTri * cascadeModDepth_));
					size_t readPos = (origWritePos + c2ModOffset) % cascadeLengths_[2];
					size_t idx = cascadeOffsets_[2] + readPos;
					float delayed = buffer_[idx];
					float output = -c2Coeff * avgIn + delayed;
					float writeVal = avgIn + c2Coeff * output;
					buffer_[cascadeOffsets_[2] + origWritePos] = writeVal;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					buffer_[cascadeOffsets_[2] + cascadeWritePos_[2]] = writeVal;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					if (cascadeDoubleUndersample_) {
						size_t tapPos = (origWritePos + kMultiTapOffsets[2]) % cascadeLengths_[2];
						buffer_[cascadeOffsets_[2] + tapPos] += writeVal * kMultiTapGain;
					}
					c2Prev_ = output;
					c2Accum_ = 0.0f;
				}
				c2Phase_ = (c2Phase_ + 1) & 1;
				c2 = c2Prev_;

				// C3 with 4x undersample + inverted pitch modulation + parallel/series blend
				// Owl: D2 inline between C2→C3 (Vast-like topology)
				float c3In = owlMode_ ? owlD2Cache_ : (cascadeIn + (c2 - cascadeIn) * cascadeSeriesMix_);
				float c3;
				c3Accum_ += c3In;
				if (c3Phase_ == 1) {
					float avgIn = c3Accum_ * 0.5f;
					float c3Coeff = cascadeCoeffs_[3];
					size_t origWritePos = cascadeWritePos_[3];
					size_t c3ModOffset = static_cast<size_t>(std::max(0.0f, -lfoTri * cascadeModDepth_));
					size_t readPos = (origWritePos + c3ModOffset) % cascadeLengths_[3];
					size_t idx = cascadeOffsets_[3] + readPos;
					float delayed = buffer_[idx];
					float output = -c3Coeff * avgIn + delayed;
					float writeVal = avgIn + c3Coeff * output;
					buffer_[cascadeOffsets_[3] + origWritePos] = writeVal;
					if (++cascadeWritePos_[3] >= cascadeLengths_[3])
						cascadeWritePos_[3] = 0;
					buffer_[cascadeOffsets_[3] + cascadeWritePos_[3]] = writeVal;
					if (++cascadeWritePos_[3] >= cascadeLengths_[3])
						cascadeWritePos_[3] = 0;
					if (cascadeDoubleUndersample_) {
						size_t tapPos = (origWritePos + kMultiTapOffsets[3]) % cascadeLengths_[3];
						buffer_[cascadeOffsets_[3] + tapPos] += writeVal * kMultiTapGain;
					}
					// Owl: track only smeared feedback signals (pure recirculating tail)
					// Ignores fresh input passing through - only catches tail buildup
					if (owlMode_) {
						float outAbs = std::max(std::abs(c2Prev_), std::abs(c3Prev_));
						// Track squared signal - envelope accelerates during exponential growth
						// Delta grows quadratically with level, matching exponential buildup rate
						float outSq = outAbs * outAbs;
						// TUNING: predelay knob controls attack/release ratio
						// predelay_ 0-1 maps to ratio 0.25-4.0 (attack speed multiplier)
						float tuneRatio = 0.25f + predelay_ * 3.75f;
						float attackCoeff = 0.0000125f * tuneRatio;
						float releaseCoeff = 0.0002f; // Very fast release (~100ms)
						float coeff = (outSq > feedbackEnvelope_) ? attackCoeff : releaseCoeff;
						feedbackEnvelope_ += coeff * (outSq - feedbackEnvelope_);
					}
					c3Prev_ = output;
					c3Accum_ = 0.0f;
				}
				// Cache LFO value when cascade stages update to avoid discontinuities
				if (c3Phase_ == 0 && owlMode_) {
					vastLfoCache_ = lfoTri; // Update cache when c3 finishes updating
				}
				c3Phase_ = (c3Phase_ + 1) & 1;
				c3 = c3Prev_;
				prevC3Out_ = c3;

				// Non-Owl modes: track feedback envelope here (already clipped for Owl)
				if (!owlMode_) {
					float c3Abs = std::abs(c3);
					float coeff = (c3Abs > feedbackEnvelope_) ? 0.15f : 0.0003f; // Faster attack
					feedbackEnvelope_ += coeff * (c3Abs - feedbackEnvelope_);
				}

				// Amplitude modulation for diffusion contour
				// Owl: use cached LFO (synced to 4x update rate like Vast)
				float lfoForAmpMod = owlMode_ ? vastLfoCache_ : lfoTri;
				if (cascadeAmpMod_ > 0.0f || owlMode_) {
					float ampMod = owlMode_ ? (skyLfoAmp_ * skyLfoRouting_) : cascadeAmpMod_;
					c2 *= (1.0f + lfoForAmpMod * ampMod);
					c3 *= (1.0f - lfoForAmpMod * ampMod);
				}

				// Mix cascade outputs - stereo tail
				// Early stages (c0-c1) provide fast stereo, late stages (c2-c3) add width
				float cascadeMono = (c2 + c3) * 0.5f;
				float earlySide = (c0 - c1) * cascadeSideGain_ * dynamicWidth;
				float lateSide = (c2 - c3) * cascadeSideGain_ * 0.5f * dynamicWidth;
				float cascadeSide = earlySide + lateSide;

				// Owl: modulate mid/side filter cutoffs for stereo movement
				// LFO positive = brighter side (more stereo highs), darker mono
				// Z3 high routes more modulation here (same as amp mod routing)
				float monoCoeff = kCascadeLpCoeffMono;
				float sideCoeff = kCascadeLpCoeffSide;
				if (owlMode_) {
					float filterMod = vastLfoCache_ * skyLfoAmp_ * skyLfoRouting_ * 0.25f;
					monoCoeff = std::clamp(kCascadeLpCoeffMono - filterMod, 0.15f, 0.75f);
					sideCoeff = std::clamp(kCascadeLpCoeffSide + filterMod, 0.4f, 0.95f);
				}
				cascadeMono = onepole(cascadeMono, cascadeLpStateMono_, monoCoeff);
				cascadeSide = onepole(cascadeSide, cascadeLpStateSide_, sideCoeff);
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;

				// Owl: LFO output modulation for stereo movement + amplitude breathing (like Vast)
				// Z3 high routes LFO here (same as amp mod routing)
				if (owlMode_) {
					float lfoOut = vastLfoCache_ * skyLfoAmp_ * skyLfoRouting_;
					cascadeOutL *= (1.0f + lfoOut * 0.7f); // ±70% amplitude
					cascadeOutR *= (1.0f - lfoOut * 0.7f); // Opposite phase for stereo movement

					// D2 echo tap - distinct repeat before C3 diffuses it
					float echoTap = owlD2Cache_ * owlEchoGain_;
					cascadeOutL += echoTap;
					cascadeOutR += echoTap;
				}

				// Inject input + cascade feedback into FDN
				if (owlMode_) {
					// Owl: All FDN delays at 4x undersample with proper AA
					// D0/D1/D2 reads accumulated earlier, writes accumulated here
					owlD2ReadAccum_ += d2;
					// Use squared scale so delays choke faster than cascade
					// fdnFb is less sensitive to Room than cascade feedback
					float h2Sample = c2 * fdnFb * 0.95f * owlDelayScale; // C2→D2→C3 envelope limited
					owlD2WriteAccum_ += h2Sample;
					owlD0WriteAccum_ += fdnIn; // Unity input gain
					if (c3Phase_ == 0) {
						// c3 just updated - average all accumulated reads/writes
						owlD0Cache_ = owlD0ReadAccum_ * 0.5f;
						owlD1Cache_ = owlD1ReadAccum_ * 0.5f;
						owlD2Cache_ = owlD2ReadAccum_ * 0.5f;
						owlD0WriteVal_ = owlD0WriteAccum_ * 0.5f;
						owlD1WriteVal_ = owlD1WriteAccum_ * 0.5f;
						owlD2WriteVal_ = owlD2WriteAccum_ * 0.5f;
						owlD0ReadAccum_ = 0.0f;
						owlD1ReadAccum_ = 0.0f;
						owlD2ReadAccum_ = 0.0f;
						owlD0WriteAccum_ = 0.0f;
						owlD1WriteAccum_ = 0.0f;
						owlD2WriteAccum_ = 0.0f;
					}
					h0 = owlD0WriteVal_;
					h1 = owlD1WriteVal_;
					h2 = owlD2WriteVal_;
				}
				else if (kMuteCascadeFeedback) {
					h0 += fdnIn;
				}
				else {
					h0 += fdnIn + cascadeMono * tailFeedback * cascadeFeedbackMult_;
				}

				// Write FDN (double write for undersampling)
				fdnWrite(0, h0);
				fdnWrite(1, h1);
				fdnWrite(2, h2);
				fdnWrite(0, h0);
				fdnWrite(1, h1);
				fdnWrite(2, h2);

				// Early reflections from FDN
				if (!kMuteEarly) {
					// Owl: use cached D0/D1 (4x rate) for consistency
					float earlyD0 = owlMode_ ? owlD0Cache_ : d0;
					float earlyD1 = owlMode_ ? owlD1Cache_ : d1;
					float earlyMid = (earlyD0 + earlyD1) * earlyMixGain_;
					// Owl: wider early reflections (2-delay FDN is more stereo)
					float earlyWidthMult = owlMode_ ? 1.3f : 1.0f;
					float earlySide = (earlyD0 - earlyD1) * earlyMixGain_ * dynamicWidth * earlyWidthMult;
					directEarlyL_ = (earlyMid + earlySide) * directEarlyGain_;
					directEarlyR_ = (earlyMid - earlySide) * directEarlyGain_;
				}
				else {
					directEarlyL_ = 0.0f;
					directEarlyR_ = 0.0f;
				}
			}
			else {
				// === NORMAL FDN + CASCADE MODE ===
				c0 = processCascadeStage(0, cascadeIn);
				c1 = processCascadeStage(1, c0);
				c2 = processCascadeStage(2, c1);

				// C3 input: blend parallel (cascadeIn) ↔ series (c2)
				float c3In = cascadeIn + (c2 - cascadeIn) * cascadeSeriesMix_;
				float c3 = processCascadeStage(3, c3In);
				prevC3Out_ = c3;

				// Mix outputs - stereo tail from cascade (mid/side)
				float cascadeMono = (c2 + c3) * 0.5f;
				float cascadeSide = (c0 - c1) * cascadeSideGain_ * dynamicWidth;
				// Skip extra LPFs for normal mode (only used in Lush/Vast)
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;

				// Inject input + cascade feedback into FDN
				if (kMuteCascadeFeedback) {
					h0 += fdnIn;
				}
				else {
					h0 += fdnIn + cascadeMono * tailFeedback * cascadeFeedbackMult_;
				}

				// Write FDN (double write for undersampling)
				fdnWrite(0, h0);
				fdnWrite(1, h1);
				fdnWrite(2, h2);
				fdnWrite(0, h0);
				fdnWrite(1, h1);
				fdnWrite(2, h2);

				// Early reflections from FDN
				if (!kMuteEarly) {
					float earlyMid = (d0 + d1) * earlyMixGain_;
					float earlySide = (d0 - d1) * earlyMixGain_ * dynamicWidth;
					directEarlyL_ = (earlyMid + earlySide) * directEarlyGain_;
					directEarlyR_ = (earlyMid - earlySide) * directEarlyGain_;
				}
				else {
					directEarlyL_ = 0.0f;
					directEarlyR_ = 0.0f;
				}
			}

			// Output: early (FDN) + late (cascade)
			// Nested modes (Sky/Vast) have no early (FDN repurposed), normal modes have early
			float earlyL = 0.0f, earlyR = 0.0f;
			if (!vastChainMode_ && !skyChainMode_ && !kMuteEarly) {
				float earlyMid = (d0 + d1) * earlyMixGain_;
				float earlySide = (d0 - d1) * earlyMixGain_ * dynamicWidth;
				earlyL = earlyMid + earlySide;
				earlyR = earlyMid - earlySide;
			}

			float newOutL, newOutR;
			if (kMuteCascade) {
				newOutL = earlyL;
				newOutR = earlyR;
			}
			else {
				newOutL = earlyL + cascadeOutL * tailMixGain_;
				newOutR = earlyR + cascadeOutR * tailMixGain_;
			}

			// Global wet side boost from width knob (mid/side)
			// width=0: normal stereo, width=1: 2x side boost
			float wetMid = (newOutL + newOutR) * 0.5f;
			float wetSide = (newOutL - newOutR) * 0.5f * (1.0f + width_);
			newOutL = wetMid + wetSide;
			newOutR = wetMid - wetSide;

			prevOutL_ = currOutL_;
			prevOutR_ = currOutR_;
			currOutL_ = newOutL;
			currOutR_ = newOutR;

			outL = currOutL_;
			outR = currOutR_;
		}
		else {
			// Interpolate
			outL = (prevOutL_ + currOutL_) * 0.5f;
			outR = (prevOutR_ + currOutR_) * 0.5f;
		}

		undersamplePhase_ = !undersamplePhase_;

		prevOutputMono_ = (outL + outR) * 0.5f;

		// Output LPF
		outL = onepole(outL, lpStateL_, outLpCoeff);
		outR = onepole(outR, lpStateR_, outLpCoeff);

		// Add direct early brightness tap (bypasses LPF for crisp transients)
		outL += directEarlyL_;
		outR += directEarlyR_;

		// Subtract dry input to remove bleedthrough (sparse topology compensation)
		// Toggle via predelay encoder button: DRY- = enabled, DRY+ = disabled
		// inOrig is pre-gain, so subtract at unity
		if (cascadeOnly_) {
			outL -= inOrig;
			outR -= inOrig;
		}

		// Clamp and output
		constexpr float kMaxFloat = 0.06f;
		outL = std::clamp(outL, -kMaxFloat, kMaxFloat);
		outR = std::clamp(outR, -kMaxFloat, kMaxFloat);
		int32_t outLq31 = static_cast<int32_t>(outL * kOutputScale);
		int32_t outRq31 = static_cast<int32_t>(outR * kOutputScale);

		output[frame].l += multiply_32x32_rshift32_rounded(outLq31, getPanLeft());
		output[frame].r += multiply_32x32_rshift32_rounded(outRq31, getPanRight());
	}
}

void Featherverb::setRoomSize(float value) {
	roomSize_ = value;
	feedback_ = 0.32f + value * 0.12f; // 0.32 → 0.44
}

void Featherverb::setDamping(float value) {
	damping_ = value;
	dampCoeff_ = 0.1f + (1.0f - value) * 0.85f;
	// cascadeDamping_ is computed in updateSizes() with vast mode modifier
	updateSizes();
}

void Featherverb::setWidth(float value) {
	width_ = value;
}

void Featherverb::setHPF(float f) {
	hpCutoff_ = f;
}

void Featherverb::setLPF(float f) {
	lpCutoff_ = f;
}

void Featherverb::setPredelay(float value) {
	predelay_ = value;
	predelayLength_ = static_cast<size_t>(value * kPredelayMaxLength);
}

// === Zone 1: Matrix morphing ===
// Simplified 3x3 matrix with phi triangle modulation

static constexpr std::array<phi::PhiTriConfig, 9> kMatrix3TriBank = {{
    {phi::kPhi025, 0.7f, 0.000f, true},
    {phi::kPhi050, 0.7f, 0.111f, true},
    {phi::kPhi075, 0.7f, 0.222f, true},
    {phi::kPhi100, 0.7f, 0.333f, true},
    {phi::kPhi125, 0.7f, 0.444f, true},
    {phi::kPhi150, 0.7f, 0.555f, true},
    {phi::kPhi175, 0.7f, 0.666f, true},
    {phi::kPhi200, 0.7f, 0.777f, true},
    {phi::kPhi033, 0.7f, 0.888f, true},
}};

void Featherverb::setZone1(int32_t value) {
	zone1_ = value;
	updateMatrix();
}

// Simple Gram-Schmidt for 3x3
static bool gramSchmidt3x3(std::array<std::array<float, 3>, 3>& m) {
	for (int col = 0; col < 3; ++col) {
		for (int prev = 0; prev < col; ++prev) {
			float dot = 0.0f;
			for (int row = 0; row < 3; ++row) {
				dot += m[row][col] * m[row][prev];
			}
			for (int row = 0; row < 3; ++row) {
				m[row][col] -= dot * m[row][prev];
			}
		}
		float norm = 0.0f;
		for (int row = 0; row < 3; ++row) {
			norm += m[row][col] * m[row][col];
		}
		norm = std::sqrt(norm);
		if (norm < 0.0001f)
			return false;
		for (int row = 0; row < 3; ++row) {
			m[row][col] /= norm;
		}
	}
	return true;
}

void Featherverb::updateMatrix() {
	using namespace phi;

	const float yNorm = static_cast<float>(zone1_) / 1023.0f;
	const int32_t zone = zone1_ >> 7;
	const double gammaPhase = zone * 0.125;

	// Vast mode: offset yNorm slightly to avoid phi triangle null at Z1 max
	float yNormAdj = (zone == 7) ? std::min(yNorm, 0.97f) : yNorm;
	const PhiTriContext ctx{yNormAdj, 1.0f, 1.0f, gammaPhase};
	std::array<float, 9> vals = ctx.evalBank(kMatrix3TriBank);

	// Base 3x3 Hadamard-like matrix
	static constexpr std::array<std::array<float, 3>, 3> kH3Base = {
	    {{1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, -1.0f}}};

	static constexpr std::array<float, 8> kZoneBlend = {0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.85f, 0.95f};
	const float blend = kZoneBlend[zone];

	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			float base = kH3Base[row][col];
			float mod = vals[row * 3 + col];
			matrix_[row][col] = base + blend * mod * 0.5f;
		}
	}

	if (!gramSchmidt3x3(matrix_)) {
		// Fallback
		for (int row = 0; row < 3; ++row) {
			for (int col = 0; col < 3; ++col) {
				matrix_[row][col] = kH3Base[row][col] * kH3Norm;
			}
		}
	}

	// Pitch wobble depth: Lush/Vast use blend, Sky uses dedicated control
	if (cascadeDoubleUndersample_) {
		modDepth_ = blend * 25.0f;
	}
	else {
		modDepth_ = 0.0f;
	}

	// Update D0/D1 lengths from phi triangles
	float d0Tri = (vals[0] + 1.0f) * 0.5f;
	float d1Tri = (vals[3] + 1.0f) * 0.5f;

	fdnLengths_[0] = kD0MinLength + static_cast<size_t>(d0Tri * (kD0MaxLength - kD0MinLength));
	fdnLengths_[1] = kD1MinLength + static_cast<size_t>(d1Tri * (kD1MaxLength - kD1MinLength));

	// Precompute delay ratio for feedback normalization (avoid division in hot path)
	delayRatio_ = static_cast<float>(fdnLengths_[0] + fdnLengths_[1]) / static_cast<float>(kD0MaxLength + kD1MaxLength);

	// Sky/Vast/Owl mode: Z1 controls various parameters using phi triangles
	if (skyChainMode_ || vastChainMode_ || owlMode_) {
		// Balance between C2→C0 and C3→C1 feedback paths
		float balanceBase = yNorm;                           // 0 = favor C2→C0, 1 = favor C3→C1
		float balanceMod = (vals[1] + 1.0f) * 0.15f - 0.15f; // ±0.15 texture
		skyFbBalance_ = std::clamp(balanceBase + balanceMod, 0.0f, 1.0f);

		// LFO amplitude and frequency from phi triangles
		float ampTri = (vals[2] + 1.0f) * 0.5f;   // 0 to 1
		float freqTri = (vals[4] + 1.0f) * 0.5f;  // 0 to 1
		float pitchTri = (vals[5] + 1.0f) * 0.5f; // 0 to 1
		skyLfoAmp_ = 0.15f + ampTri * 0.85f;      // 0.15 → 1.0
		skyLfoFreq_ = 0.5f + freqTri * 2.0f;      // 0.5x → 2.5x
		// Owl: lower pitch modulation to avoid artifacts at 4x undersample
		float maxPitch = owlMode_ ? 80.0f : 160.0f;
		modDepth_ = 20.0f + pitchTri * maxPitch;
	}

	if (fdnWritePos_[0] >= fdnLengths_[0])
		fdnWritePos_[0] = 0;
	if (fdnWritePos_[1] >= fdnLengths_[1])
		fdnWritePos_[1] = 0;
}

// === Zone 2: Size (D2 + cascade scaling) ===

void Featherverb::setZone2(int32_t value) {
	zone2_ = value;
	updateSizes();
	updateFeedbackPattern(); // Recalc cascade coefficients (depend on cascadeDoubleUndersample_)
}

void Featherverb::updateSizes() {
	const float t = static_cast<float>(zone2_) / 1023.0f;
	const int32_t zone = zone2_ >> 7; // Zone ID 0-7

	// D2 scales from min to max
	fdnLengths_[2] = kD2MinLength + static_cast<size_t>(t * (kD2MaxLength - kD2MinLength));
	if (fdnWritePos_[2] >= fdnLengths_[2])
		fdnWritePos_[2] = 0;

	// Cascade scales uniformly from 1x to 1.5x
	cascadeScale_ = 1.0f + t * (kCascadeMaxScale - 1.0f);

	// Early/tail balance: inverse relationship for room character
	// Tiny rooms: punchy early reflections, minimal tail (0.4 early, 0.25 tail)
	// Vast rooms: spacious tails, subtle early (0.15 early, 1.3 tail)
	earlyMixGain_ = 0.4f - t * 0.25f;   // 0.4 → 0.15
	tailMixGain_ = 0.25f + t * 1.05f;   // 0.25 → 1.3
	directEarlyGain_ = 0.2f - t * 0.1f; // 0.2 → 0.1 (more direct brightness at small, less at vast)

	// Zone layout (compressed small rooms to make room for Feather):
	// Zones 0-3: Compressed small rooms (Smol, Chamber, Hall, Church)
	// Zone 4: Feather - experimental mode placeholder
	// Zone 5: Sky - nested topology at 2x undersample (responsive, extended)
	// Zone 6: Lush - FDN + Cascade with 4x undersample
	// Zone 7: Vast - nested topology at 4x undersample (maximum tail length)
	bool prevFeatherMode = featherMode_;
	bool prevOwlMode = owlMode_;
	bool prevSkyMode = skyChainMode_;
	bool prevVastMode = vastChainMode_;

	cascadeDoubleUndersample_ = !kDisableVastUndersample && (zone >= 6);
	featherMode_ = (zone == 4);   // Feather: dual parallel cascades at 2x
	skyChainMode_ = (zone == 5);  // Sky: nested at 2x
	owlMode_ = (zone == 6);       // Owl: nested with smeared feedback at 4x
	vastChainMode_ = (zone == 7); // Vast: nested at 4x

	// Reset shared filter state when mode changes to prevent stereo imbalance
	// cascadeLpStateMono_/Side_ are reused differently across modes:
	// - Feather: cascadeLpStateMono_ = R channel filter
	// - Owl/Vast: cascadeLpStateMono_ = mono component in M/S processing
	bool modeChanged = (featherMode_ != prevFeatherMode) || (owlMode_ != prevOwlMode) || (skyChainMode_ != prevSkyMode)
	                   || (vastChainMode_ != prevVastMode);
	if (modeChanged) {
		cascadeLpStateMono_ = 0.0f;
		cascadeLpStateSide_ = 0.0f;
		feedbackEnvelope_ = 0.0f;
		// Reset cascade phase counters to avoid partial updates
		c0Phase_ = 0;
		c1Phase_ = 0;
		c2Phase_ = 0;
		c3Phase_ = 0;
		c0Accum_ = 0.0f;
		c1Accum_ = 0.0f;
		c2Accum_ = 0.0f;
		c3Accum_ = 0.0f;
	}

	// Mode-specific enhancements
	// Recompute cascade damping from base damping_ value to avoid compounding
	float baseCascadeDamping = 0.05f + (1.0f - damping_) * 0.6f;
	if (zone == 7) {
		// Vast: Nested topology with 4x undersample, self-limiting feedback
		cascadeDamping_ = baseCascadeDamping * 0.5f;
		cascadeModDepth_ = 14.0f;
		cascadeAmpMod_ = 0.25f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.25f, 0.0f, 0.52f); // Slightly more fb headroom
		cascadeSideGain_ = 0.23f; // Slightly wider for spacious modes
	}
	else if (zone == 6) {
		// Owl: FDN + Cascade with 4x undersample, wider stereo tail
		cascadeDamping_ = baseCascadeDamping * 0.6f;
		cascadeModDepth_ = 0.0f; // Disabled - pitch mod causes clicks at 4x undersample
		cascadeAmpMod_ = 0.15f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.25f, 0.0f, 0.55f);
		cascadeSideGain_ = 0.25f; // Wider stereo spread for Owl
	}
	else if (zone == 5) {
		// Sky: Nested topology at 2x undersample (faster response than Vast)
		cascadeDamping_ = baseCascadeDamping * 0.65f;
		cascadeModDepth_ = 8.0f;
		cascadeAmpMod_ = 0.12f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.15f, 0.0f, 0.40f);
		cascadeSideGain_ = 0.23f; // Slightly wider for spacious modes
		modDepth_ = 100.0f;       // D0/D1 pitch wobble default (~4.5ms), Z1 modulates 20-180
	}
	else if (zone == 4) {
		// Feather: Experimental mode - start with normal FDN+cascade, tweak from here
		cascadeDamping_ = baseCascadeDamping * 0.7f;
		cascadeModDepth_ = 4.0f;
		cascadeAmpMod_ = 0.08f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.1f, 0.0f, 0.35f);
		cascadeSideGain_ = 0.2f;
	}
	else {
		cascadeDamping_ = baseCascadeDamping;
		cascadeModDepth_ = 0.0f;
		cascadeAmpMod_ = 0.0f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_, 0.0f, 0.55f);
		cascadeSideGain_ = 0.2f; // Default stereo side gain
	}

	cascadeLengths_[0] = static_cast<size_t>(kC0BaseLength * cascadeScale_);
	cascadeLengths_[1] = static_cast<size_t>(kC1BaseLength * cascadeScale_);
	cascadeLengths_[2] = static_cast<size_t>(kC2BaseLength * cascadeScale_);
	cascadeLengths_[3] = static_cast<size_t>(kC3BaseLength * cascadeScale_);

	// Clamp write positions
	for (size_t i = 0; i < kNumCascade; ++i) {
		if (cascadeWritePos_[i] >= cascadeLengths_[i]) {
			cascadeWritePos_[i] = 0;
		}
	}
}

// === Zone 3: Feedback pattern ===

static constexpr std::array<phi::PhiTriConfig, 3> kFeedback3TriBank = {{
    {phi::kPhi033, 0.6f, 0.00f, true},
    {phi::kPhi067, 0.6f, 0.33f, true},
    {phi::kPhi100, 0.6f, 0.66f, true},
}};

void Featherverb::setZone3(int32_t value) {
	zone3_ = value;
	updateFeedbackPattern();
}

void Featherverb::updateFeedbackPattern() {
	using namespace phi;

	const float yNorm = static_cast<float>(zone3_) / 1023.0f;
	const int32_t zone = zone3_ >> 7;
	const double gammaPhase = zone * 0.125;

	static constexpr std::array<std::array<float, 3>, 8> kZoneBias = {{{1.00f, 1.00f, 1.00f},
	                                                                   {1.05f, 1.00f, 0.95f},
	                                                                   {0.95f, 1.00f, 1.05f},
	                                                                   {1.02f, 0.96f, 1.02f},
	                                                                   {0.92f, 1.08f, 0.92f},
	                                                                   {1.06f, 0.94f, 1.00f},
	                                                                   {0.90f, 1.00f, 1.10f},
	                                                                   {1.08f, 1.00f, 0.92f}}};

	const PhiTriContext ctx{yNorm, 1.0f, 1.0f, gammaPhase};
	std::array<float, 3> mods = ctx.evalBank(kFeedback3TriBank);

	for (size_t i = 0; i < 3; ++i) {
		feedbackMult_[i] = std::clamp(kZoneBias[zone][i] + mods[i] * 0.075f, 0.85f, 1.15f);
	}

	// Cascade series mix - 10 periods for fine density control
	// seriesMix=0 → C3 parallel (9 paths, sparse), seriesMix=1 → C3 series (16 paths, dense)
	float tri10 = dsp::triangleSimpleUnipolar(yNorm * 10.0f) * 2.0f - 1.0f; // Bipolar -1..1
	cascadeSeriesMix_ = 0.6f + tri10 * 0.25f;                               // Map to 0.35..0.85

	// Cascade feedback - 7 periods, clockwise = more feedback
	float tri7 = dsp::triangleSimpleUnipolar(yNorm * 7.0f) * 2.0f - 1.0f;
	float baseFeedback = 0.03f + yNorm * 0.12f;
	cascadeFeedbackMult_ = std::clamp(baseFeedback + tri7 * 0.02f, 0.02f, 0.2f);

	// Nested cascade feedback (C3→C0) - 5 periods
	float tri5 = dsp::triangleSimpleUnipolar(yNorm * 5.0f) * 2.0f - 1.0f;
	float baseNest = std::max(0.0f, (yNorm - 0.35f) * 0.5f);
	cascadeNestFeedbackBase_ = std::clamp(baseNest + tri5 * 0.08f, 0.0f, 0.4f);
	// Recompute combined value (Zone 2 adds vast boost)
	updateSizes();

	// LFO pitch wobble depth - 13 periods (fast), adds subtle chorus/shimmer
	if (skyChainMode_ || vastChainMode_ || owlMode_) {
		// Sky/Vast/Owl: Z3 controls pitch wobble (lower max at 4x to avoid artifacts)
		float tri13 = dsp::triangleSimpleUnipolar(yNorm * 13.0f) * 2.0f - 1.0f;
		float maxWobble = (vastChainMode_ || owlMode_) ? 80.0f : 250.0f;
		float wobbleTexture = (vastChainMode_ || owlMode_) ? 15.0f : 50.0f;
		modDepth_ = std::max(0.0f, yNorm * maxWobble + tri13 * wobbleTexture);

		// Z3 also controls local loop feedback (smeared path density)
		skyLoopFb_ = std::max(0.4f, 0.4f + yNorm * 0.9f + tri13 * 0.2f);

		// Z3 controls LFO routing - 7 periods for variety
		float tri7Route = dsp::triangleSimpleUnipolar(yNorm * 7.0f) * 2.0f - 1.0f;
		skyLfoRouting_ = std::clamp(yNorm + tri7Route * 0.15f, 0.0f, 1.0f);

		// Owl mode: D2 echo tap - 10 periods, 50% duty cycle
		owlEchoGain_ = owlMode_ ? yNorm * 0.5f * dsp::triangleSimpleUnipolar(yNorm * 10.0f, 0.5f) : 0.0f;

		// Owl mode: envelope attack/release ratio - 5 periods for slow evolution
		// Ratio modulates how fast attack is relative to release
		// Low ratio (0.5): slower attack, quicker release - longer swell, faster decay
		// High ratio (2.0): faster attack, slower release - catches peaks, holds longer
		if (owlMode_) {
			float tri5 = dsp::triangleSimpleUnipolar(yNorm * 5.0f); // [0,1]
			owlEnvRatio_ = 0.5f + tri5 * 1.5f;                      // Range: 0.5 to 2.0
		}
	}
	else if (cascadeDoubleUndersample_) {
		float tri13 = dsp::triangleSimpleUnipolar(yNorm * 13.0f) * 2.0f - 1.0f;
		modDepth_ = std::clamp(0.3f + tri13 * 0.25f, 0.0f, 0.6f);
	}
	else {
		modDepth_ = 0.0f;
	}

	// Width breathing - 11 periods (fast), expands stereo as signal decays
	float tri11 = dsp::triangleSimpleUnipolar(yNorm * 11.0f) * 2.0f - 1.0f;
	widthBreath_ = std::clamp(0.5f + tri11 * 0.4f, 0.0f, 1.2f);

	// Cross-channel bleed - 9 periods, L↔R mixing in FDN feedback
	float tri9 = dsp::triangleSimpleUnipolar(yNorm * 9.0f) * 2.0f - 1.0f;
	float baseBleed = yNorm * 0.15f;
	crossBleed_ = std::clamp(baseBleed + tri9 * 0.1f, 0.0f, 0.25f);

	// FDN feedback scale - reduce FDN feedback as Zone 3 increases (inverse relationship)
	// At low Zone 3: full FDN feedback (1.0), at high Zone 3: reduced (0.7)
	fdnFeedbackScale_ = 1.0f - yNorm * 0.3f;

	// Per-stage cascade allpass coefficients - higher coeffs = more diffusion (less slapback)
	// Each stage uses different triangle period for variety
	// Extended modes (Feather/Sky/Lush/Vast) need higher coefficients for proper diffusion
	bool extendedMode = cascadeDoubleUndersample_ || skyChainMode_ || featherMode_;
	float coeffBase = extendedMode ? (0.5f + yNorm * 0.12f)  // Extended modes: 0.5-0.62
	                               : (0.32f + yNorm * 0.2f); // Normal: 0.32-0.52

	// C0: 8 periods - fastest stage, moderate variation
	float tri8 = dsp::triangleSimpleUnipolar(yNorm * 8.0f) * 2.0f - 1.0f;
	float c0Min = extendedMode ? 0.45f : 0.28f;
	cascadeCoeffs_[0] = std::clamp(coeffBase + tri8 * 0.08f, c0Min, 0.75f);

	// C1: 6 periods - slightly more variation
	float tri6 = dsp::triangleSimpleUnipolar(yNorm * 6.0f) * 2.0f - 1.0f;
	float c1Min = extendedMode ? 0.45f : 0.28f;
	cascadeCoeffs_[1] = std::clamp(coeffBase + tri6 * 0.1f, c1Min, 0.75f);

	// C2: 4 periods - slower variation, slightly higher base for density
	float tri4 = dsp::triangleSimpleUnipolar(yNorm * 4.0f) * 2.0f - 1.0f;
	float c2Min = extendedMode ? 0.48f : 0.30f;
	cascadeCoeffs_[2] = std::clamp(coeffBase + 0.05f + tri4 * 0.1f, c2Min, 0.78f);

	// C3: 3 periods - longest stage, highest base for maximum diffusion
	float tri3 = dsp::triangleSimpleUnipolar(yNorm * 3.0f) * 2.0f - 1.0f;
	float c3Min = extendedMode ? 0.50f : 0.32f;
	cascadeCoeffs_[3] = std::clamp(coeffBase + 0.08f + tri3 * 0.12f, c3Min, 0.82f);
}

} // namespace deluge::dsp::reverb
