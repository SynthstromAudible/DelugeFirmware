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

/// @file
/// @brief A sound's voices as the sound drives them: each step builds what the kernel reads from the sound
/// (voice_setup.h) and hands it over, and the host the sound's voice pool draws on.

#include "io/debug/log.h"
#include "libdeluge/clock.h"
#include "libdeluge/system.h"
#include "memory/stack_guard.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/voice/voice_part.h"
#include "model/voice/voice_setup.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/param_dump.h"
#include "processing/sound/sound.h"
#include "storage/wave_table/wave_table.h"
#include "util/synth_noise.h"
#include <memory>
#include <new>

using deluge::synth::SourceSlot;

namespace params = deluge::modulation::params;

namespace {
uint32_t lastSoundOrder = 0;
} // namespace

void Sound::bindVoice(SoundVoice& voice) {
	for (int32_t u = 0; u < kMaxNumVoicesUnison; u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			// The slots belong to the Sound, whose oscillators hold the histories: they outlive every slot bound here
			voice.kernel.unison_parts[u].slots[s].bind({.sample = &voice.part.ports(u, s).sample,
			                                            .input = &voice.part.ports(u, s).input,
			                                            .live_inputs = &sourceResources[s].liveInputs.inputs()});
		}
		voice.kernel.bind_dx7(u, &voice.part.dx7_port(u));
	}
}

uint32_t Sound::voiceRating(const SoundVoice& voice) const {
	return voice.kernel.priority_rating(synth_.config().voicing.priority, numActiveVoices());
}

void Sound::calculateVoicePhaseIncrements(SoundVoice& voice, ModelStackWithSoundFlags& flags) {
	auto* paramManager = static_cast<ParamManagerForTimeline*>(flags.paramManager);
	voice.kernel.calculate_phase_increments(
	    note_setup_for(voice, *this, paramManager, flags, voice.kernel.note_code(), 0, 0, true), synth_.config());
}

deluge::synth::ReleaseInfo Sound::prepareVoiceRelease(SoundVoice& voice, ParamManagerForTimeline* paramManager,
                                                      bool noteTailsAllowed) {
	for (int32_t s = 0; s < kNumSources; s++) {
		voice.part.guides[s].noteOffReceived = true;
	}
	return release_info_for(*this, paramManager, noteTailsAllowed);
}

void Sound::voiceReleased() {
	for (int32_t s = 0; s < kNumSources; s++) {
		SourceResources* source = &sourceResources[s];
		if (oscType(s) == OscType::SAMPLE) {
			source->invertReversed = false; // Reset temporary flag back to normal
		}
	}
}

void Sound::voiceExpressionImmediate(SoundVoice& voice, int32_t voiceLevelValue, int32_t s) {
	voice.kernel.expression_event_immediate(monophonicExpressionValues[s - util::to_underlying(PatchSource::X)],
	                                        voiceLevelValue, s);
}

void Sound::dumpVoiceSources(const SoundVoice& voice, const deluge::synth::matrix::MatrixBlock& controlBlock,
                             const deluge::synth::SoundConfig& config, const deluge::synth::BlockValues& cfg) const {
#if defined(DELUGE_PARAM_DUMP)
	std::array<deluge::param_dump::SourceState, kMaxNumVoicesUnison * kNumSources> states{};
	size_t n = 0;
	for (int32_t u = 0; u < synth_.config().voicing.unison.count; u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			const auto& slot = voice.kernel.unison_parts[u].slots[s];
			states[n++] = {
			    .part = static_cast<uint8_t>(u),
			    .source = static_cast<uint8_t>(s),
			    .active = slot.active,
			    .cache = config.sources[s].osc_type == OscType::SAMPLE
			             && voice.kernel.pitch_mod_disallows_cache(controlBlock, cfg, s),
			    .phase_increment = slot.phase_increment_stored_value.raw(), // the dump records the word
			};
		}
	}
	deluge::param_dump::voice_sources(this, &voice, std::span{states}.first(n));
#else
	(void)voice;
	(void)controlBlock;
	(void)config;
	(void)cfg;
#endif
}

