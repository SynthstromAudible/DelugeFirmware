/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "model/mod_controllable/mod_controllable_audio.h"
#include "definitions_cxx.hpp"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "gui/l10n/l10n.h"
#include "gui/views/midi_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "io/debug/print.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "util/fast_fixed_math.h"
#include "util/misc.h"
#include <string.h>
extern "C" {}

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

ModControllableAudio::ModControllableAudio() {

	// Mod FX
	modFXBuffer = NULL;
	modFXBufferWriteIndex = 0;

	//Grain
	modFXGrainBuffer = NULL;
	wrapsToShutdown = 0;
	modFXGrainBufferWriteIndex = 0;
	grainShift = 13230; // 300ms
	grainSize = 13230;  // 300ms
	grainRate = 1260;   // 35hz
	grainFeedbackVol = 161061273;
	for (int i = 0; i < 8; i++) {
		grains[i].length = 0;
	}
	grainVol = 0;
	grainDryVol = 2147483647;
	grainPitchType = 0;
	grainLastTickCountIsZero = true;
	grainInitialized = false;

	// EQ
	withoutTrebleL = 0;
	bassOnlyL = 0;
	withoutTrebleR = 0;
	bassOnlyR = 0;

	// Stutter
	stutterer.sync = 7;
	stutterer.status = STUTTERER_STATUS_OFF;
	lpfMode = FilterMode::TRANSISTOR_24DB;
	hpfMode = FilterMode::HPLADDER;
	filterRoute = FilterRoute::HIGH_TO_LOW;

	// Sample rate reduction
	sampleRateReductionOnLastTime = false;

	// Saturation
	clippingAmount = 0;

	// Midi follow Mode
	for (int32_t xDisplay; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay; yDisplay < kDisplayHeight; yDisplay++) {
			timeLastSentCC[xDisplay][yDisplay] = 0;
		}
	}
}

ModControllableAudio::~ModControllableAudio() {
	// delay.discardBuffers(); // No! The DelayBuffers will themselves destruct and do this

	// Free the mod fx memory
	if (modFXBuffer) {
		delugeDealloc(modFXBuffer);
	}
	if (modFXGrainBuffer) {
		delugeDealloc(modFXGrainBuffer);
	}
}

void ModControllableAudio::cloneFrom(ModControllableAudio* other) {
	lpfMode = other->lpfMode;
	hpfMode = other->hpfMode;
	clippingAmount = other->clippingAmount;
	modFXType = other->modFXType;
	bassFreq = other->bassFreq; // Eventually, these shouldn't be variables like this
	trebleFreq = other->trebleFreq;
	filterRoute = other->filterRoute;
	compressor.cloneFrom(&other->compressor);
	midiKnobArray.cloneFrom(&other->midiKnobArray); // Could fail if no RAM... not too big a concern
	delay.cloneFrom(&other->delay);
}

void ModControllableAudio::initParams(ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->params[Param::Unpatched::BASS].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[Param::Unpatched::TREBLE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[Param::Unpatched::BASS_FREQ].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[Param::Unpatched::TREBLE_FREQ].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[Param::Unpatched::STUTTER_RATE].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[Param::Unpatched::MOD_FX_OFFSET].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[Param::Unpatched::SAMPLE_RATE_REDUCTION].setCurrentValueBasicForSetup(-2147483648);

	unpatchedParams->params[Param::Unpatched::BITCRUSHING].setCurrentValueBasicForSetup(-2147483648);

	unpatchedParams->params[Param::Unpatched::COMPRESSOR_SHAPE].setCurrentValueBasicForSetup(-601295438);
}

bool ModControllableAudio::hasBassAdjusted(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(Param::Unpatched::BASS) != 0);
}

bool ModControllableAudio::hasTrebleAdjusted(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(Param::Unpatched::TREBLE) != 0);
}

void ModControllableAudio::setWrapsToShutdown() {

	if (grainFeedbackVol < 33554432) {
		wrapsToShutdown = 1;
	}
	else if (grainFeedbackVol <= 100663296) {
		wrapsToShutdown = 2;
	}
	else if (grainFeedbackVol <= 218103808) {
		wrapsToShutdown = 3;
	}
	//max possible, feedback doesn't go very high
	else {
		wrapsToShutdown = 4;
	}
}

