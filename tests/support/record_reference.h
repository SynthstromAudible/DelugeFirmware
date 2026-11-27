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

// CppSpec spells `context` as a macro; a spec that includes this after cppspec.hpp must still see the instrument's
// headers as written.
#pragma push_macro("context")
#undef context

#include "instruments/classic/cable_acceptance.h"
#include "instruments/classic/codec.h"
#include "instruments/classic/settings.h"
#include "instruments/classic/sound_state.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/state_rules.h"
#include "serialization/value.h"
#include "serialization/write_json.h"
#include "support/printers/sound_record_printer.h"
#include "support/sound_matrix.h"

#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief What the instrument answers about one sound, as text per family of questions: its readers, each rule, each
/// settings row, its encoding and its cables. A spec hashes each answer and checks it against a recorded table
/// (`tests/golden/instrument/record_reference.tsv`), so a change to any answer for any sound of the matrix names the
/// sound and the family.
namespace deluge::test {

/// @brief One family's answers for one sound.
struct ReferenceAnswer {
	std::string family;
	std::string text;
};

/// @brief The 32-bit FNV-1a hash of @p text, as 8 hex digits: the recorded form of an answer.
[[nodiscard]] inline std::string reference_hash(std::string_view text) {
	uint32_t hash = 0x811c9dc5U;
	for (const char c : text) {
		hash ^= static_cast<uint8_t>(c);
		hash *= 0x01000193U;
	}
	return std::format("{:08x}", hash);
}

namespace reference_detail {

namespace in = deluge::instruments::classic;

inline std::string text_of(const in::SoundState& state) {
	std::ostringstream os;
	os << state;
	return std::move(os).str();
}

inline std::string text_of(const std::optional<serialization::Value>& value) {
	if (!value) {
		return "none";
	}
	serialization::Value doc = serialization::Value::makeMap();
	doc.set("v", *value);
	return serialization::write_json(doc);
}

/// @brief A range's text in the reference: each field the file writes, side by side, as the recorded hashes pin it.
inline std::string range_text(const synth::SourceRange& r) {
	return std::format(
	    "{{top_note {}, file \"{}\", start {}, end {}, loop_start {}, loop_end {}, transpose {}, cents {}}}",
	    r.top_note, r.file_path, r.zone.start, r.zone.end, r.zone.loop_start, r.zone.loop_end, r.tuning.transpose,
	    r.tuning.cents);
}

inline std::string readers_text(const in::SoundState& state) {
	std::ostringstream os;
	os << "mode " << std::to_underlying(in::synth_mode(state));
	for (const in::SourceId id : {in::SourceId::A, in::SourceId::B}) {
		os << " | slot " << in::slot_index(id) << " type " << std::to_underlying(in::source_type(state, id))
		   << " tuning " << in::source_tuning(state, id) << " playback " << in::sample_playback(state, id)
		   << " ranges ";
		os << '[';
		const char* separator = "";
		for (const synth::SourceRange& r : in::source_ranges(state, id)) {
			os << separator << range_text(r);
			separator = " ";
		}
		os << "] retrigger " << in::phase_word(in::source_retrigger(state, id));
	}
	os << " | dx7 pitch " << in::dx7_pitch(state) << " patch ";
	if (const in::DxPatchState* patch = in::dx7_patch(state)) {
		os << *patch;
	}
	else {
		os << "none";
	}
	const std::array<synth::Pitch, 2> mods = in::modulators(state);
	os << " | voicing " << in::voicing(state) << " sync " << int{in::oscillator_sync(state)} << " modulators ["
	   << mods[0] << " " << mods[1] << "] m1_to_m0 " << int{in::modulator1_to_modulator0(state)} << " lfos [";
	for (size_t l = 0; l < 4; l++) {
		os << in::lfo(state, l) << " ";
	}
	os << "] effects " << in::sound_effects(state) << " unison " << in::unison(state);
	return std::move(os).str();
}

/// The candidate values a settings row is written with: every choice, both bools, an integer's least, greatest,
/// default and middle values.
inline std::vector<std::string> candidates_of(const plugin::SettingInfo& row) {
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
	return candidates;
}

inline std::string setting_text(const in::SoundState& state, const plugin::SettingInfo& row) {
	std::ostringstream os;
	const std::optional<int32_t> held = in::held_setting(state, row.path);
	os << "value " << text_of(in::setting_value(state, row.path)) << " held "
	   << (held ? std::to_string(*held) : std::string{"none"}) << " takes " << int{in::takes_setting(state, row.path)};
	for (const std::string& text : candidates_of(row)) {
		const std::optional<serialization::Value> value =
		    in::canonical_setting(row.path, serialization::Value::makeScalar(text));
		if (!value) {
			os << " | " << text << " not canonical";
			continue;
		}
		in::SoundState written = state;
		const std::vector<std::string_view> entries = in::write_setting(written, row.path, *value);
		os << " | " << text << " allows " << int{in::allows_setting(state, row.path, *value)} << " entries [";
		for (const std::string_view entry : entries) {
			os << entry << " ";
		}
		os << "] same_ranges " << int{in::same_ranges(state, written, in::SourceId::A)}
		   << int{in::same_ranges(state, written, in::SourceId::B)} << " makes " << text_of(written);
	}
	return std::move(os).str();
}

inline std::string encode_text(const in::SoundState& state, int32_t magnitude) {
	const plugin::CodecContext codec_context{.input_tick_magnitude = magnitude};
	std::string text = serialization::write_json(in::encode(state, codec_context));
	std::vector keys(std::from_range, in::state_keys());
	keys.emplace_back("notAKey"); // a key no sound element has
	for (const std::string_view key : keys) {
		for (const std::optional<int32_t> threshold : {std::optional<int32_t>{}, std::optional<int32_t>{12345}}) {
			text += " | ";
			text += key;
			text += " ";
			text += text_of(in::encode_entry(state, key, codec_context, threshold));
		}
	}
	serialization::Value element = serialization::Value::makeMap();
	element.set("name", serialization::Value::makeScalar("x"));
	element.set("compressor", serialization::Value::makeMap());
	element.set("osc2", serialization::Value::makeMap());
	element.set("defaultParams", serialization::Value::makeMap());
	text += " | with_state ";
	text += serialization::write_json(in::with_state(element, state, codec_context));
	return text;
}

inline std::string cables_text(const in::SoundState& state) {
	std::string bits;
	for (const bool drum : {false, true}) {
		for (uint8_t s = 0; s < static_cast<uint8_t>(in::PatchSource::NONE); ++s) {
			for (int32_t d = 0; d <= 255; ++d) {
				bits += in::cable_allowed(static_cast<in::PatchSource>(s), plugin::ParamId{static_cast<uint8_t>(d)},
				                          state, drum)
				            ? '1'
				            : '0';
			}
		}
	}
	return bits;
}

inline std::string text_of(std::span<const std::string_view> keys) {
	std::string text = "[";
	for (const std::string_view key : keys) {
		text += key;
		text += ' ';
	}
	return text + "]";
}

inline std::string text_of(const std::expected<in::SlotChange, plugin::StateError>& change) {
	if (!change) {
		return std::format("error {}", static_cast<int>(change.error()));
	}
	return text_of(change->state) + " changed " + text_of(std::span{change->changed});
}

/// What the codec reads back: a fresh read of the sound's encoding, and the encoding of the sound itself and of a new
/// synth read onto it.
inline std::string decode_text(const in::SoundState& state) {
	const plugin::CodecContext context{.input_tick_magnitude = 1};
	in::SoundState fresh;
	const auto read = in::decode(in::encode(state, context), fresh, context);
	std::string text = read ? text_of(fresh) : std::format("error {}", static_cast<int>(read.error()));
	for (const in::SoundState* from : {&state, static_cast<const in::SoundState*>(nullptr)}) {
		const in::SoundState blank;
		const auto onto = in::decode_onto(in::encode(from != nullptr ? *from : blank, context), state, context);
		text += " | onto ";
		text += onto ? text_of(*onto) : std::format("error {}", static_cast<int>(onto.error()));
	}
	return text;
}

/// Each settings row's write as the codec makes it, at its least and greatest candidate values.
inline std::string with_setting_text(const in::SoundState& state) {
	const plugin::CodecContext context{.input_tick_magnitude = 1};
	std::string text;
	for (const plugin::SettingInfo& row : in::kSettings) {
		if (row.kind == plugin::SettingKind::STRUCTURED || row.kind == plugin::SettingKind::TEXT) {
			continue;
		}
		const std::vector<std::string> candidates = candidates_of(row);
		for (const std::string& candidate : {candidates.front(), candidates.back()}) {
			const std::optional<serialization::Value> value =
			    in::canonical_setting(row.path, serialization::Value::makeScalar(candidate));
			if (!value) {
				continue;
			}
			text += std::format(" | {} {} ", row.path, candidate);
			text += text_of(in::with_setting(state, row.path, *value, context));
		}
	}
	return text;
}

/// The change to each engine and to each kind of the second slot, as the codec reads it from the entries that differ.
inline std::string change_to_text(const in::SoundState& state) {
	const plugin::CodecContext context{.input_tick_magnitude = 1};
	std::vector<in::SoundState> nexts;
	for (const in::SynthMode mode :
	     {in::SynthMode::SUBTRACTIVE, in::SynthMode::RINGMOD, in::SynthMode::FM, in::SynthMode::DX7}) {
		nexts.push_back(in::switch_engine(state, mode));
	}
	for (const in::OscType type :
	     {in::OscType::SAW, in::OscType::WAVETABLE, in::OscType::SAMPLE, in::OscType::INPUT_L}) {
		nexts.push_back(in::switch_source(state, in::SourceId::B, type, in::source_ranges(state, in::SourceId::B),
		                                  in::source_type(state, in::SourceId::B) == in::OscType::WAVETABLE));
	}
	std::string text;
	for (const in::SoundState& next : nexts) {
		const std::vector<std::string_view> changed = in::changed_entries(state, next);
		const std::vector<std::string_view> differing = in::differing_entries(state, next, context);
		text += " | changed " + text_of(std::span{changed}) + " differing " + text_of(std::span{differing}) + " ";
		text += text_of(in::change_to(state, next, changed, context));
	}
	return text;
}

} // namespace reference_detail

/// @brief Every family's answer for @p sound, in a fixed order: the sound itself, its readers, every engine switch,
/// every kind on each slot (with the slot's ranges, as a sample's and as a wavetable's), every loop mode on each slot,
/// the setup transforms, each settings row (read, held, taken, and each candidate's acceptance and result), the
/// encoding at three tick magnitudes, the cables it accepts, what the codec reads back (a fresh read and reads onto
/// it), each row's write as the codec makes it, the change to each engine and second-slot kind, and its dump.
[[nodiscard]] inline std::vector<ReferenceAnswer> reference_answers(const instruments::classic::SoundState& sound) {
	namespace in = deluge::instruments::classic;
	using reference_detail::text_of;
	std::vector<ReferenceAnswer> answers;
	answers.push_back({"sound", text_of(sound)});
	answers.push_back({"readers", reference_detail::readers_text(sound)});
	for (const in::SynthMode mode :
	     {in::SynthMode::SUBTRACTIVE, in::SynthMode::RINGMOD, in::SynthMode::FM, in::SynthMode::DX7}) {
		answers.push_back(
		    {std::format("switch_engine {}", std::to_underlying(mode)), text_of(in::switch_engine(sound, mode))});
	}
	for (const in::SourceId id : {in::SourceId::A, in::SourceId::B}) {
		const std::vector<synth::SourceRange>& ranges = in::source_ranges(sound, id);
		for (const in::OscType type : kMatrixKinds) {
			for (const bool wavetable : {false, true}) {
				answers.push_back({std::format("switch_source {} {} {}", in::slot_index(id), std::to_underlying(type),
				                               int{wavetable}),
				                   text_of(in::switch_source(sound, id, type, ranges, wavetable))});
			}
		}
		for (const in::SampleRepeatMode mode : {in::SampleRepeatMode::CUT, in::SampleRepeatMode::ONCE,
		                                        in::SampleRepeatMode::LOOP, in::SampleRepeatMode::STRETCH}) {
			answers.push_back({std::format("with_loop_mode {} {}", in::slot_index(id), std::to_underlying(mode)),
			                   text_of(in::with_loop_mode(sound, id, mode))});
		}
	}
	answers.push_back({"as_sample_sound", text_of(in::as_sample_sound(sound))});
	answers.push_back({"as_default_synth", text_of(in::as_default_synth(sound))});
	answers.push_back({"as_blank_synth 0", text_of(in::as_blank_synth(sound, false))});
	answers.push_back({"as_blank_synth 1", text_of(in::as_blank_synth(sound, true))});
	answers.push_back({"with_song_defaults", text_of(in::with_song_defaults(sound, effects::SongDefaults{}))});
	for (const plugin::SettingInfo& row : in::kSettings) {
		if (row.kind == plugin::SettingKind::STRUCTURED || row.kind == plugin::SettingKind::TEXT) {
			continue;
		}
		answers.push_back({std::format("setting {}", row.path), reference_detail::setting_text(sound, row)});
	}
	for (const int32_t magnitude : {0, 1, 2}) {
		answers.push_back({std::format("encode {}", magnitude), reference_detail::encode_text(sound, magnitude)});
	}
	answers.push_back({"cables", reference_detail::cables_text(sound)});
	answers.push_back({"decode", reference_detail::decode_text(sound)});
	answers.push_back({"with_setting", reference_detail::with_setting_text(sound)});
	answers.push_back({"change_to", reference_detail::change_to_text(sound)});
	answers.push_back({"dump_state", serialization::write_json(in::dump_state(sound))});
	return answers;
}

} // namespace deluge::test

#pragma pop_macro("context")
