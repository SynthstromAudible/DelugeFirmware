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
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
/// NOT GRAIN! - this only does the comb filter based mod fx
void ModFXProcessor::processModFX(StereoSample* buffer, const ModFXType& modFXType, int32_t modFXRate,
                                  int32_t modFXDepth, int32_t* postFXVolume, UnpatchedParamSet* unpatchedParams,
                                  const StereoSample* bufferEnd, bool anySoundComingIn) {

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
			processModFXBuffer<ModFXType::FLANGER>(buffer, modFXRate, modFXDepth, bufferEnd, modFXLFOWaveType,
			                                       modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                       AudioEngine::renderInStereo);
			break;
		case ModFXType::CHORUS:
			processModFXBuffer<ModFXType::CHORUS>(buffer, modFXRate, modFXDepth, bufferEnd, modFXLFOWaveType,
			                                      modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                      AudioEngine::renderInStereo);
			break;
		case ModFXType::PHASER:
			processModFXBuffer<ModFXType::PHASER>(buffer, modFXRate, modFXDepth, bufferEnd, modFXLFOWaveType,
			                                      modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                      AudioEngine::renderInStereo);
			break;
		case ModFXType::CHORUS_STEREO:
			processModFXBuffer<ModFXType::CHORUS_STEREO>(buffer, modFXRate, modFXDepth, bufferEnd, modFXLFOWaveType,
			                                             modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                             AudioEngine::renderInStereo);
			break;
		case ModFXType::WARBLE:
			processModFXBuffer<ModFXType::WARBLE>(buffer, modFXRate, modFXDepth, bufferEnd, modFXLFOWaveType,
			                                      modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                      AudioEngine::renderInStereo);
			break;
		case ModFXType::GRAIN:
			break;
		case ModFXType::DIMENSION:
			processModFXBuffer<ModFXType::DIMENSION>(buffer, modFXRate, modFXDepth, bufferEnd, modFXLFOWaveType,
			                                         modFXDelayOffset, thisModFXDelayDepth, feedback,
			                                         AudioEngine::renderInStereo);
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

	*postFXVolume = multiply_32x32_rshift32(*postFXVolume, 1518500250) << 1; // Divide by sqrt(2)
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
		modFXDelayOffset += multiply_32x32_rshift32(
		    kFlangerOffset, (unpatchedParams->getValue(deluge::modulation::params::UNPATCHED_MOD_FX_OFFSET)));
		thisModFXDelayDepth = multiply_32x32_rshift32(modFXDelayOffset, modFXDepth) << 1;
		modFXLFOWaveType = LFOType::WARBLER;
	}

	else { // Phaser
		modFXLFOWaveType = LFOType::SINE;
	}
}
template <ModFXType modFXType>
void ModFXProcessor::processModFXBuffer(StereoSample* buffer, int32_t modFXRate, int32_t modFXDepth,
                                        const StereoSample* bufferEnd, LFOType& modFXLFOWaveType,
                                        int32_t modFXDelayOffset, int32_t thisModFXDelayDepth, int32_t feedback,
                                        bool stereo) {
	StereoSample* currentSample = buffer;
	if constexpr (modFXType == ModFXType::PHASER) {
		do {
			int32_t lfoOutput = modFXLFO.render(1, modFXLFOWaveType, modFXRate);
			processOnePhaserSample(modFXDepth, feedback, currentSample, lfoOutput);

		} while (++currentSample != bufferEnd);
	}
	else if (stereo) {
		do {
			int32_t lfoOutput;
			int32_t lfo2Output;
			processModLFOs<modFXType>(modFXRate, modFXLFOWaveType, lfoOutput, lfo2Output);

			processOneModFXSample<modFXType, true>(modFXDelayOffset, thisModFXDelayDepth, feedback, currentSample,
			                                       lfoOutput, lfo2Output);

		} while (++currentSample != bufferEnd);
	}
	else {
		do {
			int32_t lfoOutput;
			int32_t lfo2Output;
			processModLFOs<modFXType>(modFXRate, modFXLFOWaveType, lfoOutput, lfo2Output);
			processOneModFXSample<modFXType, false>(modFXDelayOffset, thisModFXDelayDepth, feedback, currentSample,
			                                        lfoOutput, -lfoOutput);

		} while (++currentSample != bufferEnd);
	}
}
template <ModFXType modFXType>
void ModFXProcessor::processModLFOs(int32_t modFXRate, LFOType& modFXLFOWaveType, int32_t& lfoOutput,
                                    int32_t& lfo2Output) {
	lfoOutput = modFXLFO.render(1, modFXLFOWaveType, modFXRate);
	// anymore and they get audibly out of sync, this just sounds wobblier
	constexpr q31_t width = 0.97 * ONE_Q31;
	if constexpr (modFXType == ModFXType::WARBLE) {
		// this needs a second lfo because it's a random process - we can't flip it to make a second sample but
		// these will always be different anyway

		lfo2Output = modFXLFOStereo.render(1, modFXLFOWaveType, multiply_32x32_rshift32(modFXRate, width) << 1);
	}
	else {
		lfo2Output = -lfoOutput;
	}
}
template <ModFXType modFXType, bool stereo>
void ModFXProcessor::processOneModFXSample(int32_t modFXDelayOffset, int32_t thisModFXDelayDepth, int32_t feedback,
                                           StereoSample* currentSample, int32_t lfoOutput, int32_t lfo2Output) {
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
		modFXBuffer[modFXBufferWriteIndex].l = modFXOutputL + currentSample->l; // Feedback
		modFXOutputR = multiply_32x32_rshift32_rounded(modFXOutputR, feedback) << 2;
		modFXBuffer[modFXBufferWriteIndex].r = modFXOutputR + currentSample->r; // Feedback
	}
	else if constexpr (modFXType == ModFXType::WARBLE) {
		auto fback = multiply_32x32_rshift32_rounded(modFXOutputL, feedback);
		modFXBuffer[modFXBufferWriteIndex].l = fback + currentSample->l; // Feedback
		fback = multiply_32x32_rshift32_rounded(modFXOutputR, feedback);
		modFXBuffer[modFXBufferWriteIndex].r = fback + currentSample->r; // Feedback

		modFXOutputL <<= 1;
		modFXOutputR <<= 1;
	}
	else { // Chorus, Dimension
		modFXOutputL <<= 1;
		modFXBuffer[modFXBufferWriteIndex].l = currentSample->l; // Feedback
		modFXOutputR <<= 1;
		modFXBuffer[modFXBufferWriteIndex].r = currentSample->r; // Feedback
	}

	if constexpr (modFXType == ModFXType::DIMENSION || modFXType == ModFXType::WARBLE) {
		currentSample->l = modFXOutputL << 1;
		currentSample->r = modFXOutputR << 1;
	}
	else {
		currentSample->l += modFXOutputL;
		currentSample->r += modFXOutputR;
	}

	modFXBufferWriteIndex = (modFXBufferWriteIndex + 1) & kModFXBufferIndexMask;
}
void ModFXProcessor::processOnePhaserSample(int32_t modFXDepth, int32_t feedback, StereoSample* currentSample,
                                            int32_t lfoOutput) { // "1" is sorta represented by 1073741824 here
	int32_t _a1 =
	    1073741824 - multiply_32x32_rshift32_rounded((((uint32_t)lfoOutput + (uint32_t)2147483648) >> 1), modFXDepth);

	phaserMemory.l = currentSample->l + (multiply_32x32_rshift32_rounded(phaserMemory.l, feedback) << 1);
	phaserMemory.r = currentSample->r + (multiply_32x32_rshift32_rounded(phaserMemory.r, feedback) << 1);

	// Do the allpass filters
	for (auto& sample : allpassMemory) {
		StereoSample whatWasInput = phaserMemory;

		phaserMemory.l = (multiply_32x32_rshift32_rounded(phaserMemory.l, -_a1) << 2) + sample.l;
		sample.l = (multiply_32x32_rshift32_rounded(phaserMemory.l, _a1) << 2) + whatWasInput.l;

		phaserMemory.r = (multiply_32x32_rshift32_rounded(phaserMemory.r, -_a1) << 2) + sample.r;
		sample.r = (multiply_32x32_rshift32_rounded(phaserMemory.r, _a1) << 2) + whatWasInput.r;
	}

	currentSample->l += phaserMemory.l;
	currentSample->r += phaserMemory.r;
}

void ModFXProcessor::resetMemory() {
	if (modFXBuffer) {
		memset(modFXBuffer, 0, kModFXBufferSize * sizeof(StereoSample));
	}

	else {
		memset(allpassMemory, 0, sizeof(allpassMemory));
		memset(&phaserMemory, 0, sizeof(phaserMemory));
	}
}

void ModFXProcessor::setupBuffer() {
	if (!modFXBuffer) {
		modFXBuffer = (StereoSample*)delugeAlloc(kModFXBufferSize * sizeof(StereoSample));
		if (modFXBuffer) {
			memset(modFXBuffer, 0, kModFXBufferSize * sizeof(StereoSample));
		}
	}
}
void ModFXProcessor::disableBuffer() {
	if (modFXBuffer) {
		delugeDealloc(modFXBuffer);
		modFXBuffer = nullptr;
	}
}
