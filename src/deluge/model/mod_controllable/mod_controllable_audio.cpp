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
#include "dsp/stereo_sample.h"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_takeover.h"
#include "mem_functions.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"

namespace params = deluge::modulation::params;

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

ModControllableAudio::ModControllableAudio() {

	// Mod FX
	modFXBuffer = nullptr;
	modFXBufferWriteIndex = 0;

	// Grain
	modFXGrainBuffer = nullptr;
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

	// Filters
	lpfMode = FilterMode::TRANSISTOR_24DB;
	hpfMode = FilterMode::HPLADDER;
	filterRoute = FilterRoute::HIGH_TO_LOW;

	// Sample rate reduction
	sampleRateReductionOnLastTime = false;

	// Saturation
	clippingAmount = 0;

	SyncLevel syncLevel;
	Song* song = preLoadedSong;
	if (!song) {
		song = currentSong;
	}
	if (song) {
		syncLevel = (SyncLevel)(8 - (song->insideWorldTickMagnitude + song->insideWorldTickMagnitudeOffsetFromBPM));
	}
	else {
		syncLevel = (SyncLevel)(8 - FlashStorage::defaultMagnitude);
	}
	delay.syncLevel = syncLevel;
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
	sidechain.cloneFrom(&other->sidechain);
	midiKnobArray.cloneFrom(&other->midiKnobArray); // Could fail if no RAM... not too big a concern
	delay = other->delay;
}

void ModControllableAudio::initParams(ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->params[params::UNPATCHED_BASS].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_TREBLE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_BASS_FREQ].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_TREBLE_FREQ].setCurrentValueBasicForSetup(0);

	Stutterer::initParams(paramManager);

	unpatchedParams->params[params::UNPATCHED_MOD_FX_OFFSET].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[params::UNPATCHED_SAMPLE_RATE_REDUCTION].setCurrentValueBasicForSetup(-2147483648);

	unpatchedParams->params[params::UNPATCHED_BITCRUSHING].setCurrentValueBasicForSetup(-2147483648);

	unpatchedParams->params[params::UNPATCHED_SIDECHAIN_SHAPE].setCurrentValueBasicForSetup(-601295438);
	unpatchedParams->params[params::UNPATCHED_COMPRESSOR_THRESHOLD].setCurrentValueBasicForSetup(0);
}

bool ModControllableAudio::hasBassAdjusted(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(params::UNPATCHED_BASS) != 0);
}

bool ModControllableAudio::hasTrebleAdjusted(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(params::UNPATCHED_TREBLE) != 0);
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
	// max possible, feedback doesn't go very high
	else {
		wrapsToShutdown = 4;
	}
}

