/*
 * Copyright © 2026 Synthstrom Audible Limited
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
 */

/// @file
/// @brief Test-local reference for the comb-filter mod FX (flanger, chorus, phaser, stereo chorus, warble and
/// dimension): today's `ModFXProcessor` (model/mod_controllable/ModFXProcessor.cpp), copied line for line.
///
/// @ref deluge::dsp::test::ReferenceModFx differs from it only where the test has no firmware around it:
///  - the two unpatched param reads (offset, feedback) are arguments, and `AudioEngine::renderInStereo` is the
///    `stereo` argument;
///  - the ring is an owned array with a `held` flag, where the firmware holds a heap pointer;
///  - the LFO is a copy of the sine, triangle and warbler waves of `synth/lfo.h` with the variant's re-seat rule;
///  - every add or subtract that can wrap is a wrapping one, so the spec's signed-overflow sanitizer sees the same
///    two's-complement result the target computes.

#pragma once

#include "definitions_cxx.hpp"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/bandlimited/waves.h" // triangle
#include "dsp-next/fixed/osc/sine_table.h"    // sine_table
#include "util/fixedpoint.h"
#include "util/waves.h" // the jcong/CONG RNG the warbler draws

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>

namespace deluge::dsp::test {

/// @brief The LFO waves mod FX uses, copied from `lfo_wave` (synth/lfo.h) with its phase and block conventions.
namespace ref_lfo_wave {

struct Sine {
	int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		int32_t value = deluge::dsp::fixed::sine_table.value(phase);
		phase += phase_increment * num_samples;
		return value;
	}
};

struct Triangle {
	int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		int32_t value = deluge::dsp::fixed::triangle(phase);
		phase += phase_increment * num_samples;
		return value;
	}
};

struct Warbler {
	int32_t held_value_ = 0;
	int32_t target_ = 0;
	int32_t speed_ = 0;
	int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		phase_increment *= 2; // the warbler sweeps at twice the configured rate
		if (phase + phase_increment * num_samples < phase) {
			target_ = CONG;
			speed_ = 0;
		}
		int32_t distance_to_target = wrapping_sub(target_, held_value_);
		speed_ =
		    wrapping_add(speed_, num_samples * (multiply_32x32_rshift32(distance_to_target, phase_increment >> 8)));
		held_value_ = add_saturate(held_value_, speed_);
		phase += phase_increment * num_samples;
		return held_value_;
	}
};

} // namespace ref_lfo_wave

/// @brief `LFO` (synth/lfo.h) reduced to the waves mod FX renders.
class ReferenceLfo {
public:
	/// @return The variant index of @p type: the same order as `LFO`'s, restricted to the waves mod FX uses.
	[[nodiscard]] static constexpr std::size_t wave_index(LFOType type) {
		switch (type) {
		case LFOType::TRIANGLE:
			return 1;
		case LFOType::WARBLER:
			return 2;
		default:
			return 0;
		}
	}

	/// @brief Renders one block, re-seating the wave on a change of type.
	int32_t render(int32_t num_samples, LFOType wave_type, uint32_t phase_increment) {
		selectWave(wave_type);
		return std::visit([&](auto& wave) { return wave.render(phase_, phase_increment, num_samples); }, wave_);
	}

	/// @brief Advances the phase without rendering.
	void tick(int32_t num_samples, uint32_t phase_increment) { phase_ += phase_increment * num_samples; }

	[[nodiscard]] uint32_t phase() const { return phase_; }

private:
	void emplaceWave(LFOType type) {
		switch (wave_index(type)) {
		case 1:
			wave_.emplace<ref_lfo_wave::Triangle>();
			break;
		case 2:
			wave_.emplace<ref_lfo_wave::Warbler>();
			break;
		default:
			wave_.emplace<ref_lfo_wave::Sine>();
			break;
		}
	}
	void selectWave(LFOType type) {
		if (wave_.index() != wave_index(type)) {
			emplaceWave(type);
		}
	}

