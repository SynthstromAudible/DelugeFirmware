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

#include "processing/sound/sound.h"
#include "bench/render_profile.h"
#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "dsp-next/fixed/vca/pan.h"
#include "effects/codec.h"
#include "effects/filter_slots.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "instruments/deluge/codec.h"
#include "instruments/deluge/sound_state.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/system.h"
#include "memory/fast_allocator.h"
#include "memory/general_memory_allocator.h"
#include "memory/stack_guard.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/model_stack.h"
#include "model/project_bridge/cable_bridge.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/sample/sample_low_level_reader.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_slot.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/dx7/dx_engine.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "processing/sound/sound_chain_inputs.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include "storage/wave_table/wave_table.h"
#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_fixed.h"
#include "synth/modulation/envelope.h"
#include "util/exceptions.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/misc.h"
#include "util/structural_edit_probe.h"
#include "util/synth_noise.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>

using deluge::synth::SourceSlot;
using deluge::synth::VoiceConfig;

namespace params = deluge::modulation::params;

// Defined in processing/render_scratch.cpp: the render scratch shared engine-wide, handed to each voice's render.
extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

namespace {
/// Sounds destroyed while they still had live voices. Always on; relaxed, because it is evidence, not synchronisation:
/// a Sound may be destroyed on one tier while the count is read on another.
std::atomic<int32_t> g_soundsDestroyedWithLiveVoices{0};
} // namespace

void Sound::noteDestroyedWithLiveVoices() {
	g_soundsDestroyedWithLiveVoices.fetch_add(1, std::memory_order_relaxed);
}

int32_t Sound::soundsDestroyedWithLiveVoices() {
	return g_soundsDestroyedWithLiveVoices.load(std::memory_order_relaxed);
}

namespace {
/// A note's MPE values as the voice reads them.
std::span<const int16_t, kNumExpressionDimensions> mpeSpan(const int16_t* mpeValues) {
	return std::span<const int16_t, kNumExpressionDimensions>{mpeValues, kNumExpressionDimensions};
}

deluge::synth::NoteOnEvent noteOnEvent(int32_t noteCodePreArp, int32_t noteCodePostArp, int32_t velocity,
                                       uint32_t samplesLate, bool resetEnvelopes, int32_t fromMIDIChannel,
                                       const int16_t* mpeValues) {
	return {
	    .note_before_arp = noteCodePreArp,
	    .note_after_arp = noteCodePostArp,
	    .velocity = static_cast<uint8_t>(velocity),
	    .samples_late = samplesLate,
	    .reset_envelopes = resetEnvelopes,
	    .midi_channel = fromMIDIChannel,
	    .mpe = mpeSpan(mpeValues),
	};
}
} // namespace

Sound::Sound()
    : chain_{deluge::model::kModFxRingAllocator, deluge::model::kGranularAllocator, deluge::model::delayArena()} {
	unpatchedParamKind_ = params::Kind::UNPATCHED_SOUND;

	// CONTAINMENT for the cross-tier race on voices_. The audio render runs on a
	// preemptive interrupt executor and iterates every Sound's voices_ while culling; an
	// interaction-tier note-on can push_back into that same vector at any instruction. If the
	// push_back reallocates, the audio tier's in-flight traversal walks freed memory.
	//
	// Reserving the whole pool up front makes voices_'s buffer address IMMUTABLE for this Sound's
	// lifetime, so no push_back can ever move it. Bound is the pool's capacity rather than
	// maxVoiceCount, deliberately: a note-on's publish only counts !isCullFading() voices toward
	// maxVoiceCount, so cull-fading voices occupy slots without counting and size() can legally
	// exceed it. Nothing reduces capacity afterwards — there is no shrink_to_fit, no voices_
	// assignment and no Sound::operator= — so the guarantee holds for good.
	//
	// Done HERE and nowhere else because this is the one provably safe point: a Sound under
	// construction is not yet reachable from the audio tier, so this reservation's own allocation
	// cannot race a traversal. Reserving lazily on the first publish would reallocate at
	// exactly the moment the hazard exists.
	//
	// This is containment, not a fix. It closes the memory-corruption path only; a raw
	// ActiveVoice& held across voice->note_on() can still be erased mid-call (that is what fires
	// E199), and pool-recycled Voice addresses still ABA. Both need the voice-identity model the
	// audio-engine overhaul introduces, and that work is expected to delete this reserve.
	voices_.reserve(AudioEngine::VoicePool::get().capacity());

	assignModFXType(ModFXType::NONE);

	setLpfMode(FilterMode::TRANSISTOR_24DB); // Good for samples, I think

	// LFO
	modKnobs[0][1].paramDescriptor.setToHaveParamOnly(params::GLOBAL_VOLUME_POST_FX);
	modKnobs[0][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_PAN);

	modKnobs[1][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
	modKnobs[1][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_RESONANCE);

	modKnobs[2][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_ENV_0_ATTACK);
	modKnobs[2][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_ENV_0_RELEASE);

	modKnobs[3][1].paramDescriptor.setToHaveParamOnly(params::GLOBAL_DELAY_RATE);
	modKnobs[3][0].paramDescriptor.setToHaveParamOnly(params::GLOBAL_DELAY_FEEDBACK);

	modKnobs[4][0].paramDescriptor.setToHaveParamOnly(params::GLOBAL_REVERB_AMOUNT);

	modKnobs[5][1].paramDescriptor.setToHaveParamOnly(params::GLOBAL_LFO_FREQ_1);

	modKnobs[4][1].paramDescriptor.setToHaveParamAndSource(params::GLOBAL_VOLUME_POST_REVERB_SEND,
	                                                       PatchSource::SIDECHAIN);
	modKnobs[5][0].paramDescriptor.setToHaveParamAndSource(params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1);

	modKnobs[6][1].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_STUTTER_RATE);
	modKnobs[6][0].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO);

	modKnobs[7][1].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START
	                                                  + params::UNPATCHED_SAMPLE_RATE_REDUCTION);
	modKnobs[7][0].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_BITCRUSHING);
	expressionSourcesChangedAtSynthLevel.reset();

	paramLPF.p = PARAM_LPF_OFF;

	doneReadingFromFile();
	AudioEngine::sounds.add(this);
}

void Sound::initParams(ParamManager* paramManager) {

	ModControllableAudio::initParams(paramManager);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = params::Kind::UNPATCHED_SOUND;

	unpatchedParams->params[params::UNPATCHED_PORTAMENTO].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_PORTAMENTO), -2147483648);

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[params::LOCAL_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_VOLUME), 0);
	patchedParams->params[params::LOCAL_OSC_A_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_A_VOLUME), 2147483647);
	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_B_VOLUME), 2147483647);
	patchedParams->params[params::GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_VOLUME_POST_FX),
	    getParamFromUserValue(params::GLOBAL_VOLUME_POST_FX, kDefaultSoundVolumeUserValue));
	patchedParams->params[params::GLOBAL_VOLUME_POST_REVERB_SEND].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_VOLUME_POST_REVERB_SEND), 0);
	patchedParams->params[params::LOCAL_FOLD].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_FOLD), -2147483648);
	patchedParams->params[params::LOCAL_HPF_RESONANCE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_HPF_RESONANCE), -2147483648);
	patchedParams->params[params::LOCAL_HPF_FREQ].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_HPF_FREQ), -2147483648);
	patchedParams->params[params::LOCAL_HPF_MORPH].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_HPF_MORPH), -2147483648);
	patchedParams->params[params::LOCAL_LPF_MORPH].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LPF_MORPH), -2147483648);
	patchedParams->params[params::LOCAL_PITCH_ADJUST].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_PITCH_ADJUST), 0);
	patchedParams->params[params::GLOBAL_REVERB_AMOUNT].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_REVERB_AMOUNT), -2147483648);
	patchedParams->params[params::GLOBAL_DELAY_RATE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_DELAY_RATE), 0);
	patchedParams->params[params::GLOBAL_ARP_RATE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_ARP_RATE), 0);
	patchedParams->params[params::GLOBAL_DELAY_FEEDBACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_DELAY_FEEDBACK), -2147483648);
	patchedParams->params[params::LOCAL_CARRIER_0_FEEDBACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_CARRIER_0_FEEDBACK), -2147483648);
	patchedParams->params[params::LOCAL_CARRIER_1_FEEDBACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_CARRIER_1_FEEDBACK), -2147483648);
	patchedParams->params[params::LOCAL_MODULATOR_0_FEEDBACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_MODULATOR_0_FEEDBACK), -2147483648);
	patchedParams->params[params::LOCAL_MODULATOR_1_FEEDBACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_MODULATOR_1_FEEDBACK), -2147483648);
	patchedParams->params[params::LOCAL_MODULATOR_0_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_MODULATOR_0_VOLUME), -2147483648);
	patchedParams->params[params::LOCAL_MODULATOR_1_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_MODULATOR_1_VOLUME), -2147483648);
	patchedParams->params[params::LOCAL_OSC_A_PHASE_WIDTH].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_A_PHASE_WIDTH), 0);
	patchedParams->params[params::LOCAL_OSC_B_PHASE_WIDTH].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_B_PHASE_WIDTH), 0);
	patchedParams->params[params::LOCAL_ENV_1_ATTACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_1_ATTACK), getParamFromUserValue(params::LOCAL_ENV_1_ATTACK, 20));
	patchedParams->params[params::LOCAL_ENV_1_DECAY].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_1_DECAY), getParamFromUserValue(params::LOCAL_ENV_1_DECAY, 20));
	patchedParams->params[params::LOCAL_ENV_1_SUSTAIN].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_1_SUSTAIN),
	    getParamFromUserValue(params::LOCAL_ENV_1_SUSTAIN, 25));
	patchedParams->params[params::LOCAL_ENV_1_RELEASE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_1_RELEASE),
	    getParamFromUserValue(params::LOCAL_ENV_1_RELEASE, 20));
	patchedParams->params[params::LOCAL_LFO_LOCAL_FREQ_1].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LFO_LOCAL_FREQ_1), 0);
	patchedParams->params[params::GLOBAL_LFO_FREQ_1].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_LFO_FREQ_1), getParamFromUserValue(params::GLOBAL_LFO_FREQ_1, 30));
	patchedParams->params[params::LOCAL_LFO_LOCAL_FREQ_2].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LFO_LOCAL_FREQ_2), 0);
	patchedParams->params[params::GLOBAL_LFO_FREQ_2].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_LFO_FREQ_2), getParamFromUserValue(params::GLOBAL_LFO_FREQ_2, 30));
	patchedParams->params[params::LOCAL_PAN].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_PAN), 0);
	patchedParams->params[params::LOCAL_NOISE_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_NOISE_VOLUME), -2147483648);
	patchedParams->params[params::GLOBAL_MOD_FX_DEPTH].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_MOD_FX_DEPTH), 0);
	patchedParams->params[params::GLOBAL_MOD_FX_RATE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_MOD_FX_RATE), 0);
	patchedParams->params[params::LOCAL_OSC_A_PITCH_ADJUST].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_A_PITCH_ADJUST), 0); // Don't change
	patchedParams->params[params::LOCAL_OSC_B_PITCH_ADJUST].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_B_PITCH_ADJUST), 0); // Don't change
	patchedParams->params[params::LOCAL_MODULATOR_0_PITCH_ADJUST].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_MODULATOR_0_PITCH_ADJUST), 0); // Don't change
	patchedParams->params[params::LOCAL_MODULATOR_1_PITCH_ADJUST].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_MODULATOR_1_PITCH_ADJUST), 0); // Don't change
}

void Sound::setupAsSample(ParamManagerForTimeline* paramManager) {

	polyphonic = PolyphonyMode::AUTO;
	setLpfMode(FilterMode::TRANSISTOR_24DB);

	sources[0].oscType = OscType::SAMPLE;
	sources[1].oscType = OscType::SAMPLE;

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_B_VOLUME), -2147483648);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_ATTACK), getParamFromUserValue(params::LOCAL_ENV_0_ATTACK, 0));
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_DECAY), getParamFromUserValue(params::LOCAL_ENV_0_DECAY, 20));
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_SUSTAIN),
	    getParamFromUserValue(params::LOCAL_ENV_0_SUSTAIN, 50));
	patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE),
	    getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 0));

	patchedParams->params[params::LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LPF_RESONANCE), -2147483648);
	patchedParams->params[params::LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LPF_FREQ), 2147483647);

	modKnobs[6][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_PITCH_ADJUST);

	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();
	patchCableSet->truncateCables(0);
	patchCableSet->appendDefaultCable(PatchSource::VELOCITY, params::LOCAL_VOLUME,
	                                  getParamFromUserValue(params::PATCH_CABLE, kSampleDrumVelocityCableUserValue));

	setupDefaultExpressionPatching(paramManager);

	doneReadingFromFile();
}

void Sound::setupAsDefaultSynth(ParamManager* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_B_VOLUME), 0x47AE1457);
	patchedParams->params[params::LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LPF_RESONANCE), 0xA2000000);
	patchedParams->params[params::LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LPF_FREQ), 0x10000000);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_ATTACK), 0x80000000);
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_DECAY), 0xE6666654);
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_SUSTAIN), 0x7FFFFFFF);
	patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE), 0x851EB851);
	patchedParams->params[params::LOCAL_ENV_1_ATTACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_1_ATTACK), 0xA3D70A37);
	patchedParams->params[params::LOCAL_ENV_1_DECAY].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_1_DECAY), 0xA3D70A37);
	patchedParams->params[params::LOCAL_ENV_1_SUSTAIN].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_1_SUSTAIN), 0xFFFFFFE9);
	patchedParams->params[params::LOCAL_ENV_1_RELEASE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_1_RELEASE), 0xE6666654);
	patchedParams->params[params::GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::GLOBAL_VOLUME_POST_FX), 0x50000000);

	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();
	patchCableSet->truncateCables(0);
	struct DefaultCable {
		PatchSource from;
		uint8_t to;
		int32_t amount;
	};
	for (DefaultCable const& d : {DefaultCable{PatchSource::NOTE, params::LOCAL_LPF_FREQ, 0x08F5C28C},
	                              DefaultCable{PatchSource::ENVELOPE_1, params::LOCAL_LPF_FREQ, 0x1C28F5B8},
	                              DefaultCable{PatchSource::VELOCITY, params::LOCAL_LPF_FREQ, 0x0F5C28F0},
	                              DefaultCable{PatchSource::VELOCITY, params::LOCAL_VOLUME, 0x3FFFFFE8}}) {
		patchCableSet->appendDefaultCable(d.from, d.to, d.amount);
	}

	setupDefaultExpressionPatching(paramManager);

	setLpfMode(FilterMode::TRANSISTOR_24DB); // Good for samples, I think

	sources[0].oscType = OscType::SAW;
	sources[1].transpose = -12;

	assignNumUnison(4);
	assignUnisonDetune(10);

	setTranspose(-12); // unbound at this setup point → writes the snapshot

	doneReadingFromFile();
}

void Sound::setupDefaultExpressionPatching(ParamManager* paramManager) {
	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();

	if (patchCableSet->appendDefaultCable(PatchSource::AFTERTOUCH, params::LOCAL_VOLUME,
	                                      getParamFromUserValue(params::PATCH_CABLE, 33))
	    == nullptr) {
		return;
	}
	if (getSynthMode() == SynthMode::FM) {
		patchCableSet->appendDefaultCable(PatchSource::Y, params::LOCAL_MODULATOR_0_VOLUME,
		                                  getParamFromUserValue(params::PATCH_CABLE, 15));
	}
	else {
		patchCableSet->appendDefaultCable(PatchSource::Y, params::LOCAL_LPF_FREQ,
		                                  getParamFromUserValue(params::PATCH_CABLE, 20));
	}
}

void Sound::setupAsBlankSynth(ParamManager* paramManager, bool is_dx) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_OSC_B_VOLUME), -2147483648);
	patchedParams->params[params::LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LPF_FREQ), 2147483647);
	patchedParams->params[params::LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_LPF_RESONANCE), -2147483648);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_ATTACK), -2147483648);
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_DECAY), getParamFromUserValue(params::LOCAL_ENV_0_DECAY, 20));
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
	    *patchedParams->docForParam(params::LOCAL_ENV_0_SUSTAIN), 2147483647);
	if (is_dx) {
		sources[0].oscType = OscType::DX7;
		sources[0].ensureDxPatch(); // initializes DX engine if this is the first dx7patch
		// velocity is forwarded to dx7 engine, don't do master volume
		paramManager->getPatchCableSet()->truncateCables(0);
		patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    *patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE), 2147483647); // 30 ish
	}
	else {
		patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    *patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE), -2147483648);

		PatchCableSet* patchCableSet = paramManager->getPatchCableSet();
		patchCableSet->truncateCables(0);
		patchCableSet->appendDefaultCable(PatchSource::VELOCITY, params::LOCAL_VOLUME,
		                                  getParamFromUserValue(params::PATCH_CABLE, 50));
	}

	setupDefaultExpressionPatching(paramManager);

	doneReadingFromFile();
}

ModFXType Sound::getModFXType() {
	return modControllableSnapshot_.effects.mod_fx.type;
}

void Sound::patchedParamPresetValueChanged(uint8_t p, ModelStackWithSoundFlags* modelStack, int32_t oldValue,
                                           int32_t newValue) {

	recalculatePatchingToParam(p, (ParamManagerForTimeline*)modelStack->paramManager);

	// If we just enabled an oscillator, we need to calculate voices' phase increments
	if (oldValue == -2147483648 && newValue != -2147483648) {

		// This will make inactive any voiceSources which currently have no volume. Ideally we'd only tell it to do the
		// consideration for the oscillator in question, but oh well

		switch (p) {
		case params::LOCAL_OSC_A_VOLUME:
		case params::LOCAL_OSC_B_VOLUME:
		case params::LOCAL_MODULATOR_0_VOLUME:
		case params::LOCAL_MODULATOR_1_VOLUME:
			recalculateAllVoicePhaseIncrements(modelStack);
		}
	}
}

deluge::synth::matrix::MatrixBlock Sound::matrixBlock(ParamManager& paramManager, PresetGather gather) const {
	deluge::synth::matrix::MatrixBlock block; // only what the gather fills is read
	PatchCableSet& cables = *paramManager.getPatchCableSet();
	block.program = cables.program();
	const deluge::synth::PatchRouting routing = cables.routing();
	PatchedParamSet& patched = *paramManager.getPatchedParamSet();
	if (gather == PresetGather::ALL) {
		for (int32_t p = 0; p < deluge::modulation::params::kNumParams; p++) {
			block.preset_values[p] = patched.getValue(p);
		}
	}
	else {
		for (const deluge::synth::RoutedDestination* list : routing.destinations) {
			for (const deluge::synth::RoutedDestination* d = list; d != nullptr && d->sources != 0; d++) {
				if (d->target.is_param()) {
					block.preset_values[d->target.param_index()] = patched.getValue(d->target.param_index());
				}
			}
		}
	}
	if (paramLPF.p != PARAM_LPF_OFF) {
		block.preset_values[paramLPF.p] = paramLPF.currentValue;
	}
	for (int32_t c = 0; c < cables.numUsablePatchCables; c++) {
		block.depths[c] = cables.patchCables[c].param.getCurrentValue();
	}
	return block;
}

void Sound::performInitialPatching(const deluge::synth::matrix::MatrixBlock& block) {
	deluge::synth::matrix::Evaluator<deluge::synth::matrix::MatrixFixed>::run_initial(matrixPass(), block);
}

void Sound::recalculatePatchingToParam(uint8_t p, ParamManager* paramManager) {

	const deluge::synth::RoutedDestination* destination = paramManager->getPatchCableSet()->getDestinationForParam(p);
	if (destination != nullptr) {
		// Pretend those sources have changed, and the param will update - for
		// each Voice too if local.
		sourcesChanged.fetch_or(destination->sources, std::memory_order_acq_rel);
		return;
	}

	// Otherwise, if nothing patched there...

	// Whether global...
	if (p >= params::FIRST_GLOBAL) {
		deluge::synth::matrix::Evaluator<deluge::synth::matrix::MatrixFixed>::run_uncabled_param(
		    matrixPass(), matrixBlock(*paramManager, PresetGather::ALL), p);
		return;
	}

	// Or local (do to each voice)...
	const deluge::synth::matrix::MatrixBlock block = matrixBlock(*paramManager, PresetGather::ALL);
	AudioExcludedSection section; // the render walks and edits this Sound's voices
	for (ActiveVoice& voice : voices_) {
		voice->kernel.recalculate_param_with_no_cables(p, block);
	}
}

