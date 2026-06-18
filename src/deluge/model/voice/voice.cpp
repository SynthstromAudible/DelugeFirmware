/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "model/voice/voice.h"
#include "arm_neon_shim.h"
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/interpolation/table_interpolation.h" // getDecay4
#include "dsp-next/fixed/osc/sine.h"
#include "dsp/dx/engine.h"
#include "dsp/filter/filter_set.h"
#include "dsp/timestretch/time_stretcher.h"
#include "dsp/util.hpp"
#include "gui/waveform/waveform_renderer.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "memory/stack_guard.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_cache.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/song/song.h"
#include "model/voice/source_component_render.h"
#include "model/voice/voice_sample.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/live/live_pitch_shifter.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "storage/wave_table/wave_table.h"

#include "dsp/oscillators/oscillator.h"
#include "util/functions.h" // getTanHAntialiased (output-stage saturation)
#include "util/misc.h"
#include <cstring>
#include <new>
#include <optional>

using namespace deluge;
using deluge::dsp::fixed::getDecay4;
namespace params = deluge::modulation::params;

PLACE_INTERNAL_FRUNK int32_t spareRenderingBuffer[4][SSI_TX_BUFFER_NUM_SAMPLES]
    __attribute__((aligned(CACHE_LINE_SIZE)));

using namespace deluge::voice;

namespace {
// Output-stage tanh clipping/saturation — the body of the former Sound::saturate + getShiftAmountForSaturation,
// relocated here as the voice render decouples from Sound (Phase 4). Driven by the snapshotted clippingAmount.
[[gnu::always_inline]] inline int32_t shiftForSaturation(uint8_t clippingAmount) {
	return (clippingAmount >= 2) ? (clippingAmount - 2) : 0;
}
[[gnu::always_inline]] inline void saturateSample(int32_t* data, uint32_t* workingValue, int32_t shiftAmount,
                                                  uint8_t clippingAmount) {
	*data = getTanHAntialiased(*data, workingValue, 5 + clippingAmount) << shiftAmount;
}
} // namespace

// Hopefully I could make this use the spareRenderingBuffer instead...

const Patcher::Config kPatcherConfigForVoice = {
    .firstParam = 0,
    .firstNonVolumeParam = params::FIRST_LOCAL_NON_VOLUME,
    .firstHybridParam = params::FIRST_LOCAL__HYBRID,
    .firstExpParam = params::FIRST_LOCAL_EXP,
    .endParams = params::FIRST_GLOBAL,
    .globality = GLOBALITY_LOCAL,
};

int32_t Voice::combineExpressionValues(int32_t synthLevelValue, int32_t expressionDimension) const {
	int32_t voiceLevelValue = this->localExpressionSourceValuesBeforeSmoothing[expressionDimension];
	int32_t combinedValue = (synthLevelValue >> 1) + (voiceLevelValue >> 1);
	return lshiftAndSaturate<1>(combinedValue);
}

Voice::Voice(Sound& sound)
    : patcher(kPatcherConfigForVoice, params.sourceValues, params.paramFinalValues), sound{sound} {
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it. You'll normally want to call audioDriver.voiceUnassigned() after this.
void Voice::setAsUnassigned(ModelStackWithSoundFlags* modelStack, bool deletingSong) {

	unassignStuff(deletingSong);
	delete_this_voice_ = true;
	if (!deletingSong) {
		this->sound.voiceUnassigned(modelStack);
	}
}

void Voice::unassignStuff(bool deletingSong) {
	for (int32_t s = 0; s < kNumSources; s++) {
		for (int32_t u = 0; u < this->sound.numUnison; u++) {
			unisonParts[u].sources[s].unassign(deletingSong);
		}
	}
}

uint32_t lastSoundOrder = 0;

// Returns false if fail and we need to unassign again
bool Voice::noteOn(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
                   int32_t newNoteCodeAfterArpeggiation, uint8_t velocity, uint32_t newSampleSyncLength,
                   int32_t ticksLate, uint32_t samplesLate, bool resetEnvelopes, int32_t newFromMIDIChannel,
                   const int16_t* mpeValues) {

	checkStack("Voice::noteOn");

	inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = newNoteCodeBeforeArpeggiation;
	inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = newFromMIDIChannel;
	noteCodeAfterArpeggiation = newNoteCodeAfterArpeggiation;
	orderSounded = lastSoundOrder++;
	overrideAmplitudeEnvelopeReleaseRate = 0;

	if (newNoteCodeAfterArpeggiation >= 128) {
		params.sourceValues[util::to_underlying(PatchSource::NOTE)] = 2147483647;
	}
	else if (newNoteCodeAfterArpeggiation <= 0) {
		params.sourceValues[util::to_underlying(PatchSource::NOTE)] = -2147483648;
	}
	else {
		params.sourceValues[util::to_underlying(PatchSource::NOTE)] =
		    ((int32_t)newNoteCodeAfterArpeggiation - 64) * 33554432;
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	// Setup "half-baked" envelope output values. These need to exist before we do the initial patching below - and it's
	// only after that that we can render the "actual" envelope output values, taking their own input patching into
	// account.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {

		// If no attack-stage...
		if (paramManager->getPatchedParamSet()->getValue(params::LOCAL_ENV_0_ATTACK + e) == -2147483648u) {
			envelopes[e].setOutputLevel(2147483647);
		}

		// Otherwise...
		else {
			envelopes[e].setOutputLevel(0);
		}
	}

	// Setup and render local LFO
	lfo2.setLocalInitialPhase(sound.lfoConfig[LFO2_ID]);
	params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)] = lfo2.render(0, sound.lfoConfig[LFO2_ID], 0);
	lfo4.setLocalInitialPhase(sound.lfoConfig[LFO4_ID]);
	params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)] = lfo4.render(0, sound.lfoConfig[LFO4_ID], 0);

	// Setup some sources which won't change for the duration of this note
	params.sourceValues[util::to_underlying(PatchSource::VELOCITY)] =
	    (velocity == 128) ? 2147483647 : ((int32_t)velocity - 64) * 33554432;

	// "Random" source
	params.sourceValues[util::to_underlying(PatchSource::RANDOM)] = getNoise();

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		localExpressionSourceValuesBeforeSmoothing[m] = mpeValues[m] << 16;
		params.sourceValues[util::to_underlying(PatchSource::X) + m] =
		    combineExpressionValues(sound.monophonicExpressionValues[m], m);
	}

	if (resetEnvelopes) {
		sourceAmplitudesLastTime.fill(0);
		modulatorAmplitudeLastTime.fill(0);
		overallOscAmplitudeLastTime = 0;
		doneFirstRender = false;

		filterSet.reset();

		lastSaturationTanHWorkingValue[0] = 2147483648;
		lastSaturationTanHWorkingValue[1] = 2147483648;
	}

	// Porta
	if (sound.polyphonic != PolyphonyMode::LEGATO
	    && paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_PORTAMENTO) != -2147483648
	    && sound.lastNoteCode != -2147483648) [[unlikely]] {
		setupPorta(sound);
	}

	else {
		portaEnvelopePos = 0xFFFFFFFF; // No porta
	}

	// Patch all sources to exclusive params, to give them an initial value. Exclusive params (params with just 1
	// source) aren't continuously recalculated, so they need that initial value. Remember, calculating that initial
	// value also takes into account the "preset value". This probably isn't strictly necessary for sources which we
	// know will be constantly changing, because that would make patching constantly calculate too. But that's only
	// really the envelopes, plus the LFOs (just the local one?) if they're not square
	for (int32_t s = 0; s < util::to_underlying(kFirstLocalSource); s++) {
		params.sourceValues[s] = sound.params.sourceValues[s];
	}
	patcher.performInitialPatching(sound.patchControlBlock(*paramManager));

	// Setup and render envelopes - again. Because they're local params (since mid-late 2017), we really need to render
	// them *after* initial patching is performed.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		int32_t attack = params.paramFinalValues[params::LOCAL_ENV_0_ATTACK + e];
		int32_t sustain = params.paramFinalValues[params::LOCAL_ENV_0_SUSTAIN + e];
		bool directlyToDecay = (attack > 245632); // Skip a (near-)instant attack straight to decay.
		params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e] =
		    envelopes[e].noteOn(directlyToDecay, sustain);
	}
	stampEnvelope0StageEntry();

	if (resetEnvelopes) {
		for (int32_t s = 0; s < kNumSources; s++) {
			sourceWaveIndexesLastTime[s] = params.paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s];
		}
	}

	// Make all VoiceUnisonPartSources "active" by default
	for (int32_t s = 0; s < kNumSources; s++) {

		// Various stuff in this block is only relevant for OscType::SAMPLE, but no real harm in it just happening in
		// other cases.
		guides[s].audioFileHolder = nullptr;

		bool sourceEverActive = modelStack->checkSourceEverActive(s);
		if (sourceEverActive) [[likely]] {
			guides[s].noteOffReceived = false;
			guides[s].sequenceSyncLengthTicks = 0; // That's the default - may get overwritten below

			if (sound.getSynthMode() != SynthMode::FM
			    && (sound.sources[s].oscType == OscType::SAMPLE || sound.sources[s].oscType == OscType::WAVETABLE)) {

				// Set up MultiRange
				MultiRange* range = sound.sources[s].getRange(noteCodeAfterArpeggiation + sound.transpose);
				if (!range) { // There could be no Range for a SAMPLE or WAVETABLE Source that just hasn't had a file
					          // loaded, like how OSC2 very often would be sitting
					goto gotInactive;
				}

				AudioFileHolder* holder = range->getAudioFileHolder();
				// Only actually set the Range as ours if it has an AudioFile - so that we'll always know that any
				// VoiceSource's range definitely has a sample
				if (!holder || !holder->audioFile) {
					goto gotInactive;
				}

				guides[s].audioFileHolder = holder;

				if (sound.sources[s].oscType == OscType::SAMPLE) {

					if (sound.sources[s].repeatMode == SampleRepeatMode::STRETCH) {
						guides[s].sequenceSyncLengthTicks = newSampleSyncLength;
						guides[s].sequenceSyncStartedAtTick =
						    playbackHandler.lastSwungTickActioned
						    - ticksLate; // No harm setting this even if it's not valid or needed.
						                 // And yes, this is supposed to use lastSwungTickActioned, not
						                 // getActualSwungTickCount(). ticksLate is relative to that.
					}
				}
			}
		}
		else {
gotInactive:
			if (sound.getSynthMode() == SynthMode::RINGMOD) {
				return false;
			}
			sourceEverActive = false;
		}

activenessDetermined:
		for (int32_t u = 0; u < sound.numUnison; u++) {
			unisonParts[u].sources[s].active = sourceEverActive;
		}
	}

	calculatePhaseIncrements(modelStack);

	for (int32_t s = 0; s < kNumSources; s++) {

		bool sourceEverActive = modelStack->checkSourceEverActive(s);

		if (!sourceEverActive) [[unlikely]] {
			continue;
		}

		Source* source = &sound.sources[s];

		// FM overrides osc type to always be sines
		OscType oscType;
		if (sound.getSynthMode() == SynthMode::FM) {
			oscType = OscType::SINE;
		}
		else {
			oscType = source->oscType;
		}

		// int32_t samplesLateHere = samplesLate; // Make our own copy of this - we're going to deactivate it if we're
		// in STRETCH mode, cos that works differently

		if (oscType == OscType::SAMPLE && guides[s].audioFileHolder) {
			source->sampleControls.invertReversed = sound.invertReversed; // Copy the temporary flag from the sound
			guides[s].setupPlaybackBounds(source->sampleControls.isCurrentlyReversed());

			// if (source->repeatMode == SampleRepeatMode::STRETCH) samplesLateHere = 0;
		}

		for (int32_t u = 0; u < sound.numUnison; u++) {

			// Check that we already marked this unison-part-source as active. Among other things, this ensures that if
			// the osc is set to SAMPLE, there actually is a sample loaded.
			if (unisonParts[u].sources[s].active) {
				bool success =
				    unisonParts[u].sources[s].noteOn(this, source, &guides[s], samplesLate, sound.oscRetriggerPhase[s],
				                                     resetEnvelopes, sound.synthMode, velocity);
				if (!success) [[unlikely]] {
					return false; // This shouldn't really ever happen I don't think really...
				}
			}
		}
	}

	if (sound.getSynthMode() == SynthMode::FM) {
		uint32_t initialPhase = getOscInitialPhaseForZero(OscType::SINE);
		for (int32_t u = 0; u < sound.numUnison; u++) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				if (sound.modulatorRetriggerPhase[m] != 0xFFFFFFFF) {
					unisonParts[u].modulatorPhase[m] = initialPhase + sound.modulatorRetriggerPhase[m];
				}
				if (resetEnvelopes) {
					unisonParts[u].modulatorFeedback[m] = 0;
				}
			}
		}
	}

	previouslyIgnoredNoteOff = false;
	expressionSourcesCurrentlySmoothing.reset();
	filterGainLastTime = 0;

	return true;
}