void ModControllableAudio::processFX(StereoSample* buffer, int32_t numSamples, ModFXType modFXType, int32_t modFXRate,
                                     int32_t modFXDepth, const Delay::State& delayWorkingState, int32_t* postFXVolume,
                                     ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	StereoSample* bufferEnd = buffer + numSamples;

	// Mod FX -----------------------------------------------------------------------------------
	if (modFXType != ModFXType::NONE) {

		LFOType modFXLFOWaveType;
		int32_t modFXDelayOffset;
		int32_t thisModFXDelayDepth;
		int32_t feedback;

		if (modFXType == ModFXType::FLANGER || modFXType == ModFXType::PHASER) {

			int32_t a = unpatchedParams->getValue(params::UNPATCHED_MOD_FX_FEEDBACK) >> 1;
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
			    kModFXMaxDelay, (unpatchedParams->getValue(params::UNPATCHED_MOD_FX_OFFSET) >> 1) + 1073741824);
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
			grainSize = 44 * ((((unpatchedParams->getValue(params::UNPATCHED_MOD_FX_OFFSET) >> 1) + 1073741824) >> 21));
			grainSize = std::clamp<int32_t>(grainSize, 440, 35280); // 10ms - 800ms
			// Rate
			int32_t grainRateRaw = std::clamp<int32_t>((quickLog(modFXRate) - 364249088) >> 21, 0, 256);
			grainRate = ((360 * grainRateRaw >> 8) * grainRateRaw >> 8); // 0 - 180hz
			grainRate = std::max<int32_t>(1, grainRate);
			grainRate = (kSampleRate << 1) / grainRate;
			// Preset 0=default
			grainPitchType = (int8_t)(multiply_32x32_rshift32_rounded(
			    unpatchedParams->getValue(params::UNPATCHED_MOD_FX_FEEDBACK), 5)); // Select 5 presets -2 to 2
			grainPitchType = std::clamp<int8_t>(grainPitchType, -2, 2);
			// Temposync
			if (grainPitchType == 2) {
				int tempoBPM = (int32_t)(playbackHandler.calculateBPM(currentSong->getTimePerTimerTickFloat()) + 0.5);
				grainRate = std::clamp<int32_t>(256 - grainRateRaw, 0, 256) << 4; // 4096msec
				grainRate = 44 * grainRate;                                       //(kSampleRate*grainRate)/1000;
				int32_t baseNoteSamples = (kSampleRate * 60 / tempoBPM);          // 4th
				if (grainRate < baseNoteSamples) {
					baseNoteSamples = baseNoteSamples >> 2; // 16th
				}
				grainRate = std::clamp<int32_t>((grainRate / baseNoteSamples) * baseNoteSamples, baseNoteSamples,
				                                baseNoteSamples << 2);            // Quantize
				if (grainRate < 2205) {                                           // 50ms = 20hz
					grainSize = std::min<int32_t>(grainSize, grainRate << 3) - 1; // 16 layers=<<4, 8layers = <<3
				}
				bool currentTickCountIsZero = (playbackHandler.getCurrentInternalTickCount() == 0);
				if (grainLastTickCountIsZero && currentTickCountIsZero == false) { // Start Playback
					modFXGrainBufferWriteIndex = 0;                                // Reset WriteIndex
				}
				grainLastTickCountIsZero = currentTickCountIsZero;
			}
			// Rate Adjustment
			if (grainRate < 882) {                                            // 50hz or more
				grainSize = std::min<int32_t>(grainSize, grainRate << 3) - 1; // 16 layers=<<4, 8layers = <<3
			}
			// Volume
			grainVol = modFXDepth - 2147483648;
			grainVol =
			    (multiply_32x32_rshift32_rounded(multiply_32x32_rshift32_rounded(grainVol, grainVol), grainVol) << 2)
			    + 2147483648; // Cubic
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
								if (!grains[i].rev) { // forward
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
				// Feedback (Below grainFeedbackVol means "grainVol >> 4")
				modFXGrainBuffer[writeIndex].l =
				    multiply_accumulate_32x32_rshift32_rounded(currentSample->l, grains_l, grainFeedbackVol);
				modFXGrainBuffer[writeIndex].r =
				    multiply_accumulate_32x32_rshift32_rounded(currentSample->r, grains_r, grainFeedbackVol);
				// WET and DRY Vol
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
	int32_t positive = (unpatchedParams->getValue(params::UNPATCHED_BASS) >> 1) + 1073741824;
	int32_t bassAmount = (multiply_32x32_rshift32_rounded(positive, positive) << 1) - 536870912;

	// Treble. No-change represented by 536870912
	positive = (unpatchedParams->getValue(params::UNPATCHED_TREBLE) >> 1) + 1073741824;
	int32_t trebleAmount = multiply_32x32_rshift32_rounded(positive, positive) << 1;

	if (thisDoBass || thisDoTreble) {

		if (thisDoBass) {
			bassFreq = getExp(120000000, (unpatchedParams->getValue(params::UNPATCHED_BASS_FREQ) >> 5) * 6);
		}

		if (thisDoTreble) {
			trebleFreq = getExp(700000000, (unpatchedParams->getValue(params::UNPATCHED_TREBLE_FREQ) >> 5) * 6);
		}

		StereoSample* currentSample = buffer;
		do {
			doEQ(thisDoBass, thisDoTreble, &currentSample->l, &currentSample->r, bassAmount, trebleAmount);
		} while (++currentSample != bufferEnd);
	}

	// Delay ----------------------------------------------------------------------------------
	delay.process({buffer, static_cast<size_t>(numSamples)}, delayWorkingState);
}

void ModControllableAudio::processReverbSendAndVolume(StereoSample* buffer, int32_t numSamples, int32_t* reverbBuffer,
                                                      int32_t postFXVolume, int32_t postReverbVolume,
                                                      int32_t reverbSendAmount, int32_t pan,
                                                      bool doAmplitudeIncrement) {

	StereoSample* bufferEnd = buffer + numSamples;

	int32_t reverbSendAmountAndPostFXVolume = multiply_32x32_rshift32(postFXVolume, reverbSendAmount) << 5;

	int32_t postFXAndReverbVolumeL, postFXAndReverbVolumeR, amplitudeIncrementL, amplitudeIncrementR;
	postFXAndReverbVolumeL = postFXAndReverbVolumeR = (multiply_32x32_rshift32(postReverbVolume, postFXVolume) << 5);

	// The amplitude increment applies to the post-FX volume. We want to have it just so that we can respond better to
	// sidechain volume ducking, which is done through post-FX volume.
	if (doAmplitudeIncrement) {
		auto postReverbSendVolumeIncrement =
		    (int32_t)((double)(postReverbVolume - postReverbVolumeLastTime) / (double)numSamples);
		amplitudeIncrementL = amplitudeIncrementR =
		    (multiply_32x32_rshift32(postFXVolume, postReverbSendVolumeIncrement) << 5);
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
	postReverbVolumeLastTime = postReverbVolume;
}

bool ModControllableAudio::isBitcrushingEnabled(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(params::UNPATCHED_BITCRUSHING) >= -2113929216);
}

bool ModControllableAudio::isSRREnabled(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(params::UNPATCHED_SAMPLE_RATE_REDUCTION) != -2147483648);
}

void ModControllableAudio::processSRRAndBitcrushing(StereoSample* buffer, int32_t numSamples, int32_t* postFXVolume,
                                                    ParamManager* paramManager) {
	StereoSample const* const bufferEnd = buffer + numSamples;

	uint32_t bitCrushMaskForSRR = 0xFFFFFFFF;

	bool srrEnabled = isSRREnabled(paramManager);

	// Bitcrushing ------------------------------------------------------------------------------
	if (isBitcrushingEnabled(paramManager)) {
		uint32_t positivePreset =
		    (paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_BITCRUSHING) + 2147483648) >> 29;
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

		// This function, slightly unusually, uses 22 bits to represent "1". That's 4194304. I tried using 24, but stuff
		// started clipping off where I needed it if sample rate too low

		uint32_t positivePreset =
		    paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_SAMPLE_RATE_REDUCTION) + 2147483648;
		int32_t lowSampleRateIncrement = getExp(4194304, (positivePreset >> 3));
		int32_t highSampleRateIncrement = ((uint32_t)0xFFFFFFFF / (lowSampleRateIncrement >> 6)) << 6;
		// int32_t highSampleRateIncrement = getExp(4194304, -(int32_t)(positivePreset >> 3)); // This would work too

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
				// I previously had it using strength2 instead of "lowSampleRatePos & 16777215", but that just works
				// better. Ah, writing the massive explanation would take ages.
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

void ModControllableAudio::writeAttributesToFile(Serializer& writer) {
	writer.writeAttribute("modFXType", (char*)fxTypeToString(modFXType));
	writer.writeAttribute("lpfMode", (char*)lpfTypeToString(lpfMode));
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	writer.writeAttribute("hpfMode", (char*)lpfTypeToString(hpfMode));
	writer.writeAttribute("filterRoute", (char*)filterRouteToString(filterRoute));
	if (clippingAmount) {
		writer.writeAttribute("clippingAmount", clippingAmount);
	}
}

void ModControllableAudio::writeTagsToFile(Serializer& writer) {
	// Delay
	writer.writeOpeningTagBeginning("delay");
	writer.writeAttribute("pingPong", delay.pingPong);
	writer.writeAttribute("analog", delay.analog);
	writer.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", delay.syncLevel, true);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	writer.writeSyncTypeToFile(currentSong, "syncType", delay.syncType, true);
	writer.closeTag();

	// MIDI knobs
	if (midiKnobArray.getNumElements()) {
		writer.writeArrayStart("midiKnobs");
		for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
			MIDIKnob* knob = midiKnobArray.getElement(k);
			writer.writeOpeningTagBeginning("midiKnob", true);
			knob->midiInput.writeAttributesToFile(
			    writer,
			    MIDI_MESSAGE_CC); // Writes channel and CC, but not device - we do that below.
			writer.writeAttribute("relative", knob->relative);
			writer.writeAttribute("controlsParam", params::paramNameForFile(unpatchedParamKind_,
			                                                                knob->paramDescriptor.getJustTheParam()));
			if (!knob->paramDescriptor.isJustAParam()) { // TODO: this only applies to Sounds
				writer.writeAttribute("patchAmountFromSource",
				                      sourceToString(knob->paramDescriptor.getTopLevelSource()));

				if (knob->paramDescriptor.hasSecondSource()) {
					writer.writeAttribute("patchAmountFromSecondSource",
					                      sourceToString(knob->paramDescriptor.getSecondSourceFromTop()));
				}
			}

			// Because we manually called LearnedMIDI::writeAttributesToFile() above, we have to give the MIDIDevice its
			// own tag, cos that can't be written as just an attribute.
			if (knob->midiInput.device) {
				writer.writeOpeningTagEnd();
				knob->midiInput.device->writeReferenceToFile(writer);
				writer.writeClosingTag("midiKnob", true, true);
			}
			else {
				writer.closeTag();
			}
		}
		writer.writeArrayEnding("midiKnobs");
	}

	// Sidechain (renamed from "compressor" from the official firmware)
	writer.writeOpeningTagBeginning("sidechain");
	writer.writeAttribute("attack", sidechain.attack);
	writer.writeAttribute("release", sidechain.release);
	writer.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", sidechain.syncLevel, true);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	writer.writeSyncTypeToFile(currentSong, "syncType", sidechain.syncType, true);
	writer.closeTag();

	// Audio compressor (this section is all new so we write it at the end)
	writer.writeOpeningTagBeginning("audioCompressor");
	writer.writeAttribute("attack", compressor.getAttack());
	writer.writeAttribute("release", compressor.getRelease());
	writer.writeAttribute("thresh", compressor.getThreshold());
	writer.writeAttribute("ratio", compressor.getRatio());
	writer.writeAttribute("compHPF", compressor.getSidechain());
	writer.writeAttribute("compBlend", compressor.getBlend());
	writer.closeTag();
}