#define ENSURE_PARAM_MANAGER_EXISTS                                                                                    \
	if (!paramManager->containsAnyMainParamCollections()) {                                                            \
		Error error = createParamManagerForLoading(paramManager);                                                      \
		if (error != Error::NONE)                                                                                      \
			return error;                                                                                              \
	}                                                                                                                  \
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();                      \
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;                  \
	ParamCollectionSummary* patchedParamsSummary = paramManager->getPatchedParamSetSummary();                          \
	PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;

// paramManager only required for old old song files, or for presets (because you'd be wanting to extract the
// defaultParams into it). arpSettings optional - no need if you're loading a new V2.0 song where Instruments are all
// separate from Clips and won't store any arp stuff.
namespace {
/// The sound element's tags the Deluge instrument decodes entirely (readFromFile), which this reader skips: a sound's
/// params are all in its defaultParams.
constexpr std::array<std::string_view, 26> kInstrumentOnlyTags{
    "mode",          "transpose",      "cents",     "unison",     "sideChainSend",
    "polyphonic",    "maxVoices",      "osc1",      "osc2",       "lfo1",
    "lfo2",          "lfo3",           "lfo4",      "modulator1", "modulator2",
    "midiOutput",    "modFXType",      "lpfMode",   "hpfMode",    "filterRoute",
    "voicePriority", "clippingAmount", "sidechain", "compressor", "audioCompressor",
    "stutter"};
} // namespace

Error Sound::readTagFromFileOrError(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
                                    int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song) {

	if (std::ranges::find(kInstrumentOnlyTags, std::string_view{tagName}) != kInstrumentOnlyTags.end()) {
		reader.exitTag(tagName);
		return Error::NONE;
	}

	if (!strcmp(tagName, "modKnobs")) {

		int32_t k = 0;
		int32_t w = 0;
		reader.match('[');
		while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "modKnob")) {
				reader.match('{');
				uint8_t p = params::GLOBAL_NONE;
				PatchSource s = PatchSource::NOT_AVAILABLE;
				PatchSource s2 = PatchSource::NOT_AVAILABLE;

				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "controlsParam")) {
						p = params::fileStringToParam(params::Kind::UNPATCHED_SOUND, reader.readTagOrAttributeValue(),
						                              true);
					}
					else if (!strcmp(tagName, "patchAmountFromSource")) {
						s = stringToSource(reader.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSecondSource")) {
						s2 = stringToSource(reader.readTagOrAttributeValue());
					}
					reader.exitTag(tagName);
				}
				reader.match('}'); // exit modKnobs value field.

				if (k < kNumModButtons) { // Ensure we're not loading more than actually fit in our array
					// A knob only controls a real param: "range" names no param a knob can move.
					if (p != params::GLOBAL_NONE && p != params::PLACEHOLDER_RANGE) {
						ModKnob& new_knob = modKnobs[k][w];

						if (s == PatchSource::NOT_AVAILABLE) {
							new_knob.paramDescriptor.setToHaveParamOnly(p);
						}
						else if (s2 == PatchSource::NOT_AVAILABLE) {
							new_knob.paramDescriptor.setToHaveParamAndSource(p, s);
						}
						else {
							new_knob.paramDescriptor.setToHaveParamAndTwoSources(p, s, s2);
						}

						ensureKnobReferencesCorrectVolume(new_knob);
					}
				}

				w++;
				if (w == kNumPhysicalModKnobs) {
					w = 0;
					k++;
				}
			}
			reader.exitTag(NULL, true); // Exit modKnob proper
		}
		reader.exitTag("modKnobs");
		reader.match(']');
	}

	else if (!strcmp(tagName, "patchCables")) {
		ENSURE_PARAM_MANAGER_EXISTS
		deluge::bridge::loadCables(*paramManager->getPatchCableSet(),
		                           *deluge::storage::treeReaderOf(reader).currentValue(), readAutomationUpToPos);
		reader.exitTag("patchCables");
	}

	else if (!strcmp(tagName, "defaultParams")) {
		ENSURE_PARAM_MANAGER_EXISTS
		Sound::readParamsFromFile(reader, paramManager, readAutomationUpToPos);
		reader.exitTag("defaultParams");
	}
	else {
		Error result = ModControllableAudio::readTagFromFile(reader, tagName, paramManager, readAutomationUpToPos,
		                                                     arpSettings, song);
		if (result == Error::NONE) {}
		else if (result != Error::RESULT_TAG_UNUSED) {
			return result;
		}
		else if (readTagFromFile(reader, tagName)) {}
		else {
			result = reader.tryReadingFirmwareTagFromFile(tagName, false);
			if (result != Error::NONE && result != Error::RESULT_TAG_UNUSED) {
				return result;
			}
			reader.exitTag();
		}
	}

	return Error::NONE;
}

// Exists for the purpose of potentially correcting an incorrect file as it's loaded
void Sound::ensureKnobReferencesCorrectVolume(Knob& knob) {
	int32_t p = knob.paramDescriptor.getJustTheParam();

	if (p == params::GLOBAL_VOLUME_POST_REVERB_SEND || p == params::GLOBAL_VOLUME_POST_FX
	    || p == params::LOCAL_VOLUME) {
		if (knob.paramDescriptor.isJustAParam()) {
			knob.paramDescriptor.setToHaveParamOnly(params::GLOBAL_VOLUME_POST_FX);
		}
		else if (knob.paramDescriptor.getTopLevelSource() == PatchSource::SIDECHAIN) {
			knob.paramDescriptor.changeParam(params::GLOBAL_VOLUME_POST_REVERB_SEND);
		}
		else {
			knob.paramDescriptor.changeParam(params::LOCAL_VOLUME);
		}
	}
}

// p=255 means we're just querying the source to see if it can be patched to anything
PatchCableAcceptance Sound::maySourcePatchToParam(PatchSource s, uint8_t p, ParamManager* paramManager) {

	if (s == PatchSource::NOTE && isDrum()) {
		return PatchCableAcceptance::DISALLOWED;
	}

	if (p != 255 && s != PatchSource::NOT_AVAILABLE && s >= kFirstLocalSource && p >= params::FIRST_GLOBAL) {
		return PatchCableAcceptance::DISALLOWED; // Can't patch local source to global param
	}

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	switch (p) {
	case params::GLOBAL_NONE:
		return PatchCableAcceptance::DISALLOWED;

	case params::LOCAL_VOLUME:
		return (
		           // No envelopes allowed to be patched to volume - this is hardcoded elsewhere
		           (s < PatchSource::ENVELOPE_0 || PatchSource::ENVELOPE_3 < s)
		           // Don't let the sidechain patch to local volume - it's supposed to go to global volume
		           && s != PatchSource::SIDECHAIN)
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::DISALLOWED;

	case params::LOCAL_OSC_A_PHASE_WIDTH:
		if (getSynthMode() == SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		// if (getSynthMode() == SynthMode::FM || (sources[0].oscType != OscType::SQUARE && sources[0].oscType !=
		// OscType::JUNO60_SUBOSC)) return PatchCableAcceptance::DISALLOWED;
		break;
	case params::LOCAL_OSC_A_VOLUME:
		if (getSynthMode() == SynthMode::RINGMOD) {
			return PatchCableAcceptance::DISALLOWED;
		}
	case params::LOCAL_OSC_A_PITCH_ADJUST:
		return (isSourceActiveEverDisregardingMissingSample(0, paramManager)) ? PatchCableAcceptance::ALLOWED
		                                                                      : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_CARRIER_0_FEEDBACK:
		if (getSynthMode() != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (isSourceActiveEver(0, paramManager)
		        && patchedParams->params[params::LOCAL_CARRIER_0_FEEDBACK].containsSomething(
		            *patchedParams->docForParam(params::LOCAL_CARRIER_0_FEEDBACK), -2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_OSC_B_PHASE_WIDTH:
		if (getSynthMode() == SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		// if (getSynthMode() == SynthMode::FM || (sources[1].oscType != OscType::SQUARE && sources[1].oscType !=
		// OscType::JUNO60_SUBOSC)) return PatchCableAcceptance::DISALLOWED;
		break;
	case params::LOCAL_OSC_B_VOLUME:
		if (getSynthMode() == SynthMode::RINGMOD) {
			return PatchCableAcceptance::DISALLOWED;
		}
	case params::LOCAL_OSC_B_PITCH_ADJUST:
		return (isSourceActiveEverDisregardingMissingSample(1, paramManager)) ? PatchCableAcceptance::ALLOWED
		                                                                      : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_CARRIER_1_FEEDBACK:
		if (getSynthMode() != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (isSourceActiveEver(1, paramManager)
		        && patchedParams->params[params::LOCAL_CARRIER_1_FEEDBACK].containsSomething(
		            *patchedParams->docForParam(params::LOCAL_CARRIER_1_FEEDBACK), -2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_NOISE_VOLUME:
		if (getSynthMode() == SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_NOISE_VOLUME].containsSomething(
		           *patchedParams->docForParam(params::LOCAL_NOISE_VOLUME), -2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

		// case params::LOCAL_RANGE:
		// return (rangeAdjustedParam != params::GLOBAL_NONE); // Ideally we'd also check whether the range-adjusted
		// param actually has something patched to it

	case params::LOCAL_LPF_FREQ:
	case params::LOCAL_LPF_MORPH:
	case params::LOCAL_LPF_RESONANCE:
		if (modControllableSnapshot_.effects.filter.lpf_mode == FilterMode::OFF) {
			return PatchCableAcceptance::DISALLOWED;
		}
		break;

	case params::LOCAL_HPF_FREQ:
	case params::LOCAL_HPF_MORPH:
	case params::LOCAL_HPF_RESONANCE:
		if (modControllableSnapshot_.effects.filter.hpf_mode == FilterMode::OFF) {
			return PatchCableAcceptance::DISALLOWED;
		}
		break;

	case params::LOCAL_MODULATOR_0_VOLUME:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
		if (getSynthMode() != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_MODULATOR_0_VOLUME].containsSomething(
		           *patchedParams->docForParam(params::LOCAL_MODULATOR_0_VOLUME), -2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_MODULATOR_0_FEEDBACK:
		if (getSynthMode() != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_MODULATOR_0_VOLUME].containsSomething(
		            *patchedParams->docForParam(params::LOCAL_MODULATOR_0_VOLUME), -2147483648)
		        && patchedParams->params[params::LOCAL_MODULATOR_0_FEEDBACK].containsSomething(
		            *patchedParams->docForParam(params::LOCAL_MODULATOR_0_FEEDBACK), -2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_MODULATOR_1_VOLUME:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
		if (getSynthMode() != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_MODULATOR_1_VOLUME].containsSomething(
		           *patchedParams->docForParam(params::LOCAL_MODULATOR_1_VOLUME), -2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::LOCAL_MODULATOR_1_FEEDBACK:
		if (getSynthMode() != SynthMode::FM) {
			return PatchCableAcceptance::DISALLOWED;
		}
		return (patchedParams->params[params::LOCAL_MODULATOR_1_VOLUME].containsSomething(
		            *patchedParams->docForParam(params::LOCAL_MODULATOR_1_VOLUME), -2147483648)
		        && patchedParams->params[params::LOCAL_MODULATOR_1_FEEDBACK].containsSomething(
		            *patchedParams->docForParam(params::LOCAL_MODULATOR_1_FEEDBACK), -2147483648))
		           ? PatchCableAcceptance::ALLOWED
		           : PatchCableAcceptance::EDITABLE;

	case params::GLOBAL_LFO_FREQ_1:
		return (snapshot_.lfo_config[LFO1_ID].sync_level == SYNC_LEVEL_NONE) ? PatchCableAcceptance::ALLOWED
		                                                                     : PatchCableAcceptance::DISALLOWED;

	case params::GLOBAL_LFO_FREQ_2:
		return (snapshot_.lfo_config[LFO3_ID].sync_level == SYNC_LEVEL_NONE) ? PatchCableAcceptance::ALLOWED
		                                                                     : PatchCableAcceptance::DISALLOWED;

		// Nothing may patch to post-fx volume. This is for manual control only. The sidechain patches to post-reverb
		// volume, and everything else patches to per-voice, "local" volume
	case params::GLOBAL_VOLUME_POST_FX:
		return PatchCableAcceptance::DISALLOWED;

	case params::LOCAL_PITCH_ADJUST:
		if (s == PatchSource::X) {
			return PatchCableAcceptance::DISALLOWED; // No patching X to pitch. This happens automatically.
		}
		break;

	case params::GLOBAL_VOLUME_POST_REVERB_SEND: // Only the sidechain can patch to here
		if (s != PatchSource::SIDECHAIN) {
			return PatchCableAcceptance::DISALLOWED;
		}
		break;

		// In a perfect world, we'd only allow patching to LFO rates if the LFO as a source is itself patched somewhere
		// usable
	}

	return PatchCableAcceptance::ALLOWED;
}

void Sound::noteOn(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int32_t noteCodePreArp,
                   int16_t const* mpeValues, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate,
                   int32_t velocity, int32_t fromMIDIChannel) {

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	if (!((getSynthMode() == SynthMode::RINGMOD) || (modelStackWithSoundFlags->checkSourceEverActive(0))
	      || (modelStackWithSoundFlags->checkSourceEverActive(1))
	      || (paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(
	          *paramManager->getPatchedParamSet()->docForParam(params::LOCAL_NOISE_VOLUME), -2147483648))))
	    [[unlikely]] {
		return;
	}

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	ArpeggiatorSettings* arpSettings = getArpSettings();
	if (arpSettings != nullptr) {
		arpSettings->updateParamsFromUnpatchedParamSet(unpatchedParams);
	}

	getArpBackInTimeAfterSkippingRendering(arpSettings); // Have to do this before telling the arp to noteOn()

	ArpReturnInstruction instruction;
	instruction.sampleSyncLengthOn = sampleSyncLength;

	// We used to not have to worry about the arpeggiator if one-shot samples etc. But now that we support MPE, we do
	// need to keep track of all sounding notes, even one-shot ones, and the "arpeggiator" is where this is stored.
	// These will get left here even after the note has long gone (for sequenced notes anyway), but I can't actually
	// find any negative consequence of this, or need to ever remove them en masse.
	arpeggiator->noteOn(arpSettings, noteCodePreArp, velocity, &instruction, fromMIDIChannel, mpeValues);

	bool atLeastOneNoteOn = false;
	bool atLeastOneNoteLeftPending = false;
	if (instruction.arpNoteOn != nullptr) {
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
				break;
			}
			atLeastOneNoteOn = true;

			if (AudioEngine::allowedToStartVoice()) {

				invertReversed = instruction.invertReversed;
				noteOnPostArpeggiator(modelStackWithSoundFlags, noteCodePreArp,
				                      instruction.arpNoteOn->noteCodeOnPostArp[n], instruction.arpNoteOn->velocity,
				                      mpeValues, instruction.sampleSyncLengthOn, ticksLate, samplesLate,
				                      fromMIDIChannel);
				instruction.arpNoteOn->noteStatus[n] = ArpNoteStatus::PLAYING;
			}
			else {
				// D_PRINTLN("couldn't start note from sound::noteon");
				// It stays PENDING, and render() will start it as soon as a voice is available
				atLeastOneNoteLeftPending = true;
			}
		}
	}
	if (!atLeastOneNoteOn) {
		// in the case of the arpeggiator not returning a note On (could happen if Note Probability evaluates to "don't
		// play") we must at least evaluate the render-skipping if the arpeggiator is ON
		if (arpSettings != nullptr && arpeggiator->hasAnyInputNotesActive() && arpSettings->mode != ArpMode::OFF) {
			reassessRenderSkippingStatus(modelStackWithSoundFlags);
		}
	}
	else if (atLeastOneNoteLeftPending) {
		// We started no voice for that note, so nothing else will have taken us out of render-skipping - and render()
		// is the only thing that can start the pending note (issue #2262)
		reassessRenderSkippingStatus(modelStackWithSoundFlags);
	}
}

void Sound::noteOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int32_t noteCode) {
	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();
	ArpeggiatorSettings* arpSettings = getArpSettings();

	ArpReturnInstruction instruction;
	arpeggiator->noteOff(arpSettings, noteCode, &instruction);

	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.glideNoteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.glideNoteCodeOffPostArp[n]);
	}
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.noteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.noteCodeOffPostArp[n]);
	}

	reassessRenderSkippingStatus(modelStackWithSoundFlags);
}

// Re-trigger a voice already in voices_ (see the declaration for the contract).
void Sound::retriggerVoice(const ActiveVoice& voice, ModelStackWithSoundFlags* modelStack, int32_t noteCodePreArp,
                           int32_t noteCodePostArp, int32_t velocity, int16_t const* mpeValues,
                           uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate,
                           int32_t fromMIDIChannel) {
	int32_t envelopePositions[kNumEnvelopes];
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		envelopePositions[e] = voice->kernel.envelope(e).output_level();
	}
	if (sideChainSendLevel != 0) [[unlikely]] {
		AudioEngine::registerSideChainHit(sideChainSendLevel);
	}
	if (deluge::bench::render_profile::timed_note_on([&] {
		    return voice->note_on(
		        *modelStack,
		        noteOnEvent(noteCodePreArp, noteCodePostArp, velocity, samplesLate, false, fromMIDIChannel, mpeValues),
		        sampleSyncLength, ticksLate);
	    })) {
		for (int32_t e = 0; e < kNumEnvelopes; e++) {
			voice->kernel.envelope(e).resume_attack(envelopePositions[e]);
		}
	}
	else {
		this->freeActiveVoice(voice, modelStack);
	}
}

void Sound::noteOnPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t noteCodePreArp, int32_t noteCodePostArp,
                                  int32_t velocity, int16_t const* mpeValues, uint32_t sampleSyncLength,
                                  int32_t ticksLate, uint32_t samplesLate, int32_t fromMIDIChannel) {
	const ActiveVoice* voiceToReuse = nullptr;
	const ActiveVoice* voiceForLegato = nullptr;

	// Not polyphonic: finding a voice to reuse or glide, and re-triggering it, all act on voices the render is playing,
	// so that whole path runs with the render excluded (free on the audio tier). A polyphonic note-on builds its new
	// voice with the render running and only publishes it excluded, below.
	std::optional<AudioExcludedSection> nonPolyphonic;
	if (polyphonic != PolyphonyMode::POLY) {
		nonPolyphonic.emplace();
	}

	auto* paramManager = static_cast<ParamManagerForTimeline*>(modelStack->paramManager);

	// If not polyphonic, stop any notes which are releasing, now
	if (!voices_.empty() && polyphonic != PolyphonyMode::POLY) [[unlikely]] {
		for (auto it = voices_.begin(); it != voices_.end();) {
			ActiveVoice& voice = *it;
			// if it's not MONO, and the envelope is not in release and we're allowing note tails, this is a legato
			// voice
			if (polyphonic != PolyphonyMode::MONO && voice->kernel.envelope(0).stage() < Envelope::Stage::RELEASE
			    && allowNoteTails(modelStack, true)) {
				voiceForLegato = &voice;
				break;
			}

			// If FM, or no active sources are samples, or still sounding after fast-release, unassign
			bool needs_unassign = //<
			    getSynthMode() == SynthMode::FM
			    || std::ranges::any_of(std::views::iota(0, kNumSources),
			                           [&](int32_t s) {
				                           return isSourceActiveCurrently(s, paramManager)
				                                  && sources[s].oscType != OscType::SAMPLE;
			                           })
			    || (voice->kernel.envelope(0).stage() != Envelope::Stage::FAST_RELEASE
			        && !voice->do_fast_release(kSoftCullIncrement));

			if (needs_unassign) {
				if (voiceToReuse != nullptr) {
					// already found a voice we can reuse, discard the rest
					this->freeActiveVoice(voice, modelStack, false);
					it = eraseVoiceAt(it);
					continue; // `it` increment happened on the line above, immediately continue the loop
				}
				voice->unassign_stuff(false);
				voiceToReuse = &voice;
			}
			it++;
		}
	}

	if (polyphonic == PolyphonyMode::LEGATO && voiceForLegato) [[unlikely]] {
		(*voiceForLegato)
		    ->change_note(*modelStack, noteCodePreArp, noteCodePostArp, fromMIDIChannel, mpeSpan(mpeValues));
	}
	else if (voiceToReuse != nullptr) [[unlikely]] {
		retriggerVoice(*voiceToReuse, modelStack, noteCodePreArp, noteCodePostArp, velocity, mpeValues,
		               sampleSyncLength, ticksLate, samplesLate, fromMIDIChannel);
	}
	else {
		// A new voice. Built here, where no Sound's voices_ holds it: the render can neither render nor cull it, so no
		// reference into voices_ is held across the note-on (a cull erasing or moving that slot mid-note-on is what
		// fires E199).
		std::optional<ActiveVoice> built; // ActiveVoice's deleter makes it not default-constructible
		try {
			built.emplace(AudioEngine::VoicePool::get().acquire(*this));
		} catch (deluge::exception e) {
			// No memory for another voice: start this note on one of this Sound's own voices instead, as a new note
			// (what the voice limit's steal has always done). That voice is in voices_ and the render plays it, so
			// all of it runs with the render excluded.
			AudioExcludedSection section;
			if (voices_.empty()) {
				return; // nothing to take: the note cannot play
			}
			const ActiveVoice& voice = this->stealOneActiveVoice();
			reassessRenderSkippingStatus(modelStack);
			voice->randomize_osc_phases();
			if (sideChainSendLevel != 0) [[unlikely]] {
				AudioEngine::registerSideChainHit(sideChainSendLevel);
			}
			if (!deluge::bench::render_profile::timed_note_on([&] {
				    return voice->note_on(*modelStack,
				                          noteOnEvent(noteCodePreArp, noteCodePostArp, velocity, samplesLate, true,
				                                      fromMIDIChannel, mpeValues),
				                          sampleSyncLength, ticksLate);
			    })) {
				this->freeActiveVoice(voice, modelStack);
			}
		}
		if (built.has_value()) {
			ActiveVoice& voice = *built;
			voice->randomize_osc_phases();
			if (sideChainSendLevel != 0) [[unlikely]] {
				AudioEngine::registerSideChainHit(sideChainSendLevel);
			}
			if (deluge::bench::render_profile::timed_note_on([&] {
				    return voice->note_on(*modelStack,
				                          noteOnEvent(noteCodePreArp, noteCodePostArp, velocity, samplesLate, true,
				                                      fromMIDIChannel, mpeValues),
				                          sampleSyncLength, ticksLate);
			    })) {
				// Publish: the voice limit, the push and the skipping status in one step the render cannot split.
				AudioExcludedSection section;
				auto countsTowardLimit = [](const ActiveVoice& v) { return !v->kernel.is_cull_fading(); };
				if (std::ranges::count_if(voices_, countsTowardLimit) >= maxVoiceCount) {
					this->terminateOneActiveVoice();
				}
				publishVoice(std::move(voice));
				reassessRenderSkippingStatus(modelStack);
			}
			// A voice whose note-on failed goes back to the pool when `built` goes out of scope; the render never saw
			// it. The note-on still runs to its end below (portamento's last note, MIDI out), as it always did.
		}
	}

	lastNoteCode = noteCodePostArp; // Store for porta. We store that at both note-on and note-off.

	// Send midi out for sound drums
	if (outputMidiChannel != MIDI_CHANNEL_NONE) {
		int32_t outputNoteCode = noteCodePostArp;
		if (outputMidiNoteForDrum != MIDI_NOTE_NONE) {
			int32_t noteCodeDiff = noteCodePostArp - kNoteForDrum;
			outputNoteCode = outputMidiNoteForDrum + noteCodeDiff;
			if (outputNoteCode < 0) {
				outputNoteCode = 0;
			}
			else if (outputNoteCode > 127) {
				outputNoteCode = 127;
			}
		}
		midiEngine.sendNote(this, true, outputNoteCode, velocity, outputMidiChannel, 0);

		// If the note doesn't have a tail (for ONCE samples for example and if ARP is OFF), we will never get a noteOff
		// event to be called by the sequencer, so we need to "off" the note right now
		if (!allowNoteTails(modelStack, true)) {
			midiEngine.sendNote(this, false, outputNoteCode, kDefaultNoteOffVelocity, outputMidiChannel, 0);
		}
	}
}

void Sound::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
                                                     int32_t channelOrNoteNumber,
                                                     MIDICharacteristic whichCharacteristic) {
	// Send midi if midi output enabled
	if (outputMidiChannel == MIDI_CHANNEL_NONE) {
		return;
	}
	// We only support mono or poly aftertouch at the moment (regular MIDI), not full MPE
	if (expressionDimension != 2) {
		return;
	}
	int32_t value7 = newValue >> 24;
	if (whichCharacteristic == MIDICharacteristic::CHANNEL) {
		// Channel aftertouch
		midiEngine.sendChannelAftertouch(this, outputMidiChannel, value7, kMIDIOutputFilterNoMPE);
	}
	// whichCharacteristic == MIDICharacteristic::NOTE
	else {
		// Polyphonic aftertouch
		if (getArp()->getArpType() == ArpType::DRUM) {
			// This is a sound drum (kit)
			ArpeggiatorForDrum* arpeggiator = (ArpeggiatorForDrum*)getArp();
			// Just one note is possible
			ArpNote arpNote = arpeggiator->active_note;
			for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				if (arpNote.noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
					break;
				}
				midiEngine.sendPolyphonicAftertouch(this, outputMidiChannel, value7, arpNote.noteCodeOnPostArp[n],
				                                    kMIDIOutputFilterNoMPE);
			}
		}
		else if (getArp()->getArpType() == ArpType::SYNTH) {
			// This is a sound instrument (synth)
			Arpeggiator* arpeggiator = (Arpeggiator*)getArp();
			// Search for the note
			int32_t i = searchArpNotes(arpeggiator->notes, channelOrNoteNumber);
			if (i < static_cast<int32_t>(arpeggiator->notes.size())) {
				ArpNote* arpNote = &arpeggiator->notes[i];
				for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
					if (arpNote->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
						break;
					}
					midiEngine.sendPolyphonicAftertouch(this, outputMidiChannel, value7, arpNote->noteCodeOnPostArp[n],
					                                    kMIDIOutputFilterNoMPE);
				}
			}
		}
	}
}

