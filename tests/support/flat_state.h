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

// CppSpec spells `context` as a macro; a spec or scenario that includes this after cppspec.hpp must still see the
// instrument's headers as written.
#pragma push_macro("context")
#undef context

#include "instruments/deluge/codec.h"
#include "instruments/deluge/flat_layout.h"
#include "instruments/deluge/settings.h"
#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/state_access.h"
#include "instruments/deluge/state_rules.h"
#include "serialization/tree.h"
#include "serialization/value.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace deluge::test {

/// @brief A record read in the file's flat layout, live: each access flattens the record as it is then, so a spec
/// that reads it after an Edit sees the Edit. Empty where there is no record.
class FlatView {
public:
	/// @brief The record flattened for one expression.
	struct Snapshot {
		instrument::FlatSoundState flat;
		const instrument::FlatSoundState* operator->() const { return &flat; }
	};
	explicit FlatView(const instrument::SoundState* record) : record_(record) {}
	Snapshot operator->() const { return {instrument::FlatSoundState(*record_)}; }
	instrument::FlatSoundState operator*() const { return instrument::FlatSoundState(*record_); }
	[[nodiscard]] bool has_value() const { return record_ != nullptr; }
	explicit operator bool() const { return has_value(); }

private:
	const instrument::SoundState* record_;
};

/// @brief @p record read live in the file's flat layout.
[[nodiscard]] inline FlatView flat_of(const instrument::SoundState* record) {
	return FlatView{record};
}

/// @brief Change @p record through its flat layout, as a spec that builds a state field by field does.
template <class Change>
void change_flat(instrument::SoundState& record, Change&& change) {
	instrument::FlatSoundState flat(record);
	change(flat);
	record = instrument::SoundState(flat);
}

/// @brief The first state_access reader on which @p record and its flat layout disagree; empty where none does.
[[nodiscard]] inline std::string reader_difference(const instrument::SoundState& record) {
	namespace instrument = deluge::instrument;
	const instrument::FlatSoundState flat(record);
	if (instrument::synth_mode(record) != flat.synth_mode) {
		return "synth_mode";
	}
	for (size_t s = 0; s < flat.sources.size(); s++) {
		const auto id = static_cast<instrument::SourceId>(s);
		const instrument::SourceState& src = flat.sources[s];
		if (instrument::source_type(record, id) != src.osc_type) {
			return "source_type";
		}
		if (!(instrument::source_tuning(record, id)
		      == instrument::Tuning{.transpose = src.transpose, .cents = src.cents})) {
			return "source_tuning";
		}
		if (!(instrument::sample_playback(record, id)
		      == instrument::SamplePlayback{.repeat_mode = src.repeat_mode,
		                                    .time_stretch_amount = src.time_stretch_amount,
		                                    .interpolation = src.interpolation_mode,
		                                    .pitch_and_speed_independent = src.pitch_and_speed_independent,
		                                    .reversed = src.reversed})) {
			return "sample_playback";
		}
		if (instrument::source_ranges(record, id) != src.ranges) {
			return "source_ranges";
		}
		const instrument::DxPatchState* patch = instrument::dx_patch(record, id);
		if ((patch != nullptr) != src.dx_patch.has_value() || (patch != nullptr && !(*patch == *src.dx_patch))) {
			return "dx_patch";
		}
	}
	const synth::VoicingState voicing = instrument::voicing(record);
	if (voicing.priority != flat.voice_priority || voicing.polyphony != flat.polyphonic
	    || voicing.max_voices != flat.max_voice_count || voicing.master.transpose != flat.transpose
	    || voicing.master.cents != flat.cents) {
		return "voicing";
	}
	if (instrument::oscillator_sync(record) != flat.oscillator_sync) {
		return "oscillator_sync";
	}
	for (size_t s = 0; s < flat.sources.size(); s++) {
		if (instrument::phase_word(instrument::source_retrigger(record, static_cast<instrument::SourceId>(s)))
		    != flat.osc_retrigger_phase[s]) {
			return "source_retrigger";
		}
	}
	const std::array<synth::Pitch, 2> mods = instrument::modulators(record);
	for (size_t m = 0; m < mods.size(); m++) {
		if (mods[m].tuning.transpose != flat.modulator_transpose[m] || mods[m].tuning.cents != flat.modulator_cents[m]
		    || instrument::phase_word(mods[m].retrigger) != flat.modulator_retrigger_phase[m]) {
			return "modulators";
		}
	}
	if (instrument::modulator1_to_modulator0(record) != flat.modulator1_to_modulator0) {
		return "modulator1_to_modulator0";
	}
	for (size_t l = 0; l < 4; l++) {
		const synth::LfoState lfo = instrument::lfo(record, l);
		if (lfo.shape != flat.lfo_wave_type[l] || lfo.sync_type != flat.lfo_sync_type[l]
		    || lfo.sync_level != flat.lfo_sync_level[l]) {
			return "lfo";
		}
	}
	if (!(instrument::sound_effects(record) == flat.effects)) {
		return "sound_effects";
	}
	if (!(instrument::unison(record)
	      == deluge::synth::UnisonState{
	          .count = flat.num_unison, .detune = flat.unison_detune, .spread = flat.unison_stereo_spread})) {
		return "unison";
	}
	if (!(instrument::host_fields(record)
	      == instrument::HostFields{.side_chain_send_level = flat.side_chain_send_level,
	                                .output_midi_channel = flat.output_midi_channel,
	                                .output_midi_note_for_drum = flat.output_midi_note_for_drum})) {
		return "host_fields";
	}
	return {};
}