void ModControllableAudio::writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager,
                                                      bool writeAutomation, int32_t* valuesForOverride) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute(writer, "stutterRate", params::UNPATCHED_STUTTER_RATE, writeAutomation,
	                                       false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "sampleRateReduction", params::UNPATCHED_SAMPLE_RATE_REDUCTION,
	                                       writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "bitCrush", params::UNPATCHED_BITCRUSHING, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "modFXOffset", params::UNPATCHED_MOD_FX_OFFSET, writeAutomation,
	                                       false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "modFXFeedback", params::UNPATCHED_MOD_FX_FEEDBACK, writeAutomation,
	                                       false, valuesForOverride);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	unpatchedParams->writeParamAsAttribute(writer, "compressorThreshold", params::UNPATCHED_COMPRESSOR_THRESHOLD,
	                                       writeAutomation, false, valuesForOverride);
}

void ModControllableAudio::writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                                int32_t* valuesForOverride) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	writer.writeOpeningTagBeginning("equalizer");
	unpatchedParams->writeParamAsAttribute(writer, "bass", params::UNPATCHED_BASS, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "treble", params::UNPATCHED_TREBLE, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "bassFrequency", params::UNPATCHED_BASS_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "trebleFrequency", params::UNPATCHED_TREBLE_FREQ, writeAutomation,
	                                       false, valuesForOverride);
	writer.closeTag();
}

