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

#include "dsp/phi_triangle.hpp"
#include "dsp/reverb/base.hpp"
#include "memory/general_memory_allocator.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace deluge::dsp::reverb {

/// Featherverb: Lightweight FDN reverb with allpass cascade for dense tails
/// Architecture: 3-delay FDN (early reflections) → 4-stage allpass cascade (tail density)
/// - Memory: ~68KB (vs Freeverb 93KB, Mutable 128KB)
/// - CPU: ~9K cycles with 2x undersampling
/// - 3x3 Hadamard matrix with phi triangle morphing for early character
/// - 4-stage allpass cascade replaces long D3 delay - exponential density buildup
/// - Zone 2 scales both FDN D2 and cascade lengths for room size
class Featherverb : public Base {

	// === Memory allocation strategy ===
	// true = static BSS (77KB always allocated)
	// false = dynamic via allocMaxSpeed() (preferred - frees memory when switching models)
	// Benchmarking showed <1% performance difference between BSS and dynamic.
	// The ~1.8x CPU variation (10k vs 19k cycles) is from synth cache pressure, not allocation.
	static constexpr bool kUseStaticBss = false;

	// 3 FDN delays for early reflections (D0, D1, D2)
	static constexpr size_t kNumFdnDelays = 3;

	// FDN delay lengths (D0/D1 variable from Zone 1, D2 variable from Zone 2)
	// With 2x undersample, effective times are 2x these values
	static constexpr size_t kD0MinLength = 350;  // ~16ms effective
	static constexpr size_t kD0MaxLength = 550;  // ~25ms effective (shrunk to give to D2)
	static constexpr size_t kD1MinLength = 580;  // ~26ms effective
	static constexpr size_t kD1MaxLength = 1500; // ~68ms effective
	static constexpr size_t kD2MinLength = 650;  // ~29ms effective
	static constexpr size_t kD2MaxLength = 2050; // ~93ms effective

	// 4-stage allpass cascade for tail density (replaces long D3)
	// Each stage splits impulses → exponential density growth
	// C3 can run parallel (from cascadeIn) or series (from c2) via cascadeSeriesMix_
	// Prime lengths for good diffusion, scalable by Zone 2
	// With 2x undersampling, effective lengths are 2x these values
	// Vast mode (Zone 2 > 80%): all stages at uniform 4x for extended tails
	static constexpr size_t kNumCascade = 4;
	static constexpr size_t kC0BaseLength = 773;  // ~17.5ms base, ~35ms effective - prime
	static constexpr size_t kC1BaseLength = 997;  // ~22.6ms base, ~45ms effective - prime
	static constexpr size_t kC2BaseLength = 1231; // ~27.9ms base, ~56ms effective - prime
	static constexpr size_t kC3BaseLength = 4001; // ~90ms base, ~181ms @2x, ~362ms @4x - prime
	static constexpr size_t kCascadeBaseTotal = kC0BaseLength + kC1BaseLength + kC2BaseLength + kC3BaseLength; // 7002
	static constexpr float kCascadeMaxScale = 1.8f; // Zone 2 can scale cascade up to 1.8x for vast rooms
	static constexpr size_t kCascadeMaxTotal = static_cast<size_t>(kCascadeBaseTotal * kCascadeMaxScale); // ~12604

	// Multi-tap write offsets (prime numbers for good diffusion)
	// Writes are cheap (pipelined), so add secondary write for doubled impulse density
	static constexpr bool kEnableMultiTapWrites = false; // Disabled for performance (saves ~5% cycles)
	static constexpr std::array<size_t, kNumCascade> kMultiTapOffsets = {311, 401, 509, 1607}; // C0-C3 offsets
	static constexpr float kMultiTapGain = 0.18f; // Gain for secondary tap (low to preserve feedback headroom)

	// Buffer layout: FDN delays + cascade + predelay + diffusers
	static constexpr size_t kFdnMaxSamples = kD0MaxLength + kD1MaxLength + kD2MaxLength; // 4100
	static constexpr size_t kPredelayMaxLength = 2205;                                   // 50ms at 44.1kHz (single tap)
	static constexpr size_t kNumDiffusers = 2;
	static constexpr size_t kDiffuser0Length = 137;
	static constexpr size_t kDiffuser1Length = 211;
	static constexpr size_t kDiffuserTotal = kDiffuser0Length + kDiffuser1Length; // 348

