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
#include "model/model_stack.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/voice/voice_slot.h"
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

NoteSetup note_setup_for(VoiceSlot& voice, Sound& sound, ParamManagerForTimeline* paramManager,
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