deluge::synth::VoiceBlock Sound::voiceBlock(const SoundVoice& voice) const {
	deluge::synth::VoiceBlock block{.num_active_voices = numActiveVoices(), .clock = AudioEngine::voiceClock()};
	for (int32_t s = 0; s < kNumSources; s++) {
		AudioFileHolder* holder = voice.part.guides[s].audioFileHolder;
		const OscType type = oscType(s);
		if (type == OscType::WAVETABLE && holder != nullptr) {
			block.wave_tables[s] = static_cast<WaveTable*>(holder->audioFile)->view();
		}
		// A sample with a loop end obeys it until the note-off, so it cannot run out before then
		block.sample_can_auto_release[s] =
		    type == OscType::SAMPLE && holder != nullptr
		    && !(static_cast<SampleHolderForVoice*>(holder)->loopEndPos && !voice.part.guides[s].noteOffReceived);
		block.source_in_stereo[s] =
		    SourceResources::renderInStereo(type, synth_.config().voicing.unison_spread,
		                                    synth_.config().voicing.unison.count, static_cast<SampleHolder*>(holder));
	}
	block.dx7_in_stereo = SourceResources::renderInStereo(OscType::DX7, synth_.config().voicing.unison_spread,
	                                                      synth_.config().voicing.unison.count, nullptr);
	return block;
}

// Unusually, modelStack may be null: unassigning every voice on a song swap has none.
void Sound::unassignVoice(SoundVoice& voice, ModelStackWithSoundFlags* modelStack) {
	voice.kernel.unassign_sources(synth_.config().voicing.unison.count, false);
	voiceUnassigned(modelStack);
}