void Voice::expressionEventImmediate(const Sound& sound, int32_t voiceLevelValue, int32_t s) {
	int32_t expressionDimension = s - util::to_underlying(PatchSource::X);
	localExpressionSourceValuesBeforeSmoothing[expressionDimension] = voiceLevelValue;
	expressionSourcesFinalValueChanged[expressionDimension] = true;

	params.sourceValues[s] =
	    combineExpressionValues(sound.monophonicExpressionValues[expressionDimension], expressionDimension);
}

void Voice::expressionEventSmooth(int32_t newValue, int32_t s) {
	int32_t expressionDimension = s - util::to_underlying(PatchSource::X);
	localExpressionSourceValuesBeforeSmoothing[expressionDimension] = newValue;
	expressionSourcesCurrentlySmoothing[expressionDimension] = true;
}

void Voice::changeNoteCode(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
                           int32_t newNoteCodeAfterArpeggiation, int32_t newInputMIDIChannel,
                           const int16_t* newMPEValues) {
	inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = newNoteCodeBeforeArpeggiation;
	inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = newInputMIDIChannel;
	noteCodeAfterArpeggiation = newNoteCodeAfterArpeggiation;

	// We definitely want to go to these values smoothly. Probably wish it was even smoother... Actually nah this sounds
	// / feels great!
	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		localExpressionSourceValuesBeforeSmoothing[m] = newMPEValues[m] << 16;
		// TODO: what if there's just channel aftertouch, and it's still held down...
	}
	expressionSourcesCurrentlySmoothing = 0b111;

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	if (paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_PORTAMENTO) != -2147483648) {
		setupPorta(sound);
	}

	calculatePhaseIncrements(modelStack);
}

void Voice::setupPorta(const Sound& sound) {
	portaEnvelopePos = 0;
	int32_t semitoneAdjustment = sound.lastNoteCode - noteCodeAfterArpeggiation;

	int32_t noteWithinOctave = (semitoneAdjustment + 120) % 12;
	int32_t octave = (semitoneAdjustment + 120) / 12;

	int32_t phaseIncrement = noteIntervalTable[noteWithinOctave];

	int32_t shiftRightAmount = 16 - octave;
	if (shiftRightAmount >= 0) {
		phaseIncrement >>= shiftRightAmount;
	}
	else {
		phaseIncrement = 2147483647;
	}

	portaEnvelopeMaxAmplitude = phaseIncrement - kMaxSampleValue;
}

void Voice::randomizeOscPhases(const Sound& sound) {
	for (int32_t u = 0; u < sound.numUnison; u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			unisonParts[u].sources[s].oscPos = getNoise();
			// TODO: we should do sample play pos, too
		}
		if (sound.getSynthMode() == SynthMode::FM) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				unisonParts[u].modulatorPhase[m] = getNoise();
			}
		}
	}
}

// Can accept NULL paramManager
void Voice::calculatePhaseIncrements(ModelStackWithSoundFlags* modelStack) {

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	int32_t noteCodeWithMasterTranspose = noteCodeAfterArpeggiation + sound.transpose;

	for (int32_t s = 0; s < kNumSources; s++) {

		if (!modelStack->checkSourceEverActive(s)) { // Sets all unison parts inactive by default

makeInactive: // Frequency too high to render! (Higher than 22.05kHz)
			for (int32_t u = 0; u < sound.numUnison; u++) {
				unisonParts[u].sources[s].active = false;
			}
			continue;
		}

		Source* source = &sound.sources[s];

		int32_t oscillatorTranspose;
		if (source->oscType == OscType::SAMPLE && guides[s].audioFileHolder) { // Do not do this for WaveTables
			oscillatorTranspose = ((SampleHolderForVoice*)guides[s].audioFileHolder)->transpose;
		}
		else {
			oscillatorTranspose = source->transpose;
		}

		int32_t transposedNoteCode = noteCodeWithMasterTranspose + oscillatorTranspose;

		uint32_t phaseIncrement;

		// Sample-osc
		if (sound.getSynthMode() != SynthMode::FM
		    && (source->oscType == OscType::SAMPLE || source->oscType == OscType::INPUT_L
		        || source->oscType == OscType::INPUT_R || source->oscType == OscType::INPUT_STEREO)) {

			int32_t pitchAdjustNeutralValue;
			if (source->oscType == OscType::SAMPLE) {
				pitchAdjustNeutralValue = ((SampleHolder*)guides[s].audioFileHolder)->neutralPhaseIncrement;
			}
			else {
				pitchAdjustNeutralValue = kMaxSampleValue;
			}

			int32_t noteWithinOctave = (uint16_t)(transposedNoteCode + 240) % 12;
			int32_t octave = (uint16_t)(transposedNoteCode + 120) / 12;

			phaseIncrement = multiply_32x32_rshift32(noteIntervalTable[noteWithinOctave], pitchAdjustNeutralValue);

			int32_t shiftRightAmount = 13 - octave;

			// If shifting right...
			if (shiftRightAmount >= 0) {
				phaseIncrement >>= shiftRightAmount;
			}

			// If shifting left...
			else {
				int32_t shiftLeftAmount = 0 - shiftRightAmount;

				// If frequency would end up too high...
				// (which means one semitone below the limit, because osc-cent + unison could push it up a semitone)
				if (phaseIncrement >= (2026954652 >> shiftLeftAmount)) {
					goto makeInactive;
				}

				// Or if it's fine...
				else {
					phaseIncrement <<= (shiftLeftAmount);
				}
			}
		}

		// Regular wave osc
		else {
			int32_t noteWithinOctave = (uint16_t)(transposedNoteCode + 240 - 4) % 12;
			int32_t octave = (transposedNoteCode + 120 - 4) / 12;

			int32_t shiftRightAmount = 20 - octave;
			if (shiftRightAmount >= 0) {
				phaseIncrement = noteFrequencyTable[noteWithinOctave] >> shiftRightAmount;
			}

			else {
				goto makeInactive;
			}
		}

		// Cents
		if (source->oscType == OscType::SAMPLE) { // guides[s].sampleHolder
			phaseIncrement = ((SampleHolderForVoice*)guides[s].audioFileHolder)->fineTuner.detune(phaseIncrement);
		}
		else {
			phaseIncrement = source->fineTuner.detune(phaseIncrement);
		}

		// If only one unison
		if (sound.numUnison == 1) {
			unisonParts[0].sources[s].phaseIncrementStoredValue = phaseIncrement;
		}

		// Or if multiple unison
		else {
			for (int32_t u = 0; u < sound.numUnison; u++) {
				unisonParts[u].sources[s].phaseIncrementStoredValue = sound.unisonDetuners[u].detune(phaseIncrement);
			}
		}
	}

	// FM modulators
	if (sound.getSynthMode() == SynthMode::FM) {
		for (int32_t m = 0; m < kNumModulators; m++) {

			if (sound.getSmoothedPatchedParamValue(params::LOCAL_MODULATOR_0_VOLUME + m, *paramManager)
			    == -2147483648) {
				continue; // Only if modulator active
			}

			int32_t transposedNoteCode = noteCodeWithMasterTranspose + sound.modulatorTranspose[m];
			int32_t noteWithinOctave = (transposedNoteCode + 120 - 4) % 12;
			int32_t octave = (transposedNoteCode + 120 - 4) / 12;
			int32_t shiftRightAmount = 20 - octave;

			int32_t phaseIncrement;

			if (shiftRightAmount >= 0) {
				phaseIncrement = noteFrequencyTable[noteWithinOctave] >> shiftRightAmount;
			}

			else {
				// Frequency too high to render! (Higher than 22.05kHz)
				for (int32_t u = 0; u < sound.numUnison; u++) {
					unisonParts[u].modulatorPhaseIncrement[m] = 0xFFFFFFFF; // Means "inactive"
				}
				continue;
			}

			// Cents
			phaseIncrement = sound.modulatorTransposers[m].detune(phaseIncrement);

			// If only one unison
			if (sound.numUnison == 1) {
				unisonParts[0].modulatorPhaseIncrement[m] = phaseIncrement;
			}

			// Or if multiple unison
			else {
				for (int32_t u = 0; u < sound.numUnison; u++) {
					unisonParts[u].modulatorPhaseIncrement[m] = sound.unisonDetuners[u].detune(phaseIncrement);
				}
			}
		}
	}
}