	static constexpr size_t kTotalMaxSamples = kFdnMaxSamples + kCascadeMaxTotal + kPredelayMaxLength + kDiffuserTotal;
	static constexpr size_t kBufferBytes = kTotalMaxSamples * sizeof(float); // ~77KB

public:
	Featherverb();
	~Featherverb() override { deallocate(); }

	[[nodiscard]] bool allocate();
	void deallocate();
	[[nodiscard]] bool isAllocated() const { return buffer_ != nullptr; }

	void process(std::span<int32_t> input, std::span<StereoSample> output) override;

	void setRoomSize(float value) override;
	[[nodiscard]] float getRoomSize() const override { return roomSize_; }

	void setDamping(float value) override;
	[[nodiscard]] float getDamping() const override { return damping_; }

	void setWidth(float value) override;
	[[nodiscard]] float getWidth() const override { return width_; }

	void setHPF(float f) override;
	[[nodiscard]] float getHPF() const override { return hpCutoff_; }

	void setLPF(float f) override;
	[[nodiscard]] float getLPF() const override { return lpCutoff_; }

	// === Zone parameters ===

	/// Zone 1 - Matrix: 3x3 matrix morphing via phi triangles
	void setZone1(int32_t value);
	[[nodiscard]] int32_t getZone1() const { return zone1_; }

	/// Zone 2 - Size: Scales D2 + cascade lengths (room size)
	void setZone2(int32_t value);
	[[nodiscard]] int32_t getZone2() const { return zone2_; }

	/// Zone 3 - Decay: Per-delay feedback character
	void setZone3(int32_t value);
	[[nodiscard]] int32_t getZone3() const { return zone3_; }

	/// Pre-delay: 0-100ms with multi-taps
	void setPredelay(float value);
	[[nodiscard]] float getPredelay() const { return predelay_; }

	/// Diagnostic: cascade-only mode (bypasses FDN, mutes early)
	void setCascadeOnly(bool value) { cascadeOnly_ = value; }
	[[nodiscard]] bool getCascadeOnly() const { return cascadeOnly_; }

private:
	// Buffer storage - controlled by kUseStaticBss
	// Static BSS: 77KB always in fast SRAM, no cache contention
	// Dynamic SDRAM: allocate on demand, subject to cache misses with complex synths
	static inline std::array<float, kTotalMaxSamples> staticBuffer_{}; // Only used if kUseStaticBss
	float* buffer_{kUseStaticBss ? staticBuffer_.data() : nullptr};

	// FDN delay state (3 delays)
	std::array<size_t, kNumFdnDelays> fdnWritePos_{};
	std::array<size_t, kNumFdnDelays> fdnOffsets_{};
	std::array<size_t, kNumFdnDelays> fdnLengths_{kD0MaxLength, kD1MaxLength, kD2MaxLength};
	std::array<float, kNumFdnDelays> fdnLpState_{};

	// Cascade state (4 allpass stages)
	std::array<size_t, kNumCascade> cascadeWritePos_{};
	std::array<size_t, kNumCascade> cascadeOffsets_{};
	std::array<size_t, kNumCascade> cascadeLengths_{kC0BaseLength, kC1BaseLength, kC2BaseLength, kC3BaseLength};
	float cascadeScale_{1.0f};            // Current scale factor from Zone 2
	float earlyMixGain_{0.3f};            // Early reflection gain (scales inverse with Zone 2: smaller = more early)
	float tailMixGain_{0.6f};             // Tail output gain (scales with Zone 2: bigger room = more tail)
	float directEarlyGain_{0.15f};        // Direct early tap (bypasses output LPF for brightness)
	float cascadeLpState_{0.0f};          // LP filter state for cascade output
	float cascadeSeriesMix_{0.6f};        // 0=parallel (C3 from cascadeIn), 1=series (C3 from c2)
	float cascadeFeedbackMult_{0.7f};     // How much cascade feeds back into FDN (controlled by Zone 3)
	float cascadeNestFeedback_{0.0f};     // Nested feedback: C3 → C0 for extended tails (combined)
	float cascadeNestFeedbackBase_{0.0f}; // Base nest feedback from Zone 3 (before Zone 2 vast boost)
	float prevC3Out_{0.0f};               // Previous C3 output for nested feedback delay
	float delayRatio_{1.0f};              // (D0+D1)/(D0max+D1max) for feedback normalization
	static constexpr float kCascadeCoeffBase = 0.5f; // Base allpass coefficient for cascade (higher = more diffusion)
	std::array<float, kNumCascade> cascadeCoeffs_{kCascadeCoeffBase, kCascadeCoeffBase, kCascadeCoeffBase,
	                                              kCascadeCoeffBase};