void Sound::allNotesOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator) {
	// Reset invertReversed flag so all voices get its reverse settings back to normal
	invertReversed = false;

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();
	noteOffPostArpeggiator(modelStackWithSoundFlags, ALL_NOTES_OFF);

	arpeggiator->reset();
}

// noteCode = ALL_NOTES_OFF (default) means stop *any* voice, regardless of noteCode
void Sound::noteOffPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t noteCode) {
	AudioExcludedSection section; // walks this Sound's voices, which the render edits (free on the audio tier)
	// Send midi note offs out for specific notes,
	// but only if the type of sound allows note tails (if not, note off was already sent right after its note on)
	if (outputMidiChannel != MIDI_CHANNEL_NONE && allowNoteTails(modelStack, true)) {
		if (noteCode == ALL_NOTES_OFF) {
			// We must send note offs for all active notes
			// so we will search for the current notes on postArp phase, if any

			// First any glide notes
			for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				if (getArp()->glideNoteCodeCurrentlyOnPostArp[n] == ARP_NOTE_NONE) {
					break;
				}
				int32_t outputNoteCode = getArp()->glideNoteCodeCurrentlyOnPostArp[n];
				if (outputMidiNoteForDrum != MIDI_NOTE_NONE) {
					// If note for drums is set then this is a SoundDrum and we must use the relative note code
					// (relative to kNoteForDrum)
					int32_t noteCodeDiff = outputNoteCode - kNoteForDrum;
					outputNoteCode = outputMidiNoteForDrum + noteCodeDiff;
					// Correct if out of bounds
					if (outputNoteCode < 0) {
						outputNoteCode = 0;
					}
					else if (outputNoteCode > 127) {
						outputNoteCode = 127;
					}
				}
				midiEngine.sendNote(this, false, outputNoteCode, kDefaultNoteOffVelocity, outputMidiChannel, 0);

				// The "voice" related code below will switch off the voice anyway, so it is safe to clean this flag so
				// we don't send two note offs if a normal noteOff or playback stop is received later
				getArp()->glideNoteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
			}

			// Then any normal notes
			for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				if (getArp()->active_note.noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
					break;
				}
				int32_t outputNoteCode = getArp()->active_note.noteCodeOnPostArp[n];
				if (outputMidiNoteForDrum != MIDI_NOTE_NONE) {
					// If note for drums is set then this is a SoundDrum and we must use the relative note code
					// (relative to kNoteForDrum)
					int32_t noteCodeDiff = outputNoteCode - kNoteForDrum;
					outputNoteCode = outputMidiNoteForDrum + noteCodeDiff;
					// Correct if out of bounds
					if (outputNoteCode < 0) {
						outputNoteCode = 0;
					}
					else if (outputNoteCode > 127) {
						outputNoteCode = 127;
					}
				}
				midiEngine.sendNote(this, false, outputNoteCode, kDefaultNoteOffVelocity, outputMidiChannel, 0);

				// The "voice" related code below will switch off the voice anyway, so it is safe to clean this flag so
				// we don't send two note offs if a normal noteOff or playback stop is received later
				getArp()->active_note.noteCodeOnPostArp[n] = ARP_NOTE_NONE;
				getArp()->active_note.noteStatus[n] = ArpNoteStatus::OFF;
			}
		}
		else {
			// We have an specific note code, so we'll directly use that.
			// This method has been called from the arp's noteOff so the handling of "noteCodeCurrentlyOnPostArp" has
			// already been done there
			int32_t outputNoteCode = noteCode;
			if (outputMidiNoteForDrum != MIDI_NOTE_NONE) {
				// If note for drums is set then this is a SoundDrum and we must use the relative note code
				// (relative to kNoteForDrum)
				int32_t noteCodeDiff = outputNoteCode - kNoteForDrum;
				outputNoteCode = outputMidiNoteForDrum + noteCodeDiff;
				// Correct if out of bounds
				if (outputNoteCode < 0) {
					outputNoteCode = 0;
				}
				else if (outputNoteCode > 127) {
					outputNoteCode = 127;
				}
			}
			midiEngine.sendNote(this, false, outputNoteCode, kDefaultNoteOffVelocity, outputMidiChannel, 0);
		}
	}
	if (outputMidiChannel != MIDI_CHANNEL_NONE && noteCode == ALL_NOTES_OFF) {
		// Besides all the previous specific note offs already sent, send also this special MIDI message,
		// just in case some other note is still playing and we didn't have track of it
		midiEngine.sendAllNotesOff(this, outputMidiChannel, kMIDIOutputFilterNoMPE);
	}

	if (voices_.empty()) {
		return;
	}

	ArpeggiatorSettings* arpSettings = getArpSettings();

	for (const ActiveVoice& voice : voices_) {
		if ((voice->kernel.note_code() == noteCode || noteCode == ALL_NOTES_OFF)
		    && voice->kernel.envelope(0).stage()
		           < Envelope::Stage::RELEASE) { // Don't bother if it's already "releasing"

			// If we have actual arpeggiation, just switch off.
			if ((arpSettings != nullptr) && arpSettings->mode != ArpMode::OFF) {
				goto justSwitchOff;
			}

			// If we're in LEGATO or true-MONO mode and there's another note we can switch back to...
			if ((polyphonic == PolyphonyMode::LEGATO || polyphonic == PolyphonyMode::MONO) && !isDrum()
			    && allowNoteTails(modelStack)) { // If no note-tails (i.e. yes one-shot samples etc.), the
				                                 // Arpeggiator will be full of notes which
				// might not be active anymore, cos we were keeping track of them for MPE purposes.
				Arpeggiator* arpeggiator = &((SoundInstrument*)this)->arpeggiator;
				if (arpeggiator->hasAnyInputNotesActive()) {
					ArpNote* arpNote = &arpeggiator->notes.back();
					int32_t newNoteCode = arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)];

					if (polyphonic == PolyphonyMode::LEGATO) {
						voice->change_note(
						    *modelStack, newNoteCode, newNoteCode,
						    arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)],
						    mpeSpan(arpNote->mpeValues));
						lastNoteCode = newNoteCode;
						// I think we could just return here, too?
					}
					else { // PolyphonyMode::MONO
						noteOnPostArpeggiator(
						    modelStack, newNoteCode, newNoteCode,
						    arpeggiator->lastVelocity, // Interesting - I've made it keep the velocity of presumably the
						                               // note we just switched off. I must have decided that sounded
						                               // best? I think I vaguely remember.
						    arpNote->mpeValues, // ... We take the MPE values from the "keypress" associated with the
						                        // new note we'll sound, though.
						    0, 0, 0, arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)]);
						return;
					}
				}
				else {
					goto justSwitchOff;
				}
			}

			else {
justSwitchOff:
				voice->note_off((ParamManagerForTimeline*)modelStack->paramManager, allowNoteTails(modelStack, true));
			}
		}
	}
}

bool Sound::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	// Return yes unless all active sources are play-once samples, or envelope 0 has no sustain

	// If arp on, then definitely yes
	ArpeggiatorSettings* arpSettings = getArpSettings((InstrumentClip*)modelStack->getTimelineCounterAllowNull());
	if ((arpSettings != nullptr) && arpSettings->mode != ArpMode::OFF) {
		return true;
	}

	// If no sustain ever, we definitely can't have tails
	if (!envelopeHasSustainEver(0, (ParamManagerForTimeline*)modelStack->paramManager)) {
		return false;
	}

	// After that if not subtractive (so no samples) or there's some noise, we definitely can have tails
	if (getSynthMode() != SynthMode::SUBTRACTIVE
	    || modelStack->paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(
	        *modelStack->paramManager->getPatchedParamSet()->docForParam(params::LOCAL_NOISE_VOLUME), -2147483648)) {
		return true;
	}

	// If we still don't know, just check there's at least one active oscillator that isn't a one-shot sample without a
	// loop-end point
	bool anyActiveSources = false;
	for (int32_t s = 0; s < kNumSources; s++) {
		bool sourceEverActive = modelStack->checkSourceEverActiveDisregardingMissingSample(s);

		anyActiveSources = sourceEverActive || anyActiveSources;

		if (sourceEverActive
		    && (sources[s].oscType != OscType::SAMPLE || sources[s].repeatMode != SampleRepeatMode::ONCE
		        || (!disregardSampleLoop && sources[s].hasAnyLoopEndPoint()))) {
			return true;
		}
	}

	return !anyActiveSources;
}

int32_t Sound::hasAnyTimeStretchSyncing(ParamManagerForTimeline* paramManager, bool getSampleLength, int32_t note) {

	if (getSynthMode() == SynthMode::FM) {
		return 0;
	}

	for (int32_t s = 0; s < kNumSources; s++) {

		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);

		if (sourceEverActive && sources[s].oscType == OscType::SAMPLE
		    && sources[s].repeatMode == SampleRepeatMode::STRETCH) {
			if (getSampleLength) {
				return sources[s].getLengthInSamplesAtSystemSampleRate(note + snapshot_.transpose, true);
			}
			return 1;
		}
	}

	return 0;
}

// Returns sample length in samples
int32_t Sound::hasCutOrLoopModeSamples(ParamManagerForTimeline* paramManager, int32_t note, bool* anyLooping) {

	if (getSynthMode() == SynthMode::FM) {
		return 0;
	}

	if (isNoiseActiveEver(paramManager)) {
		return 0;
	}

	int32_t maxLength = 0;
	if (anyLooping) {
		*anyLooping = false;
	}

	for (int32_t s = 0; s < kNumSources; s++) {
		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) {
			continue;
		}

		if (sources[s].oscType != OscType::SAMPLE) {
			return 0;
		}
		else if (sources[s].repeatMode == SampleRepeatMode::CUT || sources[s].repeatMode == SampleRepeatMode::LOOP) {

			if (anyLooping && sources[s].repeatMode == SampleRepeatMode::LOOP) {
				*anyLooping = true;
			}
			int32_t length = sources[s].getLengthInSamplesAtSystemSampleRate(note);

			// TODO: need a bit here to take into account the fact that the note pitch may well have lengthened or
			// shortened the sample

			maxLength = std::max(maxLength, length);
		}
	}

	return maxLength;
}

bool Sound::hasCutModeSamples(ParamManagerForTimeline* paramManager) {

	if (getSynthMode() == SynthMode::FM) {
		return false;
	}

	if (isNoiseActiveEver(paramManager)) {
		return false;
	}

	for (int32_t s = 0; s < kNumSources; s++) {
		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) {
			continue;
		}

		if (sources[s].oscType != OscType::SAMPLE || !sources[s].hasAtLeastOneAudioFileLoaded()
		    || sources[s].repeatMode != SampleRepeatMode::CUT) {
			return false;
		}
	}

	return true;
}

bool Sound::allowsVeryLateNoteStart(InstrumentClip* clip, ParamManagerForTimeline* paramManager) {

	// If arpeggiator, we can always start very late
	ArpeggiatorSettings* arpSettings = getArpSettings(clip);
	if ((arpSettings != nullptr) && arpSettings->mode != ArpMode::OFF) {
		return true;
	}

	if (getSynthMode() == SynthMode::FM) {
		return false;
	}

	// Basically, if any wave-based oscillators active, or one-shot samples, that means no not allowed
	for (int32_t s = 0; s < kNumSources; s++) {

		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) {
			continue;
		}

		switch (sources[s].oscType) {

		// Sample - generally ok, but not if one-shot
		case OscType::SAMPLE:
			if (sources[s].repeatMode == SampleRepeatMode::ONCE || !sources[s].hasAtLeastOneAudioFileLoaded()) {
				return false; // Not quite sure why the must-be-loaded requirement - maybe something would break if it
				              // tried to do a late start otherwise?
			}
			break;

		// Input - ok
		case OscType::INPUT_L:
		case OscType::INPUT_R:
		case OscType::INPUT_STEREO:
			break;

		// Wave-based - instant fail!
		default:
			return false;
		}
	}

	return true;
}

bool Sound::isSourceActiveCurrently(int32_t s, ParamManagerForTimeline* paramManager) {
	return (getSynthMode() == SynthMode::RINGMOD
	        || getSmoothedPatchedParamValue(params::LOCAL_OSC_A_VOLUME + s, *paramManager) != -2147483648)
	       && (getSynthMode() == SynthMode::FM || sources[s].oscType != OscType::SAMPLE
	           || sources[s].hasAtLeastOneAudioFileLoaded());
}

bool Sound::isSourceActiveEverDisregardingMissingSample(int32_t s, ParamManager* paramManager) {
	return (getSynthMode() == SynthMode::RINGMOD
	        || paramManager->getPatchedParamSet()->params[params::LOCAL_OSC_A_VOLUME + s].containsSomething(
	            *paramManager->getPatchedParamSet()->docForParam(params::LOCAL_OSC_A_VOLUME + s), -2147483648)
	        || renderingOscillatorSyncEver(paramManager));
}

bool Sound::isSourceActiveEver(int32_t s, ParamManager* paramManager) {
	return isSourceActiveEverDisregardingMissingSample(s, paramManager)
	       && (getSynthMode() == SynthMode::FM || sources[s].oscType != OscType::SAMPLE
	           || sources[s].hasAtLeastOneAudioFileLoaded());
}

bool Sound::isNoiseActiveEver(ParamManagerForTimeline* paramManager) {
	return (getSynthMode() != SynthMode::FM
	        && paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(
	            *paramManager->getPatchedParamSet()->docForParam(params::LOCAL_NOISE_VOLUME), -2147483648));
}

bool Sound::renderingOscillatorSyncCurrently(ParamManagerForTimeline* paramManager) {
	if (!snapshot_.oscillator_sync) {
		return false;
	}
	if (getSynthMode() == SynthMode::FM) {
		return false;
	}
	return (getSmoothedPatchedParamValue(params::LOCAL_OSC_B_VOLUME, *paramManager) != -2147483648
	        || getSynthMode() == SynthMode::RINGMOD);
}

