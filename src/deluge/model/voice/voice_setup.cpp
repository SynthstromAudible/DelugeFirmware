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

#include "model/voice/voice_setup.h"
#include "io/debug/log.h"
#include "memory/stack_guard.h"
#include "model/model_stack.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/voice/voice.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/multi_range/multi_range.h"
#include <span>

namespace params = deluge::modulation::params;
using deluge::synth::NoteSetup;
using deluge::synth::ReleaseInfo;
using deluge::synth::SourceSetup;

uint32_t lastSoundOrder = 0;

NoteSetup note_setup_for(Voice& voice, Sound& sound, ParamManagerForTimeline* paramManager,
                         ModelStackWithSoundFlags& flags, int32_t noteAfterArpeggiation, uint32_t sampleSyncLength,
                         int32_t ticksLate, bool forNoteChange) {
	NoteSetup setup;
	const SynthMode synthMode = sound.getSynthMode();

	if (!forNoteChange) {
		// Each SAMPLE or WAVETABLE source plays the file of its range for the note. Only a range with a file loaded
		// becomes the guide's, so a voice source with a range always has a file.
		bool ringModFails = false;
		for (int32_t s = 0; s < kNumSources; s++) {
			VoiceSamplePlaybackGuide& guide = voice.guides[s];
			const Source& source = sound.sources[s];
			guide.audioFileHolder = nullptr;
			bool playable = flags.checkSourceEverActive(s);
			if (playable) [[likely]] {
				guide.noteOffReceived = false;
				guide.sequenceSyncLengthTicks = 0;
				if (synthMode != SynthMode::FM
				    && (source.oscType == OscType::SAMPLE || source.oscType == OscType::WAVETABLE)) {
					MultiRange* range = sound.sources[s].getRange(noteAfterArpeggiation + sound.getTranspose());
					AudioFileHolder* holder = range != nullptr ? range->getAudioFileHolder() : nullptr;
					if (holder == nullptr || holder->audioFile == nullptr) {
						playable = false;
					}
					else {
						guide.audioFileHolder = holder;
						if (source.oscType == OscType::SAMPLE && source.repeatMode == SampleRepeatMode::STRETCH) {
							guide.sequenceSyncLengthTicks = sampleSyncLength;
							// lastSwungTickActioned, not getActualSwungTickCount(): ticksLate counts from it
							guide.sequenceSyncStartedAtTick = playbackHandler.lastSwungTickActioned - ticksLate;
						}
					}
				}
			}
			setup.sources[s].playable = playable;
			ringModFails = ringModFails || (!playable && synthMode == SynthMode::RINGMOD);
			if (ringModFails) {
				break; // the note fails here, before any later source is set up
			}
		}

		// A sample plays the way the sound reverses it for this note, between the bounds that sets.
		for (int32_t s = 0; s < kNumSources && !ringModFails; s++) {
			Source& source = sound.sources[s];
			if (synthMode != SynthMode::FM && source.oscType == OscType::SAMPLE && voice.guides[s].audioFileHolder
			    && flags.checkSourceEverActive(s)) {
				source.sampleControls.invertReversed = sound.invertReversed;
				voice.guides[s].setupPlaybackBounds(source.sampleControls.isCurrentlyReversed());
			}
		}

		setup.porta = sound.polyphonic != PolyphonyMode::LEGATO
		              && paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_PORTAMENTO) != -2147483648
		              && sound.lastNoteCode != deluge::synth::kNoLastNote;
		for (int32_t e = 0; e < kNumEnvelopes; e++) {
			setup.attack_is_zero[e] =
			    paramManager->getPatchedParamSet()->getValue(params::LOCAL_ENV_0_ATTACK + e) == -2147483648u;
		}
		std::copy_n(sound.params.sourceValues.begin(), setup.global_source_values.size(),
		            setup.global_source_values.begin());
		setup.initial_patching = sound.patchControlBlock(*paramManager, Sound::PresetGather::ALL);
	}
	else if (paramManager != nullptr) {
		setup.porta = paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_PORTAMENTO) != -2147483648;
	}

	for (int32_t s = 0; s < kNumSources; s++) {
		Source& source = sound.sources[s];
		SourceSetup& out = setup.sources[s];
		out.osc_type = source.oscType;
		out.ever_active = flags.checkSourceEverActive(s);
		// Not a wavetable's file: a wavetable plays at the source's own pitch
		const auto* sampleHolder = source.oscType == OscType::SAMPLE
		                               ? static_cast<const SampleHolderForVoice*>(voice.guides[s].audioFileHolder)
		                               : nullptr;
		out.transpose = sampleHolder != nullptr ? sampleHolder->transpose : source.transpose;
		out.fine_tuner = sampleHolder != nullptr ? sampleHolder->fineTuner : source.fineTuner;
		out.neutral_phase_increment = sampleHolder != nullptr ? sampleHolder->neutralPhaseIncrement : kMaxSampleValue;
		if (!forNoteChange && synthMode != SynthMode::FM && source.oscType == OscType::DX7 && out.ever_active) {
			out.dx_patch = source.ensureDxPatch();
		}
	}

	if (synthMode == SynthMode::FM) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			setup.modulator_audible[m] =
			    sound.getSmoothedPatchedParamValue(params::LOCAL_MODULATOR_0_VOLUME + m, *paramManager) != -2147483648;
		}
	}
	setup.last_note_code = sound.lastNoteCode;
	setup.monophonic_expression = sound.monophonicExpressionValues;
	return setup;
}