	uint32_t phase_ = 0;
	std::variant<ref_lfo_wave::Sine, ref_lfo_wave::Triangle, ref_lfo_wave::Warbler> wave_{};
};

/// @brief Today's mod FX, copied from `ModFXProcessor`.
class ReferenceModFx {
public:
	/// @brief How many adds and subtracts have wrapped since the last @ref reset_wraps.
	static inline std::size_t wraps = 0;
	static void reset_wraps() { wraps = 0; }

	StereoSample phaserMemory{0, 0};
	std::array<StereoSample, kNumAllpassFiltersPhaser> allpassMemory{};
	std::array<StereoSample, kModFXBufferSize> ring{};
	bool held{false};
	uint16_t modFXBufferWriteIndex{0};
	ReferenceLfo modFXLFO;
	ReferenceLfo modFXLFOStereo;

	/// NOT GRAIN! - this only does the comb filter based mod fx
	void processModFX(std::span<StereoSample> buffer, const ModFXType& modFXType, int32_t modFXRate, int32_t modFXDepth,
	                  int32_t* postFXVolume, int32_t modFXOffset, int32_t modFXFeedback, bool stereo,
	                  bool anySoundComingIn) {
		// if nothing rendered into the clip then we can just return
		if (anySoundComingIn == false)
			return;
		if (modFXType != ModFXType::NONE) {

			LFOType modFXLFOWaveType{};
			int32_t modFXDelayOffset{};
			int32_t thisModFXDelayDepth{};
			int32_t feedback{};

			if (modFXType == ModFXType::FLANGER || modFXType == ModFXType::PHASER || modFXType == ModFXType::WARBLE) {

				setupModFXWFeedback(modFXType, modFXDepth, postFXVolume, modFXOffset, modFXFeedback, modFXLFOWaveType,
				                    modFXDelayOffset, thisModFXDelayDepth, feedback);
			}
			else if (modFXType == ModFXType::CHORUS || modFXType == ModFXType::CHORUS_STEREO
			         || modFXType == ModFXType::DIMENSION) {
				setupChorus(modFXType, modFXDepth, postFXVolume, modFXOffset, modFXLFOWaveType, modFXDelayOffset,
				            thisModFXDelayDepth);
			}

			switch (modFXType) {

			case ModFXType::NONE:
				break;
			case ModFXType::FLANGER:
				processModFXBuffer<ModFXType::FLANGER>(buffer, modFXRate, modFXDepth, modFXLFOWaveType,
				                                       modFXDelayOffset, thisModFXDelayDepth, feedback, stereo);
				break;
			case ModFXType::CHORUS:
				processModFXBuffer<ModFXType::CHORUS>(buffer, modFXRate, modFXDepth, modFXLFOWaveType, modFXDelayOffset,
				                                      thisModFXDelayDepth, feedback, stereo);
				break;
			case ModFXType::PHASER:
				processModFXBuffer<ModFXType::PHASER>(buffer, modFXRate, modFXDepth, modFXLFOWaveType, modFXDelayOffset,
				                                      thisModFXDelayDepth, feedback, stereo);
				break;
			case ModFXType::CHORUS_STEREO:
				processModFXBuffer<ModFXType::CHORUS_STEREO>(buffer, modFXRate, modFXDepth, modFXLFOWaveType,
				                                             modFXDelayOffset, thisModFXDelayDepth, feedback, stereo);
				break;
			case ModFXType::WARBLE:
				processModFXBuffer<ModFXType::WARBLE>(buffer, modFXRate, modFXDepth, modFXLFOWaveType, modFXDelayOffset,
				                                      thisModFXDelayDepth, feedback, stereo);
				break;
			case ModFXType::GRAIN:
				break;
			case ModFXType::DIMENSION:
				processModFXBuffer<ModFXType::DIMENSION>(buffer, modFXRate, modFXDepth, modFXLFOWaveType,
				                                         modFXDelayOffset, thisModFXDelayDepth, feedback, stereo);
				break;
			}
		}
	}