bool Sound::renderingOscillatorSyncEver(ParamManager* paramManager) {
	if (!snapshot_.oscillator_sync) {
		return false;
	}
	if (getSynthMode() == SynthMode::FM) {
		return false;
	}
	return (paramManager->getPatchedParamSet()->params[params::LOCAL_OSC_B_VOLUME].containsSomething(
	            *paramManager->getPatchedParamSet()->docForParam(params::LOCAL_OSC_B_VOLUME), -2147483648)
	        || getSynthMode() == SynthMode::RINGMOD);
}

void Sound::sampleZoneChanged(MarkerType markerType, int32_t s, ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // the render walks and edits this Sound's voices
	if (voices_.empty()) {
		return;
	}

	if (sources[s].sampleControls.isCurrentlyReversed()) {
		markerType = static_cast<MarkerType>(kNumMarkerTypes - 1 - util::to_underlying(markerType));
	}

	for (auto it = voices_.begin(); it != voices_.end();) {
		const ActiveVoice& voice = *it;
		bool still_going = voice->sample_zone_changed(*modelStack, s, markerType);
		if (still_going) {
			it++;
			continue;
		}
		this->checkVoiceExists(voice, "E200");
		this->freeActiveVoice(voice, modelStack, false);
		it = eraseVoiceAt(it);
	}
}

// Unlike most functions, this one accepts modelStack as NULL, because when unassigning all voices e.g. on song swap, we
// won't have it.
void Sound::reassessRenderSkippingStatus(ModelStackWithSoundFlags* modelStack, bool shouldJustCutModFX) {

	// TODO: should get the caller to provide this, cos they usually already have it. In fact, should put this on the
	// ModelStack, cos many deeper-nested functions called by this one need it too!
	ArpeggiatorSettings* arpSettings = getArpSettings();

	// A note that couldn't get a voice yet is only ever started from render(), so we must keep rendering while one is
	// pending - otherwise it'd stay pending forever (issue #2262)
	bool skippingStatusNow =
	    (voices_.empty() && (core().delay().repeatsUntilAbandon() == 0u)
	     && (currentSong == nullptr || !currentSong->live.stutter.isStuttering(*this))
	     && ((arpSettings == nullptr) || !getArp()->hasAnyInputNotesActive() || arpSettings->mode == ArpMode::OFF)
	     && !getArp()->hasPendingNotes(arpSettings));

	if (skippingStatusNow != skippingRendering) {

		if (skippingStatusNow) {

			// We wanna start, skipping, but if MOD fx are on...
			if ((modControllableSnapshot_.effects.mod_fx.type != ModFXType::NONE)
			    || core().compressor().dsp().threshold() > 0) {

				// If we didn't start the wait-time yet, start it now
				if (!startSkippingRenderingAtTime) {

					// But wait, first, maybe we actually have just been instructed to cut the MODFX tail
					if (shouldJustCutModFX) {
doCutModFXTail:
						clearModFXMemory();
						goto yupStartSkipping;
					}

					int32_t waitSamplesModfx = 0;
					switch (modControllableSnapshot_.effects.mod_fx.type) {
					case ModFXType::CHORUS:
						[[fallthrough]];
					case ModFXType::CHORUS_STEREO:
						waitSamplesModfx = 20 * 44;
						break;
					case ModFXType::GRAIN:
						waitSamplesModfx = core().granular().samplesToShutdown();
						break;
					default:
						waitSamplesModfx = (90 * 441);
					}
					int32_t waitSamples =
					    std::max(waitSamplesModfx, static_cast<int32_t>(core().compressor().dsp().release_ms() * 44));
					startSkippingRenderingAtTime = AudioEngine::audioSampleTimer + waitSamples;
				}

				// Or if already waiting, see if the wait is over yet
				else {
					if ((int32_t)(AudioEngine::audioSampleTimer - startSkippingRenderingAtTime) >= 0) {
						startSkippingRenderingAtTime = 0;
						goto yupStartSkipping;
					}

					// Ok, we wanted to check that before manually cutting the MODFX tail, to save time, but that's
					// still an option...
					if (shouldJustCutModFX) {
						goto doCutModFXTail;
					}
				}
			}
			else {
yupStartSkipping:
				startSkippingRendering(modelStack);
			}
		}
		else {
			stopSkippingRendering(arpSettings);
		}
	}

	else {
		startSkippingRenderingAtTime = 0;
	}
}

void Sound::getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
                                   GlobalEffectableForClip** globalEffectableWithMostReverb,
                                   int32_t* highestReverbAmountFound, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	if (!patchedParams->params[params::GLOBAL_REVERB_AMOUNT].isAutomated(
	        *patchedParams->docForParam(params::GLOBAL_REVERB_AMOUNT))
	    && patchedParams->params[params::GLOBAL_REVERB_AMOUNT].containsSomething(
	        *patchedParams->docForParam(params::GLOBAL_REVERB_AMOUNT), -2147483648)) {

		// We deliberately don't use the LPF'ed param here
		int32_t reverbHere = patchedParams->getValue(params::GLOBAL_REVERB_AMOUNT);
		if (*highestReverbAmountFound < reverbHere) {
			*highestReverbAmountFound = reverbHere;
			*soundWithMostReverb = this;
			*paramManagerWithMostReverb = paramManager;
			*globalEffectableWithMostReverb = nullptr;
		}
	}
}

// fromAutomation means whether the changes was caused by automation playing back - as opposed to the user turning the
// knob right now
void Sound::notifyValueChangeViaLPF(int32_t p, bool shouldDoParamLPF, ModelStackWithThreeMainThings const* modelStack,
                                    int32_t oldValue, int32_t newValue, bool fromAutomation) {

	if (skippingRendering) {
		goto dontDoLPF;
	}

	if (!shouldDoParamLPF) {
		// If param LPF was active for this param, stop it
		if (paramLPF.p == p) {
			paramLPF.p = PARAM_LPF_OFF;
		}
		goto dontDoLPF;
	}

	// If doing param LPF
	if (params::paramNeedsLPF(p, fromAutomation)) {

		// If the param LPF was already busy...
		if (paramLPF.p != PARAM_LPF_OFF) {
			// If it was a different param, tell it to stop so that we can have it
			if (paramLPF.p != p) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				copyModelStack(modelStackMemory, modelStack, sizeof(ModelStackWithThreeMainThings));
				ModelStackWithThreeMainThings* modelStackCopy = (ModelStackWithThreeMainThings*)modelStackMemory;

				stopParamLPF(modelStackCopy->addSoundFlags());
			}

			// Otherwise, keep its current state, and just tell it it's going somewhere new
			else {
				goto changeWhereParamLPFGoing;
			}
		}

		paramLPF.currentValue = oldValue;

changeWhereParamLPFGoing:
		paramLPF.p = p;
	}

	// Or if not doing param LPF
	else {
dontDoLPF:
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		copyModelStack(modelStackMemory, modelStack, sizeof(ModelStackWithThreeMainThings));
		ModelStackWithThreeMainThings* modelStackCopy = (ModelStackWithThreeMainThings*)modelStackMemory;

		patchedParamPresetValueChanged(p, modelStackCopy->addSoundFlags(), oldValue, newValue);
	}
}

void Sound::doParamLPF(int32_t numSamples, ModelStackWithSoundFlags* modelStack) {
	if (paramLPF.p == PARAM_LPF_OFF) {
		return;
	}

	int32_t oldValue = paramLPF.currentValue;

	int32_t diff = (modelStack->paramManager->getPatchedParamSet()->getValue(paramLPF.p) >> 8) - (oldValue >> 8);

	if (diff == 0) {
		stopParamLPF(modelStack);
	}
	else {
		int32_t amountToAdd = diff * numSamples;
		paramLPF.currentValue += amountToAdd;
		// patchedParamPresetValueChanged(paramLPF.p, notifySound, oldValue, paramLPF.currentValue);
		patchedParamPresetValueChanged(paramLPF.p, modelStack, oldValue, paramLPF.currentValue);
	}
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it.
void Sound::stopParamLPF(ModelStackWithSoundFlags* modelStack) {
	bool wasActive = paramLPF.p != PARAM_LPF_OFF;
	if (wasActive) {
		int32_t p = paramLPF.p;
		paramLPF.p = PARAM_LPF_OFF; // Must do this first, because the below call will involve the Sound calling us back
		                            // for the current value
		if (modelStack) {
			patchedParamPresetValueChanged(p, modelStack, paramLPF.currentValue,
			                               modelStack->paramManager->getPatchedParamSet()->getValue(p));
		}
	}
}

void Sound::process_postarp_notes(ModelStackWithSoundFlags* modelStackWithSoundFlags, ArpeggiatorSettings* arpSettings,
                                  ArpReturnInstruction instruction) {
	if (instruction.arpNoteOn) {
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			// do we have a note to start? if no, exit early
			if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
				break;
			}
			// has the note already started? if yes, skip it
			if (instruction.arpNoteOn->noteStatus[n] != ArpNoteStatus::PENDING) {
				continue;
			}
			// note is pending, are we allowing to start it? if no, exit early
			if (!AudioEngine::allowedToStartVoice()) {
				break; // Leave the remaining notes pending for the next render.
			}
			invertReversed = instruction.invertReversed;

			noteOnPostArpeggiator(
			    modelStackWithSoundFlags,
			    instruction.arpNoteOn->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)],
			    instruction.arpNoteOn->noteCodeOnPostArp[n], instruction.arpNoteOn->velocity,
			    instruction.arpNoteOn->mpeValues, instruction.sampleSyncLengthOn, 0, 0,
			    instruction.arpNoteOn->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)]);
			instruction.arpNoteOn->noteStatus[n] = ArpNoteStatus::PLAYING;
		}
	}
}

void Sound::render(ModelStackWithThreeMainThings* modelStack, std::span<StereoSample> output, int32_t* reverbBuffer,
                   int32_t sideChainHitPending, int32_t reverbAmountAdjust, bool shouldLimitDelayFeedback,
                   int32_t pitchAdjust, SampleRecorder* recorder) {

	if (skippingRendering) {
		core().compressor().dsp().clear_gain_reduction();
		return;
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	// Do global LFO
	if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PatchSource::LFO_GLOBAL_1)) {
		const auto patchSourceLFOGlobalUnderlying = util::to_underlying(PatchSource::LFO_GLOBAL_1);

		int32_t old = params.source_values[patchSourceLFOGlobalUnderlying];
		// TODO: We don't really need to recompute phase increment unless rate, sync, or
		// playbackHandler.getTimePerInternalTickInverse() has changed. Rate and sync changes
		// already cause a resync. Maybe tempo changes do too? If so, this could be part of
		// the resync logic. Note: same issue exists with LFO2 now that it supports sync.
		params.source_values[patchSourceLFOGlobalUnderlying] =
		    globalLFO1.render(output.size(), snapshot_.lfo_config[LFO1_ID],
		                      getGlobalLFOPhaseIncrement(LFO1_ID, params::GLOBAL_LFO_FREQ_1), deluge::synth_noise);
		uint32_t anyChange = (old != params.source_values[patchSourceLFOGlobalUnderlying]);
		sourcesChanged.fetch_or(anyChange << patchSourceLFOGlobalUnderlying, std::memory_order_acq_rel);
	}
	if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PatchSource::LFO_GLOBAL_2)) {
		const auto patchSourceLFOGlobalUnderlying = util::to_underlying(PatchSource::LFO_GLOBAL_2);

		int32_t old = params.source_values[patchSourceLFOGlobalUnderlying];
		// TODO: We don't really need to recompute phase increment unless rate, sync, or
		// playbackHandler.getTimePerInternalTickInverse() has changed. Rate and sync changes
		// already cause a resync. Maybe tempo changes do too? If so, this could be part of
		// the resync logic. Note: same issue exists with LFO2 now that it supports sync.
		params.source_values[patchSourceLFOGlobalUnderlying] =
		    globalLFO3.render(output.size(), snapshot_.lfo_config[LFO3_ID],
		                      getGlobalLFOPhaseIncrement(LFO3_ID, params::GLOBAL_LFO_FREQ_2), deluge::synth_noise);
		uint32_t anyChange = (old != params.source_values[patchSourceLFOGlobalUnderlying]);
		sourcesChanged.fetch_or(anyChange << patchSourceLFOGlobalUnderlying, std::memory_order_acq_rel);
	}

	for (int s = 0; s < kNumSources; s++) {
		if (sources[s].oscType == OscType::DX7 and sources[s].dxPatch) {
			sources[s].dxPatch->computeLfo(output.size());
		}
	}

	// Do sidechain
	if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PatchSource::SIDECHAIN)) {
		if (sideChainHitPending) {
			core().sidechain().registerHit(sideChainHitPending);
		}

		const auto patchSourceSidechainUnderlying = util::to_underlying(PatchSource::SIDECHAIN);

		int32_t old = params.source_values[patchSourceSidechainUnderlying];
		params.source_values[patchSourceSidechainUnderlying] = core().sidechain().render(
		    output.size(), paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_SIDECHAIN_SHAPE),
		    modControllableSnapshot_.effects.sidechain, playbackHandler.getTimePerInternalTickInverse());
		uint32_t anyChange = (old != params.source_values[patchSourceSidechainUnderlying]);
		sourcesChanged.fetch_or(anyChange << patchSourceSidechainUnderlying, std::memory_order_acq_rel);
	}

	// Perform the actual patching
	const uint32_t soundSourcesChanged = sourcesChanged.load(std::memory_order_acquire);
	if (soundSourcesChanged != 0u) {
#if defined(DELUGE_STREAM_BENCH)
		const uint32_t t0 = deluge::bench::render_profile::cycles();
#endif
		deluge::synth::matrix::Evaluator<deluge::synth::matrix::MatrixFixed>::run(
		    matrixPass(), matrixBlock(*paramManager, PresetGather::ROUTED), soundSourcesChanged);
#if defined(DELUGE_STREAM_BENCH)
		deluge::bench::render_profile::g_totals.sound_patch += deluge::bench::render_profile::cycles() - t0;
		++deluge::bench::render_profile::g_totals.sound_blocks;
#endif
	}

	// Setup some reverb-related stuff
	int32_t reverbSendAmount =
	    multiply_32x32_rshift32_rounded(reverbAmountAdjust,
	                                    params.param_final_values[params::GLOBAL_REVERB_AMOUNT - params::FIRST_GLOBAL])
	    << 5;

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	// Arpeggiator

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	ArpeggiatorSettings* arpSettings = getArpSettings();
	if (arpSettings != nullptr) {
		arpSettings->updateParamsFromUnpatchedParamSet(unpatchedParams);
	}
	ArpReturnInstruction instruction;

	if (arpSettings != nullptr && arpSettings->mode != ArpMode::OFF) {
		uint32_t gateThreshold = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_GATE) + 2147483648;
		uint32_t phaseIncrement =
		    arpSettings->getPhaseIncrement(params.param_final_values[params::GLOBAL_ARP_RATE - params::FIRST_GLOBAL]);

		getArp()->render(arpSettings, &instruction, output.size(), gateThreshold, phaseIncrement);
	}
	else {
		getArp()->handlePendingNotes(arpSettings, &instruction);
	}
	bool atLeastOneOff = false;
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.glideNoteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		atLeastOneOff = true;
		noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.glideNoteCodeOffPostArp[n]);
	}
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.noteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		atLeastOneOff = true;
		noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.noteCodeOffPostArp[n]);
	}
	if (atLeastOneOff) {
		invertReversed = false;
	}
	process_postarp_notes(modelStackWithSoundFlags, arpSettings, instruction);

	// Setup delay
	deluge::effects::Delay::State delayWorkingState{};
	delayWorkingState.delayFeedbackAmount =
	    params.param_final_values[params::GLOBAL_DELAY_FEEDBACK - params::FIRST_GLOBAL];
	if (shouldLimitDelayFeedback) {
		delayWorkingState.delayFeedbackAmount =
		    std::min(delayWorkingState.delayFeedbackAmount, (q31_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate = params.param_final_values[params::GLOBAL_DELAY_RATE - params::FIRST_GLOBAL];
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse(true);
	core().delay().setupWorkingState(delayWorkingState, modControllableSnapshot_.effects.delay, timePerTickInverse,
	                                 !voices_.empty());
	delayWorkingState.analog_saturation = 8;

	// Render each voice into a local buffer here
	bool voice_rendered_in_stereo = renderingVoicesInStereo(modelStackWithSoundFlags);

	// FIXME(@stellar-aria): if we have simulataneous sounds rendering, they'll overwrite
	// each other in this buffer. It probably should be object or thread-local
	alignas(CACHE_LINE_SIZE) static q31_t sound_memory[SSI_TX_BUFFER_NUM_SAMPLES * 2];
	memset(sound_memory, 0, output.size() * sizeof(q31_t) * (voice_rendered_in_stereo ? 2 : 1));

	std::span sound_mono{sound_memory, output.size()};
	std::span sound_stereo{(StereoSample*)sound_memory, output.size()};

	if (!voices_.empty()) {

		// Very often, we'll just apply panning here at the Sound level rather than the Voice level
		bool applyingPanAtVoiceLevel =
		    (AudioEngine::renderInStereo
		     && paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_PAN));

		// Setup filters
		bool thisHasFilters = hasFilters();
		q31_t lpfMorph = getSmoothedPatchedParamValue(params::LOCAL_LPF_MORPH, *paramManager);
		q31_t lpfFreq = getSmoothedPatchedParamValue(params::LOCAL_LPF_FREQ, *paramManager);
		q31_t hpfMorph = getSmoothedPatchedParamValue(params::LOCAL_HPF_MORPH, *paramManager);
		q31_t hpfFreq = getSmoothedPatchedParamValue(params::LOCAL_HPF_FREQ, *paramManager);
		bool doLPF = thisHasFilters
		             && (modControllableSnapshot_.effects.filter.lpf_mode == FilterMode::TRANSISTOR_24DB_DRIVE
		                 || paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_LPF_FREQ)
		                 || (lpfFreq < 0x7FFFFFD2) || (lpfMorph > std::numeric_limits<q31_t>::min()));
		bool doHPF =
		    thisHasFilters
		    && (paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_HPF_FREQ)
		        || (hpfFreq != std::numeric_limits<q31_t>::min()) || (hpfMorph > std::numeric_limits<q31_t>::min()));
		// The sound's render config for this block, the same for each of its voices.
		std::array<OscType, kNumSources> oscTypes;
		std::array<SampleRepeatMode, kNumSources> repeatModes;
		std::array<InterpolationMode, kNumSources> interpolationModes;
		std::array<bool, kNumSources> dxPatchChangeds;
		std::array<bool, kNumSources> sourceEverActives;
		std::array<bool, kNumSources> sourceActiveCurrentlys;
		for (int32_t s = 0; s < kNumSources; s++) {
			oscTypes[s] = sources[s].oscType;
			repeatModes[s] = sources[s].repeatMode;
			interpolationModes[s] = sources[s].sampleControls.interpolationMode;
			dxPatchChangeds[s] = sources[s].dxPatchChanged;
			sourceEverActives[s] = modelStackWithSoundFlags->checkSourceEverActive(s);
			sourceActiveCurrentlys[s] = isSourceActiveCurrently(s, paramManager);
		}
		ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
		const uint8_t* bendRangesSrc = expressionParams ? expressionParams->bendRanges : FlashStorage::defaultBendRange;
		const VoiceConfig voiceConfig{
		    .low_pass = doLPF ? deluge::effects::low_pass_slot(modControllableSnapshot_.effects.filter.lpf_mode)
		                      : deluge::dsp::fixed::LowPassSlot{},
		    .high_pass = doHPF ? deluge::effects::high_pass_slot(modControllableSnapshot_.effects.filter.hpf_mode)
		                       : deluge::dsp::fixed::HighPassSlot{},
		    .filter_route = deluge::effects::filter_route(modControllableSnapshot_.effects.filter.route),
		    .volume_neutral_value_for_unison = volumeNeutralValueForUnison,
		    .cpu_direness = AudioEngine::cpuDireness,
		    .voice_priority = snapshot_.voice_priority,
		    .synth_mode = getSynthMode(),
		    .lfo2_config = snapshot_.lfo_config[LFO2_ID],
		    .lfo4_config = snapshot_.lfo_config[LFO4_ID],
		    .lfo2_synced_phase_increment = (snapshot_.lfo_config[LFO2_ID].sync_level == SYNC_LEVEL_NONE)
		                                       ? 0
		                                       : getSyncedLFOPhaseIncrement(snapshot_.lfo_config[LFO2_ID]),
		    .lfo4_synced_phase_increment = (snapshot_.lfo_config[LFO4_ID].sync_level == SYNC_LEVEL_NONE)
		                                       ? 0
		                                       : getSyncedLFOPhaseIncrement(snapshot_.lfo_config[LFO4_ID]),
		    .has_filters = thisHasFilters,
		    .expression_sources_changed_at_synth_level = expressionSourcesChangedAtSynthLevel,
		    .monophonic_expression_values = monophonicExpressionValues,
		    .clipping_amount = modControllableSnapshot_.effects.saturation.clipping_amount,
		    .doing_osc_sync = renderingOscillatorSyncCurrently(paramManager),
		    .engine_rendering_in_stereo = AudioEngine::renderInStereo,
		    .num_unison = snapshot_.num_unison,
		    .unison_stereo_spread = snapshot_.unison_stereo_spread,
		    .unison_pan = snapshot_.unison_pan,
		    .osc_retrigger_phase = snapshot_.osc_retrigger_phase,
		    .osc_type = oscTypes,
		    .repeat_mode = repeatModes,
		    .interpolation_mode = interpolationModes,
		    .dx_patch_changed = dxPatchChangeds,
		    .global_source_values = params.source_values,
		    .note_tails_allowed = allowNoteTails(modelStackWithSoundFlags, true),
		    .source_ever_active = sourceEverActives,
		    .noise_volume_contains_something =
		        paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(
		            *paramManager->getPatchedParamSet()->docForParam(params::LOCAL_NOISE_VOLUME), -2147483648),
		    .local_sources_patched_to_anything =
		        paramManager->getPatchCableSet()->sourcesPatchedToAnything[GLOBALITY_LOCAL],
		    .bend_ranges = {bendRangesSrc[BEND_RANGE_MAIN], bendRangesSrc[BEND_RANGE_FINGER_LEVEL]},
		    .portamento = paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_PORTAMENTO),
		    .modulator1_to_modulator0 = snapshot_.modulator1_to_modulator0,
		    .source_active_currently = sourceActiveCurrentlys,
		};

		// The matrix's per-block input. Sound-level + stable across the voice loop (the param sets are fixed and
		// paramLPF is only mutated after the loop), so build it once here rather than per voice.
		const deluge::synth::matrix::MatrixBlock controlBlock = matrixBlock(*paramManager, PresetGather::ROUTED);

		for (auto it = voices_.begin(); it != voices_.end();) {
			ActiveVoice& voice = *it;

			// A note-off the voice ignored is taken now if the sound has since come to allow note tails. A voice's
			// first render call only marks it created, so it is not taken then.
			if (voice->just_created && voice->kernel.previously_ignored_note_off() && voiceConfig.note_tails_allowed) {
				voice->note_off(paramManager, voiceConfig.note_tails_allowed);
			}
			const uint32_t voiceSourcesChanged = sourcesChanged.load(std::memory_order_acquire);
#if defined(DELUGE_STREAM_BENCH)
			const uint32_t t0 = deluge::bench::render_profile::cycles();
#endif
			bool stillGoing = voice->render(controlBlock, voiceConfig,
			                                {
			                                    .buffer = sound_mono.data(),
			                                    .num_samples = static_cast<int32_t>(sound_mono.size()),
			                                    .sound_in_stereo = voice_rendered_in_stereo,
			                                    .pan_at_voice_level = applyingPanAtVoiceLevel,
			                                    .sources_changed = voiceSourcesChanged,
			                                    .pitch_adjust = pitchAdjust,
			                                    .spare_buffers = {spareRenderingBuffer[0], spareRenderingBuffer[1],
			                                                      spareRenderingBuffer[2], spareRenderingBuffer[3]},
			                                    .filter_scratch = deluge::render::filter_scratch(),
			                                    .sync_scratch = deluge::render::osc_sync_scratch(),
			                                    .noise = &deluge::synth_noise,
			                                });
#if defined(DELUGE_STREAM_BENCH)
			deluge::bench::render_profile::g_totals.voice_render += deluge::bench::render_profile::cycles() - t0;
			++deluge::bench::render_profile::g_totals.voice_blocks;
#endif
			if (!stillGoing) {
				this->checkVoiceExists(voice, "E201");
				this->freeActiveVoice(voice, modelStackWithSoundFlags, false);
			}
			++it;
		}
		eraseVoicesIf([](const ActiveVoice& voice) { return voice->should_be_deleted(); });

		// We know that nothing's patched to pan, so can read it in this very basic way.
		int32_t pan = paramManager->getPatchedParamSet()->getValue(params::LOCAL_PAN) >> 1;
		int32_t amplitudeL, amplitudeR;
		bool doPanning = false;
		if (AudioEngine::renderInStereo) {
			const deluge::dsp::fixed::PanAmplitudes amplitudes = deluge::dsp::fixed::pan_amplitudes(pan);
			amplitudeL = amplitudes.left;
			amplitudeR = amplitudes.right;
			doPanning = pan != 0;
		}

		// If just rendered in mono, double that up to stereo now
		if (!voice_rendered_in_stereo) {
			if (doPanning) {
				// right to left because of in-place mono to stereo expansion
				std::transform(sound_mono.rbegin(), sound_mono.rend(), sound_stereo.rbegin(), [=](q31_t sample) {
					return StereoSample{
					    .l = multiply_32x32_rshift32(sample, amplitudeL) << 2,
					    .r = multiply_32x32_rshift32(sample, amplitudeR) << 2,
					};
				});
			}
			else {
				// right to left because of in-place mono to stereo expansion
				std::transform(sound_mono.rbegin(), sound_mono.rend(), sound_stereo.rbegin(), StereoSample::from_mono);
			}
		}

		// Or if rendered in stereo...
		// And if we're only applying pan here at the Sound level...
		else if (!applyingPanAtVoiceLevel && doPanning) {
			for (StereoSample& sample : sound_stereo) {
				sample.l = multiply_32x32_rshift32(sample.l, amplitudeL) << 2;
				sample.r = multiply_32x32_rshift32(sample.r, amplitudeR) << 2;
			}
		}
	}
	else {
		if (!delayWorkingState.doDelay) {
			reassessRenderSkippingStatus(modelStackWithSoundFlags);
		}

		if (!voice_rendered_in_stereo) {
			// Clear the non-overlapping portion of the stereo buffer (yes this is janky).
			// Use data()+size() for the past-the-end pointer, not &sound_mono[size()] — the latter
			// calls span::operator[] with an out-of-bounds index (UB; the hardened host build asserts).
			memset(sound_mono.data() + sound_mono.size(), 0, sound_stereo.size_bytes() - sound_mono.size_bytes());
		}
	}

	checkStack("delay");
	if (chain_.process(sound_stereo,
	                   soundChainInputs(*this, *paramManager, delayWorkingState, !voices_.empty(), reverbSendAmount),
	                   {reverbBuffer, sound_stereo.size()})) {
		AudioEngine::timeThereWasLastSomeReverb = AudioEngine::audioSampleTimer;
	}

	if (recorder && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		// we need to double it because for reasons I don't understand audio clips max volume is half the sample volume
		recorder->feedAudio(sound_stereo, true, 2);
	}

	// add the sound to the output, i.e. output = output + sound
	std::ranges::transform(output, sound_stereo, output.begin(), std::plus{});

	// Only the bits the sound's own pass read: a bit marked after it is read again next block, by the sound's pass
	// and by the voices, which may have read it already (a repeat patching gives the same values)
	sourcesChanged.fetch_and(~soundSourcesChanged, std::memory_order_acq_rel);
	expressionSourcesChangedAtSynthLevel.reset();
	for (int i = 0; i < kNumSources; i++) {
		sources[i].dxPatchChanged = false;
	}

	// Unlike all the other possible reasons we might want to start skipping rendering,
	// core().delay().repeatsUntilAbandon() may have changed state just now.
	if (core().delay().repeatsUntilAbandon() == 0u || startSkippingRenderingAtTime) {
		reassessRenderSkippingStatus(modelStackWithSoundFlags);
	}

	doParamLPF(output.size(), modelStackWithSoundFlags);
}