void Voice::noteOff(ParamManagerForTimeline* paramManager, bool noteTailsAllowed, bool allowReleaseStage) {

	for (int32_t s = 0; s < kNumSources; s++) {
		guides[s].noteOffReceived = true;
	}

	Sound& sound = this->sound;

	// Only do it if note-offs are meant to be processed for this sound. Otherwise ignore it.
	if (noteTailsAllowed) {

		// If no release-stage, we'll stop as soon as we can
		if (!allowReleaseStage || !hasReleaseStage()) {
			envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE);
		}

		// Or, do the release-stage
		else {
			envelopes[0].noteOff(sound.envelopeHasSustainCurrently(0, paramManager));

			// Only start releasing envelope 2 if release wasn't at max value. LOCAL_ENV_1_RELEASE is a
			// *local* param, so read the voice's own params.paramFinalValues (sized LOCAL_LAST) — not
			// sound.paramFinalValues, which holds *global* params only (indexing it here was an
			// out-of-bounds read of adjacent Sound memory; the hardened host build's bounds check caught it).
			if (params.paramFinalValues[params::LOCAL_ENV_1_RELEASE] >= 9) {
				envelopes[1].noteOff(sound.envelopeHasSustainCurrently(1, paramManager));
			}
		}
		stampEnvelope0StageEntry();
	}

	else {
		previouslyIgnoredNoteOff = true;
	}

	if (sound.synthMode != SynthMode::FM) {
		for (int32_t s = 0; s < kNumSources; s++) {
			if (sound.sources[s].oscType == OscType::SAMPLE && guides[s].loopEndPlaybackAtByte) {
				for (int32_t u = 0; u < sound.numUnison; u++) {
					if (unisonParts[u].sources[s].active) {

						bool success = unisonParts[u].sources[s].sampleSource.noteOffAtLoopEnd(this, &guides[s]);

						if (!success) {
							unisonParts[u].sources[s].unassign(false);
						}
					}
				}
			}
			else if (sound.sources[s].oscType == OscType::DX7) {
				for (int u = 0; u < sound.numUnison; u++) {
					unisonParts[u].sources[s].dx7Source.keyup();
				}
			}
		}
	}

	for (int32_t s = 0; s < kNumSources; s++) {
		Source* source = &sound.sources[s];
		if (source->oscType == OscType::SAMPLE) {
			source->sampleControls.invertReversed = false; // Reset temporary flag back to normal
		}
	}
}

// Returns false if voice needs unassigning now
bool Voice::sampleZoneChanged(ModelStackWithSoundFlags* modelStack, int32_t s, MarkerType markerType) {

	AudioFileHolder* holder = guides[s].audioFileHolder;
	if (!holder) {
		return true; // If no holder, that means this Source/Sample is not currently playing, e.g. because its volume
		             // was set to 0.
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	const Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	const Source& source = sound.sources[s];
	Sample* sample = (Sample*)holder->audioFile;

	guides[s].setupPlaybackBounds(source.sampleControls.isCurrentlyReversed());

	LoopType loopingType = guides[s].getLoopingType(sound.sources[s]);

	// Check we're still within bounds - for each unison part.
	// Well, that is, make sure we're not past the new end. Being before the start is ok, because we'll come back into
	// the still-remaining part soon enough.

	bool anyStillActive = false;

	for (int32_t u = 0; u < sound.numUnison; u++) {
		VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];

		if (voiceUnisonPartSource->active) {
			bool stillActive = voiceUnisonPartSource->sampleSource.zoneChanged(
			    &guides[s], sample, source.sampleControls.isCurrentlyReversed(), markerType, loopingType,
			    getPriorityRating());
			if (!stillActive) {
				D_PRINTLN("returned false ---------");
				voiceUnisonPartSource->unassign(false);
			}
			else {
				anyStillActive = true;
			}
		}
	}

	// If none of this source still active, and no noise, see if the other source still has any...
	if (!anyStillActive
	    && !paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(-2147483648)) {

		s = 1 - s;

		if (!modelStack->checkSourceEverActive(s)) {
			return false;
		}
		else {
			for (int32_t u = 0; u < sound.numUnison; u++) {
				VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];
				if (voiceUnisonPartSource->active) {
					return true;
				}
			}
		}
	}

	return true;
}

uint32_t Voice::getLocalLFOPhaseIncrement(const LFOConfig& config, deluge::modulation::params::Local param,
                                          uint32_t syncedPhaseIncrement) {
	if (config.syncLevel == SYNC_LEVEL_NONE) {
		return params.paramFinalValues[param];
	}
	return syncedPhaseIncrement;
}

// Before calling this, you must set the filterSetConfig's doLPF and doHPF to default values