/// @brief The first rule on which the record's native form and the flat oracle disagree for @p flat; empty where none
/// does: every engine switch, every oscillator kind on each slot (with the held ranges of either kind), every loop
/// mode, and the setup transforms.
[[nodiscard]] inline std::string rule_difference(const instrument::FlatSoundState& flat) {
	namespace in = instrument;
	const in::SoundState record(flat);
	for (const in::SynthMode mode : {in::SynthMode::SUBTRACTIVE, in::SynthMode::RINGMOD, in::SynthMode::FM}) {
		if (!(in::switch_engine(record, mode) == in::SoundState(in::with_mode(flat, mode)))) {
			return "switch_engine";
		}
	}
	const in::OscType kinds[] = {in::OscType::SINE,      in::OscType::SAW,     in::OscType::SUPERSAW,
	                             in::OscType::WAVETABLE, in::OscType::SAMPLE,  in::OscType::DX7,
	                             in::OscType::INPUT_L,   in::OscType::INPUT_R, in::OscType::INPUT_STEREO};
	for (size_t s = 0; s < 2; s++) {
		const auto id = static_cast<in::SourceId>(s);
		for (const in::OscType type : kinds) {
			for (const bool wavetable : {false, true}) {
				if (!(in::switch_source(record, id, type, flat.sources[s].ranges, wavetable)
				      == in::SoundState(in::with_osc_type(flat, id, type, flat.sources[s].ranges, wavetable)))) {
					return "switch_source";
				}
			}
		}
		for (const in::SampleRepeatMode mode : {in::SampleRepeatMode::CUT, in::SampleRepeatMode::ONCE,
		                                        in::SampleRepeatMode::LOOP, in::SampleRepeatMode::STRETCH}) {
			if (!(in::with_loop_mode(record, id, mode) == in::SoundState(in::with_loop_mode(flat, id, mode)))) {
				return "with_loop_mode";
			}
		}
	}
	if (!(in::as_sample_sound(record) == in::SoundState(in::as_sample_sound(flat)))) {
		return "as_sample_sound";
	}
	if (!(in::as_default_synth(record) == in::SoundState(in::as_default_synth(flat)))) {
		return "as_default_synth";
	}
	for (const bool dx : {false, true}) {
		if (!(in::as_blank_synth(record, dx) == in::SoundState(in::as_blank_synth(flat, dx)))) {
			return "as_blank_synth";
		}
	}
	return {};
}

