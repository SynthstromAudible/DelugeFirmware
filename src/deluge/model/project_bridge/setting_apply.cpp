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
#include "instruments/deluge/deluge_synth.h"
#include "instruments/deluge/dx7_init_voice.h"
#include "instruments/deluge/settings.h"
#include "instruments/deluge/state_rules.h"
#include "instruments/deluge/words.h"
#include "model/clip/instrument_clip.h"
#include "model/fx/effects_owner.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "plugin-api/types/sync.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
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

/// Change source @p s of @p sound to @p type as the oscillator type menu does: voices stop when either type plays
/// audio files, input or a DX7 patch, and the params are re-patched when square comes or goes (its pulse width).
void changeOscType(::Sound& sound, int32_t s, OscType type, ::Song& song) {
	const OscType old = sound.sources[s].oscType;
	const auto needsUnassignment = {
	    OscType::INPUT_L, OscType::INPUT_R, OscType::INPUT_STEREO, OscType::SAMPLE, OscType::DX7, OscType::WAVETABLE,
	};
	if (util::one_of(old, needsUnassignment) || util::one_of(type, needsUnassignment)) {
		sound.killAllVoices();
	}
	sound.sources[s].setOscType(type);
	if (old == OscType::SQUARE || type == OscType::SQUARE) {
		sound.setupPatchingForAllParamManagers(&song);
	}
}

/// Apply an `oscN/` path: false when the path has no setter here, or names sample playback on a source that does not
/// use it (interpolation: a sample or live-input source; the rest: a sample source).
bool applyOscillatorSetting(::Sound& sound, int32_t s, std::string_view rest, const std::string& text, int32_t number,
                            ::Song& song) {
	if (isSamplePlayback(rest)) {
		Source& source = sound.sources[s];
		const bool input = source.oscType == OscType::INPUT_L || source.oscType == OscType::INPUT_R
		                   || source.oscType == OscType::INPUT_STEREO;
		// Only a sample source plays samples; live input is interpolated too.
		if (sound.getSynthMode() == SynthMode::FM
		    || !(source.oscType == OscType::SAMPLE || (input && rest == "linearInterpolation"))) {
			return false;
		}
		if (rest == "loopMode") {
			// Stretching to note length needs pitch and speed independent; leaving it gives the independence up.
			const auto mode = static_cast<SampleRepeatMode>(number);
			if (mode == SampleRepeatMode::STRETCH) {
				sound.killAllVoices();
				source.setPitchAndSpeedIndependent(true);
			}
			else if (source.repeatMode == SampleRepeatMode::STRETCH) {
				sound.killAllVoices();
				source.setPitchAndSpeedIndependent(false);
			}
			source.setRepeatMode(mode);
		}
		else if (rest == "reversed") {
			sound.killAllVoices(); // a voice playing forwards cannot turn round
			source.setReversed(number != 0);
		}
		else if (rest == "timeStretchEnable") {
			source.setPitchAndSpeedIndependent(number != 0);
		}
		else if (rest == "timeStretchAmount") {
			source.setTimeStretchAmount(static_cast<int8_t>(number));
		}
		else {
			source.setInterpolationMode(number != 0 ? InterpolationMode::LINEAR : InterpolationMode::SMOOTH);
		}
		return true;
	}
	if (rest == "type") {
		changeOscType(sound, s, deluge::instrument::parse_osc_type(text), song);
	}
	else if (rest == "transpose" || rest == "cents") {
		// A sample source's tuning is its ranges' (SetSourceRanges); its own is not what a file holds.
		if (sound.getSynthMode() != SynthMode::FM && sound.sources[s].oscType == OscType::SAMPLE) {
			return false;
		}
		if (rest == "transpose") {
			sound.sources[s].setTranspose(static_cast<int16_t>(number));
		}
		else {
			sound.sources[s].setCents(number);
		}
	}
	else if (rest == "retrigPhase") {
		sound.setOscRetriggerPhase(s, static_cast<uint32_t>(number));
	}
	else if (rest == "oscillatorSync" && s == 1) {
		sound.setOscillatorSync(number != 0);
	}
	else {
		return false;
	}
	return true;
}