void ModControllableAudio::processFX(StereoSample* buffer, int32_t numSamples, ModFXType modFXType, int32_t modFXRate,
                                     int32_t modFXDepth, DelayWorkingState* delayWorkingState, int32_t* postFXVolume,
                                     ParamManager* paramManager, int32_t analogDelaySaturationAmount) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	StereoSample* bufferEnd = buffer + numSamples;

	// Mod FX -----------------------------------------------------------------------------------
	if (modFXType != ModFXType::NONE) {

		LFOType modFXLFOWaveType;
		int32_t modFXDelayOffset;
		int32_t thisModFXDelayDepth;
		int32_t feedback;

		if (modFXType == ModFXType::FLANGER || modFXType == ModFXType::PHASER) {

			int32_t a = unpatchedParams->getValue(Param::Unpatched::MOD_FX_FEEDBACK) >> 1;
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
			}
			// Though, this would be more ideally placed affecting volume before the flanger

			if (modFXType == ModFXType::FLANGER) {
				modFXDelayOffset = kFlangerOffset;
				thisModFXDelayDepth = kFlangerAmplitude;
				modFXLFOWaveType = LFOType::TRIANGLE;
			}
			else { // Phaser
				modFXLFOWaveType = LFOType::SINE;
			}
		}
		else if (modFXType == ModFXType::CHORUS || modFXType == ModFXType::CHORUS_STEREO) {
			modFXDelayOffset = multiply_32x32_rshift32(
			    kModFXMaxDelay, (unpatchedParams->getValue(Param::Unpatched::MOD_FX_OFFSET) >> 1) + 1073741824);
			thisModFXDelayDepth = multiply_32x32_rshift32(modFXDelayOffset, modFXDepth) << 2;
			modFXLFOWaveType = LFOType::SINE;
			*postFXVolume = multiply_32x32_rshift32(*postFXVolume, 1518500250) << 1; // Divide by sqrt(2)
		}
		else if (modFXType == ModFXType::GRAIN) {
			AudioEngine::logAction("grain start");
			if (!grainInitialized && modFXGrainBufferWriteIndex >= 65536) {
				grainInitialized = true;
			}
			*postFXVolume = multiply_32x32_rshift32(*postFXVolume, ONE_OVER_SQRT2_Q31) << 1; // Divide by sqrt(2)
			// Shift
			grainShift = 44 * 300; //(kSampleRate / 1000) * 300;
			// Size
			grainSize = 44 * ((((unpatchedParams->getValue(Param::Unpatched::MOD_FX_OFFSET) >> 1) + 1073741824) >> 21));
			grainSize = std::clamp<int32_t>(grainSize, 440, 35280); //10ms - 800ms
			// Rate
			int32_t grainRateRaw = std::clamp<int32_t>((quickLog(modFXRate) - 364249088) >> 21, 0, 256);
			grainRate = ((360 * grainRateRaw >> 8) * grainRateRaw >> 8); //0 - 180hz
			grainRate = std::max<int32_t>(1, grainRate);
			grainRate = (kSampleRate << 1) / grainRate;
			// Preset 0=default
			grainPitchType = (int8_t)(multiply_32x32_rshift32_rounded(
			    unpatchedParams->getValue(Param::Unpatched::MOD_FX_FEEDBACK), 5)); //Select 5 presets -2 to 2
			grainPitchType = std::clamp<int8_t>(grainPitchType, -2, 2);
			// Temposync
			if (grainPitchType == 2) {
				int tempoBPM = (int32_t)(playbackHandler.calculateBPM(currentSong->getTimePerTimerTickFloat()) + 0.5);
				grainRate = std::clamp<int32_t>(256 - grainRateRaw, 0, 256) << 4; //4096msec
				grainRate = 44 * grainRate;                                       //(kSampleRate*grainRate)/1000;
				int32_t baseNoteSamples = (kSampleRate * 60 / tempoBPM);          //4th
				if (grainRate < baseNoteSamples) {
					baseNoteSamples = baseNoteSamples >> 2; //16th
				}
				grainRate = std::clamp<int32_t>((grainRate / baseNoteSamples) * baseNoteSamples, baseNoteSamples,
				                                baseNoteSamples << 2);            //Quantize
				if (grainRate < 2205) {                                           //50ms = 20hz
					grainSize = std::min<int32_t>(grainSize, grainRate << 3) - 1; //16 layers=<<4, 8layers = <<3
				}
				bool currentTickCountIsZero = (playbackHandler.getCurrentInternalTickCount() == 0);
				if (grainLastTickCountIsZero && currentTickCountIsZero == false) { //Start Playback
					modFXGrainBufferWriteIndex = 0;                                //Reset WriteIndex
				}
				grainLastTickCountIsZero = currentTickCountIsZero;
			}
			// Rate Adjustment
			if (grainRate < 882) {                                            //50hz or more
				grainSize = std::min<int32_t>(grainSize, grainRate << 3) - 1; //16 layers=<<4, 8layers = <<3
			}
			// Volume
			grainVol = modFXDepth - 2147483648;
			grainVol =
			    (multiply_32x32_rshift32_rounded(multiply_32x32_rshift32_rounded(grainVol, grainVol), grainVol) << 2)
			    + 2147483648; //Cubic
			grainVol = std::max<int32_t>(0, std::min<int32_t>(2147483647, grainVol));
			grainDryVol = (int32_t)std::clamp<int64_t>(((int64_t)(2147483648 - grainVol) << 3), 0, 2147483647);
			grainFeedbackVol = grainVol >> 3;
		}

		StereoSample* currentSample = buffer;
		do {

			int32_t lfoOutput = modFXLFO.render(1, modFXLFOWaveType, modFXRate);

			if (modFXType == ModFXType::PHASER) {

				// "1" is sorta represented by 1073741824 here
				int32_t _a1 =
				    1073741824
				    - multiply_32x32_rshift32_rounded((((uint32_t)lfoOutput + (uint32_t)2147483648) >> 1), modFXDepth);

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
			else if (modFXType == ModFXType::GRAIN && modFXGrainBuffer) {
				if (modFXGrainBufferWriteIndex >= kModFXGrainBufferSize) {
					modFXGrainBufferWriteIndex = 0;
					wrapsToShutdown -= 1;
				}
				int32_t writeIndex = modFXGrainBufferWriteIndex; // % kModFXGrainBufferSize
				if (modFXGrainBufferWriteIndex % grainRate == 0) {
					for (int32_t i = 0; i < 8; i++) {
						if (grains[i].length <= 0) {
							grains[i].length = grainSize;
							int32_t spray = random(kModFXGrainBufferSize >> 1) - (kModFXGrainBufferSize >> 2);
							grains[i].startPoint =
							    (modFXGrainBufferWriteIndex + kModFXGrainBufferSize - grainShift + spray)
							    & kModFXGrainBufferIndexMask;
							grains[i].counter = 0;
							grains[i].rev = (getRandom255() < 76);

							int32_t pitchRand = getRandom255();
							switch (grainPitchType) {
							case -2:
								grains[i].pitch = (pitchRand < 76) ? 2048 : 1024; //unison + octave + reverse
								grains[i].rev = 1;
								break;
							case -1:
								grains[i].pitch = (pitchRand < 76) ? 512 : 1024; //unison + octave lower
								break;
							case 0:
								grains[i].pitch = (pitchRand < 76) ? 2048 : 1024; //unison + octave (default)
								break;
							case 1:
								grains[i].pitch = (pitchRand < 76) ? 1534 : 2048; //5th + octave
								break;
							case 2:
								grains[i].pitch = (pitchRand < 25)    ? 512
								                  : (pitchRand < 153) ? 2048
								                                      : 1024; // unison + octave + octave lower
								break;
							}
							if (grains[i].rev) {
								grains[i].startPoint =
								    (writeIndex + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
								grains[i].length =
								    (grains[i].pitch > 1024)
								        ? std::min<int32_t>(grains[i].length, 21659)  // Buffer length*0.3305
								        : std::min<int32_t>(grains[i].length, 30251); // 1.48s - 0.8s
							}
							else {
								if (grains[i].pitch > 1024) {
									int32_t startPointMax =
									    (writeIndex + grains[i].length - ((grains[i].length * grains[i].pitch) >> 10)
									     + kModFXGrainBufferSize)
									    & kModFXGrainBufferIndexMask;
									if (!(grains[i].startPoint < startPointMax && grains[i].startPoint > writeIndex)) {
										grains[i].startPoint =
										    (startPointMax + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
									}
								}
								else if (grains[i].pitch < 1024) {
									int32_t startPointMax =
									    (writeIndex + grains[i].length - ((grains[i].length * grains[i].pitch) >> 10)
									     + kModFXGrainBufferSize)
									    & kModFXGrainBufferIndexMask;

									if (!(grains[i].startPoint > startPointMax && grains[i].startPoint < writeIndex)) {
										grains[i].startPoint =
										    (writeIndex + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
									}
								}
							}
							if (!grainInitialized) {
								if (!grains[i].rev) { //forward
									grains[i].pitch = 1024;
									if (modFXGrainBufferWriteIndex > 13231) {
										int32_t newStartPoint =
										    std::max<int32_t>(440, random(modFXGrainBufferWriteIndex - 2));
										grains[i].startPoint = (writeIndex - newStartPoint + kModFXGrainBufferSize)
										                       & kModFXGrainBufferIndexMask;
									}
									else {
										grains[i].length = 0;
									}
								}
								else {
									grains[i].pitch = std::min<int32_t>(grains[i].pitch, 1024);
									if (modFXGrainBufferWriteIndex > 13231) {
										grains[i].length =
										    std::min<int32_t>(grains[i].length, modFXGrainBufferWriteIndex - 2);
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
								                &grains[i].panVolR); //Pan Law 0
							}
							break;
						}
					}
				}

				int32_t grains_l = 0;
				int32_t grains_r = 0;
				for (int32_t i = 0; i < 8; i++) {
					if (grains[i].length > 0) {
						//triangle window
						int32_t vol = grains[i].counter <= (grains[i].length >> 1)
						                  ? grains[i].counter * grains[i].volScale
						                  : grains[i].volScaleMax
						                        - (grains[i].counter - (grains[i].length >> 1)) * grains[i].volScale;
						int32_t delta = grains[i].counter * (grains[i].rev == 1 ? -1 : 1);
						if (grains[i].pitch != 1024) {
							delta = ((delta * grains[i].pitch) >> 10);
						}
						int32_t pos =
						    (grains[i].startPoint + delta + kModFXGrainBufferSize) & kModFXGrainBufferIndexMask;

						grains_l = multiply_accumulate_32x32_rshift32_rounded(
						    grains_l, multiply_32x32_rshift32(modFXGrainBuffer[pos].l, vol) << 0, grains[i].panVolL);
						grains_r = multiply_accumulate_32x32_rshift32_rounded(
						    grains_r, multiply_32x32_rshift32(modFXGrainBuffer[pos].r, vol) << 0, grains[i].panVolR);

						grains[i].counter++;
						if (grains[i].counter >= grains[i].length) {
							grains[i].length = 0;
						}
					}
				}

				grains_l <<= 3;
				grains_r <<= 3;
				//Feedback (Below grainFeedbackVol means "grainVol >> 4")
				modFXGrainBuffer[writeIndex].l =
				    multiply_accumulate_32x32_rshift32_rounded(currentSample->l, grains_l, grainFeedbackVol);
				modFXGrainBuffer[writeIndex].r =
				    multiply_accumulate_32x32_rshift32_rounded(currentSample->r, grains_r, grainFeedbackVol);
				//WET and DRY Vol
				currentSample->l = add_saturation(multiply_32x32_rshift32(currentSample->l, grainDryVol) << 1,
				                                  multiply_32x32_rshift32(grains_l, grainVol) << 1);
				currentSample->r = add_saturation(multiply_32x32_rshift32(currentSample->r, grainDryVol) << 1,
				                                  multiply_32x32_rshift32(grains_r, grainVol) << 1);
				modFXGrainBufferWriteIndex++;
			}
			else {

				int32_t delayTime = multiply_32x32_rshift32(lfoOutput, thisModFXDelayDepth) + modFXDelayOffset;

				int32_t strength2 = (delayTime & 65535) << 15;
				int32_t strength1 = (65535 << 15) - strength2;
				int32_t sample1Pos = modFXBufferWriteIndex - ((delayTime) >> 16);

				int32_t scaledValue1L =
				    multiply_32x32_rshift32_rounded(modFXBuffer[sample1Pos & kModFXBufferIndexMask].l, strength1);
				int32_t scaledValue2L =
				    multiply_32x32_rshift32_rounded(modFXBuffer[(sample1Pos - 1) & kModFXBufferIndexMask].l, strength2);
				int32_t modFXOutputL = scaledValue1L + scaledValue2L;

				if (modFXType == ModFXType::CHORUS_STEREO) {
					delayTime = multiply_32x32_rshift32(lfoOutput, -thisModFXDelayDepth) + modFXDelayOffset;
					strength2 = (delayTime & 65535) << 15;
					strength1 = (65535 << 15) - strength2;
					sample1Pos = modFXBufferWriteIndex - ((delayTime) >> 16);
				}

				int32_t scaledValue1R =
				    multiply_32x32_rshift32_rounded(modFXBuffer[sample1Pos & kModFXBufferIndexMask].r, strength1);
				int32_t scaledValue2R =
				    multiply_32x32_rshift32_rounded(modFXBuffer[(sample1Pos - 1) & kModFXBufferIndexMask].r, strength2);
				int32_t modFXOutputR = scaledValue1R + scaledValue2R;

				if (modFXType == ModFXType::FLANGER) {
					modFXOutputL = multiply_32x32_rshift32_rounded(modFXOutputL, feedback) << 2;
					modFXBuffer[modFXBufferWriteIndex].l = modFXOutputL + currentSample->l; // Feedback
					modFXOutputR = multiply_32x32_rshift32_rounded(modFXOutputR, feedback) << 2;
					modFXBuffer[modFXBufferWriteIndex].r = modFXOutputR + currentSample->r; // Feedback
				}

				else { // Chorus
					modFXOutputL <<= 1;
					modFXBuffer[modFXBufferWriteIndex].l = currentSample->l; // Feedback
					modFXOutputR <<= 1;
					modFXBuffer[modFXBufferWriteIndex].r = currentSample->r; // Feedback
				}

				currentSample->l += modFXOutputL;
				currentSample->r += modFXOutputR;
				modFXBufferWriteIndex = (modFXBufferWriteIndex + 1) & kModFXBufferIndexMask;
			}
		} while (++currentSample != bufferEnd);
		if (modFXType == ModFXType::GRAIN) {
			AudioEngine::logAction("grain end");
		}
	}

	// EQ -------------------------------------------------------------------------------------
	bool thisDoBass = hasBassAdjusted(paramManager);
	bool thisDoTreble = hasTrebleAdjusted(paramManager);

	// Bass. No-change represented by 0. Off completely represented by -536870912
	int32_t positive = (unpatchedParams->getValue(Param::Unpatched::BASS) >> 1) + 1073741824;
	int32_t bassAmount = (multiply_32x32_rshift32_rounded(positive, positive) << 1) - 536870912;

	// Treble. No-change represented by 536870912
	positive = (unpatchedParams->getValue(Param::Unpatched::TREBLE) >> 1) + 1073741824;
	int32_t trebleAmount = multiply_32x32_rshift32_rounded(positive, positive) << 1;

	if (thisDoBass || thisDoTreble) {

		if (thisDoBass) {
			bassFreq = getExp(120000000, (unpatchedParams->getValue(Param::Unpatched::BASS_FREQ) >> 5) * 6);
		}

		if (thisDoTreble) {
			trebleFreq = getExp(700000000, (unpatchedParams->getValue(Param::Unpatched::TREBLE_FREQ) >> 5) * 6);
		}

		StereoSample* currentSample = buffer;
		do {
			doEQ(thisDoBass, thisDoTreble, &currentSample->l, &currentSample->r, bassAmount, trebleAmount);
		} while (++currentSample != bufferEnd);
	}

	// Delay ----------------------------------------------------------------------------------
	DelayBufferSetup delayPrimarySetup;
	DelayBufferSetup delaySecondarySetup;

	if (delayWorkingState->doDelay) {

		if (delayWorkingState->userDelayRate != delay.userRateLastTime) {
			delay.userRateLastTime = delayWorkingState->userDelayRate;
			delay.countCyclesWithoutChange = 0;
		}
		else {
			delay.countCyclesWithoutChange += numSamples;
		}

		// If just a single buffer is being used for reading and writing, we can consider making a 2nd buffer
		if (!delay.secondaryBuffer.isActive()) {

			// If resampling previously recorded as happening, or just about to be recorded as happening
			if (delay.primaryBuffer.isResampling
			    || delayWorkingState->userDelayRate != delay.primaryBuffer.nativeRate) {

				// If delay speed has settled for a split second...
				if (delay.countCyclesWithoutChange >= (kSampleRate >> 5)) {
					//Debug::println("settling");
					initializeSecondaryDelayBuffer(delayWorkingState->userDelayRate, true);
				}

				// If spinning at double native rate, there's no real need to be using such a big buffer, so make a new (smaller) buffer at our new rate
				else if (delayWorkingState->userDelayRate >= (delay.primaryBuffer.nativeRate << 1)) {
					initializeSecondaryDelayBuffer(delayWorkingState->userDelayRate, false);
				}

				// If spinning below native rate, the quality's going to be suffering, so make a new buffer whose native rate is half our current rate (double the quality)
				else if (delayWorkingState->userDelayRate < delay.primaryBuffer.nativeRate) {
					initializeSecondaryDelayBuffer(delayWorkingState->userDelayRate >> 1, false);
				}
			}
		}

		// Figure some stuff out for the primary buffer
		delay.primaryBuffer.setupForRender(delayWorkingState->userDelayRate, &delayPrimarySetup);

		// Figure some stuff out for the secondary buffer - only if it's active
		if (delay.secondaryBuffer.isActive()) {
			delay.secondaryBuffer.setupForRender(delayWorkingState->userDelayRate, &delaySecondarySetup);
		}

		bool wrapped = false;

		int32_t* delayWorkingBuffer = spareRenderingBuffer[0];

		GeneralMemoryAllocator::get().checkStack("delay");

		int32_t* workingBufferEnd = delayWorkingBuffer + numSamples * 2;

		StereoSample* primaryBufferOldPos;
		uint32_t primaryBufferOldLongPos;
		uint8_t primaryBufferOldLastShortPos;

		// If nothing to read yet, easy
		if (!delay.primaryBuffer.isActive()) {
			memset(delayWorkingBuffer, 0, numSamples * 2 * sizeof(int32_t));
		}

		// Or...
		else {

			primaryBufferOldPos = delay.primaryBuffer.bufferCurrentPos;
			primaryBufferOldLongPos = delay.primaryBuffer.longPos;
			primaryBufferOldLastShortPos = delay.primaryBuffer.lastShortPos;

			// Native read
			if (!delay.primaryBuffer.isResampling) {

				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					wrapped = delay.primaryBuffer.clearAndMoveOn() || wrapped;
					workingBufferPos[0] = delay.primaryBuffer.bufferCurrentPos->l;
					workingBufferPos[1] = delay.primaryBuffer.bufferCurrentPos->r;

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			// Or, resampling read
			else {

				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					// Move forward, and clear buffer as we go
					delay.primaryBuffer.longPos += delayPrimarySetup.actualSpinRate;
					uint8_t newShortPos = delay.primaryBuffer.longPos >> 24;
					uint8_t shortPosDiff = newShortPos - delay.primaryBuffer.lastShortPos;
					delay.primaryBuffer.lastShortPos = newShortPos;

					while (shortPosDiff > 0) {
						wrapped = delay.primaryBuffer.clearAndMoveOn() || wrapped;
						shortPosDiff--;
					}

					int32_t primaryStrength2 = (delay.primaryBuffer.longPos >> 8) & 65535;
					int32_t primaryStrength1 = 65536 - primaryStrength2;

					StereoSample* nextPos = delay.primaryBuffer.bufferCurrentPos + 1;
					if (nextPos == delay.primaryBuffer.bufferEnd) {
						nextPos = delay.primaryBuffer.bufferStart;
					}
					int32_t fromDelay1L = delay.primaryBuffer.bufferCurrentPos->l;
					int32_t fromDelay1R = delay.primaryBuffer.bufferCurrentPos->r;
					int32_t fromDelay2L = nextPos->l;
					int32_t fromDelay2R = nextPos->r;

					workingBufferPos[0] = (multiply_32x32_rshift32(fromDelay1L, primaryStrength1 << 14)
					                       + multiply_32x32_rshift32(fromDelay2L, primaryStrength2 << 14))
					                      << 2;
					workingBufferPos[1] = (multiply_32x32_rshift32(fromDelay1R, primaryStrength1 << 14)
					                       + multiply_32x32_rshift32(fromDelay2R, primaryStrength2 << 14))
					                      << 2;

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}
		}

		if (delay.analog) {

			{
				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					delay.impulseResponseProcessor.process(workingBufferPos[0], workingBufferPos[1],
					                                       &workingBufferPos[0], &workingBufferPos[1]);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			{
				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					int32_t fromDelayL = workingBufferPos[0];
					int32_t fromDelayR = workingBufferPos[1];

					//delay.impulseResponseProcessor.process(fromDelayL, fromDelayR, &fromDelayL, &fromDelayR);

					// Reduce headroom, since this sounds ok with analog sim
					workingBufferPos[0] =
					    getTanHUnknown(multiply_32x32_rshift32(fromDelayL, delayWorkingState->delayFeedbackAmount),
					                   analogDelaySaturationAmount)
					    << 2;
					workingBufferPos[1] =
					    getTanHUnknown(multiply_32x32_rshift32(fromDelayR, delayWorkingState->delayFeedbackAmount),
					                   analogDelaySaturationAmount)
					    << 2;

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}
		}

		else {
			int32_t* workingBufferPos = delayWorkingBuffer;
			do {
				// Leave more headroom, because making it clip sounds bad with pure digital
				workingBufferPos[0] = signed_saturate<32 - 3>(multiply_32x32_rshift32(
				                          workingBufferPos[0], delayWorkingState->delayFeedbackAmount))
				                      << 2;
				workingBufferPos[1] = signed_saturate<32 - 3>(multiply_32x32_rshift32(
				                          workingBufferPos[1], delayWorkingState->delayFeedbackAmount))
				                      << 2;

				workingBufferPos += 2;
			} while (workingBufferPos != workingBufferEnd);
		}

		// HPF on delay output, to stop it "farting out". Corner frequency is somewhere around 40Hz after many repetitions
		{
			int32_t* workingBufferPos = delayWorkingBuffer;
			do {
				int32_t distanceToGoL = workingBufferPos[0] - delay.postLPFL;
				delay.postLPFL += distanceToGoL >> 11;
				workingBufferPos[0] -= delay.postLPFL;

				int32_t distanceToGoR = workingBufferPos[1] - delay.postLPFR;
				delay.postLPFR += distanceToGoR >> 11;
				workingBufferPos[1] -= delay.postLPFR;

				workingBufferPos += 2;
			} while (workingBufferPos != workingBufferEnd);
		}

		{
			StereoSample* currentSample = buffer;
			int32_t* workingBufferPos = delayWorkingBuffer;
			// Go through what we grabbed, sending it to the audio output buffer, and also preparing it to be fed back into the delay
			do {

				int32_t fromDelayL = workingBufferPos[0];
				int32_t fromDelayR = workingBufferPos[1];

				/*
				if (delay.analog) {
					// Reduce headroom, since this sounds ok with analog sim
					fromDelayL = getTanH(multiply_32x32_rshift32(fromDelayL, delayWorkingState->delayFeedbackAmount), 8) << 2;
					fromDelayR = getTanH(multiply_32x32_rshift32(fromDelayR, delayWorkingState->delayFeedbackAmount), 8) << 2;
				}

				else {
					fromDelayL = signed_saturate<delayWorkingState->delayFeedbackAmount>(multiply_32x32_rshift32(fromDelayL), 32 - 3) << 2;
					fromDelayR = signed_saturate<delayWorkingState->delayFeedbackAmount>(multiply_32x32_rshift32(fromDelayR), 32 - 3) << 2;
				}
				*/

				/*
				// HPF on delay output, to stop it "farting out". Corner frequency is somewhere around 40Hz after many repetitions
				int32_t distanceToGoL = fromDelayL - delay.postLPFL;
				delay.postLPFL += distanceToGoL >> 11;
				fromDelayL -= delay.postLPFL;

				int32_t distanceToGoR = fromDelayR - delay.postLPFR;
				delay.postLPFR += distanceToGoR >> 11;
				fromDelayR -= delay.postLPFR;
				*/

				// Feedback calculation, and combination with input
				if (delay.pingPong && AudioEngine::renderInStereo) {
					workingBufferPos[0] = fromDelayR;
					workingBufferPos[1] = ((currentSample->l + currentSample->r) >> 1) + fromDelayL;
				}
				else {
					workingBufferPos[0] = currentSample->l + fromDelayL;
					workingBufferPos[1] = currentSample->r + fromDelayR;
				}

				// Output
				currentSample->l += fromDelayL;
				currentSample->r += fromDelayR;

				currentSample++;
				workingBufferPos += 2;
			} while (workingBufferPos != workingBufferEnd);
		}

		// And actually feedback being applied back into the actual delay primary buffer...
		if (delay.primaryBuffer.isActive()) {

			// Native
			if (!delay.primaryBuffer.isResampling) {
				int32_t* workingBufferPos = delayWorkingBuffer;

				StereoSample* writePos = primaryBufferOldPos - delaySpaceBetweenReadAndWrite;
				if (writePos < delay.primaryBuffer.bufferStart) {
					writePos += delay.primaryBuffer.sizeIncludingExtra;
				}

				do {
					delay.primaryBuffer.writeNativeAndMoveOn(workingBufferPos[0], workingBufferPos[1], &writePos);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			// Resampling
			else {
				delay.primaryBuffer.bufferCurrentPos = primaryBufferOldPos;
				delay.primaryBuffer.longPos = primaryBufferOldLongPos;
				delay.primaryBuffer.lastShortPos = primaryBufferOldLastShortPos;

				int32_t* workingBufferPos = delayWorkingBuffer;

				do {

					// Move forward, and clear buffer as we go
					delay.primaryBuffer.longPos += delayPrimarySetup.actualSpinRate;
					uint8_t newShortPos = delay.primaryBuffer.longPos >> 24;
					uint8_t shortPosDiff = newShortPos - delay.primaryBuffer.lastShortPos;
					delay.primaryBuffer.lastShortPos = newShortPos;

					while (shortPosDiff > 0) {
						delay.primaryBuffer.moveOn();
						shortPosDiff--;
					}

					int32_t primaryStrength2 = (delay.primaryBuffer.longPos >> 8) & 65535;
					int32_t primaryStrength1 = 65536 - primaryStrength2;

					delay.primaryBuffer.writeResampled(workingBufferPos[0], workingBufferPos[1], primaryStrength1,
					                                   primaryStrength2, &delayPrimarySetup);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}
		}

		// And secondary buffer
		// If secondary buffer active, we need to tick it along and write to it too
		if (delay.secondaryBuffer.isActive()) {

			wrapped =
			    false; // We want to disregard whatever the primary buffer told us, and just use the secondary one now

			// Native
			if (!delay.secondaryBuffer.isResampling) {

				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					wrapped = delay.secondaryBuffer.clearAndMoveOn() || wrapped;
					delay.sizeLeftUntilBufferSwap--;

					// Write to secondary buffer
					delay.secondaryBuffer.writeNative(workingBufferPos[0], workingBufferPos[1]);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			// Resampled
			else {

				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					// Move forward, and clear buffer as we go
					delay.secondaryBuffer.longPos += delaySecondarySetup.actualSpinRate;
					uint8_t newShortPos = delay.secondaryBuffer.longPos >> 24;
					uint8_t shortPosDiff = newShortPos - delay.secondaryBuffer.lastShortPos;
					delay.secondaryBuffer.lastShortPos = newShortPos;

					while (shortPosDiff > 0) {
						wrapped = delay.secondaryBuffer.clearAndMoveOn() || wrapped;
						delay.sizeLeftUntilBufferSwap--;
						shortPosDiff--;
					}

					int32_t secondaryStrength2 = (delay.secondaryBuffer.longPos >> 8) & 65535;
					int32_t secondaryStrength1 = 65536 - secondaryStrength2;

					// Write to secondary buffer
					delay.secondaryBuffer.writeResampled(workingBufferPos[0], workingBufferPos[1], secondaryStrength1,
					                                     secondaryStrength2, &delaySecondarySetup);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			if (delay.sizeLeftUntilBufferSwap < 0) {
				delay.copySecondaryToPrimary();
			}
		}

		if (wrapped) {
			delay.hasWrapped();
		}
	}
}

void ModControllableAudio::processReverbSendAndVolume(StereoSample* buffer, int32_t numSamples, int32_t* reverbBuffer,
                                                      int32_t postFXVolume, int32_t postReverbVolume,
                                                      int32_t reverbSendAmount, int32_t pan, bool doAmplitudeIncrement,
                                                      int32_t amplitudeIncrement) {

	StereoSample* bufferEnd = buffer + numSamples;

	int32_t reverbSendAmountAndPostFXVolume = multiply_32x32_rshift32(postFXVolume, reverbSendAmount) << 5;

	int32_t postFXAndReverbVolumeL, postFXAndReverbVolumeR, amplitudeIncrementL, amplitudeIncrementR;
	postFXAndReverbVolumeL = postFXAndReverbVolumeR = (multiply_32x32_rshift32(postReverbVolume, postFXVolume) << 5);

	// The amplitude increment applies to the post-FX volume. We want to have it just so that we can respond better to sidechain volume ducking, which is done through post-FX volume.
	if (doAmplitudeIncrement) {
		amplitudeIncrementL = amplitudeIncrementR = (multiply_32x32_rshift32(postFXVolume, amplitudeIncrement) << 5);
	}

	if (pan != 0 && AudioEngine::renderInStereo) {
		// Set up panning
		int32_t amplitudeL;
		int32_t amplitudeR;
		shouldDoPanning(pan, &amplitudeL, &amplitudeR);

		postFXAndReverbVolumeL = multiply_32x32_rshift32(postFXAndReverbVolumeL, amplitudeL) << 2;
		postFXAndReverbVolumeR = multiply_32x32_rshift32(postFXAndReverbVolumeR, amplitudeR) << 2;

		amplitudeIncrementL = multiply_32x32_rshift32(amplitudeIncrementL, amplitudeL) << 2;
		amplitudeIncrementR = multiply_32x32_rshift32(amplitudeIncrementR, amplitudeR) << 2;
	}

	StereoSample* inputSample = buffer;

	do {
		StereoSample processingSample = *inputSample;

		// Send to reverb
		if (reverbSendAmount != 0) {
			*(reverbBuffer++) +=
			    multiply_32x32_rshift32(processingSample.l + processingSample.r, reverbSendAmountAndPostFXVolume) << 1;
		}

		if (doAmplitudeIncrement) {
			postFXAndReverbVolumeL += amplitudeIncrementL;
			postFXAndReverbVolumeR += amplitudeIncrementR;
		}

		// Apply post-fx and post-reverb-send volume
		inputSample->l = multiply_32x32_rshift32(processingSample.l, postFXAndReverbVolumeL) << 5;
		inputSample->r = multiply_32x32_rshift32(processingSample.r, postFXAndReverbVolumeR) << 5;

	} while (++inputSample != bufferEnd);

	// We've generated some sound. If reverb is happening, make note
	if (reverbSendAmount != 0) {
		AudioEngine::timeThereWasLastSomeReverb = AudioEngine::audioSampleTimer;
	}
}

bool ModControllableAudio::isBitcrushingEnabled(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(Param::Unpatched::BITCRUSHING) >= -2113929216);
}

bool ModControllableAudio::isSRREnabled(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(Param::Unpatched::SAMPLE_RATE_REDUCTION) != -2147483648);
}

void ModControllableAudio::processSRRAndBitcrushing(StereoSample* buffer, int32_t numSamples, int32_t* postFXVolume,
                                                    ParamManager* paramManager) {
	StereoSample const* const bufferEnd = buffer + numSamples;

	uint32_t bitCrushMaskForSRR = 0xFFFFFFFF;

	bool srrEnabled = isSRREnabled(paramManager);

	// Bitcrushing ------------------------------------------------------------------------------
	if (isBitcrushingEnabled(paramManager)) {
		uint32_t positivePreset =
		    (paramManager->getUnpatchedParamSet()->getValue(Param::Unpatched::BITCRUSHING) + 2147483648) >> 29;
		if (positivePreset > 4) {
			*postFXVolume >>= (positivePreset - 4);
		}

		// If not also doing SRR
		if (!srrEnabled) {
			uint32_t mask = 0xFFFFFFFF << (19 + (positivePreset));
			StereoSample* __restrict__ currentSample = buffer;
			do {
				currentSample->l &= mask;
				currentSample->r &= mask;
			} while (++currentSample != bufferEnd);
		}

		else {
			bitCrushMaskForSRR = 0xFFFFFFFF << (18 + (positivePreset));
		}
	}

	// Sample rate reduction -------------------------------------------------------------------------------------
	if (srrEnabled) {

		// Set up for first time
		if (!sampleRateReductionOnLastTime) {
			sampleRateReductionOnLastTime = true;
			lastSample.l = lastSample.r = 0;
			grabbedSample.l = grabbedSample.r = 0;
			lowSampleRatePos = 0;
		}

		// This function, slightly unusually, uses 22 bits to represent "1". That's 4194304. I tried using 24, but stuff started clipping off where I needed it if sample rate too low

		uint32_t positivePreset =
		    paramManager->getUnpatchedParamSet()->getValue(Param::Unpatched::SAMPLE_RATE_REDUCTION) + 2147483648;
		int32_t lowSampleRateIncrement = getExp(4194304, (positivePreset >> 3));
		int32_t highSampleRateIncrement = ((uint32_t)0xFFFFFFFF / (lowSampleRateIncrement >> 6)) << 6;
		//int32_t highSampleRateIncrement = getExp(4194304, -(int32_t)(positivePreset >> 3)); // This would work too

		StereoSample* currentSample = buffer;
		do {

			// Convert down.
			// If time to "grab" another sample for down-conversion...
			if (lowSampleRatePos < 4194304) {
				int32_t strength2 = lowSampleRatePos;
				int32_t strength1 = 4194303 - strength2;

				lastGrabbedSample = grabbedSample; // What was current is now last
				grabbedSample.l = multiply_32x32_rshift32_rounded(lastSample.l, strength1 << 9)
				                  + multiply_32x32_rshift32_rounded(currentSample->l, strength2 << 9);
				grabbedSample.r = multiply_32x32_rshift32_rounded(lastSample.r, strength1 << 9)
				                  + multiply_32x32_rshift32_rounded(currentSample->r, strength2 << 9);
				grabbedSample.l &= bitCrushMaskForSRR;
				grabbedSample.r &= bitCrushMaskForSRR;

				// Set the "time" at which we want to "grab" our next sample for down-conversion.
				lowSampleRatePos += lowSampleRateIncrement;

				// "Re-sync" the up-conversion spinner.
				// I previously had it using strength2 instead of "lowSampleRatePos & 16777215", but that just works better. Ah, writing the massive explanation would take ages.
				highSampleRatePos =
				    multiply_32x32_rshift32_rounded(lowSampleRatePos & 4194303, highSampleRateIncrement << 8) << 2;
			}
			lowSampleRatePos -= 4194304; // We're one step closer to grabbing our next sample for down-conversion
			lastSample = *currentSample;

			// Convert up
			int32_t strength2 =
			    std::min(highSampleRatePos,
			             (uint32_t)4194303); // Would only overshoot if we raised the sample rate during playback
			int32_t strength1 = 4194303 - strength2;
			currentSample->l = (multiply_32x32_rshift32_rounded(lastGrabbedSample.l, strength1 << 9)
			                    + multiply_32x32_rshift32_rounded(grabbedSample.l, strength2 << 9))
			                   << 2;
			currentSample->r = (multiply_32x32_rshift32_rounded(lastGrabbedSample.r, strength1 << 9)
			                    + multiply_32x32_rshift32_rounded(grabbedSample.r, strength2 << 9))
			                   << 2;

			highSampleRatePos += highSampleRateIncrement;
		} while (++currentSample != bufferEnd);
	}
	else {
		sampleRateReductionOnLastTime = false;
	}
}

void ModControllableAudio::processStutter(StereoSample* buffer, int32_t numSamples, ParamManager* paramManager) {
	if (stutterer.status == STUTTERER_STATUS_OFF) {
		return;
	}

	StereoSample* bufferEnd = buffer + numSamples;

	StereoSample* thisSample = buffer;

	DelayBufferSetup delayBufferSetup;

	int32_t rate = getStutterRate(paramManager);

	stutterer.buffer.setupForRender(rate, &delayBufferSetup);

	if (stutterer.status == STUTTERER_STATUS_RECORDING) {

		do {

			int32_t strength1;
			int32_t strength2;

			// First, tick it along, as if we were reading from it

			// Non-resampling tick-along
			if (!stutterer.buffer.isResampling) {
				stutterer.buffer.clearAndMoveOn();
				stutterer.sizeLeftUntilRecordFinished--;

				//stutterer.buffer.writeNative(thisSample->l, thisSample->r);
			}

			// Or, resampling tick-along
			else {

				// Move forward, and clear buffer as we go
				stutterer.buffer.longPos += delayBufferSetup.actualSpinRate;
				uint8_t newShortPos = stutterer.buffer.longPos >> 24;
				uint8_t shortPosDiff = newShortPos - stutterer.buffer.lastShortPos;
				stutterer.buffer.lastShortPos = newShortPos;

				while (shortPosDiff > 0) {
					stutterer.buffer.clearAndMoveOn();
					stutterer.sizeLeftUntilRecordFinished--;
					shortPosDiff--;
				}

				strength2 = (stutterer.buffer.longPos >> 8) & 65535;
				strength1 = 65536 - strength2;

				//stutterer.buffer.writeResampled(thisSample->l, thisSample->r, strength1, strength2, &delayBufferSetup);
			}

			stutterer.buffer.write(thisSample->l, thisSample->r, strength1, strength2, &delayBufferSetup);

		} while (++thisSample != bufferEnd);

		// If we've finished recording, remember to play next time instead
		if (stutterer.sizeLeftUntilRecordFinished < 0) {
			stutterer.status = STUTTERER_STATUS_PLAYING;
		}
	}

	else { // PLAYING

		do {
			int32_t strength1;
			int32_t strength2;

			// Non-resampling read
			if (!stutterer.buffer.isResampling) {
				stutterer.buffer.moveOn();
				thisSample->l = stutterer.buffer.bufferCurrentPos->l;
				thisSample->r = stutterer.buffer.bufferCurrentPos->r;
			}

			// Or, resampling read
			else {

				// Move forward, and clear buffer as we go
				stutterer.buffer.longPos += delayBufferSetup.actualSpinRate;
				uint8_t newShortPos = stutterer.buffer.longPos >> 24;
				uint8_t shortPosDiff = newShortPos - stutterer.buffer.lastShortPos;
				stutterer.buffer.lastShortPos = newShortPos;

				while (shortPosDiff > 0) {
					stutterer.buffer.moveOn();
					shortPosDiff--;
				}

				strength2 = (stutterer.buffer.longPos >> 8) & 65535;
				strength1 = 65536 - strength2;

				StereoSample* nextPos = stutterer.buffer.bufferCurrentPos + 1;
				if (nextPos == stutterer.buffer.bufferEnd) {
					nextPos = stutterer.buffer.bufferStart;
				}
				int32_t fromDelay1L = stutterer.buffer.bufferCurrentPos->l;
				int32_t fromDelay1R = stutterer.buffer.bufferCurrentPos->r;
				int32_t fromDelay2L = nextPos->l;
				int32_t fromDelay2R = nextPos->r;

				thisSample->l = (multiply_32x32_rshift32(fromDelay1L, strength1 << 14)
				                 + multiply_32x32_rshift32(fromDelay2L, strength2 << 14))
				                << 2;
				thisSample->r = (multiply_32x32_rshift32(fromDelay1R, strength1 << 14)
				                 + multiply_32x32_rshift32(fromDelay2R, strength2 << 14))
				                << 2;
			}
		} while (++thisSample != bufferEnd);
	}
}

int32_t ModControllableAudio::getStutterRate(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	int32_t paramValue = unpatchedParams->getValue(Param::Unpatched::STUTTER_RATE);

	// Quantized Stutter diff
	// Convert to knobPos (range -64 to 64) for easy operation
	int32_t knobPos = unpatchedParams->paramValueToKnobPos(paramValue, NULL);
	// Add diff "lastQuantizedKnobDiff" (this value will be set if Quantized Stutter is On, zero if not so this will be a no-op)
	knobPos = knobPos + stutterer.lastQuantizedKnobDiff;
	// Convert back to value range
	paramValue = unpatchedParams->knobPosToParamValue(knobPos, NULL);

	int32_t rate =
	    getFinalParameterValueExp(paramNeutralValues[Param::Global::DELAY_RATE], cableToExpParamShortcut(paramValue));

	if (stutterer.sync != 0) {
		rate = multiply_32x32_rshift32(rate, playbackHandler.getTimePerInternalTickInverse());

		// Limit to the biggest number we can store...
		int32_t lShiftAmount =
		    stutterer.sync + 6
		    - (currentSong->insideWorldTickMagnitude + currentSong->insideWorldTickMagnitudeOffsetFromBPM);
		int32_t limit = 2147483647 >> lShiftAmount;
		rate = std::min(rate, limit);
		rate <<= lShiftAmount;
	}
	return rate;
}

void ModControllableAudio::initializeSecondaryDelayBuffer(int32_t newNativeRate,
                                                          bool makeNativeRatePreciseRelativeToOtherBuffer) {
	uint8_t result = delay.secondaryBuffer.init(newNativeRate, delay.primaryBuffer.size);
	if (result == NO_ERROR) {
		//Debug::print("new buffer, size: ");
		//Debug::println(delay.secondaryBuffer.size);

		// 2 different options here for different scenarios. I can't very clearly remember how to describe the difference
		if (makeNativeRatePreciseRelativeToOtherBuffer) {
			//Debug::println("making precise");
			delay.primaryBuffer.makeNativeRatePreciseRelativeToOtherBuffer(&delay.secondaryBuffer);
		}
		else {
			delay.primaryBuffer.makeNativeRatePrecise();
			delay.secondaryBuffer.makeNativeRatePrecise();
		}
		delay.sizeLeftUntilBufferSwap = delay.secondaryBuffer.size + 5;
	}
}

inline void ModControllableAudio::doEQ(bool doBass, bool doTreble, int32_t* inputL, int32_t* inputR, int32_t bassAmount,
                                       int32_t trebleAmount) {
	int32_t trebleOnlyL;
	int32_t trebleOnlyR;

	if (doTreble) {
		int32_t distanceToGoL = *inputL - withoutTrebleL;
		int32_t distanceToGoR = *inputR - withoutTrebleR;
		withoutTrebleL += multiply_32x32_rshift32(distanceToGoL, trebleFreq) << 1;
		withoutTrebleR += multiply_32x32_rshift32(distanceToGoR, trebleFreq) << 1;
		trebleOnlyL = *inputL - withoutTrebleL;
		trebleOnlyR = *inputR - withoutTrebleR;
		*inputL = withoutTrebleL; // Input now has had the treble removed. Or is this bad?
		*inputR = withoutTrebleR; // Input now has had the treble removed. Or is this bad?
	}

	if (doBass) {
		int32_t distanceToGoL = *inputL - bassOnlyL;
		int32_t distanceToGoR = *inputR - bassOnlyR;
		bassOnlyL += multiply_32x32_rshift32(distanceToGoL, bassFreq); // 33554432
		bassOnlyR += multiply_32x32_rshift32(distanceToGoR, bassFreq); // 33554432
	}

	if (doTreble) {
		*inputL += (multiply_32x32_rshift32(trebleOnlyL, trebleAmount) << 3);
		*inputR += (multiply_32x32_rshift32(trebleOnlyR, trebleAmount) << 3);
	}
	if (doBass) {
		*inputL += (multiply_32x32_rshift32(bassOnlyL, bassAmount) << 3);
		*inputR += (multiply_32x32_rshift32(bassOnlyR, bassAmount) << 3);
	}
}

void ModControllableAudio::writeAttributesToFile() {
	storageManager.writeAttribute("lpfMode", (char*)lpfTypeToString(lpfMode));
	storageManager.writeAttribute("hpfMode", (char*)lpfTypeToString(hpfMode));
	storageManager.writeAttribute("modFXType", (char*)fxTypeToString(modFXType));
	storageManager.writeAttribute("filterRoute", (char*)filterRouteToString(filterRoute));
	if (clippingAmount) {
		storageManager.writeAttribute("clippingAmount", clippingAmount);
	}
}

void ModControllableAudio::writeTagsToFile() {
	// Delay
	storageManager.writeOpeningTagBeginning("delay");
	storageManager.writeAttribute("pingPong", delay.pingPong);
	storageManager.writeAttribute("analog", delay.analog);
	storageManager.writeSyncTypeToFile(currentSong, "syncType", delay.syncType);
	storageManager.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", delay.syncLevel);
	storageManager.closeTag();

	// Sidechain compressor
	storageManager.writeOpeningTagBeginning("compressor");
	storageManager.writeSyncTypeToFile(currentSong, "syncType", compressor.syncType);
	storageManager.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", compressor.syncLevel);
	storageManager.writeAttribute("attack", compressor.attack);
	storageManager.writeAttribute("release", compressor.release);
	storageManager.closeTag();

	// MIDI knobs
	if (midiKnobArray.getNumElements()) {
		storageManager.writeOpeningTag("midiKnobs");
		for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
			MIDIKnob* knob = midiKnobArray.getElement(k);
			storageManager.writeOpeningTagBeginning("midiKnob");
			knob->midiInput.writeAttributesToFile(
			    MIDI_MESSAGE_CC); // Writes channel and CC, but not device - we do that below.
			storageManager.writeAttribute("relative", knob->relative);
			storageManager.writeAttribute("controlsParam", paramToString(knob->paramDescriptor.getJustTheParam()));
			if (!knob->paramDescriptor.isJustAParam()) { // TODO: this only applies to Sounds
				storageManager.writeAttribute("patchAmountFromSource",
				                              sourceToString(knob->paramDescriptor.getTopLevelSource()));

				if (knob->paramDescriptor.hasSecondSource()) {
					storageManager.writeAttribute("patchAmountFromSecondSource",
					                              sourceToString(knob->paramDescriptor.getSecondSourceFromTop()));
				}
			}

			// Because we manually called LearnedMIDI::writeAttributesToFile() above, we have to give the MIDIDevice its own tag, cos that can't be written as just an attribute.
			if (knob->midiInput.device) {
				storageManager.writeOpeningTagEnd();
				knob->midiInput.device->writeReferenceToFile();
				storageManager.writeClosingTag("midiKnob");
			}
			else {
				storageManager.closeTag();
			}
		}
		storageManager.writeClosingTag("midiKnobs");
	}
}

void ModControllableAudio::writeParamAttributesToFile(ParamManager* paramManager, bool writeAutomation,
                                                      int32_t* valuesForOverride) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute("stutterRate", Param::Unpatched::STUTTER_RATE, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("sampleRateReduction", Param::Unpatched::SAMPLE_RATE_REDUCTION,
	                                       writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("bitCrush", Param::Unpatched::BITCRUSHING, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("modFXOffset", Param::Unpatched::MOD_FX_OFFSET, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("modFXFeedback", Param::Unpatched::MOD_FX_FEEDBACK, writeAutomation, false,
	                                       valuesForOverride);
}

void ModControllableAudio::writeParamTagsToFile(ParamManager* paramManager, bool writeAutomation,
                                                int32_t* valuesForOverride) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	storageManager.writeOpeningTagBeginning("equalizer");
	unpatchedParams->writeParamAsAttribute("bass", Param::Unpatched::BASS, writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("treble", Param::Unpatched::TREBLE, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("bassFrequency", Param::Unpatched::BASS_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("trebleFrequency", Param::Unpatched::TREBLE_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	storageManager.closeTag();
}

bool ModControllableAudio::readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                                int32_t readAutomationUpToPos) {
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "equalizer")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "bass")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::BASS, readAutomationUpToPos);
				storageManager.exitTag("bass");
			}
			else if (!strcmp(tagName, "treble")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::TREBLE, readAutomationUpToPos);
				storageManager.exitTag("treble");
			}
			else if (!strcmp(tagName, "bassFrequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::BASS_FREQ, readAutomationUpToPos);
				storageManager.exitTag("bassFrequency");
			}
			else if (!strcmp(tagName, "trebleFrequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::TREBLE_FREQ,
				                           readAutomationUpToPos);
				storageManager.exitTag("trebleFrequency");
			}
		}
		storageManager.exitTag("equalizer");
	}

	else if (!strcmp(tagName, "stutterRate")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::STUTTER_RATE, readAutomationUpToPos);
		storageManager.exitTag("stutterRate");
	}

	else if (!strcmp(tagName, "sampleRateReduction")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::SAMPLE_RATE_REDUCTION,
		                           readAutomationUpToPos);
		storageManager.exitTag("sampleRateReduction");
	}

	else if (!strcmp(tagName, "bitCrush")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::BITCRUSHING, readAutomationUpToPos);
		storageManager.exitTag("bitCrush");
	}

	else if (!strcmp(tagName, "modFXOffset")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::MOD_FX_OFFSET, readAutomationUpToPos);
		storageManager.exitTag("modFXOffset");
	}

	else if (!strcmp(tagName, "modFXFeedback")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::MOD_FX_FEEDBACK, readAutomationUpToPos);
		storageManager.exitTag("modFXFeedback");
	}

	else {
		return false;
	}

	return true;
}

// paramManager is optional
int32_t ModControllableAudio::readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                              int32_t readAutomationUpToPos, Song* song) {

	int32_t p;

	if (!strcmp(tagName, "lpfMode")) {
		lpfMode = stringToLPFType(storageManager.readTagOrAttributeValue());
		storageManager.exitTag("lpfMode");
	}
	else if (!strcmp(tagName, "hpfMode")) {
		hpfMode = stringToLPFType(storageManager.readTagOrAttributeValue());
		storageManager.exitTag("hpfMode");
	}
	else if (!strcmp(tagName, "filterRoute")) {
		filterRoute = stringToFilterRoute(storageManager.readTagOrAttributeValue());
		storageManager.exitTag("filterRoute");
	}

	else if (!strcmp(tagName, "clippingAmount")) {
		clippingAmount = storageManager.readTagOrAttributeValueInt();
		storageManager.exitTag("clippingAmount");
	}

	else if (!strcmp(tagName, "delay")) {
		// Set default values in case they are not configured
		delay.syncType = SYNC_TYPE_EVEN;
		delay.syncLevel = SYNC_LEVEL_NONE;

		while (*(tagName = storageManager.readNextTagOrAttributeName())) {

			// These first two ensure compatibility with old files (pre late 2016 I think?)
			if (!strcmp(tagName, "feedback")) {
				p = Param::Global::DELAY_FEEDBACK;
doReadPatchedParam:
				if (paramManager) {
					if (!paramManager->containsAnyMainParamCollections()) {
						int32_t error = Sound::createParamManagerForLoading(paramManager);
						if (error) {
							return error;
						}
					}
					ParamCollectionSummary* patchedParamsSummary = paramManager->getPatchedParamSetSummary();
					PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;
					patchedParams->readParam(patchedParamsSummary, Param::Global::DELAY_FEEDBACK,
					                         readAutomationUpToPos);
				}
				storageManager.exitTag();
			}
			else if (!strcmp(tagName, "rate")) {
				p = Param::Global::DELAY_RATE;
				goto doReadPatchedParam;
			}

			else if (!strcmp(tagName, "pingPong")) {
				int32_t contents = storageManager.readTagOrAttributeValueInt();
				delay.pingPong = std::max((int32_t)0, std::min((int32_t)1, contents));
				storageManager.exitTag("pingPong");
			}
			else if (!strcmp(tagName, "analog")) {
				int32_t contents = storageManager.readTagOrAttributeValueInt();
				delay.analog = std::max((int32_t)0, std::min((int32_t)1, contents));
				storageManager.exitTag("analog");
			}
			else if (!strcmp(tagName, "syncType")) {
				delay.syncType = storageManager.readSyncTypeFromFile(song);
				storageManager.exitTag("syncType");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				delay.syncLevel = storageManager.readAbsoluteSyncLevelFromFile(song);
				storageManager.exitTag("syncLevel");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("delay");
	}

	else if (!strcmp(tagName, "compressor")) { // Remember, Song doesn't use this
		// Set default values in case they are not configured
		compressor.syncType = SYNC_TYPE_EVEN;
		compressor.syncLevel = SYNC_LEVEL_NONE;

		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				compressor.attack = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("attack");
			}
			else if (!strcmp(tagName, "release")) {
				compressor.release = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("release");
			}
			else if (!strcmp(tagName, "syncType")) {
				compressor.syncType = storageManager.readSyncTypeFromFile(song);
				storageManager.exitTag("syncType");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				compressor.syncLevel = storageManager.readAbsoluteSyncLevelFromFile(song);
				storageManager.exitTag("syncLevel");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("compressor");
	}

	else if (!strcmp(tagName, "midiKnobs")) {

		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "midiKnob")) {

				MIDIDevice* device = NULL;
				uint8_t channel;
				uint8_t ccNumber;
				bool relative;
				uint8_t p = Param::Global::NONE;
				PatchSource s = PatchSource::NOT_AVAILABLE;
				PatchSource s2 = PatchSource::NOT_AVAILABLE;

				while (*(tagName = storageManager.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "device")) {
						device = MIDIDeviceManager::readDeviceReferenceFromFile();
					}
					else if (!strcmp(tagName, "channel")) {
						channel = storageManager.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "ccNumber")) {
						ccNumber = storageManager.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "relative")) {
						relative = storageManager.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "controlsParam")) {
						p = stringToParam(storageManager.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSource")) {
						s = stringToSource(storageManager.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSecondSource")) {
						s2 = stringToSource(storageManager.readTagOrAttributeValue());
					}
					storageManager.exitTag();
				}

				if (p != Param::Global::NONE && p != Param::PLACEHOLDER_RANGE) {
					MIDIKnob* newKnob = midiKnobArray.insertKnobAtEnd();
					if (newKnob) {
						newKnob->midiInput.device = device;
						newKnob->midiInput.channelOrZone = channel;
						newKnob->midiInput.noteOrCC = ccNumber;
						newKnob->relative = relative;

						if (s == PatchSource::NOT_AVAILABLE) {
							newKnob->paramDescriptor.setToHaveParamOnly(p);
						}
						else if (s2 == PatchSource::NOT_AVAILABLE) {
							newKnob->paramDescriptor.setToHaveParamAndSource(p, s);
						}
						else {
							newKnob->paramDescriptor.setToHaveParamAndTwoSources(p, s, s2);
						}
					}
				}
			}
			storageManager.exitTag();
		}
		storageManager.exitTag("midiKnobs");
	}

	else {
		return RESULT_TAG_UNUSED;
	}

	return NO_ERROR;
}

