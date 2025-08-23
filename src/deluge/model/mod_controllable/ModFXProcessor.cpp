/*
 * Copyright © 2024 Mark Adams
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

#include "ModFXProcessor.h"
#include "definitions_cxx.hpp"
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "util/comparison.h"
#include <cstdint>

/// NOT GRAIN! - this only does the comb filter based mod fx
void ModFXProcessor::processModFX(deluge::dsp::StereoBuffer<q31_t> buffer, const ModFXType& modFXType,
                                  int32_t modFXRate, int32_t modFXDepth, int32_t* postFXVolume,
                                  UnpatchedParamSet* unpatchedParams, bool anySoundComingIn) {

	if (modFXType != ModFXType::NONE) {

		LFOType modFXLFOWaveType{};
		int32_t modFXDelayOffset{};
		int32_t thisModFXDelayDepth{};
		int32_t feedback{};

		if (modFXType == ModFXType::FLANGER || modFXType == ModFXType::PHASER || modFXType == ModFXType::WARBLE) {

			setupModFXWFeedback(modFXType, modFXDepth, postFXVolume, unpatchedParams, modFXLFOWaveType,
			                    modFXDelayOffset, thisModFXDelayDepth, feedback);
		}
		else if (modFXType == ModFXType::CHORUS || modFXType == ModFXType::CHORUS_STEREO
		         || modFXType == ModFXType::DIMENSION) {
			setupChorus(modFXType, modFXDepth, postFXVolume, unpatchedParams, modFXLFOWaveType, modFXDelayOffset,
			            thisModFXDelayDepth);
		}

		switch (modFXType) {

		case ModFXType::NONE:
			break;
		case ModFXType::FLANGER:
			processModFXBuffer<ModFXType::FLANGER>(buffer, modFXRate, modFXDepth, modFXLFOWaveType, modFXDelayOffset,
			                                       thisModFXDelayDepth, feedback, AudioEngine::renderInStereo);
			break;
		case ModFXType::CHORUS:
			processModFXBuffer<ModFXType::CHORUS>(buffer, modFXRate, modFXDepth, modFXLFOWaveType, modFXDelayOffset,
			                                      thisModFXDelayDepth, feedback, AudioEngine::renderInStereo);
			break;
		case ModFXType::PHASER:
			processModFXBuffer<ModFXType::PHASER>(buffer, modFXRate, modFXDepth, modFXLFOWaveType, modFXDelayOffset,
			                                      thisModFXDelayDepth, feedback, AudioEngine::renderInStereo);
			break;
		case ModFXType::CHORUS_STEREO:
			processModFXBuffer<ModFXType::CHORUS_STEREO>(buffer, modFXRate, modFXDepth, modFXLFOWaveType,
			                                             modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                             AudioEngine::renderInStereo);
			break;
		case ModFXType::WARBLE:
			processModFXBuffer<ModFXType::WARBLE>(buffer, modFXRate, modFXDepth, modFXLFOWaveType, modFXDelayOffset,
			                                      thisModFXDelayDepth, feedback, AudioEngine::renderInStereo);
			break;
		case ModFXType::GRAIN:
			break;
		case ModFXType::DIMENSION:
			processModFXBuffer<ModFXType::DIMENSION>(buffer, modFXRate, modFXDepth, modFXLFOWaveType, modFXDelayOffset,
			                                         thisModFXDelayDepth, feedback, AudioEngine::renderInStereo);
			break;
		}
	}
}
void ModFXProcessor::setupChorus(const ModFXType& modFXType, int32_t modFXDepth, int32_t* postFXVolume,
                                 UnpatchedParamSet* unpatchedParams, LFOType& modFXLFOWaveType,
                                 int32_t& modFXDelayOffset, int32_t& thisModFXDelayDepth) const {
	modFXDelayOffset = multiply_32x32_rshift32(
	    kModFXMaxDelay,
	    (unpatchedParams->getValue(deluge::modulation::params::UNPATCHED_MOD_FX_OFFSET) >> 1) + 1073741824);
	thisModFXDelayDepth = multiply_32x32_rshift32(modFXDelayOffset, modFXDepth) << 2;
	if (modFXType == ModFXType::DIMENSION) {
		modFXLFOWaveType = LFOType::TRIANGLE;
	}
	else {
		modFXLFOWaveType = LFOType::SINE;
	}

	*postFXVolume = q31_mult(*postFXVolume, 1518500250); // Divide by sqrt(2)
}
void ModFXProcessor::setupModFXWFeedback(const ModFXType& modFXType, int32_t modFXDepth, int32_t* postFXVolume,
                                         UnpatchedParamSet* unpatchedParams, LFOType& modFXLFOWaveType,
                                         int32_t& modFXDelayOffset, int32_t& thisModFXDelayDepth,
                                         int32_t& feedback) const {
	int32_t a = unpatchedParams->getValue(deluge::modulation::params::UNPATCHED_MOD_FX_FEEDBACK) >> 1;
	int32_t b = 2147483647 - ((a + 1073741824) >> 2) * 3;
	int32_t c = multiply_32x32_rshift32(b, b);
	int32_t d = multiply_32x32_rshift32(b, c);

	feedback = 2147483648 - (d << 2);

	// Adjust volume for flanger feedback
	int32_t squared = q31_mult(feedback, feedback);
	int32_t squared2 = q31_mult(squared, squared);
	squared2 = q31_mult(squared2, squared);
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
		modFXDelayOffset += multiply_32x32_rshift32(
		    kFlangerOffset, (unpatchedParams->getValue(deluge::modulation::params::UNPATCHED_MOD_FX_OFFSET)));
		thisModFXDelayDepth = q31_mult(modFXDelayOffset, modFXDepth);
		modFXLFOWaveType = LFOType::WARBLER;
	}

	else { // Phaser
		modFXLFOWaveType = LFOType::SINE;
	}
}
template <ModFXType modFXType>
void ModFXProcessor::processModFXBuffer(deluge::dsp::StereoBuffer<q31_t> buffer, int32_t modFXRate, int32_t modFXDepth,
                                        LFOType& modFXLFOWaveType, int32_t modFXDelayOffset,
                                        int32_t thisModFXDelayDepth, int32_t feedback, bool stereo) {
	if constexpr (modFXType == ModFXType::PHASER) {
		for (deluge::dsp::StereoSample<q31_t>& sample : buffer) {
			int32_t lfo = modFXLFO.render(1, modFXLFOWaveType, modFXRate);
			sample = processOnePhaserSample(sample, modFXDepth, feedback, lfo);
		}
		return;
	}
	if (stereo) {
		for (deluge::dsp::StereoSample<q31_t>& sample : buffer) {
			auto [lfo1, lfo2] = processModLFOs<modFXType>(modFXRate, modFXLFOWaveType);
			sample = processOneModFXSample<modFXType, true>(sample, modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                                lfo1, lfo2);
		}
	}
	else {
		for (deluge::dsp::StereoSample<q31_t>& sample : buffer) {
			auto [lfo1, lfo2] = processModLFOs<modFXType>(modFXRate, modFXLFOWaveType);
			sample = processOneModFXSample<modFXType, false>(sample, modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                                 lfo1, -lfo1);
		}
	}
}

template <ModFXType modFXType>
std::pair<int32_t, int32_t> ModFXProcessor::processModLFOs(int32_t modFXRate, LFOType modFXLFOWaveType) {
	int32_t lfo1 = modFXLFO.render(1, modFXLFOWaveType, modFXRate);
	int32_t lfo2 = 0;
	// anymore and they get audibly out of sync, this just sounds wobblier
	constexpr q31_t width = 0.97 * ONE_Q31;
	if constexpr (modFXType == ModFXType::WARBLE) {
		// this needs a second lfo because it's a random process - we can't flip it to make a second sample but
		// these will always be different anyway

		lfo2 = modFXLFOStereo.render(1, modFXLFOWaveType, q31_mult(modFXRate, width));
	}
	else {
		lfo2 = -lfo1;
	}
	return {lfo1, lfo2};
}

template <ModFXType modFXType, bool stereo>
deluge::dsp::StereoSample<q31_t> ModFXProcessor::processOneModFXSample(deluge::dsp::StereoSample<q31_t> sample,
                                                                       int32_t modFXDelayOffset,
                                                                       int32_t thisModFXDelayDepth, int32_t feedback,
                                                                       int32_t lfoOutput, int32_t lfo2Output) {
	int32_t delayTime = multiply_32x32_rshift32(lfoOutput, thisModFXDelayDepth) + modFXDelayOffset;

	int32_t strength2 = (delayTime & 65535) << 15;
	int32_t strength1 = (65535 << 15) - strength2;
	int32_t sample1Pos = modFXBufferWriteIndex - ((delayTime) >> 16);

	int32_t scaledValue1L =
	    multiply_32x32_rshift32_rounded(modFXBuffer[sample1Pos & kModFXBufferIndexMask].l, strength1);
	int32_t scaledValue2L =
	    multiply_32x32_rshift32_rounded(modFXBuffer[(sample1Pos - 1) & kModFXBufferIndexMask].l, strength2);
	int32_t modFXOutputL = scaledValue1L + scaledValue2L;

	if constexpr (stereo || modFXType == ModFXType::DIMENSION || modFXType == ModFXType::WARBLE) {
		delayTime = multiply_32x32_rshift32(lfo2Output, thisModFXDelayDepth) + modFXDelayOffset;
		strength2 = (delayTime & 65535) << 15;
		strength1 = (65535 << 15) - strength2;
		sample1Pos = modFXBufferWriteIndex - ((delayTime) >> 16);
	}

	int32_t scaledValue1R =
	    multiply_32x32_rshift32_rounded(modFXBuffer[sample1Pos & kModFXBufferIndexMask].r, strength1);
	int32_t scaledValue2R =
	    multiply_32x32_rshift32_rounded(modFXBuffer[(sample1Pos - 1) & kModFXBufferIndexMask].r, strength2);
	int32_t modFXOutputR = scaledValue1R + scaledValue2R;

	// feedback also controls the mix? Weird but ok, I guess it makes it work on one knob
	if constexpr (modFXType == ModFXType::FLANGER) {
		modFXOutputL = multiply_32x32_rshift32_rounded(modFXOutputL, feedback) << 2;
		modFXBuffer[modFXBufferWriteIndex].l = modFXOutputL + sample.l; // Feedback
		modFXOutputR = multiply_32x32_rshift32_rounded(modFXOutputR, feedback) << 2;
		modFXBuffer[modFXBufferWriteIndex].r = modFXOutputR + sample.r; // Feedback
	}
	else if constexpr (modFXType == ModFXType::WARBLE) {
		auto fback = multiply_32x32_rshift32_rounded(modFXOutputL, feedback);
		modFXBuffer[modFXBufferWriteIndex].l = fback + sample.l; // Feedback
		fback = multiply_32x32_rshift32_rounded(modFXOutputR, feedback);
		modFXBuffer[modFXBufferWriteIndex].r = fback + sample.r; // Feedback

		modFXOutputL <<= 1;
		modFXOutputR <<= 1;
	}
	else { // Chorus, Dimension
		modFXOutputL <<= 1;
		modFXBuffer[modFXBufferWriteIndex].l = sample.l; // Feedback
		modFXOutputR <<= 1;
		modFXBuffer[modFXBufferWriteIndex].r = sample.r; // Feedback
	}

	if constexpr (modFXType == ModFXType::DIMENSION || modFXType == ModFXType::WARBLE) {
		sample.l = modFXOutputL << 1;
		sample.r = modFXOutputR << 1;
	}
	else {
		sample.l += modFXOutputL;
		sample.r += modFXOutputR;
	}

	modFXBufferWriteIndex = (modFXBufferWriteIndex + 1) & kModFXBufferIndexMask;
	return sample;
}

deluge::dsp::StereoSample<q31_t>
ModFXProcessor::processOnePhaserSample(deluge::dsp::StereoSample<q31_t> sample, int32_t modFXDepth, int32_t feedback,
                                       int32_t lfoOutput) { // "1" is sorta represented by 1073741824 here
	int32_t _a1 =
	    1073741824 - multiply_32x32_rshift32_rounded((((uint32_t)lfoOutput + (uint32_t)2147483648) >> 1), modFXDepth);

	phaserMemory.l = sample.l + (q31_mult_rounded(phaserMemory.l, feedback));
	phaserMemory.r = sample.r + (q31_mult_rounded(phaserMemory.r, feedback));

	// Do the allpass filters
	for (auto& sample : allpassMemory) {
		deluge::dsp::StereoSample<q31_t> whatWasInput = phaserMemory;

		phaserMemory.l = (multiply_32x32_rshift32_rounded(phaserMemory.l, -_a1) << 2) + sample.l;
		sample.l = (multiply_32x32_rshift32_rounded(phaserMemory.l, _a1) << 2) + whatWasInput.l;

		phaserMemory.r = (multiply_32x32_rshift32_rounded(phaserMemory.r, -_a1) << 2) + sample.r;
		sample.r = (multiply_32x32_rshift32_rounded(phaserMemory.r, _a1) << 2) + whatWasInput.r;
	}

	sample.l += phaserMemory.l;
	sample.r += phaserMemory.r;

	return sample;
}

void ModFXProcessor::resetMemory() {
	if (modFXBuffer) {
		memset(modFXBuffer, 0, kModFXBufferSize * sizeof(deluge::dsp::StereoSample<q31_t>));
	}

	else {
		memset(allpassMemory, 0, sizeof(allpassMemory));
		memset(&phaserMemory, 0, sizeof(phaserMemory));
	}
}

void ModFXProcessor::setupBuffer() {
	if (modFXBuffer == nullptr) {
		modFXBuffer =
		    (deluge::dsp::StereoSample<q31_t>*)delugeAlloc(kModFXBufferSize * sizeof(deluge::dsp::StereoSample<q31_t>));
		if (modFXBuffer != nullptr) {
			memset(modFXBuffer, 0, kModFXBufferSize * sizeof(deluge::dsp::StereoSample<q31_t>));
		}
	}
}
void ModFXProcessor::disableBuffer() {
	if (modFXBuffer != nullptr) {
		delugeDealloc(modFXBuffer);
		modFXBuffer = nullptr;
	}
}
