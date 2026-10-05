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

#include "engines/deluge/settings.h"
#include "engines/deluge/words.h"
#include "processing/sound/sound.h"
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

/// Apply an `oscN/` path: false when the path has no setter here.
bool applyOscillatorSetting(::Sound& sound, int32_t s, std::string_view rest, const std::string& text, int32_t number,
                            ::Song& song) {
	if (rest == "type") {
		changeOscType(sound, s, deluge::instrument::parse_osc_type(text), song);
	}
	else if (rest == "transpose") {
		sound.sources[s].setTranspose(static_cast<int16_t>(number));
	}
	else if (rest == "cents") {
		sound.sources[s].setCents(number);
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
	else {
		return Error::UNSPECIFIED; // the path has no setter here
	}
	return Error::NONE;
}

} // namespace deluge::bridge