ModelStackWithAutoParam* ModControllableAudio::getParamFromMIDIKnob(MIDIKnob* knob,
                                                                    ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;

	int32_t paramId = knob->paramDescriptor.getJustTheParam() - Param::Unpatched::START;

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(paramCollection, summary, paramId);

	ModelStackWithAutoParam* modelStackWithAutoParam = paramCollection->getAutoParamFromId(modelStackWithParamId);

	return modelStackWithAutoParam;
}

ModelStackWithThreeMainThings* ModControllableAudio::addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack,
                                                                             int32_t noteRowIndex) {
	NoteRow* noteRow = NULL;
	int32_t noteRowId = 0;
	ParamManager* paramManager;

	if (noteRowIndex != -1) {
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();
#if ALPHA_OR_BETA_VERSION
		if (noteRowIndex >= clip->noteRows.getNumElements()) {
			FREEZE_WITH_ERROR("E406");
		}
#endif
		noteRow = clip->noteRows.getElement(noteRowIndex);
		noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
		paramManager = &noteRow->paramManager;
	}
	else {
		if (modelStack->timelineCounterIsSet()) {
			paramManager = &modelStack->getTimelineCounter()->paramManager;
		}
		else {
			paramManager = modelStack->song->getBackedUpParamManagerPreferablyWithClip(
			    this,
			    NULL); // Could be NULL if a NonAudioInstrument - those don't back up any paramManagers (when they even have them).
		}
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addNoteRow(noteRowId, noteRow)->addOtherTwoThings(this, paramManager);
	return modelStackWithThreeMainThings;
}

bool ModControllableAudio::offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber,
                                                          uint8_t value, ModelStackWithTimelineCounter* modelStack,
                                                          int32_t noteRowIndex) {
	bool messageUsed = false;

	//if you're currently in midiSessionView, see if you can learn this CC
	if (getRootUI() == &midiSessionView) {
		midiSessionView.learnCC(channel, ccNumber);
		messageUsed = true;
	}
	else {
		//if midi follow mode is enabled and current channel is the midi follow channel for params
		//allow CC's learned in midi session/learning view to control parameters
		if ((midiEngine.midiFollow) && (channel == midiEngine.midiFollowChannelParam)) {
			offerReceivedCCToMidiFollow(modelStack, channel, ccNumber, value, midiEngine.midiFollowDisplayParam);
		}
		// For each MIDI knob...
		for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
			MIDIKnob* knob = midiKnobArray.getElement(k);

			// If this is the knob...
			if (knob->midiInput.equalsNoteOrCC(fromDevice, channel, ccNumber)) {

				messageUsed = true;

				// See if this message is evidence that the knob is not "relative"
				if (value >= 16 && value < 112) {
					knob->relative = false;
				}
				// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a region.
				int32_t modPos = 0;
				int32_t modLength = 0;

				if (modelStack->timelineCounterIsSet()) {
					if (view.modLength
					    && modelStack->getTimelineCounter()
					           == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
						modPos = view.modPos;
						modLength = view.modLength;
					}

					modelStack->getTimelineCounter()->possiblyCloneForArrangementRecording(modelStack);
				}

				// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from scratch now from that.
				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

				ModelStackWithAutoParam* modelStackWithParam =
				    getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

				if (modelStackWithParam && modelStackWithParam->autoParam) {
					int32_t newKnobPos;

					if (knob->relative) {
						int32_t offset = value;
						if (offset >= 64) {
							offset -= 128;
						}

						int32_t previousValue =
						    modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
						int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(
						    previousValue, modelStackWithParam);
						int32_t lowerLimit = std::min(-64_i32, knobPos);
						newKnobPos = knobPos + offset;
						newKnobPos = std::max(newKnobPos, lowerLimit);
						newKnobPos = std::min(newKnobPos, 64_i32);
						if (newKnobPos == knobPos) {
							continue;
						}
					}
					else {
						newKnobPos = calculateKnobPosForMidiTakeover(modelStackWithParam, modPos, value, knob);
					}

					//Convert the New Knob Position to a Parameter Value
					int32_t newValue =
					    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

					//Set the new Parameter Value for the MIDI Learned Parameter
					modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
					                                                          modLength);

					//	if (midiEngine.midiFollowDisplayParam) {
					//		Param::Kind kind = modelStackWithParam->paramCollection->getParamKind();
					//		view.displayModEncoderValuePopup(kind, modelStackWithParam->paramId, newKnobPos);
					//	}
				}
			}
		}
	}
	return messageUsed;
}

