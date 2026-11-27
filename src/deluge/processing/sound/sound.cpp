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
#include "modulation/params/arp_param_set.h"

#include "bench/render_profile.h"
#include "definitions_cxx.hpp"
#include "dsp/core/fixed_point.h"
#include "dsp/core/stereo_sample.h"
#include "dsp/fixed/pitch/fine_tune.h"
#include "dsp/fixed/vca/pan.h"
#include "effects/codec.h"
#include "effects/filter_slots.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "instruments/classic/codec.h"
#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/pages.h"
#include "instruments/classic/sound_state.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/state_rules.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/clock.h"
#include "libdeluge/system.h"
#include "memory/fast_allocator.h"
#include "memory/general_memory_allocator.h"
#include "memory/stack_guard.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/fx/chain_codec.h"
#include "model/fx/chain_params.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/model_stack.h"
#include "model/project_bridge/arp_bridge.h"
#include "model/project_bridge/cable_bridge.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/lane_target.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "model/voice/voice_part.h"
#include "model/voice/voice_sample.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "plugin/types/param_mapping.h"
#include "processing/dx7/dx_engine.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "processing/sound/fx_section_inputs.h"
#include "processing/sound/param_dump.h"
#include "processing/sound/param_manager_view.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/param_automation.h"
#include "project/sound_routing_codec.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include "storage/wave_table/wave_table.h"
#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_fixed.h"
#include "synth/matrix/param_format.h"
#include "synth/modulation/envelope.h"
#include "synth/voice/unison.h"
#include "util/exceptions.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/misc.h"
#include "util/notify.h"
#include "util/structural_edit_probe.h"
#include "util/synth_noise.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <utility>

using namespace deluge;

using deluge::synth::BlockValues;
using deluge::synth::SourceSlot;

namespace params = deluge::modulation::params;

// Defined in processing/render_scratch.cpp: the render scratch shared engine-wide, handed to each voice's render.
extern deluge::dsp::Q<31> spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

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

/// @p sound's mod knobs, set to the knobs of @p pages (one page per mod button).
void takeKnobsOf(Sound& sound, std::span<const deluge::plugin::RemotePage> pages) {
	for (const deluge::plugin::RemotePage& page : pages) {
		for (uint8_t knob = 0; knob < kNumPhysicalModKnobs; knob++) {
			if (const auto& param = page.slots[knob].param) {
				sound.modKnobs[page.section][knob].target =
				    deluge::bridge::knobOfControl(*param, params::Kind::UNPATCHED_SOUND);
			}
		}
	}
}
} // namespace

Sound::Sound()
    : fxSection_{deluge::model::kModFxRingAllocator, deluge::model::kGranularAllocator, deluge::model::delayArena()} {

	// The render walks every Sound's voices from the audio tier, which preempts a note-on anywhere: a publish that
	// grew the list would move it under that walk. Reserving the engine pool's whole capacity here, before the audio
	// tier can reach this Sound, keeps the list's buffer where it is for the Sound's lifetime. The bound is the pool's
	// capacity rather than the voice limit, since cull-fading voices do not count toward the limit but are listed;
	// nothing ever shrinks the list's capacity.
	synth_.voices().reserve(AudioEngine::VoicePool::get().capacity());

	deluge::model::changeEffects(*effectsOwner(),
	                             [](deluge::effects::EffectStates& e) { e.mod_fx.type = ModFXType::NONE; });

	deluge::model::changeEffects(*effectsOwner(), [](deluge::effects::EffectStates& e) {
		e.filter.lpf_mode = FilterMode::TRANSISTOR_24DB;
	}); // Good for samples, I think

	// The default knobs: the instrument's published page for each mod button.
	takeKnobsOf(*this, deluge::instruments::classic::kDevicePages);
	expressionSourcesChangedAtSynthLevel.reset();

	paramLPF.p = PARAM_LPF_OFF;

	// A new sound plays a new synth's state, its effects following the song
	adoptState(deluge::instruments::classic::with_song_defaults(deluge::instruments::classic::SoundState{},
	                                                            deluge::model::songDefaults()));
	AudioEngine::sounds.add(this);
}

void Sound::initParams(ParamManager* paramManager) {

	deluge::model::initSharedParams(paramManager, params::Kind::UNPATCHED_SOUND);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = params::Kind::UNPATCHED_SOUND;
	setParamsToDefaults(*unpatchedParams, params::Kind::UNPATCHED_SOUND, {params::UNPATCHED_PORTAMENTO});

	// The params a new sound starts at; the rest keep their values
	setParamsToDefaults(*paramManager->getPatchedParamSet(), params::Kind::PATCHED,
	                    {params::LOCAL_VOLUME,
	                     params::LOCAL_OSC_A_VOLUME,
	                     params::LOCAL_OSC_B_VOLUME,
	                     params::GLOBAL_VOLUME_POST_FX,
	                     params::GLOBAL_VOLUME_POST_REVERB_SEND,
	                     params::LOCAL_FOLD,
	                     params::LOCAL_HPF_RESONANCE,
	                     params::LOCAL_HPF_FREQ,
	                     params::LOCAL_HPF_MORPH,
	                     params::LOCAL_LPF_MORPH,
	                     params::LOCAL_PITCH_ADJUST,
	                     params::GLOBAL_REVERB_AMOUNT,
	                     params::GLOBAL_DELAY_RATE,
	                     params::GLOBAL_DELAY_FEEDBACK,
	                     params::LOCAL_CARRIER_0_FEEDBACK,
	                     params::LOCAL_CARRIER_1_FEEDBACK,
	                     params::LOCAL_MODULATOR_0_FEEDBACK,
	                     params::LOCAL_MODULATOR_1_FEEDBACK,
	                     params::LOCAL_MODULATOR_0_VOLUME,
	                     params::LOCAL_MODULATOR_1_VOLUME,
	                     params::LOCAL_OSC_A_PHASE_WIDTH,
	                     params::LOCAL_OSC_B_PHASE_WIDTH,
	                     params::LOCAL_ENV_1_ATTACK,
	                     params::LOCAL_ENV_1_DECAY,
	                     params::LOCAL_ENV_1_SUSTAIN,
	                     params::LOCAL_ENV_1_RELEASE,
	                     params::LOCAL_LFO_LOCAL_FREQ_1,
	                     params::GLOBAL_LFO_FREQ_1,
	                     params::LOCAL_LFO_LOCAL_FREQ_2,
	                     params::GLOBAL_LFO_FREQ_2,
	                     params::LOCAL_PAN,
	                     params::LOCAL_NOISE_VOLUME,
	                     params::GLOBAL_MOD_FX_DEPTH,
	                     params::GLOBAL_MOD_FX_RATE,
	                     params::LOCAL_OSC_A_PITCH_ADJUST,
	                     params::LOCAL_OSC_B_PITCH_ADJUST,
	                     params::LOCAL_MODULATOR_0_PITCH_ADJUST,
	                     params::LOCAL_MODULATOR_1_PITCH_ADJUST});
}

void Sound::setupAsSample(ParamManagerForTimeline* paramManager) {
	const deluge::instruments::classic::SoundState before = *state_;

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setBase(*patchedParams->docForParam(params::LOCAL_OSC_B_VOLUME),
	                                                          -2147483648);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_ATTACK),
	                                                          deluge::plugin::preset_word(0));
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_DECAY),
	                                                         deluge::plugin::preset_word(20));
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_SUSTAIN),
	                                                           deluge::plugin::preset_word(50));
	patchedParams->params[params::LOCAL_ENV_0_RELEASE].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE),
	                                                           deluge::plugin::preset_word(0));

	patchedParams->params[params::LOCAL_LPF_RESONANCE].setBase(*patchedParams->docForParam(params::LOCAL_LPF_RESONANCE),
	                                                           -2147483648);
	patchedParams->params[params::LOCAL_LPF_FREQ].setBase(*patchedParams->docForParam(params::LOCAL_LPF_FREQ),
	                                                      2147483647);

	takeKnobsOf(*this, deluge::instruments::classic::kSamplePages);

	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();
	patchCableSet->truncateCables(0);
	patchCableSet->appendDefaultCable(PatchSource::VELOCITY, params::LOCAL_VOLUME,
	                                  cableDepthFromPreset(kSampleDrumVelocityCableUserValue));

	setupDefaultExpressionPatching(paramManager);

	adoptState(deluge::instruments::classic::as_sample_sound(before));
}

void Sound::setupAsDefaultSynth(ParamManager* paramManager) {
	const deluge::instruments::classic::SoundState before = *state_;

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setBase(*patchedParams->docForParam(params::LOCAL_OSC_B_VOLUME),
	                                                          0x47AE1457);
	patchedParams->params[params::LOCAL_LPF_RESONANCE].setBase(*patchedParams->docForParam(params::LOCAL_LPF_RESONANCE),
	                                                           0xA2000000);
	patchedParams->params[params::LOCAL_LPF_FREQ].setBase(*patchedParams->docForParam(params::LOCAL_LPF_FREQ),
	                                                      0x10000000);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_ATTACK),
	                                                          0x80000000);
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_DECAY),
	                                                         0xE6666654);
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_SUSTAIN),
	                                                           0x7FFFFFFF);
	patchedParams->params[params::LOCAL_ENV_0_RELEASE].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE),
	                                                           0x851EB851);
	patchedParams->params[params::LOCAL_ENV_1_ATTACK].setBase(*patchedParams->docForParam(params::LOCAL_ENV_1_ATTACK),
	                                                          0xA3D70A37);
	patchedParams->params[params::LOCAL_ENV_1_DECAY].setBase(*patchedParams->docForParam(params::LOCAL_ENV_1_DECAY),
	                                                         0xA3D70A37);
	patchedParams->params[params::LOCAL_ENV_1_SUSTAIN].setBase(*patchedParams->docForParam(params::LOCAL_ENV_1_SUSTAIN),
	                                                           0xFFFFFFE9);
	patchedParams->params[params::LOCAL_ENV_1_RELEASE].setBase(*patchedParams->docForParam(params::LOCAL_ENV_1_RELEASE),
	                                                           0xE6666654);
	patchedParams->params[params::GLOBAL_VOLUME_POST_FX].setBase(
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

	adoptState(deluge::instruments::classic::as_default_synth(before));
}