	void resetMemory() {
		if (held) {
			ring.fill(StereoSample{0, 0});
		}

		else {
			allpassMemory.fill(StereoSample{0, 0});
			phaserMemory = StereoSample{0, 0};
		}
	}

	void setupBuffer() {
		if (!held) {
			held = true;
			ring.fill(StereoSample{0, 0});
		}
	}
	void disableBuffer() { held = false; }
	void tickLFO(int32_t numSamples, int32_t phaseIncrement) {
		// Do Mod FX
		modFXLFO.tick(numSamples, phaseIncrement);
	}

private:
	/// @return @p a + @p b, wrapping, counted in @ref wraps when it wraps.
	[[nodiscard]] static q31_t add(q31_t a, q31_t b) {
		wraps += static_cast<std::size_t>(static_cast<int64_t>(a) + b != static_cast<int64_t>(wrapping_add(a, b)));
		return wrapping_add(a, b);
	}
	/// @return @p a - @p b, wrapping, counted in @ref wraps when it wraps.
	[[nodiscard]] static q31_t sub(q31_t a, q31_t b) {
		wraps += static_cast<std::size_t>(static_cast<int64_t>(a) - b != static_cast<int64_t>(wrapping_sub(a, b)));
		return wrapping_sub(a, b);
	}

	void setupChorus(const ModFXType& modFXType, int32_t modFXDepth, int32_t* postFXVolume, int32_t modFXOffset,
	                 LFOType& modFXLFOWaveType, int32_t& modFXDelayOffset, int32_t& thisModFXDelayDepth) const {
		modFXDelayOffset = multiply_32x32_rshift32(kModFXMaxDelay, (modFXOffset >> 1) + 1073741824);
		thisModFXDelayDepth = multiply_32x32_rshift32(modFXDelayOffset, modFXDepth) << 2;
		if (modFXType == ModFXType::DIMENSION) {
			modFXLFOWaveType = LFOType::TRIANGLE;
		}
		else {
			modFXLFOWaveType = LFOType::SINE;
		}

		*postFXVolume = multiply_32x32_rshift32(*postFXVolume, 1518500250) << 1; // Divide by sqrt(2)
	}

	/// flanger, phaser, warble - generally any modulated delay tap based effect with feedback
	void setupModFXWFeedback(const ModFXType& modFXType, int32_t modFXDepth, int32_t* postFXVolume, int32_t modFXOffset,
	                         int32_t modFXFeedback, LFOType& modFXLFOWaveType, int32_t& modFXDelayOffset,
	                         int32_t& thisModFXDelayDepth, int32_t& feedback) const {
		int32_t a = modFXFeedback >> 1;
		int32_t b = 2147483647 - ((a + 1073741824) >> 2) * 3;
		int32_t c = multiply_32x32_rshift32(b, b);
		int32_t d = multiply_32x32_rshift32(b, c);

		feedback = wrapping_sub(INT32_MIN, d << 2); // 2^31 - (d << 2), mod 2^32

		// Adjust volume for flanger feedback
		int32_t squared = multiply_32x32_rshift32(feedback, feedback) << 1;
		int32_t squared2 = multiply_32x32_rshift32(squared, squared) << 1;
		squared2 = multiply_32x32_rshift32(squared2, squared) << 1;
		squared2 = (multiply_32x32_rshift32(squared2, squared2) >> 4)
		           * 23; // 22 // Make bigger to have more of a volume cut happen at high resonance
		//*postFXVolume = (multiply_32x32_rshift32(*postFXVolume, 2147483647 - squared2) >> 1) * 3;
		*postFXVolume = multiply_32x32_rshift32(*postFXVolume, 2147483647 - squared2);

		if (modFXType == ModFXType::FLANGER) {
			*postFXVolume <<= 1;
			modFXDelayOffset = kFlangerOffset;
			thisModFXDelayDepth = kFlangerAmplitude;
			modFXLFOWaveType = LFOType::TRIANGLE;
		}
		else if (modFXType == ModFXType::WARBLE) {
			*postFXVolume <<= 1;
			modFXDelayOffset = kFlangerOffset;
			modFXDelayOffset += multiply_32x32_rshift32(kFlangerOffset, modFXOffset);
			thisModFXDelayDepth = multiply_32x32_rshift32(modFXDelayOffset, modFXDepth) << 1;
			modFXLFOWaveType = LFOType::WARBLER;
		}

		else { // Phaser
			modFXLFOWaveType = LFOType::SINE;
		}
	}

