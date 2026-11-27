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

#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/source_bridge.h"

#include "effects/settings.h"
#include "effects/words.h"
#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/dx7_init_voice.h"
#include "instruments/classic/settings.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/state_rules.h"
#include "instruments/classic/words.h"
#include "model/clip/instrument_clip.h"
#include "model/fx/effects_owner.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "plugin/types/sync.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/sound/source_resources.h"
#include "serialization/tree.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "util/comparison.h"
#include "util/notify.h"

namespace deluge::bridge {

namespace {

/// The source index a path's `oscN/` prefix names, or -1 when it names none; @p rest is the path after the prefix.
int32_t oscillatorOf(std::string_view path, std::string_view& rest) {
	if (path.size() > 5 && path.starts_with("osc") && (path[3] == '1' || path[3] == '2') && path[4] == '/') {
		rest = path.substr(5);
		return path[3] - '1';
	}
	return -1;
}

/// Whether @p rest (a path after its `oscN/`) is a sample-playback setting.
bool isSamplePlayback(std::string_view rest) {
	return rest == "loopMode" || rest == "reversed" || rest == "timeStretchEnable" || rest == "timeStretchAmount"
	       || rest == "linearInterpolation";
}

/// The model stack a gesture on @p sound works in: its clip (a kit row's note row in its kit's clip) and the params it
/// plays with there, or its backed-up params when it has no clip. Nullptr when it has no params at all.
ModelStackWithSoundFlags* modelStackFor(void* memory, ::Sound& sound, ::Song& song) {
	if (sound.isDrum()) {
		auto& drum = static_cast<SoundDrum&>(sound);
		auto* clip = drum.kit != nullptr ? static_cast<InstrumentClip*>(drum.kit->getActiveClip()) : nullptr;
		int32_t noteRowIndex = 0;
		NoteRow* noteRow = clip != nullptr ? clip->getNoteRowForDrum(&drum, &noteRowIndex) : nullptr;
		if (noteRow != nullptr) {
			return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, &song, clip, noteRowIndex, noteRow, &drum,
			                                                          &noteRow->paramManager)
			    ->addSoundFlags();
		}
		ParamManager* params = song.live.param_backup.getPreferablyWithClip(&drum, nullptr);
		return params != nullptr
		           ? setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, &drum, clip, params)->addSoundFlags()
		           : nullptr;
	}
	auto& synth = static_cast<SoundInstrument&>(sound);
	Clip* clip = synth.getActiveClip();
	ParamManager* params =
	    clip != nullptr ? &clip->paramManager : song.live.param_backup.getPreferablyWithClip(&synth, nullptr);
	return params != nullptr
	           ? setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, &synth, clip, params)->addSoundFlags()
	           : nullptr;
}