// Returns false if became inactive and needs unassigning
[[gnu::hot]] bool Voice::render(ParamManagerForTimeline* paramManager, const ControlBlock& controlBlock,
                                int32_t* soundBuffer, int32_t numSamples, bool soundRenderingInStereo,
                                bool applyingPanAtVoiceLevel, uint32_t sourcesChanged, bool doLPF, bool doHPF,
                                int32_t externalPitchAdjust, const VoiceConfig& cfg) {
	// we spread out over a render cycle - allocating and starting the voice takes more time than rendering it so this
	// avoids the cpu spike at note on
	if (justCreated == false) {
		justCreated = true;
		return true;
	}
	checkStack("Voice::render");

	bool didStereoTempBuffer = false;

	// If we've previously ignored a note-off, we need to check that the user hasn't changed the preset so that we're
	// now waiting for a note-off again
	if (previouslyIgnoredNoteOff && cfg.noteTailsAllowed) {
		noteOff(paramManager, cfg.noteTailsAllowed);
	}

	// Do envelopes - if they're patched to something (always do the first one though)
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		if (e == 0 || (cfg.localSourcesPatchedToAnything & (1 << (util::to_underlying(PatchSource::ENVELOPE_0) + e)))) {
			int32_t old = params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e];
			int32_t release = params.paramFinalValues[params::LOCAL_ENV_0_RELEASE + e];
			if (e == 0 && overrideAmplitudeEnvelopeReleaseRate) {
				release = overrideAmplitudeEnvelopeReleaseRate;
			}
			params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e] = envelopes[e].render(
			    numSamples, params.paramFinalValues[params::LOCAL_ENV_0_ATTACK + e],
			    params.paramFinalValues[params::LOCAL_ENV_0_DECAY + e],
			    params.paramFinalValues[params::LOCAL_ENV_0_SUSTAIN + e], release, decayTableSmall8);
			uint32_t anyChange = (old != params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e]);
			sourcesChanged |= anyChange << (util::to_underlying(PatchSource::ENVELOPE_0) + e);
		}
	}
	stampEnvelope0StageEntry();

	bool unassignVoiceAfter = (envelopes[0].stage() == Envelope::Stage::OFF)
	                          || (envelopes[0].stage() > Envelope::Stage::DECAY
	                              && params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0)]
	                                     == std::numeric_limits<int32_t>::min());
	// Local LFO
	if (cfg.localSourcesPatchedToAnything & (1 << util::to_underlying(PatchSource::LFO_LOCAL_1))) {
		int32_t old = params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)] = lfo2.render(
		    numSamples, cfg.lfo2Config,
		    getLocalLFOPhaseIncrement(cfg.lfo2Config, params::LOCAL_LFO_LOCAL_FREQ_1, cfg.lfo2SyncedPhaseIncrement));
		uint32_t anyChange = (old != params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)]);
		sourcesChanged |= anyChange << util::to_underlying(PatchSource::LFO_LOCAL_1);
	}
	if (cfg.localSourcesPatchedToAnything & (1 << util::to_underlying(PatchSource::LFO_LOCAL_2))) {
		int32_t old = params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)] = lfo4.render(
		    numSamples, cfg.lfo4Config,
		    getLocalLFOPhaseIncrement(cfg.lfo4Config, params::LOCAL_LFO_LOCAL_FREQ_2, cfg.lfo4SyncedPhaseIncrement));
		uint32_t anyChange = (old != params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)]);
		sourcesChanged |= anyChange << util::to_underlying(PatchSource::LFO_LOCAL_2);
	}

	// MPE params

	expressionSourcesCurrentlySmoothing |= cfg.expressionSourcesChangedAtSynthLevel;

	if (expressionSourcesCurrentlySmoothing.any()) {
		expressionSourcesFinalValueChanged |= expressionSourcesCurrentlySmoothing;

		for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
			if (expressionSourcesCurrentlySmoothing[i]) {

				int32_t targetValue = combineExpressionValues(cfg.monophonicExpressionValues[i], i);

				int32_t diff = (targetValue >> 8) - (params.sourceValues[i + util::to_underlying(PatchSource::X)] >> 8);

				if (diff == 0) {
					expressionSourcesCurrentlySmoothing[i] = false;
				}
				else {
					int32_t amountToAdd = diff * numSamples / 4;
					params.sourceValues[i + util::to_underlying(PatchSource::X)] += amountToAdd;
				}
			}
		}
	}

	sourcesChanged |= expressionSourcesFinalValueChanged.to_ulong() << util::to_underlying(PatchSource::X);

	expressionSourcesFinalValueChanged.reset();

	// Patch all the sources to their parameters
	if (sourcesChanged) {
		for (int32_t s = 0; s < util::to_underlying(kFirstLocalSource); s++) {
			params.sourceValues[s] = cfg.globalSourceValues[s];
		}
		patcher.performPatching(sourcesChanged, controlBlock);
	}

	// Sort out pitch
	int32_t overallPitchAdjust = params.paramFinalValues[params::LOCAL_PITCH_ADJUST];

	// Pitch adjust from "external" - e.g. the Kit
	if (externalPitchAdjust != kMaxSampleValue) {
		int32_t output = multiply_32x32_rshift32_rounded(overallPitchAdjust, externalPitchAdjust);
		if (output > 8388607) {
			output = 8388607; // Limit it a bit. Not really quite sure if necessary?
		}
		overallPitchAdjust = output << 8;
	}

	// Pitch adjust via MIDI pitch bend and MPE
	int32_t totalBendAmount = // Comes out as a 32-bit number representing a range of +-192 semitones
	    (cfg.monophonicExpressionValues[0] / 192) * cfg.bendRanges[BEND_RANGE_MAIN]
	    + (localExpressionSourceValuesBeforeSmoothing[0] / 192) * cfg.bendRanges[BEND_RANGE_FINGER_LEVEL];

	overallPitchAdjust = getExp(overallPitchAdjust, totalBendAmount >> 1);

	// Porta
	if (portaEnvelopePos < 8388608) {
		int32_t envValue = getDecay4(portaEnvelopePos, 23);
		int32_t pitchAdjustmentHere =
		    kMaxSampleValue + (multiply_32x32_rshift32_rounded(envValue, portaEnvelopeMaxAmplitude) << 1);

		int32_t a = multiply_32x32_rshift32_rounded(overallPitchAdjust, pitchAdjustmentHere);
		if (a > 8388607) {
			a = 8388607; // Prevent overflow! Happened to Matt Bates
		}
		overallPitchAdjust = a << 8;

		// Move envelope on. Using the "release rate" lookup table gives by far the best range of speed values
		int32_t envelopeSpeed = lookupReleaseRate(cableToExpParamShortcut(cfg.portamento)) >> 13;
		portaEnvelopePos += envelopeSpeed * numSamples;
	}

	// Decide whether to do an auto-release for sample. Despite this being envelope-related, meaning we'd ideally prefer
	// to do it before patching, we can only do it after because we need to know pitch

	// If not already releasing and some release is set, and no noise-source...
	//@todo - cache this, it never changes
	if (cfg.synthMode != SynthMode::FM && envelopes[0].stage() < Envelope::Stage::RELEASE && hasReleaseStage()
	    && !cfg.noiseVolumeContainsSomething) {

		uint32_t whichSourcesNeedAttention = 0;

		// We only want to do this if all active sources are play-once samples
		// For each source...
		for (int32_t s = 0; s < kNumSources; s++) {
			// If this source isn't enabled, skip it
			if (!cfg.sourceEverActive[s]) {
				continue;
			}

			// If it's not a sample, or it's not a play-once, or it has a loop-end point but we haven't received the
			// note-off, then we don't want the auto-release feature for it
			if (cfg.oscType[s] != OscType::SAMPLE
			    || cfg.repeatMode[s]
			           != SampleRepeatMode::ONCE // Don't do it for anything else. STRETCH is too hard to calculate
			    || !guides[s].audioFileHolder
			    || (((SampleHolderForVoice*)guides[s].audioFileHolder)->loopEndPos && !guides[s].noteOffReceived)) {
				goto skipAutoRelease;
			}

			whichSourcesNeedAttention |= (1 << s);
		}

		// If either / both sources need attention...
		// only happens for auto release of play once samples - we probably don't get here
		if (whichSourcesNeedAttention) [[unlikely]] {

			int32_t releaseStageLengthSamples =
			    (uint32_t)8388608 / (uint32_t)params.paramFinalValues[params::LOCAL_ENV_0_RELEASE];

			int32_t highestNumSamplesLeft = 0;

			// For each source...
			for (int32_t s = 0; s < kNumSources; s++) {

				// If it needed attention...
				if (whichSourcesNeedAttention & (1 << s)) {

					// This Source needs an auto release applied. Calculate for the last unison, because that'll have
					// the higher pitch, so will be ending soonest
					VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[cfg.numUnison - 1].sources[s];
					if (!voiceUnisonPartSource->active) {
						continue;
					}

					Sample* sample = (Sample*)guides[s].audioFileHolder->audioFile;
					int32_t bytePos = voiceUnisonPartSource->sampleSource.getPlayByteLowLevel(sample, &guides[s]);

					int32_t bytesLeft =
					    (int32_t)((uint32_t)guides[s].endPlaybackAtByte - (uint32_t)bytePos) * guides[s].playDirection;

					int32_t bytesPerSample = sample->byteDepth * sample->numChannels;

					int32_t releaseStageLengthBytes = releaseStageLengthSamples * bytesPerSample;

					// Work out the actual sample read rate, from the "native" read rate for the last unison, combined
					// with the "pitch adjust" amount, and the pitch adjust for this source alone. If the pitch goes
					// crazy-high, this will fall through and prevent auto-release from happening
					uint32_t actualSampleReadRate = voiceUnisonPartSource->phaseIncrementStoredValue;
					if (!adjustPitch(actualSampleReadRate, overallPitchAdjust)) {
						continue;
					}
					if (!adjustPitch(actualSampleReadRate,
					                 params.paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
						continue;
					}
					// TODO: actualSampleReadRate should probably be affected by time stretching, too. BUT that'd stuff
					// up some existing users' songs - e.g. Michael B's one I tried during V3.0 beta phase, July 2019

					// Scale that according to our resampling rate
					if (actualSampleReadRate != kMaxSampleValue) {
						releaseStageLengthBytes = ((int64_t)releaseStageLengthBytes * actualSampleReadRate) >> 24;
					}

					// If this sample says it's not time to do auto-release yet, then we don't want to do it yet, so get
					// out
					if (bytesLeft >= releaseStageLengthBytes) {
						goto skipAutoRelease;
					}

					// And also see how many audio samples were left for this source. Only do this in here because it
					// involves time-consuming division
					int32_t samplesLeft = bytesLeft / (bytesPerSample);

					// Scale that according to our resampling rate
					if (actualSampleReadRate != kMaxSampleValue) {
						samplesLeft = (((int64_t)samplesLeft << 24) / actualSampleReadRate);
					}

					highestNumSamplesLeft = std::max(highestNumSamplesLeft, samplesLeft);
				}
			}

			// If we're still here, then *all* sources which needed attention say yes do it now, so do it
			// Just do this for the amplitude envelope
			overrideAmplitudeEnvelopeReleaseRate = 8388608 / highestNumSamplesLeft;
			if (envelopes[0].isAtAttackStart()) {
				envelopes[0].setOutputLevel(2147483647);
			}
			envelopes[0].unconditionalRelease();
			stampEnvelope0StageEntry();
		}
	}
skipAutoRelease: {}

	if (!doneFirstRender && params.paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			modulatorAmplitudeLastTime[m] = params.paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m];
		}
	}

	// Apply envelope 0 to volume. This takes effect as a cut only; when the envelope is at max height, volume is
	// unaffected. Important that we use lshiftAndSaturate here - otherwise, number can overflow if combining high
	// velocity patching with big LFO
	int32_t overallOscAmplitude = lshiftAndSaturate<2>(
	    multiply_32x32_rshift32(params.paramFinalValues[params::LOCAL_VOLUME],
	                            (params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0)] >> 1) + 1073741824));

	// This is the gain which gets applied to compensate for any change in gain that the filter is going to cause
	int32_t filterGain;

	// Prepare the filters
	// Checking if filters should run now happens within the filterset
	FilterMode lpfMode = doLPF ? cfg.lpfMode : FilterMode::OFF;
	FilterMode hpfMode = doHPF ? cfg.hpfMode : FilterMode::OFF;
	filterGain = filterSet.setConfig(
	    params.paramFinalValues[params::LOCAL_LPF_FREQ], params.paramFinalValues[params::LOCAL_LPF_RESONANCE], lpfMode,
	    params.paramFinalValues[params::LOCAL_LPF_MORPH], params.paramFinalValues[params::LOCAL_HPF_FREQ],
	    (params
	         .paramFinalValues[params::LOCAL_HPF_RESONANCE]), // >> StorageManager::devVarA) << StorageManager::devVarA,
	    hpfMode, params.paramFinalValues[params::LOCAL_HPF_MORPH], cfg.volumeNeutralValueForUnison << 1,
	    cfg.filterRoute, false, nullptr); // Level adjustment for unison now happens *before* the filter!

	SynthMode synthMode = cfg.synthMode;

	int32_t sourceAmplitudes[kNumSources];
	int32_t sourceAmplitudeIncrements[kNumSources];

	bool modulatorsActive[kNumModulators];
	int32_t modulatorAmplitudeIncrements[kNumModulators];

	int32_t overallOscillatorAmplitudeIncrement;

	// If not ringmod, then sources need their volume calculated
	if (synthMode != SynthMode::RINGMOD) [[likely]] {

		// params::LOCAL_OSC_x_VOLUME can normally only be up to a quarter of full range, but patching can make it up to
		// full-range overallOscAmplitude (same range as) params::LOCAL_VOLUME, is the same.

		// Let's impose a new limit, that only a total of 4x amplification via patching is possible (not 16x).
		// Chances are, the user won't even need that much, let alone would have the osc volume *and* the synth master
		// volume on full We then have space to make each osc's amplitude 4x what it could have been otherwise

		// If FM, we work the overall amplitude into each oscillator's, to avoid having to do an extra multiplication
		// for every audio sample at the end
		if (synthMode == SynthMode::FM) {

			// Apply compensation for unison
			overallOscAmplitude = multiply_32x32_rshift32_rounded(overallOscAmplitude, cfg.volumeNeutralValueForUnison)
			                      << 3;

			int32_t a =
			    multiply_32x32_rshift32(params.paramFinalValues[params::LOCAL_OSC_A_VOLUME], overallOscAmplitude);
			int32_t b =
			    multiply_32x32_rshift32(params.paramFinalValues[params::LOCAL_OSC_B_VOLUME], overallOscAmplitude);

			// Clip off those amplitudes before they get too high. I think these were originally intended to stop the
			// amplitude rising to more than "4", whatever that meant?
			sourceAmplitudes[0] = std::min(a, (int32_t)134217727);
			sourceAmplitudes[1] = std::min(b, (int32_t)134217727);
		}

		// Or if subtractive, we don't do that, because we do want to apply the overall amplitude *after* the filter
		else {
			if (cfg.hasFilters) {
				sourceAmplitudes[0] =
				    multiply_32x32_rshift32_rounded(params.paramFinalValues[params::LOCAL_OSC_A_VOLUME], filterGain);
				sourceAmplitudes[1] =
				    multiply_32x32_rshift32_rounded(params.paramFinalValues[params::LOCAL_OSC_B_VOLUME], filterGain);
			}
			else {
				sourceAmplitudes[0] = params.paramFinalValues[params::LOCAL_OSC_A_VOLUME] >> 4;
				sourceAmplitudes[1] = params.paramFinalValues[params::LOCAL_OSC_B_VOLUME] >> 4;
			}
		}

		bool shouldAvoidIncrementing = doneFirstRender ? (filterGainLastTime != filterGain)
		                                               : (params.paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632);

		if (shouldAvoidIncrementing) {
			for (int32_t s = 0; s < kNumSources; s++) {
				sourceAmplitudesLastTime[s] = sourceAmplitudes[s];
			}
		}

		for (int32_t s = 0; s < kNumSources; s++) {
			sourceAmplitudeIncrements[s] = (int32_t)(sourceAmplitudes[s] - sourceAmplitudesLastTime[s]) / numSamples;
		}

		filterGainLastTime = filterGain;

		// If FM, cache whether modulators are active
		if (synthMode == SynthMode::FM) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				modulatorsActive[m] = (params.paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m] != 0
				                       || modulatorAmplitudeLastTime[m] != 0);

				if (modulatorsActive[m]) {
					modulatorAmplitudeIncrements[m] =
					    (int32_t)(params.paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m]
					              - modulatorAmplitudeLastTime[m])
					    / numSamples;
				}
			}
		}
	}

	int32_t sourceWaveIndexIncrements[kNumSources];

	if (synthMode != SynthMode::FM) {
		if (!doneFirstRender && params.paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632) {
			overallOscAmplitudeLastTime = overallOscAmplitude;
		}
		overallOscillatorAmplitudeIncrement = (int32_t)(overallOscAmplitude - overallOscAmplitudeLastTime) / numSamples;

		for (int32_t s = 0; s < kNumSources; s++) {
			sourceWaveIndexIncrements[s] =
			    (int32_t)(params.paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s] - sourceWaveIndexesLastTime[s])
			    / numSamples;
		}
	}

	doneFirstRender = true;

	uint32_t oscSyncPos[kMaxNumVoicesUnison];
	bool doingOscSync = cfg.doingOscSync;

	// Oscillator sync: capture the master (source 0) phase at block start — from whichever persistent engine owns
	// it (virtual-analog / wavetable), else oscPos.
	if (doingOscSync) {
		for (int32_t u = 0; u < cfg.numUnison; u++) {
			oscSyncPos[u] = unisonParts[u].sources[0].currentOscPhase();
		}
	}

	// whether stereo unison actually is active. if stereo is being vetoed from higher up, don't do it.
	bool stereoUnison = cfg.unisonStereoSpread && cfg.numUnison > 1 && soundRenderingInStereo;

	int32_t* oscBuffer;
	bool anythingInOscBuffer = false;
	int32_t sourceAmplitudesNow[kNumSources];
	for (int32_t s = 0; s < kNumSources; s++) {
		sourceAmplitudesNow[s] = sourceAmplitudesLastTime[s];
	}

	int32_t amplitudeL, amplitudeR;
	bool doPanning;

	// two first indicies are reserved in case we need stereo for unison spread
	oscBuffer = spareRenderingBuffer[0];
	int32_t channels = stereoUnison ? 2 : 1;

	int32_t const* const oscBufferEnd = oscBuffer + numSamples * channels;

	// If any noise, do that. By cutting a corner here, we do it just once for all "unison", rather than for each
	// unison. Increasing number of unison cuts the volume of the oscillators
	if (params.paramFinalValues[params::LOCAL_NOISE_VOLUME] != 0 && synthMode != SynthMode::FM) [[unlikely]] {

		// This was >>2, but because I had a bug in V2.0.x which made noise too loud if filter on,
		// I'm now making this louder to compensate and remain consistent by going just >>1.
		// So now I really need to make it so that sounds made before V2.0 halve their noise volume... (Hey, did I
		// ever do this? Who knows...)
		int32_t n = params.paramFinalValues[params::LOCAL_NOISE_VOLUME] >> 1;
		if (cfg.hasFilters) {
			n = multiply_32x32_rshift32(n, filterGain) << 4;
		}

		// Perform the same limiting that we do above for the oscillators
		int32_t noiseAmplitude = std::min(n, (int32_t)268435455) >> 2;

		int32_t* __restrict__ thisSample = oscBuffer;
		do {
			*thisSample = multiply_32x32_rshift32(getNoise(), noiseAmplitude);
			thisSample++;
		} while (thisSample != oscBufferEnd);

		anythingInOscBuffer = true;
	}

	// Otherwise, clear the buffer
	else {
		memset(oscBuffer, 0, channels * numSamples * sizeof(int32_t));
	}

	// Even if first rendering into a local Voice buffer, we'll very often still just do panning at the Sound level
	if (!applyingPanAtVoiceLevel) {
		doPanning = false;
	}
	else {
		// Set up panning
		doPanning = (cfg.engineRenderingInStereo
		             && shouldDoPanning(params.paramFinalValues[params::LOCAL_PAN], &amplitudeL, &amplitudeR));
	}

	// Normal mode: subtractive / samples. The engine owns the per-source render loop + osc-sync + the stereo
	// buffer dance + the unison-inactive→unassign decision (it delegates each source's render back to
	// renderBasicSource for now).
	if (synthMode == SynthMode::SUBTRACTIVE) {
		subtractiveEngine.render(*this, paramManager, oscBuffer, numSamples, doingOscSync, oscSyncPos,
		                         overallPitchAdjust, sourceAmplitudesNow, sourceAmplitudeIncrements,
		                         sourceWaveIndexIncrements, anythingInOscBuffer, didStereoTempBuffer,
		                         unassignVoiceAfter, cfg);
	}

	// Otherwise (FM and ringmod) we go through each unison first, and for each one we render both sources together
	else {
		if (stereoUnison) {
			// oscBuffer is always a stereo temp buffer
			didStereoTempBuffer = true;
		}

		// For each unison part
		for (int32_t u = 0; u < cfg.numUnison; u++) {

			int32_t unisonAmplitudeL, unisonAmplitudeR;
			shouldDoPanning((stereoUnison ? cfg.unisonPan[u] : 0), &unisonAmplitudeL, &unisonAmplitudeR);

			// Work out the phase increments of the two sources. If these are too high, sourceAmplitudes[s] is set to 0.
			// Yes this will affect all unison parts, which seems like it's not what we want, but since we're traversing
			// the unison parts in ascending frequency, it's fine!

			uint32_t phaseIncrements[kNumSources];
			for (int32_t s = 0; s < kNumSources; s++) {
				phaseIncrements[s] = unisonParts[u].sources[s].phaseIncrementStoredValue;
			}

			// If overall pitch adjusted...
			if (overallPitchAdjust != kMaxSampleValue) {
				for (int32_t s = 0; s < kNumSources; s++) {
					if (!adjustPitch(phaseIncrements[s], overallPitchAdjust)) {
						if (synthMode == SynthMode::RINGMOD) {
							goto skipUnisonPart;
						}
						else {
							sourceAmplitudes[s] = 0; // For FM
						}
					}
				}
			}

			// If individual source pitch adjusted...
			for (int32_t s = 0; s < kNumSources; s++) {
				if (!adjustPitch(phaseIncrements[s], params.paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
					if (synthMode == SynthMode::RINGMOD) {
						goto skipUnisonPart;
					}
					else {
						sourceAmplitudes[s] = 0; // For FM
					}
				}
			}

			// If ringmod
			if (synthMode == SynthMode::RINGMOD) [[unlikely]] {

				// Build the per-source render context; the engine owns the cross-source ring-mod relationship
				// (render both sources raw with source 0 the hard-sync master, then multiply + pan).
				RingModEngine::Source ringModSources[kNumSources];
				for (int32_t s = 0; s < kNumSources; s++) {
					// Work out pulse width, from parameter. This has no effect if we're not actually using square
					// waves, but just do it anyway, it's a simple calculation
					int32_t pulseWidth =
					    (uint32_t)lshiftAndSaturate<1>(params.paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s]);

					OscType oscType = cfg.oscType[s];

					// Non-wavetable sources have no audioFileHolder; only OscType::WAVETABLE reads the table.
					WaveTable* waveTable = (oscType == OscType::WAVETABLE)
					                           ? static_cast<WaveTable*>(guides[s].audioFileHolder->audioFile)
					                           : nullptr;

					ringModSources[s] = RingModEngine::Source{
					    .part = &unisonParts[u].sources[s],
					    .phaseIncrement = phaseIncrements[s],
					    .pulseWidth = pulseWidth,
					    .waveTable = waveTable,
					    .waveIndexLast = sourceWaveIndexesLastTime[s],
					    .waveIndexIncrement = sourceWaveIndexIncrements[s],
					    .oscType = oscType,
					    .retriggerPhase = cfg.oscRetriggerPhase[s],
					};
				}

				ringModEngine.render(ringModSources[0], ringModSources[1], doingOscSync, oscSyncPos[u], cfg.hasFilters,
				                     filterGain, unisonAmplitudeL, unisonAmplitudeR, stereoUnison, oscBuffer,
				                     numSamples, spareRenderingBuffer[2], spareRenderingBuffer[3]);
			}

			// Or if FM
			else {
				fmEngine.render(*this, u, numSamples, overallPitchAdjust, stereoUnison, unisonAmplitudeL,
				                unisonAmplitudeR, phaseIncrements, sourceAmplitudes, sourceAmplitudesNow,
				                sourceAmplitudeIncrements, modulatorsActive, modulatorAmplitudeIncrements, oscBuffer,
				                spareRenderingBuffer[2], spareRenderingBuffer[3], cfg);
			}
		}

skipUnisonPart: {}
	}

	if (didStereoTempBuffer) {
		int32_t* const oscBufferEnd = oscBuffer + (numSamples << 1);
		// fold
		if (params.paramFinalValues[params::LOCAL_FOLD] > 0) {
			dsp::foldBufferPolyApproximation(oscBuffer, oscBufferEnd, params.paramFinalValues[params::LOCAL_FOLD]);
		}
		// Filters
		filterSet.renderLongStereo(oscBuffer, oscBufferEnd);

		// No clipping
		if (!cfg.clippingAmount) {

			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			StereoSample* __restrict__ outputSample = (StereoSample*)soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;

			do {
				int32_t outputSampleL = *(oscBufferPos++);
				int32_t outputSampleR = *(oscBufferPos++);

				overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
				if (synthMode != SynthMode::FM) {
					outputSampleL = multiply_32x32_rshift32_rounded(outputSampleL, overallOscAmplitudeNow) << 1;
					outputSampleR = multiply_32x32_rshift32_rounded(outputSampleR, overallOscAmplitudeNow) << 1;
				}

				// Write to the output buffer, panning or not
				if (doPanning) {
					*outputSample += deluge::dsp::pannedStereo(outputSampleL, outputSampleR, amplitudeL, amplitudeR);
				}
				else {
					*outputSample += StereoSample{.l = outputSampleL, .r = outputSampleR};
				}

				outputSample++;
			} while (oscBufferPos != oscBufferEnd);
		}

		// Yes clipping
		else {

			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			StereoSample* __restrict__ outputSample = (StereoSample*)soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;
			int32_t shiftAmount = shiftForSaturation(cfg.clippingAmount);

			do {
				int32_t outputSampleL = *(oscBufferPos++);
				int32_t outputSampleR = *(oscBufferPos++);

				overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
				if (synthMode != SynthMode::FM) {
					outputSampleL = multiply_32x32_rshift32_rounded(outputSampleL, overallOscAmplitudeNow) << 1;
					outputSampleR = multiply_32x32_rshift32_rounded(outputSampleR, overallOscAmplitudeNow) << 1;
				}

				saturateSample(&outputSampleL, &lastSaturationTanHWorkingValue[0], shiftAmount, cfg.clippingAmount);
				saturateSample(&outputSampleR, &lastSaturationTanHWorkingValue[1], shiftAmount, cfg.clippingAmount);

				// Write to the output buffer, panning or not
				if (doPanning) {
					*outputSample += deluge::dsp::pannedStereo(outputSampleL, outputSampleR, amplitudeL, amplitudeR);
				}
				else {
					*outputSample += StereoSample{.l = outputSampleL, .r = outputSampleR};
				}

				outputSample++;
			} while (oscBufferPos != oscBufferEnd);
		}
	}
	else {
		/*
		do {
		    int32_t distanceToGoL = *oscBufferPos - hpfMem;
		    hpfMem += distanceToGoL >> 11;
		    *oscBufferPos -= hpfMem;

		} while (++oscBufferPos != oscBufferEnd);

		oscBufferPos = oscBuffer;
		*/

		int32_t* const oscBufferEnd = oscBuffer + numSamples;
		// wavefolding pre filter
		if (params.paramFinalValues[params::LOCAL_FOLD] > 0) {
			q31_t foldAmount = params.paramFinalValues[params::LOCAL_FOLD];

			dsp::foldBufferPolyApproximation(oscBuffer, oscBufferEnd, foldAmount);
		}

		filterSet.renderLong(oscBuffer, oscBufferEnd, numSamples);

		// No clipping
		if (!cfg.clippingAmount) {
			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			int32_t* __restrict__ outputSample = soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;

			do {
				int32_t output = *oscBufferPos;

				if (synthMode != SynthMode::FM) {
					overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
					output = multiply_32x32_rshift32_rounded(output, overallOscAmplitudeNow) << 1;
				}

				if (soundRenderingInStereo) {
					if (doPanning) {
						*((StereoSample*)outputSample) += deluge::dsp::pannedMono(output, amplitudeL, amplitudeR);
					}
					else {
						*((StereoSample*)outputSample) += StereoSample::fromMono(output);
					}
					outputSample += 2;
				}
				else {
					*outputSample += output;
					outputSample++;
				}
			} while (++oscBufferPos != oscBufferEnd);
		}

		// Yes clipping
		else {
			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			int32_t* __restrict__ outputSample = soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;

			int32_t shiftAmount = shiftForSaturation(cfg.clippingAmount);
			do {
				int32_t output = *oscBufferPos;

				if (synthMode != SynthMode::FM) {
					overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
					output = multiply_32x32_rshift32_rounded(output, overallOscAmplitudeNow) << 1;
				}

				saturateSample(&output, &lastSaturationTanHWorkingValue[0], shiftAmount, cfg.clippingAmount);

				if (soundRenderingInStereo) {
					if (doPanning) {
						*((StereoSample*)outputSample) += deluge::dsp::pannedMono(output, amplitudeL, amplitudeR);
					}
					else {
						*((StereoSample*)outputSample) += StereoSample::fromMono(output);
					}
					outputSample += 2;
				}
				else {
					*outputSample += output;
					outputSample++;
				}
			} while (++oscBufferPos != oscBufferEnd);
		}
	}