// This is virtual, and gets extended by drums!
void Sound::setSkippingRendering(bool newSkipping) {
	skippingRendering = newSkipping;
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it.
void Sound::startSkippingRendering(ModelStackWithSoundFlags* modelStack) {
	timeStartedSkippingRenderingModFX = AudioEngine::audioSampleTimer;
	timeStartedSkippingRenderingLFO = AudioEngine::audioSampleTimer;
	timeStartedSkippingRenderingArp = AudioEngine::audioSampleTimer;
	// compressor.status = Envelope::Stage::OFF; // Was this doing anything? Have removed, to make all of this
	// completely reversible without doing anything

	setSkippingRendering(true);
	core().granular().startSkippingRendering();
	stopParamLPF(modelStack);
}

void Sound::stopSkippingRendering(ArpeggiatorSettings* arpSettings) {
	if (skippingRendering) {

		int32_t modFXTimeOff = AudioEngine::audioSampleTimer
		                       - timeStartedSkippingRenderingModFX; // This variable is a good indicator of whether it
		                                                            // actually was skipping at all

		// If rendering was actually stopped for any length of time...
		if (modFXTimeOff) {

			// Do LFO
			globalLFO1.tick(AudioEngine::audioSampleTimer - timeStartedSkippingRenderingLFO,
			                getGlobalLFOPhaseIncrement(LFO1_ID, params::GLOBAL_LFO_FREQ_1));
			globalLFO3.tick(AudioEngine::audioSampleTimer - timeStartedSkippingRenderingLFO,
			                getGlobalLFOPhaseIncrement(LFO3_ID, params::GLOBAL_LFO_FREQ_2));

			// Do Mod FX
			core().mod_fx().tick(modFXTimeOff,
			                     params.param_final_values[params::GLOBAL_MOD_FX_RATE - params::FIRST_GLOBAL]);

			// Do arp
			getArpBackInTimeAfterSkippingRendering(arpSettings);

			// Do sidechain
			// if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PatchSource::SIDECHAIN)) {
			if (AudioEngine::sizeLastSideChainHit) {
				core().sidechain().registerHitRetrospectively(
				    AudioEngine::sizeLastSideChainHit,
				    AudioEngine::audioSampleTimer - AudioEngine::timeLastSideChainHit,
				    modControllableSnapshot_.effects.sidechain, playbackHandler.getTimePerInternalTickInverse());
				//}
			}
			// Special state to make it grab the actual value the first time it's rendered
			core().send_and_volume().forget();

			// clearModFXMemory(); // No need anymore, now we wait for this to basically empty before starting skipping
		}

		setSkippingRendering(false);
	}
}

void Sound::getArpBackInTimeAfterSkippingRendering(ArpeggiatorSettings* arpSettings) {

	if (skippingRendering) {
		if (arpSettings != nullptr && arpSettings->mode != ArpMode::OFF) {
			uint32_t phaseIncrement = arpSettings->getPhaseIncrement(
			    params.param_final_values[params::GLOBAL_ARP_RATE - params::FIRST_GLOBAL]);
			getArp()->gatePos +=
			    (phaseIncrement >> 8) * (AudioEngine::audioSampleTimer - timeStartedSkippingRenderingArp);

			timeStartedSkippingRenderingArp = AudioEngine::audioSampleTimer;
		}
	}
}

uint32_t Sound::getGlobalLFOPhaseIncrement(LFO_ID lfoId, deluge::modulation::params::Global param) {
	const LFOConfig& config = snapshot_.lfo_config[lfoId];
	if (config.sync_level == SYNC_LEVEL_NONE) {
		return params.param_final_values[param - params::FIRST_GLOBAL];
	}
	return getSyncedLFOPhaseIncrement(config);
}

uint32_t Sound::getSyncedLFOPhaseIncrement(const LFOConfig& config) {
	uint32_t phaseIncrement =
	    (playbackHandler.getTimePerInternalTickInverse())
	    >> (deluge::plugin::index_of(SYNC_LEVEL_256TH) - deluge::plugin::index_of(config.sync_level));
	switch (config.sync_type) {
	case SYNC_TYPE_EVEN:
		// Nothing to do
		break;
	case SYNC_TYPE_TRIPLET:
		phaseIncrement = phaseIncrement * 3 / 2;
		break;
	case SYNC_TYPE_DOTTED:
		phaseIncrement = phaseIncrement * 2 / 3;
		break;
	}
	return phaseIncrement;
}

void Sound::resyncGlobalLFOs(bool reset_state) {
	if (!playbackHandler.isEitherClockActive()) {
		return; // no clock, no sync
	}
	// TOOD: convert to a guard clause. Now avoiding re-indenting the whole function.
	if (snapshot_.lfo_config[LFO1_ID].sync_level != SYNC_LEVEL_NONE) {

		timeStartedSkippingRenderingLFO =
		    AudioEngine::audioSampleTimer; // Resets the thing where the number of samples skipped is later converted
		                                   // into LFO phase increment

		globalLFO1.set_global_initial_phase(snapshot_.lfo_config[LFO1_ID], reset_state);

		uint32_t timeSinceLastTick;

		int64_t lastInternalTickDone = playbackHandler.getCurrentInternalTickCount(&timeSinceLastTick);

		// If we're right at the first tick, no need to do anything else!
		if (lastInternalTickDone || timeSinceLastTick) {
			uint32_t numInternalTicksPerPeriod =
			    3 << (deluge::plugin::index_of(SYNC_LEVEL_256TH)
			          - deluge::plugin::index_of(snapshot_.lfo_config[LFO1_ID].sync_level));
			switch (snapshot_.lfo_config[LFO1_ID].sync_type) {
			case SYNC_TYPE_EVEN:
				// Nothing to do
				break;
			case SYNC_TYPE_TRIPLET:
				numInternalTicksPerPeriod = numInternalTicksPerPeriod * 2 / 3;
				break;
			case SYNC_TYPE_DOTTED:
				numInternalTicksPerPeriod = numInternalTicksPerPeriod * 3 / 2;
				break;
			}
			uint32_t offsetTicks = (uint64_t)lastInternalTickDone % (uint16_t)numInternalTicksPerPeriod;

			// If we're right at a bar (or something), no need to do anyting else
			if (timeSinceLastTick || offsetTicks) {
				uint32_t timePerInternalTick = playbackHandler.getTimePerInternalTick();
				uint32_t timePerPeriod = numInternalTicksPerPeriod * timePerInternalTick;
				uint32_t offsetTime = offsetTicks * timePerInternalTick + timeSinceLastTick;
				globalLFO1.advance_phase((uint32_t)((float)offsetTime / timePerPeriod * 4294967296));
			}
		}
	}
	if (snapshot_.lfo_config[LFO3_ID].sync_level != SYNC_LEVEL_NONE) {

		timeStartedSkippingRenderingLFO =
		    AudioEngine::audioSampleTimer; // Resets the thing where the number of samples skipped is later converted
		                                   // into LFO phase increment

		globalLFO3.set_global_initial_phase(snapshot_.lfo_config[LFO3_ID], reset_state);

		uint32_t timeSinceLastTick;

		int64_t lastInternalTickDone = playbackHandler.getCurrentInternalTickCount(&timeSinceLastTick);

		// If we're right at the first tick, no need to do anything else!
		if (lastInternalTickDone || timeSinceLastTick) {
			uint32_t numInternalTicksPerPeriod =
			    3 << (deluge::plugin::index_of(SYNC_LEVEL_256TH)
			          - deluge::plugin::index_of(snapshot_.lfo_config[LFO3_ID].sync_level));
			switch (snapshot_.lfo_config[LFO3_ID].sync_type) {
			case SYNC_TYPE_EVEN:
				// Nothing to do
				break;
			case SYNC_TYPE_TRIPLET:
				numInternalTicksPerPeriod = numInternalTicksPerPeriod * 2 / 3;
				break;
			case SYNC_TYPE_DOTTED:
				numInternalTicksPerPeriod = numInternalTicksPerPeriod * 3 / 2;
				break;
			}
			uint32_t offsetTicks = (uint64_t)lastInternalTickDone % (uint16_t)numInternalTicksPerPeriod;

			// If we're right at a bar (or something), no need to do anyting else
			if (timeSinceLastTick || offsetTicks) {
				uint32_t timePerInternalTick = playbackHandler.getTimePerInternalTick();
				uint32_t timePerPeriod = numInternalTicksPerPeriod * timePerInternalTick;
				uint32_t offsetTime = offsetTicks * timePerInternalTick + timeSinceLastTick;
				globalLFO3.advance_phase((uint32_t)((float)offsetTime / timePerPeriod * 4294967296));
			}
		}
	}
}

// ------------------------------------
// ModControllable implementation
// ------------------------------------

// whichKnob is either which physical mod knob, or which MIDI CC code.
// For mod knobs, supply midiChannel as 255
// Returns false if fail due to insufficient RAM.
bool Sound::learnKnob(MIDICable* cable, ParamDescriptor paramDescriptor, uint8_t whichKnob, uint8_t modKnobMode,
                      uint8_t midiChannel, Song* song) {

	// If a mod knob
	if (midiChannel >= 16) {

		// If that knob was patched to something else...
		bool overwroteExistingKnob = (modKnobs[modKnobMode][whichKnob].paramDescriptor != paramDescriptor);

		modKnobs[modKnobMode][whichKnob].paramDescriptor = paramDescriptor;

		if (overwroteExistingKnob) {
			ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
		}

		return true;
	}

	// If a MIDI knob
	else {
		return ModControllableAudio::learnKnob(cable, paramDescriptor, whichKnob, modKnobMode, midiChannel, song);
	}
}

// Song may be NULL
void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {

	// We gotta do this for any backedUpParamManagers too!
	for (auto it = song->live.param_backup.map.lower_bound(
	         {static_cast<ModControllableAudio*>(this), static_cast<Clip*>(nullptr)});
	     it != song->live.param_backup.map.end() && it->first.first == this; ++it) {
		Clip* clip = it->first.second;
		if (clip) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    setupModelStackWithThreeMainThingsButNoNoteRow(modelStackMemory, song, this, clip, &it->second);
			ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
		}
		else {
			ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&it->second);
		}
	}

	song->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(this); // What does this do exactly, again?
}

const uint8_t patchedParamsWhichShouldBeZeroIfNoKnobAssigned[] = {
    params::LOCAL_PITCH_ADJUST, params::LOCAL_OSC_A_PITCH_ADJUST, params::LOCAL_OSC_B_PITCH_ADJUST,
    params::LOCAL_MODULATOR_0_PITCH_ADJUST, params::LOCAL_MODULATOR_1_PITCH_ADJUST};

void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(ParamManager* paramManager) {

	for (int32_t i = 0; i < sizeof(patchedParamsWhichShouldBeZeroIfNoKnobAssigned); i++) {
		int32_t p = patchedParamsWhichShouldBeZeroIfNoKnobAssigned[i];
		ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(paramManager, p);
	}
}

// Song may be NULL
void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithThreeMainThings* modelStack) {

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->paramManager->getPatchCableSet(modelStack);

	for (int32_t i = 0; i < sizeof(patchedParamsWhichShouldBeZeroIfNoKnobAssigned); i++) {
		ModelStackWithParamId* modelStackWithParamId =
		    modelStackWithParamCollection->addParamId(patchedParamsWhichShouldBeZeroIfNoKnobAssigned[i]);
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStackWithParamId->paramCollection->getAutoParamFromId(
		    modelStackWithParamId, false); // Don't allow creation
		if (modelStackWithAutoParam->autoParam) {
			ensureParamPresetValueWithoutKnobIsZero(modelStackWithAutoParam);
		}
	}
}

