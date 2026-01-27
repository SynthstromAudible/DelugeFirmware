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
#include "ModFXProcessor.h"
#include "definitions_cxx.hpp"
#include "deluge/dsp/granular/GranularProcessor.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "dsp/stereo_sample.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
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
#include "modulation/knob.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include <algorithm>

namespace params = deluge::modulation::params;

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

ModControllableAudio::ModControllableAudio() {

	// Grain

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
	delete grainFX;
}

void ModControllableAudio::cloneFrom(ModControllableAudio* other) {
	lpfMode = other->lpfMode;
	hpfMode = other->hpfMode;
	clippingAmount = other->clippingAmount;
	modFXType_ = other->modFXType_;
	bassFreq = other->bassFreq; // Eventually, these shouldn't be variables like this
	trebleFreq = other->trebleFreq;
	filterRoute = other->filterRoute;
	sidechain.cloneFrom(&other->sidechain);
	midi_knobs = other->midi_knobs; // Could fail if no RAM... not too big a concern
	delay = other->delay;
	stutterConfig = other->stutterConfig;
}

void ModControllableAudio::initParams(ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->params[params::UNPATCHED_BASS].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_TREBLE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_BASS_FREQ].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_TREBLE_FREQ].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[params::UNPATCHED_ARP_GATE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_NOTE_PROBABILITY].setCurrentValueBasicForSetup(2147483647);
	unpatchedParams->params[params::UNPATCHED_ARP_BASS_PROBABILITY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SWAP_PROBABILITY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_GLIDE_PROBABILITY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_REVERSE_PROBABILITY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_CHORD_PROBABILITY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_RATCHET_PROBABILITY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_RATCHET_AMOUNT].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SEQUENCE_LENGTH].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_CHORD_POLYPHONY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_RHYTHM].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_SPREAD_VELOCITY].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SPREAD_GATE].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SPREAD_OCTAVE].setCurrentValueBasicForSetup(-2147483648);

	Stutterer::initParams(paramManager);

	unpatchedParams->params[params::UNPATCHED_MOD_FX_OFFSET].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(0);

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

void ModControllableAudio::processFX(std::span<StereoSample> buffer, ModFXType modFXType, int32_t modFXRate,
                                     int32_t modFXDepth, const Delay::State& delayWorkingState, int32_t* postFXVolume,
                                     ParamManager* paramManager, bool anySoundComingIn, q31_t reverbSendAmount) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	// Mod FX -----------------------------------------------------------------------------------
	if (modFXType == ModFXType::GRAIN) {
		processGrainFX(buffer, modFXRate, modFXDepth, postFXVolume, unpatchedParams, anySoundComingIn,
		               reverbSendAmount);
	}
	else {
		modfx.processModFX(buffer, modFXType, modFXRate, modFXDepth, postFXVolume, unpatchedParams, anySoundComingIn);
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

		for (StereoSample& sample : buffer) {
			doEQ(thisDoBass, thisDoTreble, &sample.l, &sample.r, bassAmount, trebleAmount);
		}
	}

	// Delay ----------------------------------------------------------------------------------
	delay.process(buffer, delayWorkingState);
}
void ModControllableAudio::processGrainFX(std::span<StereoSample> buffer, int32_t modFXRate, int32_t modFXDepth,
                                          int32_t* postFXVolume, UnpatchedParamSet* unpatchedParams,
                                          bool anySoundComingIn, q31_t verbAmount) {
	// this shouldn't be possible but just in case
	if (anySoundComingIn && !grainFX) [[unlikely]] {
		enableGrain();
	}

	if (grainFX) {
		int32_t reverbSendAmountAndPostFXVolume = multiply_32x32_rshift32(*postFXVolume, verbAmount) << 5;
		grainFX->processGrainFX(buffer, modFXRate, modFXDepth,
		                        unpatchedParams->getValue(params::UNPATCHED_MOD_FX_OFFSET),
		                        unpatchedParams->getValue(params::UNPATCHED_MOD_FX_FEEDBACK), postFXVolume,
		                        anySoundComingIn, currentSong->calculateBPM(), reverbSendAmountAndPostFXVolume);
	}
}