bool ModControllableAudio::readParamTagFromFile(Deserializer& reader, char const* tagName,
                                                ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "equalizer")) {
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "bass")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_BASS,
				                           readAutomationUpToPos);
				reader.exitTag("bass");
			}
			else if (!strcmp(tagName, "treble")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_TREBLE,
				                           readAutomationUpToPos);
				reader.exitTag("treble");
			}
			else if (!strcmp(tagName, "bassFrequency")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_BASS_FREQ,
				                           readAutomationUpToPos);
				reader.exitTag("bassFrequency");
			}
			else if (!strcmp(tagName, "trebleFrequency")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_TREBLE_FREQ,
				                           readAutomationUpToPos);
				reader.exitTag("trebleFrequency");
			}
		}
		reader.exitTag("equalizer", true);
	}

	else if (!strcmp(tagName, "stutterRate")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_STUTTER_RATE,
		                           readAutomationUpToPos);
		reader.exitTag("stutterRate");
	}

	else if (!strcmp(tagName, "sampleRateReduction")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_SAMPLE_RATE_REDUCTION,
		                           readAutomationUpToPos);
		reader.exitTag("sampleRateReduction");
	}

	else if (!strcmp(tagName, "bitCrush")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_BITCRUSHING,
		                           readAutomationUpToPos);
		reader.exitTag("bitCrush");
	}

	else if (!strcmp(tagName, "modFXOffset")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_MOD_FX_OFFSET,
		                           readAutomationUpToPos);
		reader.exitTag("modFXOffset");
	}

	else if (!strcmp(tagName, "modFXFeedback")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_MOD_FX_FEEDBACK,
		                           readAutomationUpToPos);
		reader.exitTag("modFXFeedback");
	}
	else if (!strcmp(tagName, "compressorThreshold")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_COMPRESSOR_THRESHOLD,
		                           readAutomationUpToPos);
		reader.exitTag("compressorThreshold");
	}

	else {
		return false;
	}

	return true;
}