// Only worked for patched params
void Sound::ensureParamPresetValueWithoutKnobIsZero(ModelStackWithAutoParam* modelStack) {

	// If the param is automated, we'd better not try setting it to 0 - the user probably wants the automation
	if (modelStack->autoParam->isAutomated(*modelStack->doc())) {
		return;
	}

	for (int32_t k = 0; k < kNumModButtons; k++) {
		for (int32_t w = 0; w < kNumPhysicalModKnobs; w++) {
			if (modKnobs[k][w].paramDescriptor.isSetToParamWithNoSource(modelStack->paramId)) {
				return;
			}
		}
	}

	bool any_assigned = std::ranges::any_of(midi_knobs, [&](const MIDIKnob& knob) {
		return knob.paramDescriptor.isSetToParamWithNoSource(modelStack->paramId);
	});

	// No knobs were assigned to this param, so make it 0
	if (!any_assigned) {
		modelStack->autoParam->setCurrentValueWithNoReversionOrRecording(modelStack, 0);
	}
}

void Sound::ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(ParamManager* paramManager, int32_t p) {

	AutoParam* param = &paramManager->getPatchedParamSet()->params[p];

	// If the param is automated, we'd better not try setting it to 0 - the user probably wants the automation
	if (param->isAutomated(*paramManager->getPatchedParamSet()->docForParam(p))) {
		return;
	}

	for (int32_t k = 0; k < kNumModButtons; k++) {
		for (int32_t w = 0; w < kNumPhysicalModKnobs; w++) {
			if (modKnobs[k][w].paramDescriptor.isSetToParamWithNoSource(p)) {
				return;
			}
		}
	}

	bool any_assigned = std::ranges::any_of(midi_knobs, [&](const MIDIKnob& knob) {
		return knob.paramDescriptor.isSetToParamWithNoSource(p); //<
	});

	// No knobs were assigned to this param, so make it 0 (in the store — the persisted source — and the cursor).
	if (!any_assigned) {
		param->setCurrentValueBasicForSetup(*paramManager->getPatchedParamSet()->docForParam(p), 0);
	}
}

void Sound::doneReadingFromFile() {
	calculateEffectiveVolume();
	recalculateFineTuner();

	for (int32_t s = 0; s < kNumSources; s++) {
		sources[s].doneReadingFromFile(this);
	}

	setupUnisonDetuners(nullptr);
	setupUnisonStereoSpread();

	for (int32_t m = 0; m < kNumModulators; m++) {
		recalculateModulatorTransposer(m, nullptr);
	}

	// Parsing is complete, so refresh the DOM-owned scalars from the now-final engine fields. This is what a SoundDrum
	// needs: its adoptDrum bound the record with default values before its fields were parsed. A
	// SoundInstrument/fresh Sound isn't bound yet here (state_ == nullptr); bindState seeds it later, post-parse.
	reseedRecord();
}

void Sound::reseedRecord() {
	if (state_ == nullptr) {
		return;
	}
	seedDocSoundScalars();
	projectModControllableSnapshot();
	projectSnapshot();
	for (int32_t s = 0; s < kNumSources; s++) {
		sources[s].seedDocSourceScalars();
		sources[s].seedDocDxPatch();
		sources[s].seedDocRanges();
	}
}

// Set cents for master transpose
void Sound::set_cents(int8_t new_cents) {
	if (state_ != nullptr) {
		state_->cents = new_cents;
		snapshot_.cents = state_->cents;
	}
	else {
		snapshot_.cents = new_cents;
	}
	recalculateFineTuner();
}

// Setup master transpose
void Sound::recalculateFineTuner() {
	snapshot_.fine_tuner.setup((int32_t)snapshot_.cents * 42949672);
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it.
void Sound::voiceUnassigned(ModelStackWithSoundFlags* modelStack) {
	reassessRenderSkippingStatus(modelStack);
}

// modelStack may be NULL if no voices currently active
void Sound::setupUnisonDetuners(ModelStackWithSoundFlags* modelStack) {
	computeUnisonDetuners();
	recalculateAllVoicePhaseIncrements(modelStack); // Can handle NULL
}

void Sound::computeUnisonDetuners() {
	if (snapshot_.num_unison != 1) {
		int32_t detuneScaled = (int32_t)snapshot_.unison_detune * 42949672;
		int32_t lowestVoice = -(detuneScaled >> 1);
		int32_t voiceSpacing = detuneScaled / (snapshot_.num_unison - 1);

		for (int32_t u = 0; u < snapshot_.num_unison; u++) {

			// Middle unison part gets no detune
			if ((snapshot_.num_unison & 1) && u == ((snapshot_.num_unison - 1) >> 1)) {
				snapshot_.unison_detuners[u].setNoDetune();
			}
			else {
				snapshot_.unison_detuners[u].setup(lowestVoice + voiceSpacing * u);
			}
		}
	}
}

void Sound::setupUnisonStereoSpread() {
	if (snapshot_.num_unison != 1) {
		int32_t spreadScaled = (int32_t)snapshot_.unison_stereo_spread * 42949672;
		int32_t lowestVoice = -(spreadScaled >> 1);
		int32_t voiceSpacing = spreadScaled / (snapshot_.num_unison - 1);

		for (int32_t u = 0; u < snapshot_.num_unison; u++) {
			// alternate the voices like -2 +1 0 -1 +2 for more balanced
			// interaction with detune
			bool isOdd = std::min(u, snapshot_.num_unison - 1 - u) & 1;
			int32_t sign = isOdd ? -1 : 1;

			snapshot_.unison_pan[u] = sign * (lowestVoice + voiceSpacing * u);
		}
	}
}

void Sound::calculateEffectiveVolume() {
	// volumeNeutralValueForUnison = (float)getParamNeutralValue(params::LOCAL_VOLUME) / sqrt(numUnison);
	volumeNeutralValueForUnison = (float)134217728 / sqrtf(snapshot_.num_unison);
}

// May change mod knob functions. You must update mod knob levels after calling this
void Sound::setSynthMode(SynthMode value, Song* song) {

	killAllVoices(); // This saves a lot of potential problems, to do with samples playing. E002 was being caused

	SynthMode oldSynthMode = getSynthMode();
	assignSynthMode(value);
	const SynthMode synthMode = getSynthMode();

	// Change mod knob functions over. Switching *to* FM...
	if (synthMode == SynthMode::FM && oldSynthMode != SynthMode::FM) {
		for (int32_t f = 0; f < kNumModButtons; f++) {
			if (modKnobs[f][0].paramDescriptor.isJustAParam() && modKnobs[f][1].paramDescriptor.isJustAParam()) {
				int32_t p0 = modKnobs[f][0].paramDescriptor.getJustTheParam();
				int32_t p1 = modKnobs[f][1].paramDescriptor.getJustTheParam();

				if ((p0 == params::LOCAL_LPF_RESONANCE || p0 == params::LOCAL_HPF_RESONANCE
				     || p0 == params::UNPATCHED_START + params::UNPATCHED_BASS)
				    && (p1 == params::LOCAL_LPF_FREQ || p1 == params::LOCAL_HPF_FREQ
				        || p1 == params::UNPATCHED_START + params::UNPATCHED_TREBLE)) {
					modKnobs[f][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_MODULATOR_1_VOLUME);
					modKnobs[f][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_MODULATOR_0_VOLUME);
				}
			}
		}
		// switch the filters off so they don't render unless deliberately enabled
		setLpfMode(FilterMode::OFF);
		setHpfMode(FilterMode::OFF);
	}

	// ... and switching *from* FM...
	if (synthMode != SynthMode::FM && oldSynthMode == SynthMode::FM) {
		for (int32_t f = 0; f < kNumModButtons; f++) {
			if (modKnobs[f][0].paramDescriptor.isSetToParamWithNoSource(params::LOCAL_MODULATOR_1_VOLUME)
			    && modKnobs[f][1].paramDescriptor.isSetToParamWithNoSource(params::LOCAL_MODULATOR_0_VOLUME)) {
				modKnobs[f][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_RESONANCE);
				modKnobs[f][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
			}
		}
		// switch the filters back on if needed
		if (modControllableSnapshot_.effects.filter.lpf_mode == FilterMode::OFF) {
			setLpfMode(FilterMode::TRANSISTOR_24DB);
		}
		if (modControllableSnapshot_.effects.filter.hpf_mode == FilterMode::OFF) {
			setHpfMode(FilterMode::HPLADDER);
		}
	}

	// in Sound::maySourcePatchToParam, the filter mode is checked to determine if patch cables are allowed
	// Setup patching after changing filter mode above to ensure PatchCableAcceptance is correctly determined
	setupPatchingForAllParamManagers(song);
}

void Sound::setModulatorTranspose(int32_t m, int32_t value, ModelStackWithSoundFlags* modelStack) {
	assignModulatorTranspose(m, value);
	recalculateAllVoicePhaseIncrements(modelStack);
}

void Sound::setModulatorCents(int32_t m, int32_t value, ModelStackWithSoundFlags* modelStack) {
	assignModulatorCents(m, static_cast<int8_t>(value));
	recalculateModulatorTransposer(m, modelStack);
}

// Can handle NULL modelStack, which you'd only want to do if no Voices active
void Sound::recalculateModulatorTransposer(uint8_t m, ModelStackWithSoundFlags* modelStack) {
	snapshot_.modulator_transposers[m].setup((int32_t)snapshot_.modulator_cents[m] * 42949672);
	recalculateAllVoicePhaseIncrements(modelStack); // Can handle NULL
}

// Can handle NULL modelStack, which you'd only want to do if no Voices active
void Sound::recalculateAllVoicePhaseIncrements(ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // walks this Sound's voices, which the render edits (free on the audio tier)

	if (voices_.empty() || modelStack == nullptr) {
		return; // These two "should" always be false in tandem...
	}

	for (const ActiveVoice& voice : voices_) {
		voice->calculate_phase_increments(*modelStack);
	}
}

void Sound::setNumUnison(int32_t newNum, ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // the render walks and edits this Sound's voices
	int32_t oldNum = snapshot_.num_unison;

	assignNumUnison(static_cast<uint8_t>(newNum));
	setupUnisonDetuners(modelStack); // Can handle NULL. Also calls recalculateAllVoicePhaseIncrements()
	setupUnisonStereoSpread();
	calculateEffectiveVolume();

	// Effective volume has changed. Need to pass that change onto Voices
	for (const ActiveVoice& voice : voices_) {
		if (getSynthMode() == SynthMode::SUBTRACTIVE) {
			for (int32_t s = 0; s < kNumSources; s++) {
				bool sourceEverActive = modelStack->checkSourceEverActive(s);

				if (sourceEverActive && getSynthMode() != SynthMode::FM && sources[s].oscType == OscType::SAMPLE
				    && voice->guides[s].audioFileHolder && voice->guides[s].audioFileHolder->audioFile) {

					// For samples, set the current play pos for the new unison part, if num unison went up
					if (newNum > oldNum) {

						SourceSlot* newPart = &voice->kernel.unison_parts[oldNum].slots[s];
						SourceSlot* oldPart = &voice->kernel.unison_parts[oldNum - 1].slots[s];

						newPart->active = oldPart->active;

						if (newPart->active) {
							newPart->osc_pos = oldPart->osc_pos;
							newPart->phase_increment_stored_value = oldPart->phase_increment_stored_value;
							newPart->carrier_feedback = oldPart->carrier_feedback;

							SamplePlaybackAdapter& newSample = voice->ports(oldNum, s).sample;
							if (!newSample.cloneVoiceSampleFrom(voice->ports(oldNum - 1, s).sample)) {
								newPart->active = false;
								continue;
							}

							VoiceSample& newVoiceSample = *newSample.voiceSample();

							// Don't do any caching for new part. Old parts will stop using their cache anyway
							// because their pitch will have changed
							newVoiceSample.stopUsingCache(
							    &voice->guides[s], (Sample*)voice->guides[s].audioFileHolder->audioFile,
							    voice->priority_rating(),
							    voice->guides[s].getLoopingType(sources[s]) == LoopType::LOW_LEVEL);
							// TODO: should really check success of that...
						}
					}
					else if (newNum < oldNum) {
						for (int32_t l = 0; l < kNumClustersLoadedAhead; l++) {
							voice->kernel.unison_parts[newNum].slots[s].unassign(false);
						}
					}
				}
			}
		}
	}
}

void Sound::setUnisonDetune(int32_t newAmount, ModelStackWithSoundFlags* modelStack) {
	assignUnisonDetune(static_cast<int8_t>(newAmount));
	setupUnisonDetuners(modelStack); // Can handle NULL
}

void Sound::setUnisonStereoSpread(int32_t newAmount) {
	assignUnisonStereoSpread(static_cast<uint8_t>(newAmount));
	setupUnisonStereoSpread();
}

bool Sound::anyNoteIsOn() {

	ArpeggiatorSettings* arpSettings = getArpSettings();

	if (arpSettings != nullptr && arpSettings->mode != ArpMode::OFF) {
		return (getArp()->hasAnyInputNotesActive());
	}

	return !voices_.empty();
}

bool Sound::hasFilters() {
	return (modControllableSnapshot_.effects.filter.lpf_mode != FilterMode::OFF
	        || modControllableSnapshot_.effects.filter.hpf_mode != FilterMode::OFF);
}

void Sound::readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
                               int32_t readAutomationUpToPos) {
	// Read the sound param block DOM-first into both the patched and unpatched DOM stores (initParams wrote each
	// default straight into them via docForParam), then project each engine RT cursor from its store: the DOM holds
	// the authoritative current_value, not just the nodes. <patchCables> is the Deluge
	// instrument's to decode (older formats included); the bridge fills the cable set from it.
	UnpatchedParamSet* unpatched = paramManager->getUnpatchedParamSet();
	PatchedParamSet* patched = paramManager->getPatchedParamSet();
	ParamCollectionSummary* unpatchedSummary = paramManager->getUnpatchedParamSetSummary();
	ParamCollectionSummary* patchedSummary = paramManager->getPatchedParamSetSummary();
	unpatched->bindDomStore();
	patched->bindDomStore();
	deluge::project::DocParamSet* uStore = unpatched->domStore();
	deluge::project::DocParamSet* pStore = patched->domStore();
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "patchCables")) {
			deluge::bridge::loadCables(*paramManager->getPatchCableSet(),
			                           *deluge::storage::treeReaderOf(reader).currentValue(), readAutomationUpToPos);
			reader.exitTag("patchCables");
		}
		else if (deluge::bridge::readSoundParamTagToDoc(reader, tagName, *pStore, *uStore, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	reader.match('}');
	unpatched->projectFromStoreAfterRead(unpatchedSummary);
	patched->projectFromStoreAfterRead(patchedSummary);
}

// paramManager only required for old old song files, or for presets (because you'd be wanting to extract the
// defaultParams into it) arpSettings optional - no need if you're loading a new V2.0+ song where Instruments are all
// separate from Clips and won't store any arp stuff
Error Sound::readFromFile(Deserializer& reader, ModelStackWithModControllable* modelStack,
                          int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings) {
	// The sound's settings are the Deluge instrument's: decoded from the element, onto the state this sound starts
	// from, and applied once the reader below has read the params, the effects and the rest.
	const deluge::serialization::Value* element = deluge::storage::treeReaderOf(reader).currentElement();
	if (element == nullptr) {
		return Error::FILE_CORRUPTED;
	}
	loadedElement_ = *element;
	deluge::instrument::SoundState decoded = seededState();
	if (!deluge::instrument::decode(*element, decoded,
	                                {.input_tick_magnitude = modelStack->song->getInputTickMagnitude()})) {
		return Error::FILE_CORRUPTED;
	}
	delayTypePending = !deluge::effects::saves_delay_type(*element);

	char const* tagName;

	ParamManagerForTimeline paramManager;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		Error result = readTagFromFileOrError(reader, tagName, &paramManager, readAutomationUpToPos, arpSettings,
		                                      modelStack->song);
		if (result == Error::NONE) {}
		else if (result != Error::RESULT_TAG_UNUSED) {
			return result;
		}
		else {
			reader.exitTag(tagName);
		}
	}

	if (Error error = applyState(decoded); error != Error::NONE) {
		return error;
	}

	// old FM patches can have a filter mode saved in them even though it wouldn't have rendered at the time
	if (getSynthMode() == SynthMode::FM && song_firmware_version < FirmwareVersion::community({1, 2, 0})) {
		setHpfMode(FilterMode::OFF);
		setLpfMode(FilterMode::OFF);
	}

	// A sound with params files them with the Song
	if (paramManager.containsAnyMainParamCollections()) {
		modelStack->song->live.param_backup.backUp(this, (Clip*)modelStack->getTimelineCounterAllowNull(),
		                                           &paramManager, true);
	}

	doneReadingFromFile();

	// Ensure all MIDI knobs reference correct volume...
	for (MIDIKnob& knob : midi_knobs) {
		ensureKnobReferencesCorrectVolume(knob);
	}

	return Error::NONE;
}

Error Sound::createParamManagerForLoading(ParamManagerForTimeline* paramManager) {

	Error error = paramManager->setupWithPatching();
	if (error != Error::NONE) {
		return error;
	}

	initParams(paramManager);

	paramManager->getUnpatchedParamSet()->params[params::UNPATCHED_SIDECHAIN_SHAPE].setCurrentValueBasicForSetup(
	    *paramManager->getUnpatchedParamSet()->docForParam(params::UNPATCHED_SIDECHAIN_SHAPE),
	    2147483647); // Hmm, why this here? Obviously I had some reason...
	return Error::NONE;
}

void Sound::writeParamsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation) {
	// Serialize from the value DOM (deluge::project). serializeSoundParams emits the patched + unpatched flat attrs,
	// the shared ModControllableAudio block, the mid-block writeOpeningTagEnd, the <envelope*> groups, the
	// <patchCables> block and the shared tags.
	deluge::project::DocParamManager doc = deluge::bridge::toDocParamManagerFromStore(*paramManager);
	deluge::bridge::serializeSoundParams(**deluge::bridge::storeFor(doc, params::Kind::PATCHED),
	                                     **deluge::bridge::storeFor(doc, params::Kind::UNPATCHED_SOUND),
	                                     paramManager->getPatchCableSet()->records().records(), doc.cable_depths,
	                                     writer, writeAutomation);
}

// Copy the DOM-owned scalars engine->record. Assumes state_ is bound. Run from bindState and re-run at
// doneReadingFromFile (see those for the bind-vs-parse ordering that makes the re-run necessary for SoundDrums).
void Sound::seedScalarsInto(deluge::instrument::SoundState& d) const {
	d.voice_priority = snapshot_.voice_priority;
	d.max_voice_count = maxVoiceCount;
	d.side_chain_send_level = sideChainSendLevel;
	d.output_midi_channel = outputMidiChannel;
	d.output_midi_note_for_drum = outputMidiNoteForDrum;
	d.oscillator_sync = snapshot_.oscillator_sync;
	// Top-level scalars (native fields kept as hot caches; record is authoritative for save). SoundDrums bind before
	// parse, so the re-seed at doneReadingFromFile is what captures their loaded values into the record.
	d.polyphonic = polyphonic;
	d.synth_mode = snapshot_.synth_mode;
	d.transpose = snapshot_.transpose;
	d.cents = snapshot_.cents; // transpose lives on the snapshot; the record seeds from it (bind-time)
	d.num_unison = snapshot_.num_unison;
	d.unison_detune = snapshot_.unison_detune;
	d.unison_stereo_spread = snapshot_.unison_stereo_spread;
	d.modulator1_to_modulator0 = snapshot_.modulator1_to_modulator0;
	for (int32_t i = 0; i < 2; i++) {
		d.modulator_transpose[i] = snapshot_.modulator_transpose[i];
		d.modulator_cents[i] = snapshot_.modulator_cents[i];
		d.osc_retrigger_phase[i] = snapshot_.osc_retrigger_phase[i];
		d.modulator_retrigger_phase[i] = snapshot_.modulator_retrigger_phase[i];
	}
	for (int32_t i = 0; i < LFO_COUNT; i++) {
		d.lfo_wave_type[i] = snapshot_.lfo_config[i].wave_type;
		d.lfo_sync_type[i] = snapshot_.lfo_config[i].sync_type;
		d.lfo_sync_level[i] = snapshot_.lfo_config[i].sync_level;
	}
}

void Sound::seedDocSoundScalars() {
	if (state_ != nullptr) {
		seedScalarsInto(*state_);
	}
}

