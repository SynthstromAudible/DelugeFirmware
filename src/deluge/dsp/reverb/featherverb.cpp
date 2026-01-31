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
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "util/cfunctions.h"
#include "util/fixedpoint.h"

namespace deluge::dsp::reverb {

using namespace deluge::dsp;

// Compile-time diagnostic toggles
static constexpr bool kMuteEarly = false;
static constexpr bool kMuteCascade = false;
static constexpr bool kMuteCascadeFeedback = false;
static constexpr bool kBypassFdnToCascade = false;
static constexpr bool kDisableVastUndersample = false;

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
	cascadeLpStateR_ = 0.0f;
	cascadeLpStateMono_ = 0.0f;
	cascadeLpStateSide_ = 0.0f;
	feedbackEnvelope_ = 0.0f;
	// inputAccum_ persists - holds peak input level for servo ratio
	owlFbEnvScale_ = 1.0f;
	owlSilenceCount_ = 0;
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
	inputAccum_ = 0.0f;
	inputPeakReset_ = true;
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

	// Owl: lower HPF cutoff to let more bass sustain
	const float hpCoeff = owlMode_ ? (0.997f - hpCutoff_ * 0.05f) : (0.995f - hpCutoff_ * 0.09f);
	const float outLpCoeff = 0.1f + lpCutoff_ * 0.85f;
	const float tailFeedback = feedback_ * feedback_; // Hoist: tail decays faster than early

	// Owl mode feedback constants (hoisted - don't change per sample)
	const float owlFbDelta = feedback_ - 0.32f;
	const float owlRoomNorm = owlFbDelta * 8.333f;
	const float owlFdnFb = 0.32f + owlFbDelta * 0.7f;
	const float owlCascadeFb = 0.32f + owlFbDelta * 2.0f;
	const float owlGlobalFbBase = 0.9f + owlRoomNorm * 0.4f;
	const float owlGlobalFbCoeff = (owlGlobalFbBase + cascadeNestFeedback_) * skyLoopFb_;

	// Owl servo: ratio-based feedback limiting (every buffer)
	float owlDelayScale = 1.0f;
	if (owlMode_) {
		float ratio = feedbackEnvelope_ / std::max(inputAccum_, 1e-6f);
		float kneeRatio = 0.1f + (owlZ3Norm_ * 0.1f);
		constexpr float kBandwidth = 0.001f;
		float excess = std::max(0.0f, ratio - kneeRatio - kBandwidth);
		float deficit = std::max(0.0f, kneeRatio - kBandwidth - ratio);
		constexpr float kLimitStrength = 10.0f;
		constexpr float kBoostStrength = 10.0f;
		float denom = 1.0f + excess * kLimitStrength;
		float numer = 1.0f + deficit * kBoostStrength;
		float targetScale = (numer) / (denom);
		targetScale = std::clamp(targetScale, 0.001f, 10.0f);
		owlFbEnvScale_ += (targetScale - owlFbEnvScale_) * 0.003f; // Very slow smoothing to avoid servo oscillations
	}
	// Owl-specific buffer-rate calculations (skip entirely when not in Owl mode)
	float owlEffFb = 0.0f;
	float owlGlobalFb = 0.0f;
	float owlReadCacheScale = 0.5f;
	if (owlMode_) {
		owlDelayScale = owlFbEnvScale_ * owlFbEnvScale_; // Squared for faster choke
		owlEffFb = owlFdnFb * fdnFeedbackScale_ * 1.8f * owlDelayScale;
		owlGlobalFb = owlGlobalFbCoeff * owlFbEnvScale_;
		owlReadCacheScale = owlFbEnvScale_ * 0.5f;
	}

	// Sky/Vast shared feedback constants (hoisted - don't change per sample)
	float chainGlobalFbBase = 0.0f;
	float chainC2FbBase = 0.0f;
	float chainC3FbBase = 0.0f;
	if (skyChainMode_ || vastChainMode_) {
		const float chainLoopFbBase = feedback_ * 0.4f * delayRatio_ * skyLoopFb_;
		chainGlobalFbBase = cascadeNestFeedback_ * 0.8f;
		chainC2FbBase = chainLoopFbBase * (1.6f - skyFbBalance_ * 1.4f);
		chainC3FbBase = chainLoopFbBase * (0.2f + skyFbBalance_ * 1.4f);
	}

	// Normal mode hoisted constant
	const float cascadeFbMult = tailFeedback * cascadeFeedbackMult_;