	// not grain!
	template <ModFXType modFXType>
	void processModFXBuffer(std::span<StereoSample> buffer, int32_t modFXRate, int32_t modFXDepth,
	                        LFOType& modFXLFOWaveType, int32_t modFXDelayOffset, int32_t thisModFXDelayDepth,
	                        int32_t feedback, bool stereo) {
		if constexpr (modFXType == ModFXType::PHASER) {
			for (StereoSample& sample : buffer) {
				int32_t lfo = modFXLFO.render(1, modFXLFOWaveType, modFXRate);
				sample = processOnePhaserSample(sample, modFXDepth, feedback, lfo);
			}
			return;
		}

		if (!held) {
			// The buffer could not be allocated this block (the caller retries every block): pass the signal
			// through, as with the effect off.
			return;
		}

		for (StereoSample& sample : buffer) {
			auto [lfo1, lfo2] = processModLFOs<modFXType>(modFXRate, modFXLFOWaveType);
			sample = stereo ? processOneModFXSample<modFXType, true>(sample, modFXDelayOffset, thisModFXDelayDepth,
			                                                         feedback, lfo1, lfo2)
			                : processOneModFXSample<modFXType, false>(sample, modFXDelayOffset, thisModFXDelayDepth,
			                                                          feedback, lfo1, sub(0, lfo1));
		}
	}

	template <ModFXType modFXType>
	std::pair<int32_t, int32_t> processModLFOs(int32_t modFXRate, LFOType modFXLFOWaveType) {
		int32_t lfo1 = modFXLFO.render(1, modFXLFOWaveType, modFXRate);
		int32_t lfo2 = 0;
		// anymore and they get audibly out of sync, this just sounds wobblier
		constexpr q31_t width = 0.97 * ONE_Q31;
		if constexpr (modFXType == ModFXType::WARBLE) {
			// this needs a second lfo because it's a random process - we can't flip it to make a second sample but
			// these will always be different anyway

			lfo2 = modFXLFOStereo.render(1, modFXLFOWaveType, multiply_32x32_rshift32(modFXRate, width) << 1);
		}
		else {
			lfo2 = sub(0, lfo1);
		}
		return {lfo1, lfo2};
	}