void Sound::projectSnapshot() {
	// Re-derive the engine's hot-field read-view from the authoritative SoundState. No-op when unbound (preview /
	// GlobalEffectable): those sounds write snapshot_ directly and have no record to project from.
	if (state_ == nullptr) {
		return;
	}
	snapshot_.transpose = state_->transpose;
	snapshot_.cents = state_->cents;
	recalculateFineTuner();
	snapshot_.num_unison = state_->num_unison;
	snapshot_.unison_detune = state_->unison_detune;
	snapshot_.unison_stereo_spread = state_->unison_stereo_spread;
	computeUnisonDetuners();
	setupUnisonStereoSpread();
	snapshot_.synth_mode = static_cast<SynthMode>(state_->synth_mode);
	snapshot_.oscillator_sync = state_->oscillator_sync;
	snapshot_.voice_priority = static_cast<VoicePriority>(state_->voice_priority);
	snapshot_.modulator1_to_modulator0 = state_->modulator1_to_modulator0;
	for (int32_t i = 0; i < kNumModulators; i++) {
		snapshot_.modulator_cents[i] = state_->modulator_cents[i];
		snapshot_.modulator_transposers[i].setup((int32_t)snapshot_.modulator_cents[i] * 42949672);
		snapshot_.modulator_transpose[i] = state_->modulator_transpose[i];
		snapshot_.modulator_retrigger_phase[i] = state_->modulator_retrigger_phase[i];
	}
	for (int32_t s = 0; s < kNumSources; s++) {
		snapshot_.osc_retrigger_phase[s] = state_->osc_retrigger_phase[s];
	}
	for (int32_t l = 0; l < LFO_COUNT; l++) {
		snapshot_.lfo_config[l].wave_type = static_cast<LFOType>(state_->lfo_wave_type[l]);
		snapshot_.lfo_config[l].sync_type = static_cast<SyncType>(state_->lfo_sync_type[l]);
		snapshot_.lfo_config[l].sync_level = static_cast<SyncLevel>(state_->lfo_sync_level[l]);
	}
}

static_assert(deluge::instrument::kMaxUnisonVoices == kMaxNumVoicesUnison
              && deluge::instrument::kMaxUnisonDetune == kMaxUnisonDetune
              && deluge::instrument::kMaxUnisonSpread == kMaxUnisonStereoSpread);

Error Sound::applyState(const deluge::instrument::SoundState& state) {
	polyphonic = state.polyphonic;
	maxVoiceCount = state.max_voice_count;
	sideChainSendLevel = state.side_chain_send_level;
	outputMidiChannel = state.output_midi_channel;
	outputMidiNoteForDrum = state.output_midi_note_for_drum;
	setVoicePriority(state.voice_priority);
	assignSynthMode(state.synth_mode);
	setTranspose(state.transpose);
	set_cents(state.cents);
	assignNumUnison(state.num_unison);
	assignUnisonDetune(state.unison_detune);
	assignUnisonStereoSpread(state.unison_stereo_spread);
	setOscillatorSync(state.oscillator_sync);
	setModulator1ToModulator0(state.modulator1_to_modulator0);
	for (int32_t m = 0; m < kNumModulators; m++) {
		assignModulatorTranspose(m, state.modulator_transpose[m]);
		assignModulatorCents(m, state.modulator_cents[m]);
		setModulatorRetriggerPhase(m, state.modulator_retrigger_phase[m]);
	}
	for (int32_t l = 0; l < LFO_COUNT; l++) {
		setLfoWaveType(l, state.lfo_wave_type[l]);
		setLfoSync(l, state.lfo_sync_type[l], state.lfo_sync_level[l]);
	}
	resyncGlobalLFOs();

	// The mod-FX type through its setter. A ring type whose ring cannot be allocated keeps its
	// type and passes the audio through; the next effects edit on this sound retries.
	deluge::effects::EffectStates effects = state.effects;
	if (effects.mod_fx.type != this->effects().mod_fx.type && !setModFXType(effects.mod_fx.type)) {
		display->displayError(Error::INSUFFICIENT_RAM);
	}
	effects.mod_fx.type = this->effects().mod_fx.type;
	setEffects(effects);
	core().compressor().dsp().set_threshold(effects.compressor.threshold); // what a save before any render writes back

	for (int32_t s = 0; s < kNumSources; s++) {
		const deluge::instrument::SourceState& from = state.sources[s];
		Source& source = sources[s];
		setOscRetriggerPhase(s, state.osc_retrigger_phase[s]);
		source.setOscType(from.osc_type);
		source.transpose = from.transpose;
		source.cents = from.cents;
		source.repeatMode = from.repeat_mode;
		source.timeStretchAmount = from.time_stretch_amount;
		source.sampleControls.interpolationMode = from.interpolation_mode;
		source.sampleControls.pitchAndSpeedAreIndependent = from.pitch_and_speed_independent;
		source.sampleControls.reversed = from.reversed;
		if (from.dx_patch) {
			DxPatch* patch = source.ensureDxPatch();
			std::ranges::copy(from.dx_patch->params, patch->params.begin());
			// A file names an engine only when it is not "auto" (0), and only then is the engine chosen again: an auto
			// patch keeps the engine its new patch was given.
			if (from.dx_patch->engine_mode != 0) {
				patch->setEngineMode(from.dx_patch->engine_mode);
			}
			patch->random_detune = from.dx_patch->random_detune;
		}

		// The ranges, of the kind the oscillator type plays.
		const bool wavetable = from.osc_type == OscType::WAVETABLE;
		source.ranges.clear();
		(void)source.ranges.changeType(wavetable ? sizeof(MultiWaveTableRange) : sizeof(MultisampleRange));
		for (const deluge::instrument::RangeState& range : from.ranges) {
			MultiRangeArray::RangeVariant variant =
			    wavetable ? MultiRangeArray::RangeVariant(std::in_place_type<MultiWaveTableRange>)
			              : MultiRangeArray::RangeVariant(std::in_place_type<MultisampleRange>);
			MultiRange* built = MultiRangeArray::variantToRange(variant);
			built->topNote = range.top_note;
			built->getAudioFileHolder()->filePath = range.file_path;
			if (!wavetable) {
				auto* holder = static_cast<SampleHolderForVoice*>(built->getAudioFileHolder());
				holder->startPos = range.start_pos;
				holder->endPos = range.end_pos;
				holder->loopStartPos = range.loop_start_pos;
				holder->loopEndPos = range.loop_end_pos;
				holder->transpose = range.transpose;
				holder->cents = range.cents;
			}
			if (Error error = source.ranges.insertVariant(std::ssize(source.ranges), std::move(variant));
			    error != Error::NONE) {
				return error;
			}
		}
	}
	return Error::NONE;
}

deluge::instrument::SoundState Sound::seededState() const {
	deluge::instrument::SoundState seeded;
	seedScalarsInto(seeded);
	seeded.effects = modControllableSnapshot_.effects; // what the render plays
	for (int32_t s = 0; s < kNumSources; s++) {
		sources[s].seedScalarsInto(seeded.sources[s]);
		sources[s].seedDxPatchInto(seeded.sources[s]);
		sources[s].seedRangesInto(seeded.sources[s]);
	}
	return seeded;
}

void Sound::bindState() {
	state_ = &ownedState_;
	deluge::instrument::SoundState* d = state_;
	// Seed the DOM-owned scalars from the engine caches. Correct for a SoundInstrument (bound at assignMissingIds,
	// after its fields are parsed) and for a freshly-created Sound. A SoundDrum is bound BEFORE parse (adoptDrum runs
	// first), so this seed is stale for drums — doneReadingFromFile re-runs it once parsing is done.
	seedDocSoundScalars();
	projectSnapshot(); // derive the render read-view from the now-seeded record
	bindEffects(&d->effects);
	for (int32_t s = 0; s < kNumSources; s++) {
		sources[s].bindState(&d->sources[s]);
	}
}

// DOM-owned scalars: each setter writes the authoritative record (when bound) and the native cache. Render / note-on /
// MIDI-send read the cache; save reads the record. Guard on state_ so pre-bind writes (load, preview) land in the
// cache and get picked up by the bind-time seed.
void Sound::setVoicePriority(VoicePriority value) {
	if (state_ != nullptr) {
		state_->voice_priority = value;
		snapshot_.voice_priority = static_cast<VoicePriority>(state_->voice_priority);
	}
	else {
		snapshot_.voice_priority = value;
	}
}
void Sound::setMaxVoiceCount(uint8_t value) {
	maxVoiceCount = value;
	if (state_ != nullptr) {
		state_->max_voice_count = value;
	}
}
void Sound::setSideChainSendLevel(int32_t value) {
	sideChainSendLevel = value;
	if (state_ != nullptr) {
		state_->side_chain_send_level = value;
	}
}
void Sound::setOutputMidiChannel(uint8_t value) {
	outputMidiChannel = value;
	if (state_ != nullptr) {
		state_->output_midi_channel = value;
	}
}
void Sound::setOutputMidiNoteForDrum(uint8_t value) {
	outputMidiNoteForDrum = value;
	if (state_ != nullptr) {
		state_->output_midi_note_for_drum = value;
	}
}
void Sound::setOscillatorSync(bool value) {
	if (state_ != nullptr) {
		state_->oscillator_sync = value;
		snapshot_.oscillator_sync = state_->oscillator_sync;
	}
	else {
		snapshot_.oscillator_sync = value;
	}
}
void Sound::setPolyphonic(PolyphonyMode value) {
	polyphonic = value;
	if (state_ != nullptr) {
		state_->polyphonic = value;
	}
}
void Sound::assignSynthMode(SynthMode value) {
	if (state_ != nullptr) {
		state_->synth_mode = value;
		snapshot_.synth_mode = static_cast<SynthMode>(state_->synth_mode);
	}
	else {
		snapshot_.synth_mode = value;
	}
}

void Sound::setTranspose(int16_t value) {
	// Bound → write the authoritative record then reproject the read-view (a derivation, not a dual-write);
	// unbound (preview / GlobalEffectable) → write the snapshot directly (no record).
	if (state_ != nullptr) {
		state_->transpose = value;
		snapshot_.transpose = state_->transpose;
	}
	else {
		snapshot_.transpose = value;
	}
}
void Sound::setModulator1ToModulator0(bool value) {
	if (state_ != nullptr) {
		state_->modulator1_to_modulator0 = value;
		snapshot_.modulator1_to_modulator0 = state_->modulator1_to_modulator0;
	}
	else {
		snapshot_.modulator1_to_modulator0 = value;
	}
}
void Sound::setOscRetriggerPhase(int32_t s, uint32_t value) {
	if (state_ != nullptr) {
		state_->osc_retrigger_phase[s] = value;
		snapshot_.osc_retrigger_phase[s] = state_->osc_retrigger_phase[s];
	}
	else {
		snapshot_.osc_retrigger_phase[s] = value;
	}
}
void Sound::setModulatorRetriggerPhase(int32_t m, uint32_t value) {
	if (state_ != nullptr) {
		state_->modulator_retrigger_phase[m] = value;
		snapshot_.modulator_retrigger_phase[m] = state_->modulator_retrigger_phase[m];
	}
	else {
		snapshot_.modulator_retrigger_phase[m] = value;
	}
}
void Sound::assignNumUnison(uint8_t value) {
	if (state_ != nullptr) {
		state_->num_unison = value;
		snapshot_.num_unison = state_->num_unison;
	}
	else {
		snapshot_.num_unison = value;
	}
}
void Sound::assignUnisonDetune(int8_t value) {
	if (state_ != nullptr) {
		state_->unison_detune = value;
		snapshot_.unison_detune = state_->unison_detune;
	}
	else {
		snapshot_.unison_detune = value;
	}
}
void Sound::assignUnisonStereoSpread(uint8_t value) {
	if (state_ != nullptr) {
		state_->unison_stereo_spread = value;
		snapshot_.unison_stereo_spread = state_->unison_stereo_spread;
	}
	else {
		snapshot_.unison_stereo_spread = value;
	}
}
void Sound::assignModulatorCents(int32_t m, int8_t value) {
	if (state_ != nullptr) {
		state_->modulator_cents[m] = value;
		snapshot_.modulator_cents[m] = state_->modulator_cents[m];
	}
	else {
		snapshot_.modulator_cents[m] = value;
	}
}
void Sound::assignModulatorTranspose(int32_t m, int16_t value) {
	if (state_ != nullptr) {
		state_->modulator_transpose[m] = value;
		snapshot_.modulator_transpose[m] = state_->modulator_transpose[m];
	}
	else {
		snapshot_.modulator_transpose[m] = value;
	}
}
void Sound::setLfoWaveType(int32_t lfoId, LFOType value) {
	if (state_ != nullptr) {
		state_->lfo_wave_type[lfoId] = value;
		snapshot_.lfo_config[lfoId].wave_type = static_cast<LFOType>(state_->lfo_wave_type[lfoId]);
	}
	else {
		snapshot_.lfo_config[lfoId].wave_type = value;
	}
}
void Sound::setLfoSync(int32_t lfoId, SyncType syncType, SyncLevel syncLevel) {
	if (state_ != nullptr) {
		state_->lfo_sync_type[lfoId] = syncType;
		state_->lfo_sync_level[lfoId] = syncLevel;
		snapshot_.lfo_config[lfoId].sync_type = static_cast<SyncType>(state_->lfo_sync_type[lfoId]);
		snapshot_.lfo_config[lfoId].sync_level = static_cast<SyncLevel>(state_->lfo_sync_level[lfoId]);
	}
	else {
		snapshot_.lfo_config[lfoId].sync_type = syncType;
		snapshot_.lfo_config[lfoId].sync_level = syncLevel;
	}
}

void Sound::writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager,
                        ArpeggiatorSettings* arpSettings, const char* pathAttribute) {
	// The instrument writes its own settings; the effects', the params, the arpeggiator and the mod knobs sit between
	// its pieces in the file.
	std::optional<deluge::instrument::SoundState> unbound;
	if (state_ == nullptr) {
		unbound = seededState(); // a sound under preview or audition saves what its runtime fields hold
	}
	const deluge::instrument::SoundState& state = state_ != nullptr ? *state_ : *unbound;
	const deluge::instrument::SoundPieces pieces = deluge::instrument::encode_pieces(
	    state, {.input_tick_magnitude = currentSong->getInputTickMagnitude()}, savedCompressorThreshold());

	writer.writeValue(pieces.head);
	writer.writeValue(pieces.effect_attributes);
	// Community Firmware parameters (always write them after the official ones)
	if (pathAttribute) {
		writer.writeAttribute("path", pathAttribute);
	}
	writer.writeValue(pieces.max_voices);
	writer.writeOpeningTagEnd();

	writer.writeValue(pieces.body);

	if (paramManager) {
		writer.writeOpeningTagBeginning("defaultParams");
		Sound::writeParamsToFile(writer, paramManager, false);
		writer.writeClosingTag("defaultParams");
	}

	if (arpSettings != nullptr) {
		writer.writeOpeningTagBeginning("arpeggiator");
		arpSettings->writeCommonParamsToFile(writer, currentSong);
		writer.closeTag();
	}

	// Mod knobs (read the authoritative record; cache fallback only for an unbound Sound)
	writer.writeArrayStart("modKnobs");
	for (int32_t k = 0; k < kNumModButtons; k++) {
		for (int32_t w = 0; w < kNumPhysicalModKnobs; w++) {
			ParamDescriptor pd;
			pd.data = modKnobs[k][w].paramDescriptor.data; // the controller mappings' runtime copy
			writer.writeOpeningTagBeginning("modKnob", true);
			writer.writeAttribute("controlsParam",
			                      params::paramNameForFile(params::Kind::UNPATCHED_SOUND, pd.getJustTheParam()), false);
			if (!pd.isJustAParam()) {
				writer.writeAttribute("patchAmountFromSource", sourceToString(pd.getTopLevelSource()), false);

				if (pd.hasSecondSource()) {
					writer.writeAttribute("patchAmountFromSecondSource", sourceToString(pd.getSecondSourceFromTop()));
				}
			}
			writer.closeTag(true);
		}
	}
	writer.writeArrayEnding("modKnobs");

	writer.writeValue(pieces.midi_output);
	writer.writeValue(pieces.delay);
	writeMidiKnobsToFile(writer);
	writer.writeValue(pieces.effect_tags);
}

int16_t Sound::getMaxOscTranspose(InstrumentClip* clip) {

	int32_t maxRawOscTranspose = -32768;
	for (int32_t s = 0; s < kNumSources; s++) {
		if (getSynthMode() == SynthMode::FM || sources[s].oscType != OscType::SAMPLE) {
			maxRawOscTranspose = std::max<int32_t>(maxRawOscTranspose, sources[s].transpose);
		}
	}

	if (getSynthMode() == SynthMode::FM) {
		maxRawOscTranspose = std::max(maxRawOscTranspose, (int32_t)snapshot_.modulator_transpose[0]);
		maxRawOscTranspose = std::max(maxRawOscTranspose, (int32_t)snapshot_.modulator_transpose[1]);
	}

	if (maxRawOscTranspose == -32768) {
		maxRawOscTranspose = 0;
	}

	ArpeggiatorSettings* arpSettings = getArpSettings(clip);

	if (arpSettings != nullptr && arpSettings->mode != ArpMode::OFF) {
		maxRawOscTranspose += (arpSettings->numOctaves - 1) * 12;
	}

	return maxRawOscTranspose + snapshot_.transpose;
}

int16_t Sound::getMinOscTranspose() {

	int32_t minRawOscTranspose = 32767;
	for (int32_t s = 0; s < kNumSources; s++) {
		if (getSynthMode() == SynthMode::FM || sources[s].oscType != OscType::SAMPLE) {
			minRawOscTranspose = std::min<int32_t>(minRawOscTranspose, sources[s].transpose);
		}
	}

	if (getSynthMode() == SynthMode::FM) {
		minRawOscTranspose = std::min(minRawOscTranspose, (int32_t)snapshot_.modulator_transpose[0]);
		minRawOscTranspose = std::min(minRawOscTranspose, (int32_t)snapshot_.modulator_transpose[1]);
	}

	if (minRawOscTranspose == 32767) {
		minRawOscTranspose = 0;
	}

	return minRawOscTranspose + snapshot_.transpose;
}

deluge::storage::Walk Sound::walkSourceDemands(deluge::storage::SampleLoadVisitor& visitor) {

	for (int32_t s = 0; s < kNumSources; s++) {
		if (sources[s].oscType == OscType::SAMPLE || sources[s].oscType == OscType::WAVETABLE) {
			if (sources[s].walkSampleDemands(visitor) == deluge::storage::Walk::Stop) {
				return deluge::storage::Walk::Stop;
			}
		}
	}

	return deluge::storage::Walk::Continue;
}

bool Sound::envelopeHasSustainCurrently(int32_t e, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	// These params are fetched "pre-LPF"
	return (patchedParams->getValue(params::LOCAL_ENV_0_SUSTAIN + e) != -2147483648
	        || patchedParams->getValue(params::LOCAL_ENV_0_DECAY + e)
	               > patchedParams->getValue(params::LOCAL_ENV_0_RELEASE + e));
}

bool Sound::envelopeHasSustainEver(int32_t e, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	return (patchedParams->params[params::LOCAL_ENV_0_SUSTAIN + e].containsSomething(
	            *patchedParams->docForParam(params::LOCAL_ENV_0_SUSTAIN + e), -2147483648)
	        || patchedParams->params[params::LOCAL_ENV_0_DECAY + e].isAutomated(
	            *patchedParams->docForParam(params::LOCAL_ENV_0_DECAY + e))
	        || patchedParams->params[params::LOCAL_ENV_0_RELEASE + e].isAutomated(
	            *patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE + e))
	        || patchedParams->getValue(params::LOCAL_ENV_0_DECAY + e)
	               > patchedParams->getValue(params::LOCAL_ENV_0_RELEASE + e));
}

