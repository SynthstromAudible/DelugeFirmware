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
	const float hp_coeff = owlMode_ ? (0.997f - hpCutoff_ * 0.05f) : (0.995f - hpCutoff_ * 0.09f);
	const float out_lp_coeff = 0.1f + lpCutoff_ * 0.85f;
	const float tail_feedback = feedback_ * feedback_; // Hoist: tail decays faster than early

	// Owl mode feedback constants (hoisted - don't change per sample)
	const float owl_fb_delta = feedback_ - 0.32f;
	const float owl_room_norm = owl_fb_delta * 8.333f;
	const float owl_fdn_fb = 0.32f + owl_fb_delta * 0.7f;
	const float owl_cascade_fb = 0.32f + owl_fb_delta * 2.0f;
	const float owl_global_fb_base = 0.9f + owl_room_norm * 0.4f;
	const float owl_global_fb_coeff = (owl_global_fb_base + cascadeNestFeedback_) * skyLoopFb_;

	// Owl servo: ratio-based feedback limiting (every buffer)
	float owl_delay_scale = 1.0f;
	if (owlMode_) {
		float ratio = feedbackEnvelope_ / std::max(inputAccum_, 1e-6f);
		float knee_ratio = 0.1f + (owlZ3Norm_ * 0.1f);
		constexpr float kBandwidth = 0.001f;
		float excess = std::max(0.0f, ratio - knee_ratio - kBandwidth);
		float deficit = std::max(0.0f, knee_ratio - kBandwidth - ratio);
		constexpr float kLimitStrength = 10.0f;
		constexpr float kBoostStrength = 10.0f;
		float denom = 1.0f + excess * kLimitStrength;
		float numer = 1.0f + deficit * kBoostStrength;
		float target_scale = (numer) / (denom);
		target_scale = std::clamp(target_scale, 0.001f, 10.0f);
		owlFbEnvScale_ += (target_scale - owlFbEnvScale_) * 0.003f; // Very slow smoothing to avoid servo oscillations
	}
	// Owl-specific buffer-rate calculations (skip entirely when not in Owl mode)
	float owl_eff_fb = 0.0f;
	float owl_global_fb = 0.0f;
	float owl_read_cache_scale = 0.5f;
	if (owlMode_) {
		owl_delay_scale = owlFbEnvScale_ * owlFbEnvScale_; // Squared for faster choke
		owl_eff_fb = owl_fdn_fb * fdnFeedbackScale_ * 1.8f * owl_delay_scale;
		owl_global_fb = owl_global_fb_coeff * owlFbEnvScale_;
		owl_read_cache_scale = owlFbEnvScale_ * 0.5f;
	}

	// Sky/Vast shared feedback constants (hoisted - don't change per sample)
	float chain_global_fb_base = 0.0f;
	float chain_c2_fb_base = 0.0f;
	float chain_c3_fb_base = 0.0f;
	if (skyChainMode_ || vastChainMode_) {
		const float chain_loop_fb_base = feedback_ * 0.4f * delayRatio_ * skyLoopFb_;
		chain_global_fb_base = cascadeNestFeedback_ * 0.8f;
		chain_c2_fb_base = chain_loop_fb_base * (1.6f - skyFbBalance_ * 1.4f);
		chain_c3_fb_base = chain_loop_fb_base * (0.2f + skyFbBalance_ * 1.4f);
	}

	// Normal mode hoisted constant
	const float cascade_fb_mult = tail_feedback * cascadeFeedbackMult_;

	// LFO for Sky/Vast/Owl (every buffer - striding causes tonal artifacts)
	float lfo_tri_cached = skyRandWalkSmooth_;
	float d0_mod_cached = 0.0f;
	float d1_mod_cached = 0.0f;
	if (skyChainMode_ || vastChainMode_ || owlMode_) {
		const size_t num_steps = input.size() / 2;
		skyRandState_ = skyRandState_ * 1664525u + 1013904223u;
		float rand_step = (static_cast<float>(skyRandState_ >> 16) / 32768.0f - 1.0f);
		float step_size = 0.001f * skyLfoFreq_ * static_cast<float>(num_steps);
		skyRandWalk_ += rand_step * step_size;
		skyRandWalk_ *= 0.97f;
		skyRandWalk_ = std::clamp(skyRandWalk_, -1.0f, 1.0f);
		float smooth_coeff = vastChainMode_ ? 0.012f * skyLfoFreq_ : 0.025f * skyLfoFreq_;
		float buffer_smooth = std::min(1.0f, smooth_coeff * static_cast<float>(num_steps));
		skyRandWalkSmooth_ += buffer_smooth * (skyRandWalk_ - skyRandWalkSmooth_);
		lfo_tri_cached = skyRandWalkSmooth_;
		// Compute modulation offsets as floats for interpolation (Sky/Vast only)
		if (!owlMode_) {
			float pitch_scale = 1.0f - skyLfoRouting_;
			d0_mod_cached = std::max(0.0f, lfo_tri_cached * modDepth_ * pitch_scale);
			d1_mod_cached = std::max(0.0f, -lfo_tri_cached * modDepth_ * pitch_scale);
		}
	}

	// Flag for FDN pitch interpolation (only Sky/Vast use it - Owl and normal don't)
	const bool needs_pitch_interp = skyChainMode_ || vastChainMode_;

	// Owl filter coefficients (buffer-rate - depend on cached LFO)
	float owl_mono_coeff = kCascadeLpCoeffMono;
	float owl_side_coeff = kCascadeLpCoeffSide;
	float owl_amp_mod_l = 1.0f;
	float owl_amp_mod_r = 1.0f;
	if (owlMode_) {
		float filter_mod = lfo_tri_cached * skyLfoAmp_ * skyLfoRouting_ * 0.5f;
		owl_mono_coeff = std::clamp(kOwlLpCoeffMono - filter_mod, 0.2f, 0.85f);
		owl_side_coeff = std::clamp(kOwlLpCoeffSide + filter_mod * 1.5f, 0.4f, 0.99f);
	}
	// Hoisted values to eliminate in-loop ternaries
	const float owl_c2_scale = owlMode_ ? owlFbEnvScale_ : 1.0f;
	const float cascade_lp_coeff_mono = owlMode_ ? owl_mono_coeff : kCascadeLpCoeffMono;
	const float cascade_lp_coeff_side = owlMode_ ? owl_side_coeff : kCascadeLpCoeffSide;
	const float cascade_amp_mod_val = owlMode_ ? (skyLfoAmp_ * skyLfoRouting_) : cascadeAmpMod_;
	// Owl: aggregate decay for inputAccum_ (~12s at 22kHz, applied once per buffer)
	// Linear approx: (1-x)^n ≈ 1 - n*x for small x (avoids std::pow)
	const float input_accum_decay = 1.0f - (owlMode_ ? 5e-6f * static_cast<float>(input.size()) : 0.0f);

	// Sky/Vast feedback envelope scaling and amplitude modulation (buffer-rate)
	float chain_global_fb = 0.0f;
	float chain_c2_fb = 0.0f;
	float chain_c3_fb = 0.0f;
	float chain_lfo_out = 0.0f;
	float chain_amp_mod_l = 1.0f;
	float chain_amp_mod_r = 1.0f;
	if (skyChainMode_ || vastChainMode_) {
		const float chain_fb_env_scale = 1.0f - std::min(feedbackEnvelope_ * 3.0f, 1.0f);
		chain_global_fb = chain_global_fb_base * chain_fb_env_scale;
		chain_c2_fb = chain_c2_fb_base * chain_fb_env_scale;
		chain_c3_fb = chain_c3_fb_base * chain_fb_env_scale;
		chain_lfo_out = lfo_tri_cached * skyLfoAmp_ * skyLfoRouting_;
		chain_amp_mod_l = 1.0f + chain_lfo_out * 0.3f;
		chain_amp_mod_r = 1.0f - chain_lfo_out * 0.3f;
	}

	// Stereo rotation coefficients (buffer-rate, small-angle approx)
	const float rot_sin_a = lfo_tri_cached * 0.5f;
	const float rot_cos_a = 1.0f - rot_sin_a * rot_sin_a * 0.5f;

	// Owl: buffer-rate modulation values (skip entirely when not in Owl mode)
	int32_t tap_mod_l = 0;
	int32_t tap_mod_r = 0;
	float owl_fb_lfo_mod = 1.0f;
	float owl_cascade_fb_mod = 1.0f;
	float owl_d2_read_mod = 1.0f;
	float owl_write_scale = 0.0f;
	float owl_h2_scale = 0.0f;
	if (owlMode_) {
		tap_mod_l = static_cast<int32_t>(lfo_tri_cached * 280.0f);
		tap_mod_r = -tap_mod_l;
		const float abs_lfo_tri = std::abs(lfo_tri_cached);
		owl_fb_lfo_mod = 1.0f - lfo_tri_cached * 0.3f;
		owl_cascade_fb_mod = (1.3f + abs_lfo_tri * 0.3f) * owlFbEnvScale_;
		owl_d2_read_mod = 1.0f - abs_lfo_tri * 0.3f;
		owl_write_scale = owl_eff_fb * owl_fb_lfo_mod;
		owl_h2_scale = owl_fdn_fb * 0.95f * owl_delay_scale;
	}

	// Cache matrix
	const auto& m = matrix_;

	for (size_t frame = 0; frame < input.size(); ++frame) {
		// === Full-rate: HPF, envelope, predelay ===
		float in = static_cast<float>(input[frame]) * kInputScale;
		float hp_out = in - hpState_;
		hpState_ += (1.0f - hp_coeff) * hp_out;

		// Save input for dry subtraction BEFORE gain boost
		float in_orig = hp_out;

		// +3dB input gain when dry subtraction enabled (linked to DRY- toggle)
		in = dryMinus_ ? hp_out * 1.414f : hp_out;

		// Predelay (single tap)
		if (predelayLength_ > 0) {
			writePredelay(in);
			in = readPredelay(predelayLength_);
		}

		float out_l, out_r;

		// === 2x Undersampling ===
		accumIn_ += in;

		if (undersamplePhase_) {
			float fdn_in = accumIn_ * 0.5f;
			accumIn_ = 0.0f;

			// Peak hold with reset after sustained silence
			// Scale by sqrt(2) to approximate peak-to-RMS ratio
			constexpr float kPeakToRmsScale = 1.414f;
			constexpr float kNoiseFloor = 1e-6f;
			constexpr uint8_t kSilenceThreshold = 64; // ~1.5ms at 44.1kHz/2x undersample
			static uint8_t silenceCount = 0;

			float abs_fdn_in = std::abs(fdn_in) * kPeakToRmsScale;
			if (abs_fdn_in > kNoiseFloor) {
				// Above noise floor - reset silence counter
				silenceCount = 0;
				if (inputPeakReset_) {
					// Dirty flag set - allow new peak even if lower
					// But require significant signal, not just noise (1e-4 ~= -40dB)
					if (abs_fdn_in > 1e-4f) {
						inputAccum_ = abs_fdn_in;
						inputPeakReset_ = false;
					}
				}
				else if (abs_fdn_in > inputAccum_) {
					// New peak - track with smoothing
					inputAccum_ += (abs_fdn_in - inputAccum_) * 0.1f;
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
			const float lfo_tri = lfo_tri_cached;
			const float d0Mod = d0_mod_cached;
			const float d1Mod = d1_mod_cached;

			// Read FDN delays - use interpolation only for Sky/Vast pitch mod (avoids overhead in normal/Owl)
			float d0, d1;
			if (needs_pitch_interp) {
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
				d2 *= owl_d2_read_mod; // 0.7 to 1.0, decorrelated from D0/D1
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

			float effective_feedback = feedback_ * fdnFeedbackScale_;

			// Damping + feedback
			h0 = onepole(h0, fdnLpState_[0], dampCoeff_) * effective_feedback * feedbackMult_[0];
			h1 = onepole(h1, fdnLpState_[1], dampCoeff_) * effective_feedback * feedbackMult_[1];
			h2 = onepole(h2, fdnLpState_[2], dampCoeff_) * effective_feedback * feedbackMult_[2];

			// DC blocking on FDN
			float dc_sum = (h0 + h1 + h2) * 0.333f;
			dcBlockState_ += 0.007f * (dc_sum - dcBlockState_);
			h0 -= dcBlockState_;
			h1 -= dcBlockState_;
			h2 -= dcBlockState_;

			// === Cascade: 4-stage with parallel/series blend ===
			// c0→c1→c2 always series, c3 input blends between cascade_in (parallel) and c2 (series)
			// seriesMix=0 → 9 paths (sparse), seriesMix=1 → 16 paths (dense)
			float cascade_in;
			if (kBypassFdnToCascade) {
				cascade_in = fdn_in * 1.4f + prevC3Out_ * cascadeNestFeedback_ * tail_feedback;
			}
			else {
				// 50% dry + 50% FDN to cascade
				cascade_in = fdn_in * 0.7f + (d0 + d1 + d2) * 0.7f + prevC3Out_ * cascadeNestFeedback_ * tail_feedback;
			}

			// c0→c1→c2 series chain with optional 4x undersample for vast rooms
			float c0, c1, c2;
			float cascade_out_l, cascade_out_r;
			float dynamic_width = width_ * widthBreath_; // Z3-controlled width variation

			if (vastChainMode_) {
				// === VAST CHAIN MODE (Smeared feedback like Sky) ===
				// Topology with smeared feedback loops + global recirculation:
				//   Input → C0 → D0 → C1 → D1 → C2 → D2 → C3 → output
				//                 ↑         ↑         ↑
				//            (C2→C0)    (C3→C1)    (C3)
				// Feedback passes through cascade stages for diffusion before entering delays
				// This reduces comb filter sensitivity compared to direct feedback

				// Pre-decimation AA filter with global C3 feedback (hoisted coeffs)
				float chain_in =
				    onepole(fdn_in * 1.4f + prevC3Out_ * chain_global_fb, cascadeAaState1_, kPreCascadeAaCoeff);

				// C0 with 4x undersample (input → first allpass)
				c0Accum_ += chain_in;
				if (c0Phase_ == 1) {
					float avg_in = c0Accum_ * 0.5f;
					c0Prev_ = processCascadeStage(0, avg_in);
					// Smeared feedback: C2 → C0 → D0 (Z1 controls balance)
					float c2Smeared = processCascadeStage(0, c2Prev_ * chain_c2_fb);
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
					float avg_in = c1Accum_ * 0.5f;
					c1Prev_ = processCascadeStage(1, avg_in);
					// Smeared feedback: C3 → C1 → D1 (Z1 controls balance)
					float c3Smeared = processCascadeStage(1, c3Prev_ * chain_c3_fb);
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
					float avg_in = c2Accum_ * 0.5f;
					float c2Coeff = cascadeCoeffs_[2];
					size_t orig_write_pos = cascadeWritePos_[2];
					size_t read_pos = orig_write_pos;
					size_t idx = cascadeOffsets_[2] + read_pos;
					float delayed = buffer_[idx];
					float output = -c2Coeff * avg_in + delayed;
					float write_val = avg_in + c2Coeff * output;
					buffer_[cascadeOffsets_[2] + orig_write_pos] = write_val;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					buffer_[cascadeOffsets_[2] + cascadeWritePos_[2]] = write_val;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					if (cascadeDoubleUndersample_) {
						size_t tap_pos = (orig_write_pos + kMultiTapOffsets[2]) % cascadeLengths_[2];
						buffer_[cascadeOffsets_[2] + tap_pos] += write_val * kMultiTapGain;
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
					float avg_in = c3Accum_ * 0.5f;
					float c3Coeff = cascadeCoeffs_[3];
					size_t orig_write_pos = cascadeWritePos_[3];
					size_t read_pos = orig_write_pos;
					size_t idx = cascadeOffsets_[3] + read_pos;
					float delayed = buffer_[idx];
					float output = -c3Coeff * avg_in + delayed;
					float write_val = avg_in + c3Coeff * output;
					buffer_[cascadeOffsets_[3] + orig_write_pos] = write_val;
					if (++cascadeWritePos_[3] >= cascadeLengths_[3])
						cascadeWritePos_[3] = 0;
					buffer_[cascadeOffsets_[3] + cascadeWritePos_[3]] = write_val;
					if (++cascadeWritePos_[3] >= cascadeLengths_[3])
						cascadeWritePos_[3] = 0;
					if (cascadeDoubleUndersample_) {
						size_t tap_pos = (orig_write_pos + kMultiTapOffsets[3]) % cascadeLengths_[3];
						buffer_[cascadeOffsets_[3] + tap_pos] += write_val * kMultiTapGain;
					}
					c3Prev_ = output;
					c3Accum_ = 0.0f;
				}
				// Cache LFO value when cascade stages update to avoid discontinuities
				if (c3Phase_ == 0) {
					vastLfoCache_ = lfo_tri; // Update cache when c3 finishes updating
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
				c2 *= (1.0f + chain_lfo_out);
				c3 *= (1.0f - chain_lfo_out);

				// Mix chain outputs - stereo from well-matched early stages only
				// C0(773) and C1(997) are ~30% different - good for balanced stereo
				// C2(1231) and C3(4001) are 3x different - mono only to avoid L/R imbalance
				float cascade_mono = (c2 + c3) * 0.5f;
				float cascade_side = (c0 - c1) * cascade_side_gain_ * dynamic_width;
				cascade_mono = onepole(cascade_mono, cascadeLpStateMono_, kCascadeLpCoeffMono);
				cascade_side = onepole(cascade_side, cascadeLpStateSide_, kCascadeLpCoeffSide);
				cascade_mono = onepole(cascade_mono, cascadeLpState_, cascadeDamping_);
				cascade_out_l = cascade_mono + cascade_side;
				cascade_out_r = cascade_mono - cascade_side;
				// LFO output modulation: stereo movement (buffer-rate)
				cascade_out_l *= chain_amp_mod_l;
				cascade_out_r *= chain_amp_mod_r;

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
				float chain_in = fdn_in * 1.4f + prevC3Out_ * chain_global_fb;

				// C0 at 2x rate
				c0 = processCascadeStage(0, chain_in);

				// D0 delay - feedback from C2 smeared through C0 (interpolated pitch mod)
				// Z1 controls balance: low Z1 = more C2→C0, high Z1 = more C3→C1
				float c2Smeared = processCascadeStage(0, c2Prev_ * chain_c2_fb);
				fdnWrite(0, c0 + c2Smeared);
				float d0Out = fdnReadAtInterp(0, d0Mod);

				// C1 at 2x rate
				c1 = processCascadeStage(1, d0Out);

				// D1 delay - feedback from C3 smeared through C1 (interpolated pitch mod)
				float c3Smeared = processCascadeStage(1, c3Prev_ * chain_c3_fb);
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
				c2 *= (1.0f + chain_lfo_out);
				c3 *= (1.0f - chain_lfo_out);

				// Ping-pong stereo: C0,C2→L, C1→R, C3→center
				float cascade_mono = c3;
				float cascade_side = (c0 + c2 * 0.7f - c1) * cascade_side_gain_ * dynamic_width;
				cascade_mono = onepole(cascade_mono, cascadeLpState_, cascadeDamping_);
				cascade_out_l = cascade_mono + cascade_side;
				cascade_out_r = cascade_mono - cascade_side;
				// Stereo rotation following LFO (buffer-rate coefficients)
				float temp_l = cascade_out_l * rot_cos_a - cascade_out_r * rot_sin_a;
				cascade_out_r = cascade_out_l * rot_sin_a + cascade_out_r * rot_cos_a;
				cascade_out_l = temp_l;

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
				float cascade_inWithFb = cascade_in + cascadeFb;

				float c_l1, c_r1;
				if (cascadeDoubleUndersample_) {
					// === OWL: 4x undersample on cascades with multi-tap density ===
					// Pre-decimation AA filter
					cascade_inWithFb = onepole(cascade_inWithFb, cascadeAaState1_, kPreCascadeAaCoeff);

					// Accumulate for L cascade (C0→C1)
					c0Accum_ += cascade_inWithFb;
					if (++c0Phase_ >= 2) {
						c0Phase_ = 0;
						float c_l0 = processCascadeStage(0, c0Accum_ * 0.5f);
						// Cross-channel multi-tap: L writes to R's buffer (C0 → C2)
						size_t prev_pos_0 =
						    (cascadeWritePos_[0] == 0) ? cascadeLengths_[0] - 1 : cascadeWritePos_[0] - 1;
						float write_val0 = buffer_[cascadeOffsets_[0] + prev_pos_0];
						size_t tap_off_0 = static_cast<size_t>(static_cast<int32_t>(kMultiTapOffsets[0]) + tap_mod_l);
						size_t tap_pos0 = (prev_pos_0 + tap_off_0) % cascadeLengths_[2];
						buffer_[cascadeOffsets_[2] + tap_pos0] += write_val0 * kMultiTapGain;

						c0Accum_ = 0.0f;
						c1Accum_ += c_l0;
						if (++c1Phase_ >= 2) {
							c1Phase_ = 0;
							c_l1 = processCascadeStage(1, c1Accum_ * 0.5f);
							// Cross-channel multi-tap: L writes to R's buffer (C1 → C3)
							size_t prev_pos_1 =
							    (cascadeWritePos_[1] == 0) ? cascadeLengths_[1] - 1 : cascadeWritePos_[1] - 1;
							float write_val1 = buffer_[cascadeOffsets_[1] + prev_pos_1];
							size_t tap_off_1 =
							    static_cast<size_t>(static_cast<int32_t>(kMultiTapOffsets[1]) + tap_mod_l);
							size_t tap_pos1 = (prev_pos_1 + tap_off_1) % cascadeLengths_[3];
							buffer_[cascadeOffsets_[3] + tap_pos1] += write_val1 * kMultiTapGain;

							c1Accum_ = 0.0f;
							c1Prev_ = c_l1;
						}
						else {
							c_l1 = c1Prev_;
						}
						c0Prev_ = c_l1;
					}
					else {
						c_l1 = c0Prev_;
					}

					// Accumulate for R cascade (C2→C3)
					float cascade_inR = cascade_inWithFb * 0.98f + (d0 - d1) * 0.02f;
					c2Accum_ += cascade_inR;
					if (++c2Phase_ >= 2) {
						c2Phase_ = 0;
						float c_r0 = processCascadeStage(2, c2Accum_ * 0.5f);
						// Cross-channel multi-tap: R writes to L's buffer (C2 → C0)
						size_t prev_pos_2 =
						    (cascadeWritePos_[2] == 0) ? cascadeLengths_[2] - 1 : cascadeWritePos_[2] - 1;
						float write_val2 = buffer_[cascadeOffsets_[2] + prev_pos_2];
						size_t tap_off_2 = static_cast<size_t>(static_cast<int32_t>(kMultiTapOffsets[2]) + tap_mod_r);
						size_t tap_pos2 = (prev_pos_2 + tap_off_2) % cascadeLengths_[0];
						buffer_[cascadeOffsets_[0] + tap_pos2] += write_val2 * kMultiTapGain;

						c2Accum_ = 0.0f;
						c3Accum_ += c_r0;
						if (++c3Phase_ >= 2) {
							c3Phase_ = 0;
							c_r1 = processCascadeStage(3, c3Accum_ * 0.5f);
							// Cross-channel multi-tap: R writes to L's buffer (C3 → C1)
							size_t prev_pos_3 =
							    (cascadeWritePos_[3] == 0) ? cascadeLengths_[3] - 1 : cascadeWritePos_[3] - 1;
							float write_val3 = buffer_[cascadeOffsets_[3] + prev_pos_3];
							size_t tap_off_3 =
							    static_cast<size_t>(static_cast<int32_t>(kMultiTapOffsets[3]) + tap_mod_r);
							size_t tap_pos3 = (prev_pos_3 + tap_off_3) % cascadeLengths_[1];
							buffer_[cascadeOffsets_[1] + tap_pos3] += write_val3 * kMultiTapGain;

							c3Accum_ = 0.0f;
							c3Prev_ = c_r1;
						}
						else {
							c_r1 = c3Prev_;
						}
						c2Prev_ = c_r1;
					}
					else {
						c_r1 = c2Prev_;
					}
				}
				else {
					// === FEATHER: Serial cascade with full stereo ===
					float c0 = processCascadeStage(0, cascade_inWithFb);
					float c1 = processCascadeStage(1, c0);
					float c2 = processCascadeStage(2, c1);
					float c3 = processCascadeStage(3, c2);

					// Ping-pong stereo: C0,C2→L, C1→R, C3→center
					float cascade_mono = c3;
					float cascade_side = (c0 + c2 * 0.7f - c1) * cascade_side_gain_ * dynamic_width;
					cascade_mono = onepole(cascade_mono, cascadeLpState_, cascadeDamping_);
					c_l1 = cascade_mono + cascade_side;
					c_r1 = cascade_mono - cascade_side;
				}

				cascade_out_l = c_l1;
				cascade_out_r = c_r1;
				// Stereo rotation following LFO (buffer-rate coefficients)
				float temp_l = cascade_out_l * rot_cos_a - cascade_out_r * rot_sin_a;
				cascade_out_r = cascade_out_l * rot_sin_a + cascade_out_r * rot_cos_a;
				cascade_out_l = temp_l;
				prevC3Out_ = cascade_out_l + cascade_out_r;

				// Keep early reflections from FDN
				if (!kMuteEarly) {
					float early_mid = (d0 + d1) * earlyMixGain_;
					// Owl mode: narrow early (60%) for focused transients, late cascade provides width
					float early_width_scale = cascadeDoubleUndersample_ ? 0.6f : 1.0f;
					float early_side = (d0 - d1) * earlyMixGain_ * dynamic_width * early_width_scale;
					directEarlyL_ = (early_mid + early_side) * directEarlyGain_;
					directEarlyR_ = (early_mid - early_side) * directEarlyGain_;
				}
				else {
					directEarlyL_ = 0.0f;
					directEarlyR_ = 0.0f;
				}

				// Inject input into FDN (no cascade feedback for cleaner separation)
				h0 += fdn_in;

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
				cascade_in = onepole(cascade_in, cascadeAaState1_, kPreCascadeAaCoeff);

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
					fdnFb = owl_fdn_fb;
					cascadeFb = owl_cascade_fb;

					// Accumulate D0/D1/D2 reads for 4x AA (envelope scale moved to 4x cache averaging)
					owlD0ReadAccum_ += d0;
					owlD1ReadAccum_ += d1;
					// Envelope tracking moved to buffer rate for performance

					// Cheap HPF on feedback to tame LF rumble (~100Hz at 11kHz effective rate)
					c3ForFb = prevC3Out_ - dcBlockState_;
					dcBlockState_ += 0.057f * c3ForFb;
					cascade_in = owlD0Cache_ + c3ForFb * owl_global_fb;

					// D0 write: input - envelope limited + LFO modulated (pre-combined scale)
					owlD0WriteAccum_ += fdn_in * owl_write_scale;
					// D1 write will be set from C0 output below
					// h2 = C3 for nested feedback (accumulated and written at 4x rate)
				}

				// C0 with 4x undersample - fed by D0
				c0Accum_ += cascade_in;
				if (c0Phase_ == 1) {
					float avg_in = c0Accum_ * 0.5f;
					c0Prev_ = processCascadeStage(0, avg_in);
					// Double write for 4x undersample
					if (cascadeWritePos_[0] == 0) {
						cascadeWritePos_[0] = cascadeLengths_[0] - 1;
					}
					else {
						--cascadeWritePos_[0];
					}
					processCascadeStage(0, avg_in);
					c0Accum_ = 0.0f;
				}
				c0Phase_ = (c0Phase_ + 1) & 1;
				c0 = c0Prev_;

				// Owl: C0 → D1 → C1 (D1 sits between C0 and C1)
				if (owlMode_) {
					// Accumulate C0 output for D1 write (pre-combined scale)
					owlD1WriteAccum_ += c0 * owl_write_scale;
				}

				// C1 with 4x undersample - fed by D1 (Owl) or C0 (Lush)
				float c1Input = owlMode_ ? owlD1Cache_ : c0;
				c1Accum_ += c1Input;
				if (c1Phase_ == 1) {
					float avg_in = c1Accum_ * 0.5f;
					c1Prev_ = processCascadeStage(1, avg_in);
					if (cascadeWritePos_[1] == 0) {
						cascadeWritePos_[1] = cascadeLengths_[1] - 1;
					}
					else {
						--cascadeWritePos_[1];
					}
					processCascadeStage(1, avg_in);
					c1Accum_ = 0.0f;
				}
				c1Phase_ = (c1Phase_ + 1) & 1;
				c1 = c1Prev_;

				// C2 with 4x undersample + pitch modulation
				c2Accum_ += c1 * owl_c2_scale;
				if (c2Phase_ == 1) {
					float avg_in = c2Accum_ * 0.5f;
					float c2Coeff = cascadeCoeffs_[2];
					size_t orig_write_pos = cascadeWritePos_[2];
					// Pitch mod with subtraction wrap (no modulo - much cheaper)
					size_t c2_mod_offset = static_cast<size_t>(std::max(0.0f, lfo_tri * cascadeModDepth_));
					size_t read_pos = orig_write_pos + c2_mod_offset;
					if (read_pos >= cascadeLengths_[2])
						read_pos -= cascadeLengths_[2];
					float delayed = buffer_[cascadeOffsets_[2] + read_pos];
					float output = -c2Coeff * avg_in + delayed;
					float write_val = avg_in + c2Coeff * output;
					buffer_[cascadeOffsets_[2] + orig_write_pos] = write_val;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					buffer_[cascadeOffsets_[2] + cascadeWritePos_[2]] = write_val;
					if (++cascadeWritePos_[2] >= cascadeLengths_[2])
						cascadeWritePos_[2] = 0;
					// Multi-tap write with subtraction wrap (cheaper than modulo)
					if (cascadeDoubleUndersample_) {
						size_t tap_pos = orig_write_pos + kMultiTapOffsets[2];
						if (tap_pos >= cascadeLengths_[2])
							tap_pos -= cascadeLengths_[2];
						buffer_[cascadeOffsets_[2] + tap_pos] += write_val * kMultiTapGain;
					}
					c2Prev_ = output;
					c2Accum_ = 0.0f;
				}
				c2Phase_ = (c2Phase_ + 1) & 1;
				c2 = c2Prev_;

				// C3 with 4x undersample + inverted pitch modulation + parallel/series blend
				// Owl: D2 inline between C2→C3 (Vast-like topology)
				float c3In = owlMode_ ? owlD2Cache_ : (cascade_in + (c2 - cascade_in) * cascadeSeriesMix_);
				float c3;
				c3Accum_ += c3In;
				if (c3Phase_ == 1) {
					float avg_in = c3Accum_ * 0.5f;
					float c3Coeff = cascadeCoeffs_[3];
					size_t orig_write_pos = cascadeWritePos_[3];
					size_t c3_mod_offset = static_cast<size_t>(std::max(0.0f, -lfo_tri * cascadeModDepth_));
					size_t read_pos = (orig_write_pos + c3_mod_offset) % cascadeLengths_[3];
					size_t idx = cascadeOffsets_[3] + read_pos;
					float delayed = buffer_[idx];
					float output = -c3Coeff * avg_in + delayed;
					float write_val = avg_in + c3Coeff * output;
					buffer_[cascadeOffsets_[3] + orig_write_pos] = write_val;
					if (++cascadeWritePos_[3] >= cascadeLengths_[3])
						cascadeWritePos_[3] = 0;
					buffer_[cascadeOffsets_[3] + cascadeWritePos_[3]] = write_val;
					if (++cascadeWritePos_[3] >= cascadeLengths_[3])
						cascadeWritePos_[3] = 0;
					if (cascadeDoubleUndersample_) {
						size_t tap_pos = (orig_write_pos + kMultiTapOffsets[3]) % cascadeLengths_[3];
						buffer_[cascadeOffsets_[3] + tap_pos] += write_val * kMultiTapGain;
					}
					c3Prev_ = output;
					c3Accum_ = 0.0f;
				}
				// Cache LFO value when cascade stages update to avoid discontinuities
				if (c3Phase_ == 0 && owlMode_) {
					vastLfoCache_ = lfo_tri; // Update cache when c3 finishes updating
				}
				c3Phase_ = (c3Phase_ + 1) & 1;
				c3 = c3Prev_;
				prevC3Out_ = c3;

				// Amplitude modulation for diffusion contour (hoisted cascade_amp_mod_val)
				if (cascade_amp_mod_val != 0.0f) {
					// Owl: use cached LFO (synced to 4x update rate like Vast)
					float lfo_for_amp_mod = owlMode_ ? vastLfoCache_ : lfo_tri;
					c2 *= (1.0f + lfo_for_amp_mod * cascade_amp_mod_val);
					c3 *= (1.0f - lfo_for_amp_mod * cascade_amp_mod_val);
				}

				// Mix cascade outputs - stereo from well-matched early stages only
				// C0(773) and C1(997) are ~30% different - good for balanced stereo
				// C2(1231) and C3(4001) are 3x different - mono only to avoid L/R imbalance
				float cascade_mono = (c2 + c3) * 0.5f;
				float cascade_side = (c0 - c1) * cascade_side_gain_ * dynamic_width;

				// Owl: modulate mid/side filter cutoffs for stereo movement (buffer-rate coeffs)
				cascade_mono = onepole(cascade_mono, cascadeLpStateMono_, cascade_lp_coeff_mono);
				cascade_side = onepole(cascade_side, cascadeLpStateSide_, cascade_lp_coeff_side);
				cascade_mono = onepole(cascade_mono, cascadeLpState_, cascadeDamping_);
				cascade_out_l = cascade_mono + cascade_side;
				cascade_out_r = cascade_mono - cascade_side;

				// Owl: LFO output modulation for stereo movement (buffer-rate)
				if (owlMode_) {
					cascade_out_l *= owl_amp_mod_l;
					cascade_out_r *= owl_amp_mod_r;

					// Stereo crossfeed to even out channels over time
					float cross_l = cascade_out_l + crossBleed_ * cascade_out_r;
					float cross_r = cascade_out_r + crossBleed_ * cascade_out_l;
					cascade_out_l = cross_l;
					cascade_out_r = cross_r;

					// D2 echo tap - distinct repeat before C3 diffuses it
					float echo_tap = owlD2Cache_ * owlEchoGain_;
					cascade_out_l += echo_tap;
					cascade_out_r += echo_tap;
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
					owlD2WriteAccum_ += c2 * owl_h2_scale;
					// AA filter on input before FDN (prevents aliasing at 4x rate)
					float fdn_inAA = onepole(fdn_in, owlInputAaState_, kPreCascadeAaCoeff);
					owlD0WriteAccum_ += fdn_inAA;
					if (c3Phase_ == 0) {
						// c3 just updated - average + envelope scale all accumulated reads/writes
						owlD0Cache_ = owlD0ReadAccum_ * owl_read_cache_scale;
						owlD1Cache_ = owlD1ReadAccum_ * owl_read_cache_scale;
						owlD2Cache_ = owlD2ReadAccum_ * owl_read_cache_scale;
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
					h0 += fdn_in;
				}
				else {
					h0 += fdn_in + cascade_mono * cascade_fb_mult * owl_cascade_fb_mod;
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
					float early_d0 = owlMode_ ? owlD0Cache_ : d0;
					float early_d1 = owlMode_ ? owlD1Cache_ : d1;
					float early_mid = (early_d0 + early_d1) * earlyMixGain_;
					// Owl: wider early reflections (2-delay FDN is more stereo)
					float early_width_mult = owlMode_ ? 1.3f : 1.0f;
					float early_side = (early_d0 - early_d1) * earlyMixGain_ * dynamic_width * early_width_mult;
					directEarlyL_ = (early_mid + early_side) * directEarlyGain_;
					directEarlyR_ = (early_mid - early_side) * directEarlyGain_;
				}
				else {
					directEarlyL_ = 0.0f;
					directEarlyR_ = 0.0f;
				}
			}
			else {
				// === NORMAL FDN + CASCADE MODE ===
				c0 = processCascadeStage(0, cascade_in);
				c1 = processCascadeStage(1, c0);
				c2 = processCascadeStage(2, c1);

				// C3 input: blend parallel (cascade_in) ↔ series (c2)
				float c3In = cascade_in + (c2 - cascade_in) * cascadeSeriesMix_;
				float c3 = processCascadeStage(3, c3In);
				prevC3Out_ = c3;

				// Mix outputs - stereo tail from cascade (mid/side)
				float cascade_mono = (c2 + c3) * 0.5f;
				float cascade_side = (c0 - c1) * cascade_side_gain_ * dynamic_width;
				// Skip extra LPFs for normal mode (only used in Lush/Vast)
				cascade_mono = onepole(cascade_mono, cascadeLpState_, cascadeDamping_);
				cascade_out_l = cascade_mono + cascade_side;
				cascade_out_r = cascade_mono - cascade_side;

				// Inject input + cascade feedback into FDN
				if (kMuteCascadeFeedback) {
					h0 += fdn_in;
				}
				else {
					h0 += fdn_in + cascade_mono * cascade_fb_mult;
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
					float early_mid = (d0 + d1) * earlyMixGain_;
					float early_side = (d0 - d1) * earlyMixGain_ * dynamic_width;
					directEarlyL_ = (early_mid + early_side) * directEarlyGain_;
					directEarlyR_ = (early_mid - early_side) * directEarlyGain_;
				}
				else {
					directEarlyL_ = 0.0f;
					directEarlyR_ = 0.0f;
				}
			}

			// Output: early (FDN) + late (cascade)
			// Nested modes (Sky/Vast) have no early (FDN repurposed), normal modes have early
			float early_l = 0.0f, early_r = 0.0f;
			if (!vastChainMode_ && !skyChainMode_ && !kMuteEarly) {
				float early_mid = (d0 + d1) * earlyMixGain_;
				float early_side = (d0 - d1) * earlyMixGain_ * dynamic_width;
				early_l = early_mid + early_side;
				early_r = early_mid - early_side;
			}

			float new_out_l, new_out_r;
			if (kMuteCascade) {
				new_out_l = early_l;
				new_out_r = early_r;
			}
			else {
				new_out_l = early_l + cascade_out_l * tailMixGain_;
				new_out_r = early_r + cascade_out_r * tailMixGain_;
			}

			// Global wet side boost from width knob (mid/side)
			// width=0: normal stereo, width=1: 2x side boost
			float wet_mid = (new_out_l + new_out_r) * 0.5f;
			float wet_side = (new_out_l - new_out_r) * 0.5f * (1.0f + width_);
			new_out_l = wet_mid + wet_side;
			new_out_r = wet_mid - wet_side;

			prevOutL_ = currOutL_;
			prevOutR_ = currOutR_;
			currOutL_ = new_out_l;
			currOutR_ = new_out_r;

			out_l = currOutL_;
			out_r = currOutR_;
		}
		else {
			// Interpolate
			out_l = (prevOutL_ + currOutL_) * 0.5f;
			out_r = (prevOutR_ + currOutR_) * 0.5f;
		}

		undersamplePhase_ = !undersamplePhase_;

		// prevOutputMono_ = (out_l + out_r) * 0.5f * (1.0f + std::abs(in_orig) * predelay_ * 2.0f);
		prevOutputMono_ = (out_l + out_r) * 0.5f + (std::abs(in_orig) * predelay_ * 2.0f);

		// Output LPF
		out_l = onepole(out_l, lpStateL_, out_lp_coeff);
		out_r = onepole(out_r, lpStateR_, out_lp_coeff);

		// Add direct early brightness tap (bypasses LPF for crisp transients)
		out_l += directEarlyL_;
		out_r += directEarlyR_;

		// Subtract dry input to remove bleedthrough (sparse topology compensation)
		// Toggle via predelay encoder button: DRY- = enabled, DRY+ = disabled
		// in_orig is pre-gain, so subtract at unity
		if (dryMinus_) {
			out_l -= in_orig;
			out_r -= in_orig;
		}

		// Clamp and output
		constexpr float kMaxFloat = 0.06f;
		out_l = std::clamp(out_l, -kMaxFloat, kMaxFloat);
		out_r = std::clamp(out_r, -kMaxFloat, kMaxFloat);
		int32_t out_lq31 = static_cast<int32_t>(out_l * kOutputScale);
		int32_t out_rq31 = static_cast<int32_t>(out_r * kOutputScale);

		output[frame].l += multiply_32x32_rshift32_rounded(out_lq31, getPanLeft());
		output[frame].r += multiply_32x32_rshift32_rounded(out_rq31, getPanRight());
	}

	// Owl: apply aggregate inputAccum_ decay (hoisted from per-sample)
	if (owlMode_) {
		inputAccum_ *= input_accum_decay;
	}

	// Buffer-end envelope update: track amplitude (not squared)
	float out_amp = std::abs(prevOutputMono_);
	float release_coeff = 0.003f;
	if (owlMode_) {
		// Feedback envelope: track output level (every buffer)
		float attack_coeff = 0.008f + predelay_ * 0.019f;
		constexpr float kReleaseCoeff = 0.05f;
		float fb_coeff = (out_amp > feedbackEnvelope_) ? attack_coeff : kReleaseCoeff;
		feedbackEnvelope_ += fb_coeff * (out_amp - feedbackEnvelope_);
	}
	else {
		// Non-Owl modes: original per-buffer tracking
		float attack_coeff = 0.02f;
		float env_coeff = (out_amp > feedbackEnvelope_) ? attack_coeff : release_coeff;
		feedbackEnvelope_ += env_coeff * (out_amp - feedbackEnvelope_);
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
		cascade_side_gain_ = 0.23f; // Slightly wider for spacious modes
	}
	else if (zone == 6) {
		// Owl: FDN + Cascade with 4x undersample, wider stereo tail
		cascadeDamping_ = baseCascadeDamping * 0.6f;
		cascadeModDepth_ = 0.0f; // Disabled - pitch mod causes clicks at 4x undersample
		cascadeAmpMod_ = 0.15f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.25f, 0.0f, 0.55f);
		cascade_side_gain_ = 0.25f; // Wider stereo spread for Owl
	}
	else if (zone == 5) {
		// Sky: Nested topology at 2x undersample (faster response than Vast)
		cascadeDamping_ = baseCascadeDamping * 0.65f;
		cascadeModDepth_ = 8.0f;
		cascadeAmpMod_ = 0.12f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.15f, 0.0f, 0.40f);
		cascade_side_gain_ = 0.23f; // Slightly wider for spacious modes
		modDepth_ = 100.0f;         // D0/D1 pitch wobble default (~4.5ms), Z1 modulates 20-180
	}
	else if (zone == 4) {
		// Feather: Experimental mode - start with normal FDN+cascade, tweak from here
		cascadeDamping_ = baseCascadeDamping * 0.7f;
		cascadeModDepth_ = 4.0f;
		cascadeAmpMod_ = 0.08f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_ + 0.1f, 0.0f, 0.35f);
		cascade_side_gain_ = 0.2f;
	}
	else {
		cascadeDamping_ = baseCascadeDamping;
		cascadeModDepth_ = 0.0f;
		cascadeAmpMod_ = 0.0f;
		cascadeNestFeedback_ = std::clamp(cascadeNestFeedbackBase_, 0.0f, 0.55f);
		cascade_side_gain_ = 0.2f; // Default stereo side gain
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