/// The LFO index a path's `lfoN/` prefix names, or -1 when it names none; @p rest is the path after the prefix.
int32_t lfoOf(std::string_view path, std::string_view& rest) {
	if (path.size() > 5 && path.starts_with("lfo") && path[3] >= '1' && path[3] <= '4' && path[4] == '/') {
		rest = path.substr(5);
		return path[3] - '1';
	}
	return -1;
}

/// Apply an `lfoN/` path as the LFO menus do, resyncing the song-wide LFOs and, for sync, re-patching: false when the
/// path has no setter here.
bool applyLfoSetting(::Sound& sound, int32_t l, std::string_view rest, const std::string& text, int32_t number,
                     ::Song& song) {
	const LFOConfig& now = sound.config().lfos[l];
	if (rest == "type") {
		sound.setLfoWaveType(l, deluge::instrument::parse_lfo_type(text));
	}
	else if (rest == "syncType") {
		sound.setLfoSync(l, static_cast<SyncType>(number), now.sync_level);
		sound.setupPatchingForAllParamManagers(&song);
	}
	else if (rest == "syncLevel") {
		sound.setLfoSync(l, now.sync_type, static_cast<SyncLevel>(number));
		sound.setupPatchingForAllParamManagers(&song);
	}
	else {
		return false;
	}
	sound.resyncGlobalLFOs();
	return true;
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

/// Apply a `unison/` path as the unison menus do, re-tuning the sound's live voices: false when the path has no setter
/// here, or the sound has no params to re-tune its voices with.
bool applyUnisonSetting(::Sound& sound, std::string_view rest, int32_t number, ::Song& song) {
	if (rest == "spread") {
		sound.setUnisonStereoSpread(number);
		return true;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = modelStackFor(memory, sound, song);
	if (modelStack == nullptr && sound.numActiveVoices() != 0) {
		return false;
	}
	if (rest == "num") {
		sound.setNumUnison(number, modelStack);
	}
	else if (rest == "detune") {
		sound.setUnisonDetune(number, modelStack);
	}
	else {
		return false;
	}
	return true;
}

/// Apply a `modulatorN/` path (N = @p m + 1) as the FM modulator menus do, re-tuning the live voices: false when the
/// path has no setter here, the sound is not FM (only FM writes its modulators), or it has live voices and no params.
bool applyModulatorSetting(::Sound& sound, int32_t m, std::string_view rest, int32_t number, ::Song& song) {
	if (sound.getSynthMode() != SynthMode::FM) {
		return false;
	}
	if (rest == "retrigPhase") {
		sound.setModulatorRetriggerPhase(m, static_cast<uint32_t>(number));
		return true;
	}
	if (rest == "toModulator1" && m == 1) {
		sound.setModulator1ToModulator0(number != 0);
		return true;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = modelStackFor(memory, sound, song);
	if (modelStack == nullptr && sound.numActiveVoices() != 0) {
		return false;
	}
	if (rest == "transpose") {
		sound.setModulatorTranspose(m, number, modelStack);
	}
	else if (rest == "cents") {
		sound.setModulatorCents(m, number, modelStack);
	}
	else {
		return false;
	}
	return true;
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
		const OscType type = sound.sources[s].oscType;
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

Error applyChange(::Sound& sound, const deluge::instrument::SoundState& before,
                  const deluge::instrument::SoundState& after, ::Song& song) {
	using deluge::instrument::Part;
	const deluge::instrument::Parts parts = deluge::instrument::DelugeSynth::changes(before, after);
	const auto changed = [&](Part part) { return parts.test(std::to_underlying(part)); };
	const auto slotPart = [](Part a, int32_t s) { return static_cast<Part>(std::to_underlying(a) + s); };
	const auto playsFiles = [](OscType type) {
		return util::one_of(type, {OscType::INPUT_L, OscType::INPUT_R, OscType::INPUT_STEREO, OscType::SAMPLE,
		                           OscType::DX7, OscType::WAVETABLE});
	};

	// Voices stop first, as each setting's menu stops them before it changes anything.
	bool stopVoices = changed(Part::MODE);
	for (int32_t s = 0; s < kNumSources; s++) {
		const deluge::instrument::SourceState& was = before.sources[s];
		const deluge::instrument::SourceState& now = after.sources[s];
		stopVoices =
		    stopVoices
		    || (changed(slotPart(Part::SOURCE_A_TYPE, s)) && (playsFiles(was.osc_type) || playsFiles(now.osc_type)))
		    || was.reversed != now.reversed
		    || (was.repeat_mode != now.repeat_mode
		        && (was.repeat_mode == SampleRepeatMode::STRETCH || now.repeat_mode == SampleRepeatMode::STRETCH));
	}
	if (stopVoices) {
		sound.killAllVoices();
	}

	// The sound's own fields and its sources' follow the record; a new type reshapes a source's ranges.
	if (changed(Part::HOST)) {
		sound.sideChainSendLevel = after.side_chain_send_level;
		sound.outputMidiChannel = after.output_midi_channel;
		sound.outputMidiNoteForDrum = after.output_midi_note_for_drum;
	}
	bool squareCameOrWent = false;
	for (int32_t s = 0; s < kNumSources; s++) {
		Source& source = sound.sources[s];
		if (changed(slotPart(Part::SOURCE_A_TYPE, s))) {
			squareCameOrWent = squareCameOrWent || before.sources[s].osc_type == OscType::SQUARE
			                   || after.sources[s].osc_type == OscType::SQUARE;
			source.setOscType(after.sources[s].osc_type);
		}
		source.reflect(after.sources[s]);
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = modelStackFor(memory, sound, song);
	{
		// The render sees the new config with its voices already retuned and re-voiced.
		AudioExcludedSection section;
		sound.projectSnapshot();
		if (changed(Part::TUNING) || changed(Part::UNISON_COUNT) || changed(Part::UNISON_DETUNE)
		    || changed(Part::MODULATOR_TUNING) || changed(Part::SOURCE_A_TUNING) || changed(Part::SOURCE_B_TUNING)) {
			sound.recalculateAllVoicePhaseIncrements(modelStack);
		}
		if (changed(Part::UNISON_COUNT)) {
			// After the retune: a new part plays on from its neighbour, increment included (Sound::setNumUnison's
			// order)
			sound.revoiceUnison(before.num_unison, after.num_unison, modelStack);
		}
	}
	if (changed(Part::EFFECTS)) {
		deluge::model::storeEffects(*sound.effectsOwner(), after.effects);
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
	for (int32_t s = 0; s < kNumSources; s++) {
		const deluge::instrument::SourceState& wanted = after.sources[s];
		const bool retyped = changed(slotPart(Part::SOURCE_A_TYPE, s));
		deluge::instrument::SourceState held;
		sound.sources[s].seedDxPatchInto(held);
		sound.sources[s].seedRangesInto(held);
		const std::optional<deluge::instrument::DxPatchState>& heldPatch =
		    retyped ? held.dx_patch : before.sources[s].dx_patch;
		const std::vector<deluge::instrument::RangeState>& heldRanges =
		    retyped ? held.ranges : before.sources[s].ranges;
		Error error = Error::NONE;
		if (wanted.dx_patch && heldPatch != wanted.dx_patch && sound.sources[s].oscType == OscType::DX7) {
			error = applyDxPatch(sound, s, *wanted.dx_patch);
		}
		if (error == Error::NONE && heldRanges != wanted.ranges) {
			error = applyRanges(sound, s, wanted.ranges);
		}
		if (error != Error::NONE) {
			if (deluge::instrument::SoundState* record = sound.boundRecord()) {
				sound.sources[s].seedDxPatchInto(record->sources[s]);
				sound.sources[s].seedRangesInto(record->sources[s]);
			}
			return error;
		}
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

namespace {

/// The setting at @p path written into @p sound's runtime.
Error writeSetting(::Sound& sound, std::string_view path, const deluge::serialization::Value& value, ::Song& song) {
	if (!deluge::instrument::check_setting(path, value)) {
		return Error::UNSPECIFIED;
	}
	const std::string& text = *deluge::serialization::text_of(&value);
	const int32_t number = deluge::serialization::int_of(&value).value_or(0);
	if (const std::optional<deluge::effects::EffectStates> next =
	        withEffectValue(sound.effects(), path, text, number)) {
		deluge::model::storeEffects(*sound.effectsOwner(), *next);
		return Error::NONE;
	}
	std::string_view rest;
	if (const int32_t s = oscillatorOf(path, rest); s >= 0) {
		if (!applyOscillatorSetting(sound, s, rest, text, number, song)) {
			return Error::UNSPECIFIED;
		}
		if (rest == "transpose" || rest == "cents") {
			retuneVoices(sound, song);
		}
		return Error::NONE;
	}
	if (const int32_t l = lfoOf(path, rest); l >= 0) {
		return applyLfoSetting(sound, l, rest, text, number, song) ? Error::NONE : Error::UNSPECIFIED;
	}
	if (path.size() > 11 && path.starts_with("modulator") && (path[9] == '1' || path[9] == '2') && path[10] == '/') {
		return applyModulatorSetting(sound, path[9] - '1', path.substr(11), number, song) ? Error::NONE
		                                                                                  : Error::UNSPECIFIED;
	}
	if (path.starts_with("unison/")) {
		return applyUnisonSetting(sound, path.substr(7), number, song) ? Error::NONE : Error::UNSPECIFIED;
	}
	if (path == "polyphonic") {
		sound.setPolyphonic(deluge::instrument::parse_polyphony_mode(text));
	}
	else if (path == "voicePriority") {
		sound.setVoicePriority(static_cast<VoicePriority>(number));
	}
	else if (path == "maxVoices") {
		sound.setMaxVoiceCount(static_cast<uint8_t>(number));
	}
	else if (path == "mode") {
		sound.setSynthMode(deluge::instrument::parse_synth_mode(text), &song);
	}
	else if (path == "transpose") {
		sound.setTranspose(static_cast<int16_t>(number));
		retuneVoices(sound, song);
	}
	else if (path == "cents") {
		sound.set_cents(static_cast<int8_t>(number));
		retuneVoices(sound, song);
	}
	else if (path == "sideChainSend") {
		sound.setSideChainSendLevel(number);
	}
	else if (path == "midiOutput/channel") {
		sound.setOutputMidiChannel(static_cast<uint8_t>(number));
	}
	else if (path == "midiOutput/noteForDrum") {
		sound.setOutputMidiNoteForDrum(static_cast<uint8_t>(number));
	}
	else {
		return Error::UNSPECIFIED; // the path has no setter here
	}
	return Error::NONE;
}

} // namespace

Error applySetting(::Sound& sound, std::string_view path, const deluge::serialization::Value& value, ::Song& song) {
	const Error error = writeSetting(sound, path, value, song);
	if (error == Error::NONE) {
		sound.projectSnapshot(); // an unbound sound's oscillator settings reach its config here
	}
	return error;
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

std::optional<deluge::instrument::SoundState> stateWithSetting(const ::Sound& sound, std::string_view path,
                                                               const deluge::serialization::Value& value) {
	namespace instrument = deluge::instrument;
	if (sound.boundState() == nullptr) {
		return std::nullopt;
	}
	if (path == "mode") {
		return instrument::with_mode(*sound.boundState(), instrument::parse_synth_mode(value.str()));
	}
	std::string_view rest;
	const int32_t s = oscillatorOf(path, rest);
	if (s < 0 || (rest != "type" && rest != "loopMode")) {
		return std::nullopt;
	}
	const auto id = static_cast<instrument::SourceId>(s);
	if (rest == "loopMode") {
		const auto mode = static_cast<SampleRepeatMode>(deluge::serialization::int_of(&value).value_or(0));
		return instrument::with_loop_mode(*sound.boundState(), id, mode);
	}
	// The ranges the slot plays are the runtime's, as the record holds them; their kind is the runtime's too.
	instrument::SourceState held;
	sound.sources[s].seedRangesInto(held);
	const bool heldAreWavetable = sound.sources[s].ranges.currentElementSize() == sizeof(MultiWaveTableRange);
	return instrument::with_osc_type(*sound.boundState(), id, instrument::parse_osc_type(value.str()), held.ranges,
	                                 heldAreWavetable);
}

} // namespace deluge::bridge