// paramManager is optional
Error ModControllableAudio::readTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                            Song* song) {

	int32_t p;

	if (!strcmp(tagName, "lpfMode")) {
		lpfMode = stringToLPFType(reader.readTagOrAttributeValue());
		reader.exitTag("lpfMode");
	}
	else if (!strcmp(tagName, "hpfMode")) {
		hpfMode = stringToLPFType(reader.readTagOrAttributeValue());
		reader.exitTag("hpfMode");
	}
	else if (!strcmp(tagName, "filterRoute")) {
		filterRoute = stringToFilterRoute(reader.readTagOrAttributeValue());
		reader.exitTag("filterRoute");
	}

	else if (!strcmp(tagName, "clippingAmount")) {
		clippingAmount = reader.readTagOrAttributeValueInt();
		reader.exitTag("clippingAmount");
	}

	else if (!strcmp(tagName, "delay")) {
		// Set default values in case they are not configured
		delay.syncType = SYNC_TYPE_EVEN;
		delay.syncLevel = SYNC_LEVEL_NONE;
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {

			// These first two ensure compatibility with old files (pre late 2016 I think?)
			if (!strcmp(tagName, "feedback")) {
				p = params::GLOBAL_DELAY_FEEDBACK;
doReadPatchedParam:
				if (paramManager) {
					if (!paramManager->containsAnyMainParamCollections()) {
						Error error = Sound::createParamManagerForLoading(paramManager);
						if (error != Error::NONE) {
							return error;
						}
					}
					ParamCollectionSummary* patchedParamsSummary = paramManager->getPatchedParamSetSummary();
					PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;
					patchedParams->readParam(reader, patchedParamsSummary, params::GLOBAL_DELAY_FEEDBACK,
					                         readAutomationUpToPos);
				}
				reader.exitTag();
			}
			else if (!strcmp(tagName, "rate")) {
				p = params::GLOBAL_DELAY_RATE;
				goto doReadPatchedParam;
			}

			else if (!strcmp(tagName, "pingPong")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				delay.pingPong = static_cast<bool>(std::clamp(contents, 0_i32, 1_i32));
				reader.exitTag("pingPong");
			}
			else if (!strcmp(tagName, "analog")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				delay.analog = static_cast<bool>(std::clamp(contents, 0_i32, 1_i32));
				reader.exitTag("analog");
			}
			else if (!strcmp(tagName, "syncType")) {
				delay.syncType = (SyncType)reader.readTagOrAttributeValueInt();
				reader.exitTag("syncType");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				delay.syncLevel =
				    (SyncLevel)song->convertSyncLevelFromFileValueToInternalValue(reader.readTagOrAttributeValueInt());
				reader.exitTag("syncLevel");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("delay", true);
	}

	else if (!strcmp(tagName, "audioCompressor")) {
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				q31_t masterCompressorAttack = reader.readTagOrAttributeValueInt();
				compressor.setAttack(masterCompressorAttack);
				reader.exitTag("attack");
			}
			else if (!strcmp(tagName, "release")) {
				q31_t masterCompressorRelease = reader.readTagOrAttributeValueInt();
				compressor.setRelease(masterCompressorRelease);
				reader.exitTag("release");
			}
			else if (!strcmp(tagName, "thresh")) {
				q31_t masterCompressorThresh = reader.readTagOrAttributeValueInt();
				compressor.setThreshold(masterCompressorThresh);
				reader.exitTag("thresh");
			}
			else if (!strcmp(tagName, "ratio")) {
				q31_t masterCompressorRatio = reader.readTagOrAttributeValueInt();
				compressor.setRatio(masterCompressorRatio);
				reader.exitTag("ratio");
			}
			else if (!strcmp(tagName, "compHPF")) {
				q31_t masterCompressorSidechain = reader.readTagOrAttributeValueInt();
				compressor.setSidechain(masterCompressorSidechain);
				reader.exitTag("compHPF");
			}
			else if (!strcmp(tagName, "compBlend")) {
				q31_t masterCompressorBlend = reader.readTagOrAttributeValueInt();
				compressor.setBlend(masterCompressorBlend);
				reader.exitTag("compBlend");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("AudioCompressor", true);
	}
	// this is actually the sidechain but pre c1.1 songs save it as compressor
	else if (!strcmp(tagName, "compressor") || !strcmp(tagName, "sidechain")) { // Remember, Song doesn't use this
		// Set default values in case they are not configured
		const char* name = tagName;
		sidechain.syncType = SYNC_TYPE_EVEN;
		sidechain.syncLevel = SYNC_LEVEL_NONE;

		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				sidechain.attack = reader.readTagOrAttributeValueInt();
				reader.exitTag("attack");
			}
			else if (!strcmp(tagName, "release")) {
				sidechain.release = reader.readTagOrAttributeValueInt();
				reader.exitTag("release");
			}
			else if (!strcmp(tagName, "syncType")) {
				sidechain.syncType = (SyncType)reader.readTagOrAttributeValueInt();
				reader.exitTag("syncType");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				sidechain.syncLevel =
				    (SyncLevel)song->convertSyncLevelFromFileValueToInternalValue(reader.readTagOrAttributeValueInt());
				reader.exitTag("syncLevel");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag(name, true);
	}

	else if (!strcmp(tagName, "midiKnobs")) {
		reader.match('[');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (reader.match('{') && !strcmp(tagName, "midiKnob")) {

				MIDIDevice* device = nullptr;
				uint8_t channel;
				uint8_t ccNumber;
				bool relative;
				uint8_t p = params::GLOBAL_NONE;
				PatchSource s = PatchSource::NOT_AVAILABLE;
				PatchSource s2 = PatchSource::NOT_AVAILABLE;

				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "device")) {
						device = MIDIDeviceManager::readDeviceReferenceFromFile(reader);
					}
					else if (!strcmp(tagName, "channel")) {
						channel = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "ccNumber")) {
						ccNumber = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "relative")) {
						relative = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "controlsParam")) {
						// if the unpatched kind for the current mod controllable is sound then we also want to check
						// against patched params. Otherwise skip them to avoid a bug from patched volume params having
						// the same name in files as unpatched global volumes
						p = params::fileStringToParam(unpatchedParamKind_, reader.readTagOrAttributeValue(),
						                              unpatchedParamKind_
						                                  == deluge::modulation::params::Kind::UNPATCHED_SOUND);
					}
					else if (!strcmp(tagName, "patchAmountFromSource")) {
						s = stringToSource(reader.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSecondSource")) {
						s2 = stringToSource(reader.readTagOrAttributeValue());
					}
					reader.exitTag();
				}
				reader.match('}'); // close value object.
				reader.match('}'); // close box.

				if (p != params::GLOBAL_NONE && p != params::PLACEHOLDER_RANGE) {
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
			reader.exitTag();
		}
		reader.match(']'); // close array.
		reader.exitTag("midiKnobs");
	}

	else {
		return Error::RESULT_TAG_UNUSED;
	}

	return Error::NONE;
}

ModelStackWithAutoParam* ModControllableAudio::getParamFromMIDIKnob(MIDIKnob* knob,
                                                                    ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;

	int32_t paramId = knob->paramDescriptor.getJustTheParam() - params::UNPATCHED_START;

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(paramCollection, summary, paramId);

	ModelStackWithAutoParam* modelStackWithAutoParam = paramCollection->getAutoParamFromId(modelStackWithParamId);

	return modelStackWithAutoParam;
}

ModelStackWithThreeMainThings* ModControllableAudio::addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack,
                                                                             int32_t noteRowIndex) {
	NoteRow* noteRow = nullptr;
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
			    nullptr); // Could be NULL if a NonAudioInstrument - those don't back up any paramManagers (when they
			              // even have them).
		}
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addNoteRow(noteRowId, noteRow)->addOtherTwoThings(this, paramManager);
	return modelStackWithThreeMainThings;
}