renderingDone:

	for (int32_t s = 0; s < kNumSources; s++) {
		sourceAmplitudesLastTime[s] = sourceAmplitudes[s];
		sourceWaveIndexesLastTime[s] = params.paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s];
	}
	for (int32_t m = 0; m < kNumModulators; m++) {
		modulatorAmplitudeLastTime[m] = params.paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m];
	}
	overallOscAmplitudeLastTime = overallOscAmplitude;

	return !unassignVoiceAfter;
}

bool Voice::areAllUnisonPartsInactive(const VoiceConfig& cfg) const {
	// If no noise-source, then it might be time to unassign the voice...
	if (!cfg.noiseVolumeContainsSomething) {

		// See if all unison parts are now inactive
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!cfg.sourceEverActive[s]) {
				continue;
			}
			for (int32_t u = 0; u < cfg.numUnison; u++) {
				if (unisonParts[u].sources[s].active) {
					return false;
				}
			}
		}

		// If here, no parts active anymore!
		return true;
	}

	return false;
}

// Returns false if it takes us above 22.05kHz, in which case it doesn't return a valid value
bool Voice::adjustPitch(uint32_t& phaseIncrement, int32_t adjustment) {
	if (adjustment != kMaxSampleValue) {
		int32_t output = multiply_32x32_rshift32_rounded(phaseIncrement, adjustment);
		if (output >= (1 << 24)) {
			return false;
		}
		phaseIncrement = output << 8;
	}
	return true;
}

