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

#include "engines/deluge/codec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace deluge::instrument {

using serialization::Value;

namespace {

/// The top-level keys encode writes, in file order.
constexpr std::array<std::string_view, 17> kStateKeys{
    "polyphonic", "voicePriority", "sideChainSend", "mode",   "transpose", "cents",
    "maxVoices",  "osc1",          "osc2",          "lfo1",   "lfo2",      "lfo3",
    "lfo4",       "modulator1",    "modulator2",    "unison", "midiOutput"};

/// A number as a file holds it: a 32-bit signed integer, whatever the field's own type.
Value number(int64_t value) {
	return Value::makeNumber(static_cast<int32_t>(value));
}

Value text(std::string_view value) {
	return Value::makeScalar(std::string(value));
}

/// @p bytes as upper-case hex, two digits a byte.
std::string hex(std::span<const uint8_t> bytes) {
	constexpr std::string_view kDigits = "0123456789ABCDEF";
	std::string out;
	out.reserve(bytes.size() * 2);
	for (uint8_t b : bytes) {
		out.push_back(kDigits[b >> 4]);
		out.push_back(kDigits[b & 15]);
	}
	return out;
}

/// The sample positions of a range's zone.
Value zone(const RangeState& range) {
	auto map = Value::makeMap();
	map.set("startSamplePos", number(static_cast<int64_t>(range.start_pos)));
	map.set("endSamplePos", number(static_cast<int64_t>(range.end_pos)));
	if (range.loop_start_pos != 0) {
		map.set("startLoopPos", number(range.loop_start_pos));
	}
	if (range.loop_end_pos != 0) {
		map.set("endLoopPos", number(range.loop_end_pos));
	}
	return map;
}

/// A sample range's file, tuning and zone, into @p map.
void sampleRange(Value& map, const RangeState& range) {
	map.set("fileName", text(range.file_path));
	if (range.transpose != 0) {
		map.set("transpose", number(range.transpose));
	}
	if (range.cents != 0) {
		map.set("cents", number(range.cents));
	}
	map.set("zone", zone(range));
}

Value boxed(std::string_view name, Value value) {
	auto box = Value::makeMap();
	box.set(std::string(name), std::move(value));
	return box;
}

Value source(const SoundState& state, size_t s) {
	const SourceState& src = state.sources[s];
	const bool fm = state.synth_mode == SynthMode::FM;
	auto map = Value::makeMap();
	if (!fm) {
		map.set("type", text(to_string(src.osc_type)));
	}
	const auto numRanges = src.ranges.size();
	if (src.osc_type == OscType::SAMPLE && !fm) {
		map.set("loopMode", number(std::to_underlying(src.repeat_mode)));
		map.set("reversed", number(src.reversed ? 1 : 0));
		map.set("timeStretchEnable", number(src.pitch_and_speed_independent ? 1 : 0));
		map.set("timeStretchAmount", number(src.time_stretch_amount));
		if (src.interpolation_mode == engine::InterpolationMode::LINEAR) {
			map.set("linearInterpolation", number(1));
		}
		if (numRanges == 1) {
			sampleRange(map, src.ranges.front());
		}
		else if (numRanges > 1) {
			auto list = Value::makeArray();
			for (size_t e = 0; e < numRanges; e++) {
				auto range = Value::makeMap();
				if (e != numRanges - 1) {
					range.set("rangeTopNote", number(src.ranges[e].top_note));
				}
				sampleRange(range, src.ranges[e]);
				list.append(boxed("sampleRange", std::move(range)));
			}
			map.set("sampleRanges", std::move(list));
		}
		return map;
	}
	map.set("transpose", number(src.transpose));
	map.set("cents", number(src.cents));
	if (s == 1 && state.oscillator_sync) {
		map.set("oscillatorSync", number(1));
	}
	map.set("retrigPhase", number(state.osc_retrigger_phase[s]));
	if (src.osc_type == OscType::WAVETABLE && !fm) {
		if (numRanges == 1) {
			map.set("fileName", text(src.ranges.front().file_path));
		}
		else if (numRanges > 1) {
			auto list = Value::makeArray();
			for (size_t e = 0; e < numRanges; e++) {
				auto range = Value::makeMap();
				if (e != numRanges - 1) {
					range.set("rangeTopNote", number(src.ranges[e].top_note));
				}
				range.set("fileName", text(src.ranges[e].file_path));
				list.append(boxed("wavetableRange", std::move(range)));
			}
			map.set("wavetableRanges", std::move(list));
		}
	}
	else if (src.osc_type == OscType::DX7 && !fm && src.dx_patch) {
		map.set("dx7patch", text(hex(src.dx_patch->params)));
		if (src.dx_patch->engine_mode != 0) {
			map.set("dx7enginemode", number(src.dx_patch->engine_mode));
		}
		if (src.dx_patch->random_detune != 0) {
			map.set("dx7randomdetune", number(src.dx_patch->random_detune));
		}
	}
	return map;
}

Value lfo(const SoundState& state, size_t l, const engine::CodecContext& context) {
	auto map = Value::makeMap();
	map.set("type", text(to_string(state.lfo_wave_type[l])));
	map.set("syncLevel", number(engine::sync_level_to_file(engine::index_of(state.lfo_sync_level[l]),
	                                                       context.input_tick_magnitude)));
	map.set("syncType", number(std::to_underlying(state.lfo_sync_type[l])));
	return map;
}

Value modulator(const SoundState& state, size_t m) {
	auto map = Value::makeMap();
	map.set("transpose", number(state.modulator_transpose[m]));
	map.set("cents", number(state.modulator_cents[m]));
	map.set("retrigPhase", number(state.modulator_retrigger_phase[m]));
	if (m == 1) {
		map.set("toModulator1", number(state.modulator1_to_modulator0 ? 1 : 0));
	}
	return map;
}

void append(Value& to, const Value& from) {
	for (const auto& [key, value] : from.entries()) {
		to.set(key, value);
	}
}

} // namespace