void Sound::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {
	endStutter(paramManager);

	int32_t modKnobMode = *getModKnobMode();

	ModKnob* ourModKnobTop = &modKnobs[modKnobMode][1];
	ModKnob* ourModKnobBottom = &modKnobs[modKnobMode][0];

	// mod button popup logic
	// if top knob == LPF Freq && bottom knob == LPF Reso
	// if top knob == HPF Freq && bottom knob == HPF Reso
	// if top knob == Treble && bottom knob == Bass
	// --> displayFilterSettings(on, currentFilterType);

	// if top knob == Delay Rate && bottom knob == Delay Amount
	// --> displayDelaySettings(on);

	// if top knob == Sidechain && bottom knob == Reverb Amount
	// --> displaySidechainAndReverbSettings(on);

	// else --> display param name

	if (ourModKnobTop->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_LPF_FREQ)
	    && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_LPF_RESONANCE)) {
		displayFilterSettings(on, FilterType::LPF);
	}
	else if (ourModKnobTop->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_HPF_FREQ)
	         && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_HPF_RESONANCE)) {
		displayFilterSettings(on, FilterType::HPF);
	}
	else if (ourModKnobTop->paramDescriptor.isSetToParamWithNoSource(params::UNPATCHED_START + params::UNPATCHED_TREBLE)
	         && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::UNPATCHED_START
	                                                                       + params::UNPATCHED_BASS)) {
		displayFilterSettings(on, FilterType::EQ);
	}
	else if (ourModKnobTop->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_DELAY_RATE)
	         && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_DELAY_FEEDBACK)) {
		displayDelaySettings(on);
	}
	else if ((ourModKnobTop->paramDescriptor.hasJustOneSource()
	          && ourModKnobTop->paramDescriptor.getTopLevelSource() == PatchSource::SIDECHAIN)
	         && ourModKnobBottom->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_REVERB_AMOUNT)) {
		displaySidechainAndReverbSettings(on);
	}
	else {
		displayOtherModKnobSettings(whichModButton, on);
	}
}

ModelStackWithAutoParam* Sound::getParamFromModEncoder(int32_t whichModEncoder,
                                                       ModelStackWithThreeMainThings* modelStack, bool allowCreation) {

	// If setting up a macro by holding its encoder down, the knobs will represent macro control-amounts rather than
	// actual "params", so there's no "param".
	if (isUIModeActive(UI_MODE_MACRO_SETTING_UP)) {
		return modelStack->addParam(nullptr, nullptr, 0, nullptr); // "none"
	}
	return getParamFromModEncoderDeeper(whichModEncoder, modelStack, allowCreation);
}

ModelStackWithAutoParam* Sound::getParamFromModEncoderDeeper(int32_t whichModEncoder,
                                                             ModelStackWithThreeMainThings* modelStack,
                                                             bool allowCreation) {

	int32_t paramId;
	ParamCollectionSummary* summary;

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	int32_t modKnobMode = *getModKnobMode();
	ModKnob* knob = &modKnobs[modKnobMode][whichModEncoder];

	if (knob->paramDescriptor.isJustAParam()) {
		int32_t p = knob->paramDescriptor.getJustTheParam();

		// Unpatched param
		if (p >= params::UNPATCHED_START) {
			paramId = p - params::UNPATCHED_START;
			summary = paramManager->getUnpatchedParamSetSummary();
		}

		// Patched param
		else {
			paramId = p;
			summary = paramManager->getPatchedParamSetSummary();
		}
	}

	// Patch cable
	else {
		paramId = knob->paramDescriptor.data;
		summary = paramManager->getPatchCableSetSummary();
	}

	ModelStackWithParamId* newModelStack1 =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);
	return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
}

bool Sound::modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) {
	// A knob it cycled is recorded by its caller, through the knobs' Edit.
	return modEncoderButtonActionImpl(whichModEncoder, on, modelStack);
}

bool Sound::modEncoderButtonActionImpl(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) {

	int32_t modKnobMode = *getModKnobMode();

	ModKnob* ourModKnob = &modKnobs[modKnobMode][whichModEncoder];

	if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::UNPATCHED_START
	                                                         + params::UNPATCHED_STUTTER_RATE)) {
		if (on) {
			beginStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		else {
			endStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		reassessRenderSkippingStatus(modelStack->addSoundFlags());

		return false;
	}

	// Switch delay pingpong
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_DELAY_RATE)) {
		if (on) {
			if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
			    == RuntimeFeatureStateToggle::On) {
				switchDelaySyncType();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayDelaySettings(on);
				}
				else {
					display->displayPopup(getDelaySyncTypeDisplayName());
				}
			}
			else {
				switchDelayPingPong();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayDelaySettings(on);
				}
				else {
					display->displayPopup(getDelayPingPongStatusDisplayName());
				}
			}
			return true;
		}
		else {
			return false;
		}
	}

	// Switch delay analog sim
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_DELAY_FEEDBACK)) {
		if (on) {
			if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
			    == RuntimeFeatureStateToggle::On) {
				switchDelaySyncLevel();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayDelaySettings(on);
				}
				else {
					char displayName[30];
					getDelaySyncLevelDisplayName(displayName);
					display->displayPopup(displayName);
				}
			}
			else {
				cycleDelayType();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayDelaySettings(on);
				}
				else {
					display->displayPopup(getDelayTypeDisplayName());
				}
			}
			return true;
		}
		else {
			return false;
		}
	}

	// Switch LPF mode
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_LPF_RESONANCE)) {
		if (on) {
			switchLPFMode();
			FilterType currentFilterType = FilterType::LPF;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterModeDisplayName(currentFilterType));
			}
			return true;
		}
		else {
			return false;
		}
	}
	// Switch HPF mode
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_HPF_RESONANCE)) {
		if (on) {
			switchHPFMode();
			FilterType currentFilterType = FilterType::HPF;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterModeDisplayName(currentFilterType));
			}
			return true;
		}
		else {
			return false;
		}
	}
	// Cycle through reverb presets
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::GLOBAL_REVERB_AMOUNT)) {
		if (on) {
			view.cycleThroughReverbPresets();

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displaySidechainAndReverbSettings(on);
			}
			else {
				display->displayPopup(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));
			}
		}
		return false;
	}

	// Switch sidechain sync level
	else if (ourModKnob->paramDescriptor.hasJustOneSource()
	         && ourModKnob->paramDescriptor.getTopLevelSource() == PatchSource::SIDECHAIN) {
		if (on) {
			int32_t insideWorldTickMagnitude;
			if (currentSong) { // Bit of a hack just referring to currentSong in here...
				insideWorldTickMagnitude =
				    (currentSong->project.transport.inside_world_tick_magnitude
				     + currentSong->project.transport.inside_world_tick_magnitude_offset_from_bpm);
			}
			else {
				insideWorldTickMagnitude = FlashStorage::defaultMagnitude;
			}

			deluge::edits::changeEffect(*this, "sidechain/syncLevel",
			                            deluge::serialization::Value::makeNumber(
			                                effects().sidechain.sync_level == (SyncLevel)(7 - insideWorldTickMagnitude)
			                                    ? 9 - insideWorldTickMagnitude
			                                    : 7 - insideWorldTickMagnitude));

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displaySidechainAndReverbSettings(on);
			}
			else {
				display->displayPopup(getSidechainDisplayName());
			}
			return true;
		}
		else {
			return false;
		}
	}

	// Switching between LPF, HPF and EQ
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_LPF_FREQ)) {
		if (on && getSynthMode() != SynthMode::FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(params::LOCAL_HPF_FREQ);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        params::LOCAL_LPF_RESONANCE)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(
				    params::LOCAL_HPF_RESONANCE);
			}
			FilterType currentFilterType = FilterType::HPF;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterTypeDisplayName(currentFilterType));
			}
		}
		return false;
	}

	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::LOCAL_HPF_FREQ)) {
		if (on && getSynthMode() != SynthMode::FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START + params::UNPATCHED_TREBLE);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        params::LOCAL_HPF_RESONANCE)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_START
				                                                                              + params::UNPATCHED_BASS);
			}
			FilterType currentFilterType = FilterType::EQ;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterTypeDisplayName(currentFilterType));
			}
		}
		return false;
	}

	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(params::UNPATCHED_START + params::UNPATCHED_TREBLE)) {
		if (on && getSynthMode() != SynthMode::FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        params::UNPATCHED_START + params::UNPATCHED_BASS)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(
				    params::LOCAL_LPF_RESONANCE);
			}
			FilterType currentFilterType = FilterType::LPF;

			// if mod button is pressed, update mod button pop up
			if (Buttons::isButtonPressed(
			        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
				displayFilterSettings(on, currentFilterType);
			}
			else {
				display->displayPopup(getFilterTypeDisplayName(currentFilterType));
			}
		}
		return false;
	}

	return false;
}

// modelStack may be NULL
void Sound::fastReleaseAllVoices(ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // the render walks and edits this Sound's voices
	for (auto it = voices_.begin(); it != voices_.end();) {
		const ActiveVoice& voice = *it;
		bool stillGoing = voice->do_fast_release(kSoftCullIncrement);

		if (!stillGoing) {
			this->checkVoiceExists(voice, "E212");
			this->freeActiveVoice(voice, modelStack, false); // Accepts NULL
			it = eraseVoiceAt(it);
		}
		else {
			it++;
		}
	}
}

void Sound::prepareForHibernation() {
	wontBeRenderedForAWhile();
	detachSourcesFromAudioFiles();
}

// This can get called either for hibernation, or because drum now has no active noteRow
void Sound::wontBeRenderedForAWhile() {
	ModControllableAudio::wontBeRenderedForAWhile();

	killAllVoices(); // Can't remember if this is always necessary, but it is when this is called from
	                 // Instrumentclip::detachFromInstrument()

	getArp()->reset(); // Surely this shouldn't be quite necessary?
	core().sidechain().stop();

	// Tell it to just cut the MODFX tail - we needa change status urgently!
	reassessRenderSkippingStatus(nullptr, true);

	// If it still thinks it's meant to be rendering, we did something wrong
	if (ALPHA_OR_BETA_VERSION && !skippingRendering) {
		FREEZE_WITH_ERROR("E322");
	}
}

void Sound::detachSourcesFromAudioFiles() {
	for (int32_t s = 0; s < kNumSources; s++) {
		sources[s].detachAllAudioFiles();
	}
}

void Sound::deleteMultiRange(int32_t s, int32_t r) {
	// Because range storage is about to change, must unassign all voices, and make sure no more can be assigned
	// during memory allocation
	// audioRoutineLocked gates only the cooperative audio routine, not the render, so excluding the render here needs
	// AudioExcludedSection.
	AudioExcludedSection section;
	killAllVoices();
	sources[s].ranges.erase(sources[s].ranges.begin() + r); // Destructs the range
	// The range collection just shrank: reflect it into the record.
	sources[s].seedDocRanges();
}

// This function has to give the same outcome as Source::renderInStereo()
bool Sound::renderingVoicesInStereo(ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // walks this Sound's voices, which the render edits (free on the audio tier)

	// audioDriver deciding we're rendering in mono overrides everything
	if (!AudioEngine::renderInStereo) {
		return false;
	}

	if (voices_.empty()) {
		return false;
	}

	// Stereo live-input
	if ((sources[0].oscType == OscType::INPUT_STEREO || sources[1].oscType == OscType::INPUT_STEREO)
	    && (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) {
		return true;
	}

	if (modelStack->paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_PAN)) {
		return true;
	}

	if (snapshot_.unison_stereo_spread && snapshot_.num_unison > 1) {
		return true;
	}

	uint32_t mustExamineSourceInEachVoice = 0;

	// Have a look at what samples, if any, are in each Source
	for (int32_t s = 0; s < kNumSources; s++) {
		Source* source = &sources[s];

		if (!modelStack->checkSourceEverActive(s)) {
			continue;
		}

		if (source->oscType == OscType::SAMPLE) { // Just SAMPLE, because WAVETABLEs can't be stereo.

			int32_t numRanges = std::ssize(source->ranges);

			// If multiple ranges, we have to come back and examine Voices to see which are in use
			if (numRanges > 1) {
				mustExamineSourceInEachVoice |= (1 << s);
			}

			// Or if just 1 range, we can examine it now
			else if (numRanges == 1) {
				MultiRange* range = source->ranges.getElement(0);
				AudioFileHolder* holder = range->getAudioFileHolder();

				if (holder->audioFile && holder->audioFile->numChannels == 2) {
					return true;
				}
			}
		}
	}

	// Ok, if that determined that either source has multiple samples (multisample ranges), we now have to
	// investigate each Voice
	if (mustExamineSourceInEachVoice) {
		for (const ActiveVoice& voice : voices_) {
			for (int32_t s = 0; s < kNumSources; s++) {
				if (mustExamineSourceInEachVoice & (1 << s)) {
					AudioFileHolder* holder = voice->guides[s].audioFileHolder;
					if (holder && holder->audioFile && holder->audioFile->numChannels == 2) {
						return true;
					}
				}
			}
		}
	}

	// No stereo stuff found - we're rendering in mono.
	return false;
}

ModelStackWithAutoParam* Sound::getParamFromMIDIKnob(MIDIKnob& knob, ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary;
	int32_t paramId;

	if (knob.paramDescriptor.isJustAParam()) {

		int32_t p = knob.paramDescriptor.getJustTheParam();

		// Unpatched parameter
		if (p >= params::UNPATCHED_START) {
			return ModControllableAudio::getParamFromMIDIKnob(knob, modelStack);
		}

		// Actual (patched) parameter
		else {
			summary = modelStack->paramManager->getPatchedParamSetSummary();
			paramId = p;
		}
	}

	// Patch cable strength
	else {
		summary = modelStack->paramManager->getPatchCableSetSummary();
		paramId = knob.paramDescriptor.data;
	}

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);

	ModelStackWithAutoParam* modelStackWithAutoParam = summary->paramCollection->getAutoParamFromId(
	    modelStackWithParamId, true); // Allow patch cable creation. TODO: think this through better...

	return modelStackWithAutoParam;
}

// **** This is the main function that enacts the unassigning of the Voice
// modelStack can be NULL if you really insist
void Sound::freeActiveVoice(const ActiveVoice& voice, ModelStackWithSoundFlags* modelStack, bool erase) {
	// Capture the identity BEFORE any mutation, and erase by that value rather than by `voice`.
	//
	// `voice` aliases an element of `voices_` at every call site: a range-for over `voices_`,
	// `stealOneActiveVoice()`, and `AudioEngine::terminateOneVoice` all hand back a reference into the
	// vector itself. `std::erase(c, value)` is specified as `erase_if(c, [&](auto& e){ return e == value; })`
	// — it compares each element against `value` while `std::remove` shifts elements within the
	// container. When `value` aliases one of those elements, its storage is overwritten part-way
	// through the algorithm and the remaining comparisons run against a moved-from value.
	//
	// `ActiveVoice` is a `unique_ptr`, so "overwritten" means MOVED: that path can recycle a
	// `VoiceSlot` twice or drop one, which corrupts the ObjectPool free-list and, through it, the heap
	// that `GeneralMemoryAllocator::checkEverythingOk` walks. Comparing against a plain `VoiceSlot*`
	// copied out first removes the aliasing entirely, so the predicate is stable no matter how
	// `remove` rearranges the container.
	//
	// Single-threaded UB, not a race — it needs only repeated culling, which is why it surfaces
	// under a cull storm rather than under concurrency. Distinct from the cross-tier race on
	// voices_ (see the containment note in Sound::Sound()), which remains open.
	VoiceSlot* const target = voice.get();
	voice->set_as_unassigned(modelStack);
	if (erase) {
		eraseVoicesIf([target](const ActiveVoice& v) { return v.get() == target; });
	}
}

void Sound::killAllVoices() {
	AudioExcludedSection section; // the whole unassign-and-clear: the render walks and edits these voices
	// Reset invertReversed flag so all voices get its reverse settings back to normal
	invertReversed = false;

	for (const ActiveVoice& voice : voices_) {
		voice->set_as_unassigned(nullptr);
	}
	clearVoices();
}

const Sound::ActiveVoice& Sound::getLowestPriorityVoice() const {
	// Compare by voice priority (VoiceSlot::operator<=> on priority_rating()), NOT by the unique_ptr's
	// pointer value. `voices_` holds ActiveVoice == std::unique_ptr<VoiceSlot>, whose default ordering is by
	// address — so a bare max_element(voices_) selects the highest-addressed voice, which is both
	// semantically wrong (culls an arbitrary voice, not the lowest-priority one) and heap-layout-dependent
	// (it made the offline golden render depend on allocation addresses — see docs/dev/overread_hunt.md).
	return *std::ranges::max_element(voices_, [](const auto& a, const auto& b) { return *a < *b; });
}

const Sound::ActiveVoice& Sound::stealOneActiveVoice() {
	if (voices_.empty()) {
		FREEZE_WITH_ERROR("ENOV");
	};
	const ActiveVoice& voice = this->getLowestPriorityVoice();

	/// Reconstruct the voice
	this->freeActiveVoice(voice, nullptr, false);
	voice->~VoiceSlot();
	new (voice.get()) VoiceSlot(*this);

	return voice;
}

/// Force a voice to release very quickly - will be almost instant but not click
void Sound::terminateOneActiveVoice() {
	AudioExcludedSection section; // walks this Sound's voices, which the render edits (free on the audio tier)
	if (voices_.empty()) {
		return;
	}

	// The eligibility filter must apply to EVERY voice, including the first: seeding `best` with front()
	// unfiltered meant a just-fast-released front voice (highest rating, since FAST_RELEASE outranks every
	// sounding stage) absorbed every subsequent terminate in the same render window - so chords overran
	// maxVoiceCount and the limiter sometimes chopped held notes instead (issue #4721).
	ActiveVoice* best = nullptr;
	for (ActiveVoice& voice : voices_) {
		if (voice->kernel.is_cull_fading()) {
			continue;
		}
		if (best == nullptr || (*best)->priority_rating() < voice->priority_rating()) {
			best = &voice;
		}
	}

	if (best == nullptr) {
		return;
	}

	const ActiveVoice& voice = *best;
	// SOFT_CULL_INCREMENT = a 128-sample (~2.9 ms) fade, matching release 1.2's voice-limit steal. Faster rates
	// (the 4x, 32-sample sub-millisecond one) read as a pop on sustained material when a low maxVoices makes every
	// note-on steal (issue #4721).
	bool still_rendering = voice->do_fast_release(kSoftCullIncrement);

	if (!still_rendering) {
		this->freeActiveVoice(voice);
	}
}

void Sound::forceReleaseOneActiveVoice() {
	AudioExcludedSection section; // walks this Sound's voices, which the render edits (free on the audio tier)
	if (voices_.empty()) {
		return;
	}

	// As in terminateOneActiveVoice: the filter must cover the first voice too, or an already-fast-releasing
	// front voice (highest rating) soaks up every call in the window.
	// Note isCullFading() only protects voices already fading at >= SOFT_CULL_INCREMENT; a voice fast-releasing
	// more slowly (increment in [4096, SOFT_CULL_INCREMENT)) is still eligible, so speedUpRelease() below doubles
	// it up to the cull-fade rate. This is intentional: a slow fade shouldn't be allowed to linger under load.
	ActiveVoice* best = nullptr;
	for (ActiveVoice& voice : voices_) {
		if (voice->kernel.is_cull_fading()) {
			continue;
		}
		if (best == nullptr || (*best)->priority_rating() < voice->priority_rating()) {
			best = &voice;
		}
	}

	if (best == nullptr) {
		return;
	}

	const ActiveVoice& voice = *best;

	bool still_rendering = voice->speed_up_release();

	if (!still_rendering) {
		this->freeActiveVoice(voice);
	}
}

void Sound::checkVoiceExists(const ActiveVoice& voice, const char* error) const {
	if (std::ranges::find(voices_, voice) == voices_.end()) {
		FREEZE_WITH_ERROR(error);
	}
}

#pragma GCC diagnostic pop

void Sound::noteVoicesEdit() {
	deluge::noteStructuralEdit(deluge::StructuralContainer::VOICES);
}

void Sound::publishVoice(ActiveVoice voice) {
	AudioExcludedSection section; // the render walks every Sound's voices
	noteVoicesEdit();
	voices_.push_back(std::move(voice));
}

Sound::ActiveVoiceIterator Sound::eraseVoiceAt(ActiveVoiceIterator it) {
	AudioExcludedSection section; // the render walks every Sound's voices
	noteVoicesEdit();
	return voices_.erase(it);
}

void Sound::clearVoices() {
	AudioExcludedSection section; // the render walks every Sound's voices
	noteVoicesEdit();
	voices_.clear();
}