void ModControllableAudio::offerReceivedCCToMidiFollow(ModelStackWithTimelineCounter* modelStack, int32_t channel,
                                                       int32_t ccNumber, int32_t value, bool renderDisplay) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (midiSessionView.paramToCC[xDisplay][yDisplay] != kNoSelection) {
				if ((modelStack != nullptr)
				    && ((AudioEngine::audioSampleTimer - timeLastSentCC[xDisplay][yDisplay]) < kShortPressTime)) {
					return;
				}
				ModelStackWithAutoParam* modelStackWithParam =
				    midiSessionView.getModelStackWithParam(xDisplay, yDisplay, ccNumber, renderDisplay);
				if (modelStackWithParam && modelStackWithParam->autoParam) {
					if (modelStackWithParam->getTimelineCounter()
					    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
						if ((modelStack != nullptr) && (midiSessionView.paramToCC[xDisplay][yDisplay] == ccNumber)) {

							/*DEF_STACK_STRING_BUF(popupMsg, 40);
							popupMsg.append("CH: ");
							popupMsg.appendInt(channel + 1);
							popupMsg.append("; CC: ");
							popupMsg.appendInt(ccNumber);
							popupMsg.append("; Val: ");
							popupMsg.appendInt(value);
							display->displayPopup(popupMsg.c_str());*/

							int32_t oldValue =
							    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);

							int32_t newKnobPos = calculateKnobPosForMidiTakeover(
							    modelStackWithParam, view.modPos, value, nullptr, true, xDisplay, yDisplay);

							//Convert the New Knob Position to a Parameter Value
							int32_t newValue = modelStackWithParam->paramCollection->knobPosToParamValue(
							    newKnobPos, modelStackWithParam);

							if (oldValue != newValue) {
								//Set the new Parameter Value for the MIDI Learned Parameter
								modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam,
								                                                          view.modPos, view.modLength);

								if (midiEngine.midiFollowDisplayParam && renderDisplay) {
									Param::Kind kind = modelStackWithParam->paramCollection->getParamKind();
									view.displayModEncoderValuePopup(kind, modelStackWithParam->paramId, newKnobPos);
								}
							}
						}
						//no midi feedback if this is called by midi being received because that leads to a feedback loop
						//controller sending a value to the deluge has the latest value and doesn't need to be told that same
						//value in return

						//this code will be called in a couple scenarios:
						//use of deluge mod encoders to change values
						//changing context on the deluge (e.g. switching to another view, peeking a clip by holding a pad,
						//changing instruments
						if ((modelStack == nullptr) && midiEngine.midiFollowFeedback) {
							int32_t value =
							    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
							int32_t knobPos =
							    modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);

							midiEngine.sendCC(midiEngine.midiFollowChannelParam,
							                  midiSessionView.paramToCC[xDisplay][yDisplay], knobPos + kKnobPosOffset,
							                  0);

							timeLastSentCC[xDisplay][yDisplay] = AudioEngine::audioSampleTimer;
						}
					}
				}
				//	else {
				//		if (midiEngine.midiFollowFeedback) {
				//			midiEngine.sendCC(midiEngine.midiFollowChannel, midiSessionView.paramToCC[xDisplay][yDisplay],
				//			                  0, 0);

				//			timeLastSentCC[xDisplay][yDisplay] = AudioEngine::audioSampleTimer;
				//		}
				//	}
			}
		}
	}
}