/// @brief The first settings row on which the record's native form and the flat oracle disagree for @p flat: what a
/// row reads, holds, takes and allows, and what writing it makes (the state and the entries it changes), for each
/// row's candidate values (every choice, both bools, an integer's least, greatest, default and middle values).
/// Empty where none does.
[[nodiscard]] inline std::string setting_difference(const instrument::FlatSoundState& flat) {
	namespace in = instrument;
	const in::SoundState record(flat);
	for (const plugin::SettingInfo& row : in::kSettings) {
		if (row.kind == plugin::SettingKind::STRUCTURED || row.kind == plugin::SettingKind::TEXT) {
			continue;
		}
		const std::string path(row.path);
		if (in::setting_value(record, row.path) != in::setting_value(flat, row.path)) {
			return path + " value";
		}
		if (in::held_setting(record, row.path) != in::held_setting(flat, row.path)) {
			return path + " held";
		}
		if (in::takes_setting(record, row.path) != in::takes_setting(flat, row.path)) {
			return path + " takes";
		}
		std::vector<std::string> candidates;
		if (row.kind == plugin::SettingKind::CHOICE) {
			for (const std::string_view choice : row.choices) {
				candidates.emplace_back(choice);
			}
		}
		else if (row.kind == plugin::SettingKind::BOOL) {
			candidates = {"0", "1"};
		}
		else {
			for (const int64_t n : {int64_t{row.min}, int64_t{row.max}, int64_t{row.default_value},
			                        (int64_t{row.min} + int64_t{row.max}) / 2}) {
				candidates.push_back(std::to_string(n));
			}
		}
		for (const std::string& text : candidates) {
			const std::optional<serialization::Value> value =
			    in::canonical_setting(row.path, serialization::Value::makeScalar(text));
			if (!value) {
				continue;
			}
			if (in::allows_setting(record, row.path, *value) != in::allows_setting(flat, row.path, *value)) {
				return path + " allows " + text;
			}
			in::SoundState written = record;
			in::FlatSoundState flat_written = flat;
			const std::vector<std::string_view> entries = in::write_setting(written, row.path, *value);
			const std::vector<std::string_view> flat_entries = in::write_setting(flat_written, row.path, *value);
			if (!(written == in::SoundState(flat_written))) {
				return path + " write " + text;
			}
			if (entries != flat_entries) {
				return path + " entries " + text;
			}
		}
	}
	return {};
}

/// @brief Where the record's encoding and the flat oracle's first differ for @p flat: the whole element, each entry
/// (with and without a compressor threshold in place of the state's), and an element's entries replaced, at three tick
/// magnitudes. Empty where none does.
[[nodiscard]] inline std::string encode_difference(const instrument::FlatSoundState& flat) {
	namespace in = instrument;
	const in::SoundState record(flat);
	constexpr std::array<std::string_view, 27> kKeys{
	    "polyphonic",     "voicePriority", "sideChainSend", "mode",       "transpose",
	    "cents",          "modFXType",     "lpfMode",       "hpfMode",    "filterRoute",
	    "clippingAmount", "maxVoices",     "osc1",          "osc2",       "lfo1",
	    "lfo2",           "lfo3",          "lfo4",          "modulator1", "modulator2",
	    "unison",         "midiOutput",    "delay",         "sidechain",  "audioCompressor",
	    "stutter",        "notAKey"};
	for (const int32_t magnitude : {0, 1, 2}) {
		const plugin::CodecContext codec_context{.input_tick_magnitude = magnitude};
		if (std::string d =
		        serialization::first_difference(in::encode(record, codec_context), in::encode(flat, codec_context));
		    !d.empty()) {
			return "encode at " + d;
		}
		for (const std::string_view key : kKeys) {
			for (const std::optional<int32_t> threshold : {std::optional<int32_t>{}, std::optional<int32_t>{12345}}) {
				const auto a = in::encode_entry(record, key, codec_context, threshold);
				const auto b = in::encode_entry(flat, key, codec_context, threshold);
				if (a.has_value() != b.has_value() || (a && !serialization::first_difference(*a, *b).empty())) {
					return "encode_entry " + std::string(key);
				}
			}
		}
		serialization::Value element = serialization::Value::makeMap();
		element.set("name", serialization::Value::makeScalar("x"));
		element.set("compressor", serialization::Value::makeMap());
		element.set("osc2", serialization::Value::makeMap());
		element.set("defaultParams", serialization::Value::makeMap());
		if (std::string d = serialization::first_difference(in::with_state(element, record, codec_context),
		                                                    in::with_state(element, flat, codec_context));
		    !d.empty()) {
			return "with_state at " + d;
		}
	}
	return {};
}

} // namespace deluge::test

#pragma pop_macro("context")
