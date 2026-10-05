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

#include "engine-api/types/sync.h"
#include "engines/deluge/settings.h"
#include "engines/deluge/words.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "serialization/tree.h"
#include "util/comparison.h"

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
	const LFOConfig& now = sound.snapshot().lfoConfig[l];
	if (rest == "type") {
		sound.setLfoWaveType(l, deluge::instrument::parse_lfo_type(text));
	}
	else if (rest == "syncType") {
		sound.setLfoSync(l, static_cast<SyncType>(number), now.syncLevel);
		sound.setupPatchingForAllParamManagers(&song);
	}
	else if (rest == "syncLevel") {
		sound.setLfoSync(l, now.syncType, static_cast<SyncLevel>(number));
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
	char memory[MODEL_STACK_MAX_SIZE];
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
	char memory[MODEL_STACK_MAX_SIZE];
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

} // namespace

Error applySetting(::Sound& sound, std::string_view path, const deluge::serialization::Value& value, ::Song& song) {
	if (!deluge::instrument::check_setting(path, value)) {
		return Error::UNSPECIFIED;
	}
	const std::string& text = *deluge::serialization::text_of(&value);
	const int32_t number = deluge::serialization::int_of(&value).value_or(0);
	std::string_view rest;
	if (const int32_t s = oscillatorOf(path, rest); s >= 0) {
		return applyOscillatorSetting(sound, s, rest, text, number, song) ? Error::NONE : Error::UNSPECIFIED;
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
	}
	else if (path == "cents") {
		sound.set_cents(static_cast<int8_t>(number));
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

} // namespace deluge::bridge