int32_t ModControllableAudio::calculateKnobPosForMidiTakeover(ModelStackWithAutoParam* modelStackWithParam,
                                                              int32_t modPos, int32_t value, MIDIKnob* knob,
                                                              bool midiFollow, int32_t xDisplay, int32_t yDisplay) {
	int32_t newKnobPos = 0;

	if (midiEngine.midiTakeover == MIDITakeoverMode::JUMP) { //Midi Takeover Mode = Jump
		newKnobPos = 64;
		if (value < 127) {
			newKnobPos = value - 64;
		}
		if (knob != nullptr) {
			knob->previousPositionSaved = false;
		}
		else if (midiFollow) {
			midiSessionView.previousKnobPos[xDisplay][yDisplay] = kNoSelection;
		}
	}
	else { //Midi Takeover Mode = Pickup or Value Scaling
		/*

		Step #1: Convert Midi Controller's CC Value to Deluge Knob Position Value

		- Midi CC Values for non endless encoders typically go from 0 to 127
		- Deluge Knob Position Value goes from -64 to 64

		To convert Midi CC Value to Deluge Knob Position Value, subtract 64 from the Midi CC Value

		So a Midi CC Value of 0 is equal to a Deluge Knob Position Value of -64 (0 less 64).

		Similarly a Midi CC Value of 127 is equal to a Deluge Knob Position Value of +63 (127 less 64)

		*/

		int32_t midiKnobPos = value - 64;

		//Save previous knob position for first time
		//The first time a midi knob is turned in a session, no previous midi knob position information exists, so to start, it will be equal to the current midiKnobPos
		//This code is also executed when takeover mode is changed to Jump and back to Pickup/Scale because in Jump mode no previousPosition information gets saved

		if (knob != nullptr) {
			if (!knob->previousPositionSaved) {
				knob->previousPosition = midiKnobPos;

				knob->previousPositionSaved = true;
			}
		}
		else if (midiFollow) {
			if (midiSessionView.previousKnobPos[xDisplay][yDisplay] == kNoSelection) {
				midiSessionView.previousKnobPos[xDisplay][yDisplay] = midiKnobPos;
			}
		}

		//adjust previous knob position saved

		//Here we check to see if the midi knob position previously saved is greater or less than the current midi knob position +/- 1
		//If it's by more than 1, the previous position is adjusted.
		//This could happen for example if you changed banks and the previous position is no longer valid.
		//By resetting the previous position we ensure that the there isn't unwanted jumpyness in the calculation of the midi knob position change amount
		if (knob != nullptr) {
			if (knob->previousPosition > (midiKnobPos + 1) || knob->previousPosition < (midiKnobPos - 1)) {

				knob->previousPosition = midiKnobPos;
			}
		}
		else if (midiFollow) {
			int32_t previousKnobPos = midiSessionView.previousKnobPos[xDisplay][yDisplay];
			if (previousKnobPos > (midiKnobPos + 1) || previousKnobPos < (midiKnobPos - 1)) {

				midiSessionView.previousKnobPos[xDisplay][yDisplay] = midiKnobPos;
			}
		}

		//Here we obtain the current Parameter Value on the Deluge
		int32_t previousValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);

		//Here we convert the current Parameter Value on the Deluge to a Knob Position Value
		int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(previousValue, modelStackWithParam);

		//Here is where we check if the Knob/Fader on the Midi Controller is out of sync with the Deluge Knob Position

		//First we check if the Midi Knob/Fader is sending a Value that is greater than or less than the current Deluge Knob Position by a max difference of +/- kMIDITakeoverKnobSyncThreshold
		//If the difference is greater than kMIDITakeoverKnobSyncThreshold, ignore the CC value change (or scale it if value scaling is on)
		int32_t midiKnobMinPos = knobPos - kMIDITakeoverKnobSyncThreshold;
		int32_t midiKnobMaxPos = knobPos + kMIDITakeoverKnobSyncThreshold;

		if ((midiKnobMinPos <= midiKnobPos) && (midiKnobPos <= midiKnobMaxPos)) {
			newKnobPos = knobPos + (midiKnobPos - knobPos);
		}
		else {
			//if the above conditions fail and pickup mode is enabled, then the Deluge Knob Position (and therefore the Parameter Value with it) remains unchanged
			if (midiEngine.midiTakeover == MIDITakeoverMode::PICKUP) { //Midi Pickup Mode On
				newKnobPos = knobPos;
			}
			//if the first two conditions fail and value scaling mode is enabled, then the Deluge Knob Position is scaled upwards or downwards based on relative
			//positions of Midi Controller Knob and Deluge Knob to min/max of knob range.
			else { //Midi Value Scaling Mode On
				//Set the max and min of the deluge midi knob position range
				int32_t knobMaxPos = 64;
				int32_t knobMinPos = -64;

				//calculate amount of deluge "knob runway" is remaining from current knob position to max and min of knob position range
				int32_t delugeKnobMaxPosDelta = knobMaxPos - knobPos; //Positive Runway
				int32_t delugeKnobMinPosDelta = knobPos - knobMinPos; //Negative Runway

				//calculate amount of midi "knob runway" is remaining from current knob position to max and min of knob position range
				int32_t midiKnobMaxPosDelta = knobMaxPos - midiKnobPos; //Positive Runway
				int32_t midiKnobMinPosDelta = midiKnobPos - knobMinPos; //Negative Runway

				//calculate by how much the current midiKnobPos has changed from the previous midiKnobPos recorded
				int32_t midiKnobPosChange = 0;
				if (knob != nullptr) {
					midiKnobPosChange = midiKnobPos - knob->previousPosition;
				}
				else if (midiFollow) {
					midiKnobPosChange = midiKnobPos - midiSessionView.previousKnobPos[xDisplay][yDisplay];
				}

				//Set fixed point variable which will be used calculate the percentage in midi knob position
				int32_t midiKnobPosChangePercentage;

				//if midi knob position change is greater than 0, then the midi knob position has increased (e.g. turned knob right)
				if (midiKnobPosChange > 0) {
					//fixed point math calculation of new deluge knob position when midi knob position has increased

					midiKnobPosChangePercentage = (midiKnobPosChange << 20) / midiKnobMaxPosDelta;

					newKnobPos = knobPos + ((delugeKnobMaxPosDelta * midiKnobPosChangePercentage) >> 20);
				}
				//if midi knob position change is less than 0, then the midi knob position has decreased (e.g. turned knob left)
				else if (midiKnobPosChange < 0) {
					//fixed point math calculation of new deluge knob position when midi knob position has decreased

					midiKnobPosChangePercentage = (midiKnobPosChange << 20) / midiKnobMinPosDelta;

					newKnobPos = knobPos + ((delugeKnobMinPosDelta * midiKnobPosChangePercentage) >> 20);
				}
				//if midi knob position change is 0, then the midi knob position has not changed and thus no change in deluge knob position / parameter value is required
				else {
					newKnobPos = knobPos;
				}
			}
		}

		//save the current midi knob position as the previous midi knob position so that it can be used next time the takeover code is executed
		if (knob != nullptr) {
			knob->previousPosition = midiKnobPos;
		}
		else if (midiFollow) {
			midiSessionView.previousKnobPos[xDisplay][yDisplay] = midiKnobPos;
		}
	}

	return newKnobPos;
}