void ModControllableAudio::processReverbSendAndVolume(std::span<StereoSample> buffer, int32_t* reverbBuffer,
                                                      int32_t postFXVolume, int32_t postReverbVolume,
                                                      int32_t reverbSendAmount, int32_t pan,
                                                      bool doAmplitudeIncrement) {

	int32_t reverbSendAmountAndPostFXVolume = multiply_32x32_rshift32(postFXVolume, reverbSendAmount) << 5;

	int32_t postFXAndReverbVolumeL, postFXAndReverbVolumeR, amplitudeIncrementL, amplitudeIncrementR;
	postFXAndReverbVolumeL = postFXAndReverbVolumeR = (multiply_32x32_rshift32(postReverbVolume, postFXVolume) << 5);

	// The amplitude increment applies to the post-FX volume. We want to have it just so that we can respond better to
	// sidechain volume ducking, which is done through post-FX volume.
	if (doAmplitudeIncrement) {
		auto postReverbSendVolumeIncrement =
		    (int32_t)((double)(postReverbVolume - postReverbVolumeLastTime) / (double)buffer.size());
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

	for (StereoSample& sample : buffer) {
		// Send to reverb
		if (reverbSendAmount != 0) {
			*(reverbBuffer++) += multiply_32x32_rshift32(sample.l + sample.r, reverbSendAmountAndPostFXVolume) << 1;
		}

		if (doAmplitudeIncrement) {
			postFXAndReverbVolumeL += amplitudeIncrementL;
			postFXAndReverbVolumeR += amplitudeIncrementR;
		}

		// Apply post-fx and post-reverb-send volume
		sample.l = multiply_32x32_rshift32(sample.l, postFXAndReverbVolumeL) << 5;
		sample.r = multiply_32x32_rshift32(sample.r, postFXAndReverbVolumeR) << 5;
	}

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

void ModControllableAudio::processSRRAndBitcrushing(std::span<StereoSample> buffer, int32_t* postFXVolume,
                                                    ParamManager* paramManager) {
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
			for (StereoSample& sample : buffer) {
				sample.l &= mask;
				sample.r &= mask;
			}
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

		for (StereoSample& sample : buffer) {
			// Convert down.
			// If time to "grab" another sample for down-conversion...
			if (lowSampleRatePos < 4194304) {
				int32_t strength2 = lowSampleRatePos;
				int32_t strength1 = 4194303 - strength2;

				lastGrabbedSample = grabbedSample; // What was current is now last
				grabbedSample.l = multiply_32x32_rshift32_rounded(lastSample.l, strength1 << 9)
				                  + multiply_32x32_rshift32_rounded(sample.l, strength2 << 9);
				grabbedSample.r = multiply_32x32_rshift32_rounded(lastSample.r, strength1 << 9)
				                  + multiply_32x32_rshift32_rounded(sample.r, strength2 << 9);
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
			lastSample = sample;

			// Convert up
			// Would only overshoot if we raised the sample rate during playback
			int32_t strength2 = std::min(highSampleRatePos, (uint32_t)4194303);
			int32_t strength1 = 4194303 - strength2;
			sample.l = (multiply_32x32_rshift32_rounded(lastGrabbedSample.l, strength1 << 9)
			            + multiply_32x32_rshift32_rounded(grabbedSample.l, strength2 << 9))
			           << 2;
			sample.r = (multiply_32x32_rshift32_rounded(lastGrabbedSample.r, strength1 << 9)
			            + multiply_32x32_rshift32_rounded(grabbedSample.r, strength2 << 9))
			           << 2;

			highSampleRatePos += highSampleRateIncrement;
		}
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
	writer.writeAttribute("modFXType", (char*)fxTypeToString(modFXType_));
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
	if (midi_knobs.size() > 0) {
		writer.writeArrayStart("midiKnobs");
		for (MIDIKnob& knob : midi_knobs) {
			writer.writeOpeningTagBeginning("midiKnob", true);
			knob.midiInput.writeAttributesToFile(
			    writer,
			    MIDI_MESSAGE_CC); // Writes channel and CC, but not device - we do that below.
			writer.writeAttribute("relative", knob.relative);
			writer.writeAttribute(
			    "controlsParam", params::paramNameForFile(unpatchedParamKind_, knob.paramDescriptor.getJustTheParam()));
			if (!knob.paramDescriptor.isJustAParam()) { // TODO: this only applies to Sounds
				writer.writeAttribute("patchAmountFromSource",
				                      sourceToString(knob.paramDescriptor.getTopLevelSource()));

				if (knob.paramDescriptor.hasSecondSource()) {
					writer.writeAttribute("patchAmountFromSecondSource",
					                      sourceToString(knob.paramDescriptor.getSecondSourceFromTop()));
				}
			}

			// Because we manually called LearnedMIDI::writeAttributesToFile() above, we have to give the MIDIDevice its
			// own tag, cos that can't be written as just an attribute.
			if (knob.midiInput.cable != nullptr) {
				writer.writeOpeningTagEnd();
				knob.midiInput.cable->writeReferenceToFile(writer);
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

	// Stutter
	writer.writeOpeningTagBeginning("stutter");
	writer.writeAttribute("quantized", stutterConfig.quantized);
	writer.writeAttribute("reverse", stutterConfig.reversed);
	writer.writeAttribute("pingPong", stutterConfig.pingPong);
	// Scatter mode settings (only write if non-default)
	if (stutterConfig.scatterMode != ScatterMode::Classic) {
		writer.writeAttribute("scatterMode", static_cast<int32_t>(stutterConfig.scatterMode));
	}
	if (stutterConfig.latch) {
		writer.writeAttribute("scatterLatch", 1);
	}
	if (stutterConfig.leakyWriteProb != 0.2f) {
		writer.writeAttribute("scatterPWrite", static_cast<int32_t>(stutterConfig.leakyWriteProb * 100.0f));
	}
	if (stutterConfig.pitchScale != 0) {
		writer.writeAttribute("scatterPitchScale", stutterConfig.pitchScale);
	}
	// Secret knob phase offsets (only write if non-zero)
	if (stutterConfig.zoneAPhaseOffset != 0) {
		writer.writeAttribute("scatterPhaseA", static_cast<int32_t>(stutterConfig.zoneAPhaseOffset * 10.0f));
	}
	if (stutterConfig.zoneBPhaseOffset != 0) {
		writer.writeAttribute("scatterPhaseB", static_cast<int32_t>(stutterConfig.zoneBPhaseOffset * 10.0f));
	}
	if (stutterConfig.macroConfigPhaseOffset != 0) {
		writer.writeAttribute("scatterPhaseMacro", static_cast<int32_t>(stutterConfig.macroConfigPhaseOffset * 10.0f));
	}
	if (stutterConfig.gammaPhase != 0) {
		writer.writeAttribute("scatterGamma", static_cast<int32_t>(stutterConfig.gammaPhase * 10.0f));
	}
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

	unpatchedParams->writeParamAsAttribute(writer, "arpeggiatorGate", params::UNPATCHED_ARP_GATE, writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "noteProbability", params::UNPATCHED_NOTE_PROBABILITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "bassProbability", params::UNPATCHED_ARP_BASS_PROBABILITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "swapProbability", params::UNPATCHED_ARP_SWAP_PROBABILITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "glideProbability", params::UNPATCHED_ARP_GLIDE_PROBABILITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "reverseProbability", params::UNPATCHED_REVERSE_PROBABILITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "chordProbability", params::UNPATCHED_ARP_CHORD_PROBABILITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "ratchetProbability", params::UNPATCHED_ARP_RATCHET_PROBABILITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "ratchetAmount", params::UNPATCHED_ARP_RATCHET_AMOUNT,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "sequenceLength", params::UNPATCHED_ARP_SEQUENCE_LENGTH,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "chordPolyphony", params::UNPATCHED_ARP_CHORD_POLYPHONY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "rhythm", params::UNPATCHED_ARP_RHYTHM, writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "spreadVelocity", params::UNPATCHED_SPREAD_VELOCITY,
	                                       writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "spreadGate", params::UNPATCHED_ARP_SPREAD_GATE, writeAutomation);
	unpatchedParams->writeParamAsAttribute(writer, "spreadOctave", params::UNPATCHED_ARP_SPREAD_OCTAVE,
	                                       writeAutomation);
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

	// Arpeggiator stuff

	else if (!strcmp(tagName, "arpeggiatorGate")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_GATE, readAutomationUpToPos);
		reader.exitTag("arpeggiatorGate");
	}

	else if (!strcmp(tagName, "ratchetAmount")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RATCHET_AMOUNT,
		                           readAutomationUpToPos);
		reader.exitTag("ratchetAmount");
	}

	else if (!strcmp(tagName, "ratchetProbability")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RATCHET_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("ratchetProbability");
	}

	else if (!strcmp(tagName, "chordPolyphony")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_CHORD_POLYPHONY,
		                           readAutomationUpToPos);
		reader.exitTag("chordPolyphony");
	}

	else if (!strcmp(tagName, "chordProbability")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_CHORD_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("chordProbability");
	}

	else if (!strcmp(tagName, "reverseProbability")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_REVERSE_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("reverseProbability");
	}

	else if (!strcmp(tagName, "bassProbability")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_BASS_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("bassProbability");
	}

	else if (!strcmp(tagName, "swapProbability")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_SWAP_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("swapProbability");
	}

	else if (!strcmp(tagName, "glideProbability")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_GLIDE_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("glideProbability");
	}

	else if (!strcmp(tagName, "noteProbability")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_NOTE_PROBABILITY,
		                           readAutomationUpToPos);
		reader.exitTag("noteProbability");
	}

	else if (!strcmp(tagName, "sequenceLength")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_SEQUENCE_LENGTH,
		                           readAutomationUpToPos);
		reader.exitTag("sequenceLength");
	}

	else if (!strcmp(tagName, "rhythm")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_RHYTHM, readAutomationUpToPos);
		reader.exitTag("rhythm");
	}

	else if (!strcmp(tagName, "spreadVelocity")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_SPREAD_VELOCITY,
		                           readAutomationUpToPos);
		reader.exitTag("spreadVelocity");
	}

	else if (!strcmp(tagName, "spreadGate")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_SPREAD_GATE,
		                           readAutomationUpToPos);
		reader.exitTag("spreadGate");
	}

	else if (!strcmp(tagName, "spreadOctave")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_ARP_SPREAD_OCTAVE,
		                           readAutomationUpToPos);
		reader.exitTag("spreadOctave");
	}

	else {
		return false;
	}

	return true;
}