void Voice::renderSineWaveWithFeedback(int32_t* bufferStart, int32_t numSamples, uint32_t* phase, int32_t amplitude,
                                       uint32_t phaseIncrement, int32_t feedbackAmount, int32_t* lastFeedbackValue,
                                       bool add, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t feedbackValue = *lastFeedbackValue;
		int32_t* bufferEnd = bufferStart + numSamples;
		do {
			amplitudeNow += amplitudeIncrement;
			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			feedbackValue = dsp::fixed::sine_fm_operator(phaseNow += phaseIncrement, feedback);

			if (add) {
				*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, feedbackValue, amplitudeNow);
			}
			else {
				*thisSample = multiply_32x32_rshift32(feedbackValue, amplitudeNow);
			}
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;

		if (amplitudeIncrement) {
			do {
				auto phaseVector = Argon<uint32_t>{phaseNow}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phaseIncrement);
				phaseNow += phaseIncrement * 4;
				int32x4_t sineValueVector = dsp::fixed::sine(phaseVector);

				int32x4_t amplitudeVector;
				for (int32_t i = 0; i < 4; i++) {
					amplitudeNow += amplitudeIncrement;
					amplitudeVector[i] = amplitudeNow >> 1;
				}

				int32x4_t resultValueVector = vqdmulhq_s32(amplitudeVector, sineValueVector);

				if (add) {
					int32x4_t existingBufferContents = vld1q_s32(thisSample);
					int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
					vst1q_s32(thisSample, added);
				}
				else {
					vst1q_s32(thisSample, resultValueVector);
				}

				thisSample += 4;
			} while (thisSample < bufferEnd);
		}

		else {
			do {
				auto phaseVector = Argon<uint32_t>{phaseNow}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phaseIncrement);
				phaseNow += phaseIncrement * 4;
				int32x4_t sineValueVector = dsp::fixed::sine(phaseVector);
				int32x4_t resultValueVector = vqrdmulhq_n_s32(sineValueVector, amplitudeNow >> 1);

				if (add) {
					int32x4_t existingBufferContents = vld1q_s32(thisSample);
					int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
					vst1q_s32(thisSample, added);
				}
				else {
					vst1q_s32(thisSample, resultValueVector);
				}

				thisSample += 4;
			} while (thisSample < bufferEnd);
		}
	}
}

