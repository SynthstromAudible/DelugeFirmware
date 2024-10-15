/*
 * Copyright © 2024 Mark Adams and Alter-Alter
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

#include "GranularProcessor.h"
#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "model/fx/stutterer.h"
#include "model/mod_controllable/mod_controllable.h"
#include "modulation/lfo.h"
#include "playback/playback_handler.h"

void GranularProcessor::setWrapsToShutdown() {

	if (_grainFeedbackVol < 33554432) {
		wrapsToShutdown = 1;
	}
	else if (_grainFeedbackVol <= 100663296) {
		wrapsToShutdown = 2;
	}
	else if (_grainFeedbackVol <= 218103808) {
		wrapsToShutdown = 3;
	}
	// max possible, feedback doesn't go very high
	else {
		wrapsToShutdown = 4;
	}
	grainBuffer->inUse = true;
}

void GranularProcessor::processGrainFX(StereoSample* buffer, int32_t grainRate, int32_t grainMix, int32_t grainDensity,
                                       int32_t pitchRandomness, int32_t* postFXVolume, const StereoSample* bufferEnd,
                                       bool anySoundComingIn, float tempoBPM) {
	if (anySoundComingIn || wrapsToShutdown >= 0) {
		if (anySoundComingIn) {
			setWrapsToShutdown();
		}
		if (grainBuffer == nullptr) {
			getBuffer(); // in case it was stolen
		}
		setupGrainFX(grainRate, grainMix, grainDensity, pitchRandomness, postFXVolume, tempoBPM);
		StereoSample* currentSample = buffer;
		int i = 0;
		do {
			StereoSample grainWet = processOneGrainSample(currentSample);
			auto wetl = multiply_32x32_rshift32(grainWet.l, _grainVol) << 1;
			auto wetr = multiply_32x32_rshift32(grainWet.r, _grainVol) << 1;
			// WET and DRY Vol
			currentSample->l = add_saturation(multiply_32x32_rshift32(currentSample->l, _grainDryVol) << 1, wetl);
			currentSample->r = add_saturation(multiply_32x32_rshift32(currentSample->r, _grainDryVol) << 1, wetr);
			// adding a small amount of extra reverb covers a lot of the granular artifacts
			AudioEngine::feedReverbBackdoorForGrain(i, (wetl + wetr) >> 3);
			i += 1;

		} while (++currentSample != bufferEnd);

		if (wrapsToShutdown < 0) {
			grainBuffer->inUse = false;
		}
	}
}
void GranularProcessor::setupGrainFX(int32_t grainRate, int32_t grainMix, int32_t grainDensity, int32_t pitchRandomness,
                                     int32_t* postFXVolume, float tempoBPM) {
	if (!grainInitialized && bufferWriteIndex >= 65536) {
		grainInitialized = true;
	}
	*postFXVolume = multiply_32x32_rshift32(*postFXVolume, ONE_OVER_SQRT2_Q31) << 1; // Divide by sqrt(2)
	                                                                                 // Shift
	_grainShift = 44 * 300;                                                          //(kSampleRate / 1000) * 300;
	// Size
	q31_t density = ((grainDensity / 2) + (1073741824)); // convert to 0-2^31
	// the maximum length is 8x the rate, past that grains get stolen for new grains. This keeps a consistent proportion
	// of grain sound as you increase the rate
	_grainSize = 1760 + q31_mult(_grainRate << 3, density);
	// Rate
	int32_t grainRateRaw = std::clamp<int32_t>((quickLog(grainRate) - 364249088) >> 21, 0, 256);
	_grainRate = ((360 * grainRateRaw >> 8) * grainRateRaw >> 8); // 0 - 180hz
	_grainRate = std::max<int32_t>(1, _grainRate);
	_grainRate = (kSampleRate << 1) / _grainRate;

	_pitchRandomness = toPositive(pitchRandomness);
	// Tempo sync
	if (false) {
		_grainRate = std::clamp<int32_t>(256 - grainRateRaw, 0, 256) << 4; // 4096msec
		_grainRate = 44 * _grainRate;                                      //(kSampleRate*grainRate)/1000;
		auto baseNoteSamples = (int32_t)(kSampleRate * 60. / tempoBPM);    // 4th
		if (_grainRate < baseNoteSamples) {
			baseNoteSamples = baseNoteSamples >> 2; // 16th
		}
		_grainRate = std::clamp<int32_t>((_grainRate / baseNoteSamples) * baseNoteSamples, baseNoteSamples,
		                                 baseNoteSamples << 2);              // Quantize
		if (_grainRate < 2205) {                                             // 50ms = 20hz
			_grainSize = std::min<int32_t>(_grainSize, _grainRate << 3) - 1; // 16 layers=<<4, 8layers = <<3
		}
		bool currentTickCountIsZero = (playbackHandler.getCurrentInternalTickCount() == 0);
		if (grainLastTickCountIsZero && !currentTickCountIsZero) { // Start Playback
			bufferWriteIndex = 0;                                  // Reset WriteIndex
		}
		grainLastTickCountIsZero = currentTickCountIsZero;
	}

	// Volume
	_grainVol = grainMix - 2147483648;
	_grainVol = (multiply_32x32_rshift32_rounded(multiply_32x32_rshift32_rounded(_grainVol, _grainVol), _grainVol) << 2)
	            + 2147483648; // Cubic
	_grainVol = std::max<int32_t>(0, std::min<int32_t>(2147483647, _grainVol));
	_grainDryVol = (int32_t)std::clamp<int64_t>(((int64_t)(2147483648 - _grainVol) << 3), 0, 2147483647);
	_grainFeedbackVol = _grainVol >> 2;
}
StereoSample GranularProcessor::processOneGrainSample(StereoSample* currentSample) {
	if (bufferWriteIndex >= kModFXGrainBufferSize) {
		bufferWriteIndex = 0;
		wrapsToShutdown -= 1;
	}
	int32_t writeIndex = bufferWriteIndex; // % kModFXGrainBufferSize
	if (bufferWriteIndex % _grainRate == 0) [[unlikely]] {
		for (int32_t i = 0; i < 8; i++) {
			if (grains[i].length <= 0) {
				grains[i].length = _grainSize;
				int32_t spray = random(kModFXGrainBufferSize >> 1) - (kModFXGrainBufferSize >> 2);
				grains[i].startPoint =
				    (bufferWriteIndex + kModFXGrainBufferSize - _grainShift + spray) & kModFXGrainBufferIndexMask;
				grains[i].counter = 0;
				grains[i].rev = (getRandom255() < 76);

				int32_t pitchRand = getRandom255();
				// randomly select a type of grain to generate, options are based on the amount of randomness
				int8_t typeRand = multiply_32x32_rshift32(q31_mult(getNoise(), _pitchRandomness), 7);
				switch (typeRand) {

				case -3:
					grains[i].pitch = 512; // octave down
					grains[i].rev = true;
					break;
				case -2:
					grains[i].pitch = 767; // 4th down (e.g. it's the 5th)
					grains[i].rev = true;
					break;
				case -1:
					grains[i].pitch = 1024; // unison reverse
					grains[i].rev = true;
					break;
				case 0:
					grains[i].pitch = 1024; // unison
					break;
				case 1:
					grains[i].pitch = 2048; //  octave
					break;
				case 2:
					grains[i].pitch = 1534; // 5th
					break;
				case 3:
					grains[i].pitch = 2048; //  octave
					grains[i].rev = true;
					break;
					// This is pretty rare even at max randomness
				default:
					grains[i].pitch = 30772; //  octave + 5th
					grains[i].rev = true;
					break;
				}
				if (grains[i].rev) {
					grains[i].startPoint = (writeIndex + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
					grains[i].length = (grains[i].pitch > 1024)
					                       ? std::min<int32_t>(grains[i].length, 21659)  // Buffer length*0.3305
					                       : std::min<int32_t>(grains[i].length, 30251); // 1.48s - 0.8s
				}
				else {
					if (grains[i].pitch > 1024) {
						int32_t startPointMax = (writeIndex + grains[i].length
						                         - ((grains[i].length * grains[i].pitch) >> 10) + kModFXGrainBufferSize)
						                        & kModFXGrainBufferIndexMask;
						if (!(grains[i].startPoint < startPointMax && grains[i].startPoint > writeIndex)) {
							grains[i].startPoint =
							    (startPointMax + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
						}
					}
					else if (grains[i].pitch < 1024) {
						int32_t startPointMax = (writeIndex + grains[i].length
						                         - ((grains[i].length * grains[i].pitch) >> 10) + kModFXGrainBufferSize)
						                        & kModFXGrainBufferIndexMask;

						if (!(grains[i].startPoint > startPointMax && grains[i].startPoint < writeIndex)) {
							grains[i].startPoint =
							    (writeIndex + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
						}
					}
				}
				if (!grainInitialized) {
					if (!grains[i].rev) { // forward
						grains[i].pitch = 1024;
						if (bufferWriteIndex > 13231) {
							int32_t newStartPoint = std::max<int32_t>(440, random(bufferWriteIndex - 2));
							grains[i].startPoint =
							    (writeIndex - newStartPoint + kModFXGrainBufferSize) & kModFXGrainBufferIndexMask;
						}
						else {
							grains[i].length = 0;
						}
					}
					else {
						grains[i].pitch = std::min<int32_t>(grains[i].pitch, 1024);
						if (bufferWriteIndex > 13231) {
							grains[i].length = std::min<int32_t>(grains[i].length, bufferWriteIndex - 2);
							grains[i].startPoint =
							    (writeIndex - 1 + kModFXGrainBufferSize) & kModFXGrainBufferIndexMask;
						}
						else {
							grains[i].length = 0;
						}
					}
				}
				if (grains[i].length > 0) {
					grains[i].volScale = (2147483647 / (grains[i].length >> 1));
					grains[i].volScaleMax = grains[i].volScale * (grains[i].length >> 1);
					shouldDoPanning((getRandom255() - 128) << 23, &grains[i].panVolL,
					                &grains[i].panVolR); // Pan Law 0
				}
				break;
			}
		}
	}

	int32_t grains_l = 0;
	int32_t grains_r = 0;
	for (int32_t i = 0; i < 8; i++) {
		if (grains[i].length > 0) {
			// triangle window
			int32_t vol =
			    grains[i].counter <= (grains[i].length >> 1)
			        ? grains[i].counter * grains[i].volScale
			        : grains[i].volScaleMax - (grains[i].counter - (grains[i].length >> 1)) * grains[i].volScale;
			int32_t delta = grains[i].counter * (grains[i].rev == 1 ? -1 : 1);
			if (grains[i].pitch != 1024) {
				delta = ((delta * grains[i].pitch) >> 10);
			}
			int32_t pos = (grains[i].startPoint + delta + kModFXGrainBufferSize) & kModFXGrainBufferIndexMask;
			grains_l = multiply_accumulate_32x32_rshift32_rounded(
			    grains_l, multiply_32x32_rshift32((*grainBuffer)[pos].l, vol) << 0, grains[i].panVolL);
			grains_r = multiply_accumulate_32x32_rshift32_rounded(
			    grains_r, multiply_32x32_rshift32((*grainBuffer)[pos].r, vol) << 0, grains[i].panVolR);

			grains[i].counter++;
			if (grains[i].counter >= grains[i].length) {
				grains[i].length = 0;
			}
		}
	}

	grains_l <<= 3;
	grains_r <<= 3;
	// Feedback (Below grainFeedbackVol means "grainVol >> 4")
	(*grainBuffer)[writeIndex].l =
	    multiply_accumulate_32x32_rshift32_rounded(currentSample->l, grains_l, _grainFeedbackVol);
	(*grainBuffer)[writeIndex].r =
	    multiply_accumulate_32x32_rshift32_rounded(currentSample->r, grains_r, _grainFeedbackVol);

	bufferWriteIndex++;
	return StereoSample{grains_l, grains_r};
}
void GranularProcessor::clearGrainFXBuffer() {
	for (int i = 0; i < 8; i++) {
		grains[i].length = 0;
	}
	grainInitialized = false;
	bufferWriteIndex = 0;
	grainBuffer->clearBuffer();
}
GranularProcessor::GranularProcessor() {
	wrapsToShutdown = 0;
	bufferWriteIndex = 0;
	_grainShift = 13230; // 300ms
	_grainSize = 13230;  // 300ms
	_grainRate = 1260;   // 35hz
	_grainFeedbackVol = 161061273;
	for (int i = 0; i < 8; i++) {
		GranularProcessor::grains[i].length = 0;
	}
	_grainVol = 0;
	_grainDryVol = 2147483647;
	_pitchRandomness = 0;
	grainLastTickCountIsZero = true;
	grainInitialized = false;
	getBuffer();
}
void GranularProcessor::getBuffer() {
	void* grainBufferMemory = GeneralMemoryAllocator::get().allocStealable(sizeof(GrainBuffer));
	grainBuffer = new (grainBufferMemory) GrainBuffer(this);
}
GranularProcessor::~GranularProcessor() {
	delete grainBuffer;
}
GranularProcessor::GranularProcessor(const GranularProcessor& other) {
	wrapsToShutdown = other.wrapsToShutdown;
	bufferWriteIndex = other.bufferWriteIndex;
	_grainShift = other._grainShift; // 300ms
	_grainSize = other._grainSize;   // 300ms
	_grainRate = other._grainRate;   // 35hz
	_grainFeedbackVol = other._grainFeedbackVol;
	for (int i = 0; i < 8; i++) {
		GranularProcessor::grains[i].length = 0;
	}
	_grainVol = other._grainVol;
	_grainDryVol = other._grainDryVol;
	_pitchRandomness = other._pitchRandomness;
	grainLastTickCountIsZero = true;
	grainInitialized = false;
	void* grainBufferMemory = GeneralMemoryAllocator::get().allocStealable(sizeof(GrainBuffer));
	grainBuffer = new (grainBufferMemory) GrainBuffer(this);
}
void GranularProcessor::startSkippingRendering() {
	if (grainBuffer) {
		grainBuffer->inUse = false;
	}
}