/// The settings @p from with the effect setting at @p path made @p text / @p number, as its menu sets it; nullopt when
/// @p path is no effect's. A mod-FX type whose ring cannot be allocated keeps its type and passes the audio through
/// until the next effects edit (the ring is taken where the type reaches the render).
std::optional<deluge::effects::EffectStates> withEffectValue(deluge::effects::EffectStates e, std::string_view path,
                                                             const std::string& text, int32_t number) {
	namespace fx = deluge::effects;
	const bool on = number != 0;
	if (path == "modFXType") {
		e.mod_fx.type = fx::parse_mod_fx_type(text);
	}
	else if (path == "lpfMode") {
		e.filter.lpf_mode = fx::parse_filter_mode(text);
	}
	else if (path == "hpfMode") {
		e.filter.hpf_mode = fx::parse_filter_mode(text);
	}
	else if (path == "filterRoute") {
		e.filter.route = fx::parse_filter_route(text);
	}
	else if (path == "clippingAmount") {
		e.saturation.clipping_amount = fx::clipping_amount_from(number);
	}
	else if (path == "delay/pingPong") {
		e.delay.ping_pong = on;
	}
	else if (path == "delay/type") {
		e.delay.type = fx::parse_delay_type(text);
	}
	else if (path == "delay/syncType") {
		e.delay.sync_type = static_cast<SyncType>(number);
	}
	else if (path == "delay/syncLevel") {
		e.delay.sync_level = deluge::plugin::sync_level_from_index(number);
	}
	else if (path == "sidechain/attack") {
		e.sidechain.attack = number;
	}
	else if (path == "sidechain/release") {
		e.sidechain.release = number;
	}
	else if (path == "sidechain/syncType") {
		e.sidechain.sync_type = static_cast<SyncType>(number);
	}
	else if (path == "sidechain/syncLevel") {
		e.sidechain.sync_level = deluge::plugin::sync_level_from_index(number);
	}
	else if (path == "audioCompressor/attack") {
		e.compressor.attack = number;
	}
	else if (path == "audioCompressor/release") {
		e.compressor.release = number;
	}
	else if (path == "audioCompressor/thresh") {
		e.compressor.threshold = number;
	}
	else if (path == "audioCompressor/ratio") {
		e.compressor.ratio = number;
	}
	else if (path == "audioCompressor/compHPF") {
		e.compressor.sidechain_hpf = number;
	}
	else if (path == "audioCompressor/compBlend") {
		e.compressor.blend = number;
	}
	else if (path == "stutter/quantized") {
		e.stutter.quantized = on;
	}
	else if (path == "stutter/reverse") {
		e.stutter.reversed = on;
	}
	else if (path == "stutter/pingPong") {
		e.stutter.ping_pong = on;
	}
	else if (path == "stutter/useSongStutter") {
		e.stutter.use_song_stutter = on;
	}
	else {
		return std::nullopt;
	}
	return e;
}
} // namespace

bool runtimeTakes(const ::Sound& sound, std::string_view path) {
	const bool fm = sound.getSynthMode() == SynthMode::FM;
	std::string_view rest;
	if (const int32_t s = oscillatorOf(path, rest); s >= 0) {
		const OscType type = sound.oscType(s);
		if (isSamplePlayback(rest)) {
			const bool input = type == OscType::INPUT_L || type == OscType::INPUT_R || type == OscType::INPUT_STEREO;
			return !fm && (type == OscType::SAMPLE || (input && rest == "linearInterpolation"));
		}
		if (rest == "transpose" || rest == "cents") {
			return fm || type != OscType::SAMPLE;
		}
		return true;
	}
	if (path.starts_with("modulator")) {
		return fm;
	}
	return true;
}