void Voice::renderFMWithFeedback(int32_t* bufferStart, int32_t numSamples, int32_t* fmBuffer, uint32_t* phase,
                                 int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
                                 int32_t* lastFeedbackValue, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;

		int32_t feedbackValue = *lastFeedbackValue;
		do {
			amplitudeNow += amplitudeIncrement;

			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			uint32_t sum = (uint32_t)*thisSample + (uint32_t)feedback;

			feedbackValue = dsp::fixed::sine_fm_operator(phaseNow += phaseIncrement, sum);
			*thisSample = multiply_32x32_rshift32(feedbackValue, amplitudeNow);
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;
		do {
			amplitudeNow += amplitudeIncrement;
			int32_t fmValue = dsp::fixed::sine_fm_operator(phaseNow += phaseIncrement, *thisSample);
			*thisSample = multiply_32x32_rshift32(fmValue, amplitudeNow);
		} while (++thisSample != bufferEnd);
	}
}

void Voice::renderFMWithFeedbackAdd(int32_t* bufferStart, int32_t numSamples, int32_t* fmBuffer, uint32_t* phase,
                                    int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
                                    int32_t* lastFeedbackValue, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* fmSample = fmBuffer;
		int32_t* bufferEnd = bufferStart + numSamples;

		int32_t feedbackValue = *lastFeedbackValue;
		do {
			amplitudeNow += amplitudeIncrement;

			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			uint32_t sum = (uint32_t)*(fmSample++) + (uint32_t)feedback;

			feedbackValue = dsp::fixed::sine_fm_operator(phaseNow += phaseIncrement, sum);
			*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, feedbackValue, amplitudeNow);
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;
		uint32_t* fmSample = (uint32_t*)fmBuffer;
		int32_t* bufferPreEnd = bufferEnd - 4;

		uint32x4_t phaseVector;
		for (int32_t i = 0; i < 4; i++) {
			phaseNow += phaseIncrement;
			phaseVector[i] = phaseNow;
		}

		uint32x4_t phaseIncrementVector = vdupq_n_u32(phaseIncrement << 2);

		if (amplitudeIncrement) {
			while (true) {
				uint32x4_t phaseShift = vld1q_u32(fmSample);
				int32x4_t sineValueVector =
				    dsp::fixed::sine(Argon<uint32_t>{phaseVector} + (Argon<uint32_t>{phaseShift} << 8));

				int32x4_t amplitudeVector;
				for (int32_t i = 0; i < 4; i++) {
					amplitudeNow += amplitudeIncrement;
					amplitudeVector[i] = amplitudeNow >> 1;
				}

				int32x4_t resultValueVector = vqdmulhq_s32(amplitudeVector, sineValueVector);

				int32x4_t existingBufferContents = vld1q_s32(thisSample);
				int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
				vst1q_s32(thisSample, added);

				if (thisSample >= bufferPreEnd) {
					break;
				}

				thisSample += 4;
				fmSample += 4;
				phaseVector = vaddq_u32(phaseVector, phaseIncrementVector);
			}
		}

		else {
			while (true) {
				uint32x4_t phaseShift = vld1q_u32(fmSample);
				int32x4_t sineValueVector =
				    dsp::fixed::sine(Argon<uint32_t>{phaseVector} + (Argon<uint32_t>{phaseShift} << 8));

				int32x4_t resultValueVector = vqrdmulhq_n_s32(sineValueVector, amplitudeNow >> 1);

				int32x4_t existingBufferContents = vld1q_s32(thisSample);
				int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
				vst1q_s32(thisSample, added);

				if (thisSample >= bufferPreEnd) {
					break;
				}

				thisSample += 4;
				fmSample += 4;
				phaseVector = vaddq_u32(phaseVector, phaseIncrementVector);
			}
		}
	}
}

// This function renders all unison for a source/oscillator. Amplitude and the incrementing thereof is done
// independently for each unison, despite being the same for all of them, and you might be wondering why this is. Yes in
// the case of an 8-unison sound it'd work out slightly better to apply amplitude to all unison together, but here's why
// this just generally isn't all that advantageous:
//	-	Doing it separately for each unison, as we are, means each unison of *both* sources can just sum itself directly
// onto the buffer for the whole Voice, or in special cases the Sound, 		which only had to be wiped clean once. But
// applying amplitude to the combined unisons of one source would mean we'd need to clear a buffer for that source, then
// render our 		potentially just one unison into that (summing to the buffer's existing contents), then as amplitude
// is applied to the buffer, we'd write the output of that into the 		Voice buffer (summing to its contents too).
// So basically an extra level of summing and clearing would have to happen, in addition to the extra copying which is
// obvious. 	-	And you might be thinking that for oscillator sync this would work well, because it already applies
// amplitude in a separate step after rendering the wave. 		But no - a key part of why this works well as it is is
// that the summing between unisons only happens along with that amplitude-application, after the initial wave render
// has finished chopping around the contents of its buffer. Having that summed into an all-unison buffer would still
// require an additional copying(summing) step, 		and we might as well just apply amplitude while that's
// happening, which is exactly how it is currently.