	// Diffuser state
	std::array<size_t, kNumDiffusers> diffuserOffsets_{};
	std::array<size_t, kNumDiffusers> diffuserWritePos_{};
	static constexpr float kDiffuserCoeff = 0.5f;

	// Predelay state (single tap, 50ms max)
	size_t predelayOffset_{0};
	size_t predelayWritePos_{0};
	size_t predelayLength_{0};
	float prevOutputMono_{0.0f};

	// Parameters
	float roomSize_{0.5f};
	float damping_{0.5f};
	float width_{1.0f};
	float hpCutoff_{0.0f};
	float lpCutoff_{1.0f};

	// Zone parameters
	int32_t zone1_{0};
	int32_t zone2_{512};
	int32_t zone3_{0};
	float predelay_{0.0f};

	// Diagnostic
	bool cascadeOnly_{false}; // Runtime toggle: true = cascade-only (bypasses FDN, mutes early)

	// Derived coefficients
	float feedback_{0.85f};
	float dampCoeff_{0.5f};
	float cascadeDamping_{0.7f}; // Damping in cascade (darker tail)

	// Precomputed hot-path values (updated in setZone2)
	float feedbackFloor_{0.8f};     // kMinFeedbackMult + zone2 blend
	float envReleaseRate_{0.0002f}; // Envelope release rate

	// 3x3 matrix for FDN (normalized Hadamard)
	// H3 = 1/sqrt(3) * [[1,1,1], [1,w,w^2], [1,w^2,w]] where w = e^(2πi/3)
	// Using real approximation: sign pattern with 1/sqrt(3) normalization
	static constexpr float kH3Norm = 0.577350269f; // 1/sqrt(3)
	std::array<std::array<float, 3>, 3> matrix_{{{kH3Norm, kH3Norm, kH3Norm},
	                                             {kH3Norm, -kH3Norm, 0.0f}, // Simplified orthogonal
	                                             {kH3Norm, 0.0f, -kH3Norm}}};

	// Per-delay feedback multipliers
	std::array<float, kNumFdnDelays> feedbackMult_{1.0f, 1.0f, 1.0f};

	// Filter states
	float hpState_{0.0f};
	float lpStateL_{0.0f};
	float lpStateR_{0.0f};
	float dcBlockState_{0.0f};

	// LFO for modulation
	float lfoPhase_{0.0f};
	float modDepth_{0.0f};         // LFO pitch wobble depth for FDN (controlled by Zone 3)
	float cascadeModDepth_{0.0f};  // LFO pitch wobble depth for C2/C3 in vast mode
	float cascadeAmpMod_{0.0f};    // LFO amplitude modulation depth for C2/C3 diffusion contour
	float widthBreath_{0.0f};      // Width breathing amount (controlled by Zone 3)
	float crossBleed_{0.0f};       // L↔R cross-channel bleed in FDN (controlled by Zone 3)
	float fdnFeedbackScale_{1.0f}; // Inverse scale: reduce FDN feedback as Zone 3 (cascade) increases

	// Envelope followers
	float inputEnvelope_{0.0f};
	float feedbackEnvelope_{0.0f}; // Tracks C3 output for self-limiting feedback

	// Undersampling state
	bool undersamplePhase_{false};
	float accumIn_{0.0f};
	float prevOutL_{0.0f};
	float prevOutR_{0.0f};
	float currOutL_{0.0f};
	float currOutR_{0.0f};

	// Cascade extra undersampling for vast rooms
	// Normal: 2x, Lush/Vast: all stages at uniform 4x (8x caused ringing)
	bool cascadeDoubleUndersample_{false}; // When true, cascade runs at 4x undersample (Lush or Vast zones)
	bool vastChainMode_{false};            // When true, uses chain topology (Vast only); false = FDN+cascade (Lush)
	float cascadeAaState1_{0.0f};          // Anti-alias LP filter state (pre-decimation, vast only)
	float cascadeLpStateMono_{0.0f};       // Cascade output LP filter state (mono component)
	float cascadeLpStateSide_{0.0f};       // Cascade output LP filter state (side component)
	static constexpr float kPreCascadeAaCoeff = 0.35f;  // LP coeff ~3.5kHz for pre-decimation AA (below 4x Nyquist)
	static constexpr float kCascadeLpCoeffMono = 0.6f;  // LP coeff ~5.5kHz for cascade mono (darker tail)
	static constexpr float kCascadeLpCoeffSide = 0.85f; // LP coeff ~8kHz for cascade side (brighter stereo spread)
	uint8_t c0Phase_{0};                                // Phase counter for C0 undersampling
	float c0Accum_{0.0f};                               // Accumulated input for C0
	float c0Prev_{0.0f};                                // Previous C0 output for interpolation
	uint8_t c1Phase_{0};                                // Phase counter for C1 undersampling
	float c1Accum_{0.0f};                               // Accumulated input for C1
	float c1Prev_{0.0f};                                // Previous C1 output for interpolation
	uint8_t c2Phase_{0};                                // Phase counter for C2 undersampling
	float c2Accum_{0.0f};                               // Accumulated input for C2
	float c2Prev_{0.0f};                                // Previous C2 output for interpolation
	uint8_t c3Phase_{0};                                // Phase counter for C3 undersampling
	float c3Accum_{0.0f};                               // Accumulated input for C3
	float c3Prev_{0.0f};                                // Previous C3 output for interpolation