Error applyChange(::Sound& sound, const deluge::instruments::classic::SoundState& before,
                  const deluge::instruments::classic::SoundState& after, ::Song& song) {
	using deluge::instruments::classic::Part;
	const deluge::instruments::classic::Parts parts = deluge::instruments::classic::DelugeSynth::changes(before, after);
	const auto changed = [&](Part part) { return parts.test(std::to_underlying(part)); };
	const auto slotPart = [](Part a, int32_t s) { return static_cast<Part>(std::to_underlying(a) + s); };
	const auto playsFiles = [](OscType type) {
		return util::one_of(type, {OscType::INPUT_L, OscType::INPUT_R, OscType::INPUT_STEREO, OscType::SAMPLE,
		                           OscType::DX7, OscType::WAVETABLE});
	};

	// Voices stop first, as each setting's menu stops them before it changes anything.
	bool stopVoices = changed(Part::MODE);
	for (int32_t s = 0; s < kNumSources; s++) {
		const auto id = static_cast<deluge::instruments::classic::SourceId>(s);
		const deluge::instruments::classic::SamplePlayback was =
		    deluge::instruments::classic::sample_playback(before, id);
		const deluge::instruments::classic::SamplePlayback now =
		    deluge::instruments::classic::sample_playback(after, id);
		stopVoices =
		    stopVoices
		    || (changed(slotPart(Part::SOURCE_A_TYPE, s))
		        && (playsFiles(deluge::instruments::classic::source_type(before, id))
		            || playsFiles(deluge::instruments::classic::source_type(after, id))))
		    || was.reversed != now.reversed
		    || (was.repeat_mode != now.repeat_mode
		        && (was.repeat_mode == SampleRepeatMode::STRETCH || now.repeat_mode == SampleRepeatMode::STRETCH));
	}
	if (stopVoices) {
		sound.killAllVoices();
	}

	// The sound's sources follow the record; a new type reshapes a source's ranges.
	bool squareCameOrWent = false;
	for (int32_t s = 0; s < kNumSources; s++) {
		SourceResources& source = sound.sourceResources[s];
		const auto id = static_cast<deluge::instruments::classic::SourceId>(s);
		const bool reversedNow = deluge::instruments::classic::sample_playback(after, id).reversed;
		if (changed(slotPart(Part::SOURCE_A_TYPE, s))) {
			const OscType type = deluge::instruments::classic::source_type(after, id);
			squareCameOrWent = squareCameOrWent
			                   || deluge::instruments::classic::source_type(before, id) == OscType::SQUARE
			                   || type == OscType::SQUARE;
			source.setOscType(type);
			// The render reads the type from the config: it learns the new one as soon as the ranges have it. Retyping
			// itself can release clusters, which must not happen with the render excluded.
			AudioExcludedSection section;
			sound.deriveConfig();
		}
		if (deluge::instruments::classic::sample_playback(before, id).reversed != reversedNow) {
			// A new direction re-claims the ranges' clusters; the zone ends it clamps go back into the record
			source.followDirection(reversedNow != source.invertReversed); // the config is not yet the new one
			sound.recordHeldRanges(s);
		}
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = modelStackFor(memory, sound, song);
	{
		// The render sees the new config with its voices already retuned and re-voiced.
		AudioExcludedSection section;
		sound.deriveConfig();
		if (changed(Part::TUNING) || changed(Part::UNISON_COUNT) || changed(Part::UNISON_DETUNE)
		    || changed(Part::MODULATOR_TUNING) || changed(Part::SOURCE_A_TUNING) || changed(Part::SOURCE_B_TUNING)
		    || changed(Part::DX7_TUNING)) {
			sound.recalculateAllVoicePhaseIncrements(modelStack);
		}
		if (changed(Part::UNISON_COUNT)) {
			// After the retune: a new part plays on from its neighbour, increment included
			sound.revoiceUnison(deluge::instruments::classic::unison(before).count,
			                    deluge::instruments::classic::unison(after).count, modelStack);
		}
	}
	if (changed(Part::EFFECTS)) {
		deluge::model::storeEffects(*sound.effectsOwner(), deluge::instruments::classic::sound_effects(after));
	}
	if (changed(Part::MODE) || squareCameOrWent || changed(Part::LFO_SYNC)) {
		sound.setupPatchingForAllParamManagers(&song);
	}
	if (changed(Part::LFO_WAVE) || changed(Part::LFO_SYNC)) {
		sound.resyncGlobalLFOs();
	}

	// The ranges and DX7 patches the runtime holds follow the change's, as the record held them before it: a new type
	// has just reshaped them in the runtime, which reflected them into the record. A source whose ranges or patch the
	// runtime refuses keeps what the runtime holds, in the record too.
	const auto optionalOf = [](const deluge::instruments::classic::DxPatchState* patch) {
		return patch != nullptr ? std::optional{*patch} : std::nullopt;
	};
	for (int32_t s = 0; s < kNumSources; s++) {
		const auto id = static_cast<deluge::instruments::classic::SourceId>(s);
		const deluge::instruments::classic::DxPatchState* wantedPatch =
		    deluge::instruments::classic::dx_patch(after, id);
		const bool retyped = changed(slotPart(Part::SOURCE_A_TYPE, s));
		const std::optional<deluge::instruments::classic::DxPatchState> heldPatch =
		    retyped ? sound.sourceResources[s].heldDxPatch()
		            : optionalOf(deluge::instruments::classic::dx_patch(before, id));
		// Ranges are copied only when they may differ: a retyped slot's runtime reshaped its own
		const bool rangesDiffer =
		    retyped ? sound.sourceResources[s].heldRanges() != deluge::instruments::classic::source_ranges(after, id)
		            : !deluge::instruments::classic::same_ranges(before, after, id);
		Error error = Error::NONE;
		if (wantedPatch != nullptr && heldPatch != *wantedPatch && sound.oscType(s) == OscType::DX7) {
			error = applyDxPatch(sound, s, *wantedPatch);
		}
		if (error == Error::NONE && rangesDiffer) {
			// A copy: applying them writes the sound's record, which `after` may be
			const std::vector<synth::SourceRange> wanted = deluge::instruments::classic::source_ranges(after, id);
			error = applyRanges(sound, s, wanted);
		}
		if (error != Error::NONE) {
			deluge::instruments::classic::SoundState& record = sound.record();
			record = deluge::instruments::classic::with_ranges(
			    deluge::instruments::classic::with_dx_patch(std::move(record), id,
			                                                sound.sourceResources[s].heldDxPatch()),
			    id, sound.sourceResources[s].heldRanges());
			return error;
		}
	}
	// The DX7 engine's voice follows the change's while the DX7 plays; one the runtime cannot make keeps what it holds
	if (const deluge::instruments::classic::DxPatchState* wanted = deluge::instruments::classic::dx7_patch(after);
	    wanted != nullptr && deluge::instruments::classic::synth_mode(after) == SynthMode::DX7
	    && sound.dx7Patch.state() != *wanted && !sound.dx7Patch.assign(*wanted)) {
		sound.recordHeldDx7Patch();
		return Error::INSUFFICIENT_RAM;
	}
	return Error::NONE;
}

void retuneVoices(::Sound& sound, ::Song& song) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (ModelStackWithSoundFlags* modelStack = modelStackFor(memory, sound, song)) {
		sound.recalculateAllVoicePhaseIncrements(modelStack);
	}
}