bool ModControllableAudio::offerReceivedCCToLearnedParamsForClip(MIDIDevice* fromDevice, uint8_t channel,
                                                                 uint8_t ccNumber, uint8_t value,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int32_t noteRowIndex) {
	bool messageUsed = false;

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

			int32_t modPos = 0;
			int32_t modLength = 0;
			bool isStepEditing = false;

			if (modelStack->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStack->getTimelineCounter();

				// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
				// region.
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
					isStepEditing = true;
				}

				timelineCounter->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from
			// scratch now from that.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				int32_t newKnobPos;
				int32_t currentValue;

				// get current value
				if (isStepEditing) {
					currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
				}
				else {
					currentValue = modelStackWithParam->autoParam->getCurrentValue();
				}

				// convert current value to knobPos to compare to cc value being received
				int32_t knobPos =
				    modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);

				// calculate new knob position based on value received and deluge current value
				newKnobPos = MidiTakeover::calculateKnobPos(knobPos, value, knob, false, CC_NUMBER_NONE, isStepEditing);

				// is the cc being received for the same value as the current knob pos? If so, do nothing
				if (newKnobPos == knobPos) {
					continue;
				}

				// Convert the New Knob Position to a Parameter Value
				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				// Set the new Parameter Value for the MIDI Learned Parameter
				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);

				// if you're in automation view and editing the same parameter that was just updated
				// by a learned midi knob, then re-render the pads on the automation editor grid
				if (getRootUI() == &automationView && !automationView.onArrangerView) {
					Clip* clip = (Clip*)modelStack->getTimelineCounter();
					// check that the clip that the param is being edited for is the same as the
					// current clip as the current clip is what's actively displayed in automation view
					if (clip == getCurrentClip()) {
						// pass the current clip because you want to check that you're editing the param
						// for the same clip active in automation view
						int32_t id = modelStackWithParam->paramId;
						params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
						automationView.possiblyRefreshAutomationEditorGrid(clip, kind, id);
					}
				}
			}
		}
	}
	return messageUsed;
}

bool ModControllableAudio::offerReceivedCCToLearnedParamsForSong(
    MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
    ModelStackWithThreeMainThings* modelStackWithThreeMainThings) {
	bool messageUsed = false;

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

			int32_t modPos = 0;
			int32_t modLength = 0;
			bool isStepEditing = false;

			if (modelStackWithThreeMainThings->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStackWithThreeMainThings->getTimelineCounter();

				// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
				// region.
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
					isStepEditing = true;
				}
			}

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				int32_t newKnobPos;
				int32_t currentValue;

				// get current value
				if (isStepEditing) {
					currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
				}
				else {
					currentValue = modelStackWithParam->autoParam->getCurrentValue();
				}

				// convert current value to knobPos to compare to cc value being received
				int32_t knobPos =
				    modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);

				// calculate new knob position based on value received and deluge current value
				newKnobPos = MidiTakeover::calculateKnobPos(knobPos, value, knob, false, CC_NUMBER_NONE, isStepEditing);

				// is the cc being received for the same value as the current knob pos? If so, do nothing
				if (newKnobPos == knobPos) {
					continue;
				}

				// Convert the New Knob Position to a Parameter Value
				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				// Set the new Parameter Value for the MIDI Learned Parameter
				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);

				// check if you're currently editing the same learned param in automation view or
				// performance view if so, you will need to refresh the automation editor grid or the
				// performance view
				RootUI* rootUI = getRootUI();
				if (rootUI == &automationView || rootUI == &performanceSessionView) {
					int32_t id = modelStackWithParam->paramId;
					params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

					if (rootUI == &automationView) {
						automationView.possiblyRefreshAutomationEditorGrid(nullptr, kind, id);
					}
					else {
						performanceSessionView.possiblyRefreshPerformanceViewDisplay(kind, id, newKnobPos);
					}
				}
			}
		}
	}
	return messageUsed;
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

			// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
			// region.
			int32_t modPos = 0;
			int32_t modLength = 0;

			if (modelStack->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStack->getTimelineCounter();
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
				}

				timelineCounter->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from
			// scratch now from that.
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
	if (Error::NONE
	    == stutterer.beginStutter(
	        this, paramManager, runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::QuantizedStutterRate),
	        currentSong->getInputTickMagnitude(), playbackHandler.getTimePerInternalTickInverse())) {
		// Redraw the LEDs. Really only for quantized stutter, but doing it for unquantized won't hurt.
		view.notifyParamAutomationOccurred(paramManager);
		enterUIMode(UI_MODE_STUTTERING);
	}
}

void ModControllableAudio::processStutter(StereoSample* buffer, int32_t numSamples, ParamManager* paramManager) {
	if (stutterer.isStuttering(this)) {
		stutterer.processStutter(buffer, numSamples, paramManager, currentSong->getInputTickMagnitude(),
		                         playbackHandler.getTimePerInternalTickInverse());
	}
}

// paramManager is optional - if you don't send it, it won't restore the stutter rate and we won't redraw the LEDs
void ModControllableAudio::endStutter(ParamManagerForTimeline* paramManager) {
	stutterer.endStutter(paramManager);
	if (paramManager) {
		// Redraw the LEDs.
		view.notifyParamAutomationOccurred(paramManager);
	}
	exitUIMode(UI_MODE_STUTTERING);
}

void ModControllableAudio::switchDelayPingPong() {
	delay.pingPong = !delay.pingPong;
}

void ModControllableAudio::switchDelayAnalog() {
	delay.analog = !delay.analog;
}

char const* ModControllableAudio::getDelayTypeDisplayName() {
	switch (delay.analog) {
		using enum deluge::l10n::String;
	case 0:
		return l10n::get(STRING_FOR_DIGITAL_DELAY);
	default:
		return l10n::get(STRING_FOR_ANALOG_DELAY);
	}
}

void ModControllableAudio::switchDelaySyncType() {
	switch (delay.syncType) {
	case SYNC_TYPE_TRIPLET:
		delay.syncType = SYNC_TYPE_DOTTED;
		break;
	case SYNC_TYPE_DOTTED:
		delay.syncType = SYNC_TYPE_EVEN;
		break;

	default: // SYNC_TYPE_EVEN
		delay.syncType = SYNC_TYPE_TRIPLET;
		break;
	}
}