// Returns true if the message was used by something
bool ModControllableAudio::offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1,
                                                                 uint8_t data2,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int32_t noteRowIndex) {

	bool messageUsed = false;

	// For each MIDI knob...
	for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);

		// If this is the knob...
		if (knob->midiInput.equalsNoteOrCC(fromDevice, channel,
		                                   128)) { // I've got 128 representing pitch bend here... why again?

			messageUsed = true;

			// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a region.
			int32_t modPos = 0;
			int32_t modLength = 0;

			if (modelStack->timelineCounterIsSet()) {
				if (view.modLength
				    && modelStack->getTimelineCounter()
				           == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
				}

				modelStack->getTimelineCounter()->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from scratch now from that.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam->autoParam) {

				uint32_t value14 = (uint32_t)data1 | ((uint32_t)data2 << 7);

				int32_t newValue = (value14 << 18) - 2147483648;

				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);
				return true;
			}
		}
	}
	return messageUsed;
}

void ModControllableAudio::beginStutter(ParamManagerForTimeline* paramManager) {
	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	    && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW
	    && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		return;
	}

	// Quantized Stutter FX
	if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::QuantizedStutterRate) == RuntimeFeatureStateToggle::On) {
		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
		int32_t paramValue = unpatchedParams->getValue(Param::Unpatched::STUTTER_RATE);
		int32_t knobPos = unpatchedParams->paramValueToKnobPos(paramValue, NULL);
		if (knobPos < -39) {
			knobPos = -16; // 4ths
		}
		else if (knobPos < -14) {
			knobPos = -8; // 8ths
		}
		else if (knobPos < 14) {
			knobPos = 0; // 16ths
		}
		else if (knobPos < 39) {
			knobPos = 8; // 32nds
		}
		else {
			knobPos = 16; // 64ths
		}
		// Save current values for later recovering them
		stutterer.valueBeforeStuttering = paramValue;
		stutterer.lastQuantizedKnobDiff = knobPos;

		// When stuttering, we center the value at 0, so the center is the reference for the stutter rate that we selected just before pressing the knob
		// and we use the lastQuantizedKnobDiff value to calculate the relative (real) value
		unpatchedParams->params[Param::Unpatched::STUTTER_RATE].setCurrentValueBasicForSetup(0);
		view.notifyParamAutomationOccurred(paramManager);
	}

	// You'd think I should apply "false" here, to make it not add extra space to the buffer, but somehow this seems to sound as good if not better (in terms of ticking / crackling)...
	bool error = stutterer.buffer.init(getStutterRate(paramManager), 0, true);
	if (error == NO_ERROR) {
		stutterer.status = STUTTERER_STATUS_RECORDING;
		stutterer.sizeLeftUntilRecordFinished = stutterer.buffer.size;
		enterUIMode(UI_MODE_STUTTERING);
	}
}