	template <ModFXType modFXType, bool stereo>
	StereoSample processOneModFXSample(StereoSample sample, int32_t modFXDelayOffset, int32_t thisModFXDelayDepth,
	                                   int32_t feedback, int32_t lfoOutput, int32_t lfo2Output) {
		int32_t delayTime = multiply_32x32_rshift32(lfoOutput, thisModFXDelayDepth) + modFXDelayOffset;

		int32_t strength2 = (delayTime & 65535) << 15;
		int32_t strength1 = (65535 << 15) - strength2;
		int32_t sample1Pos = modFXBufferWriteIndex - ((delayTime) >> 16);

		int32_t scaledValue1L = multiply_32x32_rshift32_rounded(ring[sample1Pos & kModFXBufferIndexMask].l, strength1);
		int32_t scaledValue2L =
		    multiply_32x32_rshift32_rounded(ring[(sample1Pos - 1) & kModFXBufferIndexMask].l, strength2);
		int32_t modFXOutputL = scaledValue1L + scaledValue2L;

		if constexpr (stereo || modFXType == ModFXType::DIMENSION || modFXType == ModFXType::WARBLE) {
			delayTime = multiply_32x32_rshift32(lfo2Output, thisModFXDelayDepth) + modFXDelayOffset;
			strength2 = (delayTime & 65535) << 15;
			strength1 = (65535 << 15) - strength2;
			sample1Pos = modFXBufferWriteIndex - ((delayTime) >> 16);
		}

		int32_t scaledValue1R = multiply_32x32_rshift32_rounded(ring[sample1Pos & kModFXBufferIndexMask].r, strength1);
		int32_t scaledValue2R =
		    multiply_32x32_rshift32_rounded(ring[(sample1Pos - 1) & kModFXBufferIndexMask].r, strength2);
		int32_t modFXOutputR = scaledValue1R + scaledValue2R;

		// feedback also controls the mix? Weird but ok, I guess it makes it work on one knob
		if constexpr (modFXType == ModFXType::FLANGER) {
			modFXOutputL = multiply_32x32_rshift32_rounded(modFXOutputL, feedback) << 2;
			ring[modFXBufferWriteIndex].l = add(modFXOutputL, sample.l); // Feedback
			modFXOutputR = multiply_32x32_rshift32_rounded(modFXOutputR, feedback) << 2;
			ring[modFXBufferWriteIndex].r = add(modFXOutputR, sample.r); // Feedback
		}
		else if constexpr (modFXType == ModFXType::WARBLE) {
			auto fback = multiply_32x32_rshift32_rounded(modFXOutputL, feedback);
			ring[modFXBufferWriteIndex].l = add(fback, sample.l); // Feedback
			fback = multiply_32x32_rshift32_rounded(modFXOutputR, feedback);
			ring[modFXBufferWriteIndex].r = add(fback, sample.r); // Feedback

			modFXOutputL <<= 1;
			modFXOutputR <<= 1;
		}
		else { // Chorus, Dimension
			modFXOutputL <<= 1;
			ring[modFXBufferWriteIndex].l = sample.l; // Feedback
			modFXOutputR <<= 1;
			ring[modFXBufferWriteIndex].r = sample.r; // Feedback
		}

		if constexpr (modFXType == ModFXType::DIMENSION || modFXType == ModFXType::WARBLE) {
			sample.l = modFXOutputL << 1;
			sample.r = modFXOutputR << 1;
		}
		else {
			sample.l = add(sample.l, modFXOutputL);
			sample.r = add(sample.r, modFXOutputR);
		}

		modFXBufferWriteIndex = (modFXBufferWriteIndex + 1) & kModFXBufferIndexMask;
		return sample;
	}

	StereoSample processOnePhaserSample(StereoSample sample, int32_t modFXDepth, int32_t feedback,
	                                    int32_t lfoOutput) { // "1" is sorta represented by 1073741824 here
		int32_t _a1 = sub(
		    1073741824, multiply_32x32_rshift32_rounded(
		                    ((static_cast<uint32_t>(lfoOutput) + static_cast<uint32_t>(2147483648)) >> 1), modFXDepth));

		phaserMemory.l = add(sample.l, multiply_32x32_rshift32_rounded(phaserMemory.l, feedback) << 1);
		phaserMemory.r = add(sample.r, multiply_32x32_rshift32_rounded(phaserMemory.r, feedback) << 1);

		// Do the allpass filters
		for (auto& sample : allpassMemory) {
			StereoSample whatWasInput = phaserMemory;

			phaserMemory.l = add(multiply_32x32_rshift32_rounded(phaserMemory.l, sub(0, _a1)) << 2, sample.l);
			sample.l = add(multiply_32x32_rshift32_rounded(phaserMemory.l, _a1) << 2, whatWasInput.l);

			phaserMemory.r = add(multiply_32x32_rshift32_rounded(phaserMemory.r, sub(0, _a1)) << 2, sample.r);
			sample.r = add(multiply_32x32_rshift32_rounded(phaserMemory.r, _a1) << 2, whatWasInput.r);
		}

		sample.l = add(sample.l, phaserMemory.l);
		sample.r = add(sample.r, phaserMemory.r);

		return sample;
	}
};

} // namespace deluge::dsp::test