// paramManager is optional
Error ModControllableAudio::readTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                            ArpeggiatorSettings* arpSettings, Song* song) {

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

	// Arpeggiator

	else if (!strcmp(tagName, "arpeggiator") && arpSettings != nullptr) {
		// Set default values in case they are not configured
		arpSettings->syncType = SYNC_TYPE_EVEN;
		arpSettings->syncLevel = SYNC_LEVEL_NONE;
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			bool readAndExited = arpSettings->readCommonTagsFromFile(reader, tagName, song);
			if (!readAndExited) {
				reader.exitTag(tagName);
			}
		}

		reader.exitTag("arpeggiator", true);
	}

	// Stutter

	else if (!strcmp(tagName, "stutter")) {
		// Set default values in case they are not configured
		stutterConfig.useSongStutter = true;
		stutterConfig.quantized = true;
		stutterConfig.reversed = false;
		stutterConfig.pingPong = false;
		stutterConfig.scatterMode = ScatterMode::Classic;
		stutterConfig.latch = false;
		stutterConfig.leakyWriteProb = 0.2f;
		stutterConfig.pitchScale = 0;
		stutterConfig.zoneAPhaseOffset = 0;
		stutterConfig.zoneBPhaseOffset = 0;
		stutterConfig.macroConfigPhaseOffset = 0;
		stutterConfig.gammaPhase = 0;
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "quantized")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				stutterConfig.quantized = static_cast<bool>(std::clamp(contents, 0_i32, 1_i32));
				reader.exitTag("quantized");
			}
			else if (!strcmp(tagName, "reverse")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				stutterConfig.reversed = static_cast<bool>(std::clamp(contents, 0_i32, 1_i32));
				reader.exitTag("reverse");
			}
			else if (!strcmp(tagName, "pingPong")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				stutterConfig.pingPong = static_cast<bool>(std::clamp(contents, 0_i32, 1_i32));
				reader.exitTag("pingPong");
			}
			else if (!strcmp(tagName, "scatterMode")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				stutterConfig.scatterMode = static_cast<ScatterMode>(std::clamp(contents, 0_i32, 7_i32));
				reader.exitTag("scatterMode");
			}
			else if (!strcmp(tagName, "scatterLatch")) {
				int32_t contents = reader.readTagOrAttributeValueInt();
				stutterConfig.latch = static_cast<bool>(std::clamp(contents, 0_i32, 1_i32));
				reader.exitTag("scatterLatch");
			}
			else if (!strcmp(tagName, "scatterPWrite")) {
				stutterConfig.leakyWriteProb = static_cast<float>(reader.readTagOrAttributeValueInt()) / 100.0f;
				reader.exitTag("scatterPWrite");
			}
			else if (!strcmp(tagName, "scatterPitchScale")) {
				stutterConfig.pitchScale =
				    static_cast<uint8_t>(std::clamp(reader.readTagOrAttributeValueInt(), 0_i32, 11_i32));
				reader.exitTag("scatterPitchScale");
			}
			else if (!strcmp(tagName, "scatterPhaseA")) {
				stutterConfig.zoneAPhaseOffset = static_cast<float>(reader.readTagOrAttributeValueInt()) / 10.0f;
				reader.exitTag("scatterPhaseA");
			}
			else if (!strcmp(tagName, "scatterPhaseB")) {
				stutterConfig.zoneBPhaseOffset = static_cast<float>(reader.readTagOrAttributeValueInt()) / 10.0f;
				reader.exitTag("scatterPhaseB");
			}
			else if (!strcmp(tagName, "scatterPhaseMacro")) {
				stutterConfig.macroConfigPhaseOffset = static_cast<float>(reader.readTagOrAttributeValueInt()) / 10.0f;
				reader.exitTag("scatterPhaseMacro");
			}
			else if (!strcmp(tagName, "scatterGamma")) {
				stutterConfig.gammaPhase = static_cast<float>(reader.readTagOrAttributeValueInt()) / 10.0f;
				reader.exitTag("scatterGamma");
			}
		}
		reader.exitTag("stutter", true);
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
		reader.exitTag("audioCompressor", true);
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

				MIDICable* cable = nullptr;
				uint8_t channel;
				uint8_t ccNumber;
				bool relative;
				uint8_t p = params::GLOBAL_NONE;
				PatchSource s = PatchSource::NOT_AVAILABLE;
				PatchSource s2 = PatchSource::NOT_AVAILABLE;

				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "device")) {
						cable = MIDIDeviceManager::readDeviceReferenceFromFile(reader);
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
					try {
						MIDIKnob& new_knob = midi_knobs.emplace_back();
						new_knob.midiInput.cable = cable;
						new_knob.midiInput.channelOrZone = channel;
						new_knob.midiInput.noteOrCC = ccNumber;
						new_knob.relative = relative;

						if (s == PatchSource::NOT_AVAILABLE) {
							new_knob.paramDescriptor.setToHaveParamOnly(p);
						}
						else if (s2 == PatchSource::NOT_AVAILABLE) {
							new_knob.paramDescriptor.setToHaveParamAndSource(p, s);
						}
						else {
							new_knob.paramDescriptor.setToHaveParamAndTwoSources(p, s, s2);
						}
					} catch (deluge::exception e) {
						if (e == deluge::exception::BAD_ALLOC) {
							// If we run out of memory, we just ignore the knob
							// TODO(@stellar-aria): This is bad practice, we should handle this better, but it
							//  currently follows the previous implementation
						}
						else {
							throw e;
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

ModelStackWithAutoParam* ModControllableAudio::getParamFromMIDIKnob(MIDIKnob& knob,
                                                                    ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;

	int32_t param_id = knob.paramDescriptor.getJustTheParam() - params::UNPATCHED_START;

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(paramCollection, summary, param_id);

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

bool ModControllableAudio::offerReceivedCCToLearnedParamsForClip(MIDICable& cable, uint8_t channel, uint8_t ccNumber,
                                                                 uint8_t value,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int32_t noteRowIndex) {
	bool messageUsed = false;

	// For each MIDI knob...
	for (MIDIKnob& knob : midi_knobs) {

		// If this is the knob...
		if (knob.midiInput.equalsNoteOrCC(&cable, channel, ccNumber)) {

			messageUsed = true;

			// See if this message is evidence that the knob is not "relative"
			if (value >= 16 && value < 112) {
				knob.relative = false;
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
				newKnobPos =
				    MidiTakeover::calculateKnobPos(knobPos, value, &knob, false, CC_NUMBER_NONE, isStepEditing);

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
    MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
    ModelStackWithThreeMainThings* modelStackWithThreeMainThings) {
	bool messageUsed = false;

	// For each MIDI knob...
	for (MIDIKnob& knob : midi_knobs) {

		// If this is the knob...
		if (knob.midiInput.equalsNoteOrCC(&cable, channel, ccNumber)) {

			messageUsed = true;

			// See if this message is evidence that the knob is not "relative"
			if (value >= 16 && value < 112) {
				knob.relative = false;
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
				newKnobPos =
				    MidiTakeover::calculateKnobPos(knobPos, value, &knob, false, CC_NUMBER_NONE, isStepEditing);

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
				if (rootUI == &automationView || rootUI == &performanceView) {
					int32_t id = modelStackWithParam->paramId;
					params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

					if (rootUI == &automationView) {
						automationView.possiblyRefreshAutomationEditorGrid(nullptr, kind, id);
					}
					else {
						performanceView.possiblyRefreshPerformanceViewDisplay(kind, id, newKnobPos);
					}
				}
			}
		}
	}
	return messageUsed;
}

// Returns true if the message was used by something
bool ModControllableAudio::offerReceivedPitchBendToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t data1,
                                                                 uint8_t data2,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int32_t noteRowIndex) {

	bool messageUsed = false;

	// For each MIDI knob...
	for (MIDIKnob& knob : midi_knobs) {

		// If this is the knob...
		if (knob.midiInput.equalsNoteOrCC(&cable, channel,
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

const uint32_t stutterUIModes[] = {UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, UI_MODE_HOLDING_ARRANGEMENT_ROW,
                                   UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION, UI_MODE_AUDITIONING, 0};

void ModControllableAudio::beginStutter(ParamManagerForTimeline* paramManager) {
	// TODO: Re-enable UI mode check after testing looper
	// Original check only allowed stutter when auditioning/holding clips
	// if (!isUIModeWithinRange(stutterUIModes)) {
	// 	return;
	// }
	// Get base config from song or local depending on useSongStutter
	StutterConfig config = stutterConfig.useSongStutter ? currentSong->globalEffectable.stutterConfig : stutterConfig;
	// Scatter mode is always per-sound (independent of useSongStutter)
	config.scatterMode = stutterConfig.scatterMode;
	// For scatter modes, also use local settings (scatter is per-sound feature)
	if (config.scatterMode != ScatterMode::Classic) {
		config.quantized = stutterConfig.quantized;
		config.latch = stutterConfig.latch;
		config.leakyWriteProb = stutterConfig.leakyWriteProb;
		config.pitchScale = stutterConfig.pitchScale;
		// Phase offsets are set via secret encoder menus on local config
		config.zoneAPhaseOffset = stutterConfig.zoneAPhaseOffset;
		config.zoneBPhaseOffset = stutterConfig.zoneBPhaseOffset;
		config.macroConfigPhaseOffset = stutterConfig.macroConfigPhaseOffset;
		config.gammaPhase = stutterConfig.gammaPhase;
	}

	int32_t magnitude = currentSong->getInputTickMagnitude();
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse();

	// Calculate loop length in samples for scatter modes (one bar, max 4 seconds)
	size_t loopLengthSamples = 0;
	bool halfBarMode = false;
	if (config.scatterMode != ScatterMode::Classic && config.scatterMode != ScatterMode::Burst
	    && playbackHandler.isEitherClockActive()) {
		uint64_t timePerTickBig = playbackHandler.getTimePerInternalTickBig();
		uint32_t barLengthInTicks = currentSong->getBarLength();
		loopLengthSamples = ((uint64_t)barLengthInTicks * timePerTickBig) >> 32;

		// If bar exceeds buffer (4 seconds), use 2 beats instead
		if (loopLengthSamples > Stutterer::kLooperBufferSize) {
			uint32_t halfBarInTicks = barLengthInTicks / 2;
			loopLengthSamples = ((uint64_t)halfBarInTicks * timePerTickBig) >> 32;
			halfBarMode = true;
		}
	}

	// For scatter modes with quantize, arm trigger to start on next beat
	// Only arm if we DON'T already own the stutter - if we do, fall through to beginStutter (trigger)
	// Repeat mode never uses quantization - it triggers immediately for responsive performance
	if (config.scatterMode != ScatterMode::Classic && config.scatterMode != ScatterMode::Repeat && config.quantized
	    && playbackHandler.isEitherClockActive() && !stutterer.ownsStutter(this)) {
		// Calculate next beat boundary (16th note = bar / 16)
		int64_t currentTick = playbackHandler.getCurrentInternalTickCount();
		uint32_t barLength = currentSong->getBarLength();
		uint32_t beatLength = barLength / 16; // 16th note resolution
		if (beatLength == 0) {
			beatLength = 1;
		}

		// Round up to next beat boundary
		int64_t nextBeat = ((currentTick / beatLength) + 1) * beatLength;

		if (Error::NONE
		    == stutterer.armStutter(this, paramManager, config, magnitude, timePerTickInverse, nextBeat,
		                            loopLengthSamples, halfBarMode)) {
			// Armed successfully, will start on beat
			view.notifyParamAutomationOccurred(paramManager);
			display->displayPopup("Armed");
		}
		return;
	}

	// Immediate trigger for Classic mode or when quantize is off
	if (Error::NONE
	    == stutterer.beginStutter(this, paramManager, config, magnitude, timePerTickInverse, loopLengthSamples,
	                              halfBarMode)) {
		// Redraw the LEDs. Really only for quantized stutter, but doing it for unquantized won't hurt.
		view.notifyParamAutomationOccurred(paramManager);
		// Classic stutter locks UI, scatter doesn't need UI mode
		if (config.scatterMode == ScatterMode::Classic) {
			enterUIMode(UI_MODE_STUTTERING);
		}
		// Show Armed notification for retrigger case (was in standby, now pending trigger)
		else if (stutterer.hasPendingTrigger(this)) {
			display->displayPopup("Armed");
		}
	}
}

void ModControllableAudio::processStutter(std::span<StereoSample> buffer, ParamManager* paramManager,
                                          const q31_t* modulatedScatterValues) {
	int32_t magnitude = currentSong->getInputTickMagnitude();
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse();
	// Use interpolated tick count for accurate beat boundary detection within audio buffers
	// (lastSwungTickActioned only updates at discrete tick events, causing up to 1 buffer latency)
	int64_t currentTick = playbackHandler.getCurrentInternalTickCount();
	uint32_t barLength = currentSong->getBarLength();
	uint32_t quarterNoteLength = barLength / 4; // Quarter note for responsive trigger sync
	if (quarterNoteLength == 0) {
		quarterNoteLength = 1;
	}

	// Check if armed trigger should fire
	if (stutterer.isArmed()) {
		stutterer.checkArmedTrigger(currentTick, paramManager, magnitude, timePerTickInverse);
	}

	// Check if pending play trigger should fire (quarter-note quantized)
	if (stutterer.hasPendingTrigger(this)) {
		stutterer.checkPendingTrigger(this, currentTick, quarterNoteLength, paramManager, magnitude,
		                              timePerTickInverse);
	}

	// Always record to standby buffer (during both STANDBY and PLAYING)
	// This captures clean input BEFORE scatter processing modifies the buffer
	// Enables instant re-trigger after playback ends (playing->armed->playing flow)
	stutterer.recordStandby(this, buffer, currentTick, quarterNoteLength);

	if (stutterer.isStuttering(this)) {
		// Update live params from current config (allows real-time adjustment while playing)
		if (stutterer.isScatterPlaying()) {
			stutterer.updateLiveParams(stutterConfig);
		}
		// Note: benchmarking is done inside processStutter() to separate classic vs scatter modes
		// Pass tick timing for bar boundary sync (locks slices to beat grid)
		uint64_t timePerTickBig = playbackHandler.getTimePerInternalTickBig();
		stutterer.processStutter(buffer, paramManager, magnitude, timePerTickInverse, currentTick, timePerTickBig,
		                         barLength, modulatedScatterValues);
	}
}

// paramManager is optional - if you don't send it, it won't restore the stutter rate and we won't redraw the LEDs
void ModControllableAudio::endStutter(ParamManagerForTimeline* paramManager) {
	// Check what role this source has in the current stutter session
	bool isPlayer = stutterer.isStuttering(this);
	bool isRecorder = stutterer.isArmedForTakeover(this);

	if (!isPlayer && !isRecorder) {
		return; // Not involved in current stutter
	}

	if (isRecorder && !isPlayer) {
		// We're recording for takeover but NOT playing yet.
		// DON'T cancel on encoder release - keep recording until we trigger.
		// This allows: press (arm) → release → press (trigger) → play
		return;
	}

	if (isPlayer) {
		// We're playing - end our playback
		// If someone else is recording for takeover, they lose their recording
		stutterer.endStutter(paramManager);
	}

	if (paramManager) {
		// Redraw the LEDs.
		view.notifyParamAutomationOccurred(paramManager);
	}
	// Exit classic stutter UI mode if active
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
	// Don't end latched scatter - it should keep playing when you switch tracks
	if (!(stutterer.isLatched() && stutterer.isStuttering(this))) {
		endStutter(nullptr);
	}
}

void ModControllableAudio::clearModFXMemory() {
	if (modFXType_ == ModFXType::GRAIN) {
		grainFX->clearGrainFXBuffer();
	}
	else if (modFXType_ != ModFXType::NONE) {
		modfx.resetMemory();
	}
}

bool ModControllableAudio::setModFXType(ModFXType newType) {
	// For us ModControllableAudios, this is really simple. Memory gets allocated in
	// GlobalEffectable::processFXForGlobalEffectable(). This function is overridden in Sound
	modFXType_ = newType;
	return true;
}

// whichKnob is either which physical mod knob, or which MIDI CC code.
// For mod knobs, supply midiChannel as 255
// Returns false if fail due to insufficient RAM.
bool ModControllableAudio::learnKnob(MIDICable* cable, ParamDescriptor paramDescriptor, uint8_t whichKnob,
                                     uint8_t modKnobMode, uint8_t midiChannel, Song* song) {
	// If a mod knob
	if (midiChannel >= 16) {
		return false;
	}

	// Was there a MIDI knob already set to control this thing?
	auto result = std::ranges::find_if(midi_knobs, [&](const MIDIKnob& knob) -> bool {
		return knob.midiInput.equalsNoteOrCC(cable, midiChannel, whichKnob) && paramDescriptor == knob.paramDescriptor;
	});

	try {
		MIDIKnob& knob = result != midi_knobs.end()
		                     ? *result                    // If it already exists, use that one
		                     : midi_knobs.emplace_back(); // If it doesn't exist, create a new one
		knob.midiInput.noteOrCC = whichKnob;
		knob.midiInput.channelOrZone = midiChannel;
		knob.midiInput.cable = cable;
		knob.paramDescriptor = paramDescriptor;
		knob.relative = (whichKnob != 128); // Guess that it's relative, unless this is a pitch-bend "knob"
		return true;

	} catch (...) {
		return false;
	}
}

// Returns whether anything was found to unlearn
bool ModControllableAudio::unlearnKnobs(ParamDescriptor paramDescriptor, Song* song) {
	// I've deactivated the unlearning of mod knobs, mainly because, if you want to unlearn a MIDI knob, you might not
	// want to also deactivate a mod knob to the same param at the same time
	size_t erased = std::erase_if(midi_knobs, [&](const MIDIKnob& knob) -> bool {
		return knob.paramDescriptor == paramDescriptor; //<
	});

	if (erased > 0) {
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
	}

	return erased > 0;
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
bool ModControllableAudio::enableGrain() {

	if (grainFX == nullptr) {
		void* grainMemory = GeneralMemoryAllocator::get().allocStealable(sizeof(GranularProcessor));
		if (grainMemory) {
			grainFX = new (grainMemory) GranularProcessor;
			return true;
		}
	}
	else {
		grainFX->clearGrainFXBuffer();
		return false;
	}
	return false;
}
void ModControllableAudio::disableGrain() {
	grainFX->startSkippingRendering();
}