// paramManager is optional - if you don't send it, it won't change the stutter rate
void ModControllableAudio::endStutter(ParamManagerForTimeline* paramManager) {
	stutterer.buffer.discard();
	stutterer.status = STUTTERER_STATUS_OFF;
	exitUIMode(UI_MODE_STUTTERING);

	if (paramManager) {

		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

		if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::QuantizedStutterRate)
		    == RuntimeFeatureStateToggle::On) {
			// Quantized Stutter FX (set back the value it had just before stuttering so orange LEDs are redrawn)
			unpatchedParams->params[Param::Unpatched::STUTTER_RATE].setCurrentValueBasicForSetup(
			    stutterer.valueBeforeStuttering);
			view.notifyParamAutomationOccurred(paramManager);
		}
		else {
			// Regular Stutter FX (if below middle value, reset it back to middle)
			// Normally we shouldn't call this directly, but it's ok because automation isn't allowed for stutter anyway
			if (unpatchedParams->getValue(Param::Unpatched::STUTTER_RATE) < 0) {
				unpatchedParams->params[Param::Unpatched::STUTTER_RATE].setCurrentValueBasicForSetup(0);
				view.notifyParamAutomationOccurred(paramManager);
			}
		}
	}
	// Reset temporary and diff values for Quantized stutter
	stutterer.lastQuantizedKnobDiff = 0;
	stutterer.valueBeforeStuttering = 0;
}

