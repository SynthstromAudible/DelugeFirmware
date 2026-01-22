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
#include "util/fixedpoint.h"

namespace deluge::dsp::reverb {

using namespace deluge::dsp;

// Compile-time diagnostic toggles (for permanent debugging builds)
// Runtime toggle cascadeOnly_ is preferred - controlled via predelay encoder button
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
	offset += kPredelayMaxLength;

	// Diffusers
	diffuserOffsets_[0] = offset;
	offset += kDiffuser0Length;
	diffuserOffsets_[1] = offset;

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
	prevC3Out_ = 0.0f;
	diffuserWritePos_.fill(0);
	predelayWritePos_ = 0;
	dcBlockState_ = 0.0f;
	inputEnvelope_ = 0.0f;
	hpState_ = 0.0f;
	lpStateL_ = 0.0f;
	lpStateR_ = 0.0f;
	lfoPhase_ = 0.0f;
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
	const float tailFeedback = feedback_ * feedback_;                           // Hoist: tail decays faster than early
	const float envReleaseRate = 0.0001f + (1023 - zone2_) * 0.0002f / 1023.0f; // Hoist: envelope release

	// Cache matrix
	const auto& m = matrix_;

	for (size_t frame = 0; frame < input.size(); ++frame) {
		// === Full-rate: HPF, envelope, predelay ===
		float in = static_cast<float>(input[frame]) * kInputScale;
		float hpOut = in - hpState_;
		hpState_ += (1.0f - hpCoeff) * hpOut;
		in = hpOut;

		// Input envelope for auto-decay
		float inAbs = std::fabs(in);
		if (inAbs > inputEnvelope_) {
			inputEnvelope_ = inAbs;
		}
		else {
			inputEnvelope_ += envReleaseRate * (inAbs - inputEnvelope_);
		}

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
			if (cascadeDoubleUndersample_ || modDepth_ > 0.0f) {
				lfoPhase_ += 0.0000034f;
				if (lfoPhase_ >= 1.0f)
					lfoPhase_ -= 1.0f;
				lfoTri = lfoPhase_ < 0.5f ? (4.0f * lfoPhase_ - 1.0f) : (3.0f - 4.0f * lfoPhase_);
				d0Mod = static_cast<size_t>(std::max(0.0f, lfoTri * modDepth_));
				d1Mod = static_cast<size_t>(std::max(0.0f, -lfoTri * modDepth_));
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

			// Feedback with auto-decay
			float effectiveFeedback = feedback_;
			constexpr float kEnvReference = 0.001f;
			constexpr float kMinFeedbackMult = 0.6f;
			float feedbackFloor = kMinFeedbackMult + (zone2_ * (1.0f - kMinFeedbackMult)) / 1023.0f;
			float envNorm = std::min(inputEnvelope_ / kEnvReference, 1.0f);
			float feedbackMod = feedbackFloor + envNorm * (1.0f - feedbackFloor);
			effectiveFeedback *= feedbackMod * fdnFeedbackScale_; // Scale inversely with Zone 3

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
			if (kBypassFdnToCascade || cascadeOnly_) {
				cascadeIn = fdnIn * 1.4f + prevC3Out_ * cascadeNestFeedback_ * tailFeedback;
			}
			else {
				// 50% dry + 50% FDN to cascade
				cascadeIn = fdnIn * 0.7f + (d0 + d1 + d2) * 0.7f + prevC3Out_ * cascadeNestFeedback_ * tailFeedback;
			}

			// c0→c1→c2 series chain with optional 4x undersample for vast rooms
			float c0, c1, c2;
			float cascadeOutL, cascadeOutR;
			float dynamicWidth = width_ + (1.0f - std::min(inputEnvelope_ * 100.0f, 1.0f)) * widthBreath_;

			if (vastChainMode_) {
				// === VAST CHAIN MODE ===
				// Topology with nested feedback loops + global recirculation:
				//   Input ←───────────────────────────────────┐
				//       → C0 → D0 ←──┐                        │
				//                └→ C1 → D1 ←──┐              │
				//                          └→ C2 → D2 ←──┐   │
				//                                    └→ C3 ──┘
				// Local loops add density, global loop adds tail length

				// Feedback coefficients: Room → density, Zone 3 → tail length
				// Self-limiting feedback: reduce when reverb level is high (can go to zero)
				float fbEnvScale = 1.0f - std::min(feedbackEnvelope_ * 8.0f, 1.0f);
				float loopFb = feedback_ * 0.4f * delayRatio_ * fbEnvScale;
				float globalFb = cascadeNestFeedback_ * fbEnvScale * 0.8f;

				// Pre-decimation AA filter with global C3 feedback
				float chainIn = onepole(fdnIn * 1.4f + prevC3Out_ * globalFb, cascadeAaState1_, kPreCascadeAaCoeff);

				// C0 with 4x undersample (input → first allpass)
				c0Accum_ += chainIn;
				if (c0Phase_ == 1) {
					float avgIn = c0Accum_ * 0.5f;
					c0Prev_ = processCascadeStage(0, avgIn);
					c0Accum_ = 0.0f;
				}
				c0Phase_ = (c0Phase_ + 1) & 1;
				c0 = c0Prev_;

				// D0 delay between C0 and C1
				fdnWrite(0, c0);
				fdnWrite(0, c0); // Double write for 4x undersample
				float d0Out = fdnRead(0);

				// C1 with 4x undersample
				c1Accum_ += d0Out;
				if (c1Phase_ == 1) {
					float avgIn = c1Accum_ * 0.5f;
					c1Prev_ = processCascadeStage(1, avgIn);
					c1Accum_ = 0.0f;
				}
				c1Phase_ = (c1Phase_ + 1) & 1;
				c1 = c1Prev_;

				// C1 → D0 nested feedback loop (single write - feedback timing less critical)
				fdnWrite(0, c1 * loopFb);

				// D1 delay between C1 and C2
				fdnWrite(1, c1);
				fdnWrite(1, c1);
				float d1Out = fdnRead(1);

				// C2 with 4x undersample + pitch modulation
				c2Accum_ += d1Out;
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

				// C2 → D1 nested feedback loop
				fdnWrite(1, c2 * loopFb);

				// D2 delay between C2 and C3
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
					c3Prev_ = output;
					c3Accum_ = 0.0f;
				}
				c3Phase_ = (c3Phase_ + 1) & 1;
				c3 = c3Prev_;

				// Soft clip C3 to prevent runaway (tanh-like)
				constexpr float kC3Limit = 0.15f;
				if (std::abs(c3) > kC3Limit) {
					c3 = std::copysign(kC3Limit + (std::abs(c3) - kC3Limit) * 0.2f, c3);
				}
				prevC3Out_ = c3;

				// Track feedback envelope for self-limiting (fast attack, slow release)
				float c3Abs = std::abs(c3);
				float coeff = (c3Abs > feedbackEnvelope_) ? 0.05f : 0.0003f;
				feedbackEnvelope_ += coeff * (c3Abs - feedbackEnvelope_);

				// C3 → D2 nested feedback loop (scaled by self-limiting)
				fdnWrite(2, c3 * loopFb);

				// Amplitude modulation for diffusion contour
				if (cascadeAmpMod_ > 0.0f) {
					c2 *= (1.0f + lfoTri * cascadeAmpMod_);
					c3 *= (1.0f - lfoTri * cascadeAmpMod_);
				}

				// Mix chain outputs - stereo from intermediate stages
				float cascadeMono = (c2 + c3) * 0.5f;
				float cascadeSide = (c0 - c1) * 0.2f * dynamicWidth;
				cascadeMono = onepole(cascadeMono, cascadeLpStateMono_, kCascadeLpCoeffMono);
				cascadeSide = onepole(cascadeSide, cascadeLpStateSide_, kCascadeLpCoeffSide);
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;

				// No early reflections in vast chain mode (FDN is repurposed)
				directEarlyL_ = 0.0f;
				directEarlyR_ = 0.0f;
			}
			else if (cascadeDoubleUndersample_) {
				// === LUSH MODE ===
				// 4x undersample on cascade stages, but keeps FDN + cascade separate
				// FDN provides early reflections, cascade provides diffuse tail

				// Pre-decimation AA filter
				cascadeIn = onepole(cascadeIn, cascadeAaState1_, kPreCascadeAaCoeff);

				// C0 with 4x undersample
				c0Accum_ += cascadeIn;
				if (c0Phase_ == 1) {
					float avgIn = c0Accum_ * 0.5f;
					c0Prev_ = processCascadeStage(0, avgIn);
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

				// C1 with 4x undersample
				c1Accum_ += c0;
				if (c1Phase_ == 1) {
					float avgIn = c1Accum_ * 0.5f;
					c1Prev_ = processCascadeStage(1, avgIn);
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
				float c3In = cascadeIn + (c2 - cascadeIn) * cascadeSeriesMix_;
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
					c3Prev_ = output;
					c3Accum_ = 0.0f;
				}
				c3Phase_ = (c3Phase_ + 1) & 1;
				c3 = c3Prev_;
				prevC3Out_ = c3;

				// Amplitude modulation for diffusion contour
				if (cascadeAmpMod_ > 0.0f) {
					c2 *= (1.0f + lfoTri * cascadeAmpMod_);
					c3 *= (1.0f - lfoTri * cascadeAmpMod_);
				}

				// Mix cascade outputs - stereo tail
				float cascadeMono = (c2 + c3) * 0.5f;
				float cascadeSide = (c0 - c1) * 0.2f * dynamicWidth;
				cascadeMono = onepole(cascadeMono, cascadeLpStateMono_, kCascadeLpCoeffMono);
				cascadeSide = onepole(cascadeSide, cascadeLpStateSide_, kCascadeLpCoeffSide);
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;

				// Inject input + cascade feedback into FDN (same as normal mode)
				if (kMuteCascadeFeedback || cascadeOnly_) {
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

				// Early reflections from FDN (Lush keeps early reflections)
				if (!kMuteEarly && !cascadeOnly_) {
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
				float cascadeSide = (c0 - c1) * 0.2f * dynamicWidth;
				cascadeMono = onepole(cascadeMono, cascadeLpStateMono_, kCascadeLpCoeffMono);
				cascadeSide = onepole(cascadeSide, cascadeLpStateSide_, kCascadeLpCoeffSide);
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;

				// Inject input + cascade feedback into FDN
				if (kMuteCascadeFeedback || cascadeOnly_) {
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
				if (!kMuteEarly && !cascadeOnly_) {
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
			// Vast chain mode has no early (FDN repurposed), Lush and normal modes have early
			float earlyL = 0.0f, earlyR = 0.0f;
			if (!vastChainMode_ && !kMuteEarly && !cascadeOnly_) {
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

	const PhiTriContext ctx{yNorm, 1.0f, 1.0f, gammaPhase};
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

	modDepth_ = cascadeDoubleUndersample_ ? (blend * 25.0f) : 0.0f;

	// Update D0/D1 lengths from phi triangles
	float d0Tri = (vals[0] + 1.0f) * 0.5f;
	float d1Tri = (vals[3] + 1.0f) * 0.5f;

	// In Vast mode, bias toward monotonic growth (70% yNorm, 30% phi variation)
	if (vastChainMode_) {
		d0Tri = yNorm * 0.7f + d0Tri * 0.3f;
		d1Tri = yNorm * 0.7f + d1Tri * 0.3f;
	}

	fdnLengths_[0] = kD0MinLength + static_cast<size_t>(d0Tri * (kD0MaxLength - kD0MinLength));
	fdnLengths_[1] = kD1MinLength + static_cast<size_t>(d1Tri * (kD1MaxLength - kD1MinLength));

	// Precompute delay ratio for feedback normalization (avoid division in hot path)
	delayRatio_ = static_cast<float>(fdnLengths_[0] + fdnLengths_[1]) / static_cast<float>(kD0MaxLength + kD1MaxLength);

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

	// 4x undersample for Lush (zone 6) and Vast (zone 7) for extended tails
	// Lush: FDN + Cascade separation with 4x undersample (old vast behavior)
	// Vast: Chain topology with nested feedback loops (new chain mode)
	cascadeDoubleUndersample_ = !kDisableVastUndersample && (zone >= 6);
	vastChainMode_ = (zone == 7); // Only Vast uses chain topology

	// Lush/Vast mode enhancements
	// Recompute cascade damping from base damping_ value to avoid compounding
	float baseCascadeDamping = 0.05f + (1.0f - damping_) * 0.6f;
	if (zone == 7) {
		// Vast: Chain topology with self-limiting feedback
		cascadeDamping_ = baseCascadeDamping * 0.5f;
		cascadeModDepth_ = 14.0f;
		cascadeAmpMod_ = 0.25f;
		// Lower max feedback than before - self-limiting handles the rest
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.25f, 0.0f, 0.45f);
	}
	else if (zone == 6) {
		// Lush: FDN + Cascade with 4x undersample, lusher shimmer
		cascadeDamping_ = baseCascadeDamping * 0.6f; // Slightly less soft than Vast
		cascadeModDepth_ = 10.0f;                    // Less pitch wobble than Vast
		cascadeAmpMod_ = 0.15f;                      // Subtler amplitude modulation
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.25f, 0.0f, 0.55f);
	}
	else {
		cascadeDamping_ = baseCascadeDamping;
		cascadeModDepth_ = 0.0f;
		cascadeAmpMod_ = 0.0f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_, 0.0f, 0.55f);
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
	// Triangle wave: 10 complete cycles over yNorm 0→1
	// seriesMix=0 → C3 parallel (9 paths, sparse), seriesMix=1 → C3 series (16 paths, dense)
	float phase10 = yNorm * 10.0f;
	float tri10 = 1.0f - 4.0f * std::abs(phase10 - std::floor(phase10) - 0.5f);
	// Map -1..1 to 0.35..0.85 range (biased toward series/dense)
	cascadeSeriesMix_ = 0.6f + tri10 * 0.25f;

	// Cascade feedback - 7 periods, clockwise = more feedback
	// Base increases with yNorm (0.4 to 0.9), triangle adds ±0.1 texture
	float phase7 = yNorm * 7.0f;
	float tri7 = 1.0f - 4.0f * std::abs(phase7 - std::floor(phase7) - 0.5f);
	float baseFeedback = 0.03f + yNorm * 0.12f; // 0.03 to 0.15 as yNorm increases (tinier)
	cascadeFeedbackMult_ = std::clamp(baseFeedback + tri7 * 0.02f, 0.02f, 0.2f);

	// Nested cascade feedback (C3→C0) - 5 periods, clockwise = more recirculation
	// Kicks in at higher yNorm values; scaled by room feedback in processing
	// Sets base value; Zone 2 adds vast boost on top
	float phase5 = yNorm * 5.0f;
	float tri5 = 1.0f - 4.0f * std::abs(phase5 - std::floor(phase5) - 0.5f);
	float baseNest = std::max(0.0f, (yNorm - 0.35f) * 0.5f); // 0 until yNorm>0.35, then 0 to 0.325
	cascadeNestFeedbackBase_ = std::clamp(baseNest + tri5 * 0.08f, 0.0f, 0.4f);
	// Recompute combined value (Zone 2 adds vast boost)
	updateSizes();

	// LFO pitch wobble depth - 13 periods (fast), adds subtle chorus/shimmer
	// Only enabled for Lush/Vast modes (cascadeDoubleUndersample_)
	if (cascadeDoubleUndersample_) {
		float phase13 = yNorm * 13.0f;
		float tri13 = 1.0f - 4.0f * std::abs(phase13 - std::floor(phase13) - 0.5f);
		modDepth_ = std::clamp(0.3f + tri13 * 0.25f, 0.0f, 0.6f);
	}
	else {
		modDepth_ = 0.0f;
	}

	// Width breathing - 11 periods (fast), expands stereo as signal decays
	// Mid/side: dynamicWidth > 1 = wider than normal, higher values = dramatic expansion
	float phase11 = yNorm * 11.0f;
	float tri11 = 1.0f - 4.0f * std::abs(phase11 - std::floor(phase11) - 0.5f);
	widthBreath_ = std::clamp(0.5f + tri11 * 0.4f, 0.0f, 1.2f); // 0.1 to 0.9 range

	// Cross-channel bleed - 9 periods, L↔R mixing in FDN feedback for stereo complexity
	// Subtle effect: 0.0 to 0.25 range adds correlation without smearing stereo image
	float phase9 = yNorm * 9.0f;
	float tri9 = 1.0f - 4.0f * std::abs(phase9 - std::floor(phase9) - 0.5f);
	float baseBleed = yNorm * 0.15f; // Increases with zone position
	crossBleed_ = std::clamp(baseBleed + tri9 * 0.1f, 0.0f, 0.25f);

	// FDN feedback scale - reduce FDN feedback as Zone 3 increases (inverse relationship)
	// At low Zone 3: full FDN feedback (1.0), at high Zone 3: reduced (0.7)
	fdnFeedbackScale_ = 1.0f - yNorm * 0.3f;

	// Per-stage cascade allpass coefficients - higher coeffs = more diffusion (less slapback)
	// Each stage uses different triangle period for variety
	// Lush/Vast need higher coefficients for proper diffusion; normal modes need lower for transient punch
	float coeffBase = cascadeDoubleUndersample_ ? (0.5f + yNorm * 0.12f)  // Lush/Vast: 0.5-0.62
	                                            : (0.32f + yNorm * 0.2f); // Normal: 0.32-0.52

	// C0: 8 periods - fastest stage, moderate variation
	float phase8 = yNorm * 8.0f;
	float tri8 = 1.0f - 4.0f * std::abs(phase8 - std::floor(phase8) - 0.5f);
	float c0Min = cascadeDoubleUndersample_ ? 0.45f : 0.28f;
	cascadeCoeffs_[0] = std::clamp(coeffBase + tri8 * 0.08f, c0Min, 0.75f);

	// C1: 6 periods - slightly more variation
	float phase6 = yNorm * 6.0f;
	float tri6 = 1.0f - 4.0f * std::abs(phase6 - std::floor(phase6) - 0.5f);
	float c1Min = cascadeDoubleUndersample_ ? 0.45f : 0.28f;
	cascadeCoeffs_[1] = std::clamp(coeffBase + tri6 * 0.1f, c1Min, 0.75f);

	// C2: 4 periods - slower variation, slightly higher base for density
	float phase4 = yNorm * 4.0f;
	float tri4 = 1.0f - 4.0f * std::abs(phase4 - std::floor(phase4) - 0.5f);
	float c2Min = cascadeDoubleUndersample_ ? 0.48f : 0.30f;
	cascadeCoeffs_[2] = std::clamp(coeffBase + 0.05f + tri4 * 0.1f, c2Min, 0.78f);

	// C3: 3 periods - longest stage, highest base for maximum diffusion
	float phase3 = yNorm * 3.0f;
	float tri3 = 1.0f - 4.0f * std::abs(phase3 - std::floor(phase3) - 0.5f);
	float c3Min = cascadeDoubleUndersample_ ? 0.50f : 0.32f;
	cascadeCoeffs_[3] = std::clamp(coeffBase + 0.08f + tri3 * 0.12f, c3Min, 0.82f);
}

} // namespace deluge::dsp::reverb