void Sound::setupDefaultExpressionPatching(ParamManager* paramManager) {
	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();

	if (patchCableSet->appendDefaultCable(PatchSource::AFTERTOUCH, params::LOCAL_VOLUME, cableDepthFromPreset(33))
	    == nullptr) {
		return;
	}
	if (getSynthMode() == SynthMode::FM) {
		patchCableSet->appendDefaultCable(PatchSource::Y, params::LOCAL_MODULATOR_0_VOLUME, cableDepthFromPreset(15));
	}
	else {
		patchCableSet->appendDefaultCable(PatchSource::Y, params::LOCAL_LPF_FREQ, cableDepthFromPreset(20));
	}
}

void Sound::setupAsBlankSynth(ParamManager* paramManager, bool is_dx) {
	const deluge::instruments::classic::SoundState before = *state_;

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[params::LOCAL_OSC_B_VOLUME].setBase(*patchedParams->docForParam(params::LOCAL_OSC_B_VOLUME),
	                                                          -2147483648);
	patchedParams->params[params::LOCAL_LPF_FREQ].setBase(*patchedParams->docForParam(params::LOCAL_LPF_FREQ),
	                                                      2147483647);
	patchedParams->params[params::LOCAL_LPF_RESONANCE].setBase(*patchedParams->docForParam(params::LOCAL_LPF_RESONANCE),
	                                                           -2147483648);
	patchedParams->params[params::LOCAL_ENV_0_ATTACK].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_ATTACK),
	                                                          -2147483648);
	patchedParams->params[params::LOCAL_ENV_0_DECAY].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_DECAY),
	                                                         deluge::plugin::preset_word(20));
	patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setBase(*patchedParams->docForParam(params::LOCAL_ENV_0_SUSTAIN),
	                                                           2147483647);
	if (is_dx) {
		sourceResources[0].ensureDxPatch(); // initializes DX engine if this is the first dx7patch
		// velocity is forwarded to dx7 engine, don't do master volume
		paramManager->getPatchCableSet()->truncateCables(0);
		patchedParams->params[params::LOCAL_ENV_0_RELEASE].setBase(
		    *patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE), 2147483647); // 30 ish
	}
	else {
		patchedParams->params[params::LOCAL_ENV_0_RELEASE].setBase(
		    *patchedParams->docForParam(params::LOCAL_ENV_0_RELEASE), -2147483648);

		PatchCableSet* patchCableSet = paramManager->getPatchCableSet();
		patchCableSet->truncateCables(0);
		patchCableSet->appendDefaultCable(PatchSource::VELOCITY, params::LOCAL_VOLUME, cableDepthFromPreset(50));
	}

	setupDefaultExpressionPatching(paramManager);

	adoptState(deluge::instruments::classic::as_blank_synth(before, is_dx));
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
	PatchedParamSet& patched = *paramManager.getPatchedParamSet();
	// The arp rate's base is the arp's own param; the patched slot of its destination holds nothing
	const auto preset = [&](int32_t p) {
		if (p != params::GLOBAL_ARP_RATE) {
			return patched.getKnob(p);
		}
		// A manager with no arp plays the arp's starting rate, zero
		static_assert(deluge::arp::default_value(deluge::arp::Owner::SOUND, deluge::arp::param::kRate) == 0);
		ArpParamSet* arp = paramManager.getArpParamSet();
		return arp != nullptr ? arp->getKnob(deluge::arp::param::kRate.value) : deluge::dsp::Q<31>{};
	};
	if (gather == PresetGather::ALL) {
		for (int32_t p = 0; p < deluge::modulation::params::kNumParams; p++) {
			block.preset_values[p] = preset(p);
		}
	}
	else if (block.program != nullptr) {
		// The routed params from the program the pass reads next, so a pass touches one cold structure
		for (const deluge::synth::matrix::ScopeView& scope : block.program->scopes) {
			for (int32_t k = scope.num_depth_destinations; k < scope.num_destinations; k++) {
				const int32_t p = scope.destinations[k].param;
				block.preset_values[p] = preset(p);
			}
		}
	}
	if (paramLPF.p != PARAM_LPF_OFF) {
		// The smoothed value is the knob's stored word
		block.preset_values[paramLPF.p] = deluge::dsp::Q<31>::from_raw(paramLPF.currentValue);
	}
	for (int32_t c = 0; c < cables.numUsablePatchCables; c++) {
		block.depths[c] = cables.patchCables[c].depth();
	}
	return block;
}