	// Direct early tap (bypasses output LPF for brightness)
	float directEarlyL_{0.0f};
	float directEarlyR_{0.0f};

	// Update functions
	void updateMatrix();
	void updateFeedbackPattern();
	void updateSizes(); // Updates D2 and cascade lengths from Zone 2

	// FDN delay helpers
	[[gnu::always_inline]] float fdnRead(size_t line) const { return buffer_[fdnOffsets_[line] + fdnWritePos_[line]]; }

	[[gnu::always_inline]] float fdnReadAt(size_t line, size_t offset) const {
		size_t pos = fdnWritePos_[line] + offset;
		if (pos >= fdnLengths_[line]) {
			pos -= fdnLengths_[line];
		}
		return buffer_[fdnOffsets_[line] + pos];
	}

	[[gnu::always_inline]] void fdnWrite(size_t line, float value) {
		buffer_[fdnOffsets_[line] + fdnWritePos_[line]] = value;
		if (++fdnWritePos_[line] >= fdnLengths_[line]) {
			fdnWritePos_[line] = 0;
		}
	}

	// Cascade allpass helpers
	[[gnu::always_inline]] float processCascadeStage(size_t stage, float input) {
		size_t idx = cascadeOffsets_[stage] + cascadeWritePos_[stage];
		float delayed = buffer_[idx];
		float coeff = cascadeCoeffs_[stage];
		float output = -coeff * input + delayed;
		float writeVal = input + coeff * output;
		buffer_[idx] = writeVal;

		// Multi-tap write: secondary echo at prime offset for doubled density
		if constexpr (kEnableMultiTapWrites) {
			size_t tapOffset = kMultiTapOffsets[stage];
			if (tapOffset < cascadeLengths_[stage]) {
				size_t tapPos = (cascadeWritePos_[stage] + tapOffset) % cascadeLengths_[stage];
				buffer_[cascadeOffsets_[stage] + tapPos] += writeVal * kMultiTapGain;
			}
		}

		if (++cascadeWritePos_[stage] >= cascadeLengths_[stage]) {
			cascadeWritePos_[stage] = 0;
		}
		return output;
	}

	// Diffuser helper
	[[gnu::always_inline]] float processDiffuser(size_t idx, float input, size_t length) {
		float delayed = buffer_[diffuserOffsets_[idx] + diffuserWritePos_[idx]];
		float output = -kDiffuserCoeff * input + delayed;
		buffer_[diffuserOffsets_[idx] + diffuserWritePos_[idx]] = input + kDiffuserCoeff * output;
		if (++diffuserWritePos_[idx] >= length) {
			diffuserWritePos_[idx] = 0;
		}
		return output;
	}

	// Predelay helpers
	[[gnu::always_inline]] float readPredelay(size_t offset) const {
		if (predelayLength_ == 0)
			return 0.0f;
		size_t pos = (predelayWritePos_ >= offset) ? (predelayWritePos_ - offset)
		                                           : (predelayWritePos_ + kPredelayMaxLength - offset);
		return buffer_[predelayOffset_ + pos];
	}

	[[gnu::always_inline]] void writePredelay(float value) {
		buffer_[predelayOffset_ + predelayWritePos_] = value;
		if (++predelayWritePos_ >= kPredelayMaxLength) {
			predelayWritePos_ = 0;
		}
	}

	[[gnu::always_inline]] static float onepole(float input, float& state, float coeff) {
		state += coeff * (input - state);
		return state;
	}
};

} // namespace deluge::dsp::reverb