	// LFO for Sky/Vast/Owl (every buffer - striding causes tonal artifacts)
	float lfoTriCached = skyRandWalkSmooth_;
	float d0ModCached = 0.0f;
	float d1ModCached = 0.0f;
	if (skyChainMode_ || vastChainMode_ || owlMode_) {
		const size_t numSteps = input.size() / 2;
		skyRandState_ = skyRandState_ * 1664525u + 1013904223u;
		float randStep = (static_cast<float>(skyRandState_ >> 16) / 32768.0f - 1.0f);
		float stepSize = 0.001f * skyLfoFreq_ * static_cast<float>(numSteps);
		skyRandWalk_ += randStep * stepSize;
		skyRandWalk_ *= 0.97f;
		skyRandWalk_ = std::clamp(skyRandWalk_, -1.0f, 1.0f);
		float smoothCoeff = vastChainMode_ ? 0.012f * skyLfoFreq_ : 0.025f * skyLfoFreq_;
		float bufferSmooth = std::min(1.0f, smoothCoeff * static_cast<float>(numSteps));
		skyRandWalkSmooth_ += bufferSmooth * (skyRandWalk_ - skyRandWalkSmooth_);
		lfoTriCached = skyRandWalkSmooth_;
		// Compute modulation offsets as floats for interpolation (Sky/Vast only)
		if (!owlMode_) {
			float pitchScale = 1.0f - skyLfoRouting_;
			d0ModCached = std::max(0.0f, lfoTriCached * modDepth_ * pitchScale);
			d1ModCached = std::max(0.0f, -lfoTriCached * modDepth_ * pitchScale);
		}
	}

	// Flag for FDN pitch interpolation (only Sky/Vast use it - Owl and normal don't)
	const bool needsPitchInterp = skyChainMode_ || vastChainMode_;

	// Owl filter coefficients (buffer-rate - depend on cached LFO)
	float owlMonoCoeff = kCascadeLpCoeffMono;
	float owlSideCoeff = kCascadeLpCoeffSide;
	float owlAmpModL = 1.0f;
	float owlAmpModR = 1.0f;
	if (owlMode_) {
		float filterMod = lfoTriCached * skyLfoAmp_ * skyLfoRouting_ * 0.5f;
		owlMonoCoeff = std::clamp(kOwlLpCoeffMono - filterMod, 0.2f, 0.85f);
		owlSideCoeff = std::clamp(kOwlLpCoeffSide + filterMod * 1.5f, 0.4f, 0.99f);
	}
	// Hoisted values to eliminate in-loop ternaries
	const float owlC2Scale = owlMode_ ? owlFbEnvScale_ : 1.0f;
	const float cascadeLpCoeffMono = owlMode_ ? owlMonoCoeff : kCascadeLpCoeffMono;
	const float cascadeLpCoeffSide = owlMode_ ? owlSideCoeff : kCascadeLpCoeffSide;
	const float cascadeAmpModVal = owlMode_ ? (skyLfoAmp_ * skyLfoRouting_) : cascadeAmpMod_;
	// Owl: aggregate decay for inputAccum_ (~12s at 22kHz, applied once per buffer)
	// Linear approx: (1-x)^n ≈ 1 - n*x for small x (avoids std::pow)
	const float inputAccumDecay = 1.0f - (owlMode_ ? 5e-6f * static_cast<float>(input.size()) : 0.0f);

	// Sky/Vast feedback envelope scaling and amplitude modulation (buffer-rate)
	float chainGlobalFb = 0.0f;
	float chainC2Fb = 0.0f;
	float chainC3Fb = 0.0f;
	float chainLfoOut = 0.0f;
	float chainAmpModL = 1.0f;
	float chainAmpModR = 1.0f;
	if (skyChainMode_ || vastChainMode_) {
		const float chainFbEnvScale = 1.0f - std::min(feedbackEnvelope_ * 3.0f, 1.0f);
		chainGlobalFb = chainGlobalFbBase * chainFbEnvScale;
		chainC2Fb = chainC2FbBase * chainFbEnvScale;
		chainC3Fb = chainC3FbBase * chainFbEnvScale;
		chainLfoOut = lfoTriCached * skyLfoAmp_ * skyLfoRouting_;
		chainAmpModL = 1.0f + chainLfoOut * 0.3f;
		chainAmpModR = 1.0f - chainLfoOut * 0.3f;
	}

	// Stereo rotation coefficients (buffer-rate, small-angle approx)
	const float rotSinA = lfoTriCached * 0.5f;
	const float rotCosA = 1.0f - rotSinA * rotSinA * 0.5f;

	// Owl: buffer-rate modulation values (skip entirely when not in Owl mode)
	int32_t tapModL = 0;
	int32_t tapModR = 0;
	float owlFbLfoMod = 1.0f;
	float owlCascadeFbMod = 1.0f;
	float owlD2ReadMod = 1.0f;
	float owlWriteScale = 0.0f;
	float owlH2Scale = 0.0f;
	if (owlMode_) {
		tapModL = static_cast<int32_t>(lfoTriCached * 280.0f);
		tapModR = -tapModL;
		const float absLfoTri = std::abs(lfoTriCached);
		owlFbLfoMod = 1.0f - lfoTriCached * 0.3f;
		owlCascadeFbMod = (1.3f + absLfoTri * 0.3f) * owlFbEnvScale_;
		owlD2ReadMod = 1.0f - absLfoTri * 0.3f;
		owlWriteScale = owlEffFb * owlFbLfoMod;
		owlH2Scale = owlFdnFb * 0.95f * owlDelayScale;
	}

	// Cache matrix
	const auto& m = matrix_;