// Render one unison part of one source, dispatching by oscType to the per-source component and folding the
// per-engine outcomes into one uniform SourceRenderResult. The single uniform dispatch seam a SourceComponent
// interface + the engine registry will build on. (Still a Voice method using public state — this is the seam,
// not yet the de-coupling.)
deluge::synth::SourceRenderResult
Voice::renderSourceComponent(int32_t u, int32_t s, ParamManagerForTimeline* paramManager, int32_t* oscBuffer,
                             int32_t numSamples, bool stereoBuffer, int32_t sourceAmplitude, int32_t amplitudeIncrement,
                             uint32_t phaseIncrement, bool doOscSync, uint32_t* oscSyncPos,
                             uint32_t* oscSyncPhaseIncrements, int32_t waveIndexIncrement, bool stereoUnison,
                             int32_t amplitudeL, int32_t amplitudeR, const VoiceConfig& cfg) {
	using deluge::synth::SourceRenderResult;
	VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];

	// SAMPLE caching policy, hoisted out of SampleSource::renderSource so it needn't reach paramManager / the param
	// planes: does any patch cable modulate pitch with a *changeable* source? Bit-exact with the legacy inline scan
	// (`cable->from` is a single source ⇒ the OR equals the if/else-if chain; only consumed on a sample's first
	// render). Computed only for SAMPLE — the cable scan would be wasted for the other sources.
	bool pitchModDisallowsCache = false;
	if (cfg.oscType[s] == OscType::SAMPLE) {
		PatchCableSet* patchCableSet = paramManager->getPatchCableSet();
		for (int32_t c = 0; c < patchCableSet->numUsablePatchCables; c++) {
			PatchCable* cable = &patchCableSet->patchCables[c];
			if (cable->destinationParamDescriptor.isSetToParamWithNoSource(params::LOCAL_PITCH_ADJUST)
			    || cable->destinationParamDescriptor.isSetToParamWithNoSource(params::LOCAL_OSC_A_PITCH_ADJUST + s)) {
				if (cable->from == PatchSource::ENVELOPE_0 || cable->from == PatchSource::ENVELOPE_1
				    || cable->from == PatchSource::LFO_GLOBAL_1 || cable->from == PatchSource::LFO_LOCAL_1
				    || cable->from == PatchSource::LFO_GLOBAL_2 || cable->from == PatchSource::LFO_LOCAL_2
				    || cable->from == PatchSource::RANDOM
				    || (cable->from == PatchSource::AFTERTOUCH
				        && params.sourceValues[util::to_underlying(PatchSource::AFTERTOUCH)])
				    || (cable->from == PatchSource::SIDECHAIN
				        && cfg.globalSourceValues[util::to_underlying(PatchSource::SIDECHAIN)])) {
					pitchModDisallowsCache = true;
					break;
				}
			}
		}
	}

	// All five sources render through the polymorphic synth::Source seam, which owns (for oscillators) the osc-sync
	// freq-too-high SKIP + mono-scratch/stereo-fold, and (per engine) the block render. The app-side adapter just
	// gathers the already-modulated per-block values into the SourceRenderContext.
	uint32_t oscRetriggerPhase = cfg.oscRetriggerPhase[s]; // Yes we might need this even if not doing osc sync.
	uint32_t oscSyncPosThisUnison = doOscSync ? oscSyncPos[u] : 0;
	uint32_t oscSyncPhaseIncrementsThisUnison = doOscSync ? oscSyncPhaseIncrements[u] : 0;

	// Work out pulse width
	uint32_t pulseWidth = (uint32_t)lshiftAndSaturate<1>(params.paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s]);

	// The wave table is only consumed when oscType == WAVETABLE. Basic oscillators (SAW/SQUARE/SINE/...) have no
	// audio file, so audioFileHolder is null — don't dereference it.
	WaveTable* waveTable =
	    guides[s].audioFileHolder != nullptr ? static_cast<WaveTable*>(guides[s].audioFileHolder->audioFile) : nullptr;

	synth::SourceRenderContext ctx{
	    .oscBuffer = oscBuffer,
	    .numSamples = numSamples,
	    .stereoBuffer = stereoBuffer,
	    .stereoUnison = stereoUnison,
	    .amplitudeL = amplitudeL,
	    .amplitudeR = amplitudeR,
	    .sourceAmplitude = sourceAmplitude,
	    .amplitudeIncrement = amplitudeIncrement,
	    .phaseIncrement = phaseIncrement,
	    .doOscSync = doOscSync,
	    .oscSyncPos = oscSyncPosThisUnison,
	    .oscSyncPhaseIncrement = oscSyncPhaseIncrementsThisUnison,
	    .controls =
	        makeBlockControls(sourceAmplitude, amplitudeIncrement, pulseWidth, /*applyAmplitude=*/true, doOscSync,
	                          oscSyncPosThisUnison, oscSyncPhaseIncrementsThisUnison, oscRetriggerPhase),
	    .waveTableData = (waveTable != nullptr) ? waveTable->view() : deluge::dsp::fixed::WaveTableData{},
	    .waveIndexLast = sourceWaveIndexesLastTime[s],
	    .waveIndexIncrement = waveIndexIncrement,
	    .stereoScratch = spareRenderingBuffer[2],
	    .stereoScratchClearSamples = SSI_TX_BUFFER_NUM_SAMPLES,
	    .syncScratch = deluge::dsp::oscSyncRenderingBuffer,
	    .ampModParam = params.paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s], // DX7 amplitude-mod control
	    .patchChanged = cfg.dxPatchChanged[s],                                       // DX7
	    .midinote = noteCodeAfterArpeggiation,                                       // DX7
	    .oscType = cfg.oscType[s],                                                   // live input: L/R/stereo routing
	    .interpolationBufferSize =
	        SampleControls::getInterpolationBufferSize(cfg.interpolationMode[s], phaseIncrement), // live input
	    .portaEnvelopePos = portaEnvelopePos,                                                     // sample
	    .priorityRating = getPriorityRating(),                                         // sample (snapshot once)
	    .phaseIncrementStoredValue = voiceUnisonPartSource->phaseIncrementStoredValue, // sample late-start
	    .pitchModDisallowsCache = pitchModDisallowsCache,                              // sample caching policy
	};

	// sample → sampleSource, DX7 → dx7Source, INPUT_* → inputSource, wavetable → wavetableSource, else virtual-analog
	// → vaSource — every source is now a persistent synth::Source; the legacy oscType megaswitch is gone.
	synth::Source* component;
	if (cfg.oscType[s] == OscType::SAMPLE) {
		component = &unisonParts[u].sources[s].sampleSource;
	}
	else if (cfg.oscType[s] == OscType::DX7) {
		component = &unisonParts[u].sources[s].dx7Source;
	}
	else if (cfg.oscType[s] == OscType::INPUT_L || cfg.oscType[s] == OscType::INPUT_R
	         || cfg.oscType[s] == OscType::INPUT_STEREO) {
		component = &unisonParts[u].sources[s].inputSource;
	}
	else if (unisonParts[u].sources[s].vaSource.has_value()) {
		component = &*unisonParts[u].sources[s].vaSource;
	}
	else {
		component = &*unisonParts[u].sources[s].wavetableSource;
	}
	return component->renderSource(ctx);
}

void Voice::renderBasicSource(ParamManagerForTimeline* paramManager, int32_t s, int32_t* __restrict__ oscBuffer,
                              int32_t numSamples, bool stereoBuffer, int32_t sourceAmplitude,
                              bool* __restrict__ unisonPartBecameInactive, int32_t overallPitchAdjust, bool doOscSync,
                              uint32_t* __restrict__ oscSyncPos, uint32_t* __restrict__ oscSyncPhaseIncrements,
                              int32_t amplitudeIncrement, uint32_t* __restrict__ getPhaseIncrements,
                              bool getOutAfterPhaseIncrements, int32_t waveIndexIncrement, const VoiceConfig& cfg) {

	checkStack("Voice::renderBasicSource");

	// For each unison part
	for (int32_t u = 0; u < cfg.numUnison; u++) {

		VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];

		// Samples may become inactive
		if (!voiceUnisonPartSource->active) {
			continue;
		}

		if (false) {
instantUnassign:

#ifdef TEST_SAMPLE_LOOP_POINTS
			FREEZE_WITH_ERROR("YEP");
#endif

			*unisonPartBecameInactive = true;
			voiceUnisonPartSource->unassign(false);
			continue;
		}

		uint32_t phaseIncrement = voiceUnisonPartSource->phaseIncrementStoredValue;

		// Overall pitch adjustment
		if (!adjustPitch(phaseIncrement, overallPitchAdjust)) {

pitchTooHigh:
			if (getPhaseIncrements) {
				getPhaseIncrements[u] = 0;
			}
			continue;
		}

		// Individual source pitch adjustment
		if (!adjustPitch(phaseIncrement, params.paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
			goto pitchTooHigh;
		}

		if (getPhaseIncrements) {
			getPhaseIncrements[u] = phaseIncrement;

			if (getOutAfterPhaseIncrements) {
				// Skipped source still advances its phase (it's the osc-sync master) — in whichever engine owns it.
				voiceUnisonPartSource->advanceOscPhase(phaseIncrement * numSamples);
				continue;
			}
		}

		bool stereoUnison = cfg.unisonStereoSpread && cfg.numUnison > 1 && stereoBuffer;
		int32_t amplitudeL, amplitudeR;
		shouldDoPanning((stereoUnison ? cfg.unisonPan[u] : 0), &amplitudeL, &amplitudeR);
		// used if mono source but stereoUnison active

		auto result =
		    renderSourceComponent(u, s, paramManager, oscBuffer, numSamples, stereoBuffer, sourceAmplitude,
		                          amplitudeIncrement, phaseIncrement, doOscSync, oscSyncPos, oscSyncPhaseIncrements,
		                          waveIndexIncrement, stereoUnison, amplitudeL, amplitudeR, cfg);
		if (result == deluge::synth::SourceRenderResult::UNASSIGN) {
			goto instantUnassign;
		}
		if (result == deluge::synth::SourceRenderResult::SKIP) {
			continue;
		}
	}
}

bool Voice::doFastRelease(uint32_t releaseIncrement) {
	if (doneFirstRender) {
		envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE, releaseIncrement);
		stampEnvelope0StageEntry();
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	else {
		return false;
	}
}

bool Voice::forceNormalRelease() {
	if (doneFirstRender) {
		// If no release-stage, we'll stop as soon as we can
		if (!hasReleaseStage()) {
			envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE, SOFT_CULL_INCREMENT);
		}
		// otherwise we'll just let it get there on its own
		else {
			envelopes[0].unconditionalRelease();
		}
		stampEnvelope0StageEntry();
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	else {
		return false;
	}
}

bool Voice::speedUpRelease() {
	auto currentStage = envelopes[0].stage();
	if (currentStage == Envelope::Stage::RELEASE) {
		auto currentRelease = params.paramFinalValues[params::LOCAL_ENV_0_RELEASE];
		envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE, currentRelease * 2);
		stampEnvelope0StageEntry();
		return true;
	}
	else if (currentStage == Envelope::Stage::FAST_RELEASE) {
		auto currentRelease = envelopes[0].fastReleaseRate();
		envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE, currentRelease * 2);
		stampEnvelope0StageEntry();
		return true;
	}

	// Or if we're not releasing just start the release
	else {
		return forceNormalRelease();
	}
}

bool Voice::doImmediateRelease() {
	if (doneFirstRender) {
		envelopes[0].unconditionalOff();
		stampEnvelope0StageEntry();
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	return false;
}

bool Voice::hasReleaseStage() {
	return (params.paramFinalValues[params::LOCAL_ENV_0_RELEASE] <= 18359);
}

// if more added, need to change the shifts around
static_assert(Envelope::kNumStages < 8, "Too many envelope stages");
static_assert(kNumVoicePriorities < 4, "Too many priority options");

// Higher numbers are lower priority. 1 is top priority. Will never return 0, because nextVoiceState starts at 1
uint32_t Voice::getPriorityRating() const {
	return
	    // Bits 30-31 - manual priority setting
	    ((uint32_t)(3 - util::to_underlying(sound.voicePriority)) << 30)

	    // Bits 27-29 - how many voices that Sound has
	    // - that one really does need to go above state, otherwise "once" samples can still cut out synth drones.
	    // In a perfect world, culling for the purpose of "soliciting" a Voice would also count the new Voice being
	    // solicited, preferring to cut out that same Sound's old, say, one Voice, than another Sound's only Voice
	    + ((uint32_t)std::min<size_t>(sound.numActiveVoices(), 7) << 27)

	    // Bits 24-26 - envelope state
	    + ((uint32_t)envelopes[0].stage() << 24)

	    // Bits  0-23 - time entered
	    + ((uint32_t)(-envelope0TimeEntered) & (0xFFFFFFFF >> 8));
}

void Voice::stampEnvelope0StageEntry() {
	if (envelopes[0].takeStageChanged()) {
		envelope0TimeEntered = AudioEngine::nextVoiceState++;
	}
}

void Voice::resetEnvelope0TimeEntered() {
	if (envelopes[0].stage() == Envelope::Stage::SUSTAIN) {
		envelope0TimeEntered = AudioEngine::nextVoiceState++;
	}
}