SoundPieces encode_pieces(const SoundState& state, const engine::CodecContext& context) {
	SoundPieces pieces{.head = Value::makeMap(),
	                   .max_voices = Value::makeMap(),
	                   .body = Value::makeMap(),
	                   .midi_output = Value::makeMap()};
	pieces.head.set("polyphonic", text(to_string(state.polyphonic)));
	pieces.head.set("voicePriority", number(std::to_underlying(state.voice_priority)));
	if (state.side_chain_send_level != 0) {
		pieces.head.set("sideChainSend", number(state.side_chain_send_level));
	}
	pieces.head.set("mode", text(to_string(state.synth_mode)));
	if (state.transpose != 0) {
		pieces.head.set("transpose", number(state.transpose));
	}
	if (state.cents != 0) {
		pieces.head.set("cents", number(state.cents));
	}

	pieces.max_voices.set("maxVoices", number(state.max_voice_count));

	pieces.body.set("osc1", source(state, 0));
	pieces.body.set("osc2", source(state, 1));
	for (size_t l = 0; l < state.lfo_wave_type.size(); l++) {
		pieces.body.set("lfo" + std::to_string(l + 1), lfo(state, l, context));
	}
	if (state.synth_mode == SynthMode::FM) {
		pieces.body.set("modulator1", modulator(state, 0));
		pieces.body.set("modulator2", modulator(state, 1));
	}
	auto unison = Value::makeMap();
	unison.set("num", number(state.num_unison));
	unison.set("detune", number(state.unison_detune));
	unison.set("spread", number(state.unison_stereo_spread));
	pieces.body.set("unison", std::move(unison));

	auto midi = Value::makeMap();
	midi.set("channel", number(state.output_midi_channel));
	midi.set("noteForDrum", number(state.output_midi_note_for_drum));
	pieces.midi_output.set("midiOutput", std::move(midi));
	return pieces;
}

Value encode(const SoundState& state, const engine::CodecContext& context) {
	SoundPieces pieces = encode_pieces(state, context);
	auto map = Value::makeMap();
	append(map, pieces.head);
	append(map, pieces.max_voices);
	append(map, pieces.body);
	append(map, pieces.midi_output);
	return map;
}

bool is_state_key(std::string_view key) {
	return std::ranges::find(kStateKeys, key) != kStateKeys.end();
}

} // namespace deluge::instrument