ReleaseInfo release_info_for(Sound& sound, ParamManagerForTimeline* paramManager, bool noteTailsAllowed) {
	ReleaseInfo release{
	    .note_tails_allowed = noteTailsAllowed,
	    .synth_mode = sound.getSynthMode(),
	    .num_unison = sound.snapshot().numUnison,
	    .voice_priority = sound.snapshot().voicePriority,
	    .num_active_voices = sound.numActiveVoices(),
	};
	if (noteTailsAllowed) {
		for (int32_t e = 0; e < kNumEnvelopes; e++) {
			release.envelope_has_sustain[e] = sound.envelopeHasSustainCurrently(e, paramManager);
		}
	}
	for (int32_t s = 0; s < kNumSources; s++) {
		release.osc_types[s] = sound.sources[s].oscType;
	}
	return release;
}

// Returns false if fail and we need to unassign again
bool Voice::noteOn(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
                   int32_t newNoteCodeAfterArpeggiation, uint8_t velocity, uint32_t newSampleSyncLength,
                   int32_t ticksLate, uint32_t samplesLate, bool resetEnvelopes, int32_t newFromMIDIChannel,
                   const int16_t* mpeValues) {
	checkStack("Voice::noteOn");
	orderSounded = lastSoundOrder++;

	auto* paramManager = static_cast<ParamManagerForTimeline*>(modelStack->paramManager);
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	const NoteSetup setup = note_setup_for(*this, sound, paramManager, *modelStack, newNoteCodeAfterArpeggiation,
	                                       newSampleSyncLength, ticksLate, false);

	// The samples' ports play this note's source through this voice's guides
	for (int32_t s = 0; s < kNumSources; s++) {
		if (sound.getSynthMode() != SynthMode::FM && sound.sources[s].oscType == OscType::SAMPLE
		    && guides[s].audioFileHolder) {
			for (int32_t u = 0; u < sound.snapshot().numUnison; u++) {
				unisonParts[u].sources[s].sampleVoiceAdapter.bind(&sound.sources[s], &guides[s]);
			}
		}
	}

	return note_on(setup, sound.snapshot(),
	               {
	                   .note_before_arp = newNoteCodeBeforeArpeggiation,
	                   .note_after_arp = newNoteCodeAfterArpeggiation,
	                   .velocity = velocity,
	                   .samples_late = samplesLate,
	                   .reset_envelopes = resetEnvelopes,
	                   .midi_channel = newFromMIDIChannel,
	                   .mpe = std::span<const int16_t, kNumExpressionDimensions>{mpeValues, kNumExpressionDimensions},
	               },
	               deluge::synth::VoiceClock{AudioEngine::nextVoiceState}, sound.numActiveVoices());
}

void Voice::changeNoteCode(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
                           int32_t newNoteCodeAfterArpeggiation, int32_t newInputMIDIChannel,
                           const int16_t* newMPEValues) {
	auto* paramManager = static_cast<ParamManagerForTimeline*>(modelStack->paramManager);
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);
	change_note(note_setup_for(*this, sound, paramManager, *modelStack, newNoteCodeAfterArpeggiation, 0, 0, true),
	            sound.snapshot(), newNoteCodeBeforeArpeggiation, newNoteCodeAfterArpeggiation, newInputMIDIChannel,
	            std::span<const int16_t, kNumExpressionDimensions>{newMPEValues, kNumExpressionDimensions});
}

void Voice::calculatePhaseIncrements(ModelStackWithSoundFlags* modelStack) {
	auto* paramManager = static_cast<ParamManagerForTimeline*>(modelStack->paramManager);
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);
	calculate_phase_increments(
	    note_setup_for(*this, sound, paramManager, *modelStack, noteCodeAfterArpeggiation, 0, 0, true),
	    sound.snapshot());
}

void Voice::noteOff(ParamManagerForTimeline* paramManager, bool noteTailsAllowed, bool allowReleaseStage) {
	for (int32_t s = 0; s < kNumSources; s++) {
		guides[s].noteOffReceived = true;
	}

	note_off(release_info_for(sound, paramManager, noteTailsAllowed), allowReleaseStage,
	         deluge::synth::VoiceClock{AudioEngine::nextVoiceState});

	for (int32_t s = 0; s < kNumSources; s++) {
		Source* source = &sound.sources[s];
		if (source->oscType == OscType::SAMPLE) {
			source->sampleControls.invertReversed = false; // Reset temporary flag back to normal
		}
	}
}

void Voice::randomizeOscPhases(const Sound& sound) {
	randomize_osc_phases(sound.snapshot());
}

void Voice::expressionEventImmediate(const Sound& sound, int32_t voiceLevelValue, int32_t s) {
	expressionEventImmediate(sound.monophonicExpressionValues[s - util::to_underlying(PatchSource::X)], voiceLevelValue,
	                         s);
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

	for (int32_t u = 0; u < sound.snapshot().numUnison; u++) {
		VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];

		if (voiceUnisonPartSource->active) {
			bool stillActive = voiceUnisonPartSource->sampleVoiceAdapter.zoneChanged(
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
	    && !paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(
	        *paramManager->getPatchedParamSet()->docForParam(params::LOCAL_NOISE_VOLUME), -2147483648)) {

		s = 1 - s;

		// The voice stays only if the other source can sound
		if (!modelStack->checkSourceEverActive(s)) {
			return false;
		}
	}

	return true;
}