// Returns false if voice needs unassigning now
bool Sound::voiceSampleZoneChanged(SoundVoice& voice, ModelStackWithSoundFlags& flags, int32_t s,
                                   MarkerType markerType) {
	ModelStackWithSoundFlags* modelStack = &flags;

	AudioFileHolder* holder = voice.part.guides[s].audioFileHolder;
	if (!holder) {
		return true; // If no holder, that means this Source/Sample is not currently playing, e.g. because its volume
		             // was set to 0.
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	Sample* sample = (Sample*)holder->audioFile;

	const bool reversed = playsReversed(s);
	voice.part.guides[s].setupPlaybackBounds(reversed);

	LoopType loopingType = voice.part.guides[s].getLoopingType(synth_.config().sources[s].repeat_mode);

	// Check we're still within bounds - for each unison part.
	// Well, that is, make sure we're not past the new end. Being before the start is ok, because we'll come back into
	// the still-remaining part soon enough.

	bool anyStillActive = false;

	for (int32_t u = 0; u < synth_.config().voicing.unison.count; u++) {
		SourceSlot* voiceUnisonPartSource = &voice.kernel.unison_parts[u].slots[s];

		if (voiceUnisonPartSource->active) {
			bool stillActive = voice.part.ports(u, s).sample.zoneChanged(&voice.part.guides[s], sample, reversed,
			                                                             markerType, loopingType, voiceRating(voice));
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
	        paramManager->getPatchedParamSet()->playedLane(params::LOCAL_NOISE_VOLUME), -2147483648)) {

		s = 1 - s;

		// The voice stays only if the other source can sound
		if (!modelStack->checkSourceEverActive(s)) {
			return false;
		}
	}

	return true;
}

SoundVoice* SoundVoiceHost::acquire() const {
	SoundVoice* voice = nullptr;
	try {
		voice = AudioEngine::VoicePool::get().acquire_for_overwrite().release();
	} catch (const deluge::exception&) {
		return nullptr;
	}
	sound->bindVoice(*voice);
	return voice;
}

void SoundVoiceHost::unassign(SoundVoice& voice, Context modelStack) const {
	sound->unassignVoice(voice, modelStack);
}

void SoundVoiceHost::release(SoundVoice& voice) const noexcept {
	sound->unassignVoice(voice, nullptr); // the slot's last unassign, as every free path ends with one
	AudioEngine::VoicePool::recycle(&voice);
}

void SoundVoiceHost::renew(SoundVoice& voice) const noexcept {
	sound->unassignVoice(voice, nullptr);
	std::destroy_at(&voice);
	::new (static_cast<void*>(&voice)) SoundVoice; // default-initialised, not zeroed
	sound->bindVoice(voice);
}

void SoundVoiceHost::admit(uint32_t cycles) const {
	AudioEngine::admitVoice(sound->predictedVoiceCycles() + (deluge_in_audio_context() ? cycles : 0));
}

void SoundVoiceHost::voice_starting() const {
	if (sound->sideChainSendLevel != 0) [[unlikely]] {
		AudioEngine::registerSideChainHit(sound->sideChainSendLevel);
	}
}

void SoundVoiceHost::voices_changed(Context modelStack) const {
	sound->reassessRenderSkippingStatus(modelStack);
}

void SoundVoiceHost::note_edit() {
	Sound::noteVoicesEdit();
}

deluge::synth::VoiceClock SoundVoiceHost::clock() {
	return AudioEngine::voiceClock();
}

deluge::synth::NoteSetup SoundVoiceHost::prepare(SoundVoice& voice, Context modelStack, const Event& event,
                                                 uint32_t sampleSyncLength, int32_t ticksLate) const {
	checkStack("SoundVoiceHost::prepare");
	auto* paramManager = static_cast<ParamManagerForTimeline*>(modelStack->paramManager);
	deluge::synth::NoteSetup setup = note_setup_for(voice, *sound, paramManager, *modelStack, event.note_after_arp,
	                                                sampleSyncLength, ticksLate, false);

	// The samples' ports play this note's source through this voice's guides
	const deluge::synth::SoundConfig& config = sound->config();
	for (int32_t s = 0; s < kNumSources; s++) {
		if (sound->playsSlots() && sound->oscType(s) == OscType::SAMPLE && voice.part.guides[s].audioFileHolder) {
			for (int32_t u = 0; u < config.voicing.unison.count; u++) {
				voice.part.ports(u, s).sample.bind(&config.sources[s], &voice.part.guides[s]);
			}
		}
	}
	return setup;
}

deluge::synth::NoteSetup SoundVoiceHost::prepare_change(SoundVoice& voice, Context modelStack, int32_t note) const {
	auto* paramManager = static_cast<ParamManagerForTimeline*>(modelStack->paramManager);
	return note_setup_for(voice, *sound, paramManager, *modelStack, note, 0, 0, true);
}

uint32_t SoundVoiceHost::next_voice_order() {
	return lastSoundOrder++;
}

uint32_t SoundVoiceHost::cycles() {
	return deluge_work_cycle_count(); // what a note-on costs, not the USB interrupt inside it
}

void SoundVoiceHost::note_started(SoundVoice& voice) const {
	deluge::param_dump::note_on(sound, &voice);
	deluge::param_dump::plane(GLOBALITY_LOCAL, sound, &voice, voice.kernel.param_final_values());
}

SoundVoiceHost::Noise& SoundVoiceHost::noise() {
	return deluge::synth_noise;
}

uint32_t SoundVoiceHost::steal_increment() {
	return kSoftCullIncrement;
}

deluge::synth::ReleaseInfo SoundVoiceHost::prepare_release(SoundVoice& voice, Context modelStack,
                                                           bool noteTailsAllowed) const {
	return sound->prepareVoiceRelease(voice, static_cast<ParamManagerForTimeline*>(modelStack->paramManager),
	                                  noteTailsAllowed);
}

void SoundVoiceHost::note_released(SoundVoice& /*voice*/) const {
	sound->voiceReleased();
}

void SoundVoiceHost::last_note(int32_t note) const {
	sound->lastNoteCode = note;
}

deluge::synth::VoiceBlock SoundVoiceHost::voice_block(SoundVoice& voice) const {
	return sound->voiceBlock(voice);
}

uint32_t SoundVoiceHost::sources_changed() const {
	return sound->sourcesChanged.load(std::memory_order_acquire);
}

void SoundVoiceHost::voice_rendered(SoundVoice& voice, const deluge::synth::matrix::MatrixBlock& controlBlock,
                                    const deluge::synth::SoundConfig& config,
                                    const deluge::synth::BlockValues& values) const {
	deluge::param_dump::plane(GLOBALITY_LOCAL, sound, &voice, voice.kernel.param_final_values());
	sound->dumpVoiceSources(voice, controlBlock, config, values);
}

#if defined(DELUGE_STREAM_BENCH)
void SoundVoiceHost::countVoice(uint32_t cycles, bool renders) const {
	const auto kind = static_cast<size_t>(sound->voiceKind());
	deluge::bench::render_profile::g_totals.voice_render += cycles;
	++deluge::bench::render_profile::g_totals.voice_blocks;
	deluge::bench::render_profile::g_totals.voice_render_by_kind[kind] += cycles;
	deluge::bench::render_profile::g_totals.voice_frames_by_kind[kind] += renders ? benchFrames : 0;
}
#endif
