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

#include "model/voice/voice_slot.h"
#include "io/debug/log.h"
#include "memory/stack_guard.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/voice/voice_setup.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/wave_table/wave_table.h"

namespace params = deluge::modulation::params;
using deluge::synth::NoteOnEvent;
using deluge::synth::VoiceClock;

namespace {
uint32_t lastSoundOrder = 0;

/// The clock every voice's envelope stamps are taken from.
VoiceClock engineClock() {
	return VoiceClock{AudioEngine::nextVoiceState};
}
} // namespace

VoiceSlot::VoiceSlot(Sound& sound) : sound{sound} {
	for (int32_t u = 0; u < kMaxNumVoicesUnison; u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			kernel.unison_parts[u].sources[s].bind(ports_[u][s]);
		}
	}
}

uint32_t VoiceSlot::priority_rating() const {
	return kernel.priority_rating(sound.snapshot().voice_priority, sound.numActiveVoices());
}

// Returns false if fail and we need to unassign again
bool VoiceSlot::note_on(ModelStackWithSoundFlags& flags, const NoteOnEvent& event, uint32_t sample_sync_length,
                        int32_t ticks_late) {
	checkStack("Voice::noteOn");
	order_sounded = lastSoundOrder++;

	auto* paramManager = static_cast<ParamManagerForTimeline*>(flags.paramManager);
	const deluge::synth::NoteSetup setup =
	    note_setup_for(*this, sound, paramManager, flags, event.note_after_arp, sample_sync_length, ticks_late, false);

	// The samples' ports play this note's source through this voice's guides
	for (int32_t s = 0; s < kNumSources; s++) {
		if (sound.getSynthMode() != SynthMode::FM && sound.sources[s].oscType == OscType::SAMPLE
		    && guides[s].audioFileHolder) {
			for (int32_t u = 0; u < sound.snapshot().num_unison; u++) {
				ports(u, s).sample.bind(&sound.sources[s], &guides[s]);
			}
		}
	}

	return kernel.note_on(setup, sound.snapshot(), event, engineClock(), sound.numActiveVoices());
}

void VoiceSlot::change_note(ModelStackWithSoundFlags& flags, int32_t note_before_arp, int32_t note_after_arp,
                            int32_t midi_channel, std::span<const int16_t, kNumExpressionDimensions> mpe) {
	auto* paramManager = static_cast<ParamManagerForTimeline*>(flags.paramManager);
	kernel.change_note(note_setup_for(*this, sound, paramManager, flags, note_after_arp, 0, 0, true), sound.snapshot(),
	                   note_before_arp, note_after_arp, midi_channel, mpe);
}

void VoiceSlot::calculate_phase_increments(ModelStackWithSoundFlags& flags) {
	auto* paramManager = static_cast<ParamManagerForTimeline*>(flags.paramManager);
	kernel.calculate_phase_increments(
	    note_setup_for(*this, sound, paramManager, flags, kernel.note_code_after_arpeggiation, 0, 0, true),
	    sound.snapshot());
}

void VoiceSlot::note_off(ParamManagerForTimeline* paramManager, bool note_tails_allowed, bool allow_release_stage) {
	for (int32_t s = 0; s < kNumSources; s++) {
		guides[s].noteOffReceived = true;
	}

	kernel.note_off(release_info_for(sound, paramManager, note_tails_allowed), allow_release_stage, engineClock());

	for (int32_t s = 0; s < kNumSources; s++) {
		Source* source = &sound.sources[s];
		if (source->oscType == OscType::SAMPLE) {
			source->sampleControls.invertReversed = false; // Reset temporary flag back to normal
		}
	}
}

void VoiceSlot::randomize_osc_phases() {
	kernel.randomize_osc_phases(sound.snapshot());
}

void VoiceSlot::expression_event_immediate(int32_t voice_level_value, int32_t s) {
	kernel.expression_event_immediate(sound.monophonicExpressionValues[s - util::to_underlying(PatchSource::X)],
	                                  voice_level_value, s);
}

bool VoiceSlot::render(const ControlBlock& control_block, const VoiceConfig& cfg,
                       const deluge::synth::VoiceRenderArgs& args) {
	if (!just_created) {
		just_created = true;
		return true;
	}
	checkStack("Voice::render");
	return kernel.render(control_block, cfg, block(), args);
}

deluge::synth::VoiceBlock VoiceSlot::block() const {
	deluge::synth::VoiceBlock block{.num_active_voices = sound.numActiveVoices(), .clock = engineClock()};
	for (int32_t s = 0; s < kNumSources; s++) {
		AudioFileHolder* holder = guides[s].audioFileHolder;
		const OscType oscType = sound.sources[s].oscType;
		if (oscType == OscType::WAVETABLE && holder != nullptr) {
			block.wave_tables[s] = static_cast<WaveTable*>(holder->audioFile)->view();
		}
		// A sample with a loop end obeys it until the note-off, so it cannot run out before then
		block.sample_can_auto_release[s] =
		    oscType == OscType::SAMPLE && holder != nullptr
		    && !(static_cast<SampleHolderForVoice*>(holder)->loopEndPos && !guides[s].noteOffReceived);
		block.source_in_stereo[s] =
		    Source::renderInStereo(oscType, sound.snapshot().unison_stereo_spread, sound.snapshot().num_unison,
		                           static_cast<SampleHolder*>(holder));
	}
	return block;
}

bool VoiceSlot::do_fast_release(uint32_t release_increment) {
	return kernel.do_fast_release(release_increment, engineClock());
}

bool VoiceSlot::speed_up_release() {
	return kernel.speed_up_release(engineClock());
}

bool VoiceSlot::do_immediate_release() {
	return kernel.do_immediate_release(engineClock());
}

// Unusually, flags may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have it.
// You'll normally want to call audioDriver.voiceUnassigned() after this.
void VoiceSlot::set_as_unassigned(ModelStackWithSoundFlags* flags, bool deleting_song) {
	unassign_stuff(deleting_song);
	delete_this_voice_ = true;
	if (!deleting_song) {
		sound.voiceUnassigned(flags);
	}
}

void VoiceSlot::unassign_stuff(bool deleting_song) {
	for (int32_t s = 0; s < kNumSources; s++) {
		for (int32_t u = 0; u < sound.snapshot().num_unison; u++) {
			kernel.unison_parts[u].sources[s].unassign(deleting_song);
		}
	}
}

// Returns false if voice needs unassigning now
bool VoiceSlot::sample_zone_changed(ModelStackWithSoundFlags& flags, int32_t s, MarkerType markerType) {
	ModelStackWithSoundFlags* modelStack = &flags;

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

	for (int32_t u = 0; u < sound.snapshot().num_unison; u++) {
		VoiceUnisonPartSource* voiceUnisonPartSource = &kernel.unison_parts[u].sources[s];

		if (voiceUnisonPartSource->active) {
			bool stillActive =
			    ports(u, s).sample.zoneChanged(&guides[s], sample, source.sampleControls.isCurrentlyReversed(),
			                                   markerType, loopingType, priority_rating());
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