void zoneMoved(::Sound& sound, int32_t s, MarkerType marker, ::Song& song) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (ModelStackWithSoundFlags* modelStack = modelStackFor(memory, sound, song)) {
		sound.sampleZoneChanged(marker, s, modelStack);
	}
}

Error applyEffectSetting(deluge::model::EffectsOwner owner, std::string_view path,
                         const deluge::serialization::Value& value) {
	if (!deluge::effects::check_effect_setting(path, value)) {
		return Error::UNSPECIFIED;
	}
	const std::optional<deluge::effects::EffectStates> next =
	    withEffectValue(owner.effects(), path, *deluge::serialization::text_of(&value),
	                    deluge::serialization::int_of(&value).value_or(0));
	if (!next) {
		return Error::UNSPECIFIED;
	}
	deluge::model::storeEffects(owner, *next);
	return Error::NONE;
}

std::optional<deluge::instruments::classic::SoundState> stateWithSetting(const ::Sound& sound, std::string_view path,
                                                                         const deluge::serialization::Value& value) {
	namespace instrument = deluge::instruments::classic;
	if (path == "mode") {
		return instrument::switch_engine(sound.state(), instrument::parse_synth_mode(value.str()));
	}
	std::string_view rest;
	const int32_t s = oscillatorOf(path, rest);
	if (s < 0 || (rest != "type" && rest != "loopMode")) {
		return std::nullopt;
	}
	const auto id = static_cast<instrument::SourceId>(s);
	if (rest == "loopMode") {
		const auto mode = static_cast<SampleRepeatMode>(deluge::serialization::int_of(&value).value_or(0));
		return instrument::with_loop_mode(sound.state(), id, mode);
	}
	// The ranges the slot plays are the runtime's, as the record holds them; their kind is the runtime's too.
	const std::vector<synth::SourceRange> held = sound.sourceResources[s].heldRanges();
	const bool heldAreWavetable = sound.sourceResources[s].ranges.currentElementSize() == sizeof(MultiWaveTableRange);
	return instrument::switch_source(sound.state(), id, instrument::parse_osc_type(value.str()), held,
	                                 heldAreWavetable);
}

} // namespace deluge::bridge
