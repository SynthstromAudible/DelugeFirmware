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

	if (grainFeedbackVol < 33554432) {
		wrapsToShutdown = 1;
	}
	else if (grainFeedbackVol <= 100663296) {
		wrapsToShutdown = 2;
	}
	else if (grainFeedbackVol <= 218103808) {
		wrapsToShutdown = 3;
	}
	// max possible, feedback doesn't go very high
	else {
		wrapsToShutdown = 4;
	}
	modFXGrainBuffer->inUse = true;
}
void GranularProcessor::processGrainFX(StereoSample* buffer, int32_t modFXRate, int32_t modFXDepth, int32_t offset,
                                       int32_t feedback, int32_t* postFXVolume, const StereoSample* bufferEnd,
                                       bool anySoundComingIn, float tempoBPM) {
	if (anySoundComingIn || wrapsToShutdown >= 0) {
		if (anySoundComingIn) {
			setWrapsToShutdown();
		}
		if (modFXGrainBuffer == nullptr) {
			getBuffer(); // in case it was stolen
		}
		setupGrainFX(modFXRate, modFXDepth, 0, 0, postFXVolume, 0);
		StereoSample* currentSample = buffer;
		do {
			processOneGrainSample(currentSample);

		} while (++currentSample != bufferEnd);

		if (wrapsToShutdown < 0) {
			modFXGrainBuffer->inUse = false;
		}
	}
}
void GranularProcessor::setupGrainFX(int32_t modFXRate, int32_t modFXDepth, int32_t offset, int32_t feedback,
                                     int32_t* postFXVolume, float tempoBPM) {
	if (!grainInitialized && modFXGrainBufferWriteIndex >= 65536) {
		grainInitialized = true;
	}
	*postFXVolume = multiply_32x32_rshift32(*postFXVolume, ONE_OVER_SQRT2_Q31) << 1; // Divide by sqrt(2)
	                                                                                 // Shift
	grainShift = 44 * 300;                                                           //(kSampleRate / 1000) * 300;
	// Size
	grainSize = 44 * (((offset >> 1) + 1073741824) >> 21);
	grainSize = std::clamp<int32_t>(grainSize, 440, 35280); // 10ms - 800ms
	// Rate
	int32_t grainRateRaw = std::clamp<int32_t>((quickLog(modFXRate) - 364249088) >> 21, 0, 256);
	grainRate = ((360 * grainRateRaw >> 8) * grainRateRaw >> 8); // 0 - 180hz
	grainRate = std::max<int32_t>(1, grainRate);
	grainRate = (kSampleRate << 1) / grainRate;
	// Preset 0=default
	grainPitchType = (int8_t)(multiply_32x32_rshift32_rounded(feedback,
	                                                          5)); // Select 5 presets -2 to 2
	grainPitchType = std::clamp<int8_t>(grainPitchType, -2, 2);
	// Tempo sync
	if (grainPitchType == 2) {
		grainRate = std::clamp<int32_t>(256 - grainRateRaw, 0, 256) << 4; // 4096msec
		grainRate = 44 * grainRate;                                       //(kSampleRate*grainRate)/1000;
		auto baseNoteSamples = (int32_t)(kSampleRate * 60. / tempoBPM);   // 4th
		if (grainRate < baseNoteSamples) {
			baseNoteSamples = baseNoteSamples >> 2; // 16th
		}
		grainRate = std::clamp<int32_t>((grainRate / baseNoteSamples) * baseNoteSamples, baseNoteSamples,
		                                baseNoteSamples << 2);            // Quantize
		if (grainRate < 2205) {                                           // 50ms = 20hz
			grainSize = std::min<int32_t>(grainSize, grainRate << 3) - 1; // 16 layers=<<4, 8layers = <<3
		}
		bool currentTickCountIsZero = (playbackHandler.getCurrentInternalTickCount() == 0);
		if (grainLastTickCountIsZero && !currentTickCountIsZero) { // Start Playback
			modFXGrainBufferWriteIndex = 0;                        // Reset WriteIndex
		}
		grainLastTickCountIsZero = currentTickCountIsZero;
	}
	// Rate Adjustment
	if (grainRate < 882) {                                            // 50hz or more
		grainSize = std::min<int32_t>(grainSize, grainRate << 3) - 1; // 16 layers=<<4, 8layers = <<3
	}
	// Volume
	grainVol = modFXDepth - 2147483648;
	grainVol = (multiply_32x32_rshift32_rounded(multiply_32x32_rshift32_rounded(grainVol, grainVol), grainVol) << 2)
	           + 2147483648; // Cubic
	grainVol = std::max<int32_t>(0, std::min<int32_t>(2147483647, grainVol));
	grainDryVol = (int32_t)std::clamp<int64_t>(((int64_t)(2147483648 - grainVol) << 3), 0, 2147483647);
	grainFeedbackVol = grainVol >> 3;
}
void GranularProcessor::processOneGrainSample(StereoSample* currentSample) {
	if (modFXGrainBufferWriteIndex >= kModFXGrainBufferSize) {
		modFXGrainBufferWriteIndex = 0;
		wrapsToShutdown -= 1;
	}
	int32_t writeIndex = modFXGrainBufferWriteIndex; // % kModFXGrainBufferSize
	if (modFXGrainBufferWriteIndex % grainRate == 0) [[unlikely]] {
		for (int32_t i = 0; i < 8; i++) {
			if (grains[i].length <= 0) {
				grains[i].length = grainSize;
				int32_t spray = random(kModFXGrainBufferSize >> 1) - (kModFXGrainBufferSize >> 2);
				grains[i].startPoint = (modFXGrainBufferWriteIndex + kModFXGrainBufferSize - grainShift + spray)
				                       & kModFXGrainBufferIndexMask;
				grains[i].counter = 0;
				grains[i].rev = (getRandom255() < 76);

				int32_t pitchRand = getRandom255();
				switch (grainPitchType) {
				case -2:
					grains[i].pitch = (pitchRand < 76) ? 2048 : 1024; // unison + octave + reverse
					grains[i].rev = 1;
					break;
				case -1:
					grains[i].pitch = (pitchRand < 76) ? 512 : 1024; // unison + octave lower
					break;
				case 0:
					grains[i].pitch = (pitchRand < 76) ? 2048 : 1024; // unison + octave (default)
					break;
				case 1:
					grains[i].pitch = (pitchRand < 76) ? 1534 : 2048; // 5th + octave
					break;
				case 2:
					grains[i].pitch = (pitchRand < 25)    ? 512
					                  : (pitchRand < 153) ? 2048
					                                      : 1024; // unison + octave + octave lower
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
						if (modFXGrainBufferWriteIndex > 13231) {
							int32_t newStartPoint = std::max<int32_t>(440, random(modFXGrainBufferWriteIndex - 2));
							grains[i].startPoint =
							    (writeIndex - newStartPoint + kModFXGrainBufferSize) & kModFXGrainBufferIndexMask;
						}
						else {
							grains[i].length = 0;
						}
					}
					else {
						grains[i].pitch = std::min<int32_t>(grains[i].pitch, 1024);
						if (modFXGrainBufferWriteIndex > 13231) {
							grains[i].length = std::min<int32_t>(grains[i].length, modFXGrainBufferWriteIndex - 2);
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
			    grains_l, multiply_32x32_rshift32((*modFXGrainBuffer)[pos].l, vol) << 0, grains[i].panVolL);
			grains_r = multiply_accumulate_32x32_rshift32_rounded(
			    grains_r, multiply_32x32_rshift32((*modFXGrainBuffer)[pos].r, vol) << 0, grains[i].panVolR);

			grains[i].counter++;
			if (grains[i].counter >= grains[i].length) {
				grains[i].length = 0;
			}
		}
	}

	grains_l <<= 3;
	grains_r <<= 3;
	// Feedback (Below grainFeedbackVol means "grainVol >> 4")
	(*modFXGrainBuffer)[writeIndex].l =
	    multiply_accumulate_32x32_rshift32_rounded(currentSample->l, grains_l, grainFeedbackVol);
	(*modFXGrainBuffer)[writeIndex].r =
	    multiply_accumulate_32x32_rshift32_rounded(currentSample->r, grains_r, grainFeedbackVol);
	// WET and DRY Vol
	currentSample->l = add_saturation(multiply_32x32_rshift32(currentSample->l, grainDryVol) << 1,
	                                  multiply_32x32_rshift32(grains_l, grainVol) << 1);
	currentSample->r = add_saturation(multiply_32x32_rshift32(currentSample->r, grainDryVol) << 1,
	                                  multiply_32x32_rshift32(grains_r, grainVol) << 1);
	modFXGrainBufferWriteIndex++;
}
void GranularProcessor::clearGrainFXBuffer() {
	for (int i = 0; i < 8; i++) {
		grains[i].length = 0;
	}
	grainInitialized = false;
	modFXGrainBufferWriteIndex = 0;
	modFXGrainBuffer->clearBuffer();
}
GranularProcessor::GranularProcessor() {
	wrapsToShutdown = 0;
	modFXGrainBufferWriteIndex = 0;
	grainShift = 13230; // 300ms
	grainSize = 13230;  // 300ms
	grainRate = 1260;   // 35hz
	grainFeedbackVol = 161061273;
	for (int i = 0; i < 8; i++) {
		GranularProcessor::grains[i].length = 0;
	}
	grainVol = 0;
	grainDryVol = 2147483647;
	grainPitchType = 0;
	grainLastTickCountIsZero = true;
	grainInitialized = false;
	getBuffer();
}
void GranularProcessor::getBuffer() {
	void* grainBufferMemory = GeneralMemoryAllocator::get().allocStealable(sizeof(GrainBuffer));
	modFXGrainBuffer = new (grainBufferMemory) GrainBuffer(this);
}
GranularProcessor::~GranularProcessor() {
	delete modFXGrainBuffer;
}
GranularProcessor::GranularProcessor(const GranularProcessor& other) {
	wrapsToShutdown = other.wrapsToShutdown;
	modFXGrainBufferWriteIndex = other.modFXGrainBufferWriteIndex;
	grainShift = other.grainShift; // 300ms
	grainSize = other.grainSize;   // 300ms
	grainRate = other.grainRate;   // 35hz
	grainFeedbackVol = other.grainFeedbackVol;
	for (int i = 0; i < 8; i++) {
		GranularProcessor::grains[i].length = 0;
	}
	grainVol = other.grainVol;
	grainDryVol = other.grainDryVol;
	grainPitchType = other.grainPitchType;
	grainLastTickCountIsZero = true;
	grainInitialized = false;
	void* grainBufferMemory = GeneralMemoryAllocator::get().allocStealable(sizeof(GrainBuffer));
	modFXGrainBuffer = new (grainBufferMemory) GrainBuffer(this);
}
void GranularProcessor::startSkippingRendering() {
	if (modFXGrainBuffer) {
		modFXGrainBuffer->inUse = false;
	}
}