void ModControllableAudio::switchDelayPingPong() {
	delay.pingPong = !delay.pingPong;

	char const* displayText;
	switch (delay.pingPong) {
	case 0:
		displayText = "Normal delay";
		break;

	default:
		displayText = "Ping-pong delay";
		break;
	}
	display->displayPopup(displayText);
}

void ModControllableAudio::switchDelayAnalog() {
	delay.analog = !delay.analog;

	char const* displayText;
	switch (delay.analog) {
	case 0:
		displayText = "Digital delay";
		break;

	default:
		displayText = deluge::l10n::get(deluge::l10n::String::STRING_FOR_ANALOG_DELAY);
		break;
	}
	display->displayPopup(displayText);
}

void ModControllableAudio::switchDelaySyncType() {
	switch (delay.syncType) {
	case SYNC_TYPE_TRIPLET:
		delay.syncType = SYNC_TYPE_DOTTED;
		break;
	case SYNC_TYPE_DOTTED:
		delay.syncType = SYNC_TYPE_EVEN;
		break;

	default: //SYNC_TYPE_EVEN
		delay.syncType = SYNC_TYPE_TRIPLET;
		break;
	}

	char const* displayText;
	switch (delay.syncType) {
	case SYNC_TYPE_TRIPLET:
		displayText = "Triplet";
		break;
	case SYNC_TYPE_DOTTED:
		displayText = "Dotted";
		break;

	default: //SYNC_TYPE_EVEN
		displayText = "Even";
		break;
	}
	display->displayPopup(displayText);
}

void ModControllableAudio::switchDelaySyncLevel() {
	// Note: SYNC_LEVEL_NONE (value 0) can't be selected
	delay.syncLevel = (SyncLevel)((delay.syncLevel) % SyncLevel::SYNC_LEVEL_256TH + 1); //cycle from 1 to 9 (omit 0)

	char* buffer = shortStringBuffer;
	currentSong->getNoteLengthName(buffer, (uint32_t)3 << (SYNC_LEVEL_256TH - delay.syncLevel));
	if (display->haveOLED()) {
		// Need to delete "-notes" from the name
		std::string noteName(buffer);
		std::string cleanName = noteName.substr(0, noteName.find("-notes"));
		display->displayPopup(cleanName.c_str());
	}
	else {
		// 7 Seg just display it
		display->displayPopup(buffer);
	}
}

void ModControllableAudio::switchLPFMode() {
	lpfMode = static_cast<FilterMode>((util::to_underlying(lpfMode) + 1) % kNumLPFModes);

	char const* displayText;
	switch (lpfMode) {
		using enum deluge::l10n::String;
	case FilterMode::TRANSISTOR_12DB:
		displayText = l10n::get(STRING_FOR_12DB_LADDER);
		break;

	case FilterMode::TRANSISTOR_24DB:
		displayText = l10n::get(STRING_FOR_24DB_LADDER);
		break;

	case FilterMode::TRANSISTOR_24DB_DRIVE:
		displayText = l10n::get(STRING_FOR_DRIVE);
		break;

	case FilterMode::SVF_BAND:
		displayText = l10n::get(STRING_FOR_SVF_BAND);
		break;
	case FilterMode::SVF_NOTCH:
		displayText = l10n::get(STRING_FOR_SVF_NOTCH);
		break;
	}
	display->displayPopup(displayText);
}
void ModControllableAudio::switchHPFMode() {
	//this works fine, the offset to the first hpf doesn't matter with the modulus
	hpfMode = static_cast<FilterMode>((util::to_underlying(hpfMode) + 1) % kNumHPFModes + kFirstHPFMode);

	char const* displayText;
	switch (hpfMode) {
		using enum deluge::l10n::String;
	case FilterMode::HPLADDER:
		displayText = l10n::get(STRING_FOR_HPLADDER);
		break;
	case FilterMode::SVF_BAND:
		displayText = l10n::get(STRING_FOR_SVF_BAND);
		break;
	case FilterMode::SVF_NOTCH:
		displayText = l10n::get(STRING_FOR_SVF_NOTCH);
		break;
	}
	display->displayPopup(displayText);
}

// This can get called either for hibernation, or because drum now has no active noteRow
void ModControllableAudio::wontBeRenderedForAWhile() {
	delay.discardBuffers();
	endStutter(NULL);
}

void ModControllableAudio::clearModFXMemory() {
	if (modFXType == ModFXType::FLANGER || modFXType == ModFXType::CHORUS || modFXType == ModFXType::CHORUS_STEREO) {
		if (modFXBuffer) {
			memset(modFXBuffer, 0, kModFXBufferSize * sizeof(StereoSample));
		}
	}
	else if (modFXType == ModFXType::GRAIN) {
		for (int i = 0; i < 8; i++) {
			grains[i].length = 0;
		}
		grainInitialized = false;
		modFXGrainBufferWriteIndex = 0;
	}
	else if (modFXType == ModFXType::PHASER) {
		memset(allpassMemory, 0, sizeof(allpassMemory));
		memset(&phaserMemory, 0, sizeof(phaserMemory));
	}
}

bool ModControllableAudio::setModFXType(ModFXType newType) {
	// For us ModControllableAudios, this is really simple. Memory gets allocated in GlobalEffectable::processFXForGlobalEffectable().
	// This function is overridden in Sound
	modFXType = newType;
	return true;
}

// whichKnob is either which physical mod knob, or which MIDI CC code.
// For mod knobs, supply midiChannel as 255
// Returns false if fail due to insufficient RAM.
bool ModControllableAudio::learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob,
                                     uint8_t modKnobMode, uint8_t midiChannel, Song* song) {

	bool overwroteExistingKnob = false;

	// If a mod knob
	if (midiChannel >= 16) {
		return false;

		// TODO: make function not virtual after this changed

		/*
		// If that knob was patched to something else...
		overwroteExistingKnob = (modKnobs[modKnobMode][whichKnob].s != s || modKnobs[modKnobMode][whichKnob].p != p);

		modKnobs[modKnobMode][whichKnob].s = s;
		modKnobs[modKnobMode][whichKnob].p = p;
		*/
	}

	// If a MIDI knob
	else {

		MIDIKnob* knob;

		// Was this MIDI knob already set to control this thing?
		for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
			knob = midiKnobArray.getElement(k);
			if (knob->midiInput.equalsNoteOrCC(fromDevice, midiChannel, whichKnob)
			    && paramDescriptor == knob->paramDescriptor) {
				//overwroteExistingKnob = (midiKnobs[k].s != s || midiKnobs[k].p != p);
				goto midiKnobFound;
			}
		}

		// Or if we're here, it doesn't already exist, so find an unused MIDIKnob
		knob = midiKnobArray.insertKnobAtEnd();
		if (!knob) {
			return false;
		}

midiKnobFound:
		knob->midiInput.noteOrCC = whichKnob;
		knob->midiInput.channelOrZone = midiChannel;
		knob->midiInput.device = fromDevice;
		knob->paramDescriptor = paramDescriptor;
		knob->relative = (whichKnob != 128); // Guess that it's relative, unless this is a pitch-bend "knob"
	}

	if (overwroteExistingKnob) {
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
	}

	return true;
}

// Returns whether anything was found to unlearn
bool ModControllableAudio::unlearnKnobs(ParamDescriptor paramDescriptor, Song* song) {
	bool anythingFound = false;

	// I've deactivated the unlearning of mod knobs, mainly because, if you want to unlearn a MIDI knob, you might not want to also deactivate a mod knob to the same param at the same time

	for (int32_t k = 0; k < midiKnobArray.getNumElements();) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		if (knob->paramDescriptor == paramDescriptor) {
			anythingFound = true;
			midiKnobArray.deleteAtIndex(k);
		}
		else {
			k++;
		}
	}

	if (anythingFound) {
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
	}

	return anythingFound;
}

char const* ModControllableAudio::paramToString(uint8_t param) {

	switch (param) {

		// Unpatched params
	case Param::Unpatched::START + Param::Unpatched::STUTTER_RATE:
		return "stutterRate";

	case Param::Unpatched::START + Param::Unpatched::BASS:
		return "bass";

	case Param::Unpatched::START + Param::Unpatched::TREBLE:
		return "treble";

	case Param::Unpatched::START + Param::Unpatched::BASS_FREQ:
		return "bassFreq";

	case Param::Unpatched::START + Param::Unpatched::TREBLE_FREQ:
		return "trebleFreq";

	case Param::Unpatched::START + Param::Unpatched::SAMPLE_RATE_REDUCTION:
		return "sampleRateReduction";

	case Param::Unpatched::START + Param::Unpatched::BITCRUSHING:
		return "bitcrushAmount";

	case Param::Unpatched::START + Param::Unpatched::MOD_FX_OFFSET:
		return "modFXOffset";

	case Param::Unpatched::START + Param::Unpatched::MOD_FX_FEEDBACK:
		return "modFXFeedback";

	case Param::Unpatched::START + Param::Unpatched::COMPRESSOR_SHAPE:
		return "compressorShape";

	default:
		return "none";
	}
}

int32_t ModControllableAudio::stringToParam(char const* string) {
	for (int32_t p = Param::Unpatched::START; p < Param::Unpatched::START + Param::Unpatched::NUM_SHARED; p++) {
		if (!strcmp(string, ModControllableAudio::paramToString(p))) {
			return p;
		}
	}
	return Param::Global::NONE;
}

ModelStackWithAutoParam* ModControllableAudio::getParamFromModEncoder(int32_t whichModEncoder,
                                                                      ModelStackWithThreeMainThings* modelStack,
                                                                      bool allowCreation) {

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	int32_t paramId;
	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;

	ModelStackWithParamId* newModelStack1 = modelStack->addParamCollectionAndId(paramCollection, summary, paramId);
	return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
}