void Sound::performInitialPatching(const deluge::synth::matrix::MatrixBlock& block) {
	deluge::synth::matrix::Evaluator<deluge::synth::matrix::MatrixFixed>::run_initial(matrixPass(), block);
	deluge::param_dump::plane(GLOBALITY_GLOBAL, this, nullptr, params.param_final_values);
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
	for (SoundVoice* voice : synth_.voices().list()) {
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
/// The sound element's tags the Deluge instrument and the sound's routing decode entirely (readFromFile), which this
/// reader skips: a sound's params are all in its defaultParams.
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
							new_knob.target = deluge::modulation::knobParam(p);
						}
						else if (s2 == PatchSource::NOT_AVAILABLE) {
							new_knob.target =
							    deluge::modulation::knobTargetOf(deluge::synth::CableTarget::depth_of(p, s));
						}
						else {
							new_knob.target =
							    deluge::modulation::knobTargetOf(deluge::synth::CableTarget::depth_of_depth(p, s, s2));
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
		Error result = deluge::model::readEffectTag(reader, tagName, *effectsOwner(), nullptr, readAutomationUpToPos,
		                                            arpSettings, &arp_read_, song);
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
void Sound::ensureKnobReferencesCorrectVolume(ModKnob& knob) {
	int32_t p = deluge::modulation::knobWord(knob.target).param_index();

	if (p == params::GLOBAL_VOLUME_POST_REVERB_SEND || p == params::GLOBAL_VOLUME_POST_FX
	    || p == params::LOCAL_VOLUME) {
		if (deluge::modulation::knobWord(knob.target).is_param()) {
			knob.target = deluge::modulation::knobParam(params::GLOBAL_VOLUME_POST_FX);
		}
		else if (deluge::modulation::knobWord(knob.target).top_source() == PatchSource::SIDECHAIN) {
			knob.target = deluge::modulation::knobTargetOf(
			    deluge::modulation::knobWord(knob.target).with_param(params::GLOBAL_VOLUME_POST_REVERB_SEND));
		}
		else {
			knob.target = deluge::modulation::knobTargetOf(
			    deluge::modulation::knobWord(knob.target).with_param(params::LOCAL_VOLUME));
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
		// if (getSynthMode() == SynthMode::FM || (oscType(0) != OscType::SQUARE &&
		// oscType(0) != OscType::JUNO60_SUBOSC)) return PatchCableAcceptance::DISALLOWED;
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
		// if (getSynthMode() == SynthMode::FM || (oscType(1) != OscType::SQUARE &&
		// oscType(1) != OscType::JUNO60_SUBOSC)) return PatchCableAcceptance::DISALLOWED;
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
		if (renderEffects().filter.lpf_mode == FilterMode::OFF) {
			return PatchCableAcceptance::DISALLOWED;
		}
		break;

	case params::LOCAL_HPF_FREQ:
	case params::LOCAL_HPF_MORPH:
	case params::LOCAL_HPF_RESONANCE:
		if (renderEffects().filter.hpf_mode == FilterMode::OFF) {
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
		return (synth_.config().lfos[LFO1_ID].sync_level == SYNC_LEVEL_NONE) ? PatchCableAcceptance::ALLOWED
		                                                                     : PatchCableAcceptance::DISALLOWED;

	case params::GLOBAL_LFO_FREQ_2:
		return (synth_.config().lfos[LFO3_ID].sync_level == SYNC_LEVEL_NONE) ? PatchCableAcceptance::ALLOWED
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
	          paramManager->getPatchedParamSet()->playedLane(params::LOCAL_NOISE_VOLUME), -2147483648)))) [[unlikely]] {
		return;
	}

	ArpeggiatorSettings* arpSettings = getArpSettings();
	if (arpSettings != nullptr) {
		arpSettings->values = paramManager->getArpParamSet()->values();
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
	if (instruction.arpNoteOn != nullptr) {
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
				break;
			}
			atLeastOneNoteOn = true;

			// The note starts now: the voice budget, not a cap on starts per render, bounds what a burst costs.
			invertReversed = instruction.invertReversed;
			noteOnPostArpeggiator(modelStackWithSoundFlags, noteCodePreArp, instruction.arpNoteOn->noteCodeOnPostArp[n],
			                      instruction.arpNoteOn->velocity, mpeValues, instruction.sampleSyncLengthOn, ticksLate,
			                      samplesLate, fromMIDIChannel);
			instruction.arpNoteOn->noteStatus[n] = ArpNoteStatus::PLAYING;
		}
	}
	if (!atLeastOneNoteOn) {
		// in the case of the arpeggiator not returning a note On (could happen if Note Probability evaluates to "don't
		// play") we must at least evaluate the render-skipping if the arpeggiator is ON
		if (arpSettings != nullptr && arpeggiator->hasAnyInputNotesActive() && arpSettings->mode != ArpMode::OFF) {
			reassessRenderSkippingStatus(modelStackWithSoundFlags);
		}
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

static_assert(std::tuple_size_v<decltype(deluge::synth::NoteFacts::source_active_now)> == kNumSources,
              "a note's facts name each source");

deluge::synth::NoteFacts Sound::noteFacts(ModelStackWithSoundFlags* modelStack) {
	auto* paramManager = static_cast<ParamManagerForTimeline*>(modelStack->paramManager);
	const ArpeggiatorSettings* arpSettings = getArpSettings();
	deluge::synth::NoteFacts facts{
	    .note_tails_allowed = allowNoteTails(modelStack, true),
	    .fm = getSynthMode() == SynthMode::FM,
	    .note_tails_counting_loops = allowNoteTails(modelStack),
	    .arpeggiating = arpSettings != nullptr && arpSettings->mode != ArpMode::OFF,
	};
	facts.falls_back = (synth_.config().voicing.polyphony == PolyphonyMode::LEGATO
	                    || synth_.config().voicing.polyphony == PolyphonyMode::MONO)
	                   && !isDrum() && facts.note_tails_counting_loops;
	for (int32_t s = 0; s < kNumSources; s++) {
		facts.source_active_now[s] = isSourceActiveCurrently(s, paramManager);
	}
	return facts;
}

void Sound::noteOnPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t noteCodePreArp, int32_t noteCodePostArp,
                                  int32_t velocity, int16_t const* mpeValues, uint32_t sampleSyncLength,
                                  int32_t ticksLate, uint32_t samplesLate, int32_t fromMIDIChannel) {
	// Not polyphonic: finding a voice to reuse or glide, and re-triggering it, all act on voices the render is playing,
	// so that whole path runs with the render excluded (free on the audio tier), through to the note's MIDI out. A
	// polyphonic note-on builds its new voice with the render running and only publishes it excluded.
	std::optional<AudioExcludedSection> nonPolyphonic;
	if (synth_.config().voicing.polyphony != PolyphonyMode::POLY) {
		nonPolyphonic.emplace();
	}

	// The facts are read only by a non-polyphonic sound's walk over its voices
	const deluge::synth::NoteFacts facts =
	    synth_.config().voicing.polyphony != PolyphonyMode::POLY && !synth_.voices().empty()
	        ? noteFacts(modelStack)
	        : deluge::synth::NoteFacts{};
	if (!synth_.voices().note_on(
	        synth_.config(), facts,
	        noteOnEvent(noteCodePreArp, noteCodePostArp, velocity, samplesLate, true, fromMIDIChannel, mpeValues),
	        sampleSyncLength, ticksLate, modelStack)) {
		return; // no memory, and no voice of this sound's to take: the note cannot play
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

void Sound::sendPolyphonicExpressionToMidiOut(int32_t newValue, int32_t expressionDimension,
                                              int32_t channelOrNoteNumber, MIDICharacteristic whichCharacteristic) {
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

	if (synth_.voices().empty()) {
		return;
	}

	const deluge::synth::NoteFacts facts = noteFacts(modelStack);
	using HeldNote = deluge::instruments::classic::VoicePool<SoundVoiceHost>::HeldNote;
	std::optional<HeldNote> held;
	Arpeggiator* arpeggiator = nullptr;
	if (!facts.arpeggiating && facts.falls_back) {
		// A LEGATO or true-MONO sound switches back to the newest note still held. With no note tails (one-shot
		// samples), the arpeggiator holds notes which may not sound any more, kept for MPE, so it falls back only with
		// them.
		arpeggiator = &static_cast<SoundInstrument*>(this)->arpeggiator;
		if (arpeggiator->hasAnyInputNotesActive()) {
			ArpNote& arpNote = arpeggiator->notes.back();
			held = HeldNote{
			    .note = arpNote.inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)],
			    .midi_channel = arpNote.inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)],
			    .mpe = mpeSpan(arpNote.mpeValues),
			};
		}
	}
	const std::optional<HeldNote> restart =
	    synth_.voices().note_off(synth_.config(), facts, noteCode, noteCode == ALL_NOTES_OFF, held, modelStack);
	if (restart.has_value()) {
		// MONO: the held note sounds as a new note, at the velocity of the note just switched off and with the MPE
		// values of the held note's own keypress
		noteOnPostArpeggiator(modelStack, restart->note, restart->note, arpeggiator->lastVelocity, restart->mpe.data(),
		                      0, 0, 0, restart->midi_channel);
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
	        modelStack->paramManager->getPatchedParamSet()->playedLane(params::LOCAL_NOISE_VOLUME), -2147483648)) {
		return true;
	}

	// If we still don't know, just check there's at least one active oscillator that isn't a one-shot sample without a
	// loop-end point
	bool anyActiveSources = false;
	for (int32_t s = 0; s < kNumSources; s++) {
		bool sourceEverActive = modelStack->checkSourceEverActiveDisregardingMissingSample(s);

		anyActiveSources = sourceEverActive || anyActiveSources;

		if (sourceEverActive
		    && (oscType(s) != OscType::SAMPLE || synth_.config().sources[s].repeat_mode != SampleRepeatMode::ONCE
		        || (!disregardSampleLoop && sourceResources[s].hasAnyLoopEndPoint()))) {
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

		if (sourceEverActive && oscType(s) == OscType::SAMPLE
		    && synth_.config().sources[s].repeat_mode == SampleRepeatMode::STRETCH) {
			if (getSampleLength) {
				return sourceResources[s].getLengthInSamplesAtSystemSampleRate(note + synth_.config().voicing.transpose,
				                                                               true);
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

		if (oscType(s) != OscType::SAMPLE) {
			return 0;
		}
		else if (synth_.config().sources[s].repeat_mode == SampleRepeatMode::CUT
		         || synth_.config().sources[s].repeat_mode == SampleRepeatMode::LOOP) {

			if (anyLooping && synth_.config().sources[s].repeat_mode == SampleRepeatMode::LOOP) {
				*anyLooping = true;
			}
			int32_t length = sourceResources[s].getLengthInSamplesAtSystemSampleRate(note);

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

		if (oscType(s) != OscType::SAMPLE || !sourceResources[s].hasAtLeastOneAudioFileLoaded()
		    || synth_.config().sources[s].repeat_mode != SampleRepeatMode::CUT) {
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

		switch (oscType(s)) {

		// Sample - generally ok, but not if one-shot
		case OscType::SAMPLE:
			if (synth_.config().sources[s].repeat_mode == SampleRepeatMode::ONCE
			    || !sourceResources[s].hasAtLeastOneAudioFileLoaded()) {
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
	       && (getSynthMode() == SynthMode::FM || oscType(s) != OscType::SAMPLE
	           || sourceResources[s].hasAtLeastOneAudioFileLoaded());
}

bool Sound::isSourceActiveEverDisregardingMissingSample(int32_t s, ParamManager* paramManager) {
	return (getSynthMode() == SynthMode::RINGMOD
	        || paramManager->getPatchedParamSet()->params[params::LOCAL_OSC_A_VOLUME + s].containsSomething(
	            paramManager->getPatchedParamSet()->playedLane(params::LOCAL_OSC_A_VOLUME + s), -2147483648)
	        || renderingOscillatorSyncEver(paramManager));
}

bool Sound::isSourceActiveEver(int32_t s, ParamManager* paramManager) {
	return isSourceActiveEverDisregardingMissingSample(s, paramManager)
	       && (getSynthMode() == SynthMode::FM || oscType(s) != OscType::SAMPLE
	           || sourceResources[s].hasAtLeastOneAudioFileLoaded());
}

bool Sound::isNoiseActiveEver(ParamManagerForTimeline* paramManager) {
	return (getSynthMode() != SynthMode::FM
	        && paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(
	            paramManager->getPatchedParamSet()->playedLane(params::LOCAL_NOISE_VOLUME), -2147483648));
}

bool Sound::renderingOscillatorSyncCurrently(ParamManagerForTimeline* paramManager) {
	if (!synth_.config().engine.osc_sync) {
		return false;
	}
	if (getSynthMode() == SynthMode::FM) {
		return false;
	}
	return (getSmoothedPatchedParamValue(params::LOCAL_OSC_B_VOLUME, *paramManager) != -2147483648
	        || getSynthMode() == SynthMode::RINGMOD);
}

bool Sound::renderingOscillatorSyncEver(ParamManager* paramManager) {
	if (!synth_.config().engine.osc_sync) {
		return false;
	}
	if (getSynthMode() == SynthMode::FM) {
		return false;
	}
	return (paramManager->getPatchedParamSet()->params[params::LOCAL_OSC_B_VOLUME].containsSomething(
	            paramManager->getPatchedParamSet()->playedLane(params::LOCAL_OSC_B_VOLUME), -2147483648)
	        || getSynthMode() == SynthMode::RINGMOD);
}

void Sound::sampleZoneChanged(MarkerType markerType, int32_t s, ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // the render walks and edits this Sound's voices
	if (synth_.voices().empty()) {
		return;
	}

	if (playsReversed(s)) {
		markerType = static_cast<MarkerType>(kNumMarkerTypes - 1 - util::to_underlying(markerType));
	}

	for (auto it = synth_.voices().list().begin(); it != synth_.voices().list().end();) {
		SoundVoice* voice = *it;
		bool still_going = voiceSampleZoneChanged(*voice, *modelStack, s, markerType);
		if (still_going) {
			it++;
			continue;
		}
		it = synth_.voices().erase(it, modelStack);
	}
}

// Unlike most functions, this one accepts modelStack as NULL, because when unassigning all voices e.g. on song swap, we
// won't have it.
void Sound::reassessRenderSkippingStatus(ModelStackWithSoundFlags* modelStack, bool shouldJustCutModFX) {

	// TODO: should get the caller to provide this, cos they usually already have it. In fact, should put this on the
	// ModelStack, cos many deeper-nested functions called by this one need it too!
	ArpeggiatorSettings* arpSettings = getArpSettings();

	// The arpeggiator's pending notes are started only from render(), so keep rendering while one is pending:
	// otherwise it would stay pending forever (issue #2262)
	bool skippingStatusNow =
	    (synth_.voices().empty() && (core().delay().repeatsUntilAbandon() == 0u)
	     && (currentSong == nullptr || !currentSong->live.stutter.isStuttering(core()))
	     && ((arpSettings == nullptr) || !getArp()->hasAnyInputNotesActive() || arpSettings->mode == ArpMode::OFF)
	     && !getArp()->hasPendingNotes(arpSettings));

	if (skippingStatusNow != skippingRendering) {

		if (skippingStatusNow) {

			// We wanna start, skipping, but if MOD fx are on...
			if ((renderEffects().mod_fx.type != ModFXType::NONE)
			    || core().compressor().dsp().threshold() > deluge::dsp::Q<31>{}) {

				// If we didn't start the wait-time yet, start it now
				if (!startSkippingRenderingAtTime) {

					// But wait, first, maybe we actually have just been instructed to cut the MODFX tail
					if (shouldJustCutModFX) {
doCutModFXTail:
						deluge::model::clearModFXMemory(*effectsOwner());
						goto yupStartSkipping;
					}

					int32_t waitSamplesModfx = 0;
					switch (renderEffects().mod_fx.type) {
					case ModFXType::NONE: // the compressor's release alone
						break;
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

deluge::model::ReverbCandidate Sound::reverbCandidate(deluge::model::ReverbCandidate best,
                                                      ParamManagerForTimeline& paramManager) {
	PatchedParamSet* patchedParams = paramManager.getPatchedParamSet();
	if (patchedParams->params[params::GLOBAL_REVERB_AMOUNT].isAutomated(
	        patchedParams->playedLane(params::GLOBAL_REVERB_AMOUNT))
	    || !patchedParams->params[params::GLOBAL_REVERB_AMOUNT].containsSomething(
	        patchedParams->playedLane(params::GLOBAL_REVERB_AMOUNT), -2147483648)) {
		return best;
	}
	// We deliberately don't use the LPF'ed param here
	const int32_t reverbHere = patchedParams->getValue(params::GLOBAL_REVERB_AMOUNT);
	if (!deluge::model::beats(reverbHere, best)) {
		return best;
	}

	// The duck's depth is the sidechain's cable to the post-reverb volume, if there is one.
	PatchCableSet* patchCableSet = paramManager.getPatchCableSet();
	const int32_t whichCable = patchCableSet->getPatchCableIndex(
	    PatchSource::SIDECHAIN, deluge::synth::CableTarget::param(params::GLOBAL_VOLUME_POST_REVERB_SEND));
	// The duck reads the cable's Q30 depth word as its Q29 depth
	const auto depth = (whichCable != 255)
	                       ? deluge::dsp::reinterpret_bits<deluge::dsp::Q<29>>(deluge::synth::modified_depth(
	                             patchCableSet->patchCables[whichCable].depth(), PatchSource::SIDECHAIN,
	                             params::GLOBAL_VOLUME_POST_REVERB_SEND))
	                       : deluge::dsp::Q<29>{};
	return {
	    .amount = reverbHere,
	    .found = true,
	    .duck_depth = depth,
	    .duck_shape = paramManager.getUnpatchedParamSet()->getKnob(params::UNPATCHED_SIDECHAIN_SHAPE),
	    .rates = renderEffects().sidechain,
	};
}

// fromAutomation means whether the changes was caused by automation playing back - as opposed to the user turning the
// knob right now
void Sound::notifyValueChangeViaLPF(int32_t p, bool shouldDoParamLPF, const deluge::bridge::LaneTarget& lane,
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
				alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
				stopParamLPF(deluge::bridge::owner_stack_of(lane, modelStackMemory)->addSoundFlags());
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
		alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStackCopy = deluge::bridge::owner_stack_of(lane, modelStackMemory);

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

void Sound::render(ModelStackWithThreeMainThings* modelStack, std::span<StereoSample> output,
                   deluge::dsp::Q<31>* reverbBuffer, int32_t sideChainHitPending, int32_t reverbAmountAdjust,
                   bool shouldLimitDelayFeedback, deluge::dsp::Q<24> pitchAdjust, SampleRecorder* recorder) {

	if (skippingRendering) {
		core().compressor().dsp().clear_gain_reduction();
		return;
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	const ParamManagerView paramView{*paramManager};

	// Do global LFO
	if (paramView.wired(PatchSource::LFO_GLOBAL_1)) {
		const auto patchSourceLFOGlobalUnderlying = util::to_underlying(PatchSource::LFO_GLOBAL_1);

		int32_t old = params.source_values[patchSourceLFOGlobalUnderlying];
		// TODO: We don't really need to recompute phase increment unless rate, sync, or
		// playbackHandler.getTimePerInternalTickInverse() has changed. Rate and sync changes
		// already cause a resync. Maybe tempo changes do too? If so, this could be part of
		// the resync logic. Note: same issue exists with LFO2 now that it supports sync.
		params.source_values[patchSourceLFOGlobalUnderlying] =
		    globalLFO1
		        .render(output.size(), synth_.config().lfos[LFO1_ID],
		                getGlobalLFOPhaseIncrement<params::GLOBAL_LFO_FREQ_1>(LFO1_ID), deluge::synth_noise)
		        .raw(); // the source values are Q31 words
		uint32_t anyChange = (old != params.source_values[patchSourceLFOGlobalUnderlying]);
		sourcesChanged.fetch_or(anyChange << patchSourceLFOGlobalUnderlying, std::memory_order_acq_rel);
	}
	if (paramView.wired(PatchSource::LFO_GLOBAL_2)) {
		const auto patchSourceLFOGlobalUnderlying = util::to_underlying(PatchSource::LFO_GLOBAL_2);

		int32_t old = params.source_values[patchSourceLFOGlobalUnderlying];
		// TODO: We don't really need to recompute phase increment unless rate, sync, or
		// playbackHandler.getTimePerInternalTickInverse() has changed. Rate and sync changes
		// already cause a resync. Maybe tempo changes do too? If so, this could be part of
		// the resync logic. Note: same issue exists with LFO2 now that it supports sync.
		params.source_values[patchSourceLFOGlobalUnderlying] =
		    globalLFO3
		        .render(output.size(), synth_.config().lfos[LFO3_ID],
		                getGlobalLFOPhaseIncrement<params::GLOBAL_LFO_FREQ_2>(LFO3_ID), deluge::synth_noise)
		        .raw(); // the source values are Q31 words
		uint32_t anyChange = (old != params.source_values[patchSourceLFOGlobalUnderlying]);
		sourcesChanged.fetch_or(anyChange << patchSourceLFOGlobalUnderlying, std::memory_order_acq_rel);
	}

	for (int s = 0; s < kNumSources; s++) {
		if (oscType(s) == OscType::DX7 and sourceResources[s].dxPatch) {
			sourceResources[s].dxPatch->computeLfo(output.size());
		}
	}

	// Do sidechain
	if (paramView.wired(PatchSource::SIDECHAIN)) {
		if (sideChainHitPending) {
			core().sidechain().register_hit(sideChainHitPending);
		}

		const auto patchSourceSidechainUnderlying = util::to_underlying(PatchSource::SIDECHAIN);

		int32_t old = params.source_values[patchSourceSidechainUnderlying];
		params.source_values[patchSourceSidechainUnderlying] =
		    core().sidechain().render(output.size(), paramView.knob(params::UNPATCHED_SIDECHAIN_SHAPE),
		                              renderEffects().sidechain, playbackHandler.getTimePerInternalTickInverse());
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
		deluge::param_dump::plane(GLOBALITY_GLOBAL, this, nullptr, params.param_final_values);
#if defined(DELUGE_STREAM_BENCH)
		deluge::bench::render_profile::g_totals.sound_patch += deluge::bench::render_profile::cycles() - t0;
		++deluge::bench::render_profile::g_totals.sound_blocks;
#endif
	}

	// Setup some reverb-related stuff
	// The Q27 adjust (2^27 is 1; the song's default volume gives a half) times the Q31 reverb amount, rounded to Q26,
	// moved to the Q31 send
	const dsp::Q<31> reverbSendAmount = mul_r_as_wrap<dsp::Q<31>>(
	    dsp::Q<27>::from_raw(reverbAmountAdjust),
	    deluge::synth::matrix::global_param_value<params::GLOBAL_REVERB_AMOUNT>(params.param_final_values));

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	// Arpeggiator

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	ArpeggiatorSettings* arpSettings = getArpSettings();
	ArpReturnInstruction instruction;

	if (arpSettings != nullptr && arpSettings->mode != ArpMode::OFF) {
		// Only the arp reads these here. With it off, noteOn() refreshes them before it uses them, so don't touch
		// them (cold memory) every render
		arpSettings->values = paramManager->getArpParamSet()->values();
		uint32_t gateThreshold = arpSettings->values.unsigned_word(deluge::arp::param::kGate);
		uint32_t phaseIncrement = arpSettings->getPhaseIncrement(
		    deluge::synth::matrix::global_param_value<params::GLOBAL_ARP_RATE>(params.param_final_values));

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
		    std::min(delayWorkingState.delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate = params.param_final_values[params::GLOBAL_DELAY_RATE - params::FIRST_GLOBAL];
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse(true);
	core().delay().setupWorkingState(delayWorkingState, renderEffects().delay, timePerTickInverse,
	                                 !synth_.voices().empty());
	delayWorkingState.analog_saturation = 8;

	// Render each voice into a local buffer here
	bool voice_rendered_in_stereo = renderingVoicesInStereo(modelStackWithSoundFlags);

	// FIXME(@stellar-aria): if we have simulataneous sounds rendering, they'll overwrite
	// each other in this buffer. It probably should be object or thread-local
	alignas(CACHE_LINE_SIZE) static deluge::dsp::Q<31> sound_memory[SSI_TX_BUFFER_NUM_SAMPLES * 2];
	std::fill_n(sound_memory, output.size() * (voice_rendered_in_stereo ? 2 : 1), deluge::dsp::Q<31>{});

	std::span sound_mono{sound_memory, output.size()};
	std::span sound_stereo{(StereoSample*)sound_memory, output.size()};

	if (!synth_.voices().empty()) {

		// Very often, we'll just apply panning here at the Sound level rather than the Voice level
		bool applyingPanAtVoiceLevel = (AudioEngine::renderInStereo && paramView.modulated(params::LOCAL_PAN));

		// The block's facts, the engine's and the sound's own, which the synth makes the block's values of
		const deluge::synth::HostBlock hostBlock{
		    .cpu_direness = AudioEngine::cpuDireness,
		    .engine_in_stereo = AudioEngine::renderInStereo,
		    .pitch_adjust = pitchAdjust,
		};
		deluge::instruments::classic::SoundBlockFacts facts{
		    .has_filters = hasFilters(),
		    .lpf_mode = renderEffects().filter.lpf_mode,
		    .hpf_mode = renderEffects().filter.hpf_mode,
		    .filter_route = renderEffects().filter.route,
		    .lpf_morph = getSmoothedPatchedParamValue(params::LOCAL_LPF_MORPH, *paramManager),
		    .lpf_freq = getSmoothedPatchedParamValue(params::LOCAL_LPF_FREQ, *paramManager),
		    .hpf_morph = getSmoothedPatchedParamValue(params::LOCAL_HPF_MORPH, *paramManager),
		    .hpf_freq = getSmoothedPatchedParamValue(params::LOCAL_HPF_FREQ, *paramManager),
		    .lpf_modulated = paramView.modulated(params::LOCAL_LPF_FREQ),
		    .hpf_modulated = paramView.modulated(params::LOCAL_HPF_FREQ),
		    .noise_can_sound = paramView.can_sound(params::LOCAL_NOISE_VOLUME),
		    .local_wired = paramView.sources_wired(GLOBALITY_LOCAL),
		    .bend_ranges = paramView.bend_ranges(),
		    .portamento = paramView.unpatched(params::UNPATCHED_PORTAMENTO),
		    .lfo2_synced_phase_increment = (synth_.config().lfos[LFO2_ID].sync_level == SYNC_LEVEL_NONE)
		                                       ? deluge::dsp::UQ<32>{}
		                                       : getSyncedLFOPhaseIncrement(synth_.config().lfos[LFO2_ID]),
		    .lfo4_synced_phase_increment = (synth_.config().lfos[LFO4_ID].sync_level == SYNC_LEVEL_NONE)
		                                       ? deluge::dsp::UQ<32>{}
		                                       : getSyncedLFOPhaseIncrement(synth_.config().lfos[LFO4_ID]),
		    .expression_sources_changed = expressionSourcesChangedAtSynthLevel,
		    .monophonic_expression_values = monophonicExpressionValues,
		    .clipping_amount = renderEffects().saturation.clipping_amount,
		    .doing_osc_sync = renderingOscillatorSyncCurrently(paramManager),
		    .global_source_values = params.source_values,
		    .note_tails_allowed = allowNoteTails(modelStackWithSoundFlags, true),
		};
		for (int32_t s = 0; s < kNumSources; s++) {
			facts.dx_patch_changed[s] = sourceResources[s].dxPatchChanged;
			facts.source_ever_active[s] = modelStackWithSoundFlags->checkSourceEverActive(s);
			facts.source_active_currently[s] = isSourceActiveCurrently(s, paramManager);
		}
		const deluge::synth::BlockValues blockValues =
		    deluge::instruments::classic::DelugeSynth::begin_block(hostBlock, facts);

		// The matrix's per-block input. Sound-level + stable across the voice loop (the param sets are fixed and
		// paramLPF is only mutated after the loop), so build it once here rather than per voice.
		const deluge::synth::matrix::MatrixBlock controlBlock = matrixBlock(*paramManager, PresetGather::ROUTED);

#if defined(DELUGE_STREAM_BENCH)
		voiceHost_.benchFrames = sound_mono.size();
#endif
		// Without the USB interrupt's time: it lands in a Sound's voices by chance, and is not what they cost
		const uint32_t voicesStart = deluge_work_cycle_count();
		const uint32_t voicesRendered =
		    synth_.voices().render(controlBlock, synth_.config(), blockValues,
		                           deluge::synth::VoiceRenderArgs{
		                               .buffer = sound_mono.data(),
		                               .num_samples = static_cast<int32_t>(sound_mono.size()),
		                               .sound_in_stereo = voice_rendered_in_stereo,
		                               .pan_at_voice_level = applyingPanAtVoiceLevel,
		                               .pitch_adjust = hostBlock.pitch_adjust,
		                               .spare_buffers = {spareRenderingBuffer[0], spareRenderingBuffer[1],
		                                                 spareRenderingBuffer[2], spareRenderingBuffer[3]},
		                               .filter_scratch = deluge::render::filter_scratch(),
		                               .sync_scratch = deluge::render::osc_sync_scratch(),
		                               .noise = &deluge::synth_noise,
		                           },
		                           modelStackWithSoundFlags);
		voiceCost_.add_render(deluge_work_cycle_count() - voicesStart,
		                      voicesRendered * static_cast<uint32_t>(sound_mono.size()));

		// We know that nothing's patched to pan, so can read it in this very basic way.
		const dsp::Q<31> pan = halve_floor(paramManager->getPatchedParamSet()->getKnob(params::LOCAL_PAN));
		dsp::fixed::PanAmplitudes amplitudes{};
		bool doPanning = false;
		if (AudioEngine::renderInStereo) {
			amplitudes = dsp::fixed::pan_amplitudes(pan);
			doPanning = pan != dsp::Q<31>{};
		}

		// If just rendered in mono, double that up to stereo now
		if (!voice_rendered_in_stereo) {
			if (doPanning) {
				// right to left because of in-place mono to stereo expansion
				std::transform(sound_mono.rbegin(), sound_mono.rend(), sound_stereo.rbegin(), [=](dsp::Q<31> sample) {
					return dsp::panned_mono(sample, amplitudes.left, amplitudes.right);
				});
			}
			else {
				// right to left because of in-place mono to stereo expansion
				std::transform(sound_mono.rbegin(), sound_mono.rend(), sound_stereo.rbegin(),
				               [](dsp::Q<31> sample) { return StereoSample::from_mono(sample); });
			}
		}

		// Or if rendered in stereo...
		// And if we're only applying pan here at the Sound level...
		else if (!applyingPanAtVoiceLevel && doPanning) {
			for (StereoSample& sample : sound_stereo) {
				sample = dsp::panned_stereo(sample.l, sample.r, amplitudes.left, amplitudes.right);
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
			std::fill(sound_mono.data() + sound_mono.size(), sound_mono.data() + (sound_stereo.size() * 2),
			          dsp::Q<31>{});
		}
	}

	checkStack("delay");
	if (fxSection_.process(
	        sound_stereo,
	        fxSectionInputs(*this, *paramManager, delayWorkingState, !synth_.voices().empty(), reverbSendAmount),
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
		sourceResources[i].dxPatchChanged = false;
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
			                getGlobalLFOPhaseIncrement<params::GLOBAL_LFO_FREQ_1>(LFO1_ID));
			globalLFO3.tick(AudioEngine::audioSampleTimer - timeStartedSkippingRenderingLFO,
			                getGlobalLFOPhaseIncrement<params::GLOBAL_LFO_FREQ_2>(LFO3_ID));

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
				    AudioEngine::audioSampleTimer - AudioEngine::timeLastSideChainHit, renderEffects().sidechain,
				    playbackHandler.getTimePerInternalTickInverse());
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
			    deluge::synth::matrix::global_param_value<params::GLOBAL_ARP_RATE>(params.param_final_values));
			getArp()->gatePos +=
			    (phaseIncrement >> 8) * (AudioEngine::audioSampleTimer - timeStartedSkippingRenderingArp);

			timeStartedSkippingRenderingArp = AudioEngine::audioSampleTimer;
		}
	}
}

template <deluge::modulation::params::Global P>
deluge::dsp::UQ<32> Sound::getGlobalLFOPhaseIncrement(LFO_ID lfoId) {
	const deluge::synth::LfoState& config = synth_.config().lfos[lfoId];
	if (config.sync_level == SYNC_LEVEL_NONE) {
		return deluge::synth::matrix::global_param_value<P>(params.param_final_values);
	}
	return getSyncedLFOPhaseIncrement(config);
}

deluge::dsp::UQ<32> Sound::getSyncedLFOPhaseIncrement(const deluge::synth::LfoState& config) {
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
	// The synced increment comes from the transport's integer tick maths
	return deluge::dsp::UQ<32>::from_raw(phaseIncrement);
}

void Sound::resyncGlobalLFOs(bool reset_state) {
	if (!playbackHandler.isEitherClockActive()) {
		return; // no clock, no sync
	}
	// TOOD: convert to a guard clause. Now avoiding re-indenting the whole function.
	if (synth_.config().lfos[LFO1_ID].sync_level != SYNC_LEVEL_NONE) {

		timeStartedSkippingRenderingLFO =
		    AudioEngine::audioSampleTimer; // Resets the thing where the number of samples skipped is later converted
		                                   // into LFO phase increment

		globalLFO1.set_global_initial_phase(synth_.config().lfos[LFO1_ID], reset_state);

		uint32_t timeSinceLastTick;

		int64_t lastInternalTickDone = playbackHandler.getCurrentInternalTickCount(&timeSinceLastTick);

		// If we're right at the first tick, no need to do anything else!
		if (lastInternalTickDone || timeSinceLastTick) {
			uint32_t numInternalTicksPerPeriod =
			    3 << (deluge::plugin::index_of(SYNC_LEVEL_256TH)
			          - deluge::plugin::index_of(synth_.config().lfos[LFO1_ID].sync_level));
			switch (synth_.config().lfos[LFO1_ID].sync_type) {
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
				// Raw: the offset's share of the period, in floating point, as a phase word
				globalLFO1.advance_phase(
				    deluge::dsp::UQ<32>::from_raw((uint32_t)((float)offsetTime / timePerPeriod * 4294967296)));
			}
		}
	}
	if (synth_.config().lfos[LFO3_ID].sync_level != SYNC_LEVEL_NONE) {

		timeStartedSkippingRenderingLFO =
		    AudioEngine::audioSampleTimer; // Resets the thing where the number of samples skipped is later converted
		                                   // into LFO phase increment

		globalLFO3.set_global_initial_phase(synth_.config().lfos[LFO3_ID], reset_state);

		uint32_t timeSinceLastTick;

		int64_t lastInternalTickDone = playbackHandler.getCurrentInternalTickCount(&timeSinceLastTick);

		// If we're right at the first tick, no need to do anything else!
		if (lastInternalTickDone || timeSinceLastTick) {
			uint32_t numInternalTicksPerPeriod =
			    3 << (deluge::plugin::index_of(SYNC_LEVEL_256TH)
			          - deluge::plugin::index_of(synth_.config().lfos[LFO3_ID].sync_level));
			switch (synth_.config().lfos[LFO3_ID].sync_type) {
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
				// Raw: the offset's share of the period, in floating point, as a phase word
				globalLFO3.advance_phase(
				    deluge::dsp::UQ<32>::from_raw((uint32_t)((float)offsetTime / timePerPeriod * 4294967296)));
			}
		}
	}
}

// ------------------------------------
// Mod knobs and effects
// ------------------------------------

bool Sound::learnModKnob(deluge::modulation::KnobTarget target, uint8_t whichKnob, uint8_t modKnobMode, Song* song) {
	// If that knob was patched to something else...
	bool overwroteExistingKnob = (modKnobs[modKnobMode][whichKnob].target != target);

	modKnobs[modKnobMode][whichKnob].target = target;

	if (overwroteExistingKnob) {
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
	}

	return true;
}

// Song may be NULL
void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {

	// We gotta do this for any backedUpParamManagers too!
	song->live.param_backup.forEachOf(
	    deluge::bridge::backupOwnerOf(*this), [&](std::optional<uint32_t> clipId, ParamManager& params) {
		    // A clip only an undo step holds is not in the song: its parked params take the clip-less form.
		    Clip* clip = clipId ? song->clipForId(*clipId) : nullptr;
		    if (clip) {
			    alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
			    ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			        setupModelStackWithThreeMainThingsButNoNoteRow(modelStackMemory, song, this, clip, &params);
			    ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
		    }
		    else {
			    ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&params, song);
		    }
	    });

	song->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(this); // What does this do exactly, again?
}

bool Sound::learnedKnobTurns(Song* song, int32_t p) {
	const deluge::project::ParamTarget param = deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(p), params::Kind::UNPATCHED_SOUND).value);
	using Kind = deluge::project::ControlsAddress::Kind;
	const std::optional<uint32_t> id =
	    isDrum() ? static_cast<SoundDrum*>(this)->id() : static_cast<SoundInstrument*>(this)->id();
	const std::optional<deluge::project::ControlsAddress> address =
	    id ? std::optional{deluge::project::ControlsAddress{isDrum() ? Kind::DRUM : Kind::OUTPUT, *id}} : std::nullopt;
	return deluge::bridge::learnedKnobTurns(song, *effectsOwner(), address, param);
}

const uint8_t patchedParamsWhichShouldBeZeroIfNoKnobAssigned[] = {
    params::LOCAL_PITCH_ADJUST, params::LOCAL_OSC_A_PITCH_ADJUST, params::LOCAL_OSC_B_PITCH_ADJUST,
    params::LOCAL_MODULATOR_0_PITCH_ADJUST, params::LOCAL_MODULATOR_1_PITCH_ADJUST};

void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(ParamManager* paramManager,
                                                                                     Song* song) {

	for (int32_t i = 0; i < sizeof(patchedParamsWhichShouldBeZeroIfNoKnobAssigned); i++) {
		int32_t p = patchedParamsWhichShouldBeZeroIfNoKnobAssigned[i];
		ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(paramManager, p, song);
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
			if ((modKnobs[k][w].target == deluge::modulation::knobParam(modelStack->paramId))) {
				return;
			}
		}
	}

	// No knobs were assigned to this param, so make it 0
	if (!learnedKnobTurns(modelStack->song, modelStack->paramId)) {
		modelStack->autoParam->setCurrentValueWithNoReversionOrRecording(modelStack, 0);
	}
}

void Sound::ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(ParamManager* paramManager, int32_t p,
                                                                      Song* song) {

	AutoParam* param = &paramManager->getPatchedParamSet()->params[p];

	// If the param is automated, we'd better not try setting it to 0 - the user probably wants the automation
	if (param->isAutomated(*paramManager->getPatchedParamSet()->docForParam(p))) {
		return;
	}

	for (int32_t k = 0; k < kNumModButtons; k++) {
		for (int32_t w = 0; w < kNumPhysicalModKnobs; w++) {
			if ((modKnobs[k][w].target == deluge::modulation::knobParam(p))) {
				return;
			}
		}
	}

	// No knobs were assigned to this param, so make it 0 (in the store — the persisted source — and the cursor).
	if (!learnedKnobTurns(song, p)) {
		param->setBase(*paramManager->getPatchedParamSet()->docForParam(p), 0);
	}
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it.
void Sound::voiceUnassigned(ModelStackWithSoundFlags* modelStack) {
	reassessRenderSkippingStatus(modelStack);
}

// Can handle NULL modelStack, which you'd only want to do if no Voices active
void Sound::recalculateAllVoicePhaseIncrements(ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // walks this Sound's voices, which the render edits (free on the audio tier)

	if (synth_.voices().empty() || modelStack == nullptr) {
		return; // These two "should" always be false in tandem...
	}

	for (SoundVoice* voice : synth_.voices().list()) {
		calculateVoicePhaseIncrements(*voice, *modelStack);
	}
}

void Sound::revoiceUnison(int32_t oldNum, int32_t newNum, ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // the render walks and edits this Sound's voices
	// Effective volume has changed. Need to pass that change onto Voices
	for (SoundVoice* voice : synth_.voices().list()) {
		if (getSynthMode() == SynthMode::SUBTRACTIVE) {
			for (int32_t s = 0; s < kNumSources; s++) {
				bool sourceEverActive = modelStack->checkSourceEverActive(s);

				if (sourceEverActive && getSynthMode() != SynthMode::FM && oscType(s) == OscType::SAMPLE
				    && voice->part.guides[s].audioFileHolder && voice->part.guides[s].audioFileHolder->audioFile) {

					// For samples, set the current play pos for the new unison part, if num unison went up
					if (newNum > oldNum) {

						SourceSlot* newPart = &voice->kernel.unison_parts[oldNum].slots[s];
						SourceSlot* oldPart = &voice->kernel.unison_parts[oldNum - 1].slots[s];

						newPart->active = oldPart->active;

						if (newPart->active) {
							newPart->osc_pos = oldPart->osc_pos;
							newPart->phase_increment_stored_value = oldPart->phase_increment_stored_value;
							newPart->carrier_feedback = oldPart->carrier_feedback;

							SamplePlaybackAdapter& newSample = voice->part.ports(oldNum, s).sample;
							if (!newSample.cloneVoiceSampleFrom(voice->part.ports(oldNum - 1, s).sample)) {
								newPart->active = false;
								continue;
							}

							VoiceSample& newVoiceSample = *newSample.voiceSample();

							// Don't do any caching for new part. Old parts will stop using their cache anyway
							// because their pitch will have changed
							newVoiceSample.stopUsingCache(
							    &voice->part.guides[s], (Sample*)voice->part.guides[s].audioFileHolder->audioFile,
							    voiceRating(*voice),
							    voice->part.guides[s].getLoopingType(synth_.config().sources[s].repeat_mode)
							        == LoopType::LOW_LEVEL);
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

bool Sound::anyNoteIsOn() {

	ArpeggiatorSettings* arpSettings = getArpSettings();

	if (arpSettings != nullptr && arpSettings->mode != ArpMode::OFF) {
		return (getArp()->hasAnyInputNotesActive());
	}

	return !synth_.voices().empty();
}

bool Sound::hasFilters() {
	return (renderEffects().filter.lpf_mode != FilterMode::OFF || renderEffects().filter.hpf_mode != FilterMode::OFF);
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
	ArpParamSet* arp = paramManager->getArpParamSet(); // the arp/probability/spread params land in its store
	arp->bindDomStore();
	deluge::project::DocParamSet* aStore = arp->domStore();
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "patchCables")) {
			deluge::bridge::loadCables(*paramManager->getPatchCableSet(),
			                           *deluge::storage::treeReaderOf(reader).currentValue(), readAutomationUpToPos);
			reader.exitTag("patchCables");
		}
		else if (deluge::bridge::readSoundParamTagToDoc(reader, tagName, *pStore, *uStore, readAutomationUpToPos,
		                                                aStore)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	reader.match('}');
	unpatched->projectFromStoreAfterRead(unpatchedSummary);
	patched->projectFromStoreAfterRead(patchedSummary);
	arp->projectFromStoreAfterRead(paramManager->getArpParamSetSummary());
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
	deluge::instruments::classic::SoundState decoded = *state_;
	if (!deluge::instruments::classic::decode(*element, decoded,
	                                          {.input_tick_magnitude = modelStack->song->getInputTickMagnitude()})) {
		return Error::FILE_CORRUPTED;
	}
	effectsBinding_.delay_type_pending = !deluge::effects::saves_delay_type(*element);
	const deluge::project::SoundRouting routing = deluge::project::decode_routing(*element, *routing_);

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

	// Old FM patches can have a filter mode saved in them even though it wouldn't have rendered at the time
	if (deluge::instruments::classic::synth_mode(decoded) == SynthMode::FM
	    && song_firmware_version < FirmwareVersion::community({1, 2, 0})) {
		deluge::instruments::classic::sound_effects(decoded).filter.hpf_mode = FilterMode::OFF;
		deluge::instruments::classic::sound_effects(decoded).filter.lpf_mode = FilterMode::OFF;
	}
	if (Error error = applyState(decoded); error != Error::NONE) {
		return error;
	}
	*routing_ = routing;
	applyRouting();

	// A sound with params files them with the Song
	if (paramManager.containsAnyMainParamCollections()) {
		modelStack->song->live.param_backup.backUp(deluge::bridge::backupOwnerOf(*this),
		                                           (Clip*)modelStack->getTimelineCounterAllowNull(), &paramManager,
		                                           true);
	}

	// The sound plays what the file holds, as the decode makes it; the reader above set its runtime to the same, but
	// for a mod FX type the runtime could not allocate, which it plays without
	deluge::instruments::classic::sound_effects(decoded).mod_fx.type = effects().mod_fx.type;
	adoptState(std::move(decoded));

	return Error::NONE;
}

Error Sound::createParamManagerForLoading(ParamManagerForTimeline* paramManager) {

	Error error = paramManager->setupWithPatching();
	if (error != Error::NONE) {
		return error;
	}

	initParams(paramManager);

	paramManager->getUnpatchedParamSet()->params[params::UNPATCHED_SIDECHAIN_SHAPE].setBase(
	    *paramManager->getUnpatchedParamSet()->docForParam(params::UNPATCHED_SIDECHAIN_SHAPE),
	    2147483647); // Hmm, why this here? Obviously I had some reason...
	return Error::NONE;
}

void Sound::writeParamsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation) {
	// Serialize from the value DOM (deluge::project). serializeSoundParams emits the patched + unpatched flat attrs,
	// the shared effects param block, the mid-block writeOpeningTagEnd, the <envelope*> groups, the
	// <patchCables> block and the shared tags.
	deluge::project::DocParamManager doc = deluge::bridge::toDocParamManagerFromStore(*paramManager);
	deluge::bridge::serializeSoundParams(**deluge::bridge::storeFor(doc, params::Kind::PATCHED),
	                                     **deluge::bridge::storeFor(doc, params::Kind::UNPATCHED_SOUND),
	                                     paramManager->getPatchCableSet()->records().records(), doc.cable_depths,
	                                     writer, writeAutomation, doc.arp ? &*doc.arp : nullptr);
}

void Sound::deriveConfig() {
	synth_.configure(*state_);
}

static_assert(deluge::instruments::classic::kMaxUnisonVoices == kMaxNumVoicesUnison
              && deluge::instruments::classic::kMaxUnisonDetune == kMaxUnisonDetune
              && deluge::instruments::classic::kMaxUnisonSpread == kMaxUnisonStereoSpread);

Error Sound::applyState(const deluge::instruments::classic::SoundState& state) {
	// The config the rest of the read plays with, until the read's state is adopted
	synth_.configure(state);
	resyncGlobalLFOs();

	// The mod-FX type first, on its own. A ring type whose ring cannot be allocated keeps its type and passes the audio
	// through; the next effects edit on this sound retries.
	deluge::effects::EffectStates effects = deluge::instruments::classic::sound_effects(state);
	if (effects.mod_fx.type != this->effects().mod_fx.type) {
		const ModFXType type = effects.mod_fx.type;
		deluge::model::changeEffects(*effectsOwner(),
		                             [type](deluge::effects::EffectStates& e) { e.mod_fx.type = type; });
	}
	effects.mod_fx.type = this->effects().mod_fx.type;
	setEffects(effects);
	// what a save before any render writes back
	core().compressor().dsp().set_threshold(deluge::effects::compressor_knobs(effects.compressor).threshold);

	for (int32_t s = 0; s < kNumSources; s++) {
		const auto id = static_cast<deluge::instruments::classic::SourceId>(s);
		const OscType type = deluge::instruments::classic::source_type(state, id);
		SourceResources& source = sourceResources[s];
		source.setOscType(type);
		if (const deluge::instruments::classic::DxPatchState* from =
		        deluge::instruments::classic::dx_patch(state, id)) {
			DxPatch* patch = source.ensureDxPatch();
			if (patch == nullptr) {
				return Error::INSUFFICIENT_RAM;
			}
			std::ranges::copy(from->params, patch->params.begin());
			// The engine follows the bytes: an auto patch on algorithm 4 or 6 with feedback renders in MkI
			patch->setEngineMode(from->engine_mode);
			patch->random_detune = from->random_detune;
		}

		// The ranges, of the kind the oscillator type plays.
		const bool wavetable = type == OscType::WAVETABLE;
		source.ranges.clear();
		(void)source.ranges.changeType(wavetable ? sizeof(MultiWaveTableRange) : sizeof(MultisampleRange));
		for (const deluge::synth::SourceRange& range : deluge::instruments::classic::source_ranges(state, id)) {
			MultiRangeArray::RangeVariant variant =
			    wavetable ? MultiRangeArray::RangeVariant(std::in_place_type<MultiWaveTableRange>)
			              : MultiRangeArray::RangeVariant(std::in_place_type<MultisampleRange>);
			MultiRange* built = MultiRangeArray::variantToRange(variant);
			built->topNote = range.top_note;
			built->getAudioFileHolder()->filePath = range.file_path;
			if (!wavetable) {
				auto* holder = static_cast<SampleHolderForVoice*>(built->getAudioFileHolder());
				holder->startPos = range.zone.start;
				holder->endPos = range.zone.end;
				holder->loopStartPos = range.zone.loop_start;
				holder->loopEndPos = range.zone.loop_end;
				holder->transpose = range.tuning.transpose;
				holder->cents = range.tuning.cents;
			}
			if (Error error = source.ranges.insertVariant(std::ssize(source.ranges), std::move(variant));
			    error != Error::NONE) {
				return error;
			}
		}
	}
	return Error::NONE;
}

MultiRange* Sound::getOrCreateFirstRange(int32_t s) {
	const bool had = !sourceResources[s].ranges.empty();
	MultiRange* range = sourceResources[s].getOrCreateFirstRange();
	if (range != nullptr && !had) {
		recordHeldRanges(s);
	}
	return range;
}

void Sound::recordHeldRanges(int32_t s) {
	const auto id = static_cast<deluge::instruments::classic::SourceId>(s);
	if (std::vector held = sourceResources[s].heldRanges();
	    !std::ranges::equal(held, deluge::instruments::classic::source_ranges(*state_, id))) {
		*state_ = deluge::instruments::classic::with_ranges(std::move(*state_), id, std::move(held));
	}
}

void Sound::recordHeldDxPatch(int32_t s) {
	const auto id = static_cast<deluge::instruments::classic::SourceId>(s);
	const deluge::instruments::classic::DxPatchState* recorded = deluge::instruments::classic::dx_patch(*state_, id);
	if (std::optional held = sourceResources[s].heldDxPatch();
	    held.has_value() != (recorded != nullptr) || (held && *held != *recorded)) {
		*state_ = deluge::instruments::classic::with_dx_patch(std::move(*state_), id, held);
	}
}

void Sound::adoptState(deluge::instruments::classic::SoundState loaded) {
	// A sound bound into a slot plays the slot's record; any other plays its own state
	*state_ = std::move(loaded);
	bindEffects(&deluge::instruments::classic::sound_effects(*state_));
	deriveConfig();
}

void Sound::bindState(deluge::instruments::classic::SoundState& record, deluge::project::SoundRouting& routing) {
	if (state_ == &record) {
		return;
	}
	const bool own = state_ == &ownedState_;
	if (own) {
		record = std::move(ownedState_); // the record holds it
	}
	else {
		record = *state_; // another record's, which stays that record's
	}
	routing = *routing_;
	state_ = &record;
	routing_ = &routing;
	bindEffects(&deluge::instruments::classic::sound_effects(
	    record)); // copies the effects it played, which the move left in place
	if (own) {
		ownedState_ = {}; // no second copy stays behind
		ownedRouting_ = {};
	}
	deriveConfig();
	applyRouting();
}

void Sound::unbindState() {
	if (state_ == &ownedState_) {
		return;
	}
	ownedState_ = std::move(*state_); // the record's state moves back into the sound
	ownedRouting_ = *routing_;
	state_ = &ownedState_;
	routing_ = &ownedRouting_;
	bindEffects(&deluge::instruments::classic::sound_effects(ownedState_));
	deriveConfig();
	applyRouting();
}

void Sound::applyRouting() {
	sideChainSendLevel = routing_->side_chain_send;
	outputMidiChannel = routing_->midi_channel.value_or(MIDI_CHANNEL_NONE);
	outputMidiNoteForDrum = routing_->midi_note.value_or(MIDI_NOTE_NONE);
}

void Sound::writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager, Song* knobsSong,
                        ArpeggiatorSettings* arpSettings, const deluge::project::ArpRecord* arpRecord,
                        const char* pathAttribute) {
	// The instrument writes its own settings; the effects', the params, the arpeggiator and the mod knobs sit between
	// its pieces in the file.
	const deluge::serialization::Value routing = deluge::project::encode_routing(*routing_);
	const deluge::instruments::classic::SoundPieces pieces = deluge::instruments::classic::encode_pieces(
	    *state_, {.input_tick_magnitude = currentSong->getInputTickMagnitude()},
	    // the threshold the render last applied, its param's
	    core().compressor().dsp().threshold().raw(), &routing);

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
		deluge::bridge::writeArpAttributes(writer, *arpSettings, arpRecord, false,
		                                   currentSong->getInputTickMagnitude());
		writer.closeTag();
	}

	// Mod knobs, as the sound holds them
	writer.writeArrayStart("modKnobs");
	for (int32_t k = 0; k < kNumModButtons; k++) {
		for (int32_t w = 0; w < kNumPhysicalModKnobs; w++) {
			// the controller mappings' runtime copy, as the file's descriptor words name it
			const deluge::synth::CableTarget word = deluge::modulation::knobWord(modKnobs[k][w].target);
			writer.writeOpeningTagBeginning("modKnob", true);
			writer.writeAttribute("controlsParam",
			                      params::paramNameForFile(params::Kind::UNPATCHED_SOUND, word.param_index()), false);
			if (!word.is_param()) {
				writer.writeAttribute("patchAmountFromSource", sourceToString(word.top_source()), false);

				if (word.has_second_source()) {
					writer.writeAttribute("patchAmountFromSecondSource", sourceToString(word.second_source_from_top()));
				}
			}
			writer.closeTag(true);
		}
	}
	writer.writeArrayEnding("modKnobs");

	writer.writeValue(pieces.midi_output);
	writer.writeValue(pieces.delay);
	deluge::model::writeMidiKnobs(writer, *effectsOwner(), knobsSong, deluge::bridge::controlsAddressOf(*this));
	writer.writeValue(pieces.effect_tags);
}

int16_t Sound::getMaxOscTranspose(InstrumentClip* clip) {

	int32_t maxRawOscTranspose = -32768;
	for (int32_t s = 0; s < kNumSources; s++) {
		if (getSynthMode() == SynthMode::FM || oscType(s) != OscType::SAMPLE) {
			maxRawOscTranspose = std::max<int32_t>(maxRawOscTranspose, synth_.config().sources[s].transpose);
		}
	}

	if (getSynthMode() == SynthMode::FM) {
		maxRawOscTranspose = std::max(maxRawOscTranspose, (int32_t)synth_.config().engine.modulators[0].transpose);
		maxRawOscTranspose = std::max(maxRawOscTranspose, (int32_t)synth_.config().engine.modulators[1].transpose);
	}

	if (maxRawOscTranspose == -32768) {
		maxRawOscTranspose = 0;
	}

	ArpeggiatorSettings* arpSettings = getArpSettings(clip);

	if (arpSettings != nullptr && arpSettings->mode != ArpMode::OFF) {
		maxRawOscTranspose += (arpSettings->numOctaves - 1) * 12;
	}

	return maxRawOscTranspose + synth_.config().voicing.transpose;
}

int16_t Sound::getMinOscTranspose() {

	int32_t minRawOscTranspose = 32767;
	for (int32_t s = 0; s < kNumSources; s++) {
		if (getSynthMode() == SynthMode::FM || oscType(s) != OscType::SAMPLE) {
			minRawOscTranspose = std::min<int32_t>(minRawOscTranspose, synth_.config().sources[s].transpose);
		}
	}

	if (getSynthMode() == SynthMode::FM) {
		minRawOscTranspose = std::min(minRawOscTranspose, (int32_t)synth_.config().engine.modulators[0].transpose);
		minRawOscTranspose = std::min(minRawOscTranspose, (int32_t)synth_.config().engine.modulators[1].transpose);
	}

	if (minRawOscTranspose == 32767) {
		minRawOscTranspose = 0;
	}

	return minRawOscTranspose + synth_.config().voicing.transpose;
}

deluge::storage::Walk Sound::walkSourceDemands(deluge::storage::SampleLoadVisitor& visitor) {

	for (int32_t s = 0; s < kNumSources; s++) {
		if (oscType(s) == OscType::SAMPLE || oscType(s) == OscType::WAVETABLE) {
			if (sourceResources[s].walkSampleDemands(visitor, playsReversed(s)) == deluge::storage::Walk::Stop) {
				return deluge::storage::Walk::Stop;
			}
		}
	}

	return deluge::storage::Walk::Continue;
}

namespace {
/// Whether an envelope in @p mode holds its note, where the mode alone says: an AHD is a one-shot, AR and MPE hold
/// while the note does. An ADSR's params decide.
std::optional<bool> holds_by_mode(deluge::synth::EnvelopeMode mode) {
	switch (mode) {
	case deluge::synth::EnvelopeMode::AHD:
		return false;
	case deluge::synth::EnvelopeMode::AR:
	case deluge::synth::EnvelopeMode::MPE:
		return true;
	case deluge::synth::EnvelopeMode::ADSR:
		break;
	}
	return std::nullopt;
}
} // namespace

bool Sound::envelopeHasSustainCurrently(int32_t e, ParamManagerForTimeline* paramManager) {
	if (const std::optional<bool> holds = holds_by_mode(config().envelopes[e].mode)) {
		return *holds;
	}

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	// These params are fetched "pre-LPF"
	return (patchedParams->getValue(params::LOCAL_ENV_0_SUSTAIN + e) != -2147483648
	        || patchedParams->getValue(params::LOCAL_ENV_0_DECAY + e)
	               > patchedParams->getValue(params::LOCAL_ENV_0_RELEASE + e));
}

bool Sound::envelopeHasSustainEver(int32_t e, ParamManagerForTimeline* paramManager) {
	if (const std::optional<bool> holds = holds_by_mode(config().envelopes[e].mode)) {
		return *holds;
	}

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	return (patchedParams->params[params::LOCAL_ENV_0_SUSTAIN + e].containsSomething(
	            patchedParams->playedLane(params::LOCAL_ENV_0_SUSTAIN + e), -2147483648)
	        || patchedParams->params[params::LOCAL_ENV_0_DECAY + e].isAutomated(
	            patchedParams->playedLane(params::LOCAL_ENV_0_DECAY + e))
	        || patchedParams->params[params::LOCAL_ENV_0_RELEASE + e].isAutomated(
	            patchedParams->playedLane(params::LOCAL_ENV_0_RELEASE + e))
	        || patchedParams->getValue(params::LOCAL_ENV_0_DECAY + e)
	               > patchedParams->getValue(params::LOCAL_ENV_0_RELEASE + e));
}

// modelStack may be NULL
void Sound::fastReleaseAllVoices(ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section;                                     // the render walks and edits this Sound's voices
	synth_.voices().fast_release_all(kSoftCullIncrement, modelStack); // Accepts NULL
}

void Sound::prepareForHibernation() {
	wontBeRenderedForAWhile();
	detachSourcesFromAudioFiles();
}

// This can get called either for hibernation, or because drum now has no active noteRow
void Sound::wontBeRenderedForAWhile() {
	deluge::model::chainWontRender(*effectsOwner());

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
		sourceResources[s].detachAllAudioFiles();
	}
}

void Sound::deleteMultiRange(int32_t s, int32_t r) {
	// Because range storage is about to change, must unassign all voices, and make sure no more can be assigned
	// during memory allocation
	// An AudioTickHold holds off only the audio tick, not the render, so excluding the render here needs an
	// AudioExcludedSection.
	AudioExcludedSection section;
	killAllVoices();
	sourceResources[s].ranges.erase(sourceResources[s].ranges.begin() + r); // Destructs the range
}

// This function has to give the same outcome as SourceResources::renderInStereo()
VoiceKind Sound::voiceKind() const {
	if (getSynthMode() == SynthMode::RINGMOD) {
		return VoiceKind::RINGMOD;
	}
	bool sample = false;
	for (const deluge::synth::SourceConfig& source : synth_.config().sources) {
		if (source.osc_type == OscType::DX7) {
			return VoiceKind::FM;
		}
		sample |= source.osc_type == OscType::SAMPLE || source.osc_type == OscType::INPUT_L
		          || source.osc_type == OscType::INPUT_R || source.osc_type == OscType::INPUT_STEREO;
	}
	if (getSynthMode() == SynthMode::FM) {
		return VoiceKind::FM;
	}
	return sample ? VoiceKind::SAMPLE : VoiceKind::OSCILLATOR;
}

bool Sound::renderingVoicesInStereo(ModelStackWithSoundFlags* modelStack) {
	AudioExcludedSection section; // walks this Sound's voices, which the render edits (free on the audio tier)

	// audioDriver deciding we're rendering in mono overrides everything
	if (!AudioEngine::renderInStereo) {
		return false;
	}

	if (synth_.voices().empty()) {
		return false;
	}

	// Stereo live-input
	if ((oscType(0) == OscType::INPUT_STEREO || oscType(1) == OscType::INPUT_STEREO)
	    && (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) {
		return true;
	}

	if (modelStack->paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(params::LOCAL_PAN)) {
		return true;
	}

	if (synth_.config().voicing.unison_spread && synth_.config().voicing.unison.count > 1) {
		return true;
	}

	uint32_t mustExamineSourceInEachVoice = 0;

	// Have a look at what samples, if any, each source holds
	for (int32_t s = 0; s < kNumSources; s++) {
		SourceResources* source = &sourceResources[s];

		if (!modelStack->checkSourceEverActive(s)) {
			continue;
		}

		if (oscType(s) == OscType::SAMPLE) { // Just SAMPLE, because WAVETABLEs can't be stereo.

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
		for (const SoundVoice* voice : synth_.voices().list()) {
			for (int32_t s = 0; s < kNumSources; s++) {
				if (mustExamineSourceInEachVoice & (1 << s)) {
					AudioFileHolder* holder = voice->part.guides[s].audioFileHolder;
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

void Sound::killAllVoices() {
	AudioExcludedSection section; // the reverse flag and the voices change together: the render reads both
	// Reset invertReversed flag so all voices get its reverse settings back to normal
	invertReversed = false;

	synth_.voices().kill_all();
}

std::optional<uint32_t> Sound::cullRating(bool skipCullFading) const {
	return synth_.voices().cull_rating(synth_.config().voicing.priority, skipCullFading);
}

void Sound::cull(deluge::synth::CullKind kind, uint32_t increment) {
	synth_.voices().cull(synth_.config().voicing.priority, kind, increment);
}

#pragma GCC diagnostic pop

void Sound::noteVoicesEdit() {
	deluge::noteStructuralEdit(deluge::StructuralContainer::VOICES);
}