	for (size_t frame = 0; frame < input.size(); ++frame) {
		// === Full-rate: HPF, envelope, predelay ===
		float in = static_cast<float>(input[frame]) * kInputScale;
		float hpOut = in - hpState_;
		hpState_ += (1.0f - hpCoeff) * hpOut;

		// Save input for dry subtraction BEFORE gain boost
		float inOrig = hpOut;

		// +3dB input gain when dry subtraction enabled (linked to DRY- toggle)
		in = dryMinus_ ? hpOut * 1.414f : hpOut;

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

			// Peak hold with reset after sustained silence
			// Scale by sqrt(2) to approximate peak-to-RMS ratio
			constexpr float kPeakToRmsScale = 1.414f;
			constexpr float kNoiseFloor = 1e-6f;
			constexpr uint8_t kSilenceThreshold = 64; // ~1.5ms at 44.1kHz/2x undersample
			static uint8_t silenceCount = 0;

			float absFdnIn = std::abs(fdnIn) * kPeakToRmsScale;
			if (absFdnIn > kNoiseFloor) {
				// Above noise floor - reset silence counter
				silenceCount = 0;
				if (inputPeakReset_) {
					// Dirty flag set - allow new peak even if lower
					// But require significant signal, not just noise (1e-4 ~= -40dB)
					if (absFdnIn > 1e-4f) {
						inputAccum_ = absFdnIn;
						inputPeakReset_ = false;
					}
				}
				else if (absFdnIn > inputAccum_) {
					// New peak - track with smoothing
					inputAccum_ += (absFdnIn - inputAccum_) * 0.1f;
				}
				// else: below peak, hold (decay applied below)
			}
			else {
				// Below noise floor - count consecutive samples
				if (silenceCount < kSilenceThreshold) {
					silenceCount++;
				}
				else {
					// Sustained silence - arm reset for next note
					inputPeakReset_ = true;
				}
			}
			// Owl: inputAccum_ decay hoisted to buffer-rate (applied after loop)

			// LFO values cached at buffer rate for Sky/Vast/Owl (random walk evolves slowly)
			const float lfoTri = lfoTriCached;
			const float d0Mod = d0ModCached;
			const float d1Mod = d1ModCached;

			// Read FDN delays - use interpolation only for Sky/Vast pitch mod (avoids overhead in normal/Owl)
			float d0, d1;
			if (needsPitchInterp) {
				d0 = fdnReadAtInterp(0, d0Mod);
				d1 = fdnReadAtInterp(1, d1Mod);
			}
			else {
				d0 = fdnRead(0);
				d1 = fdnRead(1);
			}
			float d2 = fdnRead(2);
			// Owl: reduce D0/D1 gain, modulate D2 (buffer-rate values)
			if (owlMode_) {
				d0 *= 0.5f;
				d1 *= 0.5f;
				d2 *= owlD2ReadMod; // 0.7 to 1.0, decorrelated from D0/D1
			}

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
			float dynamicWidth = width_ * widthBreath_; // Z3-controlled width variation

			if (vastChainMode_) {
				// === VAST CHAIN MODE (Smeared feedback like Sky) ===
				// Topology with smeared feedback loops + global recirculation:
				//   Input → C0 → D0 → C1 → D1 → C2 → D2 → C3 → output
				//                 ↑         ↑         ↑
				//            (C2→C0)    (C3→C1)    (C3)
				// Feedback passes through cascade stages for diffusion before entering delays
				// This reduces comb filter sensitivity compared to direct feedback

				// Pre-decimation AA filter with global C3 feedback (hoisted coeffs)
				float chainIn =
				    onepole(fdnIn * 1.4f + prevC3Out_ * chainGlobalFb, cascadeAaState1_, kPreCascadeAaCoeff);

				// C0 with 4x undersample (input → first allpass)
				c0Accum_ += chainIn;
				if (c0Phase_ == 1) {
					float avgIn = c0Accum_ * 0.5f;
					c0Prev_ = processCascadeStage(0, avgIn);
					// Smeared feedback: C2 → C0 → D0 (Z1 controls balance)
					float c2Smeared = processCascadeStage(0, c2Prev_ * chainC2Fb);
					c0Prev_ += c2Smeared;
					c0Accum_ = 0.0f;
				}
				c0Phase_ = (c0Phase_ + 1) & 1;
				c0 = c0Prev_;

				// D0 delay between C0 and C1 (modulated pitch with interpolation)
				fdnWrite(0, c0);
				fdnWrite(0, c0); // Double write for 4x undersample
				float d0Out = fdnReadAtInterp(0, d0Mod);

				// C1 with 4x undersample
				c1Accum_ += d0Out;
				if (c1Phase_ == 1) {
					float avgIn = c1Accum_ * 0.5f;
					c1Prev_ = processCascadeStage(1, avgIn);
					// Smeared feedback: C3 → C1 → D1 (Z1 controls balance)
					float c3Smeared = processCascadeStage(1, c3Prev_ * chainC3Fb);
					c1Prev_ += c3Smeared;
					c1Accum_ = 0.0f;
				}
				c1Phase_ = (c1Phase_ + 1) & 1;
				c1 = c1Prev_;

				// D1 delay between C1 and C2 (modulated pitch with interpolation)
				fdnWrite(1, c1);
				fdnWrite(1, c1);
				float d1Out = fdnReadAtInterp(1, d1Mod);

				// C2 with 4x undersample + pitch modulation
				c2Accum_ += d1Out;
				if (c2Phase_ == 1) {
					float avgIn = c2Accum_ * 0.5f;
					float c2Coeff = cascadeCoeffs_[2];
					size_t origWritePos = cascadeWritePos_[2];
					size_t readPos = origWritePos;
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
					size_t readPos = origWritePos;
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

				// Track RAW c3 before clipping for servo feedback detection
				prevC3Out_ = c3;

				// Soft clip C3 at ~-2dB below 0dBFS (0.05) with 0.2 ratio
				// Input 0.1 → output 0.06 (0dBFS), smooth transition to hard clip
				constexpr float kC3Limit = 0.05f;
				if (std::abs(c3) > kC3Limit) {
					c3 = std::copysign(kC3Limit + (std::abs(c3) - kC3Limit) * 0.2f, c3);
				}

				// Amplitude modulation for diffusion contour (buffer-rate LFO)
				c2 *= (1.0f + chainLfoOut);
				c3 *= (1.0f - chainLfoOut);

				// Mix chain outputs - stereo from well-matched early stages only
				// C0(773) and C1(997) are ~30% different - good for balanced stereo
				// C2(1231) and C3(4001) are 3x different - mono only to avoid L/R imbalance
				float cascadeMono = (c2 + c3) * 0.5f;
				float cascadeSide = (c0 - c1) * cascadeSideGain_ * dynamicWidth;
				cascadeMono = onepole(cascadeMono, cascadeLpStateMono_, kCascadeLpCoeffMono);
				cascadeSide = onepole(cascadeSide, cascadeLpStateSide_, kCascadeLpCoeffSide);
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;
				// LFO output modulation: stereo movement (buffer-rate)
				cascadeOutL *= chainAmpModL;
				cascadeOutR *= chainAmpModR;

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

				// Use hoisted constants (all computed at buffer-rate)
				float chainIn = fdnIn * 1.4f + prevC3Out_ * chainGlobalFb;

				// C0 at 2x rate
				c0 = processCascadeStage(0, chainIn);

				// D0 delay - feedback from C2 smeared through C0 (interpolated pitch mod)
				// Z1 controls balance: low Z1 = more C2→C0, high Z1 = more C3→C1
				float c2Smeared = processCascadeStage(0, c2Prev_ * chainC2Fb);
				fdnWrite(0, c0 + c2Smeared);
				float d0Out = fdnReadAtInterp(0, d0Mod);

				// C1 at 2x rate
				c1 = processCascadeStage(1, d0Out);

				// D1 delay - feedback from C3 smeared through C1 (interpolated pitch mod)
				float c3Smeared = processCascadeStage(1, c3Prev_ * chainC3Fb);
				fdnWrite(1, c1 + c3Smeared);
				float d1Out = fdnReadAtInterp(1, d1Mod);

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

				// Amplitude modulation for diffusion contour (buffer-rate LFO)
				c2 *= (1.0f + chainLfoOut);
				c3 *= (1.0f - chainLfoOut);

				// Ping-pong stereo: C0,C2→L, C1→R, C3→center
				float cascadeMono = c3;
				float cascadeSide = (c0 + c2 * 0.7f - c1) * cascadeSideGain_ * dynamicWidth;
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;
				// Stereo rotation following LFO (buffer-rate coefficients)
				float tempL = cascadeOutL * rotCosA - cascadeOutR * rotSinA;
				cascadeOutR = cascadeOutL * rotSinA + cascadeOutR * rotCosA;
				cascadeOutL = tempL;

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
						size_t tapOff0 = static_cast<size_t>(static_cast<int32_t>(kMultiTapOffsets[0]) + tapModL);
						size_t tapPos0 = (prevPos0 + tapOff0) % cascadeLengths_[2];
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
							size_t tapOff1 = static_cast<size_t>(static_cast<int32_t>(kMultiTapOffsets[1]) + tapModL);
							size_t tapPos1 = (prevPos1 + tapOff1) % cascadeLengths_[3];
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
						size_t tapOff2 = static_cast<size_t>(static_cast<int32_t>(kMultiTapOffsets[2]) + tapModR);
						size_t tapPos2 = (prevPos2 + tapOff2) % cascadeLengths_[0];
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
							size_t tapOff3 = static_cast<size_t>(static_cast<int32_t>(kMultiTapOffsets[3]) + tapModR);
							size_t tapPos3 = (prevPos3 + tapOff3) % cascadeLengths_[1];
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
					// === FEATHER: Serial cascade with full stereo ===
					float c0 = processCascadeStage(0, cascadeInWithFb);
					float c1 = processCascadeStage(1, c0);
					float c2 = processCascadeStage(2, c1);
					float c3 = processCascadeStage(3, c2);

					// Ping-pong stereo: C0,C2→L, C1→R, C3→center
					float cascadeMono = c3;
					float cascadeSide = (c0 + c2 * 0.7f - c1) * cascadeSideGain_ * dynamicWidth;
					cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
					cL1 = cascadeMono + cascadeSide;
					cR1 = cascadeMono - cascadeSide;
				}

				cascadeOutL = cL1;
				cascadeOutR = cR1;
				// Stereo rotation following LFO (buffer-rate coefficients)
				float tempL = cascadeOutL * rotCosA - cascadeOutR * rotSinA;
				cascadeOutR = cascadeOutL * rotSinA + cascadeOutR * rotCosA;
				cascadeOutL = tempL;
				prevC3Out_ = cascadeOutL + cascadeOutR;

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
				// Owl mode: interleaved D0 → C0 → D1 → C1, all at 4x undersample
				// Input → D0 → C0 → D1 → C1 → C2 → C3 → Output
				//               ↑                        ↓
				//              D2 ←──────────────────────┘
				float fdnFb = feedback_;     // FDN feedback - scaled separately for Owl
				float cascadeFb = feedback_; // Cascade feedback - scaled up for Owl
				float c3ForFb = 0.0f;        // DC-blocked feedback signal for envelope tracking
				if (owlMode_) {
					// Use buffer-rate hoisted constants (envelope applied once per buffer)
					fdnFb = owlFdnFb;
					cascadeFb = owlCascadeFb;

					// Accumulate D0/D1/D2 reads for 4x AA (envelope scale moved to 4x cache averaging)
					owlD0ReadAccum_ += d0;
					owlD1ReadAccum_ += d1;
					// Envelope tracking moved to buffer rate for performance

					// Cheap HPF on feedback to tame LF rumble (~100Hz at 11kHz effective rate)
					c3ForFb = prevC3Out_ - dcBlockState_;
					dcBlockState_ += 0.057f * c3ForFb;
					cascadeIn = owlD0Cache_ + c3ForFb * owlGlobalFb;

					// D0 write: input - envelope limited + LFO modulated (pre-combined scale)
					owlD0WriteAccum_ += fdnIn * owlWriteScale;
					// D1 write will be set from C0 output below
					// h2 = C3 for nested feedback (accumulated and written at 4x rate)
				}

				// C0 with 4x undersample - fed by D0
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

				// Owl: C0 → D1 → C1 (D1 sits between C0 and C1)
				if (owlMode_) {
					// Accumulate C0 output for D1 write (pre-combined scale)
					owlD1WriteAccum_ += c0 * owlWriteScale;
				}

				// C1 with 4x undersample - fed by D1 (Owl) or C0 (Lush)
				float c1Input = owlMode_ ? owlD1Cache_ : c0;
				c1Accum_ += c1Input;
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
				c2Accum_ += c1 * owlC2Scale;
				if (c2Phase_ == 1) {
					float avgIn = c2Accum_ * 0.5f;
					float c2Coeff = cascadeCoeffs_[2];
					size_t origWritePos = cascadeWritePos_[2];
					// Pitch mod with subtraction wrap (no modulo - much cheaper)
					size_t c2ModOffset = static_cast<size_t>(std::max(0.0f, lfoTri * cascadeModDepth_));
					size_t readPos = origWritePos + c2ModOffset;
					if (readPos >= cascadeLengths_[2])
						readPos -= cascadeLengths_[2];
					float delayed = buffer_[cascadeOffsets_[2] + readPos];
					float output = -c2Coeff * avgIn + delayed;
					float writeVal = avgIn + c2Coeff * output;
					buffer_[cascadeOffsets_[2] + origWritePos] = writeVal;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					buffer_[cascadeOffsets_[2] + cascadeWritePos_[2]] = writeVal;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					// Multi-tap write with subtraction wrap (cheaper than modulo)
					if (cascadeDoubleUndersample_) {
						size_t tapPos = origWritePos + kMultiTapOffsets[2];
						if (tapPos >= cascadeLengths_[2])
							tapPos -= cascadeLengths_[2];
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

				// Amplitude modulation for diffusion contour (hoisted cascadeAmpModVal)
				if (cascadeAmpModVal != 0.0f) {
					// Owl: use cached LFO (synced to 4x update rate like Vast)
					float lfoForAmpMod = owlMode_ ? vastLfoCache_ : lfoTri;
					c2 *= (1.0f + lfoForAmpMod * cascadeAmpModVal);
					c3 *= (1.0f - lfoForAmpMod * cascadeAmpModVal);
				}

				// Mix cascade outputs - stereo from well-matched early stages only
				// C0(773) and C1(997) are ~30% different - good for balanced stereo
				// C2(1231) and C3(4001) are 3x different - mono only to avoid L/R imbalance
				float cascadeMono = (c2 + c3) * 0.5f;
				float cascadeSide = (c0 - c1) * cascadeSideGain_ * dynamicWidth;

				// Owl: modulate mid/side filter cutoffs for stereo movement (buffer-rate coeffs)
				cascadeMono = onepole(cascadeMono, cascadeLpStateMono_, cascadeLpCoeffMono);
				cascadeSide = onepole(cascadeSide, cascadeLpStateSide_, cascadeLpCoeffSide);
				cascadeMono = onepole(cascadeMono, cascadeLpState_, cascadeDamping_);
				cascadeOutL = cascadeMono + cascadeSide;
				cascadeOutR = cascadeMono - cascadeSide;

				// Owl: LFO output modulation for stereo movement (buffer-rate)
				if (owlMode_) {
					cascadeOutL *= owlAmpModL;
					cascadeOutR *= owlAmpModR;

					// Stereo crossfeed to even out channels over time
					float crossL = cascadeOutL + crossBleed_ * cascadeOutR;
					float crossR = cascadeOutR + crossBleed_ * cascadeOutL;
					cascadeOutL = crossL;
					cascadeOutR = crossR;

					// D2 echo tap - distinct repeat before C3 diffuses it
					float echoTap = owlD2Cache_ * owlEchoGain_;
					cascadeOutL += echoTap;
					cascadeOutR += echoTap;
				}

				// Inject input + cascade feedback into FDN
				if (owlMode_) {
					// Owl: All FDN delays at 4x undersample with proper AA
					// D0/D1/D2 reads accumulated earlier, writes accumulated here
					// D2 read accumulated, envelope scale applied at 4x cache averaging
					owlD2ReadAccum_ += d2;
					// Use squared scale so delays choke faster than cascade
					// fdnFb is less sensitive to Room than cascade feedback
					// C2→D2→C3 envelope limited (pre-combined scale)
					owlD2WriteAccum_ += c2 * owlH2Scale;
					// AA filter on input before FDN (prevents aliasing at 4x rate)
					float fdnInAA = onepole(fdnIn, owlInputAaState_, kPreCascadeAaCoeff);
					owlD0WriteAccum_ += fdnInAA;
					if (c3Phase_ == 0) {
						// c3 just updated - average + envelope scale all accumulated reads/writes
						owlD0Cache_ = owlD0ReadAccum_ * owlReadCacheScale;
						owlD1Cache_ = owlD1ReadAccum_ * owlReadCacheScale;
						owlD2Cache_ = owlD2ReadAccum_ * owlReadCacheScale;
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
					h0 += fdnIn + cascadeMono * cascadeFbMult * owlCascadeFbMod;
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
					h0 += fdnIn + cascadeMono * cascadeFbMult;
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

		// prevOutputMono_ = (outL + outR) * 0.5f * (1.0f + std::abs(inOrig) * predelay_ * 2.0f);
		prevOutputMono_ = (outL + outR) * 0.5f + (std::abs(inOrig) * predelay_ * 2.0f);

		// Output LPF
		outL = onepole(outL, lpStateL_, outLpCoeff);
		outR = onepole(outR, lpStateR_, outLpCoeff);

		// Add direct early brightness tap (bypasses LPF for crisp transients)
		outL += directEarlyL_;
		outR += directEarlyR_;

		// Subtract dry input to remove bleedthrough (sparse topology compensation)
		// Toggle via predelay encoder button: DRY- = enabled, DRY+ = disabled
		// inOrig is pre-gain, so subtract at unity
		if (dryMinus_) {
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

	// Owl: apply aggregate inputAccum_ decay (hoisted from per-sample)
	if (owlMode_) {
		inputAccum_ *= inputAccumDecay;
	}

	// Buffer-end envelope update: track amplitude (not squared)
	float outAmp = std::abs(prevOutputMono_);
	float releaseCoeff = 0.003f;
	if (owlMode_) {
		// Feedback envelope: track output level (every buffer)
		float attackCoeff = 0.008f + predelay_ * 0.019f;
		constexpr float kReleaseCoeff = 0.05f;
		float fbCoeff = (outAmp > feedbackEnvelope_) ? attackCoeff : kReleaseCoeff;
		feedbackEnvelope_ += fbCoeff * (outAmp - feedbackEnvelope_);
	}
	else {
		// Non-Owl modes: original per-buffer tracking
		float attackCoeff = 0.02f;
		float envCoeff = (outAmp > feedbackEnvelope_) ? attackCoeff : releaseCoeff;
		feedbackEnvelope_ += envCoeff * (outAmp - feedbackEnvelope_);
	}
}

void Featherverb::setRoomSize(float value) {
	roomSize_ = value;
	feedback_ = 0.32f + value * 0.12f;
}

void Featherverb::setDamping(float value) {
	damping_ = value;
	dampCoeff_ = 0.1f + (1.0f - value) * 0.85f;
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
	// Update Owl mode attack rate from predelay
	if (owlMode_) {
		owlEnvRatio_ = 0.5f + predelay_ * 1.5f; // Range: 0.5 to 2.0
	}
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
static bool gram_schmidt_3x3(std::array<std::array<float, 3>, 3>& m) {
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

	const float y_norm = static_cast<float>(zone1_) / 1023.0f;
	const int32_t zone = zone1_ >> 7;
	const double gamma_phase = zone * 0.125;

	// Vast mode: offset y_norm slightly to avoid phi triangle null at Z1 max
	float y_norm_adj = (zone == 7) ? std::min(y_norm, 0.97f) : y_norm;
	const PhiTriContext ctx{y_norm_adj, 1.0f, 1.0f, gamma_phase};
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

	if (!gram_schmidt_3x3(matrix_)) {
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
	float d0_tri = (vals[0] + 1.0f) * 0.5f;
	float d1_tri = (vals[3] + 1.0f) * 0.5f;

	fdnLengths_[0] = kD0MinLength + static_cast<size_t>(d0_tri * (kD0MaxLength - kD0MinLength));
	fdnLengths_[1] = kD1MinLength + static_cast<size_t>(d1_tri * (kD1MaxLength - kD1MinLength));

	// Precompute delay ratio for feedback normalization (avoid division in hot path)
	delayRatio_ = static_cast<float>(fdnLengths_[0] + fdnLengths_[1]) / static_cast<float>(kD0MaxLength + kD1MaxLength);

	// Sky/Vast/Owl mode: Z1 controls various parameters using phi triangles
	if (skyChainMode_ || vastChainMode_ || owlMode_) {
		// Balance between C2→C0 and C3→C1 feedback paths
		float balance_base = y_norm;                          // 0 = favor C2→C0, 1 = favor C3→C1
		float balance_mod = (vals[1] + 1.0f) * 0.15f - 0.15f; // ±0.15 texture
		skyFbBalance_ = std::clamp(balance_base + balance_mod, 0.0f, 1.0f);

		// LFO amplitude and frequency from phi triangles
		float amp_tri = (vals[2] + 1.0f) * 0.5f;   // 0 to 1
		float freq_tri = (vals[4] + 1.0f) * 0.5f;  // 0 to 1
		float pitch_tri = (vals[5] + 1.0f) * 0.5f; // 0 to 1
		skyLfoAmp_ = 0.15f + amp_tri * 0.85f;      // 0.15 → 1.0
		skyLfoFreq_ = 0.5f + freq_tri * 2.0f;      // 0.5x → 2.5x
		// Owl: lower pitch modulation to avoid artifacts at 4x undersample
		float max_pitch = owlMode_ ? 80.0f : 160.0f;
		modDepth_ = 20.0f + pitch_tri * max_pitch;
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
	// Resync z1-controlled params (skyFbBalance_, LFO, etc.) to avoid feedback imbalance
	if (skyChainMode_ || vastChainMode_ || owlMode_) {
		updateMatrix();
	}
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
	// - Feather: cascadeLpStateR_ = R channel filter (separate L/R)
	// - Owl/Vast: cascadeLpStateMono_/Side_ = M/S processing
	bool modeChanged = (featherMode_ != prevFeatherMode) || (owlMode_ != prevOwlMode) || (skyChainMode_ != prevSkyMode)
	                   || (vastChainMode_ != prevVastMode);
	if (modeChanged) {
		cascadeLpStateR_ = 0.0f;
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
		// Initialize Owl mode envelope tracking from current parameters
		if (owlMode_) {
			// inputAccum_ persists - don't reset
			owlFbEnvScale_ = 1.0f;
			owlSilenceCount_ = 0;
			owlEnvRatio_ = 0.5f + predelay_ * 1.5f;
			owlZ3Norm_ = static_cast<float>(zone3_) / 1023.0f;
		}
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
	// Resync z1-controlled params (skyFbBalance_, LFO, etc.) to avoid feedback imbalance
	if (skyChainMode_ || vastChainMode_ || owlMode_) {
		updateMatrix();
	}
}

void Featherverb::updateFeedbackPattern() {
	using namespace phi;

	const float y_norm = static_cast<float>(zone3_) / 1023.0f;
	const int32_t zone = zone3_ >> 7;
	const double gamma_phase = zone * 0.125;

	// Widened bias for more tonal variation across z3 zones
	static constexpr std::array<std::array<float, 3>, 8> kZoneBias = {{{1.00f, 1.00f, 1.00f},
	                                                                   {1.25f, 0.90f, 0.75f},
	                                                                   {0.70f, 1.00f, 1.30f},
	                                                                   {1.20f, 0.70f, 1.15f},
	                                                                   {0.65f, 1.35f, 0.70f},
	                                                                   {1.30f, 0.75f, 1.00f},
	                                                                   {0.60f, 1.00f, 1.40f},
	                                                                   {1.35f, 0.85f, 0.60f}}};

	const PhiTriContext ctx{y_norm, 1.0f, 1.0f, gamma_phase};
	std::array<float, 3> mods = ctx.evalBank(kFeedback3TriBank);

	for (size_t i = 0; i < 3; ++i) {
		// Wide range (±45%) for dramatic tonal variation
		feedbackMult_[i] = std::clamp(kZoneBias[zone][i] + mods[i] * 0.2f, 0.55f, 1.45f);
	}

	// Cascade series mix - 10 periods for fine density control
	// seriesMix=0 → C3 parallel (9 paths, sparse), seriesMix=1 → C3 series (16 paths, dense)
	float tri10 = dsp::triangleSimpleUnipolar(y_norm * 10.0f) * 2.0f - 1.0f; // Bipolar -1..1
	cascadeSeriesMix_ = 0.6f + tri10 * 0.25f;                                // Map to 0.35..0.85

	// Cascade feedback - 7 periods, clockwise = more feedback
	float tri7 = dsp::triangleSimpleUnipolar(y_norm * 7.0f) * 2.0f - 1.0f;
	float base_feedback = 0.03f + y_norm * 0.12f;
	cascadeFeedbackMult_ = std::clamp(base_feedback + tri7 * 0.02f, 0.02f, 0.2f);

	// Nested cascade feedback (C3→C0) - 5 periods
	float tri5 = dsp::triangleSimpleUnipolar(y_norm * 5.0f) * 2.0f - 1.0f;
	float base_nest = std::max(0.0f, (y_norm - 0.35f) * 0.5f);
	cascadeNestFeedbackBase_ = std::clamp(base_nest + tri5 * 0.08f, 0.0f, 0.4f);
	// Recompute combined value (Zone 2 adds vast boost)
	updateSizes();

	// LFO pitch wobble depth - 13 periods (fast), adds subtle chorus/shimmer
	if (skyChainMode_ || vastChainMode_ || owlMode_) {
		// Sky/Vast/Owl: Z3 controls pitch wobble (lower max at 4x to avoid artifacts)
		// Sky max capped at 180 to match z1's modDepth range and avoid aliasing
		float tri13 = dsp::triangleSimpleUnipolar(y_norm * 13.0f) * 2.0f - 1.0f;
		float max_wobble = (vastChainMode_ || owlMode_) ? 80.0f : 140.0f;
		float wobble_texture = (vastChainMode_ || owlMode_) ? 15.0f : 40.0f;
		modDepth_ = std::max(0.0f, y_norm * max_wobble + tri13 * wobble_texture);

		// Z3 also controls local loop feedback (smeared path density)
		skyLoopFb_ = std::max(0.4f, 0.4f + y_norm * 0.9f + tri13 * 0.2f);

		// Z3 controls LFO routing - 7 periods for variety
		float tri7_route = dsp::triangleSimpleUnipolar(y_norm * 7.0f) * 2.0f - 1.0f;
		skyLfoRouting_ = std::clamp(y_norm + tri7_route * 0.15f, 0.0f, 1.0f);

		// Owl mode: D2 echo tap - 10 periods, 50% duty cycle
		owlEchoGain_ = owlMode_ ? y_norm * 0.5f * dsp::triangleSimpleUnipolar(y_norm * 10.0f, 0.5f) : 0.0f;

		// Owl mode: Z3 controls max boost for ratio-based feedback
		// Low Z3: no boost (unity only), High Z3: more boost allowed
		// Envelope attack rate controlled by predelay knob
		if (owlMode_) {
			owlZ3Norm_ = y_norm;                    // Store Z3 position for ratio control max boost
			owlEnvRatio_ = 0.5f + predelay_ * 1.5f; // Range: 0.5 to 2.0
		}
	}
	else if (cascadeDoubleUndersample_) {
		float tri13 = dsp::triangleSimpleUnipolar(y_norm * 13.0f) * 2.0f - 1.0f;
		modDepth_ = std::clamp(0.3f + tri13 * 0.25f, 0.0f, 0.6f);
	}
	else {
		modDepth_ = 0.0f;
	}

	// Width breathing - 11 periods (fast), z3 increases stereo width
	float tri11 = dsp::triangleSimpleUnipolar(y_norm * 11.0f) * 2.0f - 1.0f;
	widthBreath_ = std::clamp(0.6f + y_norm * 0.5f + tri11 * 0.35f, 0.3f, 1.4f);

	// Cross-channel bleed - 9 periods, L↔R mixing in FDN feedback
	float tri9 = dsp::triangleSimpleUnipolar(y_norm * 9.0f) * 2.0f - 1.0f;
	float base_bleed = y_norm * 0.3f; // Increased for better L/R balance
	crossBleed_ = std::clamp(base_bleed + tri9 * 0.1f, 0.0f, 0.4f);

	// FDN feedback scale - keep at unity (room knob already controls overall feedback)
	// Z3 only affects tonal balance via feedbackMult_, not overall decay
	fdnFeedbackScale_ = 1.0f;

	// Per-stage cascade allpass coefficients - higher coeffs = more diffusion (less slapback)
	// Each stage uses different triangle period for variety
	// Extended modes (Feather/Sky/Lush/Vast) need higher coefficients for proper diffusion
	bool extended_mode = cascadeDoubleUndersample_ || skyChainMode_ || featherMode_;
	float coeff_base = extended_mode ? (0.5f + y_norm * 0.12f)  // Extended modes: 0.5-0.62
	                                 : (0.32f + y_norm * 0.2f); // Normal: 0.32-0.52

	// C0: 8 periods - fastest stage, moderate variation
	float tri8 = dsp::triangleSimpleUnipolar(y_norm * 8.0f) * 2.0f - 1.0f;
	float c0_min = extended_mode ? 0.45f : 0.28f;
	cascadeCoeffs_[0] = std::clamp(coeff_base + tri8 * 0.08f, c0_min, 0.75f);

	// C1: 6 periods - slightly more variation
	float tri6 = dsp::triangleSimpleUnipolar(y_norm * 6.0f) * 2.0f - 1.0f;
	float c1_min = extended_mode ? 0.45f : 0.28f;
	cascadeCoeffs_[1] = std::clamp(coeff_base + tri6 * 0.1f, c1_min, 0.75f);

	// C2: 4 periods - slower variation, slightly higher base for density
	float tri4 = dsp::triangleSimpleUnipolar(y_norm * 4.0f) * 2.0f - 1.0f;
	float c2_min = extended_mode ? 0.48f : 0.30f;
	cascadeCoeffs_[2] = std::clamp(coeff_base + 0.05f + tri4 * 0.1f, c2_min, 0.78f);

	// C3: 3 periods - longest stage, highest base for maximum diffusion
	float tri3 = dsp::triangleSimpleUnipolar(y_norm * 3.0f) * 2.0f - 1.0f;
	float c3_min = extended_mode ? 0.50f : 0.32f;
	cascadeCoeffs_[3] = std::clamp(coeff_base + 0.08f + tri3 * 0.12f, c3_min, 0.82f);
}

} // namespace deluge::dsp::reverb