char const* ModControllableAudio::getDelaySyncTypeDisplayName() {
	switch (delay.syncType) {
	case SYNC_TYPE_TRIPLET:
		return "Triplet";
	case SYNC_TYPE_DOTTED:
		return "Dotted";
	default: // SYNC_TYPE_EVEN
		return "Even";
	}
}

void ModControllableAudio::switchDelaySyncLevel() {
	// Note: SYNC_LEVEL_NONE (value 0) can't be selected
	delay.syncLevel = (SyncLevel)((delay.syncLevel) % SyncLevel::SYNC_LEVEL_256TH + 1); // cycle from 1 to 9 (omit 0)
}

void ModControllableAudio::getDelaySyncLevelDisplayName(char* displayName) {
	// Note: SYNC_LEVEL_NONE (value 0) can't be selected
	delay.syncLevel = (SyncLevel)(delay.syncLevel % SyncLevel::SYNC_LEVEL_256TH); // cycle from 1 to 9 (omit 0)
	StringBuf buffer{shortStringBuffer, kShortStringBufferSize};
	currentSong->getNoteLengthName(buffer, (uint32_t)3 << (SYNC_LEVEL_256TH - delay.syncLevel));
	strncpy(displayName, buffer.data(), 29);
}

char const* ModControllableAudio::getFilterTypeDisplayName(FilterType currentFilterType) {
	using enum deluge::l10n::String;
	switch (currentFilterType) {
	case FilterType::LPF:
		return l10n::get(STRING_FOR_LPF);
	case FilterType::HPF:
		return l10n::get(STRING_FOR_HPF);
	case FilterType::EQ:
		return l10n::get(STRING_FOR_EQ);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

void ModControllableAudio::switchLPFMode() {
	lpfMode = static_cast<FilterMode>((util::to_underlying(lpfMode) + 1) % kNumLPFModes);
}

// for future use with FM
void ModControllableAudio::switchLPFModeWithOff() {
	lpfMode = static_cast<FilterMode>((util::to_underlying(lpfMode) + 1) % kNumLPFModes);
	switch (lpfMode) {
	case FilterMode::OFF:
		lpfMode = FilterMode::TRANSISTOR_12DB;
		break;
	case lastLpfMode:
		lpfMode = FilterMode::OFF;
		break;
	default:
		lpfMode = static_cast<FilterMode>((util::to_underlying(lpfMode) + 1));
	}
}

char const* ModControllableAudio::getFilterModeDisplayName(FilterType currentFilterType) {
	switch (currentFilterType) {
	case FilterType::LPF:
		return getLPFModeDisplayName();
	case FilterType::HPF:
		return getHPFModeDisplayName();
	default:
		return l10n::get(deluge::l10n::String::STRING_FOR_NONE);
	}
}

char const* ModControllableAudio::getLPFModeDisplayName() {

	using enum deluge::l10n::String;
	switch (lpfMode) {
	case FilterMode::TRANSISTOR_12DB:
		return l10n::get(STRING_FOR_12DB_LADDER);
	case FilterMode::TRANSISTOR_24DB:
		return l10n::get(STRING_FOR_24DB_LADDER);
	case FilterMode::TRANSISTOR_24DB_DRIVE:
		return l10n::get(STRING_FOR_DRIVE);
	case FilterMode::SVF_BAND:
		return l10n::get(STRING_FOR_SVF_BAND);
	case FilterMode::SVF_NOTCH:
		return l10n::get(STRING_FOR_SVF_NOTCH);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

void ModControllableAudio::switchHPFMode() {
	// this works fine, the offset to the first hpf doesn't matter with the modulus
	hpfMode = static_cast<FilterMode>((util::to_underlying(hpfMode) + 1) % kNumHPFModes + kFirstHPFMode);
}

// for future use with FM
void ModControllableAudio::switchHPFModeWithOff() {
	switch (hpfMode) {
	case FilterMode::OFF:
		hpfMode = firstHPFMode;
		break;
	default:
		hpfMode = static_cast<FilterMode>((util::to_underlying(hpfMode) + 1));
	}
}

char const* ModControllableAudio::getHPFModeDisplayName() {
	using enum deluge::l10n::String;
	switch (hpfMode) {
	case FilterMode::HPLADDER:
		return l10n::get(STRING_FOR_HPLADDER);
	case FilterMode::SVF_BAND:
		return l10n::get(STRING_FOR_SVF_BAND);
	case FilterMode::SVF_NOTCH:
		return l10n::get(STRING_FOR_SVF_NOTCH);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

// This can get called either for hibernation, or because drum now has no active noteRow
void ModControllableAudio::wontBeRenderedForAWhile() {
	delay.discardBuffers();
	endStutter(nullptr);
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
	// For us ModControllableAudios, this is really simple. Memory gets allocated in
	// GlobalEffectable::processFXForGlobalEffectable(). This function is overridden in Sound
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
				// overwroteExistingKnob = (midiKnobs[k].s != s || midiKnobs[k].p != p);
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

	// I've deactivated the unlearning of mod knobs, mainly because, if you want to unlearn a MIDI knob, you might not
	// want to also deactivate a mod knob to the same param at the same time

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

void ModControllableAudio::displayFilterSettings(bool on, FilterType currentFilterType) {
	if (display->haveOLED()) {
		if (on) {
			DEF_STACK_STRING_BUF(popupMsg, 40);
			popupMsg.append(getFilterTypeDisplayName(currentFilterType));
			if (currentFilterType != FilterType::EQ) {
				popupMsg.append("\n");
				popupMsg.append(getFilterModeDisplayName(currentFilterType));
			}
			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	else {
		if (on) {
			display->displayPopup(getFilterTypeDisplayName(currentFilterType));
		}
		else {
			display->displayPopup(getFilterModeDisplayName(currentFilterType));
		}
	}
}

void ModControllableAudio::displayDelaySettings(bool on) {
	if (display->haveOLED()) {
		if (on) {
			DEF_STACK_STRING_BUF(popupMsg, 100);
			if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
			    == RuntimeFeatureStateToggle::On) {
				popupMsg.append("Type: ");
				popupMsg.append(getDelaySyncTypeDisplayName());

				popupMsg.append("\nSync: ");
				char displayName[30];
				getDelaySyncLevelDisplayName(displayName);
				popupMsg.append(displayName);
			}
			else {
				popupMsg.append("Ping pong: ");
				popupMsg.append(getDelayPingPongStatusDisplayName());
				popupMsg.append("\n");
				popupMsg.append(getDelayTypeDisplayName());
			}

			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	else {
		if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
		    == RuntimeFeatureStateToggle::On) {
			if (on) {
				display->displayPopup(getDelaySyncTypeDisplayName());
			}
			else {
				char displayName[30];
				getDelaySyncLevelDisplayName(displayName);
				display->displayPopup(displayName);
			}
		}
		else {
			if (on) {
				display->displayPopup(getDelayPingPongStatusDisplayName());
			}
			else {
				display->displayPopup(getDelayTypeDisplayName());
			}
		}
	}
}

char const* ModControllableAudio::getDelayPingPongStatusDisplayName() {
	using enum deluge::l10n::String;
	switch (delay.pingPong) {
	case 0:
		return l10n::get(STRING_FOR_DISABLED);
	default:
		return l10n::get(STRING_FOR_ENABLED);
	}
}

void ModControllableAudio::displaySidechainAndReverbSettings(bool on) {
	// Sidechain
	if (display->haveOLED()) {
		if (on) {
			DEF_STACK_STRING_BUF(popupMsg, 100);
			// Sidechain
			popupMsg.append("Sidechain: ");
			popupMsg.append(getSidechainDisplayName());

			popupMsg.append("\n");

			// Reverb
			popupMsg.append(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));

			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	else {
		if (on) {
			display->displayPopup(getSidechainDisplayName());
		}
		else {
			display->displayPopup(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));
		}
	}
}

char const* ModControllableAudio::getSidechainDisplayName() {
	int32_t insideWorldTickMagnitude;
	if (currentSong) { // Bit of a hack just referring to currentSong in here...
		insideWorldTickMagnitude =
		    (currentSong->insideWorldTickMagnitude + currentSong->insideWorldTickMagnitudeOffsetFromBPM);
	}
	else {
		insideWorldTickMagnitude = FlashStorage::defaultMagnitude;
	}
	using enum deluge::l10n::String;
	if (sidechain.syncLevel == (SyncLevel)(7 - insideWorldTickMagnitude)) {
		return l10n::get(STRING_FOR_SLOW);
	}
	else {
		return l10n::get(STRING_FOR_FAST);
	}
}

/// displays names of parameters assigned to gold knobs
void ModControllableAudio::displayOtherModKnobSettings(uint8_t whichModButton, bool on) {
	// the code below handles displaying parameter names on OLED and 7SEG

	/* logic for OLED:
	- it will display parameter names when you are holding down the mod button
	- it will display the parameter name assigned to the top and bottom gold knob
	- e.g. Volume
	       Pan
	*/

	/* logic for 7SEG:
	- while holding down mod button: display parameter assigned to top gold knob
	- after mod button is released: display parameter assigned to bottom gold knob
	*/

	DEF_STACK_STRING_BUF(popupMsg, 100);
	// if we have an OLED display
	// or a 7SEG display and mod button is pressed
	// then we will display the top gold knob parameter
	if (display->haveOLED() || on) {
		char parameterName[30];
		view.getParameterNameFromModEncoder(1, parameterName);
		popupMsg.append(parameterName);
	}
	// in the song context,
	// the bottom knob for modButton 6 (stutter) doesn't have a parameter
	if (!((whichModButton == 6) && (!view.isClipContext()))) {
		// if we have an OLED display, we want to add a new line so we can
		// display the bottom gold knob param below the top gold knob param
		if (display->haveOLED()) {
			popupMsg.append("\n");
		}
		// if we have an OLED display
		// or a 7SEG display and mod button is released
		// then we will display the bottom gold knob parameter
		// on OLED bottom gold knob param is rendered below the top gold knob param
		if (display->haveOLED() || !on) {

			char parameterName[30];
			view.getParameterNameFromModEncoder(0, parameterName);
			popupMsg.append(parameterName);
		}
	}
	// if we have OLED, a pop up is shown while we are holding down mod button
	// pop-up is removed after we release the mod button
	if (display->haveOLED()) {
		if (on) {
			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	// if we have 7SEG, we display a temporary popup whenever mod button is pressed
	// and when it is released
	else {
		display->displayPopup(popupMsg.c_str());
	}
}
