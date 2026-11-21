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

#pragma once

#include "instruments/deluge/dx7_init_voice.h"
#include "instruments/deluge/settings.h"
#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/state_access.h"
#include "support/record_state.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <utility>

/// @file
/// @brief The sounds the synth state's specs run over, built through the record: every engine with every pair of
/// oscillator kinds, each plain and varied (the recorded reference's rows), then sounds made for the codec's corners,
/// then a new sound.
namespace deluge::test {

namespace detail {

/// @brief Write settings row @p path as the file text @p text, as a file places it: no menu rule, any engine or kind.
inline void put(instrument::SoundState& state, std::string_view path, std::string_view text) {
	const std::optional<serialization::Value> value =
	    instrument::canonical_setting(path, serialization::Value::makeScalar(std::string(text)));
	if (!value || plugin::find_setting(instrument::kSettings, path) == nullptr) {
		std::fprintf(stderr, "sound_matrix: %.*s takes no %.*s\n", static_cast<int>(path.size()), path.data(),
		             static_cast<int>(text.size()), text.data());
		std::abort();
	}
	(void)instrument::write_setting(state, path, *value);
}

/// @brief Oscillator @p s's settings row @p key ("osc1/<key>" or "osc2/<key>").
inline std::string osc(size_t s, std::string_view key) {
	return std::format("osc{}/{}", s + 1, key);
}

} // namespace detail

/// @brief A sound with engine @p mode and kinds @p a and @p b, every oscillator field set to a value its default is
/// not, whatever its kind holds.
inline instrument::SoundState plain_sound(instrument::SynthMode mode, instrument::OscType a, instrument::OscType b) {
	using namespace instrument;
	SoundState s;
	const OscType kinds[] = {a, b};
	for (size_t i = 0; i < 2; i++) {
		const auto id = static_cast<SourceId>(i);
		detail::put(s, detail::osc(i, "type"), to_string(kinds[i]));
		detail::put(s, detail::osc(i, "transpose"), "5");
		detail::put(s, detail::osc(i, "cents"), "-7");
		detail::put(s, detail::osc(i, "loopMode"), std::to_string(std::to_underlying(SampleRepeatMode::LOOP)));
		detail::put(s, detail::osc(i, "reversed"), "1");
		std::vector<RangeState> ranges{{.top_note = 40, .file_path = "SAMPLES/A.WAV", .start_pos = 10, .end_pos = 900},
		                               {.top_note = 127, .file_path = "SAMPLES/B.WAV", .end_pos = 400, .transpose = 2}};
		s = with_ranges(std::move(s), id, std::move(ranges));
		s = with_dx_patch(std::move(s), id,
		                  DxPatchState{.params = kDx7InitVoice, .engine_mode = 2, .random_detune = 3});
	}
	detail::put(s, "osc1/retrigPhase", std::to_string(static_cast<int32_t>(0x40000000)));
	detail::put(s, "osc2/retrigPhase", "-1");
	detail::put(s, "osc2/oscillatorSync", "1");
	detail::put(s, "modulator1/transpose", "3");
	detail::put(s, "modulator2/transpose", "-12");
	detail::put(s, "modulator1/retrigPhase", "-1");
	detail::put(s, "modulator2/retrigPhase", "7");
	detail::put(s, "modulator2/toModulator1", "1");
	detail::put(s, "lfo2/syncLevel", std::to_string(std::to_underlying(plugin::SyncLevel::EIGHTH)));
	detail::put(s, "mode", to_string(mode));
	return s;
}

/// @brief plain_sound() with its second oscillator and the retriggers changed and its ranges and patch gone.
inline instrument::SoundState varied_sound(instrument::SynthMode mode, instrument::OscType a, instrument::OscType b) {
	using namespace instrument;
	SoundState s = plain_sound(mode, a, b);
	detail::put(s, "osc2/transpose", "-4");
	detail::put(s, "osc2/cents", "9");
	detail::put(s, "osc2/loopMode", std::to_string(std::to_underlying(SampleRepeatMode::ONCE)));
	detail::put(s, "osc2/reversed", "0");
	s = with_ranges(std::move(s), SourceId::B, {});
	s = with_dx_patch(std::move(s), SourceId::B, std::nullopt);
	detail::put(s, "osc1/retrigPhase", "-1");
	detail::put(s, "osc2/retrigPhase", std::to_string(static_cast<int32_t>(0x20000000)));
	detail::put(s, "osc2/oscillatorSync", "0");
	detail::put(s, "modulator1/cents", "-5");
	detail::put(s, "modulator2/cents", "11");
	return s;
}

/// @brief Sounds made for the codec's corners, by name: a new sound, FM, ring mod, a ranged sample, a DX7, a
/// wavetable, synced LFO and effects, non-zero settings.
inline std::vector<std::pair<std::string, instrument::SoundState>> corner_sounds() {
	using namespace instrument;
	std::vector<std::pair<std::string, SoundState>> states;
	states.emplace_back("new", SoundState{});

	SoundState fm = with(SoundState{}, "mode", to_string(SynthMode::FM));
	set(fm, "modulator1/transpose", "3");
	set(fm, "modulator2/transpose", "-2");
	set(fm, "modulator2/toModulator1", "1");
	// A file can hold an FM sound with its filters on, though the engine switch turns them off
	sound_effects(fm).filter = effects::EffectStates{}.filter;
	states.emplace_back("fm", fm);

	SoundState ring = with(SoundState{}, "osc2/type", to_string(OscType::SAW));
	set(ring, "mode", to_string(SynthMode::RINGMOD));
	states.emplace_back("ringmod", ring);

	SoundState sample = with_kind(SoundState{}, SourceId::A, OscType::SAMPLE);
	set(sample, "osc1/loopMode", "1"); // ONCE
	sample = with_ranges(std::move(sample), SourceId::A,
	                     {RangeState{.top_note = 60, .file_path = "SAMPLES/a.wav", .end_pos = 100},
	                      RangeState{.top_note = kOpenTopNote, .file_path = "SAMPLES/b.wav", .end_pos = 200}});
	states.emplace_back("ranged sample", sample);

	DxPatchState patch{.engine_mode = 2, .random_detune = 5};
	patch.params[7] = 42;
	const SoundState dx = with_dx_patch(with_kind(SoundState{}, SourceId::B, OscType::DX7), SourceId::B, patch);
	states.emplace_back("dx7", dx);

	SoundState wavetable = with_kind(SoundState{}, SourceId::A, OscType::WAVETABLE);
	wavetable = with_ranges(std::move(wavetable), SourceId::A,
	                        {RangeState{.top_note = kOpenTopNote, .file_path = "WAVETABLES/w.wav"}});
	set(wavetable, "osc2/oscillatorSync", "1");
	states.emplace_back("wavetable", wavetable);

	SoundState synced =
	    with(SoundState{}, "lfo2/syncLevel", std::to_string(std::to_underlying(deluge::plugin::SyncLevel::EIGHTH)));
	sound_effects(synced).delay.sync_level = deluge::plugin::SyncLevel::QUARTER;
	sound_effects(synced).sidechain.sync_level = deluge::plugin::SyncLevel::SIXTEENTH;
	states.emplace_back("synced", synced);

	SoundState nonzero = SoundState{};
	set(nonzero, "transpose", "5");
	set(nonzero, "cents", "-7");
	effects::EffectStates& fx = sound_effects(nonzero);
	fx.saturation.clipping_amount = 4;
	fx.stutter.use_song_stutter = false;
	fx.filter.lpf_mode = deluge::effects::FilterMode::OFF;
	fx.mod_fx.type = deluge::effects::ModFXType::CHORUS;
	fx.compressor.ratio = 7 << 24;
	states.emplace_back("non-zero", nonzero);
	return states;
}

/// @brief The oscillator kinds the matrix pairs.
inline constexpr instrument::OscType kMatrixKinds[] = {
    instrument::OscType::SINE,      instrument::OscType::SAW,     instrument::OscType::SUPERSAW,
    instrument::OscType::WAVETABLE, instrument::OscType::SAMPLE,  instrument::OscType::DX7,
    instrument::OscType::INPUT_L,   instrument::OscType::INPUT_R, instrument::OscType::INPUT_STEREO};

/// @brief Whether @p name is one of the recorded reference's rows: an engine and kind pair, or the new sound.
inline bool is_kind_row(std::string_view name) {
	return name == "default" || name.ends_with(" plain") || name.ends_with(" varied");
}

/// @brief Call @p check with every sound of the matrix and its name: each engine and kind pair plain and varied, named
/// "<mode> <a> <b> plain|varied" by their enum values; the corner sounds; "default", a new sound.
template <class Check>
void for_each_named_sound(const Check& check) {
	using namespace instrument;
	for (const SynthMode mode : {SynthMode::SUBTRACTIVE, SynthMode::RINGMOD, SynthMode::FM}) {
		for (const OscType a : kMatrixKinds) {
			for (const OscType b : kMatrixKinds) {
				const std::string name =
				    std::format("{} {} {}", std::to_underlying(mode), std::to_underlying(a), std::to_underlying(b));
				check(name + " plain", plain_sound(mode, a, b));
				check(name + " varied", varied_sound(mode, a, b));
			}
		}
	}
	for (const auto& [name, state] : corner_sounds()) {
		check(name, state);
	}
	check(std::string{"default"}, SoundState{});
}

/// @brief Call @p check with every sound of the matrix.
template <class Check>
void for_each_sound(const Check& check) {
	for_each_named_sound([&](const std::string&, const instrument::SoundState& state) { check(state); });
}

} // namespace deluge::test
